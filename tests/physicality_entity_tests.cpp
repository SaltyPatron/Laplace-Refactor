#include "laplace/physicality_entity.h"
#include "laplace/highway.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>
#include <gtest/gtest.h>
#include "context_fixture.h"

namespace {
struct SetOwner {
    laplace_composition_working_set* value{};
    ~SetOwner() { laplace_composition_working_set_destroy(&value); }
};
struct PlanOwner {
    laplace_physicality_entity_plan* value{};
    ~PlanOwner() { laplace_physicality_entity_plan_destroy(&value); }
};

laplace_composition_known_entity Atom(std::uint32_t position,
                                    const laplace_framework_context& context) {
    laplace_composition_known_entity atom{};
    EXPECT_EQ(laplace_identity_codepoint_witness(position, &atom.entity_id,
        &atom.identity_witness), LAPLACE_IDENTITY_OK);
    atom.centroid.component[position % 4U] = 1.0;
    atom.atom = position;
    atom.has_atom = 1U;
    laplace_persistence_physicality_record record{};
    EXPECT_EQ(laplace_persistence_atomic_point_physicality(&atom.entity_id, 1U,
        &context.epochs[0], &context.epochs[3], &atom.centroid, &record),
        LAPLACE_PERSISTENCE_OK);
    atom.physicality_id = record.physicality_id;
    return atom;
}

struct Fixture {
    laplace_framework_context context = laplace_test_context(0U);
    std::array<laplace_composition_known_entity, 3> atoms{};
    laplace_composition_known_entity target{};
    laplace_persistence_physicality_record physicality{};
    std::vector<laplace_trajectory_carrier> carriers;

    Fixture() {
        context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
        atoms = {Atom('c', context), Atom('a', context), Atom('t', context)};
        const std::array<laplace_composition_operand, 3> operands{{
            {0U,1U,0U,LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY,0U},
            {1U,1U,0U,LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY,0U},
            {2U,1U,0U,LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY,0U}}};
        const laplace_composition_request request{0U,3U,1U,1U,0U,
            context.epochs[0],context.epochs[3],{}};
        const laplace_composition_working_set_input input{&context,&context.epochs[0],
            &context.epochs[0],atoms.data(),atoms.size(),operands.data(),operands.size(),
            &request,1U,4096U,0U};
        SetOwner source;
        EXPECT_EQ(laplace_composition_working_set_create(&input,&source.value),LAPLACE_COMPOSITION_OK);
        if (source.value == nullptr) return;
        std::size_t count{};
        const auto* results = laplace_composition_working_set_results(source.value,&count);
        EXPECT_EQ(count,1U);
        target = {results[0].entity_id, results[0].identity_witness,
            results[0].physicality_id,results[0].centroid,0U,results[0].tier_floor,0U,0U};
        EXPECT_EQ(laplace_composition_working_set_physicality_candidate_get(
            source.value,0U,&physicality),LAPLACE_COMPOSITION_OK);
        const laplace_trajectory_carrier* trajectory{};
        EXPECT_EQ(laplace_composition_working_set_trajectory_candidate_view_get(
            source.value,0U,&trajectory,&count),LAPLACE_COMPOSITION_OK);
        carriers.assign(trajectory,trajectory+count);
    }

    laplace_physicality_entity_input Input() const {
        return {&physicality,carriers.data(),carriers.size(),context.epochs[3],
            10000U,100000U,10000U,100000U};
    }

