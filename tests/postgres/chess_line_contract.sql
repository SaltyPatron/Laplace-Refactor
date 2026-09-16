-- Private qualification setup. The caller uses the existing guarded Unicode
-- bootstrap and native source fixture variables; no alternative atom floor.
\ir source_admission_contract.sql

-- Retain the actual admitted context across genuine libpq backend reconnects.
-- This schema is disposable test transport, never a production chess store.
CREATE SCHEMA chess_line_contract;
CREATE TABLE chess_line_contract.context AS
SELECT pg_temp.source_admission_context() AS value,
       active.activation_epoch_id, active.epoch_fingerprint
FROM laplace.perfcache_active_control active
WHERE active.singleton AND active.active_present;

CREATE FUNCTION chess_line_contract.require_active()
RETURNS void LANGUAGE plpgsql VOLATILE AS $body$
BEGIN
    IF (SELECT count(*) FROM chess_line_contract.context) <> 1
       OR NOT EXISTS (
           SELECT 1 FROM chess_line_contract.context saved
           JOIN laplace.perfcache_active_control active
             ON active.singleton AND active.active_present
            AND active.activation_epoch_id=saved.activation_epoch_id
            AND active.epoch_fingerprint=saved.epoch_fingerprint)
       OR current_setting('fsync') <> 'on'
       OR current_setting('synchronous_commit') <> 'on'
       OR current_setting('full_page_writes') <> 'on'
    THEN RAISE EXCEPTION 'chess qualification requires pinned active Unicode and synchronous durable PostgreSQL';
    END IF;
END $body$;
SELECT chess_line_contract.require_active();

CREATE FUNCTION chess_line_contract.context_json()
RETURNS jsonb LANGUAGE plpgsql VOLATILE AS $body$
DECLARE result jsonb;
BEGIN
    PERFORM chess_line_contract.require_active();
    SELECT jsonb_build_object(
        'epochs',(SELECT jsonb_agg(encode(epoch,'hex') ORDER BY ordinal)
                  FROM unnest((c.value).epochs) WITH ORDINALITY e(epoch,ordinal)),
        'authority',encode((c.value).authority_fingerprint,'hex'),
        'memory_bytes',(c.value).memory_bytes::text,
        'cpu_slots',(c.value).cpu_slots::text,'io_slots',(c.value).io_slots::text,
        'epoch_mask',(c.value).epoch_mask::text,
        'major',(c.value).framework_major::text,'minor',(c.value).framework_minor::text,
        'flags',(c.value).flags::text,
        'activation_epoch_id',encode(c.activation_epoch_id,'hex'),
        'activation_epoch_fingerprint',encode(c.epoch_fingerprint,'hex'))
    INTO STRICT result FROM chess_line_contract.context c;
    RETURN result;
END $body$;

