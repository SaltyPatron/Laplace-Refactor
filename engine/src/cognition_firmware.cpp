#include "laplace/cognition_firmware.h"
#include "blake3.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

namespace firmware {
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t RegisterBytes = 100U;
constexpr std::size_t FrameHeaderBytes = LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES + 8U;
constexpr std::uint32_t NoStep = UINT32_MAX;
struct Failure { laplace_cognition_firmware_status status; std::uint32_t native{}; std::uint32_t disposition{}; };
[[noreturn]] void Fail(laplace_cognition_firmware_status s, std::uint32_t n = 0U, std::uint32_t d = 0U) {
    throw Failure{s,n,d};
}
template<class T> bool Same(const T& a, const T& b) {
    return std::memcmp(a.bytes,b.bytes,sizeof(a.bytes)) == 0;
}
template<class T> bool Zero(const T& a) {
    return std::all_of(std::begin(a.bytes),std::end(a.bytes),[](auto b){return b==0U;});
}
void U32(Bytes& b, std::uint32_t x) { for(unsigned i=0;i<4U;++i)b.push_back(static_cast<std::uint8_t>(x>>(8U*i))); }
void U64(Bytes& b, std::uint64_t x) { for(unsigned i=0;i<8U;++i)b.push_back(static_cast<std::uint8_t>(x>>(8U*i))); }
template<class T> void Id(Bytes& b,const T& x) { b.insert(b.end(),std::begin(x.bytes),std::end(x.bytes)); }
void Raw(Bytes& b,const std::uint8_t* p,std::size_t n) { if(n!=0U)b.insert(b.end(),p,p+n); }
std::uint32_t Read32(const std::uint8_t*& p) { std::uint32_t v=0;for(unsigned i=0;i<4U;++i)v|=std::uint32_t(*p++)<<(8U*i);return v; }
template<class T> void ReadId(const std::uint8_t*& p,T& id) { std::memcpy(id.bytes,p,sizeof(id.bytes));p+=sizeof(id.bytes); }
std::uint64_t Add(std::uint64_t a,std::uint64_t b) {
    if(b>UINT64_MAX-a)Fail(LAPLACE_COGNITION_FIRMWARE_LIMIT);
    return a+b;
}
std::uint64_t Mul(std::uint64_t a,std::uint64_t b) {
    if(a!=0U && b>UINT64_MAX/a)Fail(LAPLACE_COGNITION_FIRMWARE_LIMIT);
    return a*b;
}
std::size_t Size(std::uint64_t n) {
    if(n>std::numeric_limits<std::size_t>::max())Fail(LAPLACE_COGNITION_FIRMWARE_LIMIT);
    return static_cast<std::size_t>(n);
}
laplace_digest256 Hash(const char* domain,const std::uint8_t* data,std::size_t n) {
    blake3_hasher h{};blake3_hasher_init(&h);
    blake3_hasher_update(&h,domain,std::strlen(domain));
    if(n!=0U)blake3_hasher_update(&h,data,n);
    laplace_digest256 result{};blake3_hasher_finalize(&h,result.bytes,sizeof(result.bytes));return result;
}
laplace_digest256 Hash(const char* domain,const Bytes& b) { return Hash(domain,b.data(),b.size()); }

struct Register {
    std::uint32_t flags{};
    laplace_id128 answer{}, realization{};
    laplace_digest256 act{}, materialization{};
};
void EncodeRegisters(const std::vector<Register>& r,Bytes& b) {
    U32(b,static_cast<std::uint32_t>(r.size()));U32(b,LAPLACE_COGNITION_FIRMWARE_VERSION);
    for(const auto& v:r) { U32(b,v.flags);Id(b,v.answer);Id(b,v.realization);Id(b,v.act);Id(b,v.materialization); }
}
struct Predecessor {
    laplace_cognition_discourse_state state{};
    laplace_cognition_discourse_frame_receipt receipt{};
    laplace_digest256 checkpoint_id{};
    std::vector<Register> registers;
};
Predecessor Decode(const std::uint8_t* bytes,std::size_t count) {
    Predecessor p;
    if(bytes==nullptr||count<FrameHeaderBytes)Fail(LAPLACE_COGNITION_FIRMWARE_CHECKPOINT_INVALID);
    const auto native=laplace_cognition_discourse_frame_decode(bytes,LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES,&p.state,&p.receipt);
    if(native!=LAPLACE_COGNITION_DISCOURSE_FRAME_OK)Fail(LAPLACE_COGNITION_FIRMWARE_CHECKPOINT_INVALID,native);
    const auto* cursor=bytes+LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES;
    const auto n=Read32(cursor),version=Read32(cursor);
    if(version!=LAPLACE_COGNITION_FIRMWARE_VERSION||n==0U||count-FrameHeaderBytes!=Mul(n,RegisterBytes))
        Fail(LAPLACE_COGNITION_FIRMWARE_CHECKPOINT_INVALID);
    if((p.state.flags&LAPLACE_COGNITION_DISCOURSE_HAS_ACTIVE_ENTITIES)==0U ||
       !Same(p.state.active_entity_set_fingerprint,Hash("laplace-firmware-registers-v1",
                bytes+LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES,count-LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES)))
        Fail(LAPLACE_COGNITION_FIRMWARE_CHECKPOINT_INVALID);
    p.registers.resize(n);
    for(auto& r:p.registers) {
        r.flags=Read32(cursor);ReadId(cursor,r.answer);ReadId(cursor,r.realization);ReadId(cursor,r.act);ReadId(cursor,r.materialization);
        if((r.flags&~LAPLACE_COGNITION_FIRMWARE_STATE_HAS_REALIZATION)!=0U||Zero(r.act)||
            ((r.flags&LAPLACE_COGNITION_FIRMWARE_STATE_HAS_REALIZATION)==0U&&(!Zero(r.realization)||!Zero(r.materialization))))
            Fail(LAPLACE_COGNITION_FIRMWARE_CHECKPOINT_INVALID);
    }
    p.checkpoint_id=Hash("laplace-firmware-checkpoint-v1",bytes,count);
    return p;
}
void BindingBytes(Bytes& b,const laplace_cognition_firmware_binding& v) { U32(b,v.source);U32(b,v.step_index); }
void RealizationPolicyBytes(Bytes& b,const laplace_cognition_realization_request& r) {
    Id(b,r.modality_id);Id(b,r.language_id);Id(b,r.register_id);Id(b,r.realization_recipe_epoch);
    U32(b,r.maximum_candidates);U32(b,r.flags);U32(b,r.version);
}
bool BindingValid(const laplace_cognition_firmware_binding& b,std::uint32_t index,
                  const laplace_cognition_firmware_program& p,bool optional) {
    switch(b.source) {
        case LAPLACE_COGNITION_FIRMWARE_NO_BINDING:return optional&&b.step_index==0U;
        case LAPLACE_COGNITION_FIRMWARE_OBSERVATION:return b.step_index==0U;
        case LAPLACE_COGNITION_FIRMWARE_ANSWER:return b.step_index<index;
        case LAPLACE_COGNITION_FIRMWARE_REALIZATION:
            return b.step_index<index&&p.steps[b.step_index].kind==LAPLACE_COGNITION_FIRMWARE_EMIT;
        case LAPLACE_COGNITION_FIRMWARE_PREVIOUS_ANSWER:
        case LAPLACE_COGNITION_FIRMWARE_PREVIOUS_REALIZATION:return true;
        default:return false;
    }
}
Bytes ProgramBytes(const laplace_cognition_firmware_program& p) {
    if(p.version!=LAPLACE_COGNITION_FIRMWARE_VERSION||p.step_count==0U||p.steps==nullptr)
        Fail(LAPLACE_COGNITION_FIRMWARE_PROGRAM_INVALID);
    // This validates the complete dependency graph before any provider is called.
    Bytes b;b.reserve(Size(Add(8U,Mul(p.step_count,120U))));U32(b,p.version);U32(b,p.step_count);
    bool emitted=false;
    for(std::uint32_t i=0;i<p.step_count;++i) {
        const auto& s=p.steps[i];
        if(s.reserved!=0U||!BindingValid(s.anchor,i,p,false)||!BindingValid(s.goal,i,p,true)||
           s.kind<LAPLACE_COGNITION_FIRMWARE_INTERPRET||s.kind>LAPLACE_COGNITION_FIRMWARE_EMIT||
           (s.kind!=LAPLACE_COGNITION_FIRMWARE_EMIT && s.relation_mask==0U)||(s.relation_mask&~LAPLACE_OBSERVATION_QUERY_RELATION_MASK)!=0U)
            Fail(LAPLACE_COGNITION_FIRMWARE_PROGRAM_INVALID);
        // Interpretation always starts at the whole canonical observation. No
        // caller can replace that root with a noun/first token/expected answer.
        if(i==0U&&(s.kind!=LAPLACE_COGNITION_FIRMWARE_INTERPRET||
                   s.anchor.source!=LAPLACE_COGNITION_FIRMWARE_OBSERVATION||
                   s.goal.source!=LAPLACE_COGNITION_FIRMWARE_NO_BINDING))
            Fail(LAPLACE_COGNITION_FIRMWARE_PROGRAM_INVALID);
        const auto& r=s.realization;
        if(s.kind==LAPLACE_COGNITION_FIRMWARE_EMIT) {
            if((s.output_encoding!=LAPLACE_COGNITION_OUTPUT_UTF8&&s.output_encoding!=LAPLACE_COGNITION_OUTPUT_OCTETS)||
               r.version!=LAPLACE_COGNITION_REALIZATION_VERSION||r.maximum_candidates==0U||
               (r.flags&~LAPLACE_COGNITION_REALIZATION_KNOWN_REQUEST_FLAGS)!=0U||
               !Zero(r.evidence_epoch)||!Zero(r.context_fingerprint)||Zero(r.modality_id)||
               Zero(r.realization_recipe_epoch)||
               (((r.flags&LAPLACE_COGNITION_REALIZATION_LANGUAGE_PRESENT)!=0U)==Zero(r.language_id))||
               (((r.flags&LAPLACE_COGNITION_REALIZATION_REGISTER_PRESENT)!=0U)==Zero(r.register_id))||
               s.relation_mask!=0U||s.anchor.source!=LAPLACE_COGNITION_FIRMWARE_ANSWER||
               s.goal.source!=LAPLACE_COGNITION_FIRMWARE_NO_BINDING)
                Fail(LAPLACE_COGNITION_FIRMWARE_PROGRAM_INVALID);
            emitted=true;
        } else if(s.output_encoding!=0U||r.version!=0U||r.flags!=0U||r.maximum_candidates!=0U||
            !Zero(r.modality_id)||!Zero(r.language_id)||!Zero(r.register_id)||!Zero(r.evidence_epoch)||
            !Zero(r.realization_recipe_epoch)||!Zero(r.context_fingerprint))
                Fail(LAPLACE_COGNITION_FIRMWARE_PROGRAM_INVALID);
        U32(b,s.kind);BindingBytes(b,s.anchor);BindingBytes(b,s.goal);U32(b,s.relation_mask);
        U32(b,s.output_encoding);RealizationPolicyBytes(b,r);
    }
    if(!emitted)Fail(LAPLACE_COGNITION_FIRMWARE_PROGRAM_INVALID);
    return b;
}
laplace_digest256 Identify(const laplace_cognition_firmware_program& p) {
    return Hash("laplace-cognition-firmware-v1", ProgramBytes(p));
}
laplace_id128 Resolve(const laplace_cognition_firmware_binding& binding,
    const laplace_id128& observation,const std::vector<Register>& current,const Predecessor* previous) {
    if(binding.source==LAPLACE_COGNITION_FIRMWARE_OBSERVATION)return observation;
    bool old=binding.source==LAPLACE_COGNITION_FIRMWARE_PREVIOUS_ANSWER||binding.source==LAPLACE_COGNITION_FIRMWARE_PREVIOUS_REALIZATION;
    if(old&&previous==nullptr)Fail(LAPLACE_COGNITION_FIRMWARE_BINDING_ABSENT);
    const auto& registers=old?previous->registers:current;
    if(binding.step_index>=registers.size())Fail(LAPLACE_COGNITION_FIRMWARE_BINDING_ABSENT);
    const auto& r=registers[binding.step_index];
    if(binding.source==LAPLACE_COGNITION_FIRMWARE_REALIZATION||binding.source==LAPLACE_COGNITION_FIRMWARE_PREVIOUS_REALIZATION) {
        if((r.flags&LAPLACE_COGNITION_FIRMWARE_STATE_HAS_REALIZATION)==0U)Fail(LAPLACE_COGNITION_FIRMWARE_BINDING_ABSENT);
        return r.realization;
    }
    return r.answer;
}
struct ObservationOwner {
    laplace_cognition_observation_result* observation{};
    laplace_cognition_forward_result* forward{};
    ~ObservationOwner(){laplace_cognition_observation_result_destroy(&observation);laplace_cognition_forward_result_destroy(&forward);}
};
struct ProviderOwner {
    laplace_cognition_observation_candidate_provider_set* set{};
    ~ProviderOwner(){laplace_cognition_observation_candidate_provider_set_destroy(&set);}
};

// A goalless binding is a cardinality obligation over one fully enumerated
// frontier, not a first-row policy. The native search still owns validation,
// transitions, costs and completion. Goal-bound searches can have many paths to
// the same already-calculated goal and keep the ordinary native search law.
struct BindingProvider {
    laplace_cognition_observation_candidate_provider_v1 provider{};
    laplace_cognition_firmware_status failure{LAPLACE_COGNITION_FIRMWARE_OK};
};
int EnumerateBinding(void* opaque, const laplace_observation_query_binding* binding,
    const laplace_id128* sources, const laplace_query_search_state* frontier,
    const std::uint64_t* costs, std::size_t source_count,
    laplace_cognition_observation_candidate* candidates, std::size_t capacity,
    std::size_t* count, laplace_cognition_observation_candidate_usage* usage) {
    auto& state=*static_cast<BindingProvider*>(opaque);
    const auto& provider=state.provider;
    int status;
    try {
        status=provider.enumerate_candidates(provider.state,binding,sources,frontier,
            costs,source_count,candidates,capacity,count,usage);
    } catch (const std::bad_alloc&) {
        state.failure=LAPLACE_COGNITION_FIRMWARE_MEMORY_FAILURE;return 1;
    } catch (...) {
        state.failure=LAPLACE_COGNITION_FIRMWARE_PROVIDER_CONTRACT;return 1;
    }
    if(status!=0 || *count>capacity)return status!=0?status:1;
    if((binding->flags&LAPLACE_OBSERVATION_QUERY_BINDING_GOAL_PRESENT)==0U &&
       usage->limiting_disposition==0U && *count!=0U) {
        for(std::size_t i=1U;i<*count;++i) {
            if(!Same(candidates[0].target_entity_id,candidates[i].target_entity_id)) {
                state.failure=LAPLACE_COGNITION_FIRMWARE_AMBIGUOUS;
                // Let native validation run before classifying the valid set.
                break;
            }
        }
    }
    return 0;
}

void Deduct(std::uint64_t& remaining,std::uint64_t amount) {
    if(amount>remaining)Fail(LAPLACE_COGNITION_FIRMWARE_LIMIT);
    remaining-=amount;
}
void Consume(laplace_cognition_observation_forward_limits& limits,const laplace_cognition_forward_receipt& r) {
    Deduct(limits.max_layers,r.layer_count);Deduct(limits.max_provider_calls,r.provider_call_count);
    Deduct(limits.max_projected_queries,r.projected_query_count);Deduct(limits.max_candidate_operations,r.candidate_operation_count);
    Deduct(limits.max_resolutions,r.resolution_count);Deduct(limits.max_resource_cost,r.resource_cost);
    Deduct(limits.max_io_operations,r.io_operations);Deduct(limits.max_database_operations,r.database_operations);
}
void SearchBytes(Bytes& b,const laplace_query_search_budget& s) {
    U64(b,s.max_expanded_states);U64(b,s.max_transition_records);U64(b,s.max_emitted_states);U64(b,s.max_frontier_states);
    U64(b,s.max_memory_bytes);U64(b,s.max_io_operations);U64(b,s.max_database_operations);U64(b,s.max_provider_calls);
    U32(b,s.max_depth);U32(b,s.requested_path_count);U32(b,s.frontier_batch_width);U32(b,s.transition_batch_capacity);
}
void ForwardLimitsBytes(Bytes& b,const laplace_cognition_observation_forward_limits& s) {
    U64(b,s.max_layers);U64(b,s.max_provider_calls);U64(b,s.max_projected_queries);U64(b,s.max_candidate_operations);
    U64(b,s.max_resolutions);U64(b,s.max_resource_cost);U64(b,s.max_io_operations);U64(b,s.max_database_operations);
    U32(b,s.candidate_operation_capacity);U32(b,s.resolution_capacity);
}
void RequestBytes(Bytes& b,const laplace_cognition_observation_request& r) {
    Id(b,r.anchor_entity_id);Id(b,r.goal_entity_id);Id(b,r.world_id);Id(b,r.time_fingerprint);Id(b,r.context_fingerprint);
    Id(b,r.evidence_boundary);Id(b,r.evidence_epoch);Id(b,r.authority_id);Id(b,r.result_contract_fingerprint);
    SearchBytes(b,r.search_budget);ForwardLimitsBytes(b,r.forward_limits);
    U32(b,r.relation_mask);U32(b,r.maximum_results);U32(b,r.flags);U32(b,r.version);
}
void TraceStep(Bytes& b,const laplace_cognition_firmware_step_receipt& s) {
    U32(b,s.kind);RequestBytes(b,s.request);Id(b,s.prior_feedback);
    const auto& f=s.cognition;
    Id(b,f.receipt_id);Id(b,f.program_fingerprint);Id(b,f.initial_state_id);Id(b,f.final_state_id);
    Id(b,f.layer_trace_fingerprint);Id(b,f.output_fingerprint);U64(b,f.layer_count);U64(b,f.provider_call_count);
    U64(b,f.projected_query_count);U64(b,f.candidate_operation_count);U64(b,f.resolution_count);U64(b,f.resource_cost);
    U64(b,f.io_operations);U64(b,f.database_operations);U64(b,f.final_remaining_required_count);
    U32(b,f.final_completion);U32(b,f.disposition);U32(b,f.status);U32(b,f.version);U32(b,f.flags);
    const auto& a=s.act;
    Id(b,a.act_id);Id(b,a.request_fingerprint);Id(b,a.result_contract_fingerprint);Id(b,a.forward_receipt_id);
    Id(b,a.forward_output_fingerprint);Id(b,a.final_state_id);Id(b,a.answer_set_fingerprint);
    const auto& answer=a.primary_answer;
    Id(b,answer.entity_id);Id(b,answer.relation_id);Id(b,answer.path_id);Id(b,answer.terminal_state_id);
    U64(b,answer.total_cost);U64(b,answer.transition_count);U64(b,answer.independent_evidence_root_count);
    U32(b,answer.relation_family);U32(b,answer.source_layer);U32(b,answer.direction);U32(b,answer.rank);U32(b,answer.flags);
    U64(b,a.answer_count);U32(b,a.act_kind);U32(b,a.producer_operation_kind);U32(b,a.flags);U32(b,a.version);
    const auto& r=s.realization;
    Id(b,r.content_id);Id(b,r.language_id);Id(b,r.candidate_receipt_id);Id(b,r.realization_recipe_id);
    Id(b,r.missing_obligation_fingerprint);U64(b,r.preference_rank);U64(b,r.reused_subtree_count);U64(b,r.generated_composition_count);
    U32(b,r.structural_tier);U32(b,r.match_class);U32(b,r.missing_obligation_count);U32(b,r.disposition);U32(b,r.flags);U32(b,r.version);
    const auto& rr=s.realization_receipt;
    Id(b,rr.realization_id);Id(b,rr.semantic_act_id);Id(b,rr.provider_fingerprint);Id(b,rr.provider_receipt_id);
    Id(b,rr.request_fingerprint);Id(b,rr.result_fingerprint);U64(b,rr.candidate_count);U64(b,rr.eligible_candidate_count);
    U32(b,rr.disposition);U32(b,rr.version);
    const auto& m=s.materialization;
    Id(b,m.materialization_id);Id(b,m.source_candidate_receipt_id);Id(b,m.source_recipe_id);Id(b,m.provider_fingerprint);
    Id(b,m.readset_fingerprint);Id(b,m.output_fingerprint);Id(b,m.root_content_id);U64(b,m.resolved_node_count);
    U64(b,m.trajectory_carrier_count);U64(b,m.codepoint_count);U64(b,m.output_bytes);U32(b,m.maximum_depth_observed);U32(b,m.version);
    U64(b,s.output_offset);U64(b,s.output_bytes);
}
} // namespace firmware

