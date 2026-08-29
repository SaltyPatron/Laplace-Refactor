#include "laplace/tabular_source.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "blake3.h"
#include "sha256_internal.hpp"

namespace {

constexpr std::uint64_t RoleShift = 6u;
constexpr std::uint32_t RecipeVersion = 1u;

enum class Role : std::uint64_t {
    Tag = 1u,
    ArtifactName = 2u,
    ArtifactDigest = 3u,
    RawOctet = 4u,
    Header = 5u,
    Record = 6u,
    Field = 7u,
    Column = 8u,
    Value = 9u,
    Terminator = 10u,
    Artifact = 11u,
    ParentArtifact = 12u
};

std::uint64_t Metadata(const Role role) {
    return static_cast<std::uint64_t>(role) << RoleShift;
}

bool DigestZero(const laplace_digest256& value) {
    for (const std::uint8_t byte : value.bytes) {
        if (byte != 0u) {
            return false;
        }
    }
    return true;
}

bool DigestEqual(
    const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool Add(std::uint64_t& target, const std::uint64_t value) {
    if (target > std::numeric_limits<std::uint64_t>::max() - value) {
        return false;
    }
    target += value;
    return true;
}

void HashU32(blake3_hasher& hasher, const std::uint32_t value) {
    std::array<std::uint8_t, 4> bytes{};
    for (std::size_t index = 0u; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher& hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0u; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8u));
    }
    blake3_hasher_update(&hasher, bytes.data(), bytes.size());
}

void HashBytes(
    blake3_hasher& hasher, const void* value, const std::size_t size) {
    HashU64(hasher, static_cast<std::uint64_t>(size));
    if (size != 0u) {
        blake3_hasher_update(&hasher, value, size);
    }
}

