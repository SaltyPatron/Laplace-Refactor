#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


composition_path = Path("engine/src/composition.cpp")
composition = composition_path.read_text(encoding="utf-8")

start = composition.index("laplace_composition_status PlanResourceCounts(")
end = composition.index("\nlaplace_composition_status EstimateWorkingMemory(", start)
plan_bounds = r'''laplace_composition_status PlanResourceBounds(
    const laplace_composition_working_set_input& input,
    ResourceCounts& resources) {
    resources = ResourceCounts{};
    if (AddOverflow(
            input.known_entity_count, input.request_count,
            resources.unique_entity_count)) {
        return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
    }
    resources.unique_physicality_count = input.request_count;

    try {
        std::unordered_map<IdKey, DigestKey, ByteKeyHash<16>> witnesses;
        witnesses.reserve(static_cast<std::size_t>(input.known_entity_count));
        for (std::uint64_t index = 0U; index < input.known_entity_count; ++index) {
            const auto& known = input.known_entities[index];
            const IdKey id = Key(known.entity_id);
            const DigestKey witness = Key(known.identity_witness);
            const auto prior = witnesses.find(id);
            if (prior != witnesses.end() && !(prior->second == witness)) {
                return LAPLACE_COMPOSITION_IDENTITY_COLLISION;
            }
            witnesses.emplace(id, witness);
        }

        for (std::uint64_t request_index = 0U;
             request_index < input.request_count; ++request_index) {
            const auto& request = input.requests[request_index];
            resources.maximum_request_operand_count = std::max(
                resources.maximum_request_operand_count, request.operand_count);
            std::uint64_t request_carriers{};
            const std::uint64_t end = request.first_operand + request.operand_count;
            for (std::uint64_t operand_index = request.first_operand;
                 operand_index < end; ++operand_index) {
                const std::uint64_t multiplicity =
                    input.operands[operand_index].multiplicity;
                const std::uint64_t carriers =
                    multiplicity / LAPLACE_COMPOSITION_MAXIMUM_RUN_PER_CARRIER +
                    ((multiplicity % LAPLACE_COMPOSITION_MAXIMUM_RUN_PER_CARRIER) != 0U
                         ? 1U
                         : 0U);
                if (AddOverflow(request_carriers, carriers, request_carriers)) {
                    return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
                }
            }
            resources.maximum_request_carrier_count = std::max(
                resources.maximum_request_carrier_count, request_carriers);
            if (AddOverflow(
                    resources.expanded_trajectory_carrier_count,
                    request_carriers,
                    resources.expanded_trajectory_carrier_count)) {
                return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
            }
#if defined(LAPLACE_TEST_COMPOSITION_IMPLICIT_OCCURRENCE)
            const bool emit_occurrence = true;
#else
            const bool emit_occurrence =
                (request.flags & LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE) != 0U;
#endif
            if (emit_occurrence &&
                AddOverflow(
                    resources.unique_occurrence_count, 1U,
                    resources.unique_occurrence_count)) {
                return LAPLACE_COMPOSITION_COUNT_OVERFLOW;
            }
        }
        resources.unique_trajectory_carrier_count =
            resources.expanded_trajectory_carrier_count;
        return LAPLACE_COMPOSITION_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COMPOSITION_MEMORY_FAILURE;
    }
}
'''
composition = composition[:start] + plan_bounds + composition[end:]

composition = replace_once(
    composition,
    "    std::vector<RequestCalculation>* outputs{};\n    std::atomic<std::uint32_t> first_failure{LAPLACE_COMPOSITION_OK};",
    "    std::vector<RequestCalculation>* outputs{};\n"
    "    std::atomic<std::uint64_t>* semantic_calculation_count{};\n"
    "    std::atomic<std::uint32_t> first_failure{LAPLACE_COMPOSITION_OK};",
    "request task calculation counter")

composition = replace_once(
    composition,
    "        RequestCalculation& output = (*task.outputs)[output_index];\n"
    "        const auto status = CalculateRequest(\n"
    "            *task.input, *task.calculated, request_index,\n"
    "            output.result, output.carriers, output.physicality,\n"
    "            output.has_physicality);",
    "        RequestCalculation& output = (*task.outputs)[output_index];\n"
    "        if (task.semantic_calculation_count != nullptr) {\n"
    "            task.semantic_calculation_count->fetch_add(\n"
    "                1U, std::memory_order_relaxed);\n"
    "        }\n"
    "        const auto status = CalculateRequest(\n"
    "            *task.input, *task.calculated, request_index,\n"
    "            output.result, output.carriers, output.physicality,\n"
    "            output.has_physicality);\n"
    "#if defined(LAPLACE_TEST_COMPOSITION_DUPLICATE_CALCULATION)\n"
    "        if (status == LAPLACE_COMPOSITION_OK) {\n"
    "            RequestCalculation replay{};\n"
    "            if (task.semantic_calculation_count != nullptr) {\n"
    "                task.semantic_calculation_count->fetch_add(\n"
    "                    1U, std::memory_order_relaxed);\n"
    "            }\n"
    "            const auto replay_status = CalculateRequest(\n"
    "                *task.input, *task.calculated, request_index,\n"
    "                replay.result, replay.carriers, replay.physicality,\n"
    "                replay.has_physicality);\n"
    "            if (replay_status != LAPLACE_COMPOSITION_OK) {\n"
    "                std::uint32_t expected = LAPLACE_COMPOSITION_OK;\n"
    "                task.first_failure.compare_exchange_strong(\n"
    "                    expected, static_cast<std::uint32_t>(replay_status),\n"
    "                    std::memory_order_acq_rel);\n"
    "                return LAPLACE_EXECUTION_PROVIDER_RUN_FAILED;\n"
    "            }\n"
    "        }\n"
    "#endif",
    "semantic calculation instrumentation")

