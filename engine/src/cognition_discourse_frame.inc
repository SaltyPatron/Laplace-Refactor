#include "laplace/cognition_discourse_frame.h"

#include "blake3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace laplace_cognition_discourse_frame_detail {

constexpr std::array<std::uint8_t, 4> Magic{{'L', 'P', 'D', 'S'}};
constexpr std::uint16_t KindDiscourseState = 1U;
constexpr char PayloadDomain[] = "laplace-cognition-discourse-frame-payload-v1";
constexpr char FrameDomain[] = "laplace-cognition-discourse-frame-v1";

void PutU16(std::uint8_t*& out, const std::uint16_t value) {
    *out++ = static_cast<std::uint8_t>(value);
    *out++ = static_cast<std::uint8_t>(value >> 8U);
}
void PutU32(std::uint8_t*& out, const std::uint32_t value) {
    for (unsigned shift = 0U; shift < 32U; shift += 8U)
        *out++ = static_cast<std::uint8_t>(value >> shift);
}
void PutU64(std::uint8_t*& out, const std::uint64_t value) {
    for (unsigned shift = 0U; shift < 64U; shift += 8U)
        *out++ = static_cast<std::uint8_t>(value >> shift);
}
std::uint16_t GetU16(const std::uint8_t*& in) {
    const std::uint16_t value = static_cast<std::uint16_t>(in[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[1]) << 8U);
    in += 2;
    return value;
}
std::uint32_t GetU32(const std::uint8_t*& in) {
    std::uint32_t value = 0U;
    for (unsigned shift = 0U; shift < 32U; shift += 8U)
        value |= static_cast<std::uint32_t>(*in++) << shift;
    return value;
}
std::uint64_t GetU64(const std::uint8_t*& in) {
    std::uint64_t value = 0U;
    for (unsigned shift = 0U; shift < 64U; shift += 8U)
        value |= static_cast<std::uint64_t>(*in++) << shift;
    return value;
}
void PutDigest(std::uint8_t*& out, const laplace_digest256& value) {
    std::memcpy(out, value.bytes, sizeof(value.bytes));
    out += sizeof(value.bytes);
}
void GetDigest(const std::uint8_t*& in, laplace_digest256& value) {
    std::memcpy(value.bytes, in, sizeof(value.bytes));
    in += sizeof(value.bytes);
}
void PutId(std::uint8_t*& out, const laplace_id128& value) {
    std::memcpy(out, value.bytes, sizeof(value.bytes));
    out += sizeof(value.bytes);
}
void GetId(const std::uint8_t*& in, laplace_id128& value) {
    std::memcpy(value.bytes, in, sizeof(value.bytes));
    in += sizeof(value.bytes);
}
bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

laplace_digest256 Hash(const char* domain, const std::size_t domain_bytes,
                       const std::uint8_t* bytes, const std::size_t byte_count) {
    laplace_digest256 result{};
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, domain_bytes);
    blake3_hasher_update(&hasher, bytes, byte_count);
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

void EncodePayload(const laplace_cognition_discourse_state& s, std::uint8_t* out) {
    PutDigest(out, s.state_id); PutDigest(out, s.discourse_id);
    PutDigest(out, s.previous_state_id); PutId(out, s.observation_entity_id);
    PutDigest(out, s.observation_occurrence_id); PutDigest(out, s.semantic_act_id);
    PutDigest(out, s.active_entity_set_fingerprint);
    PutDigest(out, s.proposition_set_fingerprint); PutDigest(out, s.referent_set_fingerprint);
    PutDigest(out, s.unresolved_question_set_fingerprint); PutDigest(out, s.correction_set_fingerprint);
    PutDigest(out, s.ellipsis_binding_set_fingerprint); PutDigest(out, s.cross_modal_artifact_set_fingerprint);
    PutDigest(out, s.rejected_interpretation_set_fingerprint); PutDigest(out, s.prior_program_set_fingerprint);
    PutDigest(out, s.receipt_set_fingerprint); PutDigest(out, s.world_id);
    PutDigest(out, s.time_fingerprint); PutDigest(out, s.context_fingerprint);
    PutU64(out, s.turn_ordinal); PutU32(out, s.flags); PutU32(out, s.version);
}
void DecodePayload(const std::uint8_t* in, laplace_cognition_discourse_state& s) {
    GetDigest(in, s.state_id); GetDigest(in, s.discourse_id);
    GetDigest(in, s.previous_state_id); GetId(in, s.observation_entity_id);
    GetDigest(in, s.observation_occurrence_id); GetDigest(in, s.semantic_act_id);
    GetDigest(in, s.active_entity_set_fingerprint);
    GetDigest(in, s.proposition_set_fingerprint); GetDigest(in, s.referent_set_fingerprint);
    GetDigest(in, s.unresolved_question_set_fingerprint); GetDigest(in, s.correction_set_fingerprint);
    GetDigest(in, s.ellipsis_binding_set_fingerprint); GetDigest(in, s.cross_modal_artifact_set_fingerprint);
    GetDigest(in, s.rejected_interpretation_set_fingerprint); GetDigest(in, s.prior_program_set_fingerprint);
    GetDigest(in, s.receipt_set_fingerprint); GetDigest(in, s.world_id);
    GetDigest(in, s.time_fingerprint); GetDigest(in, s.context_fingerprint);
    s.turn_ordinal = GetU64(in); s.flags = GetU32(in); s.version = GetU32(in);
}

}  // namespace laplace_cognition_discourse_frame_detail

