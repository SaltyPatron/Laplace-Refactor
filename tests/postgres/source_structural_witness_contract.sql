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
          first.composition_working_set_receipt_id;

    SELECT witness_count, witness_fingerprint
    INTO STRICT replay_receipt_count, replay_witness_fingerprint
    FROM laplace.source_structural_witness_receipt
    WHERE source_profile_id = replay.profile_id
      AND composition_working_set_receipt =
          replay.composition_working_set_receipt_id;

    SELECT count(*) INTO STRICT receipt_count
    FROM laplace.source_structural_witness_receipt
    WHERE source_profile_id = first.profile_id;

    IF durable_witness_count <= 0
       OR profile_span_count < durable_witness_count
       OR first_receipt_count <> durable_witness_count
       OR replay_receipt_count <> durable_witness_count
       OR first_witness_fingerprint <> replay_witness_fingerprint
       OR receipt_count < 2
       OR EXISTS (
            SELECT 1
            FROM laplace.source_structural_witness AS witness
            LEFT JOIN laplace.entity AS entity
              ON entity.entity_id = witness.canonical_entity_id
            WHERE witness.source_profile_id = first.profile_id
              AND entity.entity_id IS NULL)
       OR EXISTS (
            SELECT 1
            FROM laplace.source_structural_witness_receipt AS receipt
            WHERE receipt.source_profile_id = first.profile_id
              AND (receipt.witness_count <> durable_witness_count
                   OR receipt.witness_fingerprint <>
                      first_witness_fingerprint)) THEN
        RAISE EXCEPTION
            'structural decomposition witnesses did not survive durable replay with exact canonical bindings';
    END IF;
END
$contract$;