laplace_digest256 Finish(blake3_hasher& hasher) {
    laplace_digest256 result{};
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

bool DecodeUtf8(
    const std::uint8_t* bytes,
    const std::size_t byte_count,
    std::vector<std::uint32_t>& output) {
    std::size_t offset = 0u;
    while (offset < byte_count) {
        const std::uint8_t first = bytes[offset];
        std::uint32_t value{};
        std::size_t length{};
        std::uint32_t minimum{};
        if (first <= 0x7fu) {
            value = first;
            length = 1u;
        } else if ((first & 0xe0u) == 0xc0u) {
            value = first & 0x1fu;
            length = 2u;
            minimum = 0x80u;
        } else if ((first & 0xf0u) == 0xe0u) {
            value = first & 0x0fu;
            length = 3u;
            minimum = 0x800u;
        } else if ((first & 0xf8u) == 0xf0u) {
            value = first & 0x07u;
            length = 4u;
            minimum = 0x10000u;
        } else {
            return false;
        }
        if (offset > byte_count - length) {
            return false;
        }
        for (std::size_t index = 1u; index < length; ++index) {
            const std::uint8_t continuation = bytes[offset + index];
            if ((continuation & 0xc0u) != 0x80u) {
                return false;
            }
            value = (value << 6u) | (continuation & 0x3fu);
        }
        if (value < minimum || value > 0x10ffffu ||
            (value >= 0xd800u && value <= 0xdfffu)) {
            return false;
        }
        output.push_back(value);
        offset += length;
    }
    return true;
}

void EncodeUtf8(const std::uint32_t value, std::vector<std::uint8_t>& output) {
    if (value <= 0x7fu) {
        output.push_back(static_cast<std::uint8_t>(value));
    } else if (value <= 0x7ffu) {
        output.push_back(static_cast<std::uint8_t>(0xc0u | (value >> 6u)));
        output.push_back(static_cast<std::uint8_t>(0x80u | (value & 0x3fu)));
    } else if (value <= 0xffffu) {
        output.push_back(static_cast<std::uint8_t>(0xe0u | (value >> 12u)));
        output.push_back(static_cast<std::uint8_t>(
            0x80u | ((value >> 6u) & 0x3fu)));
        output.push_back(static_cast<std::uint8_t>(0x80u | (value & 0x3fu)));
    } else {
        output.push_back(static_cast<std::uint8_t>(0xf0u | (value >> 18u)));
        output.push_back(static_cast<std::uint8_t>(
            0x80u | ((value >> 12u) & 0x3fu)));
        output.push_back(static_cast<std::uint8_t>(
            0x80u | ((value >> 6u) & 0x3fu)));
        output.push_back(static_cast<std::uint8_t>(0x80u | (value & 0x3fu)));
    }
}

std::string Hex(const std::uint8_t* bytes, const std::size_t count) {
    constexpr char Digits[] = "0123456789abcdef";
    std::string result(count * 2u, '0');
    for (std::size_t index = 0u; index < count; ++index) {
        result[index * 2u] = Digits[bytes[index] >> 4u];
        result[index * 2u + 1u] = Digits[bytes[index] & 0x0fu];
    }
    return result;
}

bool DynamicProfileFieldsZero(const laplace_source_profile_manifest& value) {
    const std::array<const std::uint64_t*, 30> fields{{
        &value.byte_count, &value.container_count, &value.member_count,
        &value.file_count, &value.record_count, &value.field_count,
        &value.syntax_node_count, &value.span_count, &value.edge_count,
        &value.reference_count, &value.occurrence_count, &value.claim_count,
        &value.mapping_count, &value.error_count, &value.unknown_count,
        &value.transformation_count, &value.output_count,
        &value.closure_subject_count, &value.accepted_count,
        &value.rejected_count, &value.duplicate_count, &value.reused_count,
        &value.transformed_count, &value.lossy_count,
        &value.unsupported_count, &value.malformed_count,
        &value.unresolved_count, &value.persisted_count,
        &value.derived_count, &value.not_applicable_mask}};
    for (const std::uint64_t* field : fields) {
        if (*field != 0u) {
            return false;
        }
    }
    const std::uint32_t epistemic_class =
        LAPLACE_SOURCE_PROFILE_GET_EPISTEMIC_CLASS(value.flags);
    const std::uint32_t evidence_source_type =
        LAPLACE_SOURCE_PROFILE_GET_EVIDENCE_SOURCE_TYPE(value.flags);
    return DigestZero(value.profile_id) &&
        value.reconstruction_class >=
            LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT &&
        value.reconstruction_class <=
            LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_NONE &&
        (value.flags & ~LAPLACE_SOURCE_PROFILE_FLAGS_KNOWN_MASK) == 0u &&
        epistemic_class <= LAPLACE_SOURCE_PROFILE_EPISTEMIC_MAX &&
        evidence_source_type <= LAPLACE_SOURCE_PROFILE_EVIDENCE_MAX;
}

bool ArtifactValid(const laplace_tabular_artifact& artifact) {
    if (artifact.bytes == nullptr || artifact.name == nullptr ||
        artifact.byte_count == 0u || artifact.name_byte_count == 0u ||
        artifact.name_byte_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        artifact.byte_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        (artifact.media_type == nullptr) != (artifact.media_type_byte_count == 0u) ||
        artifact.media_type_byte_count > static_cast<std::uint64_t>(SIZE_MAX) ||
        (artifact.media_type_byte_count != 0u &&
         std::memchr(artifact.media_type, 0,
                     static_cast<std::size_t>(artifact.media_type_byte_count)) != nullptr) ||
        artifact.flags == 0u ||
        (artifact.flags & ~(LAPLACE_TABULAR_ARTIFACT_CONTAINER |
                            LAPLACE_TABULAR_ARTIFACT_MEMBER |
                            LAPLACE_TABULAR_ARTIFACT_EXACT_DISTRIBUTION)) != 0u ||
        artifact.reserved != 0u ||
        std::memcmp(
            artifact.artifact_id.bytes, artifact.expected_sha256,
            sizeof(artifact.expected_sha256)) != 0) {
        return false;
    }
    if (artifact.mode == LAPLACE_TABULAR_ARTIFACT_RAW) {
        return artifact.columns == nullptr &&
            artifact.delimiter == 0u &&
            artifact.line_terminator == LAPLACE_TABULAR_TERMINATOR_NONE &&
            artifact.expected_column_count == 0u &&
            artifact.expected_record_count == 0u &&
            artifact.expected_field_count == 0u &&
            artifact.reference_column_mask == 0u &&
            artifact.outcome_type == 0u && artifact.header_record_count == 0u;
    }
    if (artifact.mode != LAPLACE_TABULAR_ARTIFACT_DELIMITED ||
        artifact.columns == nullptr ||
        !(artifact.delimiter > 0u && artifact.delimiter < 0x80u &&
          artifact.delimiter != '\r' && artifact.delimiter != '\n')) {
        return false;
    }
    if (!(
        (artifact.line_terminator == LAPLACE_TABULAR_TERMINATOR_LF ||
         artifact.line_terminator == LAPLACE_TABULAR_TERMINATOR_CRLF) &&
        artifact.expected_column_count > 0u &&
        artifact.expected_column_count <= 64u &&
        artifact.header_record_count <= 1u &&
        artifact.expected_record_count > artifact.header_record_count &&
        artifact.expected_field_count > 0u &&
        artifact.outcome_type >= 1u && artifact.outcome_type <= 9u)) {
        return false;
    }
    for (std::size_t index = 0u; index < artifact.expected_column_count; ++index) {
        const laplace_tabular_column& column = artifact.columns[index];
        std::vector<std::uint32_t> positions;
        if (column.bytes == nullptr || column.byte_count == 0u ||
            column.byte_count > SIZE_MAX ||
            !DecodeUtf8(
                column.bytes, static_cast<std::size_t>(column.byte_count),
                positions) || positions.empty()) {
            return false;
        }
    }
    return true;
}

bool IdZero(const laplace_digest256& value) {
    return DigestZero(value);
}

bool IdZero(const laplace_id128& value) {
    for (const std::uint8_t byte : value.bytes) {
        if (byte != 0u) {
            return false;
        }
    }
    return true;
}

laplace_tabular_source_status ArtifactGraph(
    const laplace_tabular_artifact* artifacts,
    const std::size_t artifact_count,
    laplace_digest256& result) {
    if (artifacts == nullptr || artifact_count == 0u) {
        return LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT;
    }
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    constexpr std::string_view domain{LAPLACE_TABULAR_ARTIFACT_GRAPH_DOMAIN};
    HashBytes(hasher, domain.data(), domain.size());
    HashU64(hasher, static_cast<std::uint64_t>(artifact_count));
    std::string prior_name;
    for (std::size_t index = 0u; index < artifact_count; ++index) {
        const laplace_tabular_artifact& artifact = artifacts[index];
        if (!ArtifactValid(artifact)) {
            return LAPLACE_TABULAR_SOURCE_ARTIFACT_INVALID;
        }
        const std::string name(
            artifact.name, static_cast<std::size_t>(artifact.name_byte_count));
        std::vector<std::uint32_t> name_positions;
        if (!DecodeUtf8(
                reinterpret_cast<const std::uint8_t*>(name.data()),
                name.size(), name_positions) || name_positions.empty() ||
            (!prior_name.empty() && prior_name >= name)) {
            return LAPLACE_TABULAR_SOURCE_ARTIFACT_INVALID;
        }
        prior_name = name;
        const bool member =
            (artifact.flags & LAPLACE_TABULAR_ARTIFACT_MEMBER) != 0u;
        if (member == IdZero(artifact.parent_artifact_id)) {
            return LAPLACE_TABULAR_SOURCE_ARTIFACT_INVALID;
        }
        if (member) {
            bool parent_found = false;
            for (std::size_t parent_index = 0u;
                 parent_index < index; ++parent_index) {
                if (DigestEqual(
                        artifacts[parent_index].artifact_id,
                        artifact.parent_artifact_id) &&
                    (artifacts[parent_index].flags &
                     LAPLACE_TABULAR_ARTIFACT_CONTAINER) != 0u) {
                    parent_found = true;
                    break;
                }
            }
            if (!parent_found) {
                return LAPLACE_TABULAR_SOURCE_ARTIFACT_INVALID;
            }
        }
        for (std::size_t prior_index = 0u;
             prior_index < index; ++prior_index) {
            if (DigestEqual(
                    artifacts[prior_index].artifact_id,
                    artifact.artifact_id)) {
                return LAPLACE_TABULAR_SOURCE_ARTIFACT_INVALID;
            }
        }
        const auto actual_sha = laplace::internal::Sha256(
            artifact.bytes, static_cast<std::size_t>(artifact.byte_count));
#if !defined(LAPLACE_TEST_TABULAR_SKIP_ARTIFACT_DIGEST)
        if (std::memcmp(
                actual_sha.data(), artifact.expected_sha256,
                actual_sha.size()) != 0) {
            return LAPLACE_TABULAR_SOURCE_DIGEST_MISMATCH;
        }
#endif
        HashBytes(hasher, name.data(), name.size());
        HashBytes(hasher, artifact.media_type,
                  static_cast<std::size_t>(artifact.media_type_byte_count));
        HashU64(hasher, artifact.byte_count);
        HashBytes(
            hasher, artifact.expected_sha256,
            sizeof(artifact.expected_sha256));
#if !defined(LAPLACE_TEST_TABULAR_OMIT_PARENT_EDGE)
        HashBytes(
            hasher, artifact.parent_artifact_id.bytes,
            sizeof(artifact.parent_artifact_id.bytes));
#endif
        HashU32(hasher, artifact.mode);
        HashU32(hasher, artifact.delimiter);
        HashU32(hasher, artifact.line_terminator);
        HashU32(hasher, artifact.expected_column_count);
        HashU32(hasher, artifact.header_record_count);
        for (std::size_t column_index = 0u;
             column_index < artifact.expected_column_count; ++column_index) {
            HashBytes(
                hasher, artifact.columns[column_index].bytes,
                static_cast<std::size_t>(
                    artifact.columns[column_index].byte_count));
        }
        HashU64(hasher, artifact.expected_record_count);
        HashU64(hasher, artifact.expected_field_count);
        HashU64(hasher, artifact.reference_column_mask);
        HashU32(hasher, artifact.outcome_type);
        HashU32(hasher, artifact.flags);
    }
    result = Finish(hasher);
    return LAPLACE_TABULAR_SOURCE_OK;
}

laplace_tabular_source_status SourceGraph(
    const laplace_tabular_artifact* artifacts,
    const std::size_t artifact_count,
    const laplace_tabular_reference_rule* reference_rules,
    const std::size_t reference_rule_count,
    const laplace_tabular_mapping_rule* mapping_rules,
    const std::size_t mapping_rule_count,
    laplace_digest256& result) {
    if ((reference_rules == nullptr) != (reference_rule_count == 0u) ||
        (mapping_rules == nullptr) != (mapping_rule_count == 0u)) {
        return LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT;
    }
    laplace_digest256 artifact_graph{};
    const auto artifact_status =
        ArtifactGraph(artifacts, artifact_count, artifact_graph);
    if (artifact_status != LAPLACE_TABULAR_SOURCE_OK) {
        return artifact_status;
    }
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    constexpr std::string_view domain{LAPLACE_TABULAR_SOURCE_GRAPH_DOMAIN};
    HashBytes(hasher, domain.data(), domain.size());
    HashBytes(hasher, artifact_graph.bytes, sizeof(artifact_graph.bytes));
    HashU64(hasher, static_cast<std::uint64_t>(reference_rule_count));
    for (std::size_t index = 0u; index < reference_rule_count; ++index) {
        const auto& rule = reference_rules[index];
        HashU64(hasher, rule.artifact_index);
        HashU64(hasher, rule.column_index);
        HashBytes(hasher, rule.name_space.bytes, sizeof(rule.name_space.bytes));
        HashU32(hasher, rule.kind);
        HashU32(hasher, rule.flags);
    }
    HashU64(hasher, static_cast<std::uint64_t>(mapping_rule_count));
    for (std::size_t index = 0u; index < mapping_rule_count; ++index) {
        const auto& rule = mapping_rules[index];
        if (rule.relation_content == nullptr ||
            rule.relation_content_byte_count == 0u ||
            rule.relation_content_byte_count > SIZE_MAX) {
            return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
        }
        HashU64(hasher, rule.artifact_index);
        HashU64(hasher, rule.left_column_index);
        HashU64(hasher, rule.right_column_index);
        HashBytes(
            hasher, rule.relation_content,
            static_cast<std::size_t>(rule.relation_content_byte_count));
        HashU64(hasher, rule.relation_version);
        HashU32(hasher, rule.relation_kind);
        HashU32(hasher, rule.flags);
    }
    result = Finish(hasher);
    return LAPLACE_TABULAR_SOURCE_OK;
}

struct ParsedArtifact {
    std::string name;
    std::vector<std::uint32_t> raw_octets;
    std::vector<std::vector<std::vector<std::uint32_t>>> rows;
    std::uint32_t delimiter{};
    std::uint32_t terminator{};
    std::uint32_t mode{};
    bool exact_distribution{};
};

}  // namespace

