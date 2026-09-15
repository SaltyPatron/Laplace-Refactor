#include "laplace/content_reference_view.h"
#include "context_fixture.h"
#include <array>
#include <cstring>
#include <vector>
#include <gtest/gtest.h>

namespace {
struct Set {
    laplace_composition_working_set* value{};
    ~Set() { laplace_composition_working_set_destroy(&value); }
};
struct Plan {
    laplace_content_reference_plan* value{};
    ~Plan() { laplace_content_reference_plan_destroy(&value); }
};
struct Atom {
    laplace_composition_known_entity known{};
    laplace_persistence_physicality_record body{};
    Atom(std::uint32_t position, const laplace_framework_context& context, double scale = 1.0) {
        EXPECT_EQ(laplace_identity_codepoint_witness(position, &known.entity_id, &known.identity_witness), LAPLACE_IDENTITY_OK);
        known.atom = position; known.has_atom = 1U;
        known.centroid.component[position % 4U] = scale;
        EXPECT_EQ(laplace_persistence_atomic_point_physicality(&known.entity_id, 1U,
            &context.epochs[0], &context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY],
            &known.centroid, &body), LAPLACE_PERSISTENCE_OK);
        known.physicality_id = body.physicality_id;
    }
    laplace_content_reference_atom Input() const { return {known, &body}; }
};
struct Form {
    laplace_composition_result result{};
    laplace_persistence_physicality_record body{};
    std::vector<laplace_trajectory_carrier> carriers;
    laplace_content_reference_source Input() const {
        return {&body, carriers.data(), carriers.size(), result.identity_witness};
    }
};
laplace_framework_context Context() {
    auto context = laplace_test_context(0U);
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
    return context;
}
Form Compose(const laplace_framework_context& context,
             const std::vector<laplace_composition_known_entity>& known,
             const std::vector<laplace_composition_operand>& operands) {
    Form form;
    const laplace_composition_request request{0U, operands.size(), 1U, 1U, 0U,
        context.epochs[0], context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY], {}};
    const laplace_composition_working_set_input input{&context, &context.epochs[0],
        &context.epochs[0], known.data(), known.size(), operands.data(), operands.size(), &request, 1U, 4096U, 0U};
    Set set;
    EXPECT_EQ(laplace_composition_working_set_create(&input, &set.value), LAPLACE_COMPOSITION_OK);
    if (set.value == nullptr) return form;
    std::size_t count{};
    const auto* results = laplace_composition_working_set_results(set.value, &count);
    EXPECT_EQ(count, 1U); form.result = results[0];
    EXPECT_EQ(laplace_composition_working_set_physicality_candidate_get(set.value, 0U, &form.body), LAPLACE_COMPOSITION_OK);
    const laplace_trajectory_carrier* carriers{};
    EXPECT_EQ(laplace_composition_working_set_trajectory_candidate_view_get(set.value, 0U, &carriers, &count), LAPLACE_COMPOSITION_OK);
    form.carriers.assign(carriers, carriers + count);
    return form;
}
laplace_composition_operand Known(std::uint64_t index, std::uint64_t count = 1U, std::uint64_t role = 0U) {
    return {index, count, role, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0U};
}
struct Fixture {
    laplace_framework_context context{Context()};
    Atom a{'a', context}, b{'b', context}, altered_a{'a', context, 0.25};
    Form original{Compose(context, {a.known, b.known}, {Known(0U), Known(1U)})};
    Form alternate{Compose(context, {altered_a.known, b.known}, {Known(0U, 1U, 64U), Known(1U, 1U, 128U)})};
    std::vector<laplace_id128> roots{original.result.entity_id};
    std::vector<laplace_content_reference_source> sources{original.Input(), alternate.Input()};
    std::vector<laplace_content_reference_atom> atoms{a.Input(), b.Input()};
    laplace_content_reference_input Input() const {
        return {&context, roots.data(), roots.size(), sources.data(), sources.size(), atoms.data(), atoms.size(),
            {100U, 100U, 100U, 1000U, 10000U, 32U, UINT64_C(32) * 1024U * 1024U}};
    }
};
laplace_content_reference_plan_view View(Plan& plan, const laplace_content_reference_input& input) {
    EXPECT_EQ(laplace_content_reference_plan_create(&input, &plan.value), LAPLACE_CONTENT_REFERENCE_OK);
    laplace_content_reference_plan_view view{};
    EXPECT_EQ(laplace_content_reference_plan_view_get(plan.value, &view), LAPLACE_CONTENT_REFERENCE_OK);
    return view;
}
std::vector<laplace_composition_result> Execute(const laplace_content_reference_plan_view& view) {
    const laplace_composition_working_set_input input{view.context, &view.source_fingerprint,
        &view.recipe_fingerprint, view.known_entities, view.known_entity_count,
        view.operands, view.operand_count, view.requests, view.request_count, 4096U, 0U};
    Set set;
    EXPECT_EQ(laplace_composition_working_set_create(&input, &set.value), LAPLACE_COMPOSITION_OK);
    if (set.value == nullptr) return {};
    std::size_t count{}; const auto* results = laplace_composition_working_set_results(set.value, &count);
    laplace_composition_working_set_summary summary{};
    EXPECT_EQ(laplace_composition_working_set_summary_get(set.value, &summary), LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(summary.occurrence_count, 0U);
    return {results, results + count};
}
Form Singleton(const Fixture& fixture) {
    Form form; form.result = fixture.original.result; form.body = fixture.original.body;
    form.carriers.resize(1U);
    EXPECT_EQ(laplace_trajectory_composition_encode(&form.body.entity_id, 1U, 1U,
        static_cast<std::uint64_t>(fixture.original.result.tier_floor) << LAPLACE_TRAJECTORY_TIER_SHIFT,
        &form.carriers[0]), LAPLACE_TRAJECTORY_OK);
    form.body.logical_count = 1U; form.body.vertex_count = 1U; form.body.radius = 0.0;
    EXPECT_EQ(laplace_persistence_trajectory_fingerprint(form.carriers.data(), form.carriers.size(),
        &form.body.trajectory_fingerprint), LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(laplace_persistence_physicality_identify(&form.body, &form.body.physicality_id), LAPLACE_PERSISTENCE_OK);
    laplace_physicality_entity_validation checked{};
    EXPECT_EQ(laplace_physicality_entity_record_validate(&form.body, form.carriers.data(), 1U,
        1U, 1U, &checked), LAPLACE_PHYSICALITY_ENTITY_OK);
    EXPECT_EQ(checked.witness_available, 0U);
    return form;
}

TEST(ContentReferenceView, DifferentHistoricalFormsUsePinnedCanonicalGeometry) {
    Fixture fixture;
    ASSERT_TRUE(laplace_identity_equal(&fixture.original.result.entity_id, &fixture.alternate.result.entity_id));
    ASSERT_NE(std::memcmp(fixture.original.body.physicality_id.bytes, fixture.alternate.body.physicality_id.bytes, 32U), 0);
    ASSERT_NE(std::memcmp(&fixture.original.body.centroid, &fixture.alternate.body.centroid, sizeof(laplace_point4d)), 0);
    Plan first; const auto view = View(first, fixture.Input());
    ASSERT_EQ(view.request_count, 1U); ASSERT_EQ(view.root_count, 1U);
    EXPECT_EQ(view.roots[0].reference_kind, static_cast<std::uint32_t>(LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT));
    for (std::uint64_t i = 0; i < view.operand_count; ++i) EXPECT_EQ(view.operands[i].relationship_metadata, 0U);
    const auto values = Execute(view); ASSERT_EQ(values.size(), 1U);
    EXPECT_EQ(laplace_content_reference_plan_verify_results(first.value, values.data(), values.size()), LAPLACE_CONTENT_REFERENCE_OK);
    EXPECT_EQ(std::memcmp(&values[0].centroid, &fixture.original.body.centroid, sizeof(laplace_point4d)), 0);
    fixture.sources = {fixture.alternate.Input()};
    Plan second; const auto other = Execute(View(second, fixture.Input())); ASSERT_EQ(other.size(), 1U);
    EXPECT_EQ(std::memcmp(values[0].physicality_id.bytes, other[0].physicality_id.bytes, 32U), 0);
}

TEST(ContentReferenceView, AlternateCarrierSplitsNormalizeToOneCanonicalRun) {
    Fixture fixture;
    auto merged = Compose(fixture.context, {fixture.a.known}, {Known(0U, 3U)});
    auto split = Compose(fixture.context, {fixture.altered_a.known}, {Known(0U, 1U, 64U), Known(0U, 2U, 128U)});
    ASSERT_EQ(merged.carriers.size(), 1U); ASSERT_EQ(split.carriers.size(), 2U);
    fixture.sources = {split.Input(), merged.Input()}; fixture.roots = {merged.result.entity_id};
    Plan plan; const auto view = View(plan, fixture.Input());
    ASSERT_EQ(view.request_count, 1U); ASSERT_EQ(view.operand_count, 1U);
    EXPECT_EQ(view.operands[0].multiplicity, 3U);
    const auto values = Execute(view);
    EXPECT_EQ(laplace_content_reference_plan_verify_results(plan.value, values.data(), values.size()), LAPLACE_CONTENT_REFERENCE_OK);
}

TEST(ContentReferenceView, MissingClosureIsSortedNonExecutableAndCanBeCompleted) {
    Fixture fixture; fixture.atoms.clear();
    Plan partial; auto input = fixture.Input();
    ASSERT_EQ(laplace_content_reference_plan_create(&input, &partial.value), LAPLACE_CONTENT_REFERENCE_INCOMPLETE);
    laplace_content_reference_plan_view missing{};
    ASSERT_EQ(laplace_content_reference_plan_view_get(partial.value, &missing), LAPLACE_CONTENT_REFERENCE_INCOMPLETE);
    ASSERT_EQ(missing.missing_entity_count, 2U);
    EXPECT_LT(laplace_identity_compare(&missing.missing_entity_ids[0], &missing.missing_entity_ids[1]), 0);
    EXPECT_EQ(missing.known_entity_count, 0U); EXPECT_EQ(missing.request_count, 0U); EXPECT_EQ(missing.root_count, 0U);
    EXPECT_EQ(missing.requests, nullptr);
    EXPECT_EQ(laplace_content_reference_plan_verify_results(partial.value, nullptr, 0U), LAPLACE_CONTENT_REFERENCE_INCOMPLETE);
    fixture.atoms = {fixture.a.Input(), fixture.b.Input()};
    Plan complete; const auto values = Execute(View(complete, fixture.Input()));
    EXPECT_EQ(laplace_content_reference_plan_verify_results(complete.value, values.data(), values.size()), LAPLACE_CONTENT_REFERENCE_OK);
}

TEST(ContentReferenceView, SingletonFormRequiresIndependentClosureWithoutCreatingCycle) {
    Fixture fixture; const auto singleton = Singleton(fixture);
    fixture.sources = {singleton.Input()}; fixture.atoms.clear();
    Plan missing; auto input = fixture.Input();
    ASSERT_EQ(laplace_content_reference_plan_create(&input, &missing.value), LAPLACE_CONTENT_REFERENCE_INCOMPLETE);
    laplace_content_reference_plan_view view{};
    ASSERT_EQ(laplace_content_reference_plan_view_get(missing.value, &view), LAPLACE_CONTENT_REFERENCE_INCOMPLETE);
    ASSERT_EQ(view.missing_entity_count, 1U);
    EXPECT_TRUE(laplace_identity_equal(&view.missing_entity_ids[0], &fixture.original.result.entity_id));
    fixture.sources.push_back(fixture.original.Input()); fixture.atoms = {fixture.a.Input(), fixture.b.Input()};
    Plan complete; const auto closed = View(complete, fixture.Input()); ASSERT_EQ(closed.request_count, 1U);
    const auto values = Execute(closed);
    EXPECT_EQ(laplace_content_reference_plan_verify_results(complete.value, values.data(), values.size()), LAPLACE_CONTENT_REFERENCE_OK);
}

TEST(ContentReferenceView, EveryFormAndFullWitnessAreValidated) {
    Fixture fixture;
    for (unsigned variant = 0U; variant < 3U; ++variant) {
        auto source = fixture.alternate.Input(); auto body = fixture.alternate.body;
        source.physicality = &body;
        if (variant == 0U) source.identity_witness.bytes[31] ^= 1U;
        if (variant == 1U) body.centroid.component[0] += 0.1;
        if (variant == 2U) body.trajectory_fingerprint.bytes[31] ^= 1U;
        fixture.sources = {fixture.original.Input(), source}; auto input = fixture.Input(); Plan rejected;
        EXPECT_NE(laplace_content_reference_plan_create(&input, &rejected.value), LAPLACE_CONTENT_REFERENCE_OK);
        EXPECT_EQ(rejected.value, nullptr);
    }
}

TEST(ContentReferenceView, PinnedAtomTupleBodyWitnessAndEpochMustAgree) {
    Fixture fixture;
    for (unsigned variant = 0U; variant < 4U; ++variant) {
        auto atom = fixture.a.Input(); auto body = fixture.a.body; atom.physicality = &body;
        if (variant == 0U) atom.known.identity_witness.bytes[31] ^= 1U;
        if (variant == 1U) atom.known.centroid.component[0] += 0.1;
        if (variant == 2U) atom.known.tier_floor = 1U;
        if (variant == 3U) {
            body.geometry_epoch.bytes[31] ^= 1U;
            ASSERT_EQ(laplace_persistence_physicality_identify(&body, &body.physicality_id), LAPLACE_PERSISTENCE_OK);
            atom.known.physicality_id = body.physicality_id;
        }
        fixture.atoms = {atom, fixture.b.Input()}; auto input = fixture.Input(); Plan rejected;
        EXPECT_EQ(laplace_content_reference_plan_create(&input, &rejected.value), LAPLACE_CONTENT_REFERENCE_ATOM_INVALID);
        EXPECT_EQ(rejected.value, nullptr);
    }
}

TEST(ContentReferenceView, RepeatedRootsAndNestedContentShareOnePostorderCalculation) {
    Fixture fixture;
    laplace_composition_known_entity child{fixture.original.result.entity_id, fixture.original.result.identity_witness,
        fixture.original.result.physicality_id, fixture.original.result.centroid, 0U, fixture.original.result.tier_floor, 0U, 0U};
    const auto parent = Compose(fixture.context, {child, fixture.a.known}, {Known(0U), Known(1U)});
    fixture.sources.push_back(parent.Input()); fixture.roots = {parent.result.entity_id, child.entity_id, parent.result.entity_id};
    Plan plan; const auto view = View(plan, fixture.Input()); ASSERT_EQ(view.request_count, 2U); ASSERT_EQ(view.root_count, 3U);
    EXPECT_EQ(view.roots[0].reference_index, view.roots[2].reference_index);
    EXPECT_LT(view.roots[1].reference_index, view.roots[0].reference_index);
    const auto values = Execute(view);
    EXPECT_EQ(laplace_content_reference_plan_verify_results(plan.value, values.data(), values.size()), LAPLACE_CONTENT_REFERENCE_OK);
    auto shallow = fixture.Input(); shallow.limits.maximum_depth = 2U; Plan rejected;
    EXPECT_EQ(laplace_content_reference_plan_create(&shallow, &rejected.value), LAPLACE_CONTENT_REFERENCE_LIMIT);
    EXPECT_EQ(rejected.value, nullptr);
}

TEST(ContentReferenceView, WorkAndMemoryBoundsHaveExplicitRefusals) {
    Fixture fixture; auto input = fixture.Input(); std::uint64_t bytes{};
    ASSERT_EQ(laplace_content_reference_plan_memory_bound(&input, &bytes), LAPLACE_CONTENT_REFERENCE_OK);
    ASSERT_GT(bytes, 0U);
    for (unsigned variant = 0U; variant < 5U; ++variant) {
        auto bounded = input;
        if (variant == 0U) bounded.limits.maximum_roots = 0U;
        if (variant == 1U) bounded.limits.maximum_carriers = 0U;
        if (variant == 2U) bounded.limits.maximum_logical_count = 1U;
        if (variant == 3U) bounded.limits.maximum_memory_bytes = bytes - 1U;
        if (variant == 4U) bounded.limits.maximum_nodes = 1U;
        Plan rejected;
        EXPECT_EQ(laplace_content_reference_plan_create(&bounded, &rejected.value), LAPLACE_CONTENT_REFERENCE_LIMIT);
        EXPECT_EQ(rejected.value, nullptr);
    }
}

TEST(ContentReferenceView, CalculatedResultTailWitnessCorruptionCannotPass) {
    Fixture fixture; Plan plan; const auto view = View(plan, fixture.Input()); auto values = Execute(view);
    ASSERT_EQ(values.size(), 1U);
    values[0].identity_witness.bytes[31] ^= 1U;
    EXPECT_EQ(laplace_content_reference_plan_verify_results(plan.value, values.data(), values.size()), LAPLACE_CONTENT_REFERENCE_RESULT_INVALID);
    EXPECT_EQ(laplace_content_reference_plan_verify_results(plan.value, values.data(), 0U), LAPLACE_CONTENT_REFERENCE_RESULT_INVALID);
}

TEST(ContentReferenceView, AtomicRootsAndEmptyInputHaveNoCompositionRequests) {
    Fixture fixture; fixture.roots = {fixture.a.known.entity_id, fixture.a.known.entity_id};
    fixture.sources.clear(); fixture.atoms = {fixture.a.Input(), fixture.a.Input()};
    auto input = fixture.Input(); input.limits.maximum_nodes = 1U;
    Plan atoms; const auto view = View(atoms, input);
    ASSERT_EQ(view.known_entity_count, 1U); ASSERT_EQ(view.root_count, 2U);
    EXPECT_EQ(view.request_count, 0U);
    EXPECT_EQ(view.roots[0].reference_kind, static_cast<std::uint32_t>(LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY));
    EXPECT_EQ(view.roots[0].reference_index, view.roots[1].reference_index);
    EXPECT_EQ(laplace_content_reference_plan_verify_results(atoms.value, nullptr, 0U), LAPLACE_CONTENT_REFERENCE_OK);
    fixture.roots.clear(); fixture.atoms.clear();
    Plan empty; const auto empty_view = View(empty, fixture.Input());
    EXPECT_EQ(empty_view.root_count, 0U); EXPECT_EQ(empty_view.known_entity_count, 0U);
    EXPECT_EQ(empty_view.request_count, 0U); EXPECT_EQ(empty_view.distinct_node_count, 0U);
    EXPECT_EQ(laplace_content_reference_plan_verify_results(empty.value, nullptr, 0U), LAPLACE_CONTENT_REFERENCE_OK);
}

TEST(ContentReferenceView, CarrierBudgetRefusalDoesNotDereferenceCarrierBody) {
    Fixture fixture; auto source = fixture.original.Input();
    const laplace_trajectory_carrier no_payload{};
    source.carriers = &no_payload; source.carrier_count = UINT64_MAX;
    fixture.sources = {source}; auto input = fixture.Input();
    input.limits.maximum_carriers = UINT64_MAX;
    input.limits.maximum_nodes = UINT64_MAX;
    std::uint64_t bytes = 1U;
    EXPECT_EQ(laplace_content_reference_plan_memory_bound(&input, &bytes), LAPLACE_CONTENT_REFERENCE_LIMIT);
    EXPECT_EQ(bytes, 0U);
    Plan plan;
    EXPECT_EQ(laplace_content_reference_plan_create(&input, &plan.value), LAPLACE_CONTENT_REFERENCE_LIMIT);
    EXPECT_EQ(plan.value, nullptr);
}

TEST(ContentReferenceView, LongCanonicalRunIsPackedByTheOrdinaryComposer) {
    Fixture fixture;
    constexpr std::uint64_t count = UINT64_C(65536);
    const auto long_form = Compose(fixture.context, {fixture.a.known}, {Known(0U, count)});
    ASSERT_EQ(long_form.carriers.size(), 2U);
    fixture.roots = {long_form.result.entity_id}; fixture.sources = {long_form.Input()};
    fixture.atoms = {fixture.a.Input()}; auto input = fixture.Input();
    input.limits.maximum_logical_count = count + 1U;
    Plan plan; const auto view = View(plan, input);
    ASSERT_EQ(view.request_count, 1U); ASSERT_EQ(view.operand_count, 1U);
    EXPECT_EQ(view.operands[0].multiplicity, count);
    const auto values = Execute(view); ASSERT_EQ(values.size(), 1U);
    EXPECT_EQ(laplace_content_reference_plan_verify_results(plan.value, values.data(), values.size()), LAPLACE_CONTENT_REFERENCE_OK);
}
}  // namespace
