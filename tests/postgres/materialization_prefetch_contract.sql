-- Uses the BUILD_TESTING fixture in the real extension and a real backend SIGINT.
-- The included production callback must return deferred ErrorData to SQL.
CREATE SCHEMA materialization_prefetch_contract;
CREATE FUNCTION materialization_prefetch_contract.probe(integer)
RETURNS boolean AS 'laplace_pg','laplace_pg_test_materialization_prefetch'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

CREATE FUNCTION materialization_prefetch_contract.expect_cancel(mode integer)
RETURNS void LANGUAGE plpgsql AS $function$
DECLARE
    cancelled boolean := false;
    error_message text;
BEGIN
    BEGIN
        PERFORM materialization_prefetch_contract.probe(mode);
    EXCEPTION WHEN query_canceled THEN
        GET STACKED DIAGNOSTICS error_message = MESSAGE_TEXT;
        IF error_message <> 'canceling statement due to user request' THEN
            RAISE EXCEPTION 'prefetch cancellation came from an unexpected source: %', error_message;
        END IF;
        cancelled := true;
    END;
    IF NOT cancelled THEN
        RAISE EXCEPTION 'speculative prefetch swallowed cancellation';
    END IF;
END
$function$;

DO $contract$
DECLARE
    mutant_detected boolean := false;
BEGIN
    IF NOT materialization_prefetch_contract.probe(0) THEN
        RAISE EXCEPTION 'optional prefetch failure prevented cached trajectory read';
    END IF;
    PERFORM materialization_prefetch_contract.expect_cancel(1);
    BEGIN
        PERFORM materialization_prefetch_contract.expect_cancel(2);
    EXCEPTION WHEN raise_exception THEN
        IF SQLERRM <> 'speculative prefetch swallowed cancellation' THEN
            RAISE;
        END IF;
        mutant_detected := true;
    END;
    IF NOT mutant_detected THEN
        RAISE EXCEPTION 'prefetch cancellation classification mutant escaped the oracle';
    END IF;
    IF NOT materialization_prefetch_contract.probe(0) THEN
        RAISE EXCEPTION 'backend did not preserve ordinary prefetch fallback after cancellation';
    END IF;
END
$contract$;

SELECT 'LAPLACE_QA_RECEIPT materialization_prefetch_cancellation ' ||
    jsonb_build_object('schema','laplace.materialization-prefetch-cancellation/v1',
        'actual_prefetch_wrapper',true,'actual_provider_callback',true,
        'ordinary_speculative_fallbacks',2,'backend_sigint_requests',2,
        'propagated_cancellations',1,
        'cancellation_sqlstate','57014','cancelled_read_receipt_zeroed',true,
        'cancelled_trajectory_output_untouched',true,
        'resolving_cache_reset',true,'caller_context_restored',true,
        'classification_mutants_detected',1,'backend_usable_after_cancel',true)::text;

DROP SCHEMA materialization_prefetch_contract CASCADE;
\echo LAPLACE_MATERIALIZATION_PREFETCH_CANCELLATION_OK
