#include "laplace/spool.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "blake3.h"

namespace {

constexpr std::string_view SpoolDomain{"laplace-canonical-spool-v1"};
constexpr std::string_view ProducerDomain{"laplace-canonical-spool-producer-v1"};
constexpr std::string_view CursorDomain{"laplace-canonical-spool-cursor-v1"};

struct BatchMetadata {
    std::uint64_t offset;
    std::uint64_t byte_count;
    std::uint64_t record_count;
    std::uint64_t first_ordinal;
};

void HashU32(blake3_hasher& hasher, std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8u),
        static_cast<std::uint8_t>(value >> 16u),
        static_cast<std::uint8_t>(value >> 24u)}};
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher& hasher, std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0u; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashString(blake3_hasher& hasher, std::string_view value) {
    HashU64(hasher, value.size());
    blake3_hasher_update(&hasher, value.data(), value.size());
}

laplace_digest256 Finish(blake3_hasher& hasher) {
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

bool DigestEqual(
    const laplace_digest256& left,
    const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool WriteComplete(int descriptor, const std::uint8_t* bytes, std::size_t count) {
    std::size_t written = 0u;
    while (written < count) {
        const ssize_t result = ::write(
            descriptor, bytes + written, count - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return false;
        }
        written += static_cast<std::size_t>(result);
    }
    return true;
}

laplace_digest256 CursorFingerprint(
    const laplace_canonical_spool_summary& summary,
    std::uint64_t next_batch,
    std::uint64_t next_record,
    std::uint64_t next_byte) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(hasher, CursorDomain);
    blake3_hasher_update(
        &hasher, summary.producer_fingerprint.bytes,
        sizeof(summary.producer_fingerprint.bytes));
    HashU64(hasher, next_batch);
    HashU64(hasher, next_record);
    HashU64(hasher, next_byte);
    return Finish(hasher);
}

}  // namespace

struct laplace_canonical_spool {
    std::vector<BatchMetadata> metadata;
    std::vector<laplace_framework_canonical_batch> batches;
    laplace_canonical_spool_summary summary{};
    int descriptor = -1;
    const std::uint8_t* mapping = nullptr;
    std::size_t mapping_bytes = 0u;
    bool sealed = false;
    bool failed = false;
};