struct laplace_tabular_source_plan {
    laplace_tabular_source_plan_view view{};
    std::vector<std::uint32_t> atom_positions;
    std::vector<laplace_composition_operand> operands;
    std::vector<laplace_composition_request> requests;
    std::vector<std::uint64_t> claim_result_indexes;
    std::vector<std::uint64_t> claim_source_ordinals;
    std::vector<std::uint32_t> claim_outcome_types;
    std::vector<std::uint64_t> artifact_root_result_indexes;
    std::vector<laplace_tabular_reference_occurrence> reference_occurrences;
    std::vector<laplace_tabular_mapping_occurrence> mapping_occurrences;
    std::vector<ParsedArtifact> artifacts;
};

namespace {

class PlanBuilder final {
public:
    PlanBuilder(
        laplace_tabular_source_plan& plan,
        const laplace_tabular_source_input& input)
        : plan_(plan), input_(input) {}

    laplace_tabular_source_status Build() {
        if (!DynamicProfileFieldsZero(input_.profile_declaration) ||
            input_.artifacts == nullptr || input_.artifact_count == 0u ||
            input_.artifact_count > static_cast<std::uint64_t>(SIZE_MAX) ||
            input_.flags != 0u || input_.reserved != 0u ||
            DigestZero(input_.geometry_epoch) ||
            DigestZero(input_.occurrence_context_fingerprint) ||
            DigestZero(input_.profile_declaration.recipe_program_fingerprint)) {
            return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
        }
        if (LAPLACE_SOURCE_PROFILE_GET_EPISTEMIC_CLASS(
                input_.profile_declaration.flags) !=
                LAPLACE_SOURCE_PROFILE_EPISTEMIC_UNSPECIFIED ||
            LAPLACE_SOURCE_PROFILE_GET_EVIDENCE_SOURCE_TYPE(
                input_.profile_declaration.flags) !=
                LAPLACE_SOURCE_PROFILE_EVIDENCE_UNSPECIFIED) {
            for (std::size_t artifact_index = 0u;
                 artifact_index < static_cast<std::size_t>(input_.artifact_count);
                 ++artifact_index) {
                if (input_.artifacts[artifact_index].media_type == nullptr ||
                    input_.artifacts[artifact_index].media_type_byte_count == 0u) {
                    return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
                }
            }
        }
        if (!ValidateReferenceRules() || !ValidateMappingRules()) {
            return status_;
        }
        laplace_digest256 graph{};
        const auto graph_status = SourceGraph(
            input_.artifacts, static_cast<std::size_t>(input_.artifact_count),
            input_.reference_rules,
            static_cast<std::size_t>(input_.reference_rule_count),
            input_.mapping_rules,
            static_cast<std::size_t>(input_.mapping_rule_count),
            graph);
        if (graph_status != LAPLACE_TABULAR_SOURCE_OK) {
            return graph_status;
        }
        if (!DigestEqual(
                graph,
                input_.profile_declaration.artifact_graph_fingerprint)) {
            return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
        }
        plan_.view.source_fingerprint = SourceFingerprint(graph);
        plan_.view.profile = input_.profile_declaration;
        plan_.artifacts.reserve(static_cast<std::size_t>(input_.artifact_count));
        plan_.artifact_root_result_indexes.reserve(
            static_cast<std::size_t>(input_.artifact_count));

        const auto source_tag = Tag("source-boundary");
        const auto artifact_tag = Tag("artifact");
        const auto raw_tag = Tag("raw-octets");
        const auto delimited_tag = Tag("utf8-delimited");
        const auto field_tag = Tag("field");
        const auto header_tag = Tag("header");
        const auto record_tag = Tag("record");
        const auto lf_tag = Tag("lf");
        const auto crlf_tag = Tag("crlf");
        if (!source_tag || !artifact_tag || !raw_tag || !delimited_tag ||
            !field_tag || !header_tag || !record_tag || !lf_tag ||
            !crlf_tag) {
            return LAPLACE_TABULAR_SOURCE_MEMORY_FAILURE;
        }
        mapping_relation_indexes_.reserve(
            static_cast<std::size_t>(input_.mapping_rule_count));
        for (std::size_t index = 0u;
             index < static_cast<std::size_t>(input_.mapping_rule_count);
             ++index) {
            const auto& rule = input_.mapping_rules[index];
            const auto relation = Text(
                rule.relation_content,
                static_cast<std::size_t>(rule.relation_content_byte_count),
                input_.occurrence_context_fingerprint, true);
            if (!relation) {
                return status_;
            }
            mapping_relation_indexes_.push_back(*relation);
        }
        std::vector<std::pair<std::uint64_t, Role>> root_children;
        root_children.emplace_back(*source_tag, Role::Tag);
        for (std::size_t artifact_index = 0u;
             artifact_index < static_cast<std::size_t>(input_.artifact_count);
             ++artifact_index) {
            const auto result = BuildArtifact(
                input_.artifacts[artifact_index], artifact_index,
                *artifact_tag, *raw_tag, *delimited_tag, *field_tag,
                *header_tag, *record_tag, *lf_tag, *crlf_tag);
            if (!result.has_value()) {
                return status_;
            }
            plan_.artifact_root_result_indexes.push_back(*result);
            root_children.emplace_back(*result, Role::Artifact);
        }
        const auto root = Node(root_children, input_.occurrence_context_fingerprint);
        if (!root) {
            return status_;
        }
        plan_.view.root_result_index = *root;
        if (!FinalizeProfile() || !VerifyReconstruction()) {
            return status_;
        }
        return LAPLACE_TABULAR_SOURCE_OK;
    }

private:
    bool ValidateReferenceRules() {
        if ((input_.reference_rules == nullptr) !=
                (input_.reference_rule_count == 0u) ||
            input_.reference_rule_count > static_cast<std::uint64_t>(SIZE_MAX)) {
            status_ = LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
            return false;
        }
        std::uint64_t prior_artifact = 0u;
        std::uint64_t prior_column = 0u;
        for (std::size_t index = 0u;
             index < static_cast<std::size_t>(input_.reference_rule_count);
             ++index) {
            const auto& rule = input_.reference_rules[index];
            if (rule.artifact_index >= input_.artifact_count ||
                rule.column_index >= input_.artifacts[rule.artifact_index].expected_column_count ||
                !laplace_highway_kind_valid(rule.kind) ||
                (rule.flags & LAPLACE_REFERENCE_RULE_ENDPOINT) == 0u ||
                (rule.flags & ~LAPLACE_REFERENCE_RULE_KNOWN_MASK) != 0u ||
                (rule.flags & (LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION |
                               LAPLACE_REFERENCE_RULE_RETIRED_DECLARATION)) ==
                    (LAPLACE_REFERENCE_RULE_PRESENT_DECLARATION |
                     LAPLACE_REFERENCE_RULE_RETIRED_DECLARATION) ||
                IdZero(rule.name_space) ||
                (index != 0u &&
                 (rule.artifact_index < prior_artifact ||
                  (rule.artifact_index == prior_artifact &&
                   rule.column_index <= prior_column))) ||
                ((input_.artifacts[rule.artifact_index].reference_column_mask >>
                  rule.column_index) & 1u) == 0u) {
                status_ = LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
                return false;
            }
            prior_artifact = rule.artifact_index;
            prior_column = rule.column_index;
        }
        for (std::size_t artifact = 0u;
             artifact < static_cast<std::size_t>(input_.artifact_count);
             ++artifact) {
            std::uint64_t rule_mask = 0u;
            for (std::size_t index = 0u;
                 index < static_cast<std::size_t>(input_.reference_rule_count);
                 ++index) {
                const auto& rule = input_.reference_rules[index];
                if (rule.artifact_index == artifact) {
                    rule_mask |= UINT64_C(1) << rule.column_index;
                }
            }
            if (rule_mask != input_.artifacts[artifact].reference_column_mask) {
                status_ = LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
                return false;
            }
        }
        return true;
    }