composition = replace_once(
    composition,
    "laplace_composition_status CalculateRequestLevels(\n"
    "    const laplace_composition_working_set_input& input,\n"
    "    Consumer&& consume) {",
    "laplace_composition_status CalculateRequestLevels(\n"
    "    const laplace_composition_working_set_input& input,\n"
    "    std::atomic<std::uint64_t>* semantic_calculation_count,\n"
    "    Consumer&& consume) {",
    "frontier calculator counter parameter")

composition = replace_once(
    composition,
    "            task.request_indexes = &request_indexes;\n"
    "            task.outputs = &outputs;",
    "            task.request_indexes = &request_indexes;\n"
    "            task.outputs = &outputs;\n"
    "            task.semantic_calculation_count = semantic_calculation_count;",
    "frontier task counter binding")

composition = replace_once(
    composition,
    "    const auto plan_status = PlanResourceCounts(input, resources);",
    "    const auto plan_status = PlanResourceBounds(input, resources);",
    "static resource planner call")

old_mutant = '''#if defined(LAPLACE_TEST_COMPOSITION_REQUEST_COUNT_MEMORY)\n    if (AddOverflow(input.known_entity_count, input.request_count, entity_count)) {\n        return LAPLACE_COMPOSITION_COUNT_OVERFLOW;\n    }\n    physicality_count = input.request_count;\n    carrier_count = resources.expanded_trajectory_carrier_count;\n    occurrence_count = input.request_count;\n#endif\n'''
composition = replace_once(
    composition, old_mutant, "", "obsolete request-count memory mutant")

composition = replace_once(
    composition,
    "        state->summary.operand_count = input->operand_count;\n"
    "        state->summary.estimated_peak_working_bytes = estimated_bytes;",
    "        state->summary.operand_count = input->operand_count;\n"
    "        state->summary.planned_entity_upper_bound =\n"
    "            resources.unique_entity_count;\n"
    "        state->summary.planned_physicality_upper_bound =\n"
    "            resources.unique_physicality_count;\n"
    "        state->summary.planned_trajectory_carrier_upper_bound =\n"
    "            resources.unique_trajectory_carrier_count;\n"
    "        state->summary.planned_occurrence_upper_bound =\n"
    "            resources.unique_occurrence_count;\n"
    "        state->summary.estimated_peak_working_bytes = estimated_bytes;",
    "planned bound receipt fields")

composition = replace_once(
    composition,
    "        const auto calculation_status = CalculateRequestLevels(\n"
    "            *input,\n"
    "            [&](const std::uint64_t request_index,",
    "        std::atomic<std::uint64_t> semantic_calculation_count{0U};\n"
    "        const auto calculation_status = CalculateRequestLevels(\n"
    "            *input, &semantic_calculation_count,\n"
    "            [&](const std::uint64_t request_index,",
    "single semantic calculation pass")

composition = replace_once(
    composition,
    "        state->summary.unique_entity_count =\n"
    "            static_cast<std::uint64_t>(state->entities.size());",
    "        state->summary.semantic_calculation_count =\n"
    "            semantic_calculation_count.load(std::memory_order_relaxed);\n"
    "        state->summary.unique_entity_count =\n"
    "            static_cast<std::uint64_t>(state->entities.size());",
    "semantic calculation receipt")

composition_path.write_text(composition, encoding="utf-8")

header_path = Path("engine/include/laplace/composition.h")
header = header_path.read_text(encoding="utf-8")
header = replace_once(
    header,
    "    uint64_t stream_byte_count;\n"
    "    uint64_t estimated_peak_working_bytes;",
    "    uint64_t stream_byte_count;\n"
    "    uint64_t planned_entity_upper_bound;\n"
    "    uint64_t planned_physicality_upper_bound;\n"
    "    uint64_t planned_trajectory_carrier_upper_bound;\n"
    "    uint64_t planned_occurrence_upper_bound;\n"
    "    uint64_t semantic_calculation_count;\n"
    "    uint64_t estimated_peak_working_bytes;",
    "composition summary planning receipt")
header_path.write_text(header, encoding="utf-8")

