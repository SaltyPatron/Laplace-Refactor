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
int errcode(int code) { return code; }
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
    (void)sql;(void)count;(void)types;CHECK(false);return NULL;
}
int SPI_keepplan(SPIPlanPtr plan) { (void)plan;CHECK(false);return -1; }
int SPI_execute_plan(SPIPlanPtr plan,Datum* values,const char* nulls,bool readonly,long count) {
    (void)values;(void)nulls;(void)readonly;CHECK(count==1);CHECK(depth==2);
    current_plan=plan;CurrentMemoryContext=&procedures[depth-1];
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
}
static void end_case(void) {
    CHECK(depth==1);CHECK(CurrentMemoryContext==&scratch);
    CHECK(SPI_finish()==SPI_OK_FINISH);CHECK(depth==0);failure=0;
}
static void catalog_case(unsigned mode) {
    laplace_pg_perfcache_epoch epoch={0};laplace_digest256 manifest={{0}},receipt={{0}};
    uint64 sequence=0;bool active=false;volatile bool caught=false;
    begin_case(mode);
    PG_TRY();{synchronize_read_catalog(&epoch,&manifest,&receipt,&sequence,&active);CHECK(mode==0);}
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
        status=registered?pin_native_generation(owned):native_pin_epoch(&epoch,&manifest,&pin);
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
    CHECK(PG_exception_stack==NULL);CHECK(error_context_stack==NULL);
    for(size_t i=0;i<allocation_count;++i) free(allocations[i].bytes);
    free(perfcache_shared);
    printf("{\"checks\":%u,\"catalog_cases\":4,\"native_cases\":16,\"sql_execution\":false}\n",checks);
    return 0;
}