    const laplace_tabular_reference_rule* ReferenceRule(
        const std::size_t artifact_index,
        const std::size_t column_index) const {
        for (std::size_t index = 0u;
             index < static_cast<std::size_t>(input_.reference_rule_count);
             ++index) {
            const auto& rule = input_.reference_rules[index];
            if (rule.artifact_index == artifact_index &&
                rule.column_index == column_index) {
                return &rule;
            }
        }
        return nullptr;
    }

    bool ValidateMappingRules() {
        if ((input_.mapping_rules == nullptr) !=
                (input_.mapping_rule_count == 0u) ||
            input_.mapping_rule_count > static_cast<std::uint64_t>(SIZE_MAX)) {
            status_ = LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
            return false;
        }
        const laplace_tabular_mapping_rule* prior = nullptr;
        for (std::size_t index = 0u;
             index < static_cast<std::size_t>(input_.mapping_rule_count);
             ++index) {
            const auto& rule = input_.mapping_rules[index];
            std::vector<std::uint32_t> relation_positions;
            if (rule.artifact_index >= input_.artifact_count ||
                rule.left_column_index == rule.right_column_index ||
                rule.left_column_index >=
                    input_.artifacts[rule.artifact_index].expected_column_count ||
                rule.right_column_index >=
                    input_.artifacts[rule.artifact_index].expected_column_count ||
                ReferenceRule(rule.artifact_index, rule.left_column_index) ==
                    nullptr ||
                ReferenceRule(rule.artifact_index, rule.right_column_index) ==
                    nullptr ||
                rule.relation_content == nullptr ||
                rule.relation_content_byte_count == 0u ||
                rule.relation_content_byte_count > SIZE_MAX ||
                !DecodeUtf8(
                    rule.relation_content,
                    static_cast<std::size_t>(
                        rule.relation_content_byte_count),
                    relation_positions) || relation_positions.empty() ||
                rule.relation_version == 0u ||
                rule.relation_kind != LAPLACE_HIGHWAY_KIND_RELATION ||
                (rule.flags != LAPLACE_REFERENCE_MAPPING_FLAG_DIRECTED &&
                 rule.flags != LAPLACE_REFERENCE_MAPPING_FLAG_SYMMETRIC)) {
                status_ = LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
                return false;
            }
            if (prior != nullptr) {
                const auto prior_key = std::pair{
                    prior->artifact_index,
                    std::pair{prior->left_column_index,
                              prior->right_column_index}};
                const auto current_key = std::pair{
                    rule.artifact_index,
                    std::pair{rule.left_column_index,
                              rule.right_column_index}};
                const std::string_view prior_relation{
                    reinterpret_cast<const char*>(prior->relation_content),
                    static_cast<std::size_t>(
                        prior->relation_content_byte_count)};
                const std::string_view current_relation{
                    reinterpret_cast<const char*>(rule.relation_content),
                    static_cast<std::size_t>(
                        rule.relation_content_byte_count)};
                const int relation_compare = current_relation.compare(
                    prior_relation);
                if (current_key < prior_key ||
                    (current_key == prior_key &&
                     (relation_compare < 0 ||
                      (relation_compare == 0 &&
                       (rule.relation_kind < prior->relation_kind ||
                        (rule.relation_kind == prior->relation_kind &&
                         (rule.relation_version < prior->relation_version ||
                          (rule.relation_version == prior->relation_version &&
                           rule.flags <= prior->flags)))))))) {
                    status_ = LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
                    return false;
                }
            }
            prior = &rule;
        }
        return true;
    }

    std::optional<std::uint64_t> Tag(const std::string_view value) {
        const auto found = tag_indexes_.find(std::string(value));
        if (found != tag_indexes_.end()) {
            return found->second;
        }
        const auto created = Text(
            reinterpret_cast<const std::uint8_t*>(value.data()),
            value.size(), input_.occurrence_context_fingerprint, true);
        if (created) {
            tag_indexes_.emplace(value, *created);
        }
        return created;
    }

    std::uint64_t AtomIndex(const std::uint32_t position) {
        const auto found = atom_indexes_.find(position);
        if (found != atom_indexes_.end()) {
            return found->second;
        }
        const std::uint64_t index =
            static_cast<std::uint64_t>(plan_.atom_positions.size());
        plan_.atom_positions.push_back(position);
        atom_indexes_.emplace(position, index);
        return index;
    }

    std::optional<std::uint64_t> Text(
        const std::uint8_t* bytes,
        const std::size_t byte_count,
        const laplace_digest256& context,
        const bool reuse) {
        const std::string key(
            reinterpret_cast<const char*>(bytes), byte_count);
        if (reuse) {
            const auto found = text_indexes_.find(key);
            if (found != text_indexes_.end()) {
                return found->second;
            }
        }
        std::vector<std::uint32_t> positions;
        if (!DecodeUtf8(bytes, byte_count, positions) || positions.empty()) {
            status_ = LAPLACE_TABULAR_SOURCE_UTF8_INVALID;
            return std::nullopt;
        }
        const std::uint64_t first =
            static_cast<std::uint64_t>(plan_.operands.size());
        for (const std::uint32_t position : positions) {
            plan_.operands.push_back(laplace_composition_operand{
                AtomIndex(position), 1u, 0u,
                LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0u});
        }
        const auto result = AddRequest(first, positions.size(), context);
        if (result && reuse) {
            text_indexes_.emplace(key, *result);
        }
        return result;
    }

