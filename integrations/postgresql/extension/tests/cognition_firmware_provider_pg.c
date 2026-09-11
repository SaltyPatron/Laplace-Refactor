/* PostgreSQL integration harness. Only atom/realization fixtures are synthetic;
 * every cognition candidate must come from the actual shared indexed provider.
 * This file is excluded from non-testing module builds and installs no SQL API.
 */
#include "postgres.h"
#include <string.h>
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "../src/cognition_firmware_pg.h"
#include "../src/laplace_pg_internal.h"
#include "laplace/identity.h"
#include "blake3.h"

PG_FUNCTION_INFO_V1(laplace_pg_test_firmware_indexed);

typedef struct fixture_owners {
    laplace_cognition_firmware_image* image;
    laplace_cognition_prompt_admission* admission;
    laplace_cognition_firmware_result* result;
    MemoryContextCallback cleanup;
} fixture_owners;
static void release_owners(void* opaque) {
    fixture_owners* owners = opaque;
    laplace_cognition_firmware_result_destroy(&owners->result);
    laplace_cognition_prompt_admission_destroy(&owners->admission);
    laplace_cognition_firmware_image_destroy(&owners->image);
}
static laplace_digest256 fixture_digest(unsigned char value) {
    laplace_digest256 d;
    memset(&d,value,sizeof(d));
    return d;
}
static int fixture_atoms(void* state, const uint32_t* positions, size_t count,
    laplace_composition_known_entity* known, laplace_digest256* receipt) {
    size_t i;
    (void)state;
    for(i=0u;i<count;++i) {
        memset(&known[i],0,sizeof(known[i]));
        if(laplace_identity_codepoint_witness(positions[i], &known[i].entity_id,
            &known[i].identity_witness) != LAPLACE_IDENTITY_OK) return 1;
        known[i].physicality_id=fixture_digest(0x11);
        known[i].centroid.component[0]=1.0;
        known[i].atom=positions[i];known[i].has_atom=1u;
    }
    *receipt=fixture_digest(0x12);return 0;
}
static laplace_composition_status fixture_presence(void* state,
    const laplace_composition_entity_candidate* entities, size_t entity_count,
    const laplace_persistence_physicality_record* physicalities, size_t physicality_count,
    uint8_t* entity_dispositions, uint8_t* physicality_dispositions,
    laplace_composition_presence_provider_result* result) {
    (void)state;(void)entities;(void)physicalities;
    memset(entity_dispositions,LAPLACE_COMPOSITION_NOVEL,entity_count);
    if(physicality_count!=0u)memset(physicality_dispositions,LAPLACE_COMPOSITION_NOVEL,physicality_count);
    memset(result,0,sizeof(*result));
    result->provider_fingerprint=fixture_digest(0x13);
    result->provider_receipt_id=fixture_digest(0x14);
    result->returned_entity_count=entity_count;
    result->returned_physicality_count=physicality_count;
    result->entity_round_count=entity_count?1u:0u;
    result->physicality_round_count=physicality_count?1u:0u;
    return LAPLACE_COMPOSITION_OK;
}
static laplace_decomposition_status fixture_applicable(void* state,
    const laplace_decomposition_content* content,const laplace_decomposition_span* span,int* applicable) {
    (void)state;(void)content;*applicable=span->depth==0u;return LAPLACE_DECOMPOSITION_OK;
}
static laplace_decomposition_status fixture_apply(void* state,
    const laplace_decomposition_content* content,const laplace_decomposition_span* span,
    laplace_decomposition_emit_fn emit,void* emit_state) {
    (void)state;(void)content;(void)span;(void)emit;(void)emit_state;
    return LAPLACE_DECOMPOSITION_OK;
}
static int fixture_realize(void* state,const laplace_cognition_semantic_act* act,
    const laplace_cognition_realization_request* request,
    laplace_cognition_realization_candidate* candidates,size_t capacity,size_t* count,
    laplace_cognition_realization_usage* usage) {
    (void)state;*count=0u;memset(usage,0,sizeof(*usage));
    if(capacity==0u)return 1;
    memset(candidates,0,sizeof(*candidates));
    candidates[0].content_id=act->primary_answer.entity_id;
    candidates[0].candidate_receipt_id=fixture_digest(0x21);
    candidates[0].realization_recipe_id=request->realization_recipe_epoch;
    candidates[0].obligation_fingerprint=act->act_id;
    candidates[0].reused_subtree_count=1u;
    candidates[0].match_class=LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE;
    candidates[0].flags=LAPLACE_COGNITION_REALIZATION_CANDIDATE_REUSED_WITNESSED;
    usage->provider_receipt_id=fixture_digest(0x22);
    usage->rows_examined=1u;usage->exact_whole_examined=1u;
    usage->disposition=LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE;
    *count=1u;return 0;
}
static int fixture_node(void* state,const laplace_id128* id,
    laplace_cognition_materialization_node* node) {
    uint32_t atom;
    (void)state;
    /* Fixture alphabet, not a production reverse lookup. The native query's
     * result selects the member; no target/answer is supplied to the runtime. */
    for(atom=65u;atom<=67u;++atom) {
        laplace_id128 expected;laplace_digest256 witness;
        if(laplace_identity_codepoint_witness(atom,&expected,&witness)!=LAPLACE_IDENTITY_OK)return 1;
        if(memcmp(expected.bytes,id->bytes,sizeof(id->bytes))!=0)continue;
        memset(node,0,sizeof(*node));node->entity_id=expected;node->identity_witness=witness;
        node->node_receipt_id=fixture_digest(0x23);node->logical_count=1u;node->atom=atom;
        node->kind=LAPLACE_COGNITION_MATERIALIZATION_NODE_ATOM;return 0;
    }
    return 1;
}
static int fixture_trajectory(void* state,const laplace_cognition_materialization_node* node,
    laplace_trajectory_carrier* carriers,size_t count,laplace_digest256* receipt) {
    (void)state;(void)node;(void)carriers;(void)count;(void)receipt;return 1;
}
static void hex(StringInfo str,const uint8_t* bytes,size_t count) {
    size_t i;for(i=0;i<count;++i)appendStringInfo(str,"%02x",(unsigned)bytes[i]);
}
Datum laplace_pg_test_firmware_indexed(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_cognition_firmware_request request;
    laplace_cognition_firmware_program program;
    laplace_cognition_prompt_admission_input input;
    laplace_cognition_prompt_atom_provider_v1 atoms;
    laplace_composition_presence_provider_v1 presence;
    laplace_decomposition_provider_v1 grammar;
    laplace_cognition_realization_provider_v1 realization;
    laplace_cognition_materialization_provider_v1 materialization;
    laplace_cognition_firmware_error error;
    laplace_cognition_firmware_receipt receipt;
    laplace_pg_cognition_provider_report reads;
    laplace_cognition_firmware_status status;
    ErrorData* database_error=NULL;
    fixture_owners* owners=palloc0(sizeof(*owners));
    bytea* bytes=PG_GETARG_BYTEA_PP(1);
    bytea* expected=PG_GETARG_BYTEA_PP(2);
    text* prompt=PG_GETARG_TEXT_PP(3);
    bytea* previous=PG_GETARG_BYTEA_PP(4);
    bytea* previous_id=PG_GETARG_BYTEA_PP(5);
    int64 workspace=PG_GETARG_INT64(6);
    int32 transition_capacity=PG_GETARG_INT32(7);
    bool mixed_relations=transition_capacity==63;
    const uint8_t* image_bytes;size_t image_size;
    laplace_digest256 image_id;
    const uint8_t* output=NULL;size_t output_size=0;
    const uint8_t* checkpoint=NULL;size_t checkpoint_size=0;
    StringInfoData result;
    owners->cleanup.func=release_owners;owners->cleanup.arg=owners;
    MemoryContextRegisterResetCallback(CurrentMemoryContext,&owners->cleanup);
    if(VARSIZE_ANY_EXHDR(expected)!=32||workspace<0||transition_capacity<=0||
        (VARSIZE_ANY_EXHDR(previous)!=0 && VARSIZE_ANY_EXHDR(previous_id)!=32))
        ereport(ERROR,(errmsg("invalid firmware integration fixture")));
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0),&context);
    memset(&request,0,sizeof(request));memcpy(request.selected_program.bytes,VARDATA_ANY(expected),32);
    status=laplace_cognition_firmware_image_load((const uint8_t*)VARDATA_ANY(bytes),
        VARSIZE_ANY_EXHDR(bytes),&request.selected_program,65536u,&owners->image);
    if(status!=LAPLACE_COGNITION_FIRMWARE_OK)ereport(ERROR,(errmsg("invalid firmware image %d",(int)status)));
    if(laplace_cognition_firmware_image_view(owners->image,&program,&image_bytes,&image_size,&image_id)!=
        LAPLACE_COGNITION_FIRMWARE_OK)ereport(ERROR,(errmsg("unreadable firmware image")));
    if(mixed_relations) {
        laplace_cognition_firmware_step* mixed_steps;
        uint32_t step;
        mixed_steps=palloc(sizeof(*mixed_steps)*(size_t)program.step_count);
        memcpy(mixed_steps,program.steps,sizeof(*mixed_steps)*(size_t)program.step_count);
        for(step=0u;step<program.step_count;++step) {
            if(mixed_steps[step].kind!=LAPLACE_COGNITION_FIRMWARE_EMIT)
                mixed_steps[step].relation_mask|=LAPLACE_OBSERVATION_QUERY_SEMANTIC;
        }
        program.steps=mixed_steps;
        status=laplace_cognition_firmware_identify(&program,&request.selected_program);
        if(status!=LAPLACE_COGNITION_FIRMWARE_OK)
            ereport(ERROR,(errmsg("mixed firmware program invalid %d",(int)status)));
        context.epochs[LAPLACE_FRAMEWORK_EPOCH_FIRMWARE]=request.selected_program;
    }
    memset(&grammar,0,sizeof(grammar));grammar.applicable=fixture_applicable;grammar.apply=fixture_apply;
    grammar.provider_fingerprint=fixture_digest(0x31);grammar.abi_major=LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    memset(&atoms,0,sizeof(atoms));atoms.resolve=fixture_atoms;atoms.provider_fingerprint=fixture_digest(0x32);
    atoms.abi_major=LAPLACE_COGNITION_PROMPT_ATOM_PROVIDER_ABI_MAJOR;
    memset(&presence,0,sizeof(presence));presence.resolve=fixture_presence;presence.abi_major=LAPLACE_COMPOSITION_PRESENCE_PROVIDER_ABI;
    memset(&input,0,sizeof(input));input.decomposition.content.bytes=(const uint8_t*)VARDATA_ANY(prompt);
    input.decomposition.content.byte_count=VARSIZE_ANY_EXHDR(prompt);
    input.decomposition.content.media_type="text/plain";input.decomposition.content.media_type_byte_count=10u;
    input.decomposition.providers=&grammar;input.decomposition.provider_count=1u;
    input.decomposition.maximum_spans=8u;input.decomposition.maximum_depth=3u;
    input.framework_context=&context;input.source_fingerprint=fixture_digest(0x33);
    input.content_recipe_fingerprint=fixture_digest(0x34);input.calculation_recipe_fingerprint=fixture_digest(0x35);
    input.geometry_epoch=fixture_digest(0x36);input.occurrence_context_fingerprint=fixture_digest(0x37);
    input.source_ordinal_base=1u;input.preferred_batch_bytes=512u;
    input.occurrence.principal_fingerprint=fixture_digest(0x38);input.occurrence.session_fingerprint=fixture_digest(0x39);
    input.occurrence.discourse_id=fixture_digest(0x3a);input.occurrence.world_id=fixture_digest(0x3b);
    input.occurrence.time_fingerprint=fixture_digest(0x3c);
    if(laplace_framework_context_fingerprint(&context,&input.occurrence.context_fingerprint)!=LAPLACE_FRAMEWORK_OK)
        ereport(ERROR,(errmsg("invalid firmware context")));
    input.version=LAPLACE_COGNITION_PROMPT_ADMISSION_VERSION;
    if(VARSIZE_ANY_EXHDR(previous)!=0) {
        input.occurrence.turn_ordinal=1u;input.occurrence.turn_flags=LAPLACE_COGNITION_TURN_HAS_PREVIOUS_DISCOURSE;
        request.previous_checkpoint_present=1u;memcpy(request.expected_previous_checkpoint.bytes,VARDATA_ANY(previous_id),32);
    }
    request.evidence_boundary=fixture_digest(0x40);request.result_contract_fingerprint=fixture_digest(0x41);
    request.search_budget=(laplace_query_search_budget){64,256,128,64,1048576,64,64,64,8,1,8,64};
    request.search_budget.transition_batch_capacity=(uint32_t)transition_capacity;
    request.forward_limits=(laplace_cognition_observation_forward_limits){16,32,32,32,16,8192,128,128,4,4};
    request.materialization=(laplace_cognition_materialization_request){8,8,64,8,LAPLACE_COGNITION_MATERIALIZATION_VERSION};
    request.maximum_output_bytes=128u;request.maximum_checkpoint_bytes=4096u;
    request.boundary_flags=LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE;
    request.version=LAPLACE_COGNITION_FIRMWARE_VERSION;
    memset(&realization,0,sizeof(realization));realization.enumerate=fixture_realize;
    realization.provider_fingerprint=fixture_digest(0x42);realization.maximum_candidate_records=4u;
    realization.abi_major=LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MAJOR;
    memset(&materialization,0,sizeof(materialization));materialization.resolve_node=fixture_node;
    materialization.read_trajectory=fixture_trajectory;materialization.provider_fingerprint=fixture_digest(0x43);
    materialization.abi_major=LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MAJOR;
    if(laplace_cognition_prompt_admission_create(&input,&atoms,&presence,&owners->admission)!=LAPLACE_COGNITION_PROMPT_ADMISSION_OK)
        ereport(ERROR,(errmsg("fixture observation admission failed")));
    status=laplace_pg_cognition_firmware_execute_indexed(&program,&request,&context,owners->admission,
        VARSIZE_ANY_EXHDR(previous)?(const uint8_t*)VARDATA_ANY(previous):NULL,VARSIZE_ANY_EXHDR(previous),
        &realization,&materialization,(uint64_t)workspace,NULL,NULL,&owners->result,&error,&reads,&database_error);
    if(database_error!=NULL) {release_owners(owners);ReThrowError(database_error);}
    memset(&receipt,0,sizeof(receipt));
    if(status==LAPLACE_COGNITION_FIRMWARE_OK) {
        if(laplace_cognition_firmware_result_receipt(owners->result,&receipt)!=LAPLACE_COGNITION_FIRMWARE_OK||
           laplace_cognition_firmware_result_output(owners->result,&output,&output_size)!=LAPLACE_COGNITION_FIRMWARE_OK||
           laplace_cognition_firmware_result_checkpoint(owners->result,&checkpoint,&checkpoint_size)!=LAPLACE_COGNITION_FIRMWARE_OK)
            ereport(ERROR,(errmsg("firmware result unreadable")));
    }
    initStringInfo(&result);
    appendStringInfo(&result,"{\"status\":%u,\"native_status\":%u,\"step\":%u,\"completed_steps\":%u,\"database_operations\":%llu,\"rows_fetched\":%llu,\"batch_count\":%llu,\"semantic_provider_calls\":%llu,\"semantic_rows_examined\":%llu,\"output_hex\":\"",
        (unsigned)status,error.native_status,error.step_index,receipt.completed_steps,
        (unsigned long long)reads.database_operations,(unsigned long long)reads.rows_fetched,
        (unsigned long long)reads.batch_count,(unsigned long long)reads.semantic_provider_calls,
        (unsigned long long)reads.semantic_rows_examined);
    hex(&result,output,output_size);appendStringInfoString(&result,"\",\"checkpoint_hex\":\"");
    hex(&result,checkpoint,checkpoint_size);appendStringInfoString(&result,"\",\"checkpoint_id\":\"");
    hex(&result,receipt.next_checkpoint_fingerprint.bytes,32);appendStringInfoString(&result,"\",\"receipt_id\":\"");
    hex(&result,receipt.receipt_id.bytes,32);appendStringInfoString(&result,"\",\"readset\":\"");
    hex(&result,reads.readset_fingerprint.bytes,32);appendStringInfoString(&result,"\"}");
    release_owners(owners);
    PG_RETURN_TEXT_P(cstring_to_text(result.data));
}
