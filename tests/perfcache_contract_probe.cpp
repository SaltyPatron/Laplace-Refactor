#include "laplace/perfcache.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

void Fill(std::uint8_t* destination, std::size_t count, std::uint8_t seed) {
    for (std::size_t index = 0; index < count; ++index) {
        destination[index] = static_cast<std::uint8_t>(seed + index);
    }
}

laplace_perfcache_contract Contract() {
    laplace_perfcache_contract contract{};
    Fill(contract.module_id.bytes, sizeof(contract.module_id.bytes), 0x10u);
    Fill(contract.key_schema_id.bytes, sizeof(contract.key_schema_id.bytes), 0x20u);
    Fill(contract.value_schema_id.bytes, sizeof(contract.value_schema_id.bytes), 0x30u);
    Fill(contract.activation_epoch_id.bytes,
         sizeof(contract.activation_epoch_id.bytes), 0x40u);
    Fill(contract.source_fingerprint.bytes,
         sizeof(contract.source_fingerprint.bytes), 0x50u);
    Fill(contract.recipe_fingerprint.bytes,
         sizeof(contract.recipe_fingerprint.bytes), 0x60u);
    Fill(contract.dependency_fingerprint.bytes,
         sizeof(contract.dependency_fingerprint.bytes), 0x70u);
    contract.key_bytes = 1u;
    contract.value_bytes = 1u;
    contract.access_law = LAPLACE_PERFCACHE_ACCESS_SORTED_UNIQUE_FIXED;
    return contract;
}

}  // namespace

int main() {
    const std::array<std::uint8_t, 4> unsorted_records{{2u, 20u, 1u, 10u}};
    const std::array<std::uint8_t, 2> metadata{{'{', '}'}};
    const laplace_perfcache_spec spec{
        Contract(),
        unsorted_records.data(),
        2u,
        metadata.data(),
        metadata.size()
    };
    std::array<std::uint8_t, 512> artifact{};
    std::size_t artifact_bytes = 0;
    const auto status = laplace_perfcache_write(
        &spec, artifact.data(), artifact.size(), &artifact_bytes);
    if (status == LAPLACE_PERFCACHE_KEYS_NOT_SORTED_UNIQUE) {
        return 0;
    }
    if (status == LAPLACE_PERFCACHE_OK) {
        std::fputs("perfcache-unsorted-accepted\n", stderr);
        return 2;
    }
    std::fputs("perfcache-contract-unexpected-status\n", stderr);
    return 3;
}