struct laplace_cognition_firmware_image {
    std::vector<laplace_cognition_firmware_step> steps;
    firmware::Bytes bytes;
    laplace_digest256 identity{};
};

struct laplace_cognition_firmware_result {
    laplace_cognition_firmware_receipt receipt{};
    std::vector<laplace_cognition_firmware_step_receipt> steps;
    firmware::Bytes output,checkpoint,trace;
};

extern "C" laplace_cognition_firmware_status laplace_cognition_firmware_identify(
    const laplace_cognition_firmware_program* p,laplace_digest256* id) {
    if(id!=nullptr)*id={};
    if(p==nullptr||id==nullptr)return LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT;
    try{*id=firmware::Identify(*p);return LAPLACE_COGNITION_FIRMWARE_OK;}
    catch(const firmware::Failure& e){return e.status;}
    catch(const std::bad_alloc&){return LAPLACE_COGNITION_FIRMWARE_MEMORY_FAILURE;}
    catch(const std::length_error&){return LAPLACE_COGNITION_FIRMWARE_LIMIT;}
}

extern "C" laplace_cognition_firmware_status laplace_cognition_firmware_execute(
    const laplace_cognition_firmware_program* p,const laplace_cognition_firmware_request* r,
    const laplace_framework_context* context,laplace_cognition_prompt_admission* admission,
    const std::uint8_t* previous_bytes,std::size_t previous_count,
    const laplace_cognition_observation_candidate_provider_v1* providers,std::size_t provider_count,
    const laplace_cognition_realization_provider_v1* realization_provider,
    const laplace_cognition_materialization_provider_v1* materialization_provider,
    laplace_framework_cancel_requested_fn cancelled,void* cancel_state,
    laplace_cognition_firmware_result** result,laplace_cognition_firmware_error* error) {
    return laplace_cognition_firmware_execute_with_provider_workspace(
        p,r,context,admission,previous_bytes,previous_count,providers,provider_count,
        realization_provider,materialization_provider,cancelled,cancel_state,
        0U,result,error);
}

