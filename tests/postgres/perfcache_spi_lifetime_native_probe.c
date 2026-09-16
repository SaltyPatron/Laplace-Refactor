/* Actual perfcache C connection, manifest and registered-pin cleanup owners.
 * PG/catalog/native-provider shims isolate lifecycle; this is not SQL execution,
 * real manifest validation, or a PostgreSQL durable-readback claim. */
#ifndef PERFCACHE_ADAPTER_SOURCE
#define PERFCACHE_ADAPTER_SOURCE "../../integrations/postgresql/extension/src/perfcache_pg.c"
#endif
#include PERFCACHE_ADAPTER_SOURCE
#undef printf
#undef fprintf
#undef vfprintf
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static unsigned checks;
#define CHECK(value) do { ++checks; if (!(value)) { \
    fprintf(stderr,"check %u line %d: %s\n",checks,__LINE__,#value); exit(1); \
} } while (0)
MemoryContext CurrentMemoryContext;
sigjmp_buf* PG_exception_stack;
ErrorContextCallback* error_context_stack;
uint64 SPI_processed;
SPITupleTable* SPI_tuptable;

static MemoryContextData caller, scratch, procedures[4];
static MemoryContext previous[4];
static unsigned depth;
static unsigned failure;
static unsigned prepare_calls, keepplan_calls, execute_calls;
static int last_error_code;
static uint64_t budget_used;
static SPITupleTable table;
static HeapTuple rows[1];
static struct { MemoryContext owner; void* bytes; size_t size; } allocations[1024];
static size_t allocation_count;

void* palloc(Size bytes) {
    CHECK(allocation_count < lengthof(allocations));
    void* value=malloc(bytes == 0u ? 1u : bytes);
    CHECK(value != NULL);
    allocations[allocation_count].owner=CurrentMemoryContext;
    allocations[allocation_count].bytes=value;
    allocations[allocation_count++].size=bytes;
    return value;
}
void pfree(void* value) {
    for(size_t i=0;i<allocation_count;++i) if(allocations[i].bytes==value) {
        free(value); allocations[i].bytes=NULL; allocations[i].owner=NULL; return;
    }
    CHECK(false);
}
struct varlena* pg_detoast_datum_packed(struct varlena* value) { return value; }
bytea* laplace_pg_bytes_to_bytea(const uint8_t* bytes,size_t count) {
    bytea* value=palloc(count+VARHDRSZ); SET_VARSIZE(value,count+VARHDRSZ);
    memcpy(VARDATA(value),bytes,count); return value;
}
int SPI_connect(void) {
    CHECK(depth < lengthof(procedures)); previous[depth]=CurrentMemoryContext;
    CurrentMemoryContext=&procedures[depth++]; return SPI_OK_CONNECT;
}
int SPI_finish(void) {
    CHECK(depth > 0u); MemoryContext retired=&procedures[depth-1u];
    for(size_t i=0;i<allocation_count;++i) if(allocations[i].owner==retired) {
        memset(allocations[i].bytes,0xdd,allocations[i].size);
        allocations[i].owner=NULL;
    }
    CurrentMemoryContext=previous[--depth]; return SPI_OK_FINISH;
}
void pg_re_throw(void) { CHECK(PG_exception_stack != NULL); siglongjmp(*PG_exception_stack,1); }
bool errstart(int level,const char* domain) { (void)level;(void)domain;return true; }
bool errstart_cold(int level,const char* domain) { return errstart(level,domain); }
int errcode(int code) { last_error_code=code;return code; }
int errmsg(const char* format,...) { (void)format;return 0; }
int errdetail(const char* format,...) { (void)format;return 0; }
void errfinish(const char* file,int line,const char* function) {
    (void)file;(void)line;(void)function;pg_re_throw();
}

int MaxBackends=1;
ProcNumber MyProcNumber=0;
int MyProcPid=123;
static unsigned manifest_closed, prepared_discarded, pin_calls, forgotten;
static unsigned registry_token, manifest_token, prepared_token, resource_token;
static SPIPlanPtr current_plan;
static laplace_perfcache_generation_request request;
static laplace_perfcache_generation_artifact artifact;
static laplace_framework_context framework_context;
static laplace_framework_stream_receipt stream_receipt;

