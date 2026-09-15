#include "laplace/physicality_occurrence_binding.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include <gtest/gtest.h>
#include "context_fixture.h"

namespace {

bool Equal(const laplace_digest256& a, const laplace_digest256& b) {
    return std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

bool Zero(const laplace_digest256& value) { return Equal(value, {}); }

struct Fixture {
    laplace_framework_context context = laplace_test_context(0U);
    std::array<laplace_composition_known_entity, 2> known{};
    laplace_persistence_physicality_record parent{};
    std::vector<laplace_trajectory_carrier> carriers;
    std::array<laplace_physicality_occurrence_binding, 2> bindings{};

    explicit Fixture(std::uint64_t metadata = 0U, std::uint64_t repetitions = 1U) {
        context.resource_grant.memory_bytes = UINT64_C(64) << 20U;
        for (std::size_t index = 0U; index < known.size(); ++index) {
            auto& atom = known[index];
            EXPECT_EQ(laplace_identity_codepoint_witness('A', &atom.entity_id, &atom.identity_witness), LAPLACE_IDENTITY_OK);
            atom.centroid.component[index] = 1.0;
            atom.atom = 'A';
            atom.has_atom = 1U;
            laplace_persistence_physicality_record body{};
            EXPECT_EQ(laplace_persistence_atomic_point_physicality(&atom.entity_id, 1U,
                &context.epochs[0], &context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY],
                &atom.centroid, &body), LAPLACE_PERSISTENCE_OK);
            atom.physicality_id = body.physicality_id;
        }
        const std::array<laplace_composition_operand, 2> operands{{
            {0U,repetitions,metadata,LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY,0U},
            {1U,repetitions,metadata,LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY,0U}}};
        const laplace_composition_request request{0U,2U,1U,1U,0U,
            context.epochs[0],context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY],{}};
        const laplace_composition_working_set_input input{&context,&context.epochs[0],
            &context.epochs[0],known.data(),known.size(),operands.data(),operands.size(),
            &request,1U,4096U,0U};
        laplace_composition_working_set* set{};
        EXPECT_EQ(laplace_composition_working_set_create(&input,&set),LAPLACE_COMPOSITION_OK);
        if (set == nullptr) return;
        EXPECT_EQ(laplace_composition_working_set_physicality_candidate_get(set,0U,&parent),LAPLACE_COMPOSITION_OK);
        const laplace_trajectory_carrier* actual{};
        std::size_t count{};
        EXPECT_EQ(laplace_composition_working_set_trajectory_candidate_view_get(set,0U,&actual,&count),LAPLACE_COMPOSITION_OK);
        if (actual != nullptr) carriers.assign(actual,actual+count);
        laplace_composition_working_set_destroy(&set);
        laplace_composition_occurrence occurrence{};
        if (carriers.empty()) return;
        EXPECT_EQ(laplace_trajectory_composition_decode_one(carriers.data(),1U,&occurrence),LAPLACE_TRAJECTORY_OK);
        for (std::size_t index=0U;index<bindings.size();++index)
            bindings[index]={parent.physicality_id,known[index].entity_id,known[index].physicality_id,
                index*repetitions+1U,repetitions,occurrence.metadata,LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_VERSION,0U};
    }

    laplace_physicality_occurrence_parent View() const { return {&parent,carriers.data(),carriers.size()}; }
    laplace_physicality_occurrence_status Validate(laplace_digest256* receipt) const {
        return laplace_physicality_occurrence_bindings_validate(&parent,carriers.data(),carriers.size(),
            bindings.data(),bindings.size(),64U,64U,4096U,receipt);
    }
};

TEST(PhysicalityOccurrenceBinding, OneStoredRunRetainsTwoActualSelectedPhysicalities) {
    Fixture fixture;
    ASSERT_EQ(fixture.carriers.size(),1U);
    ASSERT_EQ(fixture.parent.logical_count,2U);
    ASSERT_TRUE(laplace_identity_equal(&fixture.known[0].entity_id,&fixture.known[1].entity_id));
    ASSERT_FALSE(Equal(fixture.known[0].physicality_id,fixture.known[1].physicality_id));
    laplace_digest256 first{},repeat{};
    ASSERT_EQ(fixture.Validate(&first),LAPLACE_PHYSICALITY_OCCURRENCE_OK);
    ASSERT_EQ(fixture.Validate(&repeat),LAPLACE_PHYSICALITY_OCCURRENCE_OK);
    EXPECT_FALSE(Zero(first));
    EXPECT_TRUE(Equal(first,repeat));
    std::swap(fixture.bindings[0].selected_physicality_id,fixture.bindings[1].selected_physicality_id);
    ASSERT_EQ(fixture.Validate(&repeat),LAPLACE_PHYSICALITY_OCCURRENCE_OK);
    EXPECT_FALSE(Equal(first,repeat));
}

