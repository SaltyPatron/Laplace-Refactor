PG_FUNCTION_INFO_V1(laplace_pg_execution_context_fingerprint);

Datum laplace_pg_execution_context_fingerprint(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_digest256 fingerprint;
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    memset(&fingerprint, 0, sizeof(fingerprint));
    if (laplace_framework_context_fingerprint(&context, &fingerprint) !=
        LAPLACE_FRAMEWORK_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace execution context cannot be fingerprinted")));
    }
    PG_RETURN_BYTEA_P(laplace_pg_bytes_to_bytea(
        fingerprint.bytes, sizeof(fingerprint.bytes)));
}
