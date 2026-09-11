#include "laplace/cognition_firmware.h"
#include "context_fixture.h"
#include "prompt_admission_fixture.h"
#include <array>
#include <fstream>
#include <vector>

namespace {
using namespace laplace_test::admission;
using Step = laplace_cognition_firmware_step;
using Binding = laplace_cognition_firmware_binding;
constexpr auto OBS = LAPLACE_COGNITION_FIRMWARE_OBSERVATION;
constexpr auto ANSWER = LAPLACE_COGNITION_FIRMWARE_ANSWER;
constexpr auto REALIZATION = LAPLACE_COGNITION_FIRMWARE_REALIZATION;
constexpr auto PREVIOUS = LAPLACE_COGNITION_FIRMWARE_PREVIOUS_REALIZATION;
constexpr auto SEMANTIC = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
constexpr auto SUCCESSOR = LAPLACE_OBSERVATION_QUERY_SUCCESSOR;
Step Query(Binding anchor, std::uint32_t relations, Binding goal = {}) {
    Step step{};
    step.kind = LAPLACE_COGNITION_FIRMWARE_EXECUTE;
    step.anchor = anchor;
    step.goal = goal;
    step.relation_mask = relations;
    return step;
}
Step Emit(std::uint32_t source, std::uint32_t modality = 'T', std::uint32_t encoding = 0) {
    Step step{};
    step.kind = LAPLACE_COGNITION_FIRMWARE_EMIT;
    step.anchor = {ANSWER, source};
    step.realization.modality_id = Codepoint(modality);
    step.realization.realization_recipe_epoch = Digest(0xc0);
    step.realization.maximum_candidates = 4;
    step.realization.version = LAPLACE_COGNITION_REALIZATION_VERSION;
    step.output_encoding = encoding;
    return step;
}
struct Edge {
    laplace_id128 from{}, to{};
    std::uint32_t family{};
    std::uint8_t witness{};
};
struct Surface {
    laplace_id128 meaning{}, modality{};
    std::uint32_t atom{};
};
struct World {
    std::vector<Edge> edges;
    std::vector<Surface> surfaces;
    std::size_t calls{}, realization_calls{}, materialization_calls{};
    bool fail_realization{}, fail_materialization{}, limited{};
    std::vector<laplace_id128> observed_sources;
    std::vector<laplace_digest256> contexts;
};
int Enumerate(void* opaque, const laplace_observation_query_binding* binding,
              const laplace_id128* ids, const laplace_query_search_state* states,
              const std::uint64_t*, std::size_t size,
              laplace_cognition_observation_candidate* out, std::size_t capacity,
              std::size_t* count, laplace_cognition_observation_candidate_usage* usage) {
    auto& world = *static_cast<World*>(opaque);
    ++world.calls;
    *count = 0;
    *usage = {};
    world.contexts.push_back(states[0].context_fingerprint);
    if (world.limited) {
        usage->limiting_disposition = LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN;
        return 0;
    }
    for (std::size_t i = 0; i < size; ++i) {
        world.observed_sources.push_back(ids[i]);
        for (const auto& edge : world.edges) {
            if (!SameId(ids[i], edge.from) || (edge.family & binding->relation_mask) == 0) continue;
            if (*count == capacity) return 1;
            auto& candidate = out[(*count)++];
            candidate = {};
            candidate.target_entity_id = edge.to;
            candidate.source_state_index = i;
            candidate.source_logical_ordinal = states[i].depth;
            candidate.target_logical_ordinal = states[i].depth + 1U;
            candidate.multiplicity = 1;
            candidate.gap = 1;
            candidate.relation_family = edge.family;
            candidate.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION;
            candidate.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD;
            candidate.observation_fingerprint = Digest(edge.witness);
            // Fixture relations are calculations, not manufactured independent testimony.
            if (edge.family == SEMANTIC) {
                candidate.relation_id = Codepoint('R');
                candidate.flags = LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT;
            }
        }
    }
    usage->rows_examined = *count;
    usage->crossing_count = *count;
    usage->index_plan_count = 1;
    return 0;
}
int Realize(void* opaque, const laplace_cognition_semantic_act* act,
            const laplace_cognition_realization_request* request,
            laplace_cognition_realization_candidate* out, std::size_t capacity,
            std::size_t* count, laplace_cognition_realization_usage* usage) {
    auto& world = *static_cast<World*>(opaque);
    ++world.realization_calls;
    if (world.fail_realization) return 1;
    *count = 0;
    *usage = {};
    usage->provider_receipt_id = Digest(0xab);
    for (const auto& surface : world.surfaces) {
        if (!SameId(surface.meaning, act->primary_answer.entity_id) ||
            !SameId(surface.modality, request->modality_id)) continue;
        if (*count == capacity) return 2;
        auto& item = out[(*count)++];
        item = {};
        item.content_id = Codepoint(surface.atom);
        item.language_id = request->language_id;
        item.candidate_receipt_id = Digest(static_cast<std::uint8_t>(surface.atom));
        item.realization_recipe_id = Digest(0xac);
        item.obligation_fingerprint = act->act_id;
        item.reused_subtree_count = 1;
        item.match_class = LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE;
        item.flags = LAPLACE_COGNITION_REALIZATION_CANDIDATE_REUSED_WITNESSED;
    }
    usage->rows_examined = *count;
    usage->exact_whole_examined = *count;
    usage->disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE;
    return 0;
}
int ResolveMaterialization(void* opaque, const laplace_id128* id,
                           laplace_cognition_materialization_node* node) {
    auto& world = *static_cast<World*>(opaque);
    ++world.materialization_calls;
    if (world.fail_materialization) return 1;
    for (const auto& surface : world.surfaces) {
        if (!SameId(*id, Codepoint(surface.atom))) continue;
        *node = {};
        if (laplace_identity_codepoint_witness(surface.atom, &node->entity_id,
            &node->identity_witness) != LAPLACE_IDENTITY_OK) return 2;
        node->node_receipt_id = Digest(static_cast<std::uint8_t>(surface.atom));
        node->logical_count = 1;
        node->atom = surface.atom;
        node->kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_ATOM;
        return 0;
    }
    return 3;
}
int NoTrajectory(void*, const laplace_cognition_materialization_node*,
                 laplace_trajectory_carrier*, std::size_t, laplace_digest256*) { return 1; }
struct Result {
    laplace_cognition_firmware_result* value{};
    ~Result() { laplace_cognition_firmware_result_destroy(&value); }
    std::string output() const {
        const std::uint8_t* bytes = nullptr; std::size_t size = 0;
        EXPECT_EQ(laplace_cognition_firmware_result_output(value,&bytes,&size), LAPLACE_COGNITION_FIRMWARE_OK);
        return bytes == nullptr ? std::string{} : std::string(reinterpret_cast<const char*>(bytes),size);
    }
    std::vector<std::uint8_t> checkpoint() const {
        const std::uint8_t* bytes = nullptr; std::size_t size = 0;
        EXPECT_EQ(laplace_cognition_firmware_result_checkpoint(value,&bytes,&size), LAPLACE_COGNITION_FIRMWARE_OK);
        return bytes == nullptr ? std::vector<std::uint8_t>{} : std::vector<std::uint8_t>(bytes,bytes+size);
    }
    laplace_cognition_firmware_step_receipt step(std::size_t i) const {
        laplace_cognition_firmware_step_receipt out{};
        EXPECT_EQ(laplace_cognition_firmware_result_step(value,i,&out), LAPLACE_COGNITION_FIRMWARE_OK);
        return out;
    }
    laplace_cognition_firmware_receipt receipt() const {
        laplace_cognition_firmware_receipt out{};
        EXPECT_EQ(laplace_cognition_firmware_result_receipt(value,&out), LAPLACE_COGNITION_FIRMWARE_OK);
        return out;
    }
};
class CognitionFirmware : public ::testing::Test {
protected:
    World world;
    std::vector<Step> steps;
    laplace_cognition_firmware_program program{};
    laplace_cognition_firmware_request request{};
    laplace_framework_context context{};
    StructureFixture grammar;
    AtomFixture atoms;
    PresenceFixture presence;
    laplace_cognition_prompt_admission* admission{};
    laplace_cognition_prompt_admission_view observation{};
    laplace_cognition_observation_candidate_provider_v1 provider{};
    laplace_cognition_realization_provider_v1 realizer{};
    laplace_cognition_materialization_provider_v1 materializer{};
    laplace_cognition_firmware_error error{};
    std::string text = "the whole observation, not a selected word";
    void SetUp() override {
        steps = {Query({OBS,0},SEMANTIC),Query({OBS,0},SEMANTIC,{ANSWER,0}),Emit(1),
                 Query({REALIZATION,2},SUCCESSOR),Emit(3)};
        steps[0].kind=LAPLACE_COGNITION_FIRMWARE_INTERPRET;
        context=laplace_test_context(3);
        context.resource_grant.memory_bytes=64U*1024U*1024U;
        request.evidence_boundary=Digest(0xa0);
        request.result_contract_fingerprint=Digest(0xa1);
        request.search_budget={64,128,128,64,1024U*1024U,64,64,64,8,1,8,128};
        request.forward_limits={16,32,32,32,16,8192,128,128,4,4};
        request.materialization={8,8,64,8,LAPLACE_COGNITION_MATERIALIZATION_VERSION};
        request.maximum_output_bytes=128;
        request.maximum_checkpoint_bytes=4096;
        request.boundary_flags=LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE;
        request.version=LAPLACE_COGNITION_FIRMWARE_VERSION;
        provider.state=&world;provider.enumerate_candidates=Enumerate;
        provider.provider_fingerprint=Digest(0xa2);
        provider.maximum_candidate_records_per_expansion=8;
        provider.abi_major=LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
        realizer.state=&world;realizer.enumerate=Realize;realizer.provider_fingerprint=Digest(0xa3);
        realizer.maximum_candidate_records=4;realizer.abi_major=LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MAJOR;
        materializer.state=&world;materializer.provider_fingerprint=Digest(0xa4);
        materializer.resolve_node=ResolveMaterialization;materializer.read_trajectory=NoTrajectory;
        materializer.abi_major=LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MAJOR;
        Admit();
        world.edges={{observation.trunk_entity_id,Codepoint('K'),SEMANTIC,10},
                     {Codepoint('A'),Codepoint('L'),SUCCESSOR,11}};
        world.surfaces={{Codepoint('K'),Codepoint('T'),'A'}, {Codepoint('L'),Codepoint('T'),'B'}};
    }
    void TearDown() override { laplace_cognition_prompt_admission_destroy(&admission); }
    void Admit(std::uint64_t ordinal=0, std::uint8_t world_tag=0x83) {
        program={steps.data(),static_cast<std::uint32_t>(steps.size()),LAPLACE_COGNITION_FIRMWARE_VERSION};
        ASSERT_EQ(laplace_cognition_firmware_identify(&program,&request.selected_program),LAPLACE_COGNITION_FIRMWARE_OK);
        context.epochs[LAPLACE_FRAMEWORK_EPOCH_FIRMWARE]=request.selected_program;
        auto g=StructureProvider(&grammar);auto a=AtomProvider(&atoms);auto p=PresenceProvider(&presence);
        auto input=PromptInput(text,&context,&g);
        ASSERT_EQ(laplace_framework_context_fingerprint(&context,&input.occurrence.context_fingerprint),LAPLACE_FRAMEWORK_OK);
        input.occurrence.turn_ordinal=ordinal;
        input.occurrence.turn_flags=ordinal==0?0U:static_cast<std::uint32_t>(LAPLACE_COGNITION_TURN_HAS_PREVIOUS_DISCOURSE);
        input.occurrence.world_id=Digest(world_tag);
        laplace_cognition_prompt_admission_destroy(&admission);
        ASSERT_EQ(laplace_cognition_prompt_admission_create(&input,&a,&p,&admission),LAPLACE_COGNITION_PROMPT_ADMISSION_OK);
        ASSERT_EQ(laplace_cognition_prompt_admission_view_get(admission,&observation),LAPLACE_COGNITION_PROMPT_ADMISSION_OK);
    }
    laplace_cognition_firmware_status Run(Result& result, const std::vector<std::uint8_t>& previous={},
                                         laplace_framework_cancel_requested_fn cancel=nullptr,void* state=nullptr) {
        return laplace_cognition_firmware_execute(&program,&request,&context,admission,
            previous.empty()?nullptr:previous.data(),previous.size(),&provider,1,&realizer,&materializer,
            cancel,state,&result.value,&error);
    }
};
TEST_F(CognitionFirmware, WholeObservationBindsNativeGoalEmitsAndFeedsBack) {
    Result result;
    ASSERT_EQ(Run(result),LAPLACE_COGNITION_FIRMWARE_OK) << error.step_index << ":" << error.native_status;
    EXPECT_EQ(result.output(),"AB");
    EXPECT_TRUE(SameId(result.step(0).request.anchor_entity_id,observation.trunk_entity_id));
    EXPECT_TRUE(SameId(result.step(1).request.goal_entity_id,result.step(0).act.primary_answer.entity_id));
    EXPECT_TRUE(SameId(result.step(3).request.anchor_entity_id,result.step(2).realization.content_id));
    EXPECT_TRUE(SameDigest(result.step(2).next_feedback,result.step(3).prior_feedback));
    EXPECT_EQ(result.receipt().completed_steps,5U);
    EXPECT_EQ(result.receipt().emitted_parts,2U);
    EXPECT_EQ(result.receipt().layer_count,3U);
    EXPECT_EQ(observation.semantic_attestation_count,0U);
    EXPECT_EQ(result.step(0).act.primary_answer.independent_evidence_root_count,0U);
}
TEST_F(CognitionFirmware, DataChangesGoalAndEmissionWithoutChangingFirmwareOrTransport) {
    Result first,second;
    ASSERT_EQ(Run(first),LAPLACE_COGNITION_FIRMWARE_OK);
    world.edges[0].to=Codepoint('Q');world.surfaces[0]={Codepoint('Q'),Codepoint('T'),'C'};
    world.edges[1].from=Codepoint('C');
    ASSERT_EQ(Run(second),LAPLACE_COGNITION_FIRMWARE_OK);
    EXPECT_EQ(first.output(),"AB");EXPECT_EQ(second.output(),"CB");
    EXPECT_TRUE(SameDigest(first.receipt().program_id,second.receipt().program_id));
    EXPECT_FALSE(SameId(first.step(1).request.goal_entity_id,second.step(1).request.goal_entity_id));
    EXPECT_FALSE(SameDigest(first.step(3).request.context_fingerprint,second.step(3).request.context_fingerprint));
}
TEST_F(CognitionFirmware, SelectedModalityChangesActualBytesAndSubsequentOperation) {
    Result text_result,binary_result;
    ASSERT_EQ(Run(text_result),LAPLACE_COGNITION_FIRMWARE_OK);
    steps[2]=Emit(1,'I',LAPLACE_COGNITION_OUTPUT_OCTETS);
    world.surfaces.push_back({Codepoint('K'),Codepoint('I'),0x80});
    world.edges.push_back({Codepoint(0x80),Codepoint('M'),SUCCESSOR,13});
    world.surfaces.push_back({Codepoint('M'),Codepoint('T'),'Z'});
    Admit();
    ASSERT_EQ(Run(binary_result),LAPLACE_COGNITION_FIRMWARE_OK) << error.step_index << ":" << error.native_status;
    EXPECT_EQ(binary_result.output(),std::string("\x80Z",2));
    EXPECT_FALSE(SameDigest(text_result.receipt().program_id,binary_result.receipt().program_id));
    EXPECT_TRUE(SameId(binary_result.step(3).request.anchor_entity_id,Codepoint(0x80)));
}
TEST_F(CognitionFirmware, AmbiguousBindingDoesNotSilentlyChooseFirstCandidate) {
    world.edges.push_back({observation.trunk_entity_id,Codepoint('Q'),SEMANTIC,15});
    Result result;EXPECT_EQ(Run(result),LAPLACE_COGNITION_FIRMWARE_AMBIGUOUS);
    EXPECT_EQ(error.step_index,0U);EXPECT_EQ(result.value,nullptr);EXPECT_EQ(world.realization_calls,0U);
}
TEST_F(CognitionFirmware, DistinctPathsToSameEntityDoNotInventAmbiguityOrEvidence) {
    world.edges.push_back({observation.trunk_entity_id,Codepoint('K'),SEMANTIC,16});
    Result result;ASSERT_EQ(Run(result),LAPLACE_COGNITION_FIRMWARE_OK);
    EXPECT_EQ(result.output(),"AB");EXPECT_EQ(result.step(0).act.primary_answer.independent_evidence_root_count,0U);
}
TEST_F(CognitionFirmware, IncompleteCandidateBoundaryNeverBecomesCompletedAnswer) {
    world.limited=true;
    Result result;EXPECT_NE(Run(result),LAPLACE_COGNITION_FIRMWARE_OK);EXPECT_EQ(result.value,nullptr);
    EXPECT_EQ(world.realization_calls,0U);
}
TEST_F(CognitionFirmware, CheckpointRestoresPriorEmittedValueAfterResultDestruction) {
    Result first;
    ASSERT_EQ(Run(first),LAPLACE_COGNITION_FIRMWARE_OK);
    auto checkpoint=first.checkpoint(); auto fingerprint=first.receipt().next_checkpoint_fingerprint;
    request.expected_previous_checkpoint=fingerprint;request.previous_checkpoint_present=1U;
    laplace_cognition_firmware_result_destroy(&first.value);
    steps[3].anchor={PREVIOUS,2};Admit(1);
    // The current emission changes, but the next operation explicitly consumes
    // the prior turn's emission from the decoded checkpoint, not the new value.
    world.surfaces[0].atom='C';
    Result second;ASSERT_EQ(Run(second,checkpoint),LAPLACE_COGNITION_FIRMWARE_OK) << error.step_index << ":" << error.native_status;
    EXPECT_EQ(second.output(),"CB");EXPECT_TRUE(SameId(second.step(3).request.anchor_entity_id,Codepoint('A')));
    EXPECT_TRUE(SameDigest(second.receipt().previous_checkpoint_fingerprint,fingerprint));
}
TEST_F(CognitionFirmware, InvalidCheckpointWorldSequenceAndBytesFailBeforeQuery) {
    Result first;ASSERT_EQ(Run(first),LAPLACE_COGNITION_FIRMWARE_OK);
    auto checkpoint=first.checkpoint();std::size_t calls=world.calls;
    request.expected_previous_checkpoint=first.receipt().next_checkpoint_fingerprint;request.previous_checkpoint_present=1U;
    Admit(1);checkpoint.back()^=1;Result invalid;
    EXPECT_EQ(Run(invalid,checkpoint),LAPLACE_COGNITION_FIRMWARE_CHECKPOINT_INVALID);
    EXPECT_EQ(world.calls,calls);checkpoint.back()^=1;
    Admit(1,0x93);EXPECT_EQ(Run(invalid,checkpoint),LAPLACE_COGNITION_FIRMWARE_CHECKPOINT_INVALID);
    EXPECT_EQ(world.calls,calls);
    Admit(2);EXPECT_EQ(Run(invalid,checkpoint),LAPLACE_COGNITION_FIRMWARE_CHECKPOINT_INVALID);
    EXPECT_EQ(world.calls,calls);
}
TEST_F(CognitionFirmware, ProgramEpochMismatchAndForwardReferencesRejectBeforeProviders) {
    Result result;request.selected_program.bytes[0]^=1;
    EXPECT_EQ(Run(result),LAPLACE_COGNITION_FIRMWARE_PROGRAM_MISMATCH);EXPECT_EQ(world.calls,0U);
    request.selected_program.bytes[0]^=1;context.epochs[LAPLACE_FRAMEWORK_EPOCH_FIRMWARE].bytes[0]^=1;
    EXPECT_EQ(Run(result),LAPLACE_COGNITION_FIRMWARE_CONTEXT_INVALID);EXPECT_EQ(world.calls,0U);
    context.epochs[LAPLACE_FRAMEWORK_EPOCH_FIRMWARE].bytes[0]^=1;
    steps[1].goal={ANSWER,4};
    EXPECT_EQ(Run(result),LAPLACE_COGNITION_FIRMWARE_PROGRAM_INVALID);EXPECT_EQ(world.calls,0U);
}
TEST_F(CognitionFirmware, LaterFailureWithholdsEarlierOutputAndCheckpoint) {
    world.edges.clear();world.edges.push_back({observation.trunk_entity_id,Codepoint('K'),SEMANTIC,10});
    Result result;EXPECT_NE(Run(result),LAPLACE_COGNITION_FIRMWARE_OK);
    EXPECT_EQ(error.step_index,3U);EXPECT_EQ(world.realization_calls,1U);EXPECT_EQ(result.value,nullptr);
}
TEST_F(CognitionFirmware, WholeProgramGrantIsNotResetBetweenSteps) {
    request.forward_limits.max_layers=1;
    Result result;EXPECT_EQ(Run(result),LAPLACE_COGNITION_FIRMWARE_LIMIT);EXPECT_EQ(error.step_index,1U);
    EXPECT_EQ(result.value,nullptr);EXPECT_EQ(world.realization_calls,0U);
}
TEST_F(CognitionFirmware, CancellationAfterFirstEmissionPublishesNothing) {
    auto cancelled=[](void* value)->int{return static_cast<World*>(value)->realization_calls!=0U?1:0;};
    Result result;EXPECT_EQ(Run(result,{},cancelled,&world),LAPLACE_COGNITION_FIRMWARE_CANCELLED);
    EXPECT_EQ(error.step_index,3U);EXPECT_EQ(result.value,nullptr);
}
TEST_F(CognitionFirmware, DeterministicReplayRetainsExactOutputTraceAndCheckpoint) {
    Result first,second;ASSERT_EQ(Run(first),LAPLACE_COGNITION_FIRMWARE_OK);
    ASSERT_EQ(Run(second),LAPLACE_COGNITION_FIRMWARE_OK);
    EXPECT_EQ(first.output(),second.output());EXPECT_EQ(first.checkpoint(),second.checkpoint());
    EXPECT_TRUE(SameDigest(first.receipt().receipt_id,second.receipt().receipt_id));
}
TEST_F(CognitionFirmware, OutputAndMemoryBoundsRejectWithoutPublishedPrefix) {
    Result result;request.maximum_checkpoint_bytes=660;
    EXPECT_EQ(Run(result),LAPLACE_COGNITION_FIRMWARE_LIMIT);EXPECT_EQ(world.calls,0U);
    request.maximum_checkpoint_bytes=4096;request.maximum_output_bytes=UINT64_MAX;
    EXPECT_EQ(Run(result),LAPLACE_COGNITION_FIRMWARE_LIMIT);EXPECT_EQ(world.calls,0U);
    request.maximum_output_bytes=1;
    EXPECT_EQ(Run(result),LAPLACE_COGNITION_FIRMWARE_LIMIT);EXPECT_EQ(result.value,nullptr);
}
TEST_F(CognitionFirmware, ExternalProviderWorkspaceIsReservedBeforeExecution) {
    Result exhausted, accepted;
    EXPECT_EQ(laplace_cognition_firmware_execute_with_provider_workspace(
        &program,&request,&context,admission,nullptr,0,&provider,1,&realizer,&materializer,
        nullptr,nullptr,context.resource_grant.memory_bytes,&exhausted.value,&error),
        LAPLACE_COGNITION_FIRMWARE_LIMIT);
    EXPECT_EQ(world.calls,0U); EXPECT_EQ(exhausted.value,nullptr);
    ASSERT_EQ(laplace_cognition_firmware_execute_with_provider_workspace(
        &program,&request,&context,admission,nullptr,0,&provider,1,&realizer,&materializer,
        nullptr,nullptr,4096U,&accepted.value,&error), LAPLACE_COGNITION_FIRMWARE_OK);
    EXPECT_EQ(accepted.output(),"AB");
}
TEST_F(CognitionFirmware, ProviderWorkspaceOverflowCannotWrapIntoAvailableMemory) {
    Result result;
    EXPECT_EQ(laplace_cognition_firmware_execute_with_provider_workspace(
        &program,&request,&context,admission,nullptr,0,&provider,1,&realizer,&materializer,
        nullptr,nullptr,UINT64_MAX,&result.value,&error),LAPLACE_COGNITION_FIRMWARE_LIMIT);
    EXPECT_EQ(world.calls,0U); EXPECT_EQ(result.value,nullptr);
}
struct Image {
    laplace_cognition_firmware_image* value{};
    ~Image(){laplace_cognition_firmware_image_destroy(&value);}
};
TEST_F(CognitionFirmware, StoredProgramRoundTripsAndExecutesWithoutCallerGoal) {
    Image original,loaded;
    ASSERT_EQ(laplace_cognition_firmware_image_create(&program,65536,&original.value),LAPLACE_COGNITION_FIRMWARE_OK);
    const std::uint8_t* image_bytes=nullptr;std::size_t bytes=0;
    laplace_cognition_firmware_program view{};laplace_digest256 id{};
    ASSERT_EQ(laplace_cognition_firmware_image_view(original.value,&view,&image_bytes,&bytes,&id),LAPLACE_COGNITION_FIRMWARE_OK);
    EXPECT_EQ(bytes,8U+120U*steps.size());
    std::vector<std::uint8_t> retained(image_bytes,image_bytes+bytes);
    laplace_cognition_firmware_image_destroy(&original.value);
    ASSERT_EQ(laplace_cognition_firmware_image_load(retained.data(),retained.size(),&id,65536,&loaded.value),LAPLACE_COGNITION_FIRMWARE_OK);
    ASSERT_EQ(laplace_cognition_firmware_image_view(loaded.value,&view,&image_bytes,&bytes,&id),LAPLACE_COGNITION_FIRMWARE_OK);
    auto old_program=program;program=view;Result result;
    ASSERT_EQ(Run(result),LAPLACE_COGNITION_FIRMWARE_OK);EXPECT_EQ(result.output(),"AB");
    program=old_program;
}
TEST_F(CognitionFirmware, ProgramImageRejectsTamperingLengthAndMemoryBeforeAllocation) {
    Image image,invalid;
    ASSERT_EQ(laplace_cognition_firmware_image_create(&program,65536,&image.value),LAPLACE_COGNITION_FIRMWARE_OK);
    const std::uint8_t* bytes=nullptr;std::size_t count=0;laplace_cognition_firmware_program view{};laplace_digest256 id{};
    ASSERT_EQ(laplace_cognition_firmware_image_view(image.value,&view,&bytes,&count,&id),LAPLACE_COGNITION_FIRMWARE_OK);
    for(std::size_t i=0;i<count;++i){
        std::vector<std::uint8_t> changed(bytes,bytes+count);changed[i]^=0x80;
        EXPECT_NE(laplace_cognition_firmware_image_load(changed.data(),changed.size(),&id,65536,&invalid.value),LAPLACE_COGNITION_FIRMWARE_OK);
        EXPECT_EQ(invalid.value,nullptr);
    }
    EXPECT_EQ(laplace_cognition_firmware_image_load(bytes,count-1U,&id,65536,&invalid.value),LAPLACE_COGNITION_FIRMWARE_PROGRAM_INVALID);
    EXPECT_EQ(laplace_cognition_firmware_image_load(bytes,count,&id,1,&invalid.value),LAPLACE_COGNITION_FIRMWARE_LIMIT);
}
struct StageSink {
    std::vector<std::vector<std::uint8_t>> batches;
    bool began{},sealed{},aborted{},fail_stage{},fail_seal{};
    static laplace_framework_status Begin(void* state,const laplace_framework_context*,std::uint32_t,std::uint64_t,std::uint64_t){
        auto& self=*static_cast<StageSink*>(state);self.began=true;self.batches.clear();self.sealed=false;self.aborted=false;
        return LAPLACE_FRAMEWORK_OK;
    }
    static laplace_framework_status Stage(void* state,const laplace_framework_canonical_batch* batch){
        auto& self=*static_cast<StageSink*>(state);
        if(self.fail_stage)return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
        self.batches.emplace_back(batch->canonical_bytes,batch->canonical_bytes+batch->byte_count);
        return LAPLACE_FRAMEWORK_OK;
    }
    static laplace_framework_status Seal(void* state,const laplace_digest256* stream,laplace_digest256* artifact){
        auto& self=*static_cast<StageSink*>(state);
        if(self.fail_seal)return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
        *artifact=*stream;self.sealed=true;return LAPLACE_FRAMEWORK_OK;
    }
    static void Abort(void* state){
        auto& self=*static_cast<StageSink*>(state);self.aborted=true;self.sealed=false;self.batches.clear();
    }
    laplace_framework_sink_v1 provider(){
        return {this,Begin,Stage,Seal,Abort,LAPLACE_FRAMEWORK_SINK_ABI_MAJOR,LAPLACE_FRAMEWORK_SINK_ABI_MINOR,0,0};
    }
};
struct Activation {
    laplace_digest256 active{};
    bool prepared{},committed{},aborted{},fail_commit{};
    StageSink* input{};StageSink* output{};
    static laplace_framework_status Prepare(void* state,const laplace_framework_context* context,
        const laplace_framework_stream_receipt* receipt,const laplace_framework_activation_request* request,
        laplace_digest256* preparation){
        auto& self=*static_cast<Activation*>(state);
        if(!self.input->sealed||!self.output->sealed||!SameDigest(self.active,request->expected_epoch))
            return LAPLACE_FRAMEWORK_ACTIVATION_ADMISSION_FAILED;
        EXPECT_TRUE(SameDigest(context->epochs[LAPLACE_FRAMEWORK_EPOCH_DATABASE],self.active));
        *preparation=receipt->receipt_id;self.prepared=true;return LAPLACE_FRAMEWORK_OK;
    }
    static laplace_framework_status Commit(void* state,const laplace_framework_activation_request* request,
        const laplace_digest256* preparation,laplace_digest256* activated){
        auto& self=*static_cast<Activation*>(state);
        if(self.fail_commit||!SameDigest(self.active,request->expected_epoch))return LAPLACE_FRAMEWORK_ACTIVATION_COMMIT_FAILED;
        self.active=request->next_epoch;self.committed=true;*activated=*preparation;return LAPLACE_FRAMEWORK_OK;
    }
    static void Abort(void* state,const laplace_framework_activation_request*,const laplace_digest256*){
        static_cast<Activation*>(state)->aborted=true;
    }
    laplace_framework_activation_provider_v1 provider(){
        return {this,Prepare,Commit,Abort,LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MAJOR,
            LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MINOR,0,0};
    }
};
class FirmwareRuntime : public CognitionFirmware {
protected:
    StageSink input_sink,output_sink;
    laplace_framework_sink_v1 input_provider{},output_provider{};
    Activation state;
    laplace_framework_activation_provider_v1 activation{};
    laplace_cognition_firmware_runtime runtime{};
    laplace_cognition_prompt_admission_input raw{};
    laplace_cognition_prompt_atom_provider_v1 atom_provider{};
    laplace_composition_presence_provider_v1 presence_provider{};
    laplace_decomposition_provider_v1 structure_provider{};
    laplace_cognition_firmware_run_receipt run_receipt{};
    Image image;
    void SetUp() override {
        CognitionFirmware::SetUp();
        context.flags=0;Admit();
        structure_provider=StructureProvider(&grammar);atom_provider=AtomProvider(&atoms);presence_provider=PresenceProvider(&presence);
        raw=PromptInput(text,&context,&structure_provider);
        ASSERT_EQ(laplace_framework_context_fingerprint(&context,&raw.occurrence.context_fingerprint),LAPLACE_FRAMEWORK_OK);
        input_provider=input_sink.provider();output_provider=output_sink.provider();
        state.active=context.epochs[LAPLACE_FRAMEWORK_EPOCH_DATABASE];state.input=&input_sink;state.output=&output_sink;
        activation=state.provider();
        runtime.atoms=&atom_provider;runtime.presence=&presence_provider;
        runtime.cognition=&provider;runtime.cognition_count=1;runtime.realization=&realizer;runtime.materialization=&materializer;
        runtime.input_sinks=&input_provider;runtime.input_sink_count=1;runtime.result_sinks=&output_provider;runtime.result_sink_count=1;
        runtime.activation=&activation;runtime.result_record_type=LAPLACE_ISA_VALUE_U32_VECTOR;
        runtime.version=LAPLACE_COGNITION_FIRMWARE_VERSION;
        ASSERT_EQ(laplace_cognition_firmware_image_create(&program,65536,&image.value),LAPLACE_COGNITION_FIRMWARE_OK);
        world.calls=0;world.realization_calls=0;
    }
    laplace_cognition_firmware_status RawRun(Result& result){
        return laplace_cognition_firmware_run(image.value,&request,&raw,nullptr,0,&runtime,&result.value,&run_receipt,&error);
    }
};
TEST_F(FirmwareRuntime, RawObservationPublishesCanonicalInputStateAndOutputThroughSharedLifecycle) {
    Result result;ASSERT_EQ(RawRun(result),LAPLACE_COGNITION_FIRMWARE_OK) << error.native_status;
    EXPECT_EQ(result.output(),"AB");EXPECT_TRUE(input_sink.sealed);EXPECT_TRUE(output_sink.sealed);EXPECT_TRUE(state.committed);
    ASSERT_EQ(output_sink.batches.size(),4U);
    EXPECT_EQ(output_sink.batches[1],result.checkpoint());EXPECT_EQ(output_sink.batches[3],std::vector<std::uint8_t>({'A','B'}));
    EXPECT_EQ(run_receipt.activation.effect_disposition,LAPLACE_FRAMEWORK_EFFECT_ACTIVATED);
    EXPECT_EQ(run_receipt.input_staging.stream.effect_disposition,LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT);
    EXPECT_EQ(run_receipt.result_staging.effect_disposition,LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT);
    EXPECT_TRUE(SameDigest(run_receipt.activation.next_epoch,state.active));
    EXPECT_EQ(run_receipt.input_stream_present,1U);
    EXPECT_FALSE(input_sink.batches.empty());
}
TEST_F(FirmwareRuntime, FailureAfterInputStagingCannotPublishAnswerOrAdvanceGeneration) {
    output_sink.fail_seal=true;auto before=state.active;Result result;
    EXPECT_EQ(RawRun(result),LAPLACE_COGNITION_FIRMWARE_PUBLICATION_FAILURE);
    EXPECT_EQ(result.value,nullptr);EXPECT_EQ(run_receipt.version,0U);EXPECT_FALSE(state.prepared);
    EXPECT_TRUE(input_sink.sealed);EXPECT_TRUE(output_sink.aborted);EXPECT_TRUE(SameDigest(before,state.active));
}
TEST_F(FirmwareRuntime, FailedCompareAndSwapLeavesOnlyInertArtifactsAndNoSuccessResult) {
    state.fail_commit=true;auto before=state.active;Result result;
    EXPECT_EQ(RawRun(result),LAPLACE_COGNITION_FIRMWARE_PUBLICATION_FAILURE);
    EXPECT_EQ(result.value,nullptr);EXPECT_TRUE(state.prepared);EXPECT_TRUE(state.aborted);EXPECT_FALSE(state.committed);
    EXPECT_TRUE(SameDigest(before,state.active));EXPECT_EQ(run_receipt.version,0U);
}
TEST_F(FirmwareRuntime, ReadOnlyAuthorityCannotBeEscalatedByProgramOrPrompt) {
    context.flags=LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY;Result result;
    auto calls=atoms.calls;
    EXPECT_EQ(RawRun(result),LAPLACE_COGNITION_FIRMWARE_DENIED);
    EXPECT_EQ(atoms.calls,calls);EXPECT_EQ(world.calls,0U);EXPECT_FALSE(state.prepared);EXPECT_FALSE(input_sink.began);
}
TEST_F(FirmwareRuntime, ForeignProgramOrMalformedSinkRejectsBeforeAnyAdmission) {
    Result result;auto calls=atoms.calls;
    request.selected_program.bytes[0]^=1;
    EXPECT_EQ(RawRun(result),LAPLACE_COGNITION_FIRMWARE_PROGRAM_MISMATCH);
    request.selected_program.bytes[0]^=1;output_provider.abort=nullptr;
    EXPECT_EQ(RawRun(result),LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT);
    EXPECT_EQ(atoms.calls,calls);EXPECT_EQ(world.calls,0U);
}
TEST_F(FirmwareRuntime, ComputationFailureDoesNotEvenBeginStaging) {
    world.fail_realization=true;Result result;
    EXPECT_EQ(RawRun(result),LAPLACE_COGNITION_FIRMWARE_REALIZATION_FAILURE);
    EXPECT_FALSE(input_sink.began);EXPECT_FALSE(output_sink.began);EXPECT_FALSE(state.prepared);EXPECT_EQ(result.value,nullptr);
}

}  // namespace