resource_test_path = Path("tests/composition_resource_accounting_tests.cpp")
resource_test = resource_test_path.read_text(encoding="utf-8")
resource_test = replace_once(
    resource_test,
    "    EXPECT_LT(\n"
    "        duplicate.estimated_peak_working_bytes,\n"
    "        chain.estimated_peak_working_bytes);\n"
    "    EXPECT_LT(\n"
    "        duplicate.estimated_peak_working_bytes,\n"
    "        observed_duplicate.estimated_peak_working_bytes);",
    "    EXPECT_EQ(duplicate.semantic_calculation_count, RequestCount);\n"
    "    EXPECT_EQ(chain.semantic_calculation_count, RequestCount);\n"
    "    EXPECT_EQ(observed_duplicate.semantic_calculation_count, RequestCount);\n\n"
    "    EXPECT_EQ(duplicate.planned_entity_upper_bound, RequestCount + 2U);\n"
    "    EXPECT_EQ(chain.planned_entity_upper_bound, RequestCount + 2U);\n"
    "    EXPECT_EQ(duplicate.planned_physicality_upper_bound, RequestCount);\n"
    "    EXPECT_EQ(chain.planned_physicality_upper_bound, RequestCount);\n"
    "    EXPECT_EQ(duplicate.planned_trajectory_carrier_upper_bound, RequestCount * 2U);\n"
    "    EXPECT_EQ(chain.planned_trajectory_carrier_upper_bound, RequestCount * 2U);\n"
    "    EXPECT_EQ(duplicate.planned_occurrence_upper_bound, 0U);\n"
    "    EXPECT_EQ(chain.planned_occurrence_upper_bound, 0U);\n"
    "    EXPECT_EQ(observed_duplicate.planned_occurrence_upper_bound, RequestCount);\n\n"
    "    EXPECT_EQ(\n"
    "        duplicate.estimated_peak_working_bytes,\n"
    "        chain.estimated_peak_working_bytes);\n"
    "    EXPECT_LT(\n"
    "        duplicate.estimated_peak_working_bytes,\n"
    "        observed_duplicate.estimated_peak_working_bytes);",
    "resource accounting expectations")
resource_test_path.write_text(resource_test, encoding="utf-8")

occurrence_cmake_path = Path("tests/composition_occurrence.cmake")
occurrence_cmake = occurrence_cmake_path.read_text(encoding="utf-8")
mutant_start = occurrence_cmake.index("add_library(laplace_composition_request_count_memory_mutant STATIC")
mutant_end = occurrence_cmake.index("\n# #177:", mutant_start)
duplicate_mutant = r'''add_library(laplace_composition_duplicate_calculation_mutant STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/src/composition.cpp")
target_include_directories(laplace_composition_duplicate_calculation_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_composition_duplicate_calculation_mutant PRIVATE
    Laplace::Engine BLAKE3::blake3)
target_compile_definitions(laplace_composition_duplicate_calculation_mutant PRIVATE
    LAPLACE_TEST_COMPOSITION_DUPLICATE_CALCULATION=1)
target_compile_options(laplace_composition_duplicate_calculation_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_executable(laplace_composition_duplicate_calculation_mutation_probe
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/composition_resource_accounting_tests.cpp")
target_include_directories(laplace_composition_duplicate_calculation_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(laplace_composition_duplicate_calculation_mutation_probe PRIVATE
    laplace_composition_duplicate_calculation_mutant
    Laplace::Engine
    GTest::gtest_main)
target_compile_options(laplace_composition_duplicate_calculation_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_test(
    NAME composition.mutation-duplicate-semantic-calculation-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_composition_duplicate_calculation_mutation_probe>"
        "-DFILTER=CompositionResourceAccounting.CanonicalReuseBoundsPhysicalWorkAndSeparatesOccurrences"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_gtest_failure.cmake")
set_tests_properties(
    composition.mutation-duplicate-semantic-calculation-detected PROPERTIES
    LABELS "implementation;composition;execution;resource;billing;mutation")
'''
occurrence_cmake = occurrence_cmake[:mutant_start] + duplicate_mutant + occurrence_cmake[mutant_end:]
occurrence_cmake_path.write_text(occurrence_cmake, encoding="utf-8")

contract_path = Path("contracts/composition.json")
contract = contract_path.read_text(encoding="utf-8")
contract = replace_once(contract, '"minor": 2', '"minor": 3', "composition ABI minor")
contract = replace_once(
    contract,
    '    "resource_authority": "immutable-framework-context-memory-grant",\n',
    '    "resource_authority": "immutable-framework-context-memory-grant",\n'
    '    "resource_planning_law": "preflight derives conservative upper bounds from validated request and dependency shape without executing semantic request calculation",\n'
    '    "resource_receipt_law": "working-set summary reports planned entity physicality trajectory-carrier and occurrence upper bounds separately from actual unique counts plus the semantic calculation count",\n'
    '    "single_calculation_law": "each admitted request executes CalculateRequest-equivalent semantic work exactly once; duplicate replay is a mutation-detected defect",\n',
    "composition resource laws")
contract_path.write_text(contract, encoding="utf-8")

print("single-pass composition repair applied")