extern "C" laplace_cognition_discourse_frame_status
laplace_cognition_discourse_frame_encode(
    const laplace_cognition_discourse_state* const state, std::uint8_t* const bytes,
    const std::size_t capacity, std::size_t* const written,
    laplace_cognition_discourse_frame_receipt* const receipt) {
    using namespace laplace_cognition_discourse_frame_detail;
    if (written != nullptr) *written = 0U;
    if (receipt != nullptr) *receipt = laplace_cognition_discourse_frame_receipt{};
    if (state == nullptr || bytes == nullptr || written == nullptr || receipt == nullptr)
        return LAPLACE_COGNITION_DISCOURSE_FRAME_INVALID_ARGUMENT;
    if (capacity < LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES)
        return LAPLACE_COGNITION_DISCOURSE_FRAME_CAPACITY;
    if (laplace_cognition_discourse_state_validate(state) != LAPLACE_COGNITION_DISCOURSE_OK)
        return LAPLACE_COGNITION_DISCOURSE_FRAME_STATE_INVALID;

    std::uint8_t* cursor = bytes;
    std::memcpy(cursor, Magic.data(), Magic.size()); cursor += Magic.size();
    PutU16(cursor, LAPLACE_COGNITION_DISCOURSE_FRAME_VERSION);
    PutU16(cursor, KindDiscourseState);
    PutU32(cursor, LAPLACE_COGNITION_DISCOURSE_FRAME_PAYLOAD_BYTES);
    std::uint8_t* checksum = cursor; cursor += 32U;
    EncodePayload(*state, cursor);
    const auto payload_hash = Hash(PayloadDomain, sizeof(PayloadDomain) - 1U,
        cursor, LAPLACE_COGNITION_DISCOURSE_FRAME_PAYLOAD_BYTES);
    std::memcpy(checksum, payload_hash.bytes, sizeof(payload_hash.bytes));
    *written = LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES;
    receipt->frame_fingerprint = Hash(FrameDomain, sizeof(FrameDomain) - 1U,
        bytes, *written);
    receipt->state_id = state->state_id;
    receipt->frame_bytes = *written;
    receipt->version = LAPLACE_COGNITION_DISCOURSE_FRAME_VERSION;
    return LAPLACE_COGNITION_DISCOURSE_FRAME_OK;
}

extern "C" laplace_cognition_discourse_frame_status
laplace_cognition_discourse_frame_decode(
    const std::uint8_t* const bytes, const std::size_t byte_count,
    laplace_cognition_discourse_state* const state,
    laplace_cognition_discourse_frame_receipt* const receipt) {
    using namespace laplace_cognition_discourse_frame_detail;
    if (state != nullptr) *state = laplace_cognition_discourse_state{};
    if (receipt != nullptr) *receipt = laplace_cognition_discourse_frame_receipt{};
    if (bytes == nullptr || state == nullptr || receipt == nullptr)
        return LAPLACE_COGNITION_DISCOURSE_FRAME_INVALID_ARGUMENT;
    if (byte_count != LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES)
        return LAPLACE_COGNITION_DISCOURSE_FRAME_HEADER_INVALID;
    const std::uint8_t* cursor = bytes;
    if (std::memcmp(cursor, Magic.data(), Magic.size()) != 0)
        return LAPLACE_COGNITION_DISCOURSE_FRAME_HEADER_INVALID;
    cursor += Magic.size();
    if (GetU16(cursor) != LAPLACE_COGNITION_DISCOURSE_FRAME_VERSION ||
        GetU16(cursor) != KindDiscourseState ||
        GetU32(cursor) != LAPLACE_COGNITION_DISCOURSE_FRAME_PAYLOAD_BYTES)
        return LAPLACE_COGNITION_DISCOURSE_FRAME_HEADER_INVALID;
    laplace_digest256 stored_checksum{}; GetDigest(cursor, stored_checksum);
    const auto payload_hash = Hash(PayloadDomain, sizeof(PayloadDomain) - 1U,
        cursor, LAPLACE_COGNITION_DISCOURSE_FRAME_PAYLOAD_BYTES);
    if (!SameDigest(stored_checksum, payload_hash))
        return LAPLACE_COGNITION_DISCOURSE_FRAME_CHECKSUM_MISMATCH;
    DecodePayload(cursor, *state);
    if (laplace_cognition_discourse_state_validate(state) != LAPLACE_COGNITION_DISCOURSE_OK) {
        *state = laplace_cognition_discourse_state{};
        return LAPLACE_COGNITION_DISCOURSE_FRAME_IDENTITY_MISMATCH;
    }
    receipt->frame_fingerprint = Hash(FrameDomain, sizeof(FrameDomain) - 1U,
        bytes, byte_count);
    receipt->state_id = state->state_id;
    receipt->frame_bytes = byte_count;
    receipt->version = LAPLACE_COGNITION_DISCOURSE_FRAME_VERSION;
    return LAPLACE_COGNITION_DISCOURSE_FRAME_OK;
}
