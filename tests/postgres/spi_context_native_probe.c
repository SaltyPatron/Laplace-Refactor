/* Actual shared context boundary and Unicode metadata owner. The controlled SPI
 * shim deliberately follows PostgreSQL's _SPI_end_call(true) context switch;
 * it proves allocation/error lifetimes, not SQL execution or catalog contents. */
#ifndef UNICODE_ATOMS_ADAPTER_SOURCE
#define UNICODE_ATOMS_ADAPTER_SOURCE "../../integrations/postgresql/extension/src/unicode_atoms_pg.c"
#endif
#include UNICODE_ATOMS_ADAPTER_SOURCE
#ifndef SPI_CONTEXT_ADAPTER_HEADER
#define SPI_CONTEXT_ADAPTER_HEADER "../../integrations/postgresql/extension/src/spi_context_pg.h"
#endif
#include SPI_CONTEXT_ADAPTER_HEADER
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
static struct { MemoryContext owner; void* bytes; size_t size; } allocations[128];
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
int SPI_execute_with_args(const char* sql,int nargs,Oid* types,Datum* values,
    const char* nulls,bool read_only,long count) {
    (void)sql;(void)nargs;(void)types;(void)values;(void)nulls;(void)read_only;(void)count;
    CHECK(depth > 0u);
    /* PostgreSQL 18.6 spi.c: _SPI_end_call(true) calls _SPI_procmem(). */
    CurrentMemoryContext=&procedures[depth-1u];
    if(failure==2u) pg_re_throw();
    SPI_processed=failure==1u ? 0u : 1u;
    table.vals=rows;SPI_tuptable=&table;return SPI_OK_SELECT;
}
Datum SPI_getbinval(HeapTuple tuple,TupleDesc descriptor,int column,bool* is_null) {
    uint8_t expected[32];memset(expected,0x6b,sizeof(expected));
    (void)tuple;(void)descriptor;CHECK(column==1);*is_null=false;
    return PointerGetDatum(laplace_pg_bytes_to_bytea(expected,failure==3u ? 31u : 32u));
}

static void query_lifetime(MemoryContext target) {
    CurrentMemoryContext=&caller;CHECK(SPI_connect()==SPI_OK_CONNECT);
    MemoryContextSwitchTo(target);
    CHECK(laplace_pg_spi_execute_with_args("controlled query",0,NULL,NULL,NULL,true,1)==SPI_OK_SELECT);
    CHECK(CurrentMemoryContext==target);
    uint8_t* retained=palloc(32u);memset(retained,0x73,32u);
    CHECK(SPI_finish()==SPI_OK_FINISH);
    for(size_t i=0;i<32u;++i) CHECK(retained[i]==0x73);
    pfree(retained);
}

static void query_error(void) {
    volatile bool caught=false;
    CurrentMemoryContext=&caller;CHECK(SPI_connect()==SPI_OK_CONNECT);
    MemoryContextSwitchTo(&scratch);failure=2u;
    PG_TRY();
    { (void)laplace_pg_spi_execute_with_args("controlled error",0,NULL,NULL,NULL,true,1); }
    PG_CATCH();
    { caught=true;CHECK(CurrentMemoryContext==&scratch);CHECK(depth==1u); }
    PG_END_TRY();
    CHECK(caught);failure=0u;CHECK(SPI_finish()==SPI_OK_FINISH);
}

static void indirect_lifetime(bool raises) {
    volatile bool caught=false;
    CurrentMemoryContext=&caller;
    PG_TRY();
    {
        LAPLACE_PG_PRESERVE_MEMORY_CONTEXT(
            { MemoryContextSwitchTo(&scratch); if(raises) pg_re_throw(); });
        CHECK(!raises);CHECK(CurrentMemoryContext==&caller);
    }
    PG_CATCH();
    { caught=true;CHECK(raises);CHECK(CurrentMemoryContext==&caller); }
    PG_END_TRY();
    CHECK(caught==raises);
}

static void unicode_connection(unsigned mode) {
    laplace_pg_perfcache_epoch epoch={0};laplace_digest256 receipt={{0}};
    volatile bool caught=false;
    CurrentMemoryContext=&caller;CHECK(SPI_connect()==SPI_OK_CONNECT);
    MemoryContextSwitchTo(&scratch);failure=mode;
    PG_TRY();
    { read_root_receipt_for_epoch(&epoch,&receipt);CHECK(mode==0u); }
    PG_CATCH();
    { caught=true;CHECK(mode!=0u); }
    PG_END_TRY();
    CHECK(caught==(mode!=0u));CHECK(depth==1u);CHECK(CurrentMemoryContext==&scratch);
    if(mode==0u) for(size_t i=0;i<32u;++i) CHECK(receipt.bytes[i]==0x6b);
    failure=0u;CHECK(SPI_finish()==SPI_OK_FINISH);CHECK(depth==0u);
}

int main(void) {
    query_lifetime(&caller);query_lifetime(&scratch);query_error();
    indirect_lifetime(false);indirect_lifetime(true);
    for(unsigned mode=0u;mode<4u;++mode) unicode_connection(mode);
    CHECK(PG_exception_stack==NULL);CHECK(error_context_stack==NULL);
    for(size_t i=0;i<allocation_count;++i) free(allocations[i].bytes);
    printf("{\"checks\":%u,\"query_contexts\":2,\"unicode_connection_cases\":4,\"sql_execution\":false}\n",checks);
    return 0;
}
