#include "laplace/perfcache.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

static_assert(LAPLACE_PERFCACHE_FORMAT_VERSION == 2);
static_assert(LAPLACE_PERFCACHE_IDENTITY_DIGEST_ALL_BIT_PATTERNS_VALID == 1);

int main() {
    const std::array<std::uint8_t, 5> records{{0, 0, 0, 0, 0x2a}};
    laplace_perfcache_spec spec{};
    spec.contract.key_bytes = 4;
    spec.contract.value_bytes = 1;
    spec.contract.access_law = LAPLACE_PERFCACHE_ACCESS_SORTED_UNIQUE_FIXED;
    spec.records = records.data();
    spec.record_count = 1;

    std::size_t measured = 0;
    if (laplace_perfcache_measure(&spec, &measured) != LAPLACE_PERFCACHE_OK) {
        std::fputs("zero-sentinel-regression\n", stderr);
        return 2;
    }
    std::vector<std::uint8_t> artifact(measured);
    std::size_t written = 0;
    if (laplace_perfcache_write(
            &spec, artifact.data(), artifact.size(), &written) !=
            LAPLACE_PERFCACHE_OK ||
        written != measured) {
        std::fputs("zero-sentinel-regression\n", stderr);
        return 2;
    }
    laplace_perfcache_view view{};
    if (laplace_perfcache_validate(
            artifact.data(), artifact.size(), &spec.contract, &view) !=
        LAPLACE_PERFCACHE_OK) {
        std::fputs("zero-sentinel-regression\n", stderr);
        return 2;
    }
    const std::array<std::uint8_t, 4> key{{0, 0, 0, 0}};
    std::uint64_t index = UINT64_MAX;
    std::uint8_t found = 0;
    if (laplace_perfcache_lookup_batch(
            &view, key.data(), 1, &index, &found) != LAPLACE_PERFCACHE_OK ||
        found != 1 || index != 0) {
        std::fputs("zero-sentinel-regression\n", stderr);
        return 2;
    }
    return 0;
}