CREATE FUNCTION chess_line_contract.atoms(positions integer[])
RETURNS TABLE(ordinal bigint, known laplace.composition_known_entity_record)
LANGUAGE plpgsql VOLATILE AS $body$
DECLARE resolved laplace.unicode_tier0_batch_result;
BEGIN
    PERFORM chess_line_contract.require_active();
    IF cardinality(positions) NOT BETWEEN 1 AND 256
       OR array_position(positions,NULL) IS NOT NULL
       OR (SELECT count(DISTINCT position) FROM unnest(positions) position) <> cardinality(positions)
    THEN RAISE EXCEPTION 'chess atom request is empty, duplicate or unbounded'; END IF;
    -- Expand the native composite into columns once, rather than assigning
    -- its serialized whole row to the first integer[] field of resolved.
    SELECT native_row.* INTO STRICT resolved
    FROM chess_line_contract.context c
    CROSS JOIN LATERAL laplace.unicode_tier0_resolve_batch(
        c.activation_epoch_id,c.epoch_fingerprint,positions) native_row;
    IF resolved.codepoint_positions IS DISTINCT FROM positions
       OR cardinality(resolved.found) IS DISTINCT FROM cardinality(positions)
       OR array_position(resolved.found,false) IS NOT NULL
       OR array_position(resolved.found,NULL) IS NOT NULL
       OR NOT EXISTS (SELECT 1 FROM chess_line_contract.context c
          WHERE resolved.activation_epoch_id IS NOT DISTINCT FROM c.activation_epoch_id
            AND resolved.activation_epoch_fingerprint IS NOT DISTINCT FROM c.epoch_fingerprint)
       OR EXISTS (
           SELECT 1 FROM generate_subscripts(positions,1) i
           LEFT JOIN laplace.physicality p ON p.physicality_id=resolved.physicality_ids[i]
           LEFT JOIN laplace.entity e ON e.entity_id=resolved.entity_ids[i]
           WHERE p.physicality_id IS NULL OR e.entity_id IS NULL
             OR p.entity_id IS DISTINCT FROM resolved.entity_ids[i]
             OR e.identity_witness IS DISTINCT FROM resolved.identity_preimage_fingerprints[i]
             OR p.geometry_epoch IS DISTINCT FROM resolved.geometry_epochs[i]
             OR float8send(p.centroid_x) IS DISTINCT FROM float8send(resolved.coordinate_x[i])
             OR float8send(p.centroid_y) IS DISTINCT FROM float8send(resolved.coordinate_y[i])
             OR float8send(p.centroid_z) IS DISTINCT FROM float8send(resolved.coordinate_z[i])
             OR float8send(p.centroid_m) IS DISTINCT FROM float8send(resolved.coordinate_m[i]))
    THEN RAISE EXCEPTION 'chess atom tuple differs from the active native floor' USING ERRCODE='XX001';
    END IF;
    RETURN QUERY
    SELECT i::bigint,ROW(p.entity_id::bytea,e.identity_witness::bytea,p.physicality_id::bytea,
        p.centroid_x,p.centroid_y,p.centroid_z,p.centroid_m,
        positions[i]::bigint,0::smallint,true)::laplace.composition_known_entity_record
    FROM generate_subscripts(positions,1) i
    JOIN laplace.physicality p ON p.physicality_id=resolved.physicality_ids[i]
    JOIN laplace.entity e ON e.entity_id=p.entity_id ORDER BY i;
END $body$;

CREATE FUNCTION chess_line_contract.atom_json(positions integer[])
RETURNS jsonb LANGUAGE SQL VOLATILE AS $body$
    SELECT jsonb_agg(jsonb_build_object(
        'ordinal',ordinal::text,'atom',(known).atom::text,
        'entity_id',encode((known).entity_id,'hex'),
        'identity_witness',encode((known).identity_witness,'hex'),
        'physicality_id',encode((known).physicality_id,'hex'),
        'centroid',jsonb_build_array(
            encode(float8send((known).centroid_x),'hex'),encode(float8send((known).centroid_y),'hex'),
            encode(float8send((known).centroid_z),'hex'),encode(float8send((known).centroid_m),'hex')))
        ORDER BY ordinal)
    FROM chess_line_contract.atoms(positions)
$body$;

CREATE FUNCTION chess_line_contract.physicality_json(p laplace.physicality)
RETURNS jsonb LANGUAGE SQL IMMUTABLE STRICT AS $body$
    SELECT jsonb_build_object(
        'physicality_id',encode(p.physicality_id,'hex'),'entity_id',encode(p.entity_id,'hex'),
        'physicality_type',p.physicality_type::text,'vertex_class',p.vertex_class::text,
        'recipe_version',p.recipe_version::text,'structural_form',p.structural_form::text,
        'dimension_count',p.dimension_count::text,'flags',p.flags::text,
        'recipe_fingerprint',encode(p.recipe_fingerprint,'hex'),
        'geometry_epoch',encode(p.geometry_epoch,'hex'),
        'trajectory_fingerprint',encode(p.trajectory_fingerprint,'hex'),
        'centroid',jsonb_build_array(
            encode(float8send(p.centroid_x),'hex'),encode(float8send(p.centroid_y),'hex'),
            encode(float8send(p.centroid_z),'hex'),encode(float8send(p.centroid_m),'hex')),
        'radius',encode(float8send(p.radius),'hex'),
        'logical_count',p.logical_count::text,'vertex_count',p.vertex_count::text,
        'trajectory',encode(p.trajectory,'hex'))
$body$;

