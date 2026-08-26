#include "laplace/unicode_root_builder.h"

#include "laplace/contract/unicode-source-manifest.h"
#include "laplace/identity.h"
#include "laplace/persistence.h"

#include "blake3.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view BuildReceiptDomain{
    "laplace-unicode-root-build-v2"};

bool DigestEqual(
    const laplace_digest256& left,
    const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool DigestIsZero(const laplace_digest256& value) {
    constexpr std::array<std::uint8_t, 32> Zero{};
    return std::memcmp(value.bytes, Zero.data(), Zero.size()) == 0;
}

void HashU32(blake3_hasher& hasher, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)}};
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher& hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashDigest(blake3_hasher& hasher, const laplace_digest256& value) {
    blake3_hasher_update(&hasher, value.bytes, sizeof(value.bytes));
}

std::uint32_t ReadU32(const std::uint8_t* const bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

bool ContractDigest(
    const char* const name,
    laplace_digest256* const digest) {
    if (name == nullptr || digest == nullptr) {
        return false;
    }
    for (std::size_t index = 0U;
         index < LAPLACE_UNICODE_GENERATED_CONTRACT_COUNT; ++index) {
        const auto& contract = laplace_unicode_generated_contracts[index];
        if (std::strcmp(contract.name, name) == 0) {
            std::memcpy(
                digest->bytes, contract.sha256, sizeof(digest->bytes));
            return true;
        }
    }
    return false;
}

struct SourceBundleDeleter {
    void operator()(laplace_unicode_source_bundle* value) const {
        laplace_unicode_source_bundle_close(&value);
    }
};

struct CoreTableDeleter {
    void operator()(laplace_unicode_core_table* value) const {
        laplace_unicode_core_table_destroy(&value);
    }
};

struct DucetTableDeleter {
    void operator()(laplace_unicode_ducet_table* value) const {
        laplace_unicode_ducet_table_destroy(&value);
    }
};

struct PlacementTableDeleter {
    void operator()(laplace_unicode_placement_table* value) const {
        laplace_unicode_placement_table_destroy(&value);
    }
};

struct ValidatorDeleter {
    void operator()(laplace_unicode_root_stream_validator* value) const {
        laplace_unicode_root_stream_validator_destroy(value);
    }
};

struct SpoolDeleter {
    void operator()(laplace_canonical_spool* value) const {
        laplace_canonical_spool_destroy(&value);
    }
};

using SourceBundle = std::unique_ptr<
    laplace_unicode_source_bundle, SourceBundleDeleter>;
using CoreTable = std::unique_ptr<laplace_unicode_core_table, CoreTableDeleter>;
using DucetTable = std::unique_ptr<
    laplace_unicode_ducet_table, DucetTableDeleter>;
using PlacementTable = std::unique_ptr<
    laplace_unicode_placement_table, PlacementTableDeleter>;
using Validator = std::unique_ptr<
    laplace_unicode_root_stream_validator, ValidatorDeleter>;
using CanonicalSpool = std::unique_ptr<laplace_canonical_spool, SpoolDeleter>;

void MarkBuildFailure(
    laplace_unicode_root_build_summary* const summary,
    const laplace_unicode_root_build_status status,
    const laplace_unicode_root_build_stage stage,
    const laplace_unicode_status unicode_status,
    const laplace_spool_status spool_status) {
    summary->status = static_cast<std::uint32_t>(status);
    summary->stage = static_cast<std::uint32_t>(stage);
    summary->unicode_status = static_cast<std::uint32_t>(unicode_status);
    summary->spool_status = static_cast<std::uint32_t>(spool_status);
}

class CanonicalStreamWriter {
public:
    CanonicalStreamWriter(
        laplace_canonical_spool* const spool,
        laplace_unicode_root_stream_validator* const validator,
        const std::uint32_t maximum_frames,
        const std::size_t maximum_bytes)
        : spool_(spool),
          validator_(validator),
          maximum_frames_(maximum_frames),
          maximum_bytes_(maximum_bytes) {}

    bool Append(
        const std::uint16_t kind,
        const std::uint64_t section_ordinal,
        const std::uint8_t* const payload,
        const std::size_t payload_bytes) {
        if (payload == nullptr || payload_bytes == 0U ||
            payload_bytes > std::numeric_limits<std::uint32_t>::max()) {
            unicode_status_ = LAPLACE_UNICODE_RECORD_INVALID;
            return false;
        }
        const laplace_unicode_root_frame frame{
            payload,
            section_ordinal,
            static_cast<std::uint32_t>(payload_bytes),
            kind,
            0U};
        std::size_t frame_bytes = 0U;
        unicode_status_ = laplace_unicode_root_frame_measure(
            &frame, &frame_bytes);
        if (unicode_status_ != LAPLACE_UNICODE_OK) {
            return false;
        }
        if (frame_bytes > maximum_bytes_) {
            unicode_status_ = LAPLACE_UNICODE_SIZE_OVERFLOW;
            return false;
        }
        if (batch_frames_ != 0U &&
            (batch_frames_ >= maximum_frames_ ||
             frame_bytes > maximum_bytes_ - batch_bytes_.size()) &&
            !Flush()) {
            return false;
        }
        const std::size_t offset = batch_bytes_.size();
        batch_bytes_.resize(offset + frame_bytes);
        std::size_t encoded_bytes = 0U;
        unicode_status_ = laplace_unicode_root_frame_encode(
            &frame, batch_bytes_.data() + offset, frame_bytes,
            &encoded_bytes);
        if (unicode_status_ != LAPLACE_UNICODE_OK ||
            encoded_bytes != frame_bytes) {
            if (unicode_status_ == LAPLACE_UNICODE_OK) {
                unicode_status_ = LAPLACE_UNICODE_STREAM_STATE_INVALID;
            }
            return false;
        }
        ++batch_frames_;
        return true;
    }

    bool Flush() {
        if (batch_frames_ == 0U) {
            return true;
        }
        const laplace_framework_canonical_batch batch{
            batch_bytes_.data(),
            static_cast<std::uint64_t>(batch_bytes_.size()),
            batch_frames_,
            next_frame_ordinal_,
            LAPLACE_UNICODE_ROOT_STREAM_RECORD_TYPE,
            LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS};
        unicode_status_ = laplace_unicode_root_stream_validator_consume(
            validator_, batch.canonical_bytes,
            static_cast<std::size_t>(batch.byte_count), batch.record_count,
            batch.first_ordinal);
        if (unicode_status_ != LAPLACE_UNICODE_OK) {
            return false;
        }
        spool_status_ = laplace_canonical_spool_append(spool_, &batch);
        if (spool_status_ != LAPLACE_SPOOL_OK) {
            return false;
        }
        next_frame_ordinal_ += batch_frames_;
        batch_frames_ = 0U;
        batch_bytes_.clear();
        return true;
    }

    laplace_unicode_status UnicodeStatus() const {
        return unicode_status_;
    }

    laplace_spool_status SpoolStatus() const {
        return spool_status_;
    }

private:
    laplace_canonical_spool* spool_;
    laplace_unicode_root_stream_validator* validator_;
    std::uint32_t maximum_frames_;
    std::size_t maximum_bytes_;
    std::vector<std::uint8_t> batch_bytes_;
    std::uint64_t next_frame_ordinal_ = 0U;
    std::uint64_t batch_frames_ = 0U;
    laplace_unicode_status unicode_status_ = LAPLACE_UNICODE_OK;
    laplace_spool_status spool_status_ = LAPLACE_SPOOL_OK;
};

laplace_unicode_status EncodeAtom(
    const laplace_unicode_atom_record& record,
    std::vector<std::uint8_t>* const output) {
    std::size_t bytes = 0U;
    laplace_unicode_status status = laplace_unicode_atom_record_measure(
        &record, &bytes);
    if (status != LAPLACE_UNICODE_OK) {
        return status;
    }
    output->resize(bytes);
    status = laplace_unicode_atom_record_encode(
        &record, output->data(), output->size(), &bytes);
    return status == LAPLACE_UNICODE_OK && bytes == output->size()
        ? LAPLACE_UNICODE_OK
        : status == LAPLACE_UNICODE_OK
            ? LAPLACE_UNICODE_STREAM_STATE_INVALID
            : status;
}

laplace_unicode_status EncodeDucetPosition(
    const laplace_unicode_ducet_position_record& record,
    std::vector<std::uint8_t>* const output) {
    std::size_t bytes = 0U;
    laplace_unicode_status status = laplace_unicode_ducet_position_measure(
        &record, &bytes);
    if (status != LAPLACE_UNICODE_OK) {
        return status;
    }
    output->resize(bytes);
    status = laplace_unicode_ducet_position_encode(
        &record, output->data(), output->size(), &bytes);
    return status == LAPLACE_UNICODE_OK && bytes == output->size()
        ? LAPLACE_UNICODE_OK
        : status == LAPLACE_UNICODE_OK
            ? LAPLACE_UNICODE_STREAM_STATE_INVALID
            : status;
}

laplace_unicode_status EncodeDucetContraction(
    const laplace_unicode_ducet_contraction_record& record,
    std::vector<std::uint8_t>* const output) {
    std::size_t bytes = 0U;
    laplace_unicode_status status = laplace_unicode_ducet_contraction_measure(
        &record, &bytes);
    if (status != LAPLACE_UNICODE_OK) {
        return status;
    }
    output->resize(bytes);
    status = laplace_unicode_ducet_contraction_encode(
        &record, output->data(), output->size(), &bytes);
    return status == LAPLACE_UNICODE_OK && bytes == output->size()
        ? LAPLACE_UNICODE_OK
        : status == LAPLACE_UNICODE_OK
            ? LAPLACE_UNICODE_STREAM_STATE_INVALID
            : status;
}

bool ContractionLess(
    const laplace_unicode_ducet_mapping_view& left,
    const laplace_unicode_ducet_mapping_view& right) {
    const std::uint32_t shared = std::min(
        left.sequence_count, right.sequence_count);
    for (std::uint32_t index = 0U; index < shared; ++index) {
        if (left.sequence[index] != right.sequence[index]) {
            return left.sequence[index] < right.sequence[index];
        }
    }
    return left.sequence_count < right.sequence_count;
}

bool CompositionLess(
    const laplace_unicode_normalization_composition& left,
    const laplace_unicode_normalization_composition& right) {
    if (left.starter_position != right.starter_position) {
        return left.starter_position < right.starter_position;
    }
    if (left.combining_position != right.combining_position) {
        return left.combining_position < right.combining_position;
    }
    return left.composite_position < right.composite_position;
}

void CalculateBuildReceipt(laplace_unicode_root_build_summary* const summary) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, BuildReceiptDomain.data(), BuildReceiptDomain.size());
    HashDigest(hasher, summary->source.receipt_id);
    HashDigest(hasher, summary->core.receipt_id);
    HashDigest(hasher, summary->ducet.receipt_id);
    HashDigest(hasher, summary->placement.receipt_id);
    HashDigest(hasher, summary->numeric.receipt_id);
    HashDigest(hasher, summary->stream.receipt_id);
    HashDigest(hasher, summary->spool.spool_fingerprint);
    HashU64(hasher, summary->maximum_batch_bytes);
    HashU32(hasher, summary->maximum_batch_frames);
    HashU32(
        hasher,
        static_cast<std::uint32_t>(summary->abi_major) |
            (static_cast<std::uint32_t>(summary->abi_minor) << 16U));
    blake3_hasher_finalize(
        &hasher, summary->receipt_id.bytes, sizeof(summary->receipt_id.bytes));
}

}  // namespace