TEST(PhysicalityOccurrenceBinding, ScalarAndBatchUseOneReceiptLaw) {
    Fixture fixture;
    const auto parent=fixture.View();
    laplace_digest256 scalar{},batch{},per_parent{};
    ASSERT_EQ(fixture.Validate(&scalar),LAPLACE_PHYSICALITY_OCCURRENCE_OK);
    ASSERT_EQ(laplace_physicality_occurrence_bindings_validate_batch(&parent,1U,
        fixture.bindings.data(),fixture.bindings.size(),1U,64U,64U,4096U,&batch,&per_parent,1U),
        LAPLACE_PHYSICALITY_OCCURRENCE_OK);
    EXPECT_TRUE(Equal(scalar,batch));
    EXPECT_FALSE(Zero(per_parent));
}

TEST(PhysicalityOccurrenceBinding, ExplicitRunSplitsRemainInProvenance) {
    Fixture fixture;
    fixture.bindings[1].selected_physicality_id=fixture.bindings[0].selected_physicality_id;
    laplace_digest256 split{},merged{};
    ASSERT_EQ(fixture.Validate(&split),LAPLACE_PHYSICALITY_OCCURRENCE_OK);
    fixture.bindings[0].logical_count=2U;
    ASSERT_EQ(laplace_physicality_occurrence_bindings_validate(&fixture.parent,fixture.carriers.data(),
        fixture.carriers.size(),fixture.bindings.data(),1U,64U,64U,4096U,&merged),LAPLACE_PHYSICALITY_OCCURRENCE_OK);
    EXPECT_FALSE(Equal(split,merged));
}

TEST(PhysicalityOccurrenceBinding, IntervalsCrossCarrierSplitsAndCheckEveryMetadataWord) {
    Fixture fixture(0U,40000U);
    ASSERT_EQ(fixture.parent.logical_count,80000U);
    ASSERT_EQ(fixture.carriers.size(),2U);
    laplace_digest256 receipt{};
    ASSERT_EQ(laplace_physicality_occurrence_bindings_validate(&fixture.parent,fixture.carriers.data(),
        fixture.carriers.size(),fixture.bindings.data(),fixture.bindings.size(),2U,2U,80000U,&receipt),
        LAPLACE_PHYSICALITY_OCCURRENCE_OK);
    laplace_composition_occurrence first{},second{};
    ASSERT_EQ(laplace_trajectory_composition_decode_one(&fixture.carriers[0],1U,&first),LAPLACE_TRAJECTORY_OK);
    ASSERT_EQ(laplace_trajectory_composition_decode_one(&fixture.carriers[1],first.run_length+1U,&second),LAPLACE_TRAJECTORY_OK);
    second.metadata^=UINT64_C(1)<<6U;
    ASSERT_EQ(laplace_trajectory_composition_encode(&second.entity_id,second.logical_ordinal,
        second.run_length,second.metadata,&fixture.carriers[1]),LAPLACE_TRAJECTORY_OK);
    ASSERT_EQ(laplace_persistence_trajectory_fingerprint(fixture.carriers.data(),fixture.carriers.size(),
        &fixture.parent.trajectory_fingerprint),LAPLACE_PERSISTENCE_OK);
    ASSERT_EQ(laplace_persistence_physicality_identify(&fixture.parent,&fixture.parent.physicality_id),LAPLACE_PERSISTENCE_OK);
    for(auto& binding:fixture.bindings) binding.parent_physicality_id=fixture.parent.physicality_id;
    EXPECT_EQ(laplace_physicality_occurrence_bindings_validate(&fixture.parent,fixture.carriers.data(),
        fixture.carriers.size(),fixture.bindings.data(),fixture.bindings.size(),2U,2U,80000U,&receipt),
        LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_INVALID);
    EXPECT_TRUE(Zero(receipt));
}