CREATE FUNCTION chess_line_contract.counts()
RETURNS jsonb LANGUAGE SQL STABLE AS $body$
    SELECT jsonb_build_object(
        'entities',(SELECT count(*)::text FROM laplace.entity),
        'physicalities',(SELECT count(*)::text FROM laplace.physicality),
        'attestations',(SELECT count(*)::text FROM laplace.attestation),
        'canonical_deposits',(SELECT count(*)::text FROM laplace.canonical_deposit_receipt))
$body$;

CREATE FUNCTION chess_line_contract.verify(expected jsonb)
RETURNS jsonb LANGUAGE plpgsql VOLATILE AS $body$
DECLARE entities bigint; physicalities bigint;
BEGIN
    PERFORM chess_line_contract.require_active();
    IF jsonb_typeof(expected->'entities') IS DISTINCT FROM 'array'
       OR jsonb_typeof(expected->'physicalities') IS DISTINCT FROM 'array'
    THEN RAISE EXCEPTION 'chess native expected candidate arrays are absent'; END IF;
    entities:=jsonb_array_length(expected->'entities');
    physicalities:=jsonb_array_length(expected->'physicalities');
    IF entities NOT BETWEEN 1 AND 100000 OR physicalities NOT BETWEEN 1 AND 100000
       OR entities<>(SELECT count(DISTINCT entry->>'entity_id')
                     FROM jsonb_array_elements(expected->'entities') entry)
       OR physicalities<>(SELECT count(DISTINCT entry->>'physicality_id')
                          FROM jsonb_array_elements(expected->'physicalities') entry)
    THEN RAISE EXCEPTION 'chess native expected candidates are missing, duplicate or unbounded'; END IF;
    IF EXISTS (
        SELECT 1 FROM jsonb_array_elements(expected->'entities') entry
        LEFT JOIN laplace.entity e ON e.entity_id=decode(entry->>'entity_id','hex')
        WHERE e.entity_id IS NULL
           OR encode(e.identity_witness,'hex') IS DISTINCT FROM entry->>'identity_witness')
    THEN RAISE EXCEPTION 'chess exact entity witness readback failed' USING ERRCODE='XX001'; END IF;
    IF EXISTS (
        SELECT 1 FROM jsonb_array_elements(expected->'physicalities') entry
        LEFT JOIN laplace.physicality p ON p.physicality_id=decode(entry->>'physicality_id','hex')
        WHERE p.physicality_id IS NULL
           OR chess_line_contract.physicality_json(p) IS DISTINCT FROM entry)
    THEN RAISE EXCEPTION 'chess exact physicality and trajectory readback failed' USING ERRCODE='XX001'; END IF;
    RETURN jsonb_build_object('entities_verified',entities::text,
        'physicalities_verified',physicalities::text,'exact_full_bodies',true);
END $body$;

CREATE FUNCTION chess_line_contract.deposit(plan jsonb,source bytea,expected jsonb)
RETURNS jsonb LANGUAGE plpgsql VOLATILE AS $body$
DECLARE positions integer[]; known laplace.composition_known_entity_record[];
    operands laplace.composition_operand_record[]; requests laplace.composition_request_record[];
    context laplace.execution_context; result laplace.composition_deposit_result;
    before jsonb; after_state jsonb; checked jsonb;