extern "C" laplace_unicode_root_build_status
laplace_unicode_root_build_canonical_spool(
    const laplace_unicode_root_build_request* const request,
    laplace_canonical_spool** const output_spool,
    laplace_unicode_root_build_summary* const summary) {
    if (output_spool != nullptr) {
        *output_spool = nullptr;
    }
    if (summary != nullptr) {
        *summary = laplace_unicode_root_build_summary{};
    }
    if (request == nullptr || output_spool == nullptr || summary == nullptr ||
        request->source_root == nullptr || request->source_root[0] == '\0' ||
        request->spool_directory == nullptr ||
        request->spool_directory[0] == '\0' ||
        request->numeric_provider == nullptr ||
        request->maximum_batch_frames == 0U ||
        request->maximum_batch_bytes <
            LAPLACE_UNICODE_ROOT_FRAME_HEADER_BYTES ||
        request->maximum_batch_bytes >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()) ||
        request->abi_major != LAPLACE_UNICODE_ROOT_BUILDER_ABI_MAJOR ||
        request->abi_minor > LAPLACE_UNICODE_ROOT_BUILDER_ABI_MINOR ||
        request->flags != 0U || request->reserved != 0U) {
        if (summary != nullptr) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_INVALID_ARGUMENT,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_NONE,
                LAPLACE_UNICODE_INVALID_ARGUMENT, LAPLACE_SPOOL_OK);
        }
        return LAPLACE_UNICODE_ROOT_BUILD_INVALID_ARGUMENT;
    }
    summary->maximum_batch_bytes = request->maximum_batch_bytes;
    summary->maximum_batch_frames = request->maximum_batch_frames;
    summary->abi_major = request->abi_major;
    summary->abi_minor = request->abi_minor;
    const auto* const provider = request->numeric_provider;
    if (provider->workspace == nullptr || provider->calculate == nullptr ||
        provider->abi_major != LAPLACE_UNICODE_NUMERIC_PROVIDER_ABI_MAJOR ||
        provider->abi_minor > LAPLACE_UNICODE_NUMERIC_PROVIDER_ABI_MINOR ||
        provider->flags != 0U || provider->reserved != 0U ||
        DigestIsZero(provider->provider_fingerprint)) {
        MarkBuildFailure(
            summary, LAPLACE_UNICODE_ROOT_BUILD_INVALID_ARGUMENT,
            LAPLACE_UNICODE_ROOT_BUILD_STAGE_NUMERIC,
            LAPLACE_UNICODE_INVALID_ARGUMENT, LAPLACE_SPOOL_OK);
        return LAPLACE_UNICODE_ROOT_BUILD_INVALID_ARGUMENT;
    }

    try {
        laplace_unicode_source_bundle* raw_bundle = nullptr;
        summary->stage = LAPLACE_UNICODE_ROOT_BUILD_STAGE_SOURCE;
        laplace_unicode_status unicode_status =
            laplace_unicode_source_bundle_open(
                request->source_root, &raw_bundle, &summary->source);
        SourceBundle bundle(raw_bundle);
        if (unicode_status != LAPLACE_UNICODE_OK) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_SOURCE, unicode_status,
                LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
        }

        laplace_unicode_core_table* raw_core = nullptr;
        summary->stage = LAPLACE_UNICODE_ROOT_BUILD_STAGE_CORE;
        unicode_status = laplace_unicode_core_table_create(
            bundle.get(), &raw_core, &summary->core);
        CoreTable core(raw_core);
        if (unicode_status != LAPLACE_UNICODE_OK) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_CORE, unicode_status,
                LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
        }

        laplace_unicode_ducet_table* raw_ducet = nullptr;
        summary->stage = LAPLACE_UNICODE_ROOT_BUILD_STAGE_DUCET;
        unicode_status = laplace_unicode_ducet_table_create(
            bundle.get(), &raw_ducet, &summary->ducet);
        DucetTable ducet(raw_ducet);
        if (unicode_status != LAPLACE_UNICODE_OK) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_DUCET, unicode_status,
                LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
        }

        laplace_unicode_placement_table* raw_placement = nullptr;
        summary->stage = LAPLACE_UNICODE_ROOT_BUILD_STAGE_PLACEMENT;
        unicode_status = laplace_unicode_placement_table_create(
            ducet.get(), core.get(), &raw_placement, &summary->placement);
        PlacementTable placement(raw_placement);
        if (unicode_status != LAPLACE_UNICODE_OK) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_PLACEMENT, unicode_status,
                LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
        }

        summary->stage = LAPLACE_UNICODE_ROOT_BUILD_STAGE_NUMERIC;
        std::size_t workspace_bytes = 0U;
        unicode_status = provider->workspace(
            provider->state, LAPLACE_UNICODE_ROOT_POPULATION,
            &workspace_bytes);
        if (unicode_status != LAPLACE_UNICODE_OK || workspace_bytes == 0U) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_NUMERIC,
                unicode_status == LAPLACE_UNICODE_OK
                    ? LAPLACE_UNICODE_PROVIDER_FAILURE
                    : unicode_status,
                LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
        }
        std::vector<std::uint8_t> workspace(workspace_bytes);
        std::vector<laplace_point4d> coordinates(
            LAPLACE_UNICODE_ROOT_POPULATION);
        std::vector<laplace_unicode_hopf_point> hopf_points(
            LAPLACE_UNICODE_ROOT_POPULATION);
        unicode_status = provider->calculate(
            provider->state, 0U, LAPLACE_UNICODE_ROOT_POPULATION,
            workspace.data(), workspace.size(), coordinates.data(),
            hopf_points.data(), &summary->numeric);
        if (unicode_status != LAPLACE_UNICODE_OK ||
            summary->numeric.status != LAPLACE_UNICODE_OK ||
            summary->numeric.first_rank != 0U ||
            summary->numeric.rank_count != LAPLACE_UNICODE_ROOT_POPULATION ||
            !DigestEqual(
                summary->numeric.provider_fingerprint,
                provider->provider_fingerprint) ||
            DigestIsZero(summary->numeric.receipt_id)) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_NUMERIC,
                unicode_status == LAPLACE_UNICODE_OK
                    ? LAPLACE_UNICODE_PROVIDER_FAILURE
                    : unicode_status,
                LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
        }

        laplace_unicode_root_stream_expectation expectation{};
        expectation.source_fingerprint = summary->source.source_fingerprint;
        expectation.recipe_fingerprint = summary->source.recipe_fingerprint;
        expectation.numeric_provider_receipt = summary->numeric.receipt_id;
        if (!ContractDigest(
                "unicode-root-stream.json",
                &expectation.stream_contract_fingerprint) ||
            !ContractDigest(
                "ducet-totalization.json",
                &expectation.algorithmic_hangul_rule_fingerprint) ||
            !ContractDigest(
                "unicode-atom-record.json",
                &expectation.atom_record_contract_fingerprint) ||
            !ContractDigest(
                "unicode-atomic-physicality.json",
                &expectation.physicality_recipe_fingerprint)) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_MANIFEST,
                LAPLACE_UNICODE_STREAM_STATE_INVALID, LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE;
        }
        expectation.physicality_recipe_version =
            LAPLACE_UNICODE_ATOMIC_PHYSICALITY_RECIPE_VERSION;
        expectation.placement_rank_permutation_fingerprint =
            summary->placement.rank_permutation_fingerprint;
        std::vector<std::uint32_t> placement_ranks(
            LAPLACE_UNICODE_ROOT_POPULATION);
        for (std::uint32_t position = 0U;
             position < LAPLACE_UNICODE_ROOT_POPULATION; ++position) {
            laplace_unicode_placement_position_view placement_record{};
            unicode_status = laplace_unicode_placement_table_position(
                placement.get(), position, &placement_record);
            if (unicode_status != LAPLACE_UNICODE_OK ||
                placement_record.codepoint_position != position) {
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_NUMERIC,
                    unicode_status == LAPLACE_UNICODE_OK
                        ? LAPLACE_UNICODE_STREAM_STATE_INVALID
                        : unicode_status,
                    LAPLACE_SPOOL_OK);
                return LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE;
            }
            placement_ranks[position] = placement_record.placement_rank;
        }
        unicode_status = laplace_unicode_coordinate_table_identify(
            placement_ranks.data(), coordinates.data(),
            LAPLACE_UNICODE_ROOT_POPULATION,
            &expectation.coordinate_table_fingerprint);
        if (unicode_status != LAPLACE_UNICODE_OK) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_NUMERIC, unicode_status,
                LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
        }
        unicode_status = laplace_unicode_geometry_epoch_identify(
            &expectation.physicality_recipe_fingerprint,
            &expectation.placement_rank_permutation_fingerprint,
            &expectation.coordinate_table_fingerprint,
            &expectation.geometry_epoch);
        if (unicode_status != LAPLACE_UNICODE_OK) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_NUMERIC, unicode_status,
                LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
        }

        laplace_canonical_spool* raw_spool = nullptr;
        laplace_spool_status spool_status = laplace_canonical_spool_create(
            request->spool_directory,
            LAPLACE_UNICODE_ROOT_STREAM_RECORD_TYPE,
            &summary->source.source_fingerprint,
            &summary->source.recipe_fingerprint, &raw_spool);
        CanonicalSpool spool(raw_spool);
        if (spool_status != LAPLACE_SPOOL_OK) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_ATOMS,
                LAPLACE_UNICODE_OK, spool_status);
            return LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE;
        }
        laplace_unicode_root_stream_validator* raw_validator = nullptr;
        unicode_status = laplace_unicode_root_stream_validator_create(
            &expectation, &raw_validator);
        Validator validator(raw_validator);
        if (unicode_status != LAPLACE_UNICODE_OK) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_ATOMS, unicode_status,
                LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
        }
        CanonicalStreamWriter writer(
            spool.get(), validator.get(), request->maximum_batch_frames,
            static_cast<std::size_t>(request->maximum_batch_bytes));
        std::vector<std::uint8_t> payload;

        summary->stage = LAPLACE_UNICODE_ROOT_BUILD_STAGE_ATOMS;
        for (std::uint32_t position = 0U;
             position < LAPLACE_UNICODE_ROOT_POPULATION; ++position) {
            laplace_unicode_core_record_view core_record{};
            unicode_status = laplace_unicode_core_table_record(
                core.get(), position, &core_record);
            if (unicode_status != LAPLACE_UNICODE_OK) {
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_ATOMS, unicode_status,
                    LAPLACE_SPOOL_OK);
                return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
            }
            if (core_record.codepoint_position != position) {
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_ATOMS,
                    LAPLACE_UNICODE_STREAM_STATE_INVALID,
                    LAPLACE_SPOOL_OK);
                return LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE;
            }
            laplace_unicode_atom_record atom{};
            atom.codepoint_position = position;
            atom.placement_rank = placement_ranks[position];
            atom.position_class = core_record.position_class;
            std::size_t lup_length = 0U;
            if (laplace_unicode_position_encode(
                    position, atom.lup_v1_bytes, &lup_length) !=
                    LAPLACE_IDENTITY_OK ||
                lup_length > sizeof(atom.lup_v1_bytes) ||
                laplace_identity_codepoint_witness(
                    position, &atom.content_id,
                    &atom.identity_preimage_fingerprint) !=
                    LAPLACE_IDENTITY_OK) {
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_ATOMS,
                    LAPLACE_UNICODE_IDENTITY_MISMATCH,
                    LAPLACE_SPOOL_OK);
                return LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE;
            }
            atom.lup_v1_length = static_cast<std::uint8_t>(lup_length);