TEST(PhysicalityOccurrenceBinding, GapsOverlapReorderMetadataAndOwnershipReject) {
    for (int variant=0;variant<11;++variant) {
        Fixture fixture;
        switch(variant) {
            case 0: fixture.bindings[0].first_logical_ordinal=2U; break;
            case 1: fixture.bindings[1].first_logical_ordinal=1U; break;
            case 2: std::swap(fixture.bindings[0],fixture.bindings[1]); break;
            case 3: fixture.bindings[1].logical_count=0U; break;
            case 4: fixture.bindings[1].logical_count=UINT64_MAX; break;
            case 5: fixture.bindings[1].metadata^=UINT64_C(1)<<12U; break;
            case 6: fixture.bindings[1].entity_id.bytes[0]^=1U; break;
            case 7: fixture.bindings[1].parent_physicality_id.bytes[0]^=1U; break;
            case 8: fixture.bindings[1].version=2U; break;
            case 9: fixture.bindings[1].reserved=1U; break;
            default: fixture.bindings[1].selected_physicality_id={}; break;
        }
        laplace_digest256 receipt{};
        std::memset(&receipt,0xa5,sizeof(receipt));
        EXPECT_EQ(fixture.Validate(&receipt),LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_INVALID)<<variant;
        EXPECT_TRUE(Zero(receipt))<<variant;
    }
}

TEST(PhysicalityOccurrenceBinding, ParentNativeIdentityAndCarrierCorruptionReject) {
    for (int variant=0;variant<3;++variant) {
        Fixture fixture;
        if (variant==0) fixture.parent.radius=0.25;
        else if (variant==1) reinterpret_cast<std::uint8_t*>(fixture.carriers.data())[0]^=1U;
        else {
            fixture.parent.entity_id.bytes[0]^=1U;
            ASSERT_EQ(laplace_persistence_physicality_identify(&fixture.parent,&fixture.parent.physicality_id),LAPLACE_PERSISTENCE_OK);
            for(auto& binding:fixture.bindings) binding.parent_physicality_id=fixture.parent.physicality_id;
        }
        laplace_digest256 receipt{};
        EXPECT_EQ(fixture.Validate(&receipt),LAPLACE_PHYSICALITY_OCCURRENCE_PARENT_INVALID)<<variant;
        EXPECT_TRUE(Zero(receipt));
    }
}

TEST(PhysicalityOccurrenceBinding, BatchOrderingCoverageAndFailurePublicationAreExact) {
    Fixture first,second(UINT64_C(1)<<6U);
    ASSERT_FALSE(Equal(first.parent.physicality_id,second.parent.physicality_id));
    std::array<const Fixture*,2> ordered{&first,&second};
    std::sort(ordered.begin(),ordered.end(),[](const Fixture* a,const Fixture* b){
        return std::memcmp(a->parent.physicality_id.bytes,b->parent.physicality_id.bytes,32U)<0;
    });
    std::array<laplace_physicality_occurrence_parent,2> parents{ordered[0]->View(),ordered[1]->View()};
    std::array<laplace_physicality_occurrence_binding,4> bindings{
        ordered[0]->bindings[0],ordered[0]->bindings[1],ordered[1]->bindings[0],ordered[1]->bindings[1]};
    laplace_digest256 batch{};
    std::array<laplace_digest256,2> receipts{};
    auto validate=[&](){return laplace_physicality_occurrence_bindings_validate_batch(parents.data(),2U,
        bindings.data(),4U,2U,64U,64U,4096U,&batch,receipts.data(),2U);};
    ASSERT_EQ(validate(),LAPLACE_PHYSICALITY_OCCURRENCE_OK);
    EXPECT_FALSE(Zero(batch));
    EXPECT_FALSE(Equal(receipts[0],receipts[1]));
    bindings[3].metadata^=UINT64_C(1)<<12U;
    EXPECT_EQ(validate(),LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_INVALID);
    EXPECT_TRUE(Zero(batch));
    EXPECT_TRUE(Zero(receipts[0]));
    EXPECT_TRUE(Zero(receipts[1]));
    bindings[3]=ordered[1]->bindings[1];
    std::swap(parents[0],parents[1]);
    EXPECT_EQ(validate(),LAPLACE_PHYSICALITY_OCCURRENCE_PARENT_INVALID);
    std::swap(parents[0],parents[1]);
    std::swap(bindings[0],bindings[2]);
    EXPECT_EQ(validate(),LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_INVALID);
}