BEGIN
    PERFORM chess_line_contract.require_active();
    IF plan IS NULL OR expected IS NULL OR source IS NULL
       OR plan->>'schema' IS DISTINCT FROM 'laplace.chess-plan-transport/v1'
       OR jsonb_typeof(plan->'atoms') IS DISTINCT FROM 'array'
       OR jsonb_typeof(plan->'operands') IS DISTINCT FROM 'array'
       OR jsonb_typeof(plan->'requests') IS DISTINCT FROM 'array'
    THEN RAISE EXCEPTION 'chess native plan transport schema is incomplete'; END IF;
    IF octet_length(source)<>32 OR octet_length(plan::text)>33554432
       OR jsonb_array_length(plan->'atoms') NOT BETWEEN 1 AND 256
       OR jsonb_array_length(plan->'operands') NOT BETWEEN 1 AND 1000000
       OR jsonb_array_length(plan->'requests') NOT BETWEEN 1 AND 100000
    THEN RAISE EXCEPTION 'chess native plan transport is incomplete or unbounded'; END IF;
    SELECT array_agg(value::integer ORDER BY ordinal) INTO positions
    FROM jsonb_array_elements_text(plan->'atoms') WITH ORDINALITY a(value,ordinal);
    SELECT array_agg(a.known ORDER BY a.ordinal) INTO known FROM chess_line_contract.atoms(positions) a;
    SELECT array_agg(ROW(
        (entry->>0)::numeric,(entry->>1)::numeric,(entry->>2)::bigint,
        (entry->>3)::integer,(entry->>4)::integer)::laplace.composition_operand_record ORDER BY ordinal)
    INTO operands FROM jsonb_array_elements(plan->'operands') WITH ORDINALITY a(entry,ordinal);
    SELECT array_agg(ROW(
        (entry->>0)::numeric,(entry->>1)::numeric,(entry->>2)::numeric,
        (entry->>3)::integer,(entry->>4)::integer,decode(entry->>5,'hex'),
        decode(entry->>6,'hex'),decode(entry->>7,'hex'))::laplace.composition_request_record ORDER BY ordinal)
    INTO requests FROM jsonb_array_elements(plan->'requests') WITH ORDINALITY a(entry,ordinal);
    IF EXISTS (SELECT 1 FROM unnest(requests) r WHERE r.flags<>0)
    THEN RAISE EXCEPTION 'chess qualification cannot manufacture occurrence evidence'; END IF;
    SELECT (c.value).* INTO STRICT context FROM chess_line_contract.context c;
    before:=chess_line_contract.counts();
    result:=laplace.composition_deposit_batch(context,source,decode(plan->>'recipe_fingerprint','hex'),
        known,operands,requests,65536::numeric);
    IF result.status IS DISTINCT FROM 0
       OR result.occurrence_count IS DISTINCT FROM 0::numeric
       -- This public field counts logical children in unique physicalities,
       -- not evidence observations. Bind it to the native structural count.
       OR result.logical_occurrence_count IS DISTINCT FROM (expected->>'logical_occurrence_count')::numeric
       OR result.trajectory_vertex_count IS DISTINCT FROM (expected->>'trajectory_vertex_count')::numeric
       OR result.unique_entity_count IS DISTINCT FROM jsonb_array_length(expected->'entities')::numeric
       OR result.unique_physicality_count IS DISTINCT FROM jsonb_array_length(expected->'physicalities')::numeric
       OR result.occurrence_inserted IS DISTINCT FROM 0::numeric
       OR cardinality(result.result_entity_ids) IS DISTINCT FROM cardinality(requests)
       OR cardinality(result.result_physicality_ids) IS DISTINCT FROM cardinality(requests)
       OR (SELECT jsonb_agg(encode(id,'hex') ORDER BY ordinal)
           FROM unnest(result.result_entity_ids) WITH ORDINALITY r(id,ordinal))
          IS DISTINCT FROM expected->'result_entities'
       OR (SELECT jsonb_agg(encode(id,'hex') ORDER BY ordinal)
           FROM unnest(result.result_physicality_ids) WITH ORDINALITY r(id,ordinal))
          IS DISTINCT FROM expected->'result_physicalities'
    THEN RAISE EXCEPTION 'chess common composition call did not complete its exact boundary'; END IF;
    checked:=chess_line_contract.verify(expected);
    after_state:=chess_line_contract.counts();
    IF (after_state->>'entities')::bigint-(before->>'entities')::bigint<>result.entity_inserted
       OR (after_state->>'physicalities')::bigint-(before->>'physicalities')::bigint<>result.physicality_inserted
       OR after_state->'attestations' IS DISTINCT FROM before->'attestations'
       OR (after_state->>'canonical_deposits')::bigint-(before->>'canonical_deposits')::bigint
          <> (CASE WHEN result.producer_receipt IS NULL THEN 0 ELSE 1 END)
    THEN RAISE EXCEPTION 'chess inserted counters disagree with canonical durable state'; END IF;
    RETURN jsonb_build_object('result',to_jsonb(result),'readback',checked,
        'before',before,'after',after_state);
END $body$;