    std::optional<std::uint64_t> Raw(
        const std::vector<std::uint32_t>& bytes,
        const laplace_digest256& context) {
        if (bytes.empty()) {
            status_ = LAPLACE_TABULAR_SOURCE_ARTIFACT_INVALID;
            return std::nullopt;
        }
        const std::uint64_t first =
            static_cast<std::uint64_t>(plan_.operands.size());
        for (const std::uint32_t value : bytes) {
            plan_.operands.push_back(laplace_composition_operand{
                AtomIndex(value), 1u, Metadata(Role::RawOctet),
                LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0u});
        }
        return AddRequest(first, bytes.size(), context);
    }

    std::optional<std::uint64_t> Node(
        const std::vector<std::pair<std::uint64_t, Role>>& children,
        const laplace_digest256& context) {
        if (children.size() < 2u) {
            status_ = LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
            return std::nullopt;
        }
        const std::uint64_t first =
            static_cast<std::uint64_t>(plan_.operands.size());
        for (const auto& [index, role] : children) {
            if (index >= plan_.requests.size()) {
                status_ = LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
                return std::nullopt;
            }
            plan_.operands.push_back(laplace_composition_operand{
                index, 1u, Metadata(role),
                LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT, 0u});
        }
        return AddRequest(first, children.size(), context);
    }

    std::optional<std::uint64_t> AddRequest(
        const std::uint64_t first,
        const std::size_t count,
        const laplace_digest256& context) {
        if (count == 0u) {
            status_ = LAPLACE_TABULAR_SOURCE_OVERFLOW;
            return std::nullopt;
        }
        const std::uint64_t index =
            static_cast<std::uint64_t>(plan_.requests.size());
        plan_.requests.push_back(laplace_composition_request{
            first, static_cast<std::uint64_t>(count), index + 1u,
            RecipeVersion, 0u,
            input_.profile_declaration.recipe_program_fingerprint,
            input_.geometry_epoch, context});
        return index;
    }

    laplace_digest256 ArtifactContext(
        const laplace_tabular_artifact& artifact) const {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        constexpr std::string_view domain{"laplace-tabular-artifact-context-v1"};
        HashBytes(hasher, domain.data(), domain.size());
        HashBytes(
            hasher, input_.occurrence_context_fingerprint.bytes,
            sizeof(input_.occurrence_context_fingerprint.bytes));
        HashBytes(hasher, artifact.artifact_id.bytes, 32u);
        return Finish(hasher);
    }

    laplace_digest256 SourceFingerprint(const laplace_digest256& graph) const {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        constexpr std::string_view domain{LAPLACE_TABULAR_SOURCE_DOMAIN};
        HashBytes(hasher, domain.data(), domain.size());
        HashBytes(hasher, graph.bytes, sizeof(graph.bytes));
        HashBytes(
            hasher,
            input_.profile_declaration.selected_boundary_fingerprint.bytes,
            sizeof(input_.profile_declaration.selected_boundary_fingerprint.bytes));
        HashBytes(
            hasher,
            input_.profile_declaration.recipe_program_fingerprint.bytes,
            sizeof(input_.profile_declaration.recipe_program_fingerprint.bytes));
        return Finish(hasher);
    }

    std::optional<std::uint64_t> BuildArtifact(
        const laplace_tabular_artifact& artifact,
        const std::size_t artifact_index,
        const std::uint64_t artifact_tag,
        const std::uint64_t raw_tag,
        const std::uint64_t delimited_tag,
        const std::uint64_t field_tag,
        const std::uint64_t header_tag,
        const std::uint64_t record_tag,
        const std::uint64_t lf_tag,
        const std::uint64_t crlf_tag) {
        ParsedArtifact parsed;
        parsed.name.assign(
            artifact.name,
            static_cast<std::size_t>(artifact.name_byte_count));
        parsed.mode = artifact.mode;
        parsed.delimiter = artifact.delimiter;
        parsed.terminator = artifact.line_terminator;
        parsed.exact_distribution =
            (artifact.flags & LAPLACE_TABULAR_ARTIFACT_EXACT_DISTRIBUTION) != 0u;
        const laplace_digest256 context = ArtifactContext(artifact);
        const auto name = Text(
            reinterpret_cast<const std::uint8_t*>(parsed.name.data()),
            parsed.name.size(), context, true);
        const std::string digest_text =
            Hex(artifact.expected_sha256, sizeof(artifact.expected_sha256));
        const auto digest = Text(
            reinterpret_cast<const std::uint8_t*>(digest_text.data()),
            digest_text.size(), context, true);
        if (!name || !digest) {
            return std::nullopt;
        }
        std::vector<std::pair<std::uint64_t, Role>> artifact_children{{
            artifact_tag, Role::Tag},
            {*name, Role::ArtifactName},
            {*digest, Role::ArtifactDigest}};
        if (!IdZero(artifact.parent_artifact_id)) {
            const std::string parent_text = Hex(
                artifact.parent_artifact_id.bytes,
                sizeof(artifact.parent_artifact_id.bytes));
            const auto parent = Text(
                reinterpret_cast<const std::uint8_t*>(parent_text.data()),
                parent_text.size(), context, true);
            if (!parent) {
                return std::nullopt;
            }
            artifact_children.emplace_back(*parent, Role::ParentArtifact);
        }
        if (artifact.mode == LAPLACE_TABULAR_ARTIFACT_RAW) {
            artifact_children.emplace_back(raw_tag, Role::Tag);
            if (parsed.exact_distribution) {
                parsed.raw_octets.reserve(
                    static_cast<std::size_t>(artifact.byte_count));
                for (std::size_t index = 0u;
                     index < static_cast<std::size_t>(artifact.byte_count);
                     ++index) {
                    parsed.raw_octets.push_back(artifact.bytes[index]);
                }
                const auto raw = Raw(parsed.raw_octets, context);
                if (!raw) {
                    return std::nullopt;
                }
                artifact_children.emplace_back(*raw, Role::Value);
            }
        } else {
            artifact_children.emplace_back(delimited_tag, Role::Tag);
            const std::uint8_t delimiter_byte =
                static_cast<std::uint8_t>(artifact.delimiter);
            const auto delimiter = Text(
                &delimiter_byte, 1u, context, true);
            const std::uint64_t terminator =
                artifact.line_terminator == LAPLACE_TABULAR_TERMINATOR_LF
                ? lf_tag : crlf_tag;
            if (!delimiter ||
                !ParseDelimited(
                    artifact, artifact_index, context, parsed,
                    field_tag, header_tag,
                    record_tag, terminator, artifact_children)) {
                return std::nullopt;
            }
            artifact_children.emplace_back(*delimiter, Role::Column);
            artifact_children.emplace_back(terminator, Role::Terminator);
        }
#if defined(LAPLACE_TEST_TABULAR_CORRUPT_PARSED_RECONSTRUCTION)
        /* Deliberate defect: the retained parsed representation no longer
         * reconstructs the admitted artifact.  The reconstruction verifier
         * must observe this representation rather than consult input bytes. */
        if (parsed.exact_distribution) {
            if (!parsed.raw_octets.empty()) {
                parsed.raw_octets.front() ^= 1u;
            } else {
                for (auto& row : parsed.rows) {
                    for (auto& field : row) {
                        if (!field.empty()) {
                            field.front() ^= 1u;
                            goto parsed_reconstruction_corrupted;
                        }
                    }
                }
            }
        }
parsed_reconstruction_corrupted:
#endif
        const auto result = Node(artifact_children, context);
        if (result) {
            plan_.artifacts.push_back(std::move(parsed));
            (void)artifact_index;
        }
        return result;
    }