TEST(PhysicalityOccurrenceBinding, AggregateLimitsPrecedeAnyParentHash) {
    Fixture fixture;
    fixture.parent.physicality_id.bytes[0]^=1U; // Would fail native identity if validation started.
    const auto parent=fixture.View();
    for(int variant=0;variant<4;++variant) {
        laplace_digest256 receipt{};
        EXPECT_EQ(laplace_physicality_occurrence_bindings_validate_batch(&parent,1U,
            fixture.bindings.data(),fixture.bindings.size(),variant==0?0U:1U,
            variant==1?0U:64U,variant==2?1U:64U,variant==3?1U:4096U,&receipt,nullptr,0U),
            LAPLACE_PHYSICALITY_OCCURRENCE_LIMIT);
        EXPECT_TRUE(Zero(receipt));
    }
    auto oversized=fixture.parent;
    oversized.logical_count=UINT64_MAX;
    const std::array<laplace_physicality_occurrence_parent,2> parents{parent,{&oversized,fixture.carriers.data(),fixture.carriers.size()}};
    laplace_digest256 receipt{};
    EXPECT_EQ(laplace_physicality_occurrence_bindings_validate_batch(parents.data(),2U,
        fixture.bindings.data(),fixture.bindings.size(),2U,64U,64U,UINT64_MAX,&receipt,nullptr,0U),
        LAPLACE_PHYSICALITY_OCCURRENCE_LIMIT);
    EXPECT_TRUE(Zero(receipt));
}

TEST(PhysicalityOccurrenceBinding, EmptySetAndFiniteMemoryBoundsAreExplicit) {
    laplace_digest256 empty{},again{};
    ASSERT_EQ(laplace_physicality_occurrence_bindings_validate_batch(nullptr,0U,nullptr,0U,
        0U,0U,0U,0U,&empty,nullptr,0U),LAPLACE_PHYSICALITY_OCCURRENCE_OK);
    ASSERT_EQ(laplace_physicality_occurrence_bindings_validate_batch(nullptr,0U,nullptr,0U,
        32U,64U,64U,4096U,&again,nullptr,0U),LAPLACE_PHYSICALITY_OCCURRENCE_OK);
    EXPECT_TRUE(Equal(empty,again));
    EXPECT_FALSE(Zero(empty));
    std::uint64_t bytes{},shared{};
    ASSERT_EQ(laplace_physicality_occurrence_bindings_memory_bound(64U,&bytes),LAPLACE_PHYSICALITY_OCCURRENCE_OK);
    ASSERT_EQ(laplace_physicality_entity_validation_memory_bound(64U,&shared),LAPLACE_PHYSICALITY_ENTITY_OK);
    EXPECT_EQ(bytes,shared);
    EXPECT_GT(bytes,0U);
    EXPECT_EQ(laplace_physicality_occurrence_bindings_memory_bound(UINT64_MAX,&bytes),LAPLACE_PHYSICALITY_OCCURRENCE_LIMIT);
    EXPECT_EQ(bytes,0U);
}

TEST(PhysicalityOccurrenceBinding, IncompleteParentSetsAndOutputCapacityReject) {
    Fixture fixture;
    const auto parent=fixture.View();
    laplace_digest256 receipt{};
    EXPECT_EQ(laplace_physicality_occurrence_bindings_validate_batch(&parent,1U,nullptr,0U,
        1U,64U,64U,4096U,&receipt,nullptr,0U),LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_INVALID);
    EXPECT_TRUE(Zero(receipt));
    EXPECT_EQ(laplace_physicality_occurrence_bindings_validate_batch(nullptr,0U,fixture.bindings.data(),2U,
        1U,64U,64U,4096U,&receipt,nullptr,0U),LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_INVALID);
    EXPECT_TRUE(Zero(receipt));
    laplace_digest256 parent_receipt{};
    EXPECT_EQ(laplace_physicality_occurrence_bindings_validate_batch(&parent,1U,fixture.bindings.data(),2U,
        1U,64U,64U,4096U,&receipt,&parent_receipt,0U),LAPLACE_PHYSICALITY_OCCURRENCE_LIMIT);
    EXPECT_TRUE(Zero(receipt));
    EXPECT_EQ(laplace_physicality_occurrence_bindings_validate_batch(nullptr,1U,fixture.bindings.data(),2U,
        1U,64U,64U,4096U,&receipt,nullptr,0U),LAPLACE_PHYSICALITY_OCCURRENCE_INVALID_ARGUMENT);
    std::array<laplace_digest256,2> bounded_outputs{};
    std::memset(bounded_outputs.data(),0xa5,sizeof(bounded_outputs));
    const auto untouched=bounded_outputs[1];
    EXPECT_EQ(laplace_physicality_occurrence_bindings_validate_batch(&parent,2U,fixture.bindings.data(),2U,
        1U,64U,64U,4096U,&receipt,bounded_outputs.data(),2U),LAPLACE_PHYSICALITY_OCCURRENCE_LIMIT);
    EXPECT_TRUE(Zero(bounded_outputs[0]));
    EXPECT_TRUE(Equal(bounded_outputs[1],untouched));
}

} // namespace