CREATE FUNCTION chess_line_contract.corruption_controls(expected jsonb,root bytea)
RETURNS integer LANGUAGE plpgsql VOLATILE AS $body$
DECLARE control_variant integer; control_rejected boolean; control_entity_id bytea;
BEGIN
    SELECT p.entity_id INTO STRICT control_entity_id
    FROM laplace.physicality p WHERE p.physicality_id=corruption_controls.root;
    FOR control_variant IN 1..3 LOOP
        control_rejected:=false;
        BEGIN
            IF control_variant=1 THEN
                UPDATE laplace.entity AS e SET identity_witness=set_byte(e.identity_witness,31,
                    get_byte(e.identity_witness,31)#1) WHERE e.entity_id=control_entity_id;
            ELSIF control_variant=2 THEN
                UPDATE laplace.physicality AS p SET radius=CASE WHEN p.radius=0 THEN 0.125 ELSE 0 END
                    WHERE p.physicality_id=corruption_controls.root;
            ELSE
                UPDATE laplace.physicality AS p SET trajectory=set_byte(p.trajectory,0,get_byte(p.trajectory,0)#1)
                    WHERE p.physicality_id=corruption_controls.root;
            END IF;
            PERFORM chess_line_contract.verify(corruption_controls.expected);
        EXCEPTION WHEN data_corrupted THEN control_rejected:=true;
        END;
        IF NOT control_rejected THEN RAISE EXCEPTION 'chess corruption control % was accepted',control_variant; END IF;
        PERFORM chess_line_contract.verify(corruption_controls.expected);
    END LOOP;
    RETURN 3;
END $body$;
-- Exercise PostgreSQL's actual typed-row assignment and the selected active
-- atom provider before launching the native carrier. The deliberately unexpanded
-- projections reproduce the original failure; no synthetic atom floor is used.
DO $composite_contract$
DECLARE projected laplace.execution_context;
    broken_context laplace.execution_context;
    broken_atoms laplace.unicode_tier0_batch_result;
    atom_values jsonb; returned_positions text[];
    rejected_context boolean:=false; rejected_atoms boolean:=false;
BEGIN
    PERFORM chess_line_contract.require_active();
    SELECT (c.value).* INTO STRICT projected FROM chess_line_contract.context c;
    IF to_jsonb(projected) IS DISTINCT FROM
       (SELECT to_jsonb(c.value) FROM chess_line_contract.context c)
    THEN RAISE EXCEPTION 'chess expanded execution context changed a native field'; END IF;

    atom_values:=chess_line_contract.atom_json(ARRAY[110,117,109]);
    SELECT array_agg(entry->>'atom' ORDER BY ordinal) INTO returned_positions
    FROM jsonb_array_elements(atom_values) WITH ORDINALITY a(entry,ordinal);
    IF returned_positions IS DISTINCT FROM ARRAY['110','117','109']::text[]
    THEN RAISE EXCEPTION 'chess expanded native atom result lost ordered tuples'; END IF;

    BEGIN
        SELECT c.value INTO STRICT broken_context FROM chess_line_contract.context c;
    EXCEPTION WHEN invalid_text_representation THEN rejected_context:=true;
    END;
    BEGIN
        SELECT laplace.unicode_tier0_resolve_batch(
            c.activation_epoch_id,c.epoch_fingerprint,ARRAY[110,117,109])
        INTO STRICT broken_atoms FROM chess_line_contract.context c;
    EXCEPTION WHEN invalid_text_representation THEN rejected_atoms:=true;
    END;
    IF NOT rejected_context OR NOT rejected_atoms
    THEN RAISE EXCEPTION 'chess unexpanded-composite defect control was not rejected'; END IF;
    RAISE NOTICE 'LAPLACE_CHESS_COMPOSITE_ASSIGNMENT_RECEIPT %',jsonb_build_object(
        'schema','laplace.chess-postgres-composite-assignment/v1',
        'status','passed','active_atom_positions',returned_positions,
        'expanded_context_exact',true,'active_atom_tuples_verified',true,
        'unexpanded_context_rejected',rejected_context,
        'unexpanded_native_atoms_rejected',rejected_atoms);
END $composite_contract$;
\echo LAPLACE_CHESS_LINE_POSTGRES_SETUP_OK