void* palloc0(Size bytes) { void* p=palloc(bytes);memset(p,0,bytes);return p; }
Size mul_size(Size left,Size right) { CHECK(right==0 || left<=SIZE_MAX/right);return left*right; }
const char* GetConfigOption(const char* name,bool missing,bool restricted) {
    (void)name;(void)missing;(void)restricted;CHECK(false);return NULL;
}
SPIPlanPtr SPI_prepare(const char* sql,int count,Oid* types) {
    (void)sql;(void)types;CHECK(depth>0);++prepare_calls;
    if(failure==8) pg_re_throw();
    CHECK(count==5 || count==10 || count==0 || count==3);
    return (SPIPlanPtr)(uintptr_t)(count==5?1:count==10?2:count==0?3:4);
}
int SPI_keepplan(SPIPlanPtr plan) { CHECK(plan!=NULL);++keepplan_calls;return 0; }
int SPI_execute_plan(SPIPlanPtr plan,Datum* values,const char* nulls,bool readonly,long count) {
    (void)values;(void)nulls;(void)readonly;CHECK(count==1);CHECK(depth==2);
    ++execute_calls;current_plan=plan;CurrentMemoryContext=&procedures[depth-1];
    if(failure==2) pg_re_throw();
    SPI_processed=failure==1?0:1;table.vals=rows;SPI_tuptable=&table;
    return SPI_OK_SELECT;
}
Datum SPI_getbinval(HeapTuple tuple,TupleDesc desc,int column,bool* is_null) {
    (void)tuple;(void)desc;*is_null=false;
    if(current_plan==perfcache_active_select_plan) {
        if(column==1) return Int64GetDatum(17);
        if(column==2) return BoolGetDatum(true);
        CHECK(column>=3 && column<=6);
    } else CHECK(column==1 || column==2);
    size_t size=(current_plan==perfcache_active_select_plan && column==3)?16:32;
    if(failure==3) --size;
    uint8_t bytes[32];memset(bytes,0,sizeof(bytes));
    return PointerGetDatum(laplace_pg_bytes_to_bytea(bytes,size));
}
bool LWLockAcquire(LWLock* lock,LWLockMode mode) {
    CHECK(lock==&perfcache_shared->lock);CHECK(mode==LW_EXCLUSIVE);return true;
}
void LWLockRelease(LWLock* lock) { CHECK(lock==&perfcache_shared->lock); }
void ResourceOwnerForget(ResourceOwner owner,Datum value,const ResourceOwnerDesc* desc) {
    CHECK(owner==(ResourceOwner)&resource_token);CHECK(desc==&perfcache_pin_resource);
    CHECK(((laplace_pg_perfcache_pin*)DatumGetPointer(value))->held==0);++forgotten;
}
laplace_perfcache_registry_status laplace_perfcache_registry_pin_epoch(
    laplace_perfcache_registry* registry,const laplace_perfcache_epoch* epoch,laplace_perfcache_pin* pin) {
    (void)epoch;CHECK(registry==(laplace_perfcache_registry*)&registry_token);
    if(++pin_calls==1) return LAPLACE_PERFCACHE_REGISTRY_EPOCH_MISMATCH;
    pin->registry=registry;return LAPLACE_PERFCACHE_REGISTRY_OK;
}
laplace_perfcache_registry_status laplace_perfcache_pin_release(laplace_perfcache_pin* pin) {
    CHECK(pin->registry==perfcache_native_registry);pin->registry=NULL;return LAPLACE_PERFCACHE_REGISTRY_OK;
}
laplace_perfcache_registry_status laplace_perfcache_generation_manifest_open(
    const uint8_t* bytes,size_t count,laplace_perfcache_generation_manifest** manifest,laplace_digest256* digest) {
    (void)bytes;CHECK(count==32);*manifest=(laplace_perfcache_generation_manifest*)&manifest_token;
    memset(digest,0,sizeof(*digest));if(failure==7) digest->bytes[0]=1;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}