    bool ParseDelimited(
        const laplace_tabular_artifact& artifact,
        const std::size_t artifact_index,
        const laplace_digest256& context,
        ParsedArtifact& parsed,
        const std::uint64_t field_tag,
        const std::uint64_t header_tag,
        const std::uint64_t record_tag,
        const std::uint64_t terminator_tag,
        std::vector<std::pair<std::uint64_t, Role>>& artifact_children) {
        const std::size_t byte_count =
            static_cast<std::size_t>(artifact.byte_count);
        const std::size_t terminator_bytes =
            artifact.line_terminator == LAPLACE_TABULAR_TERMINATOR_LF ? 1u : 2u;
        std::size_t line_start = 0u;
        std::vector<std::uint64_t> columns;
        std::vector<std::vector<std::uint32_t>> declared_columns;
        columns.reserve(artifact.expected_column_count);
        declared_columns.reserve(artifact.expected_column_count);
        for (std::size_t index = 0u;
             index < artifact.expected_column_count; ++index) {
            const auto& declaration = artifact.columns[index];
            std::vector<std::uint32_t> positions;
            if (!DecodeUtf8(
                    declaration.bytes,
                    static_cast<std::size_t>(declaration.byte_count),
                    positions) || positions.empty()) {
                status_ = LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
                return false;
            }
            const auto column = Text(
                declaration.bytes,
                static_cast<std::size_t>(declaration.byte_count),
                context, true);
            if (!column) {
                return false;
            }
            columns.push_back(*column);
            declared_columns.push_back(std::move(positions));
        }
        while (line_start < byte_count) {
            std::size_t line_end = line_start;
            while (line_end < byte_count && artifact.bytes[line_end] != '\n') {
                ++line_end;
            }
            if (line_end == byte_count ||
                (terminator_bytes == 2u &&
                 (line_end == line_start || artifact.bytes[line_end - 1u] != '\r')) ||
                (terminator_bytes == 1u && line_end > line_start &&
                 artifact.bytes[line_end - 1u] == '\r')) {
                status_ = LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
                return false;
            }
            const std::size_t content_end = line_end - (terminator_bytes - 1u);
            std::vector<std::vector<std::uint32_t>> fields;
            std::vector<std::pair<std::size_t, std::size_t>> field_spans;
            std::size_t field_start = line_start;
            for (std::size_t offset = line_start; offset <= content_end; ++offset) {
                if (offset == content_end || artifact.bytes[offset] == artifact.delimiter) {
                    std::vector<std::uint32_t> positions;
                    if (!DecodeUtf8(
                            artifact.bytes + field_start,
                            offset - field_start, positions)) {
                        status_ = LAPLACE_TABULAR_SOURCE_UTF8_INVALID;
                        return false;
                    }
                    fields.push_back(std::move(positions));
                    field_spans.emplace_back(field_start, offset);
                    field_start = offset + 1u;
                }
            }
            if (fields.size() != artifact.expected_column_count) {
                status_ = LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
                return false;
            }
            const bool header =
                parsed.rows.size() < artifact.header_record_count;
            const std::uint64_t data_row_ordinal =
                header ? 0u : static_cast<std::uint64_t>(
                    parsed.rows.size() - artifact.header_record_count + 1u);
            std::vector<std::pair<std::uint64_t, Role>> row_children{{
                header ? header_tag : record_tag,
                header ? Role::Header : Role::Tag}};
            struct PendingReference {
                std::uint64_t value_result_index;
                std::uint64_t field_result_index;
                const laplace_tabular_reference_rule* rule;
                std::size_t column_index;
            };
            std::vector<PendingReference> pending_references;
            std::vector<std::uint64_t> row_reference_indexes(
                fields.size(), std::numeric_limits<std::uint64_t>::max());
            for (std::size_t field_index = 0u;
                 field_index < fields.size(); ++field_index) {
                std::optional<std::uint64_t> value;
                const auto& span = field_spans[field_index];
                if (span.first != span.second) {
                    value = Text(
                        artifact.bytes + span.first,
                        span.second - span.first, context, true);
                    if (!value) {
                        return false;
                    }
                }
                std::vector<std::pair<std::uint64_t, Role>> field_children{{
                    field_tag, Role::Tag}};
                if (header) {
                    if (!value || fields[field_index] !=
                                      declared_columns[field_index]) {
                        status_ = LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID;
                        return false;
                    }
                    field_children.emplace_back(
                        columns[field_index], Role::Column);
                } else {
                    field_children.emplace_back(
                        columns[field_index], Role::Column);
                    if (value) {
                        field_children.emplace_back(*value, Role::Value);
                    }
                    if (((artifact.reference_column_mask >> field_index) & 1u) != 0u &&
                        value && !Add(plan_.view.profile.reference_count, 1u)) {
                        status_ = LAPLACE_TABULAR_SOURCE_OVERFLOW;
                        return false;
                    }
                }
                const auto field_node = Node(field_children, context);
                if (!field_node) {
                    return false;
                }
                row_children.emplace_back(*field_node, Role::Field);
#if defined(LAPLACE_TEST_TABULAR_PROMOTE_FIELDS_TO_CLAIMS)
                /* Deliberate defect: structural field nodes are promoted into
                 * semantic testimony even though only the profile-declared
                 * record proposition owns that epistemic role. */
                if (!header) {
                    plan_.claim_result_indexes.push_back(*field_node);
                    plan_.claim_source_ordinals.push_back(
                        plan_.requests[static_cast<std::size_t>(*field_node)]
                            .source_ordinal);
                    plan_.claim_outcome_types.push_back(artifact.outcome_type);
                }
#endif
                if (!header && value) {
                    const auto* rule = ReferenceRule(artifact_index, field_index);
                    if (rule != nullptr) {
                        pending_references.push_back(PendingReference{
                            *value, *field_node, rule, field_index});
                    }
                }
            }
            row_children.emplace_back(terminator_tag, Role::Terminator);
            const auto row = Node(row_children, context);
            if (!row) {
                return false;
            }
            artifact_children.emplace_back(
                *row, header ? Role::Header : Role::Record);
            if (!header) {
                plan_.claim_result_indexes.push_back(*row);
                plan_.claim_source_ordinals.push_back(
                    plan_.requests[static_cast<std::size_t>(*row)].source_ordinal);
                plan_.claim_outcome_types.push_back(artifact.outcome_type);
                for (const auto& pending : pending_references) {
                    row_reference_indexes[pending.column_index] =
                        static_cast<std::uint64_t>(
                            plan_.reference_occurrences.size());
                    plan_.reference_occurrences.push_back(
                        laplace_tabular_reference_occurrence{
                            pending.rule->name_space,
                            pending.value_result_index,
                            pending.field_result_index,
                            *row,
                            plan_.requests[static_cast<std::size_t>(*row)].source_ordinal,
                            static_cast<std::uint64_t>(artifact_index + 1u),
                            data_row_ordinal,
                            static_cast<std::uint64_t>(pending.column_index + 1u),
                            pending.rule->kind,
                            pending.rule->flags});
                }
                for (std::size_t mapping_index = 0u;
                     mapping_index <
                         static_cast<std::size_t>(input_.mapping_rule_count);
                     ++mapping_index) {
                    const auto& rule = input_.mapping_rules[mapping_index];
                    if (rule.artifact_index != artifact_index) {
                        continue;
                    }
                    const std::uint64_t left =
                        row_reference_indexes[rule.left_column_index];
                    const std::uint64_t right =
                        row_reference_indexes[rule.right_column_index];
                    if (left == std::numeric_limits<std::uint64_t>::max() ||
                        right == std::numeric_limits<std::uint64_t>::max() ||
                        !Add(plan_.view.profile.mapping_count, 1u)) {
                        status_ = left ==
                                std::numeric_limits<std::uint64_t>::max() ||
                            right == std::numeric_limits<std::uint64_t>::max()
                            ? LAPLACE_TABULAR_SOURCE_GRAMMAR_INVALID
                            : LAPLACE_TABULAR_SOURCE_OVERFLOW;
                        return false;
                    }
                    plan_.mapping_occurrences.push_back(
                        laplace_tabular_mapping_occurrence{
                            mapping_relation_indexes_[mapping_index],
                            left,
                            right,
                            *row,
                            plan_.requests[static_cast<std::size_t>(*row)]
                                .source_ordinal,
                            static_cast<std::uint64_t>(artifact_index + 1u),
                            data_row_ordinal,
                            rule.relation_version,
                            rule.relation_kind,
                            rule.flags});
                }
            }
            parsed.rows.push_back(std::move(fields));
            line_start = line_end + 1u;
        }
        std::uint64_t field_count{};
        if (parsed.rows.size() != artifact.expected_record_count ||
            artifact.expected_record_count >
                std::numeric_limits<std::uint64_t>::max() /
                artifact.expected_column_count) {
            status_ = LAPLACE_TABULAR_SOURCE_DENOMINATOR_MISMATCH;
            return false;
        }
        field_count = artifact.expected_record_count *
            artifact.expected_column_count;
        if (field_count != artifact.expected_field_count) {
            status_ = LAPLACE_TABULAR_SOURCE_DENOMINATOR_MISMATCH;
            return false;
        }
        return Add(plan_.view.profile.record_count, artifact.expected_record_count) &&
            Add(plan_.view.profile.field_count, field_count) &&
            Add(
                plan_.view.profile.claim_count,
                artifact.expected_record_count - artifact.header_record_count);
    }