extern "C" laplace_cognition_firmware_status laplace_cognition_firmware_execute_with_provider_workspace(
    const laplace_cognition_firmware_program* p,const laplace_cognition_firmware_request* r,
    const laplace_framework_context* context,laplace_cognition_prompt_admission* admission,
    const std::uint8_t* previous_bytes,std::size_t previous_count,
    const laplace_cognition_observation_candidate_provider_v1* providers,std::size_t provider_count,
    const laplace_cognition_realization_provider_v1* realization_provider,
    const laplace_cognition_materialization_provider_v1* materialization_provider,
    laplace_framework_cancel_requested_fn cancelled,void* cancel_state,
    std::uint64_t provider_workspace_bytes,
    laplace_cognition_firmware_result** result,laplace_cognition_firmware_error* error) {
    using namespace firmware;
    if(result!=nullptr)*result=nullptr;
    if(error!=nullptr)*error={0U,NoStep,0U,0U};
    std::uint32_t step_index=NoStep;
    auto fail=[&](laplace_cognition_firmware_status s,std::uint32_t n=0U,std::uint32_t d=0U){
        if(error!=nullptr) { *error={static_cast<std::uint32_t>(s),step_index,n,d}; }
        return s;
    };
    if(p==nullptr||r==nullptr||context==nullptr||admission==nullptr||result==nullptr||
        realization_provider==nullptr||materialization_provider==nullptr||
        (provider_count!=0U&&providers==nullptr)||((previous_bytes==nullptr)!=(previous_count==0U)))
        return fail(LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT);
    try {
        if(cancelled!=nullptr&&cancelled(cancel_state)!=0)Fail(LAPLACE_COGNITION_FIRMWARE_CANCELLED);
        laplace_digest256 context_id{};
        if(laplace_framework_context_fingerprint(context,&context_id)!=LAPLACE_FRAMEWORK_OK)
            Fail(LAPLACE_COGNITION_FIRMWARE_CONTEXT_INVALID);
        if(r->version!=LAPLACE_COGNITION_FIRMWARE_VERSION||r->reserved!=0U||
           (r->boundary_flags&~LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE)!=0U||
           r->maximum_output_bytes==0U||r->maximum_checkpoint_bytes<FrameHeaderBytes||
           r->search_budget.requested_path_count!=1U||r->previous_checkpoint_present>1U||
           ((r->previous_checkpoint_present!=0U)!=(previous_count!=0U))||
           (r->previous_checkpoint_present==0U&&!Zero(r->expected_previous_checkpoint))||
           r->materialization.version!=LAPLACE_COGNITION_MATERIALIZATION_VERSION||
           r->materialization.maximum_nodes==0U||r->materialization.maximum_output_bytes==0U||
           previous_count>r->maximum_checkpoint_bytes||provider_count>SIZE_MAX/sizeof(*providers)-1U)
            Fail(LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT);
        // All retained storage and per-step scratch are reserved before program
        // traversal or provider access. Search/provider memory is not reclassified
        // as available output memory. Sequential steps reuse their working grant.
        std::uint64_t working=Mul(p->step_count,sizeof(laplace_cognition_firmware_step_receipt)+RegisterBytes+4096U);
        working=Add(working,Mul(r->maximum_output_bytes,2U));
        working=Add(working,Mul(r->maximum_checkpoint_bytes,2U));
        working=Add(working,Mul(provider_count+1U,sizeof(*providers)*2U));
        working=Add(working,r->search_budget.max_memory_bytes);
        working=Add(working,provider_workspace_bytes);
        working=Add(working,r->materialization.maximum_output_bytes);
        if(working>context->resource_grant.memory_bytes)Fail(LAPLACE_COGNITION_FIRMWARE_LIMIT);
        if(Add(FrameHeaderBytes,Mul(p->step_count,RegisterBytes))>r->maximum_checkpoint_bytes)
            Fail(LAPLACE_COGNITION_FIRMWARE_LIMIT);
        const auto program_id=Identify(*p);
        std::uint64_t realization_capacity=0U;
        for(std::uint32_t i=0;i<p->step_count;++i) {
            if(p->steps[i].kind==LAPLACE_COGNITION_FIRMWARE_EMIT)
                realization_capacity=std::max(realization_capacity,std::min<std::uint64_t>(
                    p->steps[i].realization.maximum_candidates,realization_provider->maximum_candidate_records));
        }
        working=Add(working,Mul(realization_capacity,sizeof(laplace_cognition_realization_candidate)));
        working=Add(working,Mul(r->materialization.maximum_trajectory_carriers,
            sizeof(laplace_trajectory_carrier)+sizeof(laplace_composition_occurrence)+sizeof(laplace_id_run)));
        working=Add(working,Mul(r->materialization.maximum_nodes,2U*sizeof(laplace_id128)));
        if(working>context->resource_grant.memory_bytes)Fail(LAPLACE_COGNITION_FIRMWARE_LIMIT);
        if(!Same(program_id,r->selected_program))Fail(LAPLACE_COGNITION_FIRMWARE_PROGRAM_MISMATCH);
        if((context->epoch_mask&(UINT64_C(1)<<LAPLACE_FRAMEWORK_EPOCH_FIRMWARE))==0U||
           !Same(context->epochs[LAPLACE_FRAMEWORK_EPOCH_FIRMWARE],program_id)||
           (context->epoch_mask&(UINT64_C(1)<<LAPLACE_FRAMEWORK_EPOCH_EVIDENCE))==0U)
            Fail(LAPLACE_COGNITION_FIRMWARE_CONTEXT_INVALID);
        laplace_cognition_prompt_admission_view input{};
        const auto admission_status=laplace_cognition_prompt_admission_view_get(admission,&input);
        if(admission_status!=LAPLACE_COGNITION_PROMPT_ADMISSION_OK||input.semantic_attestation_count!=0U)
            Fail(LAPLACE_COGNITION_FIRMWARE_ADMISSION_FAILURE,admission_status);
        if(Add(working,input.composition_summary.estimated_peak_working_bytes)>context->resource_grant.memory_bytes)
            Fail(LAPLACE_COGNITION_FIRMWARE_LIMIT);
        if(!Same(input.turn.context_fingerprint,context_id)||!Same(input.trunk_entity_id,input.turn.observation_entity_id))
            Fail(LAPLACE_COGNITION_FIRMWARE_CONTEXT_INVALID);
        Predecessor predecessor;
        if(previous_count!=0U) {
            if(!Same(r->expected_previous_checkpoint,Hash("laplace-firmware-checkpoint-v1",previous_bytes,previous_count)))
                Fail(LAPLACE_COGNITION_FIRMWARE_CHECKPOINT_INVALID);
            predecessor=Decode(previous_bytes,previous_count);
        }
        const Predecessor* prior=previous_count==0U?nullptr:&predecessor;
        // Validate predecessor bindings before the first native query, including
        // unavailable cross-turn realization slots. No partial work is exposed.
        for(std::uint32_t i=0;i<p->step_count;++i) {
            for(const auto& binding:{p->steps[i].anchor,p->steps[i].goal})
                if(binding.source==LAPLACE_COGNITION_FIRMWARE_PREVIOUS_ANSWER||
                   binding.source==LAPLACE_COGNITION_FIRMWARE_PREVIOUS_REALIZATION)
                    (void)Resolve(binding,input.trunk_entity_id,{},prior);
        }
        laplace_cognition_turn_policy policy{};
        policy.evidence_boundary=r->evidence_boundary;policy.evidence_epoch=context->epochs[LAPLACE_FRAMEWORK_EPOCH_EVIDENCE];
        policy.authority_id=context->authority_fingerprint;policy.result_contract_fingerprint=r->result_contract_fingerprint;
        policy.search_budget=r->search_budget;policy.forward_limits=r->forward_limits;
        policy.relation_mask=p->steps[0].relation_mask;policy.maximum_results=1U;
        policy.request_flags=r->boundary_flags|LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
        policy.version=LAPLACE_COGNITION_TURN_POLICY_VERSION;
        laplace_cognition_observation_request base{};laplace_cognition_turn_receipt turn_receipt{};
        const auto turn_status=laplace_cognition_turn_compile(&input.turn,&policy,
            previous_bytes,previous_count==0U?0U:static_cast<std::size_t>(LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES),&base,&turn_receipt);
        if(turn_status!=LAPLACE_COGNITION_TURN_OK)Fail(LAPLACE_COGNITION_FIRMWARE_CHECKPOINT_INVALID,turn_status);
        laplace_cognition_observation_candidate_provider_v1 structural{};
        const auto structural_status=laplace_cognition_prompt_admission_structural_provider(admission,&structural);
        if(structural_status!=LAPLACE_COGNITION_PROMPT_ADMISSION_OK)
            Fail(LAPLACE_COGNITION_FIRMWARE_ADMISSION_FAILURE,structural_status);
        std::vector<laplace_cognition_observation_candidate_provider_v1> all;
        all.reserve(provider_count+1U);all.push_back(structural);
        for(std::size_t i=0;i<provider_count;++i)all.push_back(providers[i]);
        ProviderOwner provider_owner;laplace_cognition_observation_candidate_provider_v1 combined{};
        const auto provider_status=laplace_cognition_observation_candidate_provider_set_create(
            all.data(),all.size(),&provider_owner.set,&combined);
        if(provider_status!=LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OK)
            Fail(LAPLACE_COGNITION_FIRMWARE_COGNITION_FAILURE,provider_status);
        auto owned=std::make_unique<laplace_cognition_firmware_result>();
        auto& output=*owned;output.steps.reserve(p->step_count);output.output.reserve(Size(r->maximum_output_bytes));
        output.trace.reserve(Size(Mul(p->step_count,4096U)));
        std::vector<Register> registers;registers.reserve(p->step_count);
        auto remaining=r->forward_limits;
        auto& receipt=output.receipt;receipt.version=LAPLACE_COGNITION_FIRMWARE_VERSION;
        receipt.program_id=program_id;receipt.prompt_admission_receipt_id=input.admission_receipt_id;
        receipt.original_observation_fingerprint=input.exact_bytes_fingerprint;receipt.context_fingerprint=context_id;
        if(prior!=nullptr)receipt.previous_checkpoint_fingerprint=prior->checkpoint_id;
        Id(output.trace,program_id);Id(output.trace,input.admission_receipt_id);Id(output.trace,context_id);
        Id(output.trace,receipt.previous_checkpoint_fingerprint);U32(output.trace,p->step_count);
        auto feedback=Hash("laplace-firmware-feedback-initial-v1",output.trace);
        for(step_index=0;step_index<p->step_count;++step_index) {
            if(cancelled!=nullptr&&cancelled(cancel_state)!=0)Fail(LAPLACE_COGNITION_FIRMWARE_CANCELLED);
            const auto& step=p->steps[step_index];
            laplace_cognition_firmware_step_receipt sr{};sr.kind=step.kind;sr.prior_feedback=feedback;
            if(step.kind!=LAPLACE_COGNITION_FIRMWARE_EMIT) {
                sr.request=base;sr.request.forward_limits=remaining;
                sr.request.relation_mask=step.relation_mask;
                sr.request.anchor_entity_id=Resolve(step.anchor,input.trunk_entity_id,registers,prior);
                if(step.goal.source!=LAPLACE_COGNITION_FIRMWARE_NO_BINDING) {
                    sr.request.goal_entity_id=Resolve(step.goal,input.trunk_entity_id,registers,prior);
                    sr.request.flags &= ~LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
                    sr.request.flags |= LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT;
                }
                Bytes step_context;Id(step_context,base.context_fingerprint);Id(step_context,program_id);
                Id(step_context,feedback);U32(step_context,step_index);
                sr.request.context_fingerprint=Hash("laplace-firmware-step-context-v1",step_context);
                sr.request.search_budget.max_io_operations=std::min(sr.request.search_budget.max_io_operations,remaining.max_io_operations);
                sr.request.search_budget.max_database_operations=std::min(sr.request.search_budget.max_database_operations,remaining.max_database_operations);
                if(remaining.max_layers==0U||remaining.max_provider_calls<2U||remaining.max_resolutions==0U||
                   remaining.max_resource_cost==0U||remaining.max_candidate_operations==0U||
                   remaining.max_io_operations==0U||remaining.max_database_operations==0U)
                    Fail(LAPLACE_COGNITION_FIRMWARE_LIMIT);
                sr.request.forward_limits.candidate_operation_capacity=static_cast<std::uint32_t>(std::min<std::uint64_t>(
                    remaining.candidate_operation_capacity,remaining.max_candidate_operations));
                sr.request.forward_limits.resolution_capacity=static_cast<std::uint32_t>(std::min<std::uint64_t>(
                    remaining.resolution_capacity,remaining.max_resolutions));
                BindingProvider binding_provider{combined,LAPLACE_COGNITION_FIRMWARE_OK};
                auto unique=combined;unique.state=&binding_provider;unique.enumerate_candidates=EnumerateBinding;
                Bytes scope;Id(scope,combined.provider_fingerprint);Id(scope,program_id);U32(scope,step_index);
                unique.provider_fingerprint=Hash("laplace-firmware-binding-provider-v1",scope);
                ObservationOwner native;
                const auto cognition=laplace_cognition_observation_request_execute_with_candidate_provider(
                    &sr.request,&unique,&native.observation,&native.forward,&sr.cognition);
                if(binding_provider.failure==LAPLACE_COGNITION_FIRMWARE_MEMORY_FAILURE||
                   binding_provider.failure==LAPLACE_COGNITION_FIRMWARE_PROVIDER_CONTRACT)Fail(binding_provider.failure);
                if(cognition!=LAPLACE_COGNITION_OBSERVATION_REQUEST_OK)
                    Fail(LAPLACE_COGNITION_FIRMWARE_COGNITION_FAILURE,cognition,sr.cognition.disposition);
                if(binding_provider.failure!=LAPLACE_COGNITION_FIRMWARE_OK)Fail(binding_provider.failure);
                Consume(remaining,sr.cognition);
                const auto act_status=laplace_cognition_observation_semantic_act_select(
                    &sr.request,native.observation,native.forward,&sr.cognition,&sr.act);
                if(act_status!=LAPLACE_COGNITION_SEMANTIC_ACT_OK)
                    Fail(LAPLACE_COGNITION_FIRMWARE_INCOMPLETE,act_status,sr.cognition.disposition);
                if(sr.act.answer_count!=1U)Fail(LAPLACE_COGNITION_FIRMWARE_AMBIGUOUS);
            } else {
                // Materialization consumes a witnessed native act, never a
                // caller-filled answer and never an invented identity self-edge.
                const auto& computed=output.steps[step.anchor.step_index];
                sr.request=computed.request;
                sr.act=computed.act;
            }
            Register reg;reg.answer=sr.act.primary_answer.entity_id;reg.act=sr.act.act_id;
            sr.output_offset=static_cast<std::uint64_t>(output.output.size());
            if(step.kind==LAPLACE_COGNITION_FIRMWARE_EMIT) {
                auto realization_request=step.realization;
                realization_request.evidence_epoch=policy.evidence_epoch;
                Bytes realization_context;Id(realization_context,sr.request.context_fingerprint);
                Id(realization_context,feedback);U32(realization_context,step_index);
                realization_request.context_fingerprint=Hash("laplace-firmware-realization-context-v1",realization_context);
                const auto realization_status=laplace_cognition_semantic_act_realize(&sr.act,&realization_request,
                    realization_provider,&sr.realization,&sr.realization_receipt);
                if(realization_status!=LAPLACE_COGNITION_REALIZATION_OK)
                    Fail(LAPLACE_COGNITION_FIRMWARE_REALIZATION_FAILURE,realization_status);
                const auto available=r->maximum_output_bytes-output.output.size();
                if(available==0U)Fail(LAPLACE_COGNITION_FIRMWARE_LIMIT);
                auto materialization=r->materialization;
                materialization.maximum_output_bytes=std::min(materialization.maximum_output_bytes,available);
                Bytes part(Size(materialization.maximum_output_bytes));std::size_t count=0U;
                const auto materialization_status=laplace_cognition_realization_materialize_encoded(
                    &sr.realization,&materialization,materialization_provider,step.output_encoding,
                    part.data(),part.size(),&count,&sr.materialization);
                if(materialization_status!=LAPLACE_COGNITION_MATERIALIZATION_OK)
                    Fail(LAPLACE_COGNITION_FIRMWARE_MATERIALIZATION_FAILURE,materialization_status);
                if(count>available)Fail(LAPLACE_COGNITION_FIRMWARE_LIMIT);
                Raw(output.output,part.data(),count);sr.output_bytes=count;
                reg.realization=sr.realization.content_id;reg.materialization=sr.materialization.materialization_id;
                reg.flags=LAPLACE_COGNITION_FIRMWARE_STATE_HAS_REALIZATION;++receipt.emitted_parts;
            }
            // The next context and any explicitly bound output operand come from
            // completed native results, including exact emitted bytes/encoding.
            const auto trace_start=output.trace.size();TraceStep(output.trace,sr);
            feedback=Hash("laplace-firmware-feedback-v1",output.trace.data()+trace_start,output.trace.size()-trace_start);
            sr.next_feedback=feedback;Id(output.trace,feedback);
            registers.push_back(reg);output.steps.push_back(sr);
            receipt.layer_count=Add(receipt.layer_count,sr.cognition.layer_count);
            receipt.provider_call_count=Add(receipt.provider_call_count,sr.cognition.provider_call_count);
            receipt.resource_cost=Add(receipt.resource_cost,sr.cognition.resource_cost);
            receipt.io_operations=Add(receipt.io_operations,sr.cognition.io_operations);
            receipt.database_operations=Add(receipt.database_operations,sr.cognition.database_operations);
            ++receipt.completed_steps;
        }
        receipt.trace_fingerprint=Hash("laplace-firmware-trace-v1",output.trace);
        receipt.output_fingerprint=Hash("laplace-firmware-output-v1",output.output);
        Bytes state_bytes;state_bytes.reserve(8U+Size(Mul(registers.size(),RegisterBytes)));
        EncodeRegisters(registers,state_bytes);
        laplace_cognition_discourse_input discourse{};
        discourse.discourse_id=input.turn.discourse_id;discourse.previous_state_id=turn_receipt.previous_state_id;
        discourse.observation_entity_id=input.trunk_entity_id;discourse.observation_occurrence_id=input.turn.observation_occurrence_id;
        discourse.active_entity_set_fingerprint=Hash("laplace-firmware-registers-v1",state_bytes);
        discourse.prior_program_set_fingerprint=program_id;discourse.receipt_set_fingerprint=receipt.trace_fingerprint;
        discourse.cross_modal_artifact_set_fingerprint=receipt.output_fingerprint;
        discourse.world_id=input.turn.world_id;discourse.time_fingerprint=input.turn.time_fingerprint;
        discourse.context_fingerprint=feedback;discourse.turn_ordinal=input.turn.turn_ordinal;
        discourse.flags=LAPLACE_COGNITION_DISCOURSE_HAS_ACTIVE_ENTITIES|LAPLACE_COGNITION_DISCOURSE_HAS_PRIOR_PROGRAMS|
            LAPLACE_COGNITION_DISCOURSE_HAS_RECEIPTS|LAPLACE_COGNITION_DISCOURSE_HAS_CROSS_MODAL_ARTIFACTS;
        if(prior!=nullptr)discourse.flags|=LAPLACE_COGNITION_DISCOURSE_HAS_PREVIOUS_STATE;
        discourse.version=LAPLACE_COGNITION_DISCOURSE_VERSION;
        laplace_cognition_discourse_state state{};
        const auto state_status=laplace_cognition_discourse_state_create(&discourse,&output.steps.back().act,&state);
        if(state_status!=LAPLACE_COGNITION_DISCOURSE_OK)Fail(LAPLACE_COGNITION_FIRMWARE_CHECKPOINT_INVALID,state_status);
        const auto checkpoint_size=Add(LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES,state_bytes.size());
        if(checkpoint_size>r->maximum_checkpoint_bytes)Fail(LAPLACE_COGNITION_FIRMWARE_LIMIT);
        output.checkpoint.resize(Size(checkpoint_size));std::size_t written=0;
        laplace_cognition_discourse_frame_receipt frame{};
        const auto frame_status=laplace_cognition_discourse_frame_encode(&state,output.checkpoint.data(),
            LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES,&written,&frame);
        if(frame_status!=LAPLACE_COGNITION_DISCOURSE_FRAME_OK||written!=LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES)
            Fail(LAPLACE_COGNITION_FIRMWARE_CHECKPOINT_INVALID,frame_status);
        std::copy(state_bytes.begin(),state_bytes.end(),output.checkpoint.begin()+LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES);
        receipt.next_checkpoint_fingerprint=Hash("laplace-firmware-checkpoint-v1",output.checkpoint);
        receipt.output_bytes=output.output.size();receipt.checkpoint_bytes=output.checkpoint.size();
        Bytes rb;Id(rb,receipt.program_id);Id(rb,receipt.prompt_admission_receipt_id);Id(rb,receipt.original_observation_fingerprint);
        Id(rb,receipt.context_fingerprint);Id(rb,receipt.previous_checkpoint_fingerprint);Id(rb,receipt.next_checkpoint_fingerprint);
        Id(rb,receipt.trace_fingerprint);Id(rb,receipt.output_fingerprint);U64(rb,receipt.output_bytes);U64(rb,receipt.checkpoint_bytes);
        U64(rb,receipt.layer_count);U64(rb,receipt.provider_call_count);U64(rb,receipt.resource_cost);U64(rb,receipt.io_operations);
        U64(rb,receipt.database_operations);U32(rb,receipt.completed_steps);U32(rb,receipt.emitted_parts);U32(rb,receipt.version);
        receipt.receipt_id=Hash("laplace-firmware-receipt-v1",rb);
        if(cancelled!=nullptr&&cancelled(cancel_state)!=0)Fail(LAPLACE_COGNITION_FIRMWARE_CANCELLED);
        *result=owned.release();return LAPLACE_COGNITION_FIRMWARE_OK;
    }catch(const Failure& e){return fail(e.status,e.native,e.disposition);}
    catch(const std::bad_alloc&){return fail(LAPLACE_COGNITION_FIRMWARE_MEMORY_FAILURE);}
    catch(const std::length_error&){return fail(LAPLACE_COGNITION_FIRMWARE_LIMIT);}
    catch(...){return fail(LAPLACE_COGNITION_FIRMWARE_PROVIDER_CONTRACT);}
}
extern "C" void laplace_cognition_firmware_result_destroy(laplace_cognition_firmware_result** r) {
    if(r!=nullptr){delete *r;*r=nullptr;}
}
extern "C" laplace_cognition_firmware_status laplace_cognition_firmware_result_receipt(
    const laplace_cognition_firmware_result* r,laplace_cognition_firmware_receipt* out) {
    if(r==nullptr||out==nullptr)return LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT;
    *out=r->receipt;return LAPLACE_COGNITION_FIRMWARE_OK;
}
extern "C" laplace_cognition_firmware_status laplace_cognition_firmware_result_step(
    const laplace_cognition_firmware_result* r,std::size_t i,laplace_cognition_firmware_step_receipt* out) {
    if(r==nullptr||out==nullptr||i>=r->steps.size())return LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT;
    *out=r->steps[i];return LAPLACE_COGNITION_FIRMWARE_OK;
}
extern "C" laplace_cognition_firmware_status laplace_cognition_firmware_result_output(
    const laplace_cognition_firmware_result* r,const std::uint8_t** bytes,std::size_t* count) {
    if(bytes!=nullptr) { *bytes=nullptr; }
    if(count!=nullptr) { *count=0U; }
    if(r==nullptr||bytes==nullptr||count==nullptr)return LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT;
    *bytes=r->output.data();*count=r->output.size();return LAPLACE_COGNITION_FIRMWARE_OK;
}
extern "C" laplace_cognition_firmware_status laplace_cognition_firmware_result_checkpoint(
    const laplace_cognition_firmware_result* r,const std::uint8_t** bytes,std::size_t* count) {
    if(bytes!=nullptr) { *bytes=nullptr; }
    if(count!=nullptr) { *count=0U; }
    if(r==nullptr||bytes==nullptr||count==nullptr)return LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT;
    *bytes=r->checkpoint.data();*count=r->checkpoint.size();return LAPLACE_COGNITION_FIRMWARE_OK;
}
extern "C" laplace_cognition_firmware_status laplace_cognition_firmware_publish(
    const laplace_cognition_firmware_result* r,const laplace_framework_context* context,
    std::uint32_t record_type,laplace_framework_sink_v1* sinks,std::size_t sink_count,
    laplace_framework_stream_receipt* receipt) {
    if(receipt!=nullptr)*receipt={};
    if(r==nullptr||context==nullptr||sinks==nullptr||sink_count==0U||receipt==nullptr||record_type==0U)
        return LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT;
    laplace_digest256 id{};
    if(laplace_framework_context_fingerprint(context,&id)!=LAPLACE_FRAMEWORK_OK||!firmware::Same(id,r->receipt.context_fingerprint))
        return LAPLACE_COGNITION_FIRMWARE_CONTEXT_INVALID;
    std::array<laplace_framework_canonical_batch,3> batches{};
    const std::array<const firmware::Bytes*,3> values{&r->checkpoint,&r->trace,&r->output};
    for(std::size_t i=0;i<values.size();++i) {
        batches[i].canonical_bytes=values[i]->data();batches[i].byte_count=values[i]->size();
        batches[i].record_count=1U;batches[i].first_ordinal=i;batches[i].record_type=record_type;
    }
    laplace_framework_canonical_stream stream{};
    stream.batches=batches.data();stream.batch_count=batches.size();
    stream.source_fingerprint=r->receipt.prompt_admission_receipt_id;stream.recipe_fingerprint=r->receipt.program_id;
    return laplace_framework_stage_canonical_stream(context,&stream,sinks,sink_count,receipt)==LAPLACE_FRAMEWORK_OK?
        LAPLACE_COGNITION_FIRMWARE_OK:LAPLACE_COGNITION_FIRMWARE_PUBLICATION_FAILURE;
}


