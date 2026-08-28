\ir spi_contract.sql

CREATE FUNCTION pg_temp.evidence_root_replay_mutant(
    laplace.execution_context,
    laplace.evidence_lineage_record[],
    numeric)
RETURNS laplace.evidence_lineage_result
AS :'persistence_mutant_module',
   'laplace_pg_evidence_record_lineage_root_replay_mutant'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

DO $mutation$
DECLARE
    records laplace.evidence_lineage_record[];
    expected evidence_expected%ROWTYPE;
    result laplace.evidence_lineage_result;
BEGIN
    SELECT * INTO STRICT expected FROM evidence_expected;
    SELECT array_agg(input.record ORDER BY (input.record).record_kind,
                                           (input.record).node_id,
                                           (input.record).parent_node_id)
    INTO STRICT records
    FROM (
        SELECT ROW(
            node.node_id,
            node.proposition_id,
            node.occurrence_id,
            node.source_id,
            node.context_id,
            decode(repeat('00', 32), 'hex'),
            node.source_ordinal,
            1,
            node.epistemic_kind,
            node.flags,
            0)::laplace.evidence_lineage_record AS record
        FROM laplace.evidence_node AS node
        UNION ALL
        SELECT ROW(
            dependence.node_id,
            decode(repeat('00', 16), 'hex'),
            decode(repeat('00', 32), 'hex'),
            decode(repeat('00', 32), 'hex'),
            decode(repeat('00', 32), 'hex'),
            dependence.parent_node_id,
            0::numeric,
            2,
            0,
            0,
            0)::laplace.evidence_lineage_record AS record
        FROM laplace.evidence_dependence AS dependence
    ) AS input;

    result := pg_temp.evidence_root_replay_mutant(
        pg_temp.persistence_context(), records, 3::numeric);
    UPDATE laplace.evidence_root_projection
    SET path_depth = path_depth + 1
    WHERE node_id = expected.copy_node
      AND root_node_id = expected.root_node;
    PERFORM pg_temp.evidence_root_replay_mutant(
        pg_temp.persistence_context(), records, 3::numeric);
    RAISE EXCEPTION
        'evidence root-replay mutant accepted conflicting durable projection for receipt %',
        encode(result.lineage_receipt_id, 'hex');
END
$mutation$;