laplace_perfcache_registry_status laplace_perfcache_generation_manifest_view(
    const laplace_perfcache_generation_manifest* manifest,const laplace_framework_context** context,
    const laplace_framework_stream_receipt** staged,const laplace_perfcache_generation_request** req) {
    CHECK(manifest==(laplace_perfcache_generation_manifest*)&manifest_token);
    if(failure==4) pg_re_throw();
    *context=&framework_context;*staged=&stream_receipt;*req=&request;
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}
void laplace_perfcache_generation_manifest_close(laplace_perfcache_generation_manifest* manifest) {
    CHECK(manifest==(laplace_perfcache_generation_manifest*)&manifest_token);++manifest_closed;
}
laplace_perfcache_registry_status laplace_perfcache_generation_manifest_fingerprint(
    const laplace_perfcache_generation_request* req,laplace_digest256* digest) {
    CHECK(req==&request);memset(digest,0,sizeof(*digest));return LAPLACE_PERFCACHE_REGISTRY_OK;
}
laplace_perfcache_registry_status laplace_perfcache_registry_prepare(
    laplace_perfcache_registry* registry,const laplace_framework_context* context,
    const laplace_framework_stream_receipt* staged,const laplace_perfcache_artifact_provider_v1* provider,
    const laplace_perfcache_generation_request* req,laplace_perfcache_prepared_generation** prepared,
    laplace_perfcache_generation_receipt* receipt) {
    (void)registry;(void)context;(void)staged;(void)provider;(void)receipt;CHECK(req==&request);
    *prepared=(laplace_perfcache_prepared_generation*)&prepared_token;
    if(failure==5) pg_re_throw();
    return LAPLACE_PERFCACHE_REGISTRY_OK;
}
laplace_perfcache_registry_status laplace_perfcache_registry_materialize_prepared(
    laplace_perfcache_registry* registry,laplace_perfcache_prepared_generation** prepared,
    laplace_perfcache_generation_receipt* receipt) {
    (void)registry;(void)receipt;CHECK(*prepared==(laplace_perfcache_prepared_generation*)&prepared_token);
    if(failure==6) return LAPLACE_PERFCACHE_REGISTRY_EPOCH_MISMATCH;
    *prepared=NULL;return LAPLACE_PERFCACHE_REGISTRY_OK;
}
void laplace_perfcache_registry_discard_prepared(laplace_perfcache_prepared_generation** prepared) {
    CHECK(*prepared==(laplace_perfcache_prepared_generation*)&prepared_token);
    *prepared=NULL;++prepared_discarded;
}
/* These branches would create a new registry; this probe starts with an existing
 * registry whose exact requested generation is absent. Do not silently model them. */
laplace_perfcache_registry_status laplace_perfcache_builtin_module_resolve(
    const laplace_perfcache_contract* contract,laplace_perfcache_module_v2* module) {
    (void)contract;(void)module;CHECK(false);return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
}
laplace_perfcache_registry_status laplace_perfcache_required_module_set_fingerprint(
    const laplace_perfcache_module_v2* modules,size_t count,laplace_digest256* digest) {
    (void)modules;(void)count;(void)digest;CHECK(false);return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
}
laplace_perfcache_registry_status laplace_perfcache_file_provider(laplace_perfcache_artifact_provider_v1* provider) {
    (void)provider;CHECK(false);return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
}
laplace_perfcache_registry_status laplace_perfcache_registry_create(
    const laplace_perfcache_module_v2* modules,size_t count,laplace_perfcache_registry** registry) {
    (void)modules;(void)count;(void)registry;CHECK(false);return LAPLACE_PERFCACHE_REGISTRY_INVALID_ARGUMENT;
}