#if defined(LAPLACE_TEST_UNICODE_ROOT_BUILDER_USE_POSITION_AS_NUMERIC_RANK)
            atom.coordinate = coordinates[position];
#else
            atom.coordinate = coordinates[atom.placement_rank];
#endif
            std::array<std::uint32_t, 4> axes{};
            for (std::size_t axis = 0U; axis < axes.size(); ++axis) {
                unicode_status = laplace_unicode_quantize_component_u32(
                    atom.coordinate.component[axis], &axes[axis]);
                if (unicode_status != LAPLACE_UNICODE_OK) {
                    MarkBuildFailure(
                        summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                        LAPLACE_UNICODE_ROOT_BUILD_STAGE_ATOMS,
                        unicode_status, LAPLACE_SPOOL_OK);
                    return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
                }
            }
            unicode_status = laplace_unicode_hilbert4_encode(
                axes.data(), atom.hilbert_key);
            if (unicode_status != LAPLACE_UNICODE_OK) {
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_ATOMS,
                    unicode_status, LAPLACE_SPOOL_OK);
                return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
            }
            atom.geometry_epoch = expectation.geometry_epoch;
            laplace_persistence_physicality_record physicality{};
            if (laplace_persistence_atomic_point_physicality(
                    &atom.content_id,
                    expectation.physicality_recipe_version,
                    &expectation.physicality_recipe_fingerprint,
                    &expectation.geometry_epoch, &atom.coordinate,
                    &physicality) != LAPLACE_PERSISTENCE_OK) {
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_ATOMS,
                    LAPLACE_UNICODE_IDENTITY_MISMATCH,
                    LAPLACE_SPOOL_OK);
                return LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE;
            }
            atom.physicality_id = physicality.physicality_id;
            for (std::size_t field = 0U;
                 field < LAPLACE_UNICODE_ATOM_FIELD_COUNT; ++field) {
                atom.fields[field] = core_record.fields[field];
            }
            unicode_status = EncodeAtom(atom, &payload);
            if (unicode_status != LAPLACE_UNICODE_OK ||
                !writer.Append(
                    LAPLACE_UNICODE_ROOT_FRAME_ATOM, position,
                    payload.data(), payload.size())) {
                const laplace_unicode_status writer_status =
                    unicode_status == LAPLACE_UNICODE_OK
                    ? writer.UnicodeStatus()
                    : unicode_status;
                if (writer.SpoolStatus() != LAPLACE_SPOOL_OK) {
                    MarkBuildFailure(
                        summary, LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE,
                        LAPLACE_UNICODE_ROOT_BUILD_STAGE_ATOMS,
                        writer_status, writer.SpoolStatus());
                    return LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE;
                }
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_ATOMS, writer_status,
                    LAPLACE_SPOOL_OK);
                return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
            }
        }

        summary->stage = LAPLACE_UNICODE_ROOT_BUILD_STAGE_DUCET_POSITIONS;
        for (std::uint32_t position = 0U;
             position < LAPLACE_UNICODE_ROOT_POPULATION; ++position) {
            laplace_unicode_placement_position_view value{};
            unicode_status = laplace_unicode_placement_table_position(
                placement.get(), position, &value);
            if (unicode_status != LAPLACE_UNICODE_OK) {
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_DUCET_POSITIONS,
                    unicode_status, LAPLACE_SPOOL_OK);
                return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
            }
            const laplace_unicode_ducet_position_record record{
                value.elements,
                value.equivalence_key,
                value.codepoint_position,
                value.element_count,
                value.equivalence_key_bytes,
                value.provenance,
                {0U, 0U, 0U}};
            unicode_status = EncodeDucetPosition(record, &payload);
            if (unicode_status != LAPLACE_UNICODE_OK ||
                !writer.Append(
                    LAPLACE_UNICODE_ROOT_FRAME_DUCET_POSITION, position,
                    payload.data(), payload.size())) {
                const laplace_unicode_status writer_status =
                    unicode_status == LAPLACE_UNICODE_OK
                    ? writer.UnicodeStatus()
                    : unicode_status;
                if (writer.SpoolStatus() != LAPLACE_SPOOL_OK) {
                    MarkBuildFailure(
                        summary, LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE,
                        LAPLACE_UNICODE_ROOT_BUILD_STAGE_DUCET_POSITIONS,
                        writer_status, writer.SpoolStatus());
                    return LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE;
                }
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_DUCET_POSITIONS,
                    writer_status, LAPLACE_SPOOL_OK);
                return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
            }
        }

        summary->stage = LAPLACE_UNICODE_ROOT_BUILD_STAGE_DUCET_CONTRACTIONS;
        std::vector<laplace_unicode_ducet_mapping_view> contractions;
        contractions.reserve(
            static_cast<std::size_t>(summary->ducet.contraction_count));
        for (std::uint64_t ordinal = 0U;
             ordinal < summary->ducet.explicit_mapping_count; ++ordinal) {
            laplace_unicode_ducet_mapping_view mapping{};
            unicode_status = laplace_unicode_ducet_table_mapping(
                ducet.get(), ordinal, &mapping);
            if (unicode_status != LAPLACE_UNICODE_OK) {
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_DUCET_CONTRACTIONS,
                    unicode_status, LAPLACE_SPOOL_OK);
                return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
            }
            if (mapping.sequence_count > 1U) {
                contractions.push_back(mapping);
            }
        }
        if (contractions.size() != summary->ducet.contraction_count) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_DUCET_CONTRACTIONS,
                LAPLACE_UNICODE_SOURCE_INCOMPLETE, LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE;
        }
        std::sort(
            contractions.begin(), contractions.end(), ContractionLess);
        for (std::size_t index = 0U; index < contractions.size(); ++index) {
            const auto& value = contractions[index];
            const laplace_unicode_ducet_contraction_record record{
                value.sequence, value.elements, value.source_line_ordinal,
                value.sequence_count, value.element_count};
            unicode_status = EncodeDucetContraction(record, &payload);
            if (unicode_status != LAPLACE_UNICODE_OK ||
                !writer.Append(
                    LAPLACE_UNICODE_ROOT_FRAME_DUCET_CONTRACTION,
                    static_cast<std::uint64_t>(index), payload.data(),
                    payload.size())) {
                const laplace_unicode_status writer_status =
                    unicode_status == LAPLACE_UNICODE_OK
                    ? writer.UnicodeStatus()
                    : unicode_status;
                if (writer.SpoolStatus() != LAPLACE_SPOOL_OK) {
                    MarkBuildFailure(
                        summary, LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE,
                        LAPLACE_UNICODE_ROOT_BUILD_STAGE_DUCET_CONTRACTIONS,
                        writer_status, writer.SpoolStatus());
                    return LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE;
                }
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_DUCET_CONTRACTIONS,
                    writer_status, LAPLACE_SPOOL_OK);
                return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
            }
        }

        summary->stage =
            LAPLACE_UNICODE_ROOT_BUILD_STAGE_NORMALIZATION_COMPOSITIONS;
        std::vector<laplace_unicode_normalization_composition> compositions;
        compositions.reserve(1024U);
        for (std::uint32_t position = 0U;
             position < LAPLACE_UNICODE_ROOT_POPULATION; ++position) {
            laplace_unicode_core_record_view value{};
            unicode_status = laplace_unicode_core_table_record(
                core.get(), position, &value);
            if (unicode_status != LAPLACE_UNICODE_OK) {
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_NORMALIZATION_COMPOSITIONS,
                    unicode_status, LAPLACE_SPOOL_OK);
                return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
            }
            const auto& decomposition = value.fields[5];
            const auto& exclusion = value.fields[18];
            if (decomposition.payload_bytes == 8U &&
                exclusion.payload_bytes == 1U &&
                exclusion.payload[0] == 0U) {
                compositions.push_back(
                    laplace_unicode_normalization_composition{
                        ReadU32(decomposition.payload),
                        ReadU32(decomposition.payload + 4U),
                        position});
            }
        }
        std::sort(compositions.begin(), compositions.end(), CompositionLess);
        for (std::size_t index = 1U; index < compositions.size(); ++index) {
            if (compositions[index - 1U].starter_position ==
                    compositions[index].starter_position &&
                compositions[index - 1U].combining_position ==
                    compositions[index].combining_position) {
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_NORMALIZATION_COMPOSITIONS,
                    LAPLACE_UNICODE_SOURCE_CONFLICT, LAPLACE_SPOOL_OK);
                return LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE;
            }
        }
        std::array<
            std::uint8_t, LAPLACE_UNICODE_NORMALIZATION_COMPOSITION_BYTES>
            composition_bytes{};
        for (std::size_t index = 0U; index < compositions.size(); ++index) {
            unicode_status = laplace_unicode_normalization_composition_encode(
                &compositions[index], composition_bytes.data());
            if (unicode_status != LAPLACE_UNICODE_OK ||
                !writer.Append(
                    LAPLACE_UNICODE_ROOT_FRAME_NORMALIZATION_COMPOSITION,
                    static_cast<std::uint64_t>(index),
                    composition_bytes.data(), composition_bytes.size())) {
                const laplace_unicode_status writer_status =
                    unicode_status == LAPLACE_UNICODE_OK
                    ? writer.UnicodeStatus()
                    : unicode_status;
                if (writer.SpoolStatus() != LAPLACE_SPOOL_OK) {
                    MarkBuildFailure(
                        summary, LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE,
                        LAPLACE_UNICODE_ROOT_BUILD_STAGE_NORMALIZATION_COMPOSITIONS,
                        writer_status, writer.SpoolStatus());
                    return LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE;
                }
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_NORMALIZATION_COMPOSITIONS,
                    writer_status, LAPLACE_SPOOL_OK);
                return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
            }
        }
        if (!writer.Flush()) {
            if (writer.SpoolStatus() != LAPLACE_SPOOL_OK) {
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_NORMALIZATION_COMPOSITIONS,
                    writer.UnicodeStatus(), writer.SpoolStatus());
                return LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE;
            }
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_NORMALIZATION_COMPOSITIONS,
                writer.UnicodeStatus(), LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
        }

        summary->stage = LAPLACE_UNICODE_ROOT_BUILD_STAGE_MANIFEST;
        laplace_unicode_root_stream_section_snapshot snapshot{};
        unicode_status =
            laplace_unicode_root_stream_validator_seal_sections(
                validator.get(), &snapshot);
        if (unicode_status != LAPLACE_UNICODE_OK ||
            snapshot.manifest_seen != 0U || snapshot.finished != 0U ||
            snapshot.section_counts[0] != LAPLACE_UNICODE_ROOT_POPULATION ||
            snapshot.section_counts[1] != LAPLACE_UNICODE_ROOT_POPULATION ||
            snapshot.section_counts[2] != contractions.size() ||
            snapshot.section_counts[3] != compositions.size() ||
            snapshot.total_frame_count ==
                std::numeric_limits<std::uint64_t>::max()) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_MANIFEST,
                unicode_status == LAPLACE_UNICODE_OK
                    ? LAPLACE_UNICODE_STREAM_STATE_INVALID
                    : unicode_status,
                LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE;
        }
        laplace_unicode_root_manifest manifest{};
        manifest.atom_count = snapshot.section_counts[0];
        manifest.ducet_position_count = snapshot.section_counts[1];
        manifest.ducet_contraction_count = snapshot.section_counts[2];
        manifest.normalization_composition_count = snapshot.section_counts[3];
        manifest.total_frame_count = snapshot.total_frame_count + 1U;
        manifest.source_fingerprint = summary->source.source_fingerprint;
        manifest.recipe_fingerprint = summary->source.recipe_fingerprint;
        manifest.numeric_provider_receipt = summary->numeric.receipt_id;
        manifest.stream_contract_fingerprint =
            expectation.stream_contract_fingerprint;
        manifest.atom_section_fingerprint = snapshot.section_fingerprints[0];
        manifest.ducet_position_section_fingerprint =
            snapshot.section_fingerprints[1];
        manifest.ducet_contraction_section_fingerprint =
            snapshot.section_fingerprints[2];
        manifest.normalization_composition_section_fingerprint =
            snapshot.section_fingerprints[3];
        manifest.algorithmic_hangul_rule_fingerprint =
            expectation.algorithmic_hangul_rule_fingerprint;
        manifest.atom_record_contract_fingerprint =
            expectation.atom_record_contract_fingerprint;
        manifest.physicality_recipe_version =
            expectation.physicality_recipe_version;
        manifest.physicality_recipe_fingerprint =
            expectation.physicality_recipe_fingerprint;
        manifest.placement_rank_permutation_fingerprint =
            expectation.placement_rank_permutation_fingerprint;
        manifest.coordinate_table_fingerprint =
            expectation.coordinate_table_fingerprint;
        manifest.geometry_epoch = expectation.geometry_epoch;
        std::array<std::uint8_t, LAPLACE_UNICODE_ROOT_MANIFEST_BYTES>
            manifest_bytes{};
        unicode_status = laplace_unicode_root_manifest_encode(
            &manifest, manifest_bytes.data());
        if (unicode_status != LAPLACE_UNICODE_OK ||
            !writer.Append(
                LAPLACE_UNICODE_ROOT_FRAME_MANIFEST, 0U,
                manifest_bytes.data(), manifest_bytes.size()) ||
            !writer.Flush()) {
            const laplace_unicode_status writer_status =
                unicode_status == LAPLACE_UNICODE_OK
                ? writer.UnicodeStatus()
                : unicode_status;
            if (writer.SpoolStatus() != LAPLACE_SPOOL_OK) {
                MarkBuildFailure(
                    summary, LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE,
                    LAPLACE_UNICODE_ROOT_BUILD_STAGE_MANIFEST,
                    writer_status, writer.SpoolStatus());
                return LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE;
            }
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_MANIFEST, writer_status,
                LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
        }

        summary->stage = LAPLACE_UNICODE_ROOT_BUILD_STAGE_VALIDATION;
        unicode_status = laplace_unicode_root_stream_validator_finish(
            validator.get(), &summary->stream);
        if (unicode_status != LAPLACE_UNICODE_OK) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_VALIDATION, unicode_status,
                LAPLACE_SPOOL_OK);
            return LAPLACE_UNICODE_ROOT_BUILD_UNICODE_FAILURE;
        }

        summary->stage = LAPLACE_UNICODE_ROOT_BUILD_STAGE_SPOOL_SEAL;
        spool_status = laplace_canonical_spool_seal(
            spool.get(), &summary->spool);
        if (spool_status != LAPLACE_SPOOL_OK) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_SPOOL_SEAL,
                LAPLACE_UNICODE_OK, spool_status);
            return LAPLACE_UNICODE_ROOT_BUILD_SPOOL_FAILURE;
        }
        if (summary->spool.total_records !=
                summary->stream.total_frame_count ||
            summary->spool.total_bytes != summary->stream.total_encoded_bytes ||
            summary->spool.record_type !=
                LAPLACE_UNICODE_ROOT_STREAM_RECORD_TYPE) {
            MarkBuildFailure(
                summary, LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE,
                LAPLACE_UNICODE_ROOT_BUILD_STAGE_SPOOL_SEAL,
                LAPLACE_UNICODE_STREAM_STATE_INVALID,
                LAPLACE_SPOOL_BATCH_INVALID);
            return LAPLACE_UNICODE_ROOT_BUILD_INVARIANT_FAILURE;
        }
        summary->stage = LAPLACE_UNICODE_ROOT_BUILD_STAGE_COMPLETE;
        summary->unicode_status = LAPLACE_UNICODE_OK;
        summary->spool_status = LAPLACE_SPOOL_OK;
        summary->status = LAPLACE_UNICODE_ROOT_BUILD_OK;
        CalculateBuildReceipt(summary);
        *output_spool = spool.release();
        return LAPLACE_UNICODE_ROOT_BUILD_OK;
    } catch (const std::bad_alloc&) {
        MarkBuildFailure(
            summary, LAPLACE_UNICODE_ROOT_BUILD_MEMORY_FAILURE,
            static_cast<laplace_unicode_root_build_stage>(summary->stage),
            LAPLACE_UNICODE_SOURCE_MEMORY_FAILURE,
            static_cast<laplace_spool_status>(summary->spool_status));
        return LAPLACE_UNICODE_ROOT_BUILD_MEMORY_FAILURE;
    }
}