namespace {

laplace_framework_status SpoolPrepare(
    void* state,
    const laplace_framework_context* context,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    laplace_framework_producer_plan* plan) {
    if (state == nullptr || context == nullptr || source_fingerprint == nullptr ||
        recipe_fingerprint == nullptr || plan == nullptr) {
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    }
    *plan = laplace_framework_producer_plan{};
    const auto* spool = static_cast<const laplace_canonical_spool*>(state);
    if (!spool->sealed || spool->failed ||
        !DigestEqual(*source_fingerprint, spool->summary.source_fingerprint) ||
        !DigestEqual(*recipe_fingerprint, spool->summary.recipe_fingerprint) ||
        laplace_framework_context_validate(context) != LAPLACE_FRAMEWORK_OK) {
        return LAPLACE_FRAMEWORK_PRODUCER_PREPARE_FAILED;
    }
    plan->producer_fingerprint = spool->summary.producer_fingerprint;
    plan->initial_cursor_fingerprint = CursorFingerprint(
        spool->summary, 0u, 0u, 0u);
    plan->batch_count = spool->summary.batch_count;
    plan->total_records = spool->summary.total_records;
    plan->total_bytes = spool->summary.total_bytes;
    plan->record_type = spool->summary.record_type;
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status SpoolNext(
    void* state,
    std::uint64_t batch_index,
    laplace_framework_canonical_batch* batch,
    laplace_digest256* cursor_fingerprint) {
    if (state == nullptr || batch == nullptr || cursor_fingerprint == nullptr) {
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    }
    *batch = laplace_framework_canonical_batch{};
    *cursor_fingerprint = laplace_digest256{};
    const auto* spool = static_cast<const laplace_canonical_spool*>(state);
    if (!spool->sealed || spool->failed || batch_index >= spool->batches.size()) {
        return LAPLACE_FRAMEWORK_PRODUCER_BATCH_FAILED;
    }
    *batch = spool->batches[static_cast<std::size_t>(batch_index)];
    *cursor_fingerprint = CursorFingerprint(
        spool->summary, batch_index + 1u,
        batch->first_ordinal + batch->record_count,
        spool->metadata[static_cast<std::size_t>(batch_index)].offset +
            batch->byte_count);
    return LAPLACE_FRAMEWORK_OK;
}

laplace_framework_status SpoolFinish(
    void* state,
    laplace_digest256* completion_fingerprint) {
    if (state == nullptr || completion_fingerprint == nullptr) {
        return LAPLACE_FRAMEWORK_INVALID_ARGUMENT;
    }
    *completion_fingerprint = laplace_digest256{};
    const auto* spool = static_cast<const laplace_canonical_spool*>(state);
    if (!spool->sealed || spool->failed) {
        return LAPLACE_FRAMEWORK_PRODUCER_FINISH_FAILED;
    }
    *completion_fingerprint = spool->summary.spool_fingerprint;
    return LAPLACE_FRAMEWORK_OK;
}

void SpoolAbort(void*) {}

}  // namespace

extern "C" laplace_spool_status laplace_canonical_spool_create(
    const char* directory,
    std::uint32_t record_type,
    const laplace_digest256* source_fingerprint,
    const laplace_digest256* recipe_fingerprint,
    laplace_canonical_spool** spool) {
    if (directory == nullptr || directory[0] == '\0' || record_type == 0u ||
        source_fingerprint == nullptr || recipe_fingerprint == nullptr ||
        spool == nullptr) {
        return LAPLACE_SPOOL_INVALID_ARGUMENT;
    }
    *spool = nullptr;
    auto* created = new (std::nothrow) laplace_canonical_spool{};
    if (created == nullptr) {
        return LAPLACE_SPOOL_MEMORY_FAILURE;
    }
    try {
        std::string pattern(directory);
        if (pattern.back() != '/') {
            pattern.push_back('/');
        }
        pattern += ".laplace-canonical-spool-XXXXXX";
        created->descriptor = ::mkstemp(pattern.data());
        if (created->descriptor < 0 ||
            ::fchmod(created->descriptor, S_IRUSR | S_IWUSR) != 0 ||
            ::unlink(pattern.c_str()) != 0) {
            if (created->descriptor >= 0) {
                (void)::close(created->descriptor);
            }
            delete created;
            return LAPLACE_SPOOL_FILE_OPEN_FAILED;
        }
    } catch (const std::bad_alloc&) {
        delete created;
        return LAPLACE_SPOOL_MEMORY_FAILURE;
    }
    created->summary.source_fingerprint = *source_fingerprint;
    created->summary.recipe_fingerprint = *recipe_fingerprint;
    created->summary.record_type = record_type;
    *spool = created;
    return LAPLACE_SPOOL_OK;
}

extern "C" laplace_spool_status laplace_canonical_spool_append(
    laplace_canonical_spool* spool,
    const laplace_framework_canonical_batch* batch) {
    if (spool == nullptr || batch == nullptr) {
        return LAPLACE_SPOOL_INVALID_ARGUMENT;
    }
    if (spool->sealed || spool->failed) {
        return LAPLACE_SPOOL_STATE_INVALID;
    }
    const std::uint64_t expected_ordinal = spool->summary.total_records;
    const bool ordinal_matches = batch->first_ordinal == expected_ordinal;
#if defined(LAPLACE_TEST_SPOOL_SKIP_ORDINAL_VALIDATION)
    (void)ordinal_matches;
#endif
    if (batch->canonical_bytes == nullptr || batch->byte_count == 0u ||
        batch->record_count == 0u ||
#if !defined(LAPLACE_TEST_SPOOL_SKIP_ORDINAL_VALIDATION)
        !ordinal_matches ||
#endif
        batch->record_type != spool->summary.record_type ||
        batch->flags != LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS ||
        batch->byte_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        std::numeric_limits<std::uint64_t>::max() -
                spool->summary.total_records < batch->record_count ||
        std::numeric_limits<std::uint64_t>::max() -
                spool->summary.total_bytes < batch->byte_count) {
        return LAPLACE_SPOOL_BATCH_INVALID;
    }
    try {
        spool->metadata.push_back(BatchMetadata{
            spool->summary.total_bytes, batch->byte_count,
            batch->record_count, batch->first_ordinal});
    } catch (const std::bad_alloc&) {
        spool->failed = true;
        return LAPLACE_SPOOL_MEMORY_FAILURE;
    }
    if (!WriteComplete(
            spool->descriptor, batch->canonical_bytes,
            static_cast<std::size_t>(batch->byte_count))) {
        spool->failed = true;
        return LAPLACE_SPOOL_FILE_IO_FAILED;
    }
    spool->summary.total_records += batch->record_count;
    spool->summary.total_bytes += batch->byte_count;
    spool->summary.batch_count += 1u;
    return LAPLACE_SPOOL_OK;
}

extern "C" laplace_spool_status laplace_canonical_spool_seal(
    laplace_canonical_spool* spool,
    laplace_canonical_spool_summary* summary) {
    if (spool == nullptr || summary == nullptr) {
        return LAPLACE_SPOOL_INVALID_ARGUMENT;
    }
    *summary = laplace_canonical_spool_summary{};
    if (spool->sealed || spool->failed || spool->metadata.empty() ||
        spool->summary.total_bytes == 0u ||
        spool->summary.total_bytes > static_cast<std::uint64_t>(SIZE_MAX)) {
        return LAPLACE_SPOOL_STATE_INVALID;
    }
    if (::fsync(spool->descriptor) != 0) {
        spool->failed = true;
        return LAPLACE_SPOOL_FILE_IO_FAILED;
    }
    struct stat information {};
    if (::fstat(spool->descriptor, &information) != 0 ||
        information.st_size < 0 ||
        static_cast<std::uint64_t>(information.st_size) !=
            spool->summary.total_bytes) {
        spool->failed = true;
        return LAPLACE_SPOOL_FILE_IO_FAILED;
    }
    spool->mapping_bytes = static_cast<std::size_t>(spool->summary.total_bytes);
    void* mapping = ::mmap(
        nullptr, spool->mapping_bytes, PROT_READ, MAP_SHARED,
        spool->descriptor, 0);
    if (mapping == MAP_FAILED) {
        spool->mapping_bytes = 0u;
        spool->failed = true;
        return LAPLACE_SPOOL_FILE_MAPPING_FAILED;
    }
    spool->mapping = static_cast<const std::uint8_t*>(mapping);
    (void)::madvise(
        const_cast<std::uint8_t*>(spool->mapping),
        spool->mapping_bytes, MADV_SEQUENTIAL);
    try {
        spool->batches.reserve(spool->metadata.size());
        for (const BatchMetadata& metadata : spool->metadata) {
            spool->batches.push_back(laplace_framework_canonical_batch{
                spool->mapping + metadata.offset,
                metadata.byte_count,
                metadata.record_count,
                metadata.first_ordinal,
                spool->summary.record_type,
                LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS});
        }
    } catch (const std::bad_alloc&) {
        spool->failed = true;
        return LAPLACE_SPOOL_MEMORY_FAILURE;
    }
    std::uint32_t verified_type = 0u;
    std::uint64_t verified_records = 0u;
    std::uint64_t verified_bytes = 0u;
    if (laplace_framework_canonical_stream_fingerprint(
            spool->batches.data(), spool->batches.size(),
            &spool->summary.stream_fingerprint, &verified_type,
            &verified_records, &verified_bytes) != LAPLACE_FRAMEWORK_OK ||
        verified_type != spool->summary.record_type ||
        verified_records != spool->summary.total_records ||
        verified_bytes != spool->summary.total_bytes) {
        spool->failed = true;
        return LAPLACE_SPOOL_BATCH_INVALID;
    }
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    HashString(hasher, SpoolDomain);
    blake3_hasher_update(
        &hasher, spool->summary.source_fingerprint.bytes,
        sizeof(spool->summary.source_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, spool->summary.recipe_fingerprint.bytes,
        sizeof(spool->summary.recipe_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, spool->summary.stream_fingerprint.bytes,
        sizeof(spool->summary.stream_fingerprint.bytes));
    HashU32(hasher, spool->summary.record_type);
    HashU64(hasher, spool->summary.total_records);
    HashU64(hasher, spool->summary.total_bytes);
    HashU64(hasher, spool->metadata.size());
    for (const BatchMetadata& metadata : spool->metadata) {
        HashU64(hasher, metadata.offset);
        HashU64(hasher, metadata.byte_count);
        HashU64(hasher, metadata.record_count);
        HashU64(hasher, metadata.first_ordinal);
    }
    spool->summary.spool_fingerprint = Finish(hasher);
    blake3_hasher_init(&hasher);
    HashString(hasher, ProducerDomain);
    blake3_hasher_update(
        &hasher, spool->summary.spool_fingerprint.bytes,
        sizeof(spool->summary.spool_fingerprint.bytes));
    spool->summary.producer_fingerprint = Finish(hasher);
    spool->summary.status = LAPLACE_SPOOL_OK;
    spool->sealed = true;
    *summary = spool->summary;
    return LAPLACE_SPOOL_OK;
}

extern "C" laplace_spool_status laplace_canonical_spool_producer(
    laplace_canonical_spool* spool,
    laplace_framework_producer_v1* producer) {
    if (spool == nullptr || producer == nullptr) {
        return LAPLACE_SPOOL_INVALID_ARGUMENT;
    }
    *producer = laplace_framework_producer_v1{};
    if (!spool->sealed || spool->failed) {
        return LAPLACE_SPOOL_STATE_INVALID;
    }
    producer->state = spool;
    producer->prepare = SpoolPrepare;
    producer->next = SpoolNext;
    producer->finish = SpoolFinish;
    producer->abort = SpoolAbort;
    producer->abi_major = LAPLACE_FRAMEWORK_PRODUCER_ABI_MAJOR;
    producer->abi_minor = LAPLACE_FRAMEWORK_PRODUCER_ABI_MINOR;
    producer->flags = LAPLACE_FRAMEWORK_KNOWN_PRODUCER_FLAGS;
    return LAPLACE_SPOOL_OK;
}

extern "C" void laplace_canonical_spool_destroy(
    laplace_canonical_spool** spool) {
    if (spool == nullptr || *spool == nullptr) {
        return;
    }
    laplace_canonical_spool* value = *spool;
    if (value->mapping != nullptr && value->mapping_bytes != 0u) {
        (void)::munmap(
            const_cast<std::uint8_t*>(value->mapping), value->mapping_bytes);
    }
    if (value->descriptor >= 0) {
        (void)::close(value->descriptor);
    }
    delete value;
    *spool = nullptr;
}