static void begin_case(unsigned mode) {
    CurrentMemoryContext=&caller;CHECK(SPI_connect()==SPI_OK_CONNECT);
    MemoryContextSwitchTo(&scratch);failure=mode;
    manifest_closed=prepared_discarded=pin_calls=forgotten=0;
    prepare_calls=keepplan_calls=execute_calls=0;last_error_code=0;budget_used=0;
}
static void end_case(void) {
    CHECK(depth==1);CHECK(CurrentMemoryContext==&scratch);
    CHECK(SPI_finish()==SPI_OK_FINISH);CHECK(depth==0);failure=0;
}
static void catalog_case(unsigned mode) {
    laplace_pg_perfcache_epoch epoch={0};laplace_digest256 manifest={{0}},receipt={{0}};
    uint64 sequence=0;bool active=false;volatile bool caught=false;
    begin_case(mode);
    PG_TRY();{synchronize_read_catalog(&epoch,&manifest,&receipt,&sequence,&active,NULL);CHECK(mode==0);}
    PG_CATCH();{caught=true;CHECK(mode!=0);}PG_END_TRY();
    CHECK(caught==(mode!=0));if(mode==0) {CHECK(sequence==17);CHECK(active);}
    end_case();
}
static void native_case(unsigned mode,bool registered) {
    laplace_pg_perfcache_epoch epoch={0};laplace_digest256 manifest={{0}};laplace_perfcache_pin pin={0};
    volatile bool caught=false;volatile laplace_pg_perfcache_status status=LAPLACE_PG_PERFCACHE_INTERNAL_ERROR;
    laplace_pg_perfcache_pin* volatile owned=NULL;
    const bool raises=mode==2 || mode==3 || mode==4 || mode==5;
    begin_case(mode);
    if(registered) {
        owned=palloc0(sizeof(*owned));owned->held=1;owned->owner_pid=MyProcPid;
        owned->owner_proc_number=MyProcNumber;owned->generation_index=0;
        owned->resource_owner=(ResourceOwner)&resource_token;
        perfcache_owners()[0].pid=MyProcPid;perfcache_owners()[0].pin_depth=1;
        perfcache_owners()[0].generation_index=0;perfcache_generations()[0].reader_count=1;
    }
    PG_TRY(); {
        status=registered?pin_native_generation(owned,NULL):native_pin_epoch(&epoch,&manifest,&pin,NULL);
        CHECK(!raises);
    } PG_CATCH();{caught=true;CHECK(raises);} PG_END_TRY();
    CHECK(caught==raises);
    CHECK(manifest_closed==((mode==0 || mode>=4)?1u:0u));
    CHECK(prepared_discarded==((mode==5 || mode==6)?1u:0u));
    if(!raises) CHECK(status==(mode==0?LAPLACE_PG_PERFCACHE_OK:LAPLACE_PG_PERFCACHE_GENERATION_MISMATCH));
    if(registered) {
        CHECK(forgotten==(raises?1u:0u));
        CHECK(perfcache_owners()[0].pin_depth==(raises?0u:1u));
        CHECK(perfcache_generations()[0].reader_count==(raises?0u:1u));
        if(!raises) {CHECK(owned->held==1);release_pin_internal(owned);pfree(owned);}
    }
    end_case();
}
/* These controls execute the production owners with a caller-owned meter.
 * SPI/provider boundaries are controlled, so these are explicit-call counts,
 * not PostgreSQL execution timings or backend-I/O measurements. */
