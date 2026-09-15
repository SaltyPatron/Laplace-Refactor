DO $contract$
DECLARE
    first source_admission_first%ROWTYPE;
    replay source_admission_replay%ROWTYPE;
    durable_witness_count bigint;
    first_receipt_count numeric;
    replay_receipt_count numeric;
    first_witness_fingerprint bytea;
    replay_witness_fingerprint bytea;
    receipt_count bigint;
    profile_span_count numeric;
BEGIN
    SELECT * INTO STRICT first FROM source_admission_first;
    SELECT * INTO STRICT replay FROM source_admission_replay;

    SELECT count(*) INTO STRICT durable_witness_count
    FROM laplace.source_structural_witness
    WHERE source_profile_id = first.profile_id;

    SELECT span_count INTO STRICT profile_span_count
    FROM laplace.source_profile
    WHERE profile_id = first.profile_id;

    SELECT witness_count, witness_fingerprint
    INTO STRICT first_receipt_count, first_witness_fingerprint
    FROM laplace.source_structural_witness_receipt
    WHERE source_profile_id = first.profile_id
      AND composition_working_set_receipt =
          first.composition_working_set_receipt_id
      AND version = 3;

    SELECT witness_count, witness_fingerprint
    INTO STRICT replay_receipt_count, replay_witness_fingerprint
    FROM laplace.source_structural_witness_receipt
    WHERE source_profile_id = replay.profile_id
      AND composition_working_set_receipt =
          replay.composition_working_set_receipt_id
      AND version = 3;

    SELECT count(*) INTO STRICT receipt_count
    FROM laplace.source_structural_witness_receipt
    WHERE source_profile_id = first.profile_id;

    IF durable_witness_count <= 0
       OR profile_span_count < durable_witness_count
       OR first_receipt_count <> durable_witness_count
       OR replay_receipt_count <> durable_witness_count
       OR first_witness_fingerprint <> replay_witness_fingerprint
       OR receipt_count < 2
       OR NOT EXISTS (
            SELECT 1
            FROM laplace.source_structural_witness AS child
            JOIN laplace.source_structural_witness AS parent
              ON parent.source_profile_id = child.source_profile_id
             AND parent.artifact_index = child.artifact_index
             AND parent.span_index = child.parent_span_index
            WHERE child.source_profile_id = first.profile_id
              AND child.span_index > 0
              AND child.parent_span_index <>
                  18446744073709551615::numeric
              AND child.depth > parent.depth)
       OR EXISTS (
            SELECT 1
            FROM laplace.source_structural_witness AS witness
            LEFT JOIN laplace.entity AS entity
              ON entity.entity_id = witness.canonical_entity_id
            LEFT JOIN laplace.physicality AS physicality
              ON physicality.physicality_id = witness.canonical_physicality_id
            WHERE witness.source_profile_id = first.profile_id
              AND (entity.entity_id IS NULL OR witness.grammar_kind IS NULL
                   OR witness.field_kind IS NULL OR witness.sibling_ordinal IS NULL
                   OR witness.syntax_flags IS NULL OR physicality.physicality_id IS NULL
                   OR physicality.entity_id IS DISTINCT FROM witness.canonical_entity_id))
       OR EXISTS (
            SELECT 1
            FROM laplace.source_structural_witness_receipt AS receipt
            WHERE receipt.source_profile_id = first.profile_id
              AND receipt.version = 3
              AND (receipt.witness_count <> durable_witness_count
                   OR receipt.witness_fingerprint <>
                      first_witness_fingerprint)) THEN
        RAISE EXCEPTION
            'structural decomposition witnesses did not survive durable replay with exact canonical bindings';
    END IF;
END
$contract$;

-- Historical bindings remain nullable until the real native source replay
-- supplies the exact result physicality. Reproduce that missing-column-value
-- boundary without manufacturing a historical receipt or selecting by epoch.
DO $physicality_enrichment$
DECLARE selected laplace.source_structural_witness%ROWTYPE;
BEGIN
 SELECT w.* INTO STRICT selected FROM laplace.source_structural_witness w
 JOIN source_admission_first f ON f.profile_id=w.source_profile_id
 WHERE w.canonical_physicality_id IS NOT NULL
 ORDER BY w.artifact_index,w.span_index LIMIT 1;
 UPDATE laplace.source_structural_witness SET canonical_physicality_id=NULL
 WHERE source_profile_id=selected.source_profile_id AND artifact_index=selected.artifact_index
  AND span_index=selected.span_index;
 PERFORM pg_temp.admit_source();
 IF (SELECT canonical_physicality_id FROM laplace.source_structural_witness
     WHERE source_profile_id=selected.source_profile_id AND artifact_index=selected.artifact_index
      AND span_index=selected.span_index) IS DISTINCT FROM selected.canonical_physicality_id THEN
   RAISE EXCEPTION 'native source replay did not restore its exact physicality binding';
 END IF;
END
$physicality_enrichment$;

DO $mutation$
DECLARE
    replay source_admission_replay%ROWTYPE;
BEGIN
    SELECT * INTO STRICT replay FROM source_admission_replay;

    BEGIN
        UPDATE laplace.source_structural_witness
        SET syntax_flags = syntax_flags + 1
        WHERE ctid = (
            SELECT ctid
            FROM laplace.source_structural_witness
            WHERE source_profile_id = replay.profile_id
            ORDER BY artifact_index, span_index
            LIMIT 1);
        PERFORM pg_temp.admit_source();
        RAISE EXCEPTION
            'structural witness mutation was accepted by durable replay';
    EXCEPTION
        WHEN SQLSTATE 'XX001' THEN NULL;
    END;

    BEGIN
        UPDATE laplace.source_structural_witness_receipt
        SET witness_fingerprint = set_byte(
            witness_fingerprint, 0,
            get_byte(witness_fingerprint, 0) # 1)
        WHERE source_profile_id = replay.profile_id
          AND composition_working_set_receipt =
              replay.composition_working_set_receipt_id
      AND version = 3;
        PERFORM pg_temp.admit_source();
        RAISE EXCEPTION
            'structural witness receipt mutation was accepted by durable replay';
    EXCEPTION
        WHEN SQLSTATE 'XX001' THEN NULL;
    END;
END
$mutation$;
