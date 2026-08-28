\ir spi_contract.sql

CREATE FUNCTION pg_temp.evidence_testimony_replay_mutant(
    laplace.execution_context,
    laplace.evidence_testimony_record[])
RETURNS laplace.evidence_testimony_result
AS :'persistence_mutant_module',
   'laplace_pg_evidence_record_testimony_replay_mutant'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

DO $mutation$
DECLARE
    expected testimony_expected%ROWTYPE;
    records laplace.evidence_testimony_record[];
    result laplace.evidence_testimony_result;
BEGIN
    SELECT * INTO STRICT expected FROM testimony_expected;
    SELECT array_agg(
        ROW(
            expected.testimony_ids[ordinal],
            expected.evidence_node_ids[ordinal],
            expected.source_profile_id,
            expected.recipe_receipt_id,
            expected.trust_input_id,
            expected.outcome_detail_ids[ordinal],
            expected.uncertainty_numerators[ordinal],
            expected.uncertainty_denominators[ordinal],
            expected.sample_counts[ordinal],
            expected.source_types[ordinal],
            expected.outcome_types[ordinal],
            expected.dispositions[ordinal],
            0)::laplace.evidence_testimony_record
        ORDER BY expected.testimony_ids[ordinal])
    INTO STRICT records
    FROM generate_subscripts(expected.testimony_ids, 1) ordinal;

    result := pg_temp.evidence_testimony_replay_mutant(
        pg_temp.persistence_context(), records);
    UPDATE laplace.evidence_testimony
    SET disposition = CASE disposition WHEN 1 THEN 2 ELSE 1 END
    WHERE testimony_id = expected.testimony_ids[1];
    PERFORM pg_temp.evidence_testimony_replay_mutant(
        pg_temp.persistence_context(), records);
    RAISE EXCEPTION
        'evidence testimony replay mutant accepted conflicting durable testimony for receipt %',
        encode(result.testimony_receipt_id, 'hex');
END
$mutation$;
