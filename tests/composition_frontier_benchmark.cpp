#include "laplace/composition_execution.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "context_fixture.h"

namespace {

constexpr std::uint64_t BenchmarkMemoryBytes = UINT64_C(2147483648);

void Fill(laplace_digest256& digest, const std::uint8_t seed) {
    for (std::size_t index = 0U; index < sizeof(digest.bytes); ++index) {
        digest.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
}

std::string Hex(const std::uint8_t* const bytes, const std::size_t count) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(count * 2U, '0');
    for (std::size_t index = 0U; index < count; ++index) {
        result[index * 2U] = digits[bytes[index] >> 4U];
        result[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
    }
    return result;
}

bool SameDigest(
    const laplace_digest256& left,
    const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool Add(std::uint64_t& total, const std::uint64_t value) {
    if (value > std::numeric_limits<std::uint64_t>::max() - total) {
        return false;
    }
    total += value;
    return true;
}

bool ParsePositive(
    const char* const raw,
    std::uint64_t& output) {
    if (raw == nullptr || *raw == '\0') {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(raw, &end, 10);
    if (errno != 0 || end == raw || *end != '\0' || parsed == 0U) {
        return false;
    }
    output = static_cast<std::uint64_t>(parsed);
    return true;
}

laplace_composition_known_entity Atom(
    const std::uint32_t position,
    const laplace_point4d& point,
    const std::uint8_t physicality_seed) {
    laplace_composition_known_entity result{};
    if (laplace_identity_codepoint_witness(
            position, &result.entity_id, &result.identity_witness) !=
        LAPLACE_IDENTITY_OK) {
        return laplace_composition_known_entity{};
    }
    Fill(result.physicality_id, physicality_seed);
    result.centroid = point;
    result.atom = position;
    result.has_atom = 1U;
    return result;
}

laplace_composition_status ResolveAllNovel(
    void*,
    const laplace_composition_entity_candidate*,
    const std::size_t entity_candidate_count,
    const laplace_persistence_physicality_record*,
    const std::size_t physicality_candidate_count,
    std::uint8_t* entity_dispositions,
    std::uint8_t* physicality_dispositions,
    laplace_composition_presence_provider_result* result) {
    if (result == nullptr ||
        (entity_candidate_count != 0U && entity_dispositions == nullptr) ||
        (physicality_candidate_count != 0U && physicality_dispositions == nullptr)) {
        return LAPLACE_COMPOSITION_PRESENCE_INVALID;
    }
    for (std::size_t index = 0U; index < entity_candidate_count; ++index) {
        entity_dispositions[index] = LAPLACE_COMPOSITION_NOVEL;
    }
    for (std::size_t index = 0U; index < physicality_candidate_count; ++index) {
        physicality_dispositions[index] = LAPLACE_COMPOSITION_NOVEL;
    }
    *result = laplace_composition_presence_provider_result{};
    Fill(result->provider_fingerprint, 0x51U);
    Fill(result->provider_receipt_id, 0x71U);
    result->returned_entity_count = entity_candidate_count;
    result->returned_physicality_count = physicality_candidate_count;
    result->entity_round_count = entity_candidate_count == 0U ? 0U : 1U;
    result->physicality_round_count = physicality_candidate_count == 0U ? 0U : 1U;
    return LAPLACE_COMPOSITION_OK;
}

struct Fixture final {
    laplace_framework_context context{};
    laplace_digest256 source{};
    laplace_digest256 calculation_recipe{};
    std::array<laplace_composition_known_entity, 2> known{};
    std::vector<laplace_composition_operand> operands;
    std::vector<laplace_composition_request> requests;
    laplace_composition_working_set_input input{};
};

bool BuildFixture(
    const std::uint32_t workers,
    const std::uint64_t request_count,
    Fixture& fixture) {
    if (request_count >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() / 2U)) {
        return false;
    }
    fixture = Fixture{};
    fixture.context = laplace_test_context(0x31U);
    fixture.context.resource_grant.memory_bytes = BenchmarkMemoryBytes;
    fixture.context.resource_grant.cpu_slots = workers;
    fixture.context.resource_grant.io_slots = 2U;
    Fill(fixture.source, 0x21U);
    Fill(fixture.calculation_recipe, 0x41U);
    fixture.known = {{
        Atom(0x41U, laplace_point4d{{1.0, 0.0, 0.0, 0.0}}, 0x61U),
        Atom(0x42U, laplace_point4d{{0.0, 1.0, 0.0, 0.0}}, 0x81U)}};

    fixture.operands.reserve(static_cast<std::size_t>(request_count * 2U));
    fixture.requests.reserve(static_cast<std::size_t>(request_count));
    for (std::uint64_t index = 0U; index < request_count; ++index) {
        laplace_composition_operand first{};
        first.reference_index = 0U;
        first.multiplicity = 1U;
        first.reference_kind = LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY;
        laplace_composition_operand second{};
        second.reference_index = 1U;
        second.multiplicity = 1U;
        second.reference_kind = LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY;
        fixture.operands.push_back(first);
        fixture.operands.push_back(second);

        laplace_composition_request request{};
        request.first_operand = index * 2U;
        request.operand_count = 2U;
        request.source_ordinal = index + 1U;
        request.recipe_version = 1U;
        Fill(request.recipe_fingerprint, 0xA1U);
        Fill(request.geometry_epoch, 0xB1U);
        Fill(request.occurrence_context_fingerprint, 0xC1U);
        fixture.requests.push_back(request);
    }

    fixture.input.context = &fixture.context;
    fixture.input.source_fingerprint = &fixture.source;
    fixture.input.calculation_recipe_fingerprint = &fixture.calculation_recipe;
    fixture.input.known_entities = fixture.known.data();
    fixture.input.known_entity_count = fixture.known.size();
    fixture.input.operands = fixture.operands.data();
    fixture.input.operand_count = fixture.operands.size();
    fixture.input.requests = fixture.requests.data();
    fixture.input.request_count = fixture.requests.size();
    return true;
}

struct Point final {
    laplace_composition_frontier_execution_plan plan{};
    laplace_digest256 semantic_receipt{};
    laplace_digest256 stream_fingerprint{};
    laplace_digest256 provider_fingerprint{};
    std::uint64_t iterations{};
    std::uint64_t completed_items{};
    std::uint64_t completed_chunks{};
    std::uint64_t wall_time_ns{};
    std::uint64_t core_time_ns{};
    std::uint64_t worker_capacity_ns{};
    std::uint64_t idle_wait_capacity_ns{};
};

bool RunOnce(
    Fixture& fixture,
    Point* const point,
    const bool measured) {
    laplace_composition_working_set* working_set = nullptr;
    if (laplace_composition_working_set_create(
            &fixture.input, &working_set) != LAPLACE_COMPOSITION_OK ||
        working_set == nullptr) {
        return false;
    }

    laplace_composition_frontier_execution_postflight postflight{};
    if (laplace_composition_working_set_frontier_execution_postflight_get(
            working_set, &postflight) != LAPLACE_COMPOSITION_OK) {
        laplace_composition_working_set_destroy(&working_set);
        return false;
    }

    laplace_composition_presence_provider_v1 presence_provider{};
    presence_provider.resolve = ResolveAllNovel;
    presence_provider.abi_major = LAPLACE_COMPOSITION_PRESENCE_PROVIDER_ABI;
    presence_provider.abi_minor = LAPLACE_COMPOSITION_ABI_MINOR;
    laplace_composition_presence_receipt presence{};
    if (laplace_composition_working_set_resolve_presence(
            working_set, &presence_provider, &presence) != LAPLACE_COMPOSITION_OK) {
        laplace_composition_working_set_destroy(&working_set);
        return false;
    }

    laplace_composition_working_set_summary summary{};
    if (laplace_composition_working_set_summary_get(
            working_set, &summary) != LAPLACE_COMPOSITION_OK) {
        laplace_composition_working_set_destroy(&working_set);
        return false;
    }

    std::size_t receipt_count = 0U;
    const auto* receipts =
        laplace_composition_working_set_frontier_execution_receipts(
            working_set, &receipt_count);
    if (receipts == nullptr || receipt_count == 0U) {
        laplace_composition_working_set_destroy(&working_set);
        return false;
    }

    bool okay = true;
    if (point != nullptr && measured) {
        if (point->iterations == 0U) {
            point->semantic_receipt = summary.receipt_id;
            point->stream_fingerprint = summary.stream_fingerprint;
            point->provider_fingerprint = receipts[0].provider_fingerprint;
        } else if (!SameDigest(point->semantic_receipt, summary.receipt_id) ||
                   !SameDigest(point->stream_fingerprint, summary.stream_fingerprint) ||
                   !SameDigest(point->provider_fingerprint, receipts[0].provider_fingerprint)) {
            okay = false;
        }
        okay = okay && Add(point->completed_items, postflight.completed_items) &&
            Add(point->completed_chunks, postflight.completed_chunks) &&
            Add(point->wall_time_ns, postflight.wall_time_ns) &&
            Add(point->core_time_ns, postflight.core_time_ns) &&
            Add(point->worker_capacity_ns, postflight.worker_capacity_ns) &&
            Add(point->idle_wait_capacity_ns, postflight.idle_wait_capacity_ns);
        if (okay) {
            ++point->iterations;
        }
    }
    laplace_composition_working_set_destroy(&working_set);
    return okay;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: composition-frontier-benchmark WORKERS REQUESTS ITERATIONS\n";
        return 2;
    }
    std::uint64_t workers_raw{};
    std::uint64_t request_count{};
    std::uint64_t iterations{};
    if (!ParsePositive(argv[1], workers_raw) ||
        !ParsePositive(argv[2], request_count) ||
        !ParsePositive(argv[3], iterations) ||
        workers_raw > std::numeric_limits<std::uint32_t>::max()) {
        std::cerr << "benchmark arguments must be positive bounded integers\n";
        return 2;
    }
    const auto workers = static_cast<std::uint32_t>(workers_raw);

    Fixture fixture{};
    if (!BuildFixture(workers, request_count, fixture)) {
        std::cerr << "failed to build benchmark fixture\n";
        return 3;
    }

    Point point{};
    if (laplace_composition_frontier_execution_plan_build(
            &fixture.input, &point.plan) != LAPLACE_COMPOSITION_OK) {
        std::cerr << "failed to build benchmark execution plan\n";
        return 4;
    }
    if (!RunOnce(fixture, nullptr, false)) {
        std::cerr << "benchmark warmup failed\n";
        return 5;
    }
    for (std::uint64_t iteration = 0U; iteration < iterations; ++iteration) {
        if (!RunOnce(fixture, &point, true)) {
            std::cerr << "benchmark measured iteration failed or semantic identity drifted\n";
            return 6;
        }
    }
    if (point.iterations != iterations || point.wall_time_ns == 0U) {
        std::cerr << "benchmark produced incomplete timing evidence\n";
        return 7;
    }

    const long double throughput =
        static_cast<long double>(point.completed_items) * 1000000000.0L /
        static_cast<long double>(point.wall_time_ns);
    const std::uint64_t bounded_core =
        std::min(point.core_time_ns, point.worker_capacity_ns);
    const long double efficiency = point.worker_capacity_ns == 0U
        ? 0.0L
        : static_cast<long double>(bounded_core) /
            static_cast<long double>(point.worker_capacity_ns);

    std::cout << std::fixed << std::setprecision(6)
        << "{\n"
        << "  \"schema\": \"laplace.composition-frontier-benchmark-point/v1\",\n"
        << "  \"workers\": " << workers << ",\n"
        << "  \"request_count\": " << request_count << ",\n"
        << "  \"iterations\": " << iterations << ",\n"
        << "  \"frontier_count\": " << point.plan.frontier_count << ",\n"
        << "  \"dependency_depth\": " << point.plan.dependency_depth << ",\n"
        << "  \"maximum_frontier_width\": " << point.plan.maximum_frontier_width << ",\n"
        << "  \"planned_chunks_per_iteration\": " << point.plan.total_planned_chunks << ",\n"
        << "  \"completed_items\": " << point.completed_items << ",\n"
        << "  \"completed_chunks\": " << point.completed_chunks << ",\n"
        << "  \"wall_time_ns\": " << point.wall_time_ns << ",\n"
        << "  \"core_time_ns\": " << point.core_time_ns << ",\n"
        << "  \"worker_capacity_ns\": " << point.worker_capacity_ns << ",\n"
        << "  \"idle_wait_capacity_ns\": " << point.idle_wait_capacity_ns << ",\n"
        << "  \"throughput_items_per_second\": "
        << static_cast<double>(throughput) << ",\n"
        << "  \"parallel_efficiency\": " << static_cast<double>(efficiency) << ",\n"
        << "  \"plan_fingerprint\": \""
        << Hex(point.plan.plan_fingerprint.bytes, sizeof(point.plan.plan_fingerprint.bytes))
        << "\",\n"
        << "  \"provider_fingerprint\": \""
        << Hex(point.provider_fingerprint.bytes, sizeof(point.provider_fingerprint.bytes))
        << "\",\n"
        << "  \"semantic_receipt\": \""
        << Hex(point.semantic_receipt.bytes, sizeof(point.semantic_receipt.bytes))
        << "\",\n"
        << "  \"stream_fingerprint\": \""
        << Hex(point.stream_fingerprint.bytes, sizeof(point.stream_fingerprint.bytes))
        << "\"\n"
        << "}\n";
    return 0;
}