    void Execute(const laplace_physicality_entity_plan_view& view, SetOwner& set) const {
        std::vector<laplace_composition_known_entity> known;
        for (std::uint64_t index=0U;index<view.external_entity_count;++index) {
            const auto& id=view.external_entity_ids[index];
            if (laplace_identity_equal(&id,&target.entity_id)) {
                auto selected=target;
                selected.physicality_id=physicality.physicality_id;
                selected.centroid=physicality.centroid;
                known.push_back(selected);
            } else {
                bool found=false;
                for (const auto& atom:atoms) if (laplace_identity_equal(&id,&atom.entity_id)) {
                    known.push_back(atom); found=true; break;
                }
                ASSERT_TRUE(found);
            }
        }
        for (std::uint64_t index=0U;index<view.atom_count;++index)
            known.push_back(Atom(view.atom_positions[index],context));
        const laplace_composition_working_set_input input{&context,
            &view.physicality_record_id,&view.recipe_fingerprint,known.data(),known.size(),
            view.operands,view.operand_count,view.requests,view.request_count,4096U,0U};
        ASSERT_EQ(laplace_composition_working_set_create(&input,&set.value),LAPLACE_COMPOSITION_OK);
    }
};

laplace_composition_result Root(const PlanOwner& plan,const SetOwner& set) {
    laplace_physicality_entity_plan_view view{};
    EXPECT_EQ(laplace_physicality_entity_plan_view_get(plan.value,&view),LAPLACE_PHYSICALITY_ENTITY_OK);
    std::size_t count{};
    const auto* results=laplace_composition_working_set_results(set.value,&count);
    EXPECT_LT(view.root_result_index,count);
    return results == nullptr || view.root_result_index >= count
        ? laplace_composition_result{} : results[view.root_result_index];
}

TEST(PhysicalityEntity, OrdinaryCompositionPreservesTargetAndReusesDescriptor) {
    Fixture fixture;
    const auto input=fixture.Input();
    PlanOwner plan;
    ASSERT_EQ(laplace_physicality_entity_plan_create(&input,&plan.value),LAPLACE_PHYSICALITY_ENTITY_OK);
    laplace_physicality_entity_plan_view view{};
    ASSERT_EQ(laplace_physicality_entity_plan_view_get(plan.value,&view),LAPLACE_PHYSICALITY_ENTITY_OK);
    EXPECT_TRUE(laplace_identity_equal(&view.realized_entity_id,&fixture.target.entity_id));
    EXPECT_EQ(view.external_entity_count,4U);
    EXPECT_EQ(view.source_validation.witness_available,1U);
    EXPECT_EQ(view.source_validation.tier_available,1U);
    EXPECT_EQ(view.source_validation.tier_floor,fixture.target.tier_floor);
    EXPECT_EQ(std::memcmp(view.source_validation.realized_identity_witness.bytes,
        fixture.target.identity_witness.bytes,32U),0);
    for (std::uint64_t i=0U;i<view.request_count;++i) EXPECT_EQ(view.requests[i].flags,0U);
    SetOwner first,repeat;
    fixture.Execute(view,first); fixture.Execute(view,repeat);
    ASSERT_NE(first.value,nullptr); ASSERT_NE(repeat.value,nullptr);
    const auto root=Root(plan,first),again=Root(plan,repeat);
    EXPECT_FALSE(laplace_identity_equal(&root.entity_id,&fixture.target.entity_id));
    EXPECT_TRUE(laplace_identity_equal(&root.entity_id,&again.entity_id));
    EXPECT_EQ(std::memcmp(root.identity_witness.bytes,again.identity_witness.bytes,32U),0);
    laplace_composition_working_set_summary summary{};
    ASSERT_EQ(laplace_composition_working_set_summary_get(first.value,&summary),LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(summary.occurrence_count,0U);
    EXPECT_LT(summary.unique_entity_count,summary.request_count+view.atom_count);
    EXPECT_GT(summary.unique_physicality_count,1U);
    EXPECT_LT(summary.unique_physicality_count,1000U);
    std::size_t a{},b{};
    const auto* r1=laplace_composition_working_set_results(first.value,&a);
    const auto* r2=laplace_composition_working_set_results(repeat.value,&b);
    ASSERT_EQ(a,b);
    for(std::size_t i=0U;i<a;++i) {
        EXPECT_TRUE(laplace_identity_equal(&r1[i].entity_id,&r2[i].entity_id));
        EXPECT_EQ(std::memcmp(r1[i].physicality_id.bytes,r2[i].physicality_id.bytes,32U),0);
    }
}

TEST(PhysicalityEntity, DistinctRecordFormsAndSignedZeroRemainDistinctEntities) {
    Fixture fixture;
    fixture.physicality.radius=0.0;
    ASSERT_EQ(laplace_persistence_physicality_identify(&fixture.physicality,
        &fixture.physicality.physicality_id),LAPLACE_PERSISTENCE_OK);
    const auto input=fixture.Input();
    PlanOwner original;
    ASSERT_EQ(laplace_physicality_entity_plan_create(&input,&original.value),LAPLACE_PHYSICALITY_ENTITY_OK);
    laplace_physicality_entity_plan_view view{};
    ASSERT_EQ(laplace_physicality_entity_plan_view_get(original.value,&view),LAPLACE_PHYSICALITY_ENTITY_OK);
    SetOwner baseline;fixture.Execute(view,baseline);ASSERT_NE(baseline.value,nullptr);
    const auto first=Root(original,baseline);
    for (const bool signed_zero : {false,true}) {
        const auto saved=fixture.physicality;
        if (signed_zero) fixture.physicality.radius=-0.0;
        else ++fixture.physicality.recipe_version;
        ASSERT_EQ(laplace_persistence_physicality_identify(&fixture.physicality,
            &fixture.physicality.physicality_id),LAPLACE_PERSISTENCE_OK);
        PlanOwner changed;const auto changed_input=fixture.Input();
        ASSERT_EQ(laplace_physicality_entity_plan_create(&changed_input,&changed.value),LAPLACE_PHYSICALITY_ENTITY_OK);
        ASSERT_EQ(laplace_physicality_entity_plan_view_get(changed.value,&view),LAPLACE_PHYSICALITY_ENTITY_OK);
        SetOwner result;fixture.Execute(view,result);ASSERT_NE(result.value,nullptr);
        const auto second=Root(changed,result);
        EXPECT_FALSE(laplace_identity_equal(&first.entity_id,&second.entity_id));
        EXPECT_TRUE(laplace_identity_equal(&view.realized_entity_id,&fixture.target.entity_id));
        fixture.physicality=saved;
    }
}

TEST(PhysicalityEntity, ViewGeometryDoesNotChangeCanonicalDescriptor) {
    Fixture fixture; auto input=fixture.Input();
    PlanOwner first,second;
    ASSERT_EQ(laplace_physicality_entity_plan_create(&input,&first.value),LAPLACE_PHYSICALITY_ENTITY_OK);
    input.view_geometry_epoch.bytes[0]^=1U;
    ASSERT_EQ(laplace_physicality_entity_plan_create(&input,&second.value),LAPLACE_PHYSICALITY_ENTITY_OK);
    laplace_physicality_entity_plan_view a{},b{};
    ASSERT_EQ(laplace_physicality_entity_plan_view_get(first.value,&a),LAPLACE_PHYSICALITY_ENTITY_OK);
    ASSERT_EQ(laplace_physicality_entity_plan_view_get(second.value,&b),LAPLACE_PHYSICALITY_ENTITY_OK);
    SetOwner one,two;fixture.Execute(a,one);fixture.Execute(b,two);
    ASSERT_NE(one.value,nullptr);ASSERT_NE(two.value,nullptr);
    const auto root1=Root(first,one),root2=Root(second,two);
    EXPECT_TRUE(laplace_identity_equal(&root1.entity_id,&root2.entity_id));
    EXPECT_NE(std::memcmp(root1.physicality_id.bytes,root2.physicality_id.bytes,32U),0);
}

TEST(PhysicalityEntity, RejectsCorruptedRecordCarrierAndFalseRealizedIdentity) {
    Fixture fixture;
    const auto saved=fixture.physicality;
    auto input=fixture.Input();PlanOwner rejected;
    fixture.physicality.centroid.component[0]=0.125;
    EXPECT_EQ(laplace_physicality_entity_plan_create(&input,&rejected.value),LAPLACE_PHYSICALITY_ENTITY_RECORD_INVALID);
    EXPECT_EQ(rejected.value,nullptr);
    fixture.physicality=saved;
    auto bytes=reinterpret_cast<std::uint8_t*>(fixture.carriers.data());bytes[0]^=1U;
    EXPECT_EQ(laplace_physicality_entity_plan_create(&input,&rejected.value),LAPLACE_PHYSICALITY_ENTITY_TRAJECTORY_INVALID);
    bytes[0]^=1U;
    fixture.physicality.entity_id=fixture.atoms[0].entity_id;
    ASSERT_EQ(laplace_persistence_physicality_identify(&fixture.physicality,
        &fixture.physicality.physicality_id),LAPLACE_PERSISTENCE_OK);
    EXPECT_EQ(laplace_physicality_entity_plan_create(&input,&rejected.value),LAPLACE_PHYSICALITY_ENTITY_TRAJECTORY_INVALID);
    fixture.physicality=saved;
    fixture.physicality.radius=std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(laplace_physicality_entity_plan_create(&input,&rejected.value),LAPLACE_PHYSICALITY_ENTITY_RECORD_INVALID);
}

TEST(PhysicalityEntity, FiniteLimitsRejectWithoutPublishingPartialPlans) {
    Fixture fixture; const auto baseline=fixture.Input();
    std::uint64_t bound{};
    ASSERT_EQ(laplace_physicality_entity_plan_memory_bound(&baseline,&bound),LAPLACE_PHYSICALITY_ENTITY_OK);
    EXPECT_GT(bound,baseline.carrier_count*sizeof(laplace_trajectory_carrier));
    ASSERT_EQ(laplace_physicality_entity_validation_memory_bound(baseline.carrier_count,&bound),LAPLACE_PHYSICALITY_ENTITY_OK);
    EXPECT_GT(bound,baseline.carrier_count*sizeof(laplace_composition_occurrence));
    EXPECT_EQ(laplace_physicality_entity_validation_memory_bound(UINT64_MAX,&bound),LAPLACE_PHYSICALITY_ENTITY_LIMIT);
    EXPECT_EQ(bound,0U);
    auto enormous=baseline;enormous.carrier_count=UINT64_MAX;
    EXPECT_EQ(laplace_physicality_entity_plan_memory_bound(&enormous,&bound),LAPLACE_PHYSICALITY_ENTITY_LIMIT);
    EXPECT_EQ(bound,0U);
    for (int which=0;which<4;++which) {
        auto input=baseline;PlanOwner rejected;
        if(which==0) input.maximum_requests=1U;
        if(which==1) input.maximum_operands=1U;
        if(which==2) input.maximum_carriers=1U;
        if(which==3) input.maximum_logical_count=1U;
        EXPECT_EQ(laplace_physicality_entity_plan_create(&input,&rejected.value),LAPLACE_PHYSICALITY_ENTITY_LIMIT);
        EXPECT_EQ(rejected.value,nullptr);
    }
    EXPECT_EQ(laplace_physicality_entity_plan_create(&baseline,nullptr),LAPLACE_PHYSICALITY_ENTITY_INVALID_ARGUMENT);
    EXPECT_EQ(laplace_physicality_entity_record_validate(&fixture.physicality,
        fixture.carriers.data(),fixture.carriers.size(),10000U,2U,nullptr),LAPLACE_PHYSICALITY_ENTITY_LIMIT);
    EXPECT_EQ(laplace_physicality_entity_record_validate(&fixture.physicality,
        fixture.carriers.data(),fixture.carriers.size(),10000U,3U,nullptr),LAPLACE_PHYSICALITY_ENTITY_OK);
}

TEST(PhysicalityEntity, AtomicPointHasFiniteExplicitEmptyTrajectory) {
    Fixture fixture;
    fixture.target=fixture.atoms[0];
    fixture.carriers.clear();
    ASSERT_EQ(laplace_persistence_atomic_point_physicality(&fixture.target.entity_id,
        1U,&fixture.context.epochs[0],&fixture.context.epochs[3],
        &fixture.target.centroid,&fixture.physicality),LAPLACE_PERSISTENCE_OK);
    const auto input=fixture.Input();PlanOwner plan;
    ASSERT_EQ(laplace_physicality_entity_plan_create(&input,&plan.value),LAPLACE_PHYSICALITY_ENTITY_OK);
    laplace_physicality_entity_plan_view view{};
    ASSERT_EQ(laplace_physicality_entity_plan_view_get(plan.value,&view),LAPLACE_PHYSICALITY_ENTITY_OK);
    EXPECT_EQ(view.external_entity_count,1U);
    EXPECT_EQ(view.source_validation.witness_available,0U);
    EXPECT_EQ(view.source_validation.tier_available,0U);
    SetOwner set;fixture.Execute(view,set);ASSERT_NE(set.value,nullptr);
    const auto root=Root(plan,set);
    EXPECT_FALSE(laplace_identity_equal(&root.entity_id,&fixture.target.entity_id));
    laplace_composition_working_set_summary summary{};
    ASSERT_EQ(laplace_composition_working_set_summary_get(set.value,&summary),LAPLACE_COMPOSITION_OK);
    EXPECT_EQ(summary.occurrence_count,0U);
    fixture.physicality.radius=-0.0;
    PlanOwner rejected;
    EXPECT_EQ(laplace_physicality_entity_plan_create(&input,&rejected.value),LAPLACE_PHYSICALITY_ENTITY_RECORD_INVALID);
}

TEST(PhysicalityEntity, CanonicalNumeralsAreSharedWithHighway) {
    Fixture fixture;const auto input=fixture.Input();PlanOwner plan;
    ASSERT_EQ(laplace_physicality_entity_plan_create(&input,&plan.value),LAPLACE_PHYSICALITY_ENTITY_OK);
    laplace_physicality_entity_plan_view view{};
    ASSERT_EQ(laplace_physicality_entity_plan_view_get(plan.value,&view),LAPLACE_PHYSICALITY_ENTITY_OK);
    SetOwner descriptor;fixture.Execute(view,descriptor);ASSERT_NE(descriptor.value,nullptr);
    struct HighwayOwner {
        laplace_highway_registry_ast_plan* value{};
        ~HighwayOwner(){laplace_highway_registry_ast_plan_destroy(&value);}
    } highway;
    ASSERT_EQ(laplace_highway_registry_ast_plan_create(&fixture.context.epochs[3],
        &fixture.context.epochs[0],&highway.value),LAPLACE_HIGHWAY_OK);
    laplace_highway_registry_ast_view h{};
    ASSERT_EQ(laplace_highway_registry_ast_plan_view(highway.value,&h),LAPLACE_HIGHWAY_OK);
    std::vector<laplace_composition_known_entity> known;
    for(std::uint64_t i=0U;i<h.atom_count;++i) known.push_back(Atom(h.atom_positions[i],fixture.context));
    const laplace_composition_working_set_input hi{&fixture.context,&h.source_fingerprint,
        &h.recipe_fingerprint,known.data(),known.size(),h.operands,h.operand_count,
        h.requests,h.request_count,4096U,0U};
    SetOwner registry;
    ASSERT_EQ(laplace_composition_working_set_create(&hi,&registry.value),LAPLACE_COMPOSITION_OK);
    const auto contains=[](const SetOwner& set,const laplace_id128& id){
        std::size_t count{};const auto* results=laplace_composition_working_set_results(set.value,&count);
        for(std::size_t i=0U;i<count;++i) if(laplace_identity_equal(&results[i].entity_id,&id)) return true;
        return false;
    };
    std::array<laplace_id128,6> tag_atoms{};
    constexpr std::array<std::uint32_t,6> spelling{'n','u','m','b','e','r'};
    ASSERT_EQ(laplace_identity_codepoint_batch(spelling.data(),spelling.size(),tag_atoms.data()),LAPLACE_IDENTITY_OK);
    laplace_id128 tag{};
    ASSERT_EQ(laplace_identity_composite(tag_atoms.data(),tag_atoms.size(),&tag),LAPLACE_IDENTITY_OK);
    for(const std::uint32_t position : {'0','1','3'}) {
        std::array<laplace_id128,2> children{{tag,{}}};laplace_id128 expected{};
        ASSERT_EQ(laplace_identity_codepoint(position,&children[1]),LAPLACE_IDENTITY_OK);
        ASSERT_EQ(laplace_identity_composite(children.data(),children.size(),&expected),LAPLACE_IDENTITY_OK);
        EXPECT_TRUE(contains(registry,expected));
        EXPECT_TRUE(contains(descriptor,expected));
    }
}
}  // namespace
