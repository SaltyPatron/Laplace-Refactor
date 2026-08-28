#include "laplace/persistence.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

int main() {
    const laplace_id128 zero_entity{};
    const laplace_digest256 zero_digest{};
    std::array<std::uint8_t, 56> entity_frame{};
    std::size_t written = 0;
    if (laplace_persistence_frame_encode_entity(
            &zero_entity, &zero_digest,
            entity_frame.data(), entity_frame.size(), &written) !=
        LAPLACE_PERSISTENCE_OK) {
        std::puts("zero-bit-pattern-rejected");
        return 2;
    }

    laplace_trajectory_carrier carrier{};
    if (laplace_trajectory_composition_encode(
            &zero_entity, 1u, 1u, 0u, &carrier) != LAPLACE_TRAJECTORY_OK) {
        return 3;
    }
    laplace_persistence_physicality_record physicality{};
    physicality.physicality_type = LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION;
    physicality.vertex_class = LAPLACE_PERSISTENCE_VERTEX_TRAJECTORY_CARRIER;
    physicality.recipe_version = 1u;
    physicality.structural_form =
        LAPLACE_PERSISTENCE_STRUCTURAL_ORDERED_COMPOSITION;
    physicality.dimension_count = LAPLACE_GEOMETRY_COMPONENTS;
    physicality.logical_count = 1u;
    physicality.vertex_count = 1u;
    if (laplace_persistence_physicality_identify(
            &physicality, &physicality.physicality_id) !=
        LAPLACE_PERSISTENCE_OK) {
        std::puts("zero-bit-pattern-rejected");
        return 2;
    }

    std::array<std::uint8_t, 80> trajectory_frame{};
    if (laplace_persistence_frame_encode_trajectory_segment(
            &zero_digest, 0u, &carrier,
            trajectory_frame.data(), trajectory_frame.size(), &written) !=
        LAPLACE_PERSISTENCE_OK) {
        std::puts("zero-bit-pattern-rejected");
        return 2;
    }

    laplace_persistence_attestation_record occurrence{};
    occurrence.source_ordinal = 1u;
    occurrence.flags = LAPLACE_PERSISTENCE_ATTESTATION_HAS_PHYSICALITY;
    occurrence.attestation_kind =
        LAPLACE_PERSISTENCE_ATTESTATION_OBSERVED_OCCURRENCE;
    if (laplace_persistence_attestation_identify(
            &occurrence, &occurrence.attestation_id) !=
        LAPLACE_PERSISTENCE_OK) {
        std::puts("zero-bit-pattern-rejected");
        return 2;
    }
    return 0;
}