extern "C" void laplace_cognition_firmware_image_destroy(laplace_cognition_firmware_image** image) {
    if (image != nullptr) { delete *image; *image = nullptr; }
}

extern "C" laplace_cognition_firmware_status laplace_cognition_firmware_image_create(
    const laplace_cognition_firmware_program* program, uint64_t memory_limit,
    laplace_cognition_firmware_image** image) {
    if (image != nullptr) *image = nullptr;
    if (program == nullptr || image == nullptr) return LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT;
    try {
        const auto required = firmware::Add(sizeof(laplace_cognition_firmware_image) + 8U,
            firmware::Mul(program->step_count, sizeof(laplace_cognition_firmware_step) + 120U));
        if (required > memory_limit) return LAPLACE_COGNITION_FIRMWARE_LIMIT;
        auto result = std::make_unique<laplace_cognition_firmware_image>();
        result->bytes = firmware::ProgramBytes(*program);
        result->identity = firmware::Hash("laplace-cognition-firmware-v1", result->bytes);
        result->steps.assign(program->steps, program->steps + program->step_count);
        *image = result.release();
        return LAPLACE_COGNITION_FIRMWARE_OK;
    } catch (const firmware::Failure& e) { return e.status; }
      catch (const std::bad_alloc&) { return LAPLACE_COGNITION_FIRMWARE_MEMORY_FAILURE; }
      catch (const std::length_error&) { return LAPLACE_COGNITION_FIRMWARE_LIMIT; }
}