static void set_catalog_plans(bool present) {
    perfcache_generation_plan=present?(SPIPlanPtr)(uintptr_t)1:NULL;
    perfcache_active_update_plan=present?(SPIPlanPtr)(uintptr_t)2:NULL;
    perfcache_active_select_plan=present?(SPIPlanPtr)(uintptr_t)3:NULL;
    perfcache_manifest_select_plan=present?(SPIPlanPtr)(uintptr_t)4:NULL;
}
static void prepare_budget_case(unsigned limit) {
    laplace_pg_spi_budget budget={&budget_used,limit};
    volatile bool caught=false;
    begin_case(0);set_catalog_plans(false);
    PG_TRY();{ensure_catalog_plans(&budget);}
    PG_CATCH();{caught=true;}PG_END_TRY();
    CHECK(caught==(limit<4));CHECK(budget_used==limit);
    CHECK(prepare_calls==limit);CHECK(keepplan_calls==limit);CHECK(execute_calls==0);
    if(caught) CHECK(last_error_code==ERRCODE_PROGRAM_LIMIT_EXCEEDED);
    CHECK((perfcache_generation_plan!=NULL)==(limit>=1));
    CHECK((perfcache_active_update_plan!=NULL)==(limit>=2));
    CHECK((perfcache_active_select_plan!=NULL)==(limit>=3));
    CHECK((perfcache_manifest_select_plan!=NULL)==(limit>=4));
    /* Retry retains each successful plan instead of preparing it again. */
    budget.maximum=4;ensure_catalog_plans(&budget);
    CHECK(budget_used==4);CHECK(prepare_calls==4);CHECK(keepplan_calls==4);
    ensure_catalog_plans(&budget);
    CHECK(budget_used==4);CHECK(prepare_calls==4);CHECK(keepplan_calls==4);
    end_case();
}
static void prepare_error_case(void) {
    laplace_pg_spi_budget budget={&budget_used,5};
    volatile bool caught=false;
    begin_case(8);set_catalog_plans(false);
    PG_TRY();{ensure_catalog_plans(&budget);}
    PG_CATCH();{caught=true;}PG_END_TRY();
    CHECK(caught);CHECK(budget_used==1);CHECK(prepare_calls==1);
    CHECK(keepplan_calls==0);CHECK(perfcache_generation_plan==NULL);
    failure=0;ensure_catalog_plans(&budget);
    CHECK(budget_used==5);CHECK(prepare_calls==5);CHECK(keepplan_calls==4);
    end_case();
}
static void catalog_budget_case(unsigned mode,bool cold,unsigned limit) {
    laplace_pg_perfcache_epoch epoch={0};laplace_digest256 manifest={{0}},receipt={{0}};
    uint64 sequence=0;bool active=false;volatile bool caught=false;
    laplace_pg_spi_budget budget={&budget_used,limit};
    const unsigned required=cold?5u:1u;
    const unsigned preparations=cold?(limit<4?limit:4u):0u;
    const unsigned executions=limit>=required?1u:0u;
    begin_case(mode);set_catalog_plans(!cold);
    PG_TRY();{synchronize_read_catalog(&epoch,&manifest,&receipt,&sequence,&active,&budget);}
    PG_CATCH();{caught=true;}PG_END_TRY();
    CHECK(caught==(limit<required || mode!=0));
    CHECK(prepare_calls==preparations);CHECK(keepplan_calls==preparations);
    CHECK(execute_calls==executions);CHECK(budget_used==preparations+executions);
    if(limit<required) CHECK(last_error_code==ERRCODE_PROGRAM_LIMIT_EXCEEDED);
    if(!caught) {CHECK(sequence==17);CHECK(active);}
    end_case();
}
static void native_budget_case(unsigned mode,bool registered,unsigned limit,bool warm) {
    laplace_pg_perfcache_epoch epoch={0};laplace_digest256 manifest={{0}};
    laplace_perfcache_pin pin={0};laplace_pg_perfcache_pin* volatile owned=NULL;
    volatile bool caught=false;
    volatile laplace_pg_perfcache_status status=LAPLACE_PG_PERFCACHE_INTERNAL_ERROR;
    laplace_pg_spi_budget budget={&budget_used,limit};
    const bool raises=!warm && (limit==0 || mode==2);
    begin_case(mode);set_catalog_plans(true);
    if(warm) pin_calls=1; /* The controlled registry already maps this epoch. */
    if(registered) {
        owned=palloc0(sizeof(*owned));owned->held=1;owned->owner_pid=MyProcPid;
        owned->owner_proc_number=MyProcNumber;owned->generation_index=0;
        owned->resource_owner=(ResourceOwner)&resource_token;
        perfcache_owners()[0].pid=MyProcPid;perfcache_owners()[0].pin_depth=1;
        perfcache_owners()[0].generation_index=0;perfcache_generations()[0].reader_count=1;
    }
    PG_TRY(); {
        status=registered?pin_native_generation(owned,&budget):
            native_pin_epoch(&epoch,&manifest,&pin,&budget);
    } PG_CATCH();{caught=true;}PG_END_TRY();
    CHECK(caught==raises);CHECK(prepare_calls==0);CHECK(keepplan_calls==0);
    CHECK(budget_used==(!warm && limit>0?1u:0u));CHECK(execute_calls==budget_used);
    CHECK(manifest_closed==(!warm && !raises?1u:0u));CHECK(prepared_discarded==0);
    if(!warm && limit==0) CHECK(last_error_code==ERRCODE_PROGRAM_LIMIT_EXCEEDED);
    if(!raises) CHECK(status==LAPLACE_PG_PERFCACHE_OK);
    if(registered) {
        CHECK(forgotten==(raises?1u:0u));
        CHECK(perfcache_owners()[0].pin_depth==(raises?0u:1u));
        CHECK(perfcache_generations()[0].reader_count==(raises?0u:1u));
        if(!raises) {CHECK(owned->held==1);release_pin_internal(owned);pfree(owned);}
    } else if(!raises) CHECK(laplace_perfcache_pin_release(&pin)==LAPLACE_PERFCACHE_REGISTRY_OK);
    end_case();
}
static void combined_cold_budget_case(void) {
    laplace_pg_perfcache_epoch epoch={0};laplace_digest256 manifest={{0}},receipt={{0}};
    laplace_perfcache_pin pin={0};uint64 sequence=0;bool active=false;
    laplace_pg_spi_budget budget={&budget_used,6};
    volatile bool caught=false;
    begin_case(0);set_catalog_plans(false);
    synchronize_read_catalog(&epoch,&manifest,&receipt,&sequence,&active,&budget);
    CHECK(budget_used==5);CHECK(prepare_calls==4);CHECK(execute_calls==1);
    CHECK(native_pin_epoch(&epoch,&manifest,&pin,&budget)==LAPLACE_PG_PERFCACHE_OK);
    CHECK(budget_used==6);CHECK(prepare_calls==4);CHECK(execute_calls==2);
    CHECK(laplace_perfcache_pin_release(&pin)==LAPLACE_PERFCACHE_REGISTRY_OK);
    /* Another synchronization has to reserve its own actual SELECT. */
    PG_TRY();{synchronize_read_catalog(&epoch,&manifest,&receipt,&sequence,&active,&budget);}
    PG_CATCH();{caught=true;}PG_END_TRY();
    CHECK(caught);CHECK(last_error_code==ERRCODE_PROGRAM_LIMIT_EXCEEDED);
    CHECK(budget_used==6);CHECK(prepare_calls==4);CHECK(execute_calls==2);
    budget.maximum=7;
    synchronize_read_catalog(&epoch,&manifest,&receipt,&sequence,&active,&budget);
    CHECK(budget_used==7);CHECK(prepare_calls==4);CHECK(execute_calls==3);
    end_case();
}
static void malformed_budget_case(void) {
    laplace_pg_spi_budget budget={NULL,1};
    volatile bool caught=false;
    begin_case(0);
    PG_TRY();{laplace_pg_spi_budget_charge(&budget);}
    PG_CATCH();{caught=true;}PG_END_TRY();
    CHECK(caught);CHECK(last_error_code==ERRCODE_INVALID_PARAMETER_VALUE);
    caught=false;budget.used=&budget_used;budget.maximum=UINT64_MAX;budget_used=UINT64_MAX;
    PG_TRY();{laplace_pg_spi_budget_charge(&budget);}
    PG_CATCH();{caught=true;}PG_END_TRY();
    CHECK(caught);CHECK(last_error_code==ERRCODE_PROGRAM_LIMIT_EXCEEDED);
    CHECK(budget_used==UINT64_MAX);CHECK(prepare_calls==0);CHECK(execute_calls==0);
    laplace_pg_spi_budget_charge(NULL);
    end_case();
}
int main(void) {
    perfcache_generation_plan=(SPIPlanPtr)(uintptr_t)1;
    perfcache_active_update_plan=(SPIPlanPtr)(uintptr_t)2;
    perfcache_active_select_plan=(SPIPlanPtr)(uintptr_t)3;
    perfcache_manifest_select_plan=(SPIPlanPtr)(uintptr_t)4;
    perfcache_native_registry=(laplace_perfcache_registry*)&registry_token;
    /* Real paths make production path admission run without a fake path check. */
    char root_path[PATH_MAX],artifact_path[PATH_MAX];
    CHECK(realpath(".",root_path)!=NULL);CHECK(realpath(__FILE__,artifact_path)!=NULL);
    perfcache_root=root_path;artifact.path=artifact_path;request.artifacts=&artifact;request.artifact_count=1;
    perfcache_shared=calloc(1,perfcache_generation_offset()+sizeof(laplace_pg_perfcache_generation_slot));
    CHECK(perfcache_shared!=NULL);perfcache_shared->generation_capacity=1;
    perfcache_generations()[0].state=LAPLACE_PG_PERFCACHE_GENERATION_ACTIVE;
    for(unsigned mode=0;mode<4;++mode) catalog_case(mode);
    for(unsigned mode=0;mode<8;++mode) {native_case(mode,false);native_case(mode,true);}
    for(unsigned limit=0;limit<=4;++limit) prepare_budget_case(limit);
    prepare_error_case();
    for(unsigned limit=0;limit<=5;++limit) catalog_budget_case(0,true,limit);
    catalog_budget_case(0,false,0);catalog_budget_case(0,false,1);
    catalog_budget_case(2,false,1);catalog_budget_case(3,false,1);
    for(unsigned registered=0;registered<2;++registered) {
        native_budget_case(0,registered!=0,0,false);
        native_budget_case(0,registered!=0,1,false);
        native_budget_case(2,registered!=0,1,false);
        native_budget_case(0,registered!=0,0,true);
    }
    combined_cold_budget_case();malformed_budget_case();
    CHECK(PG_exception_stack==NULL);CHECK(error_context_stack==NULL);
    for(size_t i=0;i<allocation_count;++i) free(allocations[i].bytes);
    free(perfcache_shared);
    printf("{\"checks\":%u,\"catalog_cases\":4,\"native_cases\":16,\"budget_cases\":26,\"sql_execution\":false}\n",checks);
    return 0;
}