    bool FinalizeProfile() {
        laplace_source_profile_manifest& profile = plan_.view.profile;
        for (std::size_t index = 0u;
             index < static_cast<std::size_t>(input_.artifact_count); ++index) {
            const laplace_tabular_artifact& artifact = input_.artifacts[index];
            if (!Add(profile.byte_count, artifact.byte_count) ||
                !Add(profile.file_count, 1u) ||
                ((artifact.flags & LAPLACE_TABULAR_ARTIFACT_CONTAINER) != 0u &&
                 !Add(profile.container_count, 1u)) ||
                ((artifact.flags & LAPLACE_TABULAR_ARTIFACT_MEMBER) != 0u &&
                 !Add(profile.member_count, 1u))) {
                status_ = LAPLACE_TABULAR_SOURCE_OVERFLOW;
                return false;
            }
        }
        profile.edge_count = static_cast<std::uint64_t>(plan_.operands.size());
        profile.output_count = static_cast<std::uint64_t>(plan_.requests.size());
        profile.syntax_node_count = profile.output_count;
        if (!Add(profile.syntax_node_count, profile.edge_count) ||
            !Add(profile.span_count, profile.field_count) ||
            !Add(profile.span_count, profile.record_count) ||
            !Add(profile.span_count, profile.file_count)) {
            status_ = LAPLACE_TABULAR_SOURCE_OVERFLOW;
            return false;
        }
        profile.closure_subject_count = profile.output_count;
        if (!Add(profile.closure_subject_count, profile.reference_count) ||
            !Add(profile.closure_subject_count, profile.mapping_count)) {
            status_ = LAPLACE_TABULAR_SOURCE_OVERFLOW;
            return false;
        }
        /* Structural capture is not reference resolution.  The generic
         * syntax recipe preserves each marked carrier but leaves it
         * explicitly unresolved until a typed Highway topology operation
         * resolves or rejects that authority-scoped endpoint. */
#if defined(LAPLACE_TEST_TABULAR_PROMOTE_REFERENCES_RESOLVED)
        profile.accepted_count = profile.reference_count;
        if (!Add(profile.accepted_count, profile.mapping_count)) {
            status_ = LAPLACE_TABULAR_SOURCE_OVERFLOW;
            return false;
        }
#else
        profile.unresolved_count = profile.reference_count;
        if (!Add(profile.unresolved_count, profile.mapping_count)) {
            status_ = LAPLACE_TABULAR_SOURCE_OVERFLOW;
            return false;
        }
#endif
        if (profile.reconstruction_class ==
            LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_EXACT) {
            profile.persisted_count = profile.output_count;
        } else if (profile.reconstruction_class ==
                   LAPLACE_SOURCE_PROFILE_RECONSTRUCTION_SEMANTIC) {
            profile.transformation_count = profile.output_count;
            profile.transformed_count = profile.output_count;
        } else {
            profile.lossy_count = profile.output_count;
        }
        if (profile.claim_count != plan_.claim_result_indexes.size() ||
            profile.reference_count != plan_.reference_occurrences.size() ||
            profile.mapping_count != plan_.mapping_occurrences.size() ||
            profile.file_count != input_.artifact_count ||
            profile.record_count == 0u || profile.field_count == 0u ||
            profile.output_count == 0u) {
            status_ = LAPLACE_TABULAR_SOURCE_DENOMINATOR_MISMATCH;
            return false;
        }
        return true;
    }

    bool VerifyReconstruction() {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        constexpr std::string_view domain{LAPLACE_TABULAR_RECONSTRUCTION_DOMAIN};
        HashBytes(hasher, domain.data(), domain.size());
        HashU64(hasher, input_.artifact_count);
        for (std::size_t index = 0u; index < plan_.artifacts.size(); ++index) {
            HashU32(hasher, plan_.artifacts[index].exact_distribution ? 1u : 0u);
            if (!plan_.artifacts[index].exact_distribution) {
                HashBytes(
                    hasher, input_.artifacts[index].artifact_id.bytes,
                    sizeof(input_.artifacts[index].artifact_id.bytes));
                continue;
            }
            std::vector<std::uint8_t> output;
            Recompose(plan_.artifacts[index], output);
#if defined(LAPLACE_TEST_TABULAR_RECOMPOSE_FROM_INPUT)
            output.assign(
                input_.artifacts[index].bytes,
                input_.artifacts[index].bytes +
                    static_cast<std::size_t>(input_.artifacts[index].byte_count));
#endif
            if (output.size() != input_.artifacts[index].byte_count ||
                std::memcmp(
                    output.data(), input_.artifacts[index].bytes,
                    output.size()) != 0) {
                status_ = LAPLACE_TABULAR_SOURCE_RECONSTRUCTION_MISMATCH;
                return false;
            }
            HashBytes(hasher, output.data(), output.size());
        }
        plan_.view.reconstruction_fingerprint = Finish(hasher);
        return true;
    }

public:
    static void Recompose(
        const ParsedArtifact& artifact,
        std::vector<std::uint8_t>& output) {
        if (artifact.mode == LAPLACE_TABULAR_ARTIFACT_RAW) {
            output.reserve(artifact.raw_octets.size());
            for (const std::uint32_t value : artifact.raw_octets) {
                output.push_back(static_cast<std::uint8_t>(value));
            }
            return;
        }
        for (const auto& row : artifact.rows) {
            for (std::size_t field_index = 0u;
                 field_index < row.size(); ++field_index) {
                if (field_index != 0u) {
                    output.push_back(static_cast<std::uint8_t>(artifact.delimiter));
                }
                for (const std::uint32_t value : row[field_index]) {
                    EncodeUtf8(value, output);
                }
            }
            if (artifact.terminator == LAPLACE_TABULAR_TERMINATOR_CRLF) {
                output.push_back('\r');
            }
            output.push_back('\n');
        }
    }

private:
    laplace_tabular_source_plan& plan_;
    const laplace_tabular_source_input& input_;
    std::map<std::uint32_t, std::uint64_t> atom_indexes_;
    std::map<std::string, std::uint64_t> text_indexes_;
    std::map<std::string, std::uint64_t> tag_indexes_;
    std::vector<std::uint64_t> mapping_relation_indexes_;
    laplace_tabular_source_status status_{LAPLACE_TABULAR_SOURCE_OK};
};

void BindView(laplace_tabular_source_plan& plan) {
    plan.view.atom_positions = plan.atom_positions.data();
    plan.view.operands = plan.operands.data();
    plan.view.requests = plan.requests.data();
    plan.view.claim_result_indexes = plan.claim_result_indexes.data();
    plan.view.claim_source_ordinals = plan.claim_source_ordinals.data();
    plan.view.claim_outcome_types = plan.claim_outcome_types.data();
    plan.view.artifact_root_result_indexes =
        plan.artifact_root_result_indexes.data();
    plan.view.reference_occurrences = plan.reference_occurrences.data();
    plan.view.mapping_occurrences = plan.mapping_occurrences.data();
    plan.view.atom_count = plan.atom_positions.size();
    plan.view.operand_count = plan.operands.size();
    plan.view.request_count = plan.requests.size();
    plan.view.claim_count = plan.claim_result_indexes.size();
    plan.view.artifact_count = plan.artifact_root_result_indexes.size();
    plan.view.reference_occurrence_count = plan.reference_occurrences.size();
    plan.view.mapping_occurrence_count = plan.mapping_occurrences.size();
    plan.view.recipe_version = RecipeVersion;
}

}  // namespace