extern "C" laplace_cognition_firmware_status laplace_cognition_firmware_image_load(
    const uint8_t* bytes, size_t byte_count, const laplace_digest256* expected_identity,
    uint64_t memory_limit, laplace_cognition_firmware_image** image) {
    if (image != nullptr) *image = nullptr;
    if (bytes == nullptr || expected_identity == nullptr || image == nullptr || byte_count < 8U)
        return LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT;
    try {
        const auto* cursor = bytes;
        const auto version = firmware::Read32(cursor);
        const auto count = firmware::Read32(cursor);
        if (version != LAPLACE_COGNITION_FIRMWARE_VERSION || count == 0U ||
            byte_count - 8U != firmware::Mul(count, 120U))
            return LAPLACE_COGNITION_FIRMWARE_PROGRAM_INVALID;
        const auto required = firmware::Add(sizeof(laplace_cognition_firmware_image),
            firmware::Add(firmware::Mul(count, sizeof(laplace_cognition_firmware_step)),byte_count));
        if (required > memory_limit) return LAPLACE_COGNITION_FIRMWARE_LIMIT;
        const auto identity = firmware::Hash("laplace-cognition-firmware-v1",bytes,byte_count);
        if (!firmware::Same(identity,*expected_identity)) return LAPLACE_COGNITION_FIRMWARE_PROGRAM_MISMATCH;
        auto result = std::make_unique<laplace_cognition_firmware_image>();
        result->steps.resize(count);
        for (auto& step : result->steps) {
            step.kind = firmware::Read32(cursor);
            step.anchor.source = firmware::Read32(cursor); step.anchor.step_index = firmware::Read32(cursor);
            step.goal.source = firmware::Read32(cursor); step.goal.step_index = firmware::Read32(cursor);
            step.relation_mask = firmware::Read32(cursor); step.output_encoding = firmware::Read32(cursor);
            auto& r = step.realization;
            firmware::ReadId(cursor,r.modality_id); firmware::ReadId(cursor,r.language_id);
            firmware::ReadId(cursor,r.register_id); firmware::ReadId(cursor,r.realization_recipe_epoch);
            r.maximum_candidates = firmware::Read32(cursor); r.flags = firmware::Read32(cursor);
            r.version = firmware::Read32(cursor);
        }
        const laplace_cognition_firmware_program program{result->steps.data(),count,version};
        result->bytes = firmware::ProgramBytes(program);
        if (result->bytes.size() != byte_count || std::memcmp(bytes,result->bytes.data(),byte_count) != 0)
            return LAPLACE_COGNITION_FIRMWARE_PROGRAM_INVALID;
        result->identity = identity;
        *image = result.release();
        return LAPLACE_COGNITION_FIRMWARE_OK;
    } catch (const firmware::Failure& e) { return e.status; }
      catch (const std::bad_alloc&) { return LAPLACE_COGNITION_FIRMWARE_MEMORY_FAILURE; }
      catch (const std::length_error&) { return LAPLACE_COGNITION_FIRMWARE_LIMIT; }
}

extern "C" laplace_cognition_firmware_status laplace_cognition_firmware_image_view(
    const laplace_cognition_firmware_image* image, laplace_cognition_firmware_program* program,
    const uint8_t** bytes, size_t* byte_count, laplace_digest256* identity) {
    if (program != nullptr) *program = {};
    if (bytes != nullptr) *bytes = nullptr;
    if (byte_count != nullptr) *byte_count = 0;
    if (identity != nullptr) *identity = {};
    if (image == nullptr || program == nullptr || bytes == nullptr || byte_count == nullptr || identity == nullptr)
        return LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT;
    *program = {image->steps.data(), static_cast<std::uint32_t>(image->steps.size()), LAPLACE_COGNITION_FIRMWARE_VERSION};
    *bytes = image->bytes.data(); *byte_count = image->bytes.size(); *identity = image->identity;
    return LAPLACE_COGNITION_FIRMWARE_OK;
}


extern "C" laplace_cognition_firmware_status laplace_cognition_firmware_run(
    const laplace_cognition_firmware_image* image,
    const laplace_cognition_firmware_request* request,
    const laplace_cognition_prompt_admission_input* observation,
    const uint8_t* previous_checkpoint, size_t previous_checkpoint_bytes,
    const laplace_cognition_firmware_runtime* runtime,
    laplace_cognition_firmware_result** result,
    laplace_cognition_firmware_run_receipt* receipt,
    laplace_cognition_firmware_error* error) {
    using namespace firmware;
    if(result!=nullptr)*result=nullptr;
    if(receipt!=nullptr)*receipt={};
    if(error!=nullptr)*error={0U,NoStep,0U,0U};
    auto failure=[&](laplace_cognition_firmware_status status,uint32_t native=0U) {
        if(error!=nullptr)*error={static_cast<uint32_t>(status),NoStep,native,0U};
        return status;
    };
    if(image==nullptr||request==nullptr||observation==nullptr||runtime==nullptr||result==nullptr||receipt==nullptr||
       observation->framework_context==nullptr||runtime->version!=LAPLACE_COGNITION_FIRMWARE_VERSION||runtime->reserved!=0U||
       runtime->atoms==nullptr||runtime->presence==nullptr||runtime->realization==nullptr||runtime->materialization==nullptr||
       (runtime->cognition_count!=0U&&runtime->cognition==nullptr)||runtime->result_record_type==0U||
       runtime->result_sink_count==0U||runtime->result_sinks==nullptr||runtime->input_sink_count==0U||runtime->input_sinks==nullptr||
       runtime->activation==nullptr)
        return failure(LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT);
    const auto* context=observation->framework_context;
    if(laplace_framework_context_validate(context)!=LAPLACE_FRAMEWORK_OK ||
       (context->epoch_mask&(UINT64_C(1)<<LAPLACE_FRAMEWORK_EPOCH_DATABASE))==0U)
        return failure(LAPLACE_COGNITION_FIRMWARE_CONTEXT_INVALID);
    if((context->flags&LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY)!=0U)
        return failure(LAPLACE_COGNITION_FIRMWARE_DENIED,LAPLACE_FRAMEWORK_EFFECT_NOT_AUTHORIZED);
    if(!Same(image->identity,request->selected_program)||
       !Same(image->identity,context->epochs[LAPLACE_FRAMEWORK_EPOCH_FIRMWARE]))
        return failure(LAPLACE_COGNITION_FIRMWARE_PROGRAM_MISMATCH);
    const auto valid_sink=[](const laplace_framework_sink_v1& sink) {
        return sink.abi_major==LAPLACE_FRAMEWORK_SINK_ABI_MAJOR&&sink.abi_minor<=LAPLACE_FRAMEWORK_SINK_ABI_MINOR&&
               sink.begin!=nullptr&&sink.stage!=nullptr&&sink.seal!=nullptr&&sink.abort!=nullptr&&sink.flags==0U&&sink.reserved==0U;
    };
    for(size_t i=0;i<runtime->input_sink_count;++i)if(!valid_sink(runtime->input_sinks[i]))
        return failure(LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT,LAPLACE_FRAMEWORK_SINK_INVALID);
    for(size_t i=0;i<runtime->result_sink_count;++i)if(!valid_sink(runtime->result_sinks[i]))
        return failure(LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT,LAPLACE_FRAMEWORK_SINK_INVALID);
    const auto* effect=runtime->activation;
    if(effect->abi_major!=LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MAJOR||
       effect->abi_minor>LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MINOR||effect->flags!=0U||effect->reserved!=0U||
       effect->prepare==nullptr||effect->commit==nullptr||effect->abort==nullptr)
        return failure(LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT,LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_INVALID);
    struct AdmissionOwner {
        laplace_cognition_prompt_admission* value{};
        ~AdmissionOwner(){laplace_cognition_prompt_admission_destroy(&value);}
    } admission;
    try {
        const auto check_cancel=[&]() {
            if(runtime->cancel_requested!=nullptr&&runtime->cancel_requested(runtime->cancel_state)!=0)
                Fail(LAPLACE_COGNITION_FIRMWARE_CANCELLED);
        };
        check_cancel();
        const auto admitted=laplace_cognition_prompt_admission_create(observation,runtime->atoms,runtime->presence,&admission.value);
        if(admitted!=LAPLACE_COGNITION_PROMPT_ADMISSION_OK)
            return failure(LAPLACE_COGNITION_FIRMWARE_ADMISSION_FAILURE,admitted);
        const laplace_cognition_firmware_program program{image->steps.data(),static_cast<uint32_t>(image->steps.size()),LAPLACE_COGNITION_FIRMWARE_VERSION};
        laplace_cognition_firmware_result* computed=nullptr;
        const auto executed=laplace_cognition_firmware_execute(&program,request,context,admission.value,
            previous_checkpoint,previous_checkpoint_bytes,runtime->cognition,runtime->cognition_count,
            runtime->realization,runtime->materialization,runtime->cancel_requested,runtime->cancel_state,&computed,error);
        std::unique_ptr<laplace_cognition_firmware_result> owned(computed);
        if(executed!=LAPLACE_COGNITION_FIRMWARE_OK)return executed;
        laplace_cognition_firmware_run_receipt staged{};
        staged.version=LAPLACE_COGNITION_FIRMWARE_VERSION;staged.execution=computed->receipt;
        laplace_framework_producer_v1 producer{};
        const auto producer_status=laplace_cognition_prompt_admission_producer(admission.value,&producer);
        if(producer_status==LAPLACE_COGNITION_PROMPT_ADMISSION_OK) {
            laplace_framework_producer_control_v1 control{};
            control.state=const_cast<laplace_cognition_firmware_runtime*>(runtime);
            control.cancel_requested=[](void* state)->int {
                const auto* host=static_cast<const laplace_cognition_firmware_runtime*>(state);
                return host->cancel_requested==nullptr?0:host->cancel_requested(host->cancel_state);
            };
            control.observe_progress=[](void*,const laplace_framework_replay_checkpoint*){};
            control.abi_major=LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MAJOR;
            const auto produced=laplace_framework_run_producer(context,&observation->source_fingerprint,
                &observation->calculation_recipe_fingerprint,&producer,&control,runtime->input_sinks,
                runtime->input_sink_count,&staged.input_staging);
            if(produced!=LAPLACE_FRAMEWORK_OK)return failure(LAPLACE_COGNITION_FIRMWARE_PUBLICATION_FAILURE,produced);
            staged.input_stream_present=1U;
        } else if(producer_status!=LAPLACE_COGNITION_PROMPT_ADMISSION_NO_PUBLICATION_REQUIRED)
            return failure(LAPLACE_COGNITION_FIRMWARE_ADMISSION_FAILURE,producer_status);
        check_cancel();
        // The final sealed stream carries the input staging dependency, not just
        // an unconnected hash of a prompt that might never have been admitted.
        Bytes envelope;U32(envelope,LAPLACE_COGNITION_FIRMWARE_VERSION);U32(envelope,staged.input_stream_present);
        Id(envelope,staged.input_staging.receipt_id);Id(envelope,computed->receipt.receipt_id);
        std::array<laplace_framework_canonical_batch,4> batches{};
        const std::array<const Bytes*,4> payloads{&envelope,&computed->checkpoint,&computed->trace,&computed->output};
        for(size_t i=0;i<payloads.size();++i) {
            batches[i].canonical_bytes=payloads[i]->data();batches[i].byte_count=payloads[i]->size();
            batches[i].record_count=1U;batches[i].first_ordinal=i;batches[i].record_type=runtime->result_record_type;
        }
        laplace_framework_canonical_stream stream{};
        stream.batches=batches.data();stream.batch_count=batches.size();
        stream.source_fingerprint=computed->receipt.prompt_admission_receipt_id;stream.recipe_fingerprint=image->identity;
        const auto sealed=laplace_framework_stage_canonical_stream(context,&stream,runtime->result_sinks,runtime->result_sink_count,&staged.result_staging);
        if(sealed!=LAPLACE_FRAMEWORK_OK)return failure(LAPLACE_COGNITION_FIRMWARE_PUBLICATION_FAILURE,sealed);
        check_cancel();
        Bytes next;Id(next,context->epochs[LAPLACE_FRAMEWORK_EPOCH_DATABASE]);Id(next,staged.result_staging.receipt_id);
        Id(next,staged.input_staging.receipt_id);Id(next,computed->receipt.next_checkpoint_fingerprint);
        laplace_framework_activation_request activation{};
        activation.epoch_slot=LAPLACE_FRAMEWORK_EPOCH_DATABASE;
        activation.expected_epoch=context->epochs[LAPLACE_FRAMEWORK_EPOCH_DATABASE];
        activation.next_epoch=Hash("laplace-firmware-database-generation-v1",next);
        Bytes complete;complete.reserve(132U);
        const auto activated=laplace_framework_activate_staged_stream(context,&staged.result_staging,&activation,effect,&staged.activation);
        if(activated!=LAPLACE_FRAMEWORK_OK)return failure(LAPLACE_COGNITION_FIRMWARE_PUBLICATION_FAILURE,activated);
        Id(complete,computed->receipt.receipt_id);Id(complete,staged.input_staging.receipt_id);
        Id(complete,staged.result_staging.receipt_id);Id(complete,staged.activation.receipt_id);U32(complete,staged.input_stream_present);
        staged.receipt_id=Hash("laplace-firmware-run-receipt-v1",complete);
        *receipt=staged;*result=owned.release();return LAPLACE_COGNITION_FIRMWARE_OK;
    } catch(const Failure& e){return failure(e.status,e.native);}
      catch(const std::bad_alloc&){return failure(LAPLACE_COGNITION_FIRMWARE_MEMORY_FAILURE);}
      catch(const std::length_error&){return failure(LAPLACE_COGNITION_FIRMWARE_LIMIT);}
      catch(...){return failure(LAPLACE_COGNITION_FIRMWARE_PROVIDER_CONTRACT);}
}