extern "C" laplace_tabular_source_status laplace_tabular_artifact_graph_identify(
    const laplace_tabular_artifact* artifacts,
    const size_t artifact_count,
    laplace_digest256* artifact_graph_fingerprint) {
    if (artifact_graph_fingerprint == nullptr) {
        return LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT;
    }
    return ArtifactGraph(artifacts, artifact_count, *artifact_graph_fingerprint);
}

extern "C" laplace_tabular_source_status laplace_tabular_source_graph_identify(
    const laplace_tabular_artifact* artifacts,
    const size_t artifact_count,
    const laplace_tabular_reference_rule* reference_rules,
    const size_t reference_rule_count,
    const laplace_tabular_mapping_rule* mapping_rules,
    const size_t mapping_rule_count,
    laplace_digest256* source_graph_fingerprint) {
    if (source_graph_fingerprint == nullptr) {
        return LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT;
    }
    return SourceGraph(
        artifacts, artifact_count, reference_rules, reference_rule_count,
        mapping_rules, mapping_rule_count, *source_graph_fingerprint);
}

extern "C" laplace_tabular_source_status laplace_tabular_source_plan_create(
    const laplace_tabular_source_input* input,
    laplace_tabular_source_plan** plan) {
    if (input == nullptr || plan == nullptr || *plan != nullptr) {
        return LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT;
    }
    try {
        auto* created = new laplace_tabular_source_plan{};
        PlanBuilder builder(*created, *input);
        const laplace_tabular_source_status status = builder.Build();
        if (status != LAPLACE_TABULAR_SOURCE_OK) {
            delete created;
            return status;
        }
        BindView(*created);
        *plan = created;
        return LAPLACE_TABULAR_SOURCE_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_TABULAR_SOURCE_MEMORY_FAILURE;
    }
}

extern "C" laplace_tabular_source_status laplace_tabular_source_plan_view_get(
    const laplace_tabular_source_plan* plan,
    laplace_tabular_source_plan_view* view) {
    if (plan == nullptr || view == nullptr || plan->view.atom_count == 0u ||
        plan->view.operand_count == 0u || plan->view.request_count == 0u ||
        plan->view.claim_count == 0u || plan->view.artifact_count == 0u ||
        plan->view.root_result_index + 1u != plan->view.request_count ||
        plan->view.atom_positions == nullptr || plan->view.operands == nullptr ||
        plan->view.requests == nullptr ||
        plan->view.claim_result_indexes == nullptr ||
        plan->view.claim_source_ordinals == nullptr ||
        plan->view.claim_outcome_types == nullptr ||
        plan->view.artifact_root_result_indexes == nullptr ||
        (plan->view.reference_occurrence_count != 0u &&
         plan->view.reference_occurrences == nullptr) ||
        (plan->view.mapping_occurrence_count != 0u &&
         plan->view.mapping_occurrences == nullptr)) {
        return LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT;
    }
    *view = plan->view;
    return LAPLACE_TABULAR_SOURCE_OK;
}

extern "C" laplace_tabular_source_status
laplace_tabular_source_profile_finalize(
    const laplace_tabular_source_plan* plan,
    const laplace_composition_working_set_summary* composition,
    laplace_source_profile_manifest* profile) {
    if (plan == nullptr || composition == nullptr || profile == nullptr ||
        composition->status != LAPLACE_COMPOSITION_OK ||
        composition->request_count != plan->view.request_count ||
        composition->operand_count != plan->view.operand_count ||
        composition->logical_occurrence_count == 0u ||
        !DigestEqual(
            composition->source_fingerprint,
            plan->view.source_fingerprint) ||
        !DigestEqual(
            composition->calculation_recipe_fingerprint,
            plan->view.profile.recipe_program_fingerprint)) {
        return LAPLACE_TABULAR_SOURCE_DENOMINATOR_MISMATCH;
    }
    *profile = plan->view.profile;
    profile->occurrence_count = composition->logical_occurrence_count;
    if (laplace_source_profile_identify(profile, &profile->profile_id) !=
            LAPLACE_SOURCE_PROFILE_OK) {
        return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }
    laplace_source_profile_receipt receipt{};
    laplace_source_profile_error error{};
    if (laplace_source_profile_validate_batch(
            profile, 1u, &receipt, &error) !=
        LAPLACE_SOURCE_PROFILE_OK) {
        return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }
    return LAPLACE_TABULAR_SOURCE_OK;
}

extern "C" laplace_tabular_source_status
laplace_tabular_source_recompose_artifact(
    const laplace_tabular_source_plan* plan,
    const size_t artifact_index,
    uint8_t* output,
    const size_t output_capacity,
    size_t* output_bytes) {
    if (plan == nullptr || output_bytes == nullptr ||
        artifact_index >= plan->artifacts.size()) {
        return LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT;
    }
    if (!plan->artifacts[artifact_index].exact_distribution) {
        return LAPLACE_TABULAR_SOURCE_RECONSTRUCTION_UNAVAILABLE;
    }
    std::vector<std::uint8_t> recomposed;
    PlanBuilder::Recompose(plan->artifacts[artifact_index], recomposed);
    *output_bytes = recomposed.size();
    if (output == nullptr || output_capacity < recomposed.size()) {
        return LAPLACE_TABULAR_SOURCE_INVALID_ARGUMENT;
    }
    if (!recomposed.empty()) {
        std::memcpy(output, recomposed.data(), recomposed.size());
    }
    return LAPLACE_TABULAR_SOURCE_OK;
}

extern "C" void laplace_tabular_source_plan_destroy(
    laplace_tabular_source_plan** plan) {
    if (plan == nullptr) {
        return;
    }
    delete *plan;
    *plan = nullptr;
}
