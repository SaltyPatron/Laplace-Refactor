-- Runs after source_admission_contract.sql in its disposable PostgreSQL cluster.
-- CMake fills context flags and one-based epoch indexes from framework.json.
-- Keep this fixture's context and receipts across a genuine backend reconnect.
-- CREATE without IF NOT EXISTS prevents cleanup from claiming an existing schema.
CREATE SCHEMA physicality_entity_contract;

CREATE TABLE physicality_entity_contract.context AS
SELECT pg_temp.source_admission_context() AS value;

CREATE TABLE physicality_entity_contract.sources AS
WITH resolved AS (
    SELECT atoms.*
    FROM laplace.perfcache_active_control active
    CROSS JOIN LATERAL laplace.unicode_tier0_resolve_batch(
        active.activation_epoch_id, active.epoch_fingerprint, ARRAY[65,66]) atoms
    WHERE active.singleton AND active.active_present
)
SELECT index AS ordinal, resolved.codepoint_positions[index] AS codepoint,
    p.physicality_id::bytea AS record_id, p.entity_id::bytea AS entity_id,
    ROW(p.entity_id::bytea,e.identity_witness::bytea,p.physicality_id::bytea,
        p.centroid_x,p.centroid_y,p.centroid_z,p.centroid_m,
        resolved.codepoint_positions[index]::bigint,0::smallint,true)
        ::laplace.composition_known_entity_record AS selected
FROM resolved CROSS JOIN generate_subscripts(resolved.entity_ids,1) AS index
JOIN laplace.physicality p ON p.physicality_id=resolved.physicality_ids[index]
JOIN laplace.entity e ON e.entity_id=p.entity_id
WHERE resolved.found[index] AND p.entity_id=resolved.entity_ids[index]
    AND e.identity_witness=resolved.identity_preimage_fingerprints[index];

DO $source$
BEGIN
    IF (SELECT count(*) FROM physicality_entity_contract.sources) <> 2
       OR EXISTS (SELECT 1 FROM physicality_entity_contract.sources source
           JOIN laplace.physicality p ON p.physicality_id=source.record_id
           CROSS JOIN physicality_entity_contract.context context
           WHERE p.geometry_epoch IS NOT DISTINCT FROM
               (context.value).epochs[@LAPLACE_PHYSICALITY_TEST_GEOMETRY_INDEX@]) THEN
        RAISE EXCEPTION 'physicality descriptor fixture lacks exact admitted Unicode inputs outside its view geometry';
    END IF;
END $source$;

CREATE FUNCTION physicality_entity_contract.counts()
RETURNS jsonb LANGUAGE SQL STABLE AS $counts$
    SELECT jsonb_build_object(
        'entities',(SELECT count(*) FROM laplace.entity),
        'physicalities',(SELECT count(*) FROM laplace.physicality),
        'attestations',(SELECT count(*) FROM laplace.attestation),
        'views',(SELECT count(*) FROM laplace.physicality_entity_view),
        'nodes',(SELECT count(*) FROM laplace.physicality_entity_node),
        'binding_sets',(SELECT count(*) FROM laplace.physicality_occurrence_binding_set),
        'binding_parents',(SELECT count(*) FROM laplace.physicality_occurrence_parent),
        'binding_members',(SELECT count(*) FROM laplace.physicality_occurrence_binding_member),
        'bindings',(SELECT count(*) FROM laplace.physicality_occurrence_binding),
        'deposition_links',(SELECT count(*) FROM laplace.physicality_entity_deposition),
        'deposit_receipts',(SELECT count(*) FROM laplace.canonical_deposit_receipt))
$counts$;

CREATE TABLE physicality_entity_contract.before AS
SELECT physicality_entity_contract.counts() AS value;

CREATE FUNCTION physicality_entity_contract.admit(
    selected_ordinal integer DEFAULT 1,
    maximum_requests bigint DEFAULT 4096,
    maximum_operands bigint DEFAULT 16384,
    maximum_carriers bigint DEFAULT 4096,
    maximum_logical_count numeric DEFAULT 1048576,
    maximum_database_operations bigint DEFAULT 128)
RETURNS TABLE(record_id bytea,view_id bytea,root_entity_id bytea,root_witness bytea,
    entity_candidate_count bigint,inserted_entity_count bigint,derived_node_count bigint,
    deposit_receipt_id bytea,batch_database_operations bigint)
LANGUAGE SQL VOLATILE AS $admit$
    SELECT admitted.*
    FROM physicality_entity_contract.context context
    CROSS JOIN physicality_entity_contract.sources source
    CROSS JOIN LATERAL laplace.physicality_entity_admit_batch(
        context.value,ARRAY[source.record_id],ARRAY[source.selected],
        maximum_requests,maximum_operands,maximum_carriers,maximum_logical_count,
        65536::bigint,maximum_database_operations) admitted
    WHERE source.ordinal=selected_ordinal
$admit$;

CREATE TABLE physicality_entity_contract.first AS
SELECT * FROM physicality_entity_contract.admit();
CREATE TABLE physicality_entity_contract.after_first AS
SELECT physicality_entity_contract.counts() AS value;
CREATE TABLE physicality_entity_contract.replay AS
SELECT * FROM physicality_entity_contract.admit();

DO $admission$
DECLARE first physicality_entity_contract.first%ROWTYPE;
    replay physicality_entity_contract.replay%ROWTYPE;
    before jsonb; after_first jsonb; owner laplace.physicality_entity_view%ROWTYPE;
BEGIN
    SELECT * INTO STRICT first FROM physicality_entity_contract.first;
    SELECT * INTO STRICT replay FROM physicality_entity_contract.replay;
    SELECT value INTO STRICT before FROM physicality_entity_contract.before;
    SELECT value INTO STRICT after_first FROM physicality_entity_contract.after_first;
    SELECT * INTO STRICT owner FROM laplace.physicality_entity_view WHERE view_id=first.view_id;
    IF first.root_entity_id=(SELECT entity_id FROM physicality_entity_contract.sources WHERE ordinal=1)
       OR first.record_id IS DISTINCT FROM owner.source_physicality_id
       OR first.root_entity_id IS DISTINCT FROM owner.root_entity_id
       OR first.root_witness IS DISTINCT FROM owner.root_witness
       OR first.entity_candidate_count<=0 OR first.derived_node_count<=0
       OR first.inserted_entity_count<=0
       OR replay.record_id IS DISTINCT FROM first.record_id
       OR replay.view_id IS DISTINCT FROM first.view_id
       OR replay.root_entity_id IS DISTINCT FROM first.root_entity_id
       OR replay.root_witness IS DISTINCT FROM first.root_witness
       OR replay.entity_candidate_count<>first.entity_candidate_count
       OR replay.derived_node_count<>first.derived_node_count
       OR replay.inserted_entity_count<>0
       OR first.batch_database_operations<=0 OR replay.batch_database_operations<=0
       OR first.deposit_receipt_id IS DISTINCT FROM replay.deposit_receipt_id
       OR (after_first->>'binding_sets')::bigint<>(before->>'binding_sets')::bigint+1
       OR before->'bindings' IS DISTINCT FROM after_first->'bindings'
       OR before->'binding_parents' IS DISTINCT FROM after_first->'binding_parents'
       OR before->'binding_members' IS DISTINCT FROM after_first->'binding_members'
       OR (after_first->>'deposition_links')::bigint<>(before->>'deposition_links')::bigint+1
       OR (after_first->>'deposit_receipts')::bigint<>(before->>'deposit_receipts')::bigint+1
       OR NOT EXISTS (
            SELECT 1 FROM laplace.physicality_entity_deposition link
            JOIN laplace.canonical_deposit_receipt receipt ON receipt.receipt_id=link.deposit_receipt_id
            WHERE link.view_id=first.view_id AND receipt.receipt_id=first.deposit_receipt_id
                AND receipt.entity_count=first.entity_candidate_count
                AND receipt.physicality_count=0 AND receipt.trajectory_vertex_count=0
                AND receipt.occurrence_count=0 AND receipt.logical_occurrence_count=0
                AND receipt.total_records=receipt.entity_count
                AND receipt.total_bytes>0 AND receipt.batch_count>0 AND receipt.status=0)
       OR physicality_entity_contract.counts() IS DISTINCT FROM after_first
       OR before->'physicalities' IS DISTINCT FROM after_first->'physicalities'
       OR before->'attestations' IS DISTINCT FROM after_first->'attestations'
       OR (after_first->>'views')::bigint<>(before->>'views')::bigint+1
       OR (after_first->>'nodes')::bigint<>(before->>'nodes')::bigint+first.derived_node_count
       OR (after_first->>'entities')::bigint<>(before->>'entities')::bigint+first.inserted_entity_count
       OR record_send(owner.admission_context) IS DISTINCT FROM
            (SELECT record_send(value) FROM physicality_entity_contract.context)
       OR record_send(owner.external_known[1]) IS DISTINCT FROM
            (SELECT record_send(selected) FROM physicality_entity_contract.sources WHERE ordinal=1)
       OR cardinality(owner.external_known)<>1
       OR EXISTS (SELECT 1 FROM laplace.physicality p WHERE p.entity_id=first.root_entity_id)
    THEN RAISE EXCEPTION 'native descriptor admission/replay changed identity, inputs, or canonical counts';
    END IF;
END $admission$;

DO $read_only_context$
DECLARE context laplace.execution_context; rejected boolean:=false;
    before jsonb:=physicality_entity_contract.counts();
BEGIN
    SELECT (value).* INTO STRICT context FROM physicality_entity_contract.context;
    context.flags:=context.flags | @LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY@;
    BEGIN
        PERFORM laplace.physicality_entity_admit_batch(context,
            ARRAY[source.record_id],ARRAY[source.selected],4096::bigint,16384::bigint,
            4096::bigint,1048576::numeric,65536::bigint,128::bigint)
        FROM physicality_entity_contract.sources source WHERE ordinal=2;
    EXCEPTION WHEN insufficient_privilege THEN rejected:=true;
    END;
    IF NOT rejected OR physicality_entity_contract.counts() IS DISTINCT FROM before THEN
        RAISE EXCEPTION 'read-only framework context admitted or retained descriptor state';
    END IF;
END $read_only_context$;

CREATE FUNCTION physicality_entity_contract.materialize(selected_entity bytea)
RETURNS laplace.content_materialization_result
LANGUAGE SQL VOLATILE AS $materialize$
    SELECT laplace.content_materialize_utf8(
        context.value,selected_entity,first.view_id,owner.descriptor_recipe,
        ROW(16384::numeric,65536::numeric,4194304::numeric,64,1)
            ::laplace.cognition_materialization_request)
    FROM physicality_entity_contract.context context
    CROSS JOIN physicality_entity_contract.first first
    JOIN laplace.physicality_entity_view owner ON owner.view_id=first.view_id
$materialize$;

CREATE TABLE physicality_entity_contract.warm AS
SELECT first.root_entity_id AS entity_id,
    physicality_entity_contract.materialize(first.root_entity_id) AS result
FROM physicality_entity_contract.first first
UNION ALL
SELECT internal.entity_id,physicality_entity_contract.materialize(internal.entity_id)
FROM physicality_entity_contract.first first
CROSS JOIN LATERAL (SELECT node.entity_id FROM laplace.physicality_entity_node node
    WHERE node.view_id=first.view_id AND node.entity_id<>first.root_entity_id
    ORDER BY node.result_index LIMIT 1) internal;

DO $warm$
BEGIN
    IF (SELECT count(*) FROM physicality_entity_contract.warm)<>2
       OR EXISTS (SELECT 1 FROM physicality_entity_contract.warm
           WHERE (result).status<>0 OR (result).root_content_id IS DISTINCT FROM entity_id
               OR (result).output_bytes<>octet_length((result).output)
               OR (result).output_bytes<=0 OR (result).resolved_node_count<=0
               OR (result).source_receipt_id IS DISTINCT FROM
                    (SELECT view_id FROM physicality_entity_contract.first)
               OR (result).source_recipe_id IS DISTINCT FROM
                    (SELECT owner.descriptor_recipe FROM laplace.physicality_entity_view owner
                     JOIN physicality_entity_contract.first first USING(view_id)))
       OR NOT EXISTS (SELECT 1 FROM physicality_entity_contract.warm warm
           JOIN physicality_entity_contract.first first ON first.root_entity_id=warm.entity_id
           WHERE position(convert_to('PhysicalityRecord','UTF8') IN (warm.result).output)>0)
       OR physicality_entity_contract.counts() IS DISTINCT FROM
            (SELECT value FROM physicality_entity_contract.after_first)
    THEN RAISE EXCEPTION 'ordinary native root/internal materialization failed or wrote canonical state';
    END IF;
END $warm$;

CREATE FUNCTION physicality_entity_contract.materialize_atom(original_selection boolean)
RETURNS laplace.content_materialization_result LANGUAGE SQL VOLATILE AS $materialize_atom$
    SELECT CASE WHEN original_selection THEN laplace.content_materialize_utf8(
        context.value,(source.selected).entity_id,first.view_id,owner.descriptor_recipe,
        ROW(16384::numeric,65536::numeric,4194304::numeric,64,1)::laplace.cognition_materialization_request,
        source.record_id)
        ELSE physicality_entity_contract.materialize((source.selected).entity_id) END
    FROM physicality_entity_contract.context context
    CROSS JOIN physicality_entity_contract.first first
    JOIN laplace.physicality_entity_view owner USING(view_id)
    CROSS JOIN physicality_entity_contract.sources source WHERE source.ordinal=1
$materialize_atom$;
CREATE TABLE physicality_entity_contract.atom_warm AS
SELECT original_selection,physicality_entity_contract.materialize_atom(original_selection) AS result
FROM unnest(ARRAY[false,true]) original_selection;
DO $atom_warm$
BEGIN
    IF (SELECT count(*) FROM physicality_entity_contract.atom_warm) IS DISTINCT FROM 2::bigint
       OR EXISTS(SELECT 1 FROM physicality_entity_contract.atom_warm warm
           WHERE (warm.result).status IS DISTINCT FROM 0
               OR (warm.result).output IS DISTINCT FROM convert_to('A','UTF8')
               OR (warm.result).resolved_node_count IS DISTINCT FROM 1::numeric
               OR (warm.result).trajectory_carrier_count IS DISTINCT FROM 0::numeric
               OR (warm.result).codepoint_count IS DISTINCT FROM 1::numeric
               OR physicality_entity_contract.materialize_atom(warm.original_selection) IS DISTINCT FROM warm.result)
       OR (SELECT (result).materialization_id FROM physicality_entity_contract.atom_warm WHERE original_selection)
          IS NOT DISTINCT FROM
          (SELECT (result).materialization_id FROM physicality_entity_contract.atom_warm WHERE NOT original_selection)
       OR (SELECT (result).readset_fingerprint FROM physicality_entity_contract.atom_warm WHERE original_selection)
          IS NOT DISTINCT FROM
          (SELECT (result).readset_fingerprint FROM physicality_entity_contract.atom_warm WHERE NOT original_selection)
       OR physicality_entity_contract.counts() IS DISTINCT FROM (SELECT value FROM physicality_entity_contract.after_first)
    THEN RAISE EXCEPTION 'canonical or original atom root lost its exact pinned source or wrote state'; END IF;
END $atom_warm$;

DO $content_binding$
DECLARE context laplace.execution_context; first physicality_entity_contract.first%ROWTYPE;
    recipe bytea; request laplace.cognition_materialization_request;
    actual laplace.content_materialization_result; variant integer; rejected boolean;
BEGIN
    SELECT (value).* INTO STRICT context FROM physicality_entity_contract.context;
    SELECT * INTO STRICT first FROM physicality_entity_contract.first;
    SELECT descriptor_recipe INTO STRICT recipe FROM laplace.physicality_entity_view WHERE view_id=first.view_id;
    rejected:=false;
    BEGIN
        PERFORM laplace.content_materialize_utf8(context,first.root_entity_id,first.view_id,
            set_byte(recipe,0,get_byte(recipe,0)#1),
            ROW(16384::numeric,65536::numeric,4194304::numeric,64,1)
                ::laplace.cognition_materialization_request);
    EXCEPTION WHEN data_corrupted THEN rejected:=true;
    END;
    IF NOT rejected THEN RAISE EXCEPTION 'generic content source accepted an unrelated recipe'; END IF;
    rejected:=false;
    BEGIN
        actual:=laplace.content_materialize_utf8(context,first.root_entity_id,
            set_byte(first.view_id,0,get_byte(first.view_id,0)#1),recipe,
            ROW(16384::numeric,65536::numeric,4194304::numeric,64,1)
                ::laplace.cognition_materialization_request);
        rejected:=actual.status<>0 AND actual.source_receipt_id IS NULL AND actual.output IS NULL;
    EXCEPTION WHEN data_corrupted OR no_data_found THEN rejected:=true;
    END;
    IF NOT rejected THEN RAISE EXCEPTION 'reflected content accepted an unrelated valid-width view owner'; END IF;
    FOR variant IN 1..4 LOOP
        request:=ROW(16384::numeric,65536::numeric,4194304::numeric,64,1)
            ::laplace.cognition_materialization_request;
        IF variant=3 THEN request.maximum_depth:=-1; END IF;
        IF variant=4 THEN request.maximum_output_bytes:=2147483647; END IF;
        rejected:=false;
        BEGIN
            PERFORM laplace.content_materialize_utf8(context,
                CASE WHEN variant=1 THEN substring(first.root_entity_id FROM 1 FOR 15) ELSE first.root_entity_id END,
                CASE WHEN variant=2 THEN substring(first.view_id FROM 1 FOR 31) ELSE first.view_id END,
                recipe,request);
        EXCEPTION WHEN invalid_binary_representation OR numeric_value_out_of_range OR program_limit_exceeded THEN
            rejected:=true;
        END;
        IF NOT rejected THEN RAISE EXCEPTION 'generic content binding accepted invalid argument variant %',variant; END IF;
    END LOOP;
    request:=ROW(1::numeric,65536::numeric,4194304::numeric,64,1)
        ::laplace.cognition_materialization_request;
    actual:=laplace.content_materialize_utf8(context,first.root_entity_id,first.view_id,recipe,request);
    IF actual.status<>9 OR actual.output IS NOT NULL OR actual.materialization_id IS NOT NULL
       OR actual.source_receipt_id IS NOT NULL OR actual.output_fingerprint IS NOT NULL THEN
        RAISE EXCEPTION 'native content read limit published partial bytes or a successful source receipt';
    END IF;
    IF physicality_entity_contract.counts() IS DISTINCT FROM
        (SELECT value FROM physicality_entity_contract.after_first) THEN
        RAISE EXCEPTION 'generic content binding refusal wrote canonical state';
    END IF;
END $content_binding$;

CREATE FUNCTION physicality_entity_contract.query(
    selected_anchor bytea DEFAULT NULL,selected_goal bytea DEFAULT NULL)
RETURNS laplace.cognition_observation_persisted_result
LANGUAGE SQL VOLATILE AS $query$
    SELECT laplace.cognition_observation_execute_persisted(context.value,
        ROW(coalesce(selected_anchor,first.root_entity_id),coalesce(selected_goal,root.child_ids[1]),
            decode(repeat('31',32),'hex'),decode(repeat('32',32),'hex'),
            decode(repeat('33',32),'hex'),decode(repeat('34',32),'hex'),
            (context.value).epochs[@LAPLACE_PHYSICALITY_TEST_EVIDENCE_INDEX@],(context.value).authority_fingerprint,
            decode(repeat('35',32),'hex'),
            ROW(4::numeric,128::numeric,128::numeric,128::numeric,
                67108864::numeric,1024::numeric,256::numeric,8::numeric,4,1,4,128)
                ::laplace.cognition_observation_search_budget,
            ROW(4::numeric,8::numeric,8::numeric,128::numeric,4::numeric,
                1048576::numeric,1024::numeric,256::numeric,128,4)
                ::laplace.cognition_observation_forward_limits,
            2,1,9,1)::laplace.cognition_observation_request)
    FROM physicality_entity_contract.context context
    CROSS JOIN physicality_entity_contract.first first
    JOIN laplace.physicality_entity_node root
        ON root.view_id=first.view_id AND root.entity_id=first.root_entity_id
$query$;

CREATE TABLE physicality_entity_contract.search AS
SELECT physicality_entity_contract.query() AS result;
DO $derived_only$
DECLARE result laplace.cognition_observation_persisted_result; goal bytea;
BEGIN
    SELECT (search.result).* INTO STRICT result FROM physicality_entity_contract.search search;
    SELECT node.child_ids[1] INTO STRICT goal FROM laplace.physicality_entity_node node
    JOIN physicality_entity_contract.first first
        ON node.view_id=first.view_id AND node.entity_id=first.root_entity_id;
    IF result.provider_rows_fetched<=0 OR result.provider_trajectory_bytes<=0
       OR result.provider_batch_count<=0 OR (result.execution).database_operations<=0
       OR (result.execution).final_remaining_required_count<>0
       OR cardinality(result.answers)<>1 OR (result.answers[1]).entity_id IS DISTINCT FROM goal
       OR (result.answers[1]).transition_count<>1
       OR physicality_entity_contract.counts() IS DISTINCT FROM
            (SELECT value FROM physicality_entity_contract.after_first)
    THEN RAISE EXCEPTION 'derived-only descriptor candidate did not enter the ordinary persisted provider: %',result;
    END IF;
END $derived_only$;

-- A complete successful admission is enclosed by a caller-owned rollback.
DO $rollback$
DECLARE before jsonb:=physicality_entity_contract.counts();
    returned_count bigint; replay_count bigint; novel_count bigint;
    shared record; observed laplace.cognition_observation_persisted_result;
BEGIN
    BEGIN
        -- One native batch contains both a prior exact owner and a new source.
        WITH admitted AS MATERIALIZED (
            SELECT admitted.* FROM physicality_entity_contract.context context
            CROSS JOIN LATERAL laplace.physicality_entity_admit_batch(context.value,
                (SELECT array_agg(record_id ORDER BY ordinal) FROM physicality_entity_contract.sources),
                (SELECT array_agg(selected ORDER BY ordinal) FROM physicality_entity_contract.sources),
                4096::bigint,16384::bigint,4096::bigint,1048576::numeric,
                65536::bigint,128::bigint) admitted)
        SELECT count(*),
            count(*) FILTER (WHERE admitted.record_id=first.record_id
                AND admitted.view_id=first.view_id
                AND admitted.root_entity_id=first.root_entity_id
                AND admitted.root_witness=first.root_witness
                AND admitted.inserted_entity_count=0),
            count(*) FILTER (WHERE admitted.record_id=source.record_id
                AND admitted.root_entity_id<>source.entity_id
                AND admitted.root_entity_id<>first.root_entity_id
                AND admitted.inserted_entity_count>0 AND admitted.derived_node_count>0)
        INTO returned_count,replay_count,novel_count
        FROM admitted CROSS JOIN physicality_entity_contract.first first
        CROSS JOIN physicality_entity_contract.sources source WHERE source.ordinal=2;
        IF returned_count<>2 OR replay_count<>1 OR novel_count<>1 THEN
            RAISE EXCEPTION 'mixed exact replay/new descriptor batch changed cardinality or identity';
        END IF;
        SELECT a.entity_id,a.child_ids[1] AS child INTO STRICT shared
        FROM laplace.physicality_entity_node a
        JOIN physicality_entity_contract.first first ON first.view_id=a.view_id
        JOIN physicality_entity_contract.warm warm ON warm.entity_id=a.entity_id
        WHERE a.entity_id<>first.root_entity_id AND EXISTS (
            SELECT 1 FROM laplace.physicality_entity_node b
            WHERE b.view_id<>a.view_id AND b.entity_id=a.entity_id
                AND b.physicality_id=a.physicality_id)
        ORDER BY a.result_index LIMIT 1;
        observed:=physicality_entity_contract.query(shared.entity_id,shared.child);
        IF observed.provider_rows_fetched<1 OR cardinality(observed.answers)<>1
           OR (observed.answers[1]).entity_id IS DISTINCT FROM shared.child
           OR (observed.answers[1]).transition_count<>1
           OR (observed.execution).final_remaining_required_count<>0 THEN
            RAISE EXCEPTION 'ordinary provider did not resolve an exact candidate shared by distinct owners';
        END IF;
        IF EXISTS (SELECT 1 FROM physicality_entity_contract.warm warm
            WHERE (physicality_entity_contract.materialize(warm.entity_id)).output
                IS DISTINCT FROM (warm.result).output) THEN
            RAISE EXCEPTION 'shared descriptor ownership changed ordinary exact materialization';
        END IF;
        RAISE EXCEPTION USING ERRCODE='P0002',MESSAGE='intentional caller rollback';
    EXCEPTION WHEN no_data_found THEN
        IF SQLERRM<>'intentional caller rollback' THEN RAISE; END IF;
    END;
    IF physicality_entity_contract.counts() IS DISTINCT FROM before THEN
        RAISE EXCEPTION 'caller rollback retained descriptor state';
    END IF;
END $rollback$;

-- Reconstruct in a new PostgreSQL backend, without a process-local plan/cache.
SELECT pg_backend_pid() AS physicality_entity_warm_pid \gset
SELECT current_setting('statement_timeout') AS physicality_entity_statement_timeout \gset
\connect :DBNAME
SELECT set_config('statement_timeout', :'physicality_entity_statement_timeout', false);
SELECT pg_backend_pid() <> :physicality_entity_warm_pid AS physicality_entity_new_backend \gset
\if :physicality_entity_new_backend
\else
    \echo 'descriptor cold read reused the original PostgreSQL backend'
    \quit 1
\endif

DO $cold$
DECLARE expected record; actual laplace.content_materialization_result;
BEGIN
    FOR expected IN SELECT * FROM physicality_entity_contract.warm LOOP
        actual:=physicality_entity_contract.materialize(expected.entity_id);
        IF actual.output IS DISTINCT FROM (expected.result).output
           OR actual.output_fingerprint IS DISTINCT FROM (expected.result).output_fingerprint
           OR actual.root_content_id IS DISTINCT FROM expected.entity_id
           OR actual.source_receipt_id IS DISTINCT FROM (expected.result).source_receipt_id
           OR actual.source_recipe_id IS DISTINCT FROM (expected.result).source_recipe_id
           OR actual.status<>0 OR actual.output_bytes<>(expected.result).output_bytes THEN
            RAISE EXCEPTION 'cold native descriptor readback changed the exact output or identity';
        END IF;
    END LOOP;
    FOR expected IN SELECT * FROM physicality_entity_contract.atom_warm LOOP
        actual:=physicality_entity_contract.materialize_atom(expected.original_selection);
        IF actual IS DISTINCT FROM expected.result THEN
            RAISE EXCEPTION 'cold canonical or original atom read changed exact content or native receipt';
        END IF;
    END LOOP;
    PERFORM physicality_entity_contract.query();
    IF physicality_entity_contract.counts() IS DISTINCT FROM
        (SELECT value FROM physicality_entity_contract.after_first) THEN
        RAISE EXCEPTION 'cold descriptor reads deposited or reflected new state';
    END IF;
END $cold$;

DO $limits$
DECLARE rejected boolean; before jsonb:=physicality_entity_contract.counts(); variant integer;
BEGIN
    FOR variant IN 1..5 LOOP
        rejected:=false;
        BEGIN
            PERFORM physicality_entity_contract.admit(1,
                CASE WHEN variant=1 THEN 1 ELSE 4096 END,
                CASE WHEN variant=2 THEN 1 ELSE 16384 END,4096,
                CASE WHEN variant=3 THEN 1 ELSE 1048576 END,
                CASE WHEN variant=4 THEN 1 WHEN variant=5 THEN 0 ELSE 128 END);
        EXCEPTION WHEN program_limit_exceeded OR invalid_parameter_value OR numeric_value_out_of_range THEN
            rejected:=true;
        END;
        IF NOT rejected THEN RAISE EXCEPTION 'descriptor resource envelope variant % was accepted',variant; END IF;
        IF physicality_entity_contract.counts() IS DISTINCT FROM before THEN
            RAISE EXCEPTION 'refused descriptor capacity retained partial state';
        END IF;
    END LOOP;
END $limits$;

-- The grant is checked before each real statement/plan preparation. Measure a
-- warm exact replay, then remove one operation and require atomic refusal.
DO $operation_boundary$
DECLARE actual record; rejected boolean:=false;
    before jsonb:=physicality_entity_contract.counts();
BEGIN
    SELECT * INTO STRICT actual FROM physicality_entity_contract.admit();
    IF actual.batch_database_operations<=1 THEN
        RAISE EXCEPTION 'descriptor operation accounting omitted its real database work';
    END IF;
    BEGIN
        PERFORM physicality_entity_contract.admit(1,4096,16384,4096,1048576,
            actual.batch_database_operations-1);
    EXCEPTION WHEN program_limit_exceeded THEN rejected:=true;
    END;
    IF NOT rejected OR physicality_entity_contract.counts() IS DISTINCT FROM before THEN
        RAISE EXCEPTION 'descriptor operation boundary did not refuse atomically';
    END IF;
END $operation_boundary$;

DO $entity_witness$
DECLARE rejected boolean:=false; before jsonb:=physicality_entity_contract.counts();
BEGIN
    BEGIN
        UPDATE laplace.entity SET identity_witness=set_byte(identity_witness,31,get_byte(identity_witness,31)#1)
        WHERE entity_id=(SELECT root_entity_id FROM physicality_entity_contract.first);
        PERFORM physicality_entity_contract.admit();
    EXCEPTION WHEN data_corrupted THEN rejected:=true;
    END;
    IF NOT rejected OR physicality_entity_contract.counts() IS DISTINCT FROM before THEN
        RAISE EXCEPTION 'descriptor entity producer accepted a conflicting full witness';
    END IF;
END $entity_witness$;

DO $corruption$
DECLARE variant integer; rejected boolean; before jsonb:=physicality_entity_contract.counts();
    selected_view bytea; source laplace.composition_known_entity_record;
    changed_context laplace.execution_context;
BEGIN
    SELECT view_id INTO STRICT selected_view FROM physicality_entity_contract.first;
    FOR variant IN 1..10 LOOP
        rejected:=false;
        BEGIN
            IF variant=1 THEN
                UPDATE laplace.physicality_entity_view SET root_witness=set_byte(root_witness,31,get_byte(root_witness,31)#1)
                WHERE view_id=selected_view;
            ELSIF variant=2 THEN
                UPDATE laplace.physicality_entity_view SET native_input_fingerprint=set_byte(native_input_fingerprint,0,get_byte(native_input_fingerprint,0)#1)
                WHERE view_id=selected_view;
            ELSIF variant=3 THEN
                SELECT (external_known[1]).* INTO STRICT source FROM laplace.physicality_entity_view WHERE view_id=selected_view;
                source.identity_witness:=set_byte(source.identity_witness,31,get_byte(source.identity_witness,31)#1);
                UPDATE laplace.physicality_entity_view SET external_known=ARRAY[source] WHERE view_id=selected_view;
            ELSIF variant=4 THEN
                SELECT (admission_context).* INTO STRICT changed_context FROM laplace.physicality_entity_view WHERE view_id=selected_view;
                changed_context.epochs[@LAPLACE_PHYSICALITY_TEST_GEOMETRY_INDEX@]:=set_byte(
                    changed_context.epochs[@LAPLACE_PHYSICALITY_TEST_GEOMETRY_INDEX@],0,
                    get_byte(changed_context.epochs[@LAPLACE_PHYSICALITY_TEST_GEOMETRY_INDEX@],0)#1);
                UPDATE laplace.physicality_entity_view SET admission_context=changed_context WHERE view_id=selected_view;
            ELSIF variant=5 THEN
                UPDATE laplace.physicality_entity_node SET identity_witness=set_byte(identity_witness,31,get_byte(identity_witness,31)#1)
                WHERE view_id=selected_view;
            ELSIF variant=6 THEN
                UPDATE laplace.physicality_entity_node SET physicality_metadata.radius=0.375
                WHERE view_id=selected_view;
            ELSIF variant=7 THEN
                UPDATE laplace.physicality SET centroid_x=
                    CASE WHEN centroid_x=0 THEN 0.125 ELSE -centroid_x END
                WHERE physicality_id=(SELECT record_id FROM physicality_entity_contract.sources WHERE ordinal=1);
            ELSIF variant=8 THEN
                UPDATE laplace.physicality_entity_node SET child_ids=
                    ARRAY[(SELECT entity_id FROM physicality_entity_contract.sources WHERE ordinal=2)]
                WHERE view_id=selected_view;
            ELSIF variant=9 THEN
                UPDATE laplace.physicality_entity_view SET descriptor_recipe=set_byte(descriptor_recipe,0,get_byte(descriptor_recipe,0)#1)
                WHERE view_id=selected_view;
            ELSE
                UPDATE laplace.physicality_entity_view SET maximum_requests=maximum_requests+1
                WHERE view_id=selected_view;
            END IF;
            PERFORM physicality_entity_contract.materialize(root_entity_id)
            FROM physicality_entity_contract.first;
        EXCEPTION WHEN no_data_found THEN
            IF variant<>4 OR SQLERRM<>
                'Laplace materialization composition is absent in the pinned geometry epoch' THEN
                RAISE;
            END IF;
            rejected:=true;
        WHEN data_corrupted OR invalid_parameter_value OR data_exception THEN
            rejected:=true;
        END;
        IF NOT rejected THEN RAISE EXCEPTION 'descriptor corruption variant % was accepted',variant; END IF;
        IF physicality_entity_contract.counts() IS DISTINCT FROM before THEN
            RAISE EXCEPTION 'descriptor rejection escaped its caller rollback';
        END IF;
    END LOOP;
END $corruption$;


-- Build a genuine common-composer parent with two A occurrences stored as one
-- RLE carrier. The source physicality is not an invented descriptor fixture.
CREATE TABLE physicality_entity_contract.occurrence_composition AS
    SELECT laplace.composition_deposit_batch(context.value,
        decode(repeat('d8',32),'hex'),decode(repeat('d9',32),'hex'),
        ARRAY[source.selected],
        ARRAY[ROW(0::numeric,2::numeric,0::bigint,1,0)::laplace.composition_operand_record],
        ARRAY[ROW(0::numeric,1::numeric,1::numeric,1,0,decode(repeat('da',32),'hex'),
            (context.value).epochs[@LAPLACE_PHYSICALITY_TEST_GEOMETRY_INDEX@],decode(repeat('00',32),'hex'))
            ::laplace.composition_request_record],65536::numeric) AS result
    FROM physicality_entity_contract.context context
    CROSS JOIN physicality_entity_contract.sources source WHERE source.ordinal=1;
CREATE TABLE physicality_entity_contract.occurrence_parent AS
SELECT p.physicality_id::bytea AS record_id,p.entity_id::bytea AS entity_id,
    ROW(p.entity_id::bytea,e.identity_witness::bytea,p.physicality_id::bytea,
        p.centroid_x,p.centroid_y,p.centroid_z,p.centroid_m,0::bigint,
        (deposited.result).result_tier_floors[1],false)::laplace.composition_known_entity_record AS selected
FROM physicality_entity_contract.occurrence_composition deposited
JOIN laplace.physicality p ON p.physicality_id=(deposited.result).result_physicality_ids[1]
JOIN laplace.entity e ON e.entity_id=p.entity_id
WHERE p.logical_count=2 AND p.vertex_count=1;

DO $actual_rle$
BEGIN
    IF (SELECT count(*) FROM physicality_entity_contract.occurrence_parent)<>1 THEN
        RAISE EXCEPTION 'ordinary composition did not produce the complete two-occurrence RLE parent';
    END IF;
END $actual_rle$;

CREATE FUNCTION physicality_entity_contract.admit_occurrences(
    selections laplace.physicality_occurrence_selection[] DEFAULT ARRAY[]::laplace.physicality_occurrence_selection[])
RETURNS TABLE(record_id bytea,view_id bytea,root_entity_id bytea,root_witness bytea,
    entity_candidate_count bigint,inserted_entity_count bigint,derived_node_count bigint,
    deposit_receipt_id bytea,batch_database_operations bigint)
LANGUAGE SQL VOLATILE AS $admit_occurrences$
    SELECT admitted.*
    FROM physicality_entity_contract.context context
    CROSS JOIN physicality_entity_contract.occurrence_parent parent
    CROSS JOIN physicality_entity_contract.sources atom
    CROSS JOIN LATERAL laplace.physicality_entity_admit_batch(context.value,
        ARRAY[parent.record_id],ARRAY[parent.selected,atom.selected],
        4096::bigint,16384::bigint,4096::bigint,1048576::numeric,65536::bigint,128::bigint,selections) admitted
    WHERE atom.ordinal=1
$admit_occurrences$;

CREATE TABLE physicality_entity_contract.occurrence_first AS
SELECT * FROM physicality_entity_contract.admit_occurrences();
ALTER TABLE physicality_entity_contract.occurrence_first ADD COLUMN binding_set_id bytea;
UPDATE physicality_entity_contract.occurrence_first first SET binding_set_id=owner.binding_set_id
FROM laplace.physicality_entity_view owner WHERE owner.view_id=first.view_id;
CREATE TABLE physicality_entity_contract.occurrence_selections AS
SELECT ARRAY[
    ROW(binding.parent_physicality_id::bytea,binding.entity_id::bytea,binding.selected_physicality_id::bytea,
        1::numeric,1::numeric,binding.metadata,1)::laplace.physicality_occurrence_selection,
    ROW(binding.parent_physicality_id::bytea,binding.entity_id::bytea,binding.selected_physicality_id::bytea,
        2::numeric,1::numeric,binding.metadata,1)::laplace.physicality_occurrence_selection] AS value
FROM physicality_entity_contract.occurrence_first first
JOIN laplace.physicality_occurrence_binding_member member USING(binding_set_id)
JOIN laplace.physicality_occurrence_binding binding USING(parent_receipt_id,parent_physicality_id)
WHERE binding.first_logical_ordinal=1 AND binding.logical_count=2;
CREATE TABLE physicality_entity_contract.occurrence_split AS
SELECT admitted.* FROM physicality_entity_contract.occurrence_selections selections
CROSS JOIN LATERAL physicality_entity_contract.admit_occurrences(selections.value) admitted;
ALTER TABLE physicality_entity_contract.occurrence_split ADD COLUMN binding_set_id bytea;
UPDATE physicality_entity_contract.occurrence_split split SET binding_set_id=owner.binding_set_id
FROM laplace.physicality_entity_view owner WHERE owner.view_id=split.view_id;

DO $intervals$
DECLARE first record; split record; replay record;
    before jsonb:=physicality_entity_contract.counts();
BEGIN
    SELECT * INTO STRICT first FROM physicality_entity_contract.occurrence_first;
    SELECT * INTO STRICT split FROM physicality_entity_contract.occurrence_split;
    SELECT replayed.* INTO STRICT replay FROM physicality_entity_contract.occurrence_selections selections
    CROSS JOIN LATERAL physicality_entity_contract.admit_occurrences(selections.value) replayed;
    IF first.root_entity_id IS DISTINCT FROM split.root_entity_id
       OR first.root_witness IS DISTINCT FROM split.root_witness
       OR first.view_id=split.view_id OR first.binding_set_id=split.binding_set_id
       OR split.inserted_entity_count<>0 OR replay.inserted_entity_count<>0
       OR replay.view_id IS DISTINCT FROM split.view_id
       OR replay.deposit_receipt_id IS DISTINCT FROM split.deposit_receipt_id
       OR (SELECT count(*) FROM laplace.physicality_occurrence_binding_member member
           JOIN laplace.physicality_occurrence_binding USING(parent_receipt_id,parent_physicality_id)
           WHERE member.binding_set_id=first.binding_set_id)<>1
       OR (SELECT count(*) FROM laplace.physicality_occurrence_binding_member member
           JOIN laplace.physicality_occurrence_binding USING(parent_receipt_id,parent_physicality_id)
           WHERE member.binding_set_id=split.binding_set_id)<>2
       OR EXISTS (SELECT 1 FROM laplace.physicality_occurrence_binding_set
           WHERE binding_set_id IN (first.binding_set_id,split.binding_set_id)
             AND (parent_count<>1 OR logical_count<>2 OR cardinality(exact_sources)<>2))
       OR physicality_entity_contract.counts() IS DISTINCT FROM before THEN
        RAISE EXCEPTION 'exact interval split or replay changed canonical content or complete source ownership';
    END IF;
END $intervals$;

CREATE TABLE physicality_entity_contract.occurrence_warm AS
SELECT split.*,laplace.content_materialize_utf8(context.value,split.root_entity_id,split.view_id,owner.descriptor_recipe,
    ROW(16384::numeric,65536::numeric,4194304::numeric,64,1)::laplace.cognition_materialization_request) AS result
FROM physicality_entity_contract.occurrence_split split
JOIN laplace.physicality_entity_view owner USING(view_id)
CROSS JOIN physicality_entity_contract.context context;
SELECT pg_backend_pid() AS occurrence_warm_pid \gset
SELECT current_setting('statement_timeout') AS occurrence_statement_timeout \gset
\connect :DBNAME
SELECT set_config('statement_timeout', :'occurrence_statement_timeout', false);
SELECT pg_backend_pid() <> :occurrence_warm_pid AS occurrence_new_backend \gset
\if :occurrence_new_backend
\else
    \echo 'occurrence cold read reused the original PostgreSQL backend'
    \quit 1
\endif

DO $interval_cold_and_corruption$
DECLARE expected record; actual laplace.content_materialization_result; rejected boolean; variant integer;
    context laplace.execution_context; recipe bytea; before jsonb:=physicality_entity_contract.counts();
BEGIN
    SELECT * INTO STRICT expected FROM physicality_entity_contract.occurrence_warm;
    SELECT (value).* INTO STRICT context FROM physicality_entity_contract.context;
    SELECT descriptor_recipe INTO STRICT recipe FROM laplace.physicality_entity_view WHERE view_id=expected.view_id;
    actual:=laplace.content_materialize_utf8(context,expected.root_entity_id,expected.view_id,recipe,
        ROW(16384::numeric,65536::numeric,4194304::numeric,64,1)::laplace.cognition_materialization_request);
    IF actual.status<>0 OR actual.output IS DISTINCT FROM (expected.result).output
       OR actual.source_receipt_id IS DISTINCT FROM expected.view_id
       OR actual.readset_fingerprint IS DISTINCT FROM (expected.result).readset_fingerprint THEN
        RAISE EXCEPTION 'cold complete occurrence reconstruction changed exact content or source readset';
    END IF;
    FOR variant IN 1..4 LOOP
        rejected:=false;
        BEGIN
            IF variant=1 THEN
                UPDATE laplace.physicality_occurrence_binding SET metadata=metadata+1
                WHERE parent_receipt_id IN (SELECT parent_receipt_id FROM laplace.physicality_occurrence_binding_member
                    WHERE binding_set_id=expected.binding_set_id) AND first_logical_ordinal=2;
            ELSIF variant=2 THEN
                DELETE FROM laplace.physicality_occurrence_binding
                WHERE parent_receipt_id IN (SELECT parent_receipt_id FROM laplace.physicality_occurrence_binding_member
                    WHERE binding_set_id=expected.binding_set_id) AND first_logical_ordinal=2;
            ELSIF variant=3 THEN
                UPDATE laplace.physicality_occurrence_binding SET selected_physicality_id=
                    (SELECT record_id FROM physicality_entity_contract.sources WHERE ordinal=2)
                WHERE parent_receipt_id IN (SELECT parent_receipt_id FROM laplace.physicality_occurrence_binding_member
                    WHERE binding_set_id=expected.binding_set_id) AND first_logical_ordinal=2;
            ELSE
                UPDATE laplace.physicality_entity_view SET binding_set_id=
                    (SELECT binding_set_id FROM physicality_entity_contract.occurrence_first)
                WHERE view_id=expected.view_id;
            END IF;
            PERFORM laplace.content_materialize_utf8(context,expected.root_entity_id,expected.view_id,recipe,
                ROW(16384::numeric,65536::numeric,4194304::numeric,64,1)::laplace.cognition_materialization_request);
        EXCEPTION WHEN data_corrupted THEN rejected:=true;
        END;
        IF NOT rejected OR physicality_entity_contract.counts() IS DISTINCT FROM before THEN
            RAISE EXCEPTION 'occurrence corruption variant % escaped complete readback or caller rollback',variant;
        END IF;
    END LOOP;
END $interval_cold_and_corruption$;

-- An unrelated batch neighbor, including a new physicality of that neighbor,
-- must not alter the already admitted source's view or duplicate its intervals.
DO $scope_batch_parity$
DECLARE context laplace.execution_context; parent physicality_entity_contract.occurrence_parent%ROWTYPE;
    atom_a laplace.composition_known_entity_record; atom_b laplace.composition_known_entity_record;
    neighbor laplace.composition_known_entity_record; deposited laplace.composition_deposit_result;
    first physicality_entity_contract.occurrence_first%ROWTYPE; actual record; variant integer;
    before jsonb:=physicality_entity_contract.counts(); prior_parent_count bigint;
BEGIN
    SELECT (value).* INTO STRICT context FROM physicality_entity_contract.context;
    SELECT * INTO STRICT parent FROM physicality_entity_contract.occurrence_parent;
    SELECT * INTO STRICT first FROM physicality_entity_contract.occurrence_first;
    SELECT (selected).* INTO STRICT atom_a FROM physicality_entity_contract.sources WHERE ordinal=1;
    SELECT (selected).* INTO STRICT atom_b FROM physicality_entity_contract.sources WHERE ordinal=2;
    BEGIN
        FOR variant IN 1..2 LOOP
            deposited:=laplace.composition_deposit_batch(context,decode(repeat('db',32),'hex'),
                decode(repeat('dc',32),'hex'),ARRAY[atom_b],
                ARRAY[ROW(0::numeric,2::numeric,0::bigint,1,0)::laplace.composition_operand_record],
                ARRAY[ROW(0::numeric,1::numeric,variant::numeric,1,0,
                    set_byte(decode(repeat('dd',32),'hex'),0,variant),
                    context.epochs[@LAPLACE_PHYSICALITY_TEST_GEOMETRY_INDEX@],decode(repeat('00',32),'hex'))
                    ::laplace.composition_request_record],65536::numeric);
            SELECT p.entity_id::bytea,e.identity_witness::bytea,p.physicality_id::bytea,
                p.centroid_x,p.centroid_y,p.centroid_z,p.centroid_m,0::bigint,
                deposited.result_tier_floors[1],false
            INTO STRICT neighbor FROM laplace.physicality p JOIN laplace.entity e USING(entity_id)
            WHERE p.physicality_id=deposited.result_physicality_ids[1];
            SELECT count(*) INTO prior_parent_count FROM laplace.physicality_occurrence_binding binding
            JOIN laplace.physicality_occurrence_binding_member member USING(parent_receipt_id,parent_physicality_id)
            WHERE member.binding_set_id=first.binding_set_id;
            SELECT admitted.* INTO STRICT actual FROM laplace.physicality_entity_admit_batch(context,
                ARRAY[parent.record_id,neighbor.physicality_id],
                ARRAY[parent.selected,atom_a,neighbor,atom_b],
                4096::bigint,16384::bigint,4096::bigint,1048576::numeric,65536::bigint,128::bigint) admitted
            WHERE admitted.record_id=parent.record_id;
            IF actual.view_id IS DISTINCT FROM first.view_id
               OR actual.root_entity_id IS DISTINCT FROM first.root_entity_id
               OR actual.root_witness IS DISTINCT FROM first.root_witness
               OR actual.inserted_entity_count<>0
               OR (SELECT binding_set_id FROM laplace.physicality_entity_view WHERE view_id=actual.view_id)
                    IS DISTINCT FROM first.binding_set_id
               OR (SELECT count(*) FROM laplace.physicality_occurrence_binding binding
                   JOIN laplace.physicality_occurrence_binding_member member USING(parent_receipt_id,parent_physicality_id)
                   WHERE member.binding_set_id=first.binding_set_id)<>prior_parent_count THEN
                RAISE EXCEPTION 'unrelated batch neighbor variant % changed an exact source view or interval owner',variant;
            END IF;
        END LOOP;
        RAISE SQLSTATE 'ZPB01' USING MESSAGE='rollback scope batch parity fixture';
    EXCEPTION WHEN SQLSTATE 'ZPB01' THEN NULL;
    END;
    IF physicality_entity_contract.counts() IS DISTINCT FROM before THEN
        RAISE EXCEPTION 'scope batch parity fixture escaped its caller rollback';
    END IF;
END $scope_batch_parity$;

-- Real noncollapsed representations share canonical E. A separate BUILD_TESTING
-- native constructor emits a transparent singleton P[E] because the ordinary
-- composer intentionally collapses a singleton request to its selected child.
-- Every record is deposited through a public native canonical sink.
CREATE FUNCTION physicality_entity_contract.singleton_frames(
    laplace.physicality_record,bytea,bytea,bytea)
RETURNS bytea[] AS 'laplace_pg','laplace_pg_test_singleton_physicality_frames'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE TABLE physicality_entity_contract.form_composition AS
SELECT laplace.composition_deposit_batch(context.value,
    decode(repeat('e1',32),'hex'),decode(repeat('e2',32),'hex'),
    (SELECT array_agg(selected ORDER BY ordinal) FROM physicality_entity_contract.sources),
    ARRAY[ROW(0::numeric,1::numeric,0::bigint,1,0)::laplace.composition_operand_record,
          ROW(1::numeric,1::numeric,0::bigint,1,0)::laplace.composition_operand_record],
    ARRAY[ROW(0::numeric,2::numeric,1::numeric,1,0,decode(repeat('e3',32),'hex'),
        (context.value).epochs[@LAPLACE_PHYSICALITY_TEST_GEOMETRY_INDEX@],decode(repeat('00',32),'hex'))
        ::laplace.composition_request_record,
        ROW(0::numeric,2::numeric,2::numeric,1,0,decode(repeat('e4',32),'hex'),
        (context.value).epochs[@LAPLACE_PHYSICALITY_TEST_GEOMETRY_INDEX@],decode(repeat('00',32),'hex'))
        ::laplace.composition_request_record],65536::numeric) AS result
FROM physicality_entity_contract.context context;
CREATE TABLE physicality_entity_contract.forms AS
SELECT n AS ordinal,p.physicality_id::bytea AS record_id,p.entity_id::bytea AS entity_id,
    ROW(p.entity_id::bytea,e.identity_witness::bytea,p.physicality_id::bytea,
        p.centroid_x,p.centroid_y,p.centroid_z,p.centroid_m,0::bigint,
        (deposited.result).result_tier_floors[n],false)::laplace.composition_known_entity_record AS selected
FROM physicality_entity_contract.form_composition deposited
CROSS JOIN generate_series(1,2) n
JOIN laplace.physicality p ON p.physicality_id=(deposited.result).result_physicality_ids[n]
JOIN laplace.entity e ON e.entity_id=p.entity_id;
ALTER TABLE physicality_entity_contract.forms ADD PRIMARY KEY(ordinal);

DO $singleton_constructor_refusals$
DECLARE original laplace.physicality_record; supplied laplace.physicality_record;
    trajectory bytea; witness bytea; supplied_witness bytea; variant integer; rejected boolean;
    selected_input record;
    before jsonb:=physicality_entity_contract.counts();
BEGIN
    SELECT ROW(p.physicality_id,p.entity_id,p.physicality_type,p.vertex_class,p.recipe_version,
        p.structural_form,p.dimension_count,p.flags,p.recipe_fingerprint,p.geometry_epoch,
        p.trajectory_fingerprint,p.centroid_x,p.centroid_y,p.centroid_z,p.centroid_m,
        p.radius,p.logical_count,p.vertex_count)::laplace.physicality_record AS original,
        p.trajectory,e.identity_witness AS witness INTO STRICT selected_input
    FROM physicality_entity_contract.forms child
    JOIN laplace.physicality p ON p.physicality_id=child.record_id
    JOIN laplace.entity e ON e.entity_id=child.entity_id WHERE child.ordinal=1;
    original:=selected_input.original;
    trajectory:=selected_input.trajectory;
    witness:=selected_input.witness;
    FOR variant IN 1..3 LOOP
        supplied:=original; supplied_witness:=witness; rejected:=false;
        IF variant=1 THEN supplied.radius:=CASE WHEN original.radius=0 THEN 0.125 ELSE 0 END; END IF;
        IF variant=2 THEN supplied_witness:=set_byte(witness,31,get_byte(witness,31)#1); END IF;
        BEGIN
            PERFORM physicality_entity_contract.singleton_frames(supplied,
                CASE WHEN variant=3 THEN decode(repeat('00',4097),'hex') ELSE trajectory END,
                supplied_witness,decode(repeat('e5',32),'hex'));
        EXCEPTION WHEN data_corrupted THEN
            IF variant=3 OR SQLERRM<>(CASE WHEN variant=1
                THEN 'Laplace physicality fields differ from their native immutable identity'
                ELSE 'singleton fixture lacks an independently validated child body and witness' END) THEN RAISE; END IF;
            rejected:=true;
        WHEN program_limit_exceeded THEN
            IF variant<>3 OR SQLERRM<>'singleton fixture child trajectory exceeds its finite envelope before detoast' THEN RAISE; END IF;
            rejected:=true;
        END;
        IF NOT rejected OR physicality_entity_contract.counts() IS DISTINCT FROM before THEN
            RAISE EXCEPTION 'singleton native input refusal variant % accepted or wrote state',variant;
        END IF;
    END LOOP;
END $singleton_constructor_refusals$;

CREATE TABLE physicality_entity_contract.singleton_input AS
SELECT source.entity_id,physicality_entity_contract.singleton_frames(
    ROW(p.physicality_id,p.entity_id,p.physicality_type,p.vertex_class,p.recipe_version,
        p.structural_form,p.dimension_count,p.flags,p.recipe_fingerprint,p.geometry_epoch,
        p.trajectory_fingerprint,p.centroid_x,p.centroid_y,p.centroid_z,p.centroid_m,
        p.radius,p.logical_count,p.vertex_count)::laplace.physicality_record,
    p.trajectory,e.identity_witness,decode(repeat('e5',32),'hex')) AS frames
FROM physicality_entity_contract.forms source
JOIN laplace.physicality p ON p.physicality_id=source.record_id
JOIN laplace.entity e ON e.entity_id=source.entity_id WHERE source.ordinal=1;
CREATE TABLE physicality_entity_contract.singleton_deposit AS
SELECT laplace.canonical_deposit_batch(context.value,decode(repeat('e6',32),'hex'),
    decode(repeat('e5',32),'hex'),source.frames) AS result
FROM physicality_entity_contract.context context
CROSS JOIN physicality_entity_contract.singleton_input source;
INSERT INTO physicality_entity_contract.forms
SELECT 3,p.physicality_id::bytea,p.entity_id::bytea,
    ROW(p.entity_id::bytea,e.identity_witness::bytea,p.physicality_id::bytea,
        p.centroid_x,p.centroid_y,p.centroid_z,p.centroid_m,0::bigint,
        (child.selected).tier_floor,false)::laplace.composition_known_entity_record
FROM physicality_entity_contract.forms child
JOIN laplace.physicality p ON p.entity_id=child.entity_id
    AND p.recipe_fingerprint=decode(repeat('e5',32),'hex')
JOIN laplace.entity e ON e.entity_id=p.entity_id
WHERE child.ordinal=1 AND p.logical_count=1 AND p.vertex_count=1;

CREATE TABLE physicality_entity_contract.distinct_parent_composition AS
SELECT laplace.composition_deposit_batch(context.value,
    decode(repeat('e7',32),'hex'),decode(repeat('e8',32),'hex'),
    (SELECT array_agg(selected ORDER BY ordinal) FROM physicality_entity_contract.forms WHERE ordinal IN(1,3)),
    ARRAY[ROW(0::numeric,1::numeric,0::bigint,1,0)::laplace.composition_operand_record,
          ROW(1::numeric,1::numeric,0::bigint,1,0)::laplace.composition_operand_record],
    ARRAY[ROW(0::numeric,2::numeric,1::numeric,1,0,decode(repeat('e9',32),'hex'),
        (context.value).epochs[@LAPLACE_PHYSICALITY_TEST_GEOMETRY_INDEX@],decode(repeat('00',32),'hex'))
        ::laplace.composition_request_record],65536::numeric) AS result
FROM physicality_entity_contract.context context;
INSERT INTO physicality_entity_contract.forms
SELECT 4,p.physicality_id::bytea,p.entity_id::bytea,
    ROW(p.entity_id::bytea,e.identity_witness::bytea,p.physicality_id::bytea,
        p.centroid_x,p.centroid_y,p.centroid_z,p.centroid_m,0::bigint,
        (deposited.result).result_tier_floors[1],false)::laplace.composition_known_entity_record
FROM physicality_entity_contract.distinct_parent_composition deposited
JOIN laplace.physicality p ON p.physicality_id=(deposited.result).result_physicality_ids[1]
JOIN laplace.entity e ON e.entity_id=p.entity_id;

-- Decode actual stored native carriers, rather than implementing metadata packing
-- in SQL. Only the repeated parent splits its single stored run into two exact
-- historical selections: first P0, then the transparent singleton P[E].
CREATE TABLE physicality_entity_contract.form_occurrences AS
SELECT form.ordinal AS form_ordinal,form.record_id,occurrence.*
FROM physicality_entity_contract.forms form
JOIN laplace.physicality p ON p.physicality_id=form.record_id
CROSS JOIN physicality_entity_contract.context context
CROSS JOIN LATERAL laplace.trajectory_composition_decode_calculate_batch(context.value,
    ARRAY(SELECT substring(p.trajectory FROM (n*32+1)::integer FOR 32)
          FROM generate_series(0,p.vertex_count::integer-1) n ORDER BY n)) decoded
CROSS JOIN LATERAL unnest(decoded.occurrences) occurrence;
CREATE TABLE physicality_entity_contract.form_selections AS
SELECT array_agg(value ORDER BY parent_id,first_ordinal) AS value
FROM (
    SELECT occurrence.record_id AS parent_id,occurrence.logical_ordinal AS first_ordinal,
        ROW(occurrence.record_id,occurrence.entity_id,
            CASE WHEN occurrence.form_ordinal=3 THEN (SELECT record_id FROM physicality_entity_contract.forms WHERE ordinal=1)
                 ELSE (SELECT record_id FROM physicality_entity_contract.sources atom WHERE atom.entity_id=occurrence.entity_id) END,
            occurrence.logical_ordinal,occurrence.run_length::numeric,occurrence.metadata::numeric,1)
            ::laplace.physicality_occurrence_selection AS value
    FROM physicality_entity_contract.form_occurrences occurrence WHERE occurrence.form_ordinal<>4
    UNION ALL
    SELECT occurrence.record_id,n::numeric,
        ROW(occurrence.record_id,occurrence.entity_id,selected.record_id,n::numeric,1::numeric,
            occurrence.metadata::numeric,1)::laplace.physicality_occurrence_selection
    FROM physicality_entity_contract.form_occurrences occurrence
    CROSS JOIN generate_series(1,2) n
    JOIN physicality_entity_contract.forms selected ON selected.ordinal=CASE WHEN n=1 THEN 1 ELSE 3 END
    WHERE occurrence.form_ordinal=4
) selection;

DO $same_entity_shapes$
DECLARE deposited laplace.canonical_deposit_result;
BEGIN
    SELECT (result).* INTO STRICT deposited FROM physicality_entity_contract.singleton_deposit;
    IF (SELECT count(*) FROM physicality_entity_contract.forms) IS DISTINCT FROM 4::bigint
       OR (SELECT count(DISTINCT entity_id) FROM physicality_entity_contract.forms WHERE ordinal<=3) IS DISTINCT FROM 1::bigint
       OR (SELECT count(DISTINCT record_id) FROM physicality_entity_contract.forms) IS DISTINCT FROM 4::bigint
       OR (SELECT array_agg((selected).tier_floor ORDER BY ordinal) FROM physicality_entity_contract.forms)
            IS DISTINCT FROM ARRAY[1,1,1,2]::smallint[]
       OR deposited.status IS DISTINCT FROM 0
       OR deposited.physicality_inserted IS DISTINCT FROM 1::numeric
       OR deposited.trajectory_vertex_inserted IS DISTINCT FROM 1::numeric
       OR deposited.entity_inserted IS DISTINCT FROM 0::numeric
       OR NOT EXISTS(SELECT 1 FROM physicality_entity_contract.form_occurrences
            WHERE form_ordinal=3 AND run_length=1 AND tier=1 AND NOT has_atom
                AND entity_id=(SELECT entity_id FROM physicality_entity_contract.forms WHERE ordinal=1))
       OR (SELECT count(*) FROM physicality_entity_contract.form_occurrences WHERE form_ordinal=4) IS DISTINCT FROM 1::bigint
       OR NOT EXISTS(SELECT 1 FROM physicality_entity_contract.form_occurrences
            WHERE form_ordinal=4 AND run_length=2 AND logical_ordinal=1 AND tier=1 AND NOT has_atom)
       OR (SELECT cardinality(value) FROM physicality_entity_contract.form_selections) IS DISTINCT FROM 7
    THEN RAISE EXCEPTION 'native same-E/singleton/RLE fixture did not establish its exact physical inputs';
    END IF;
END $same_entity_shapes$;

CREATE FUNCTION physicality_entity_contract.admit_forms(
    selected_ordinals integer[] DEFAULT ARRAY[1,2,3,4],
    omit_selections boolean DEFAULT false,reverse_parent boolean DEFAULT false)
RETURNS TABLE(record_id bytea,view_id bytea,root_entity_id bytea,root_witness bytea,
    entity_candidate_count bigint,inserted_entity_count bigint,derived_node_count bigint,
    deposit_receipt_id bytea,batch_database_operations bigint)
LANGUAGE SQL VOLATILE AS $admit_forms$
    SELECT admitted.* FROM physicality_entity_contract.context context
    CROSS JOIN physicality_entity_contract.form_selections choices
    CROSS JOIN LATERAL laplace.physicality_entity_admit_batch(context.value,
        (SELECT array_agg(record_id ORDER BY ordinal) FROM physicality_entity_contract.forms WHERE ordinal=ANY(selected_ordinals)),
        ARRAY(SELECT selected FROM physicality_entity_contract.sources ORDER BY ordinal)||
            ARRAY(SELECT selected FROM physicality_entity_contract.forms ORDER BY ordinal),
        16384::bigint,65536::bigint,16384::bigint,4194304::numeric,65536::bigint,512::bigint,
        CASE WHEN omit_selections THEN ARRAY[]::laplace.physicality_occurrence_selection[]
        WHEN reverse_parent THEN ARRAY(SELECT ROW(choice.parent_physicality_id,choice.entity_id,
            CASE WHEN choice.parent_physicality_id=(SELECT record_id FROM physicality_entity_contract.forms WHERE ordinal=4)
                 THEN (SELECT record_id FROM physicality_entity_contract.forms
                       WHERE ordinal=CASE WHEN choice.first_logical_ordinal=1 THEN 3 ELSE 1 END)
                 ELSE choice.selected_physicality_id END,
            choice.first_logical_ordinal,choice.logical_count,choice.metadata,choice.version)
                ::laplace.physicality_occurrence_selection
            FROM unnest(choices.value) choice)
        ELSE choices.value END) admitted
$admit_forms$;
CREATE TABLE physicality_entity_contract.form_before AS SELECT physicality_entity_contract.counts() AS value;
CREATE TABLE physicality_entity_contract.form_admitted AS SELECT * FROM physicality_entity_contract.admit_forms();
CREATE TABLE physicality_entity_contract.form_after AS SELECT physicality_entity_contract.counts() AS value;

CREATE FUNCTION physicality_entity_contract.materialize_form(selected_ordinal integer,canonical_content boolean,original_selection boolean DEFAULT false)
RETURNS laplace.content_materialization_result LANGUAGE SQL VOLATILE AS $materialize_form$
    SELECT CASE WHEN original_selection THEN laplace.content_materialize_utf8(context.value,
        form.entity_id,admitted.view_id,owner.descriptor_recipe,
        ROW(65536::numeric,262144::numeric,4194304::numeric,128,1)::laplace.cognition_materialization_request,
        form.record_id) ELSE laplace.content_materialize_utf8(context.value,
        CASE WHEN canonical_content THEN form.entity_id ELSE admitted.root_entity_id END,
        admitted.view_id,owner.descriptor_recipe,
        ROW(65536::numeric,262144::numeric,4194304::numeric,128,1)::laplace.cognition_materialization_request) END
    FROM physicality_entity_contract.context context
    CROSS JOIN physicality_entity_contract.forms form
    JOIN physicality_entity_contract.form_admitted admitted USING(record_id)
    JOIN laplace.physicality_entity_view owner USING(view_id)
    WHERE form.ordinal=selected_ordinal
$materialize_form$;
CREATE TABLE physicality_entity_contract.form_warm AS
SELECT form.ordinal,canonical.value AS canonical,
    physicality_entity_contract.materialize_form(form.ordinal,canonical.value) AS result
FROM physicality_entity_contract.forms form CROSS JOIN (VALUES(false),(true)) canonical(value);

CREATE TABLE physicality_entity_contract.form_original_warm AS
SELECT form.ordinal,physicality_entity_contract.materialize_form(form.ordinal,true,true) AS result
FROM physicality_entity_contract.forms form;

CREATE TABLE physicality_entity_contract.form_single_receipts(ordinal integer PRIMARY KEY,deposit_receipt_id bytea NOT NULL);

DO $form_admission_and_intervals$
DECLARE expected record; actual record; omitted_rejected boolean:=false;
BEGIN
    IF (SELECT count(*) FROM physicality_entity_contract.form_admitted) IS DISTINCT FROM 4::bigint
       OR (SELECT count(DISTINCT view_id) FROM physicality_entity_contract.form_admitted) IS DISTINCT FROM 4::bigint
       OR (SELECT count(DISTINCT root_entity_id) FROM physicality_entity_contract.form_admitted) IS DISTINCT FROM 4::bigint
       OR (SELECT value->'physicalities' FROM physicality_entity_contract.form_before)
            IS DISTINCT FROM (SELECT value->'physicalities' FROM physicality_entity_contract.form_after)
       OR (SELECT value->'attestations' FROM physicality_entity_contract.form_before)
            IS DISTINCT FROM (SELECT value->'attestations' FROM physicality_entity_contract.form_after)
    THEN RAISE EXCEPTION 'native same-E descriptor batch changed source physicality state or lost exact owners'; END IF;
    FOR expected IN SELECT admitted.*,forms.ordinal FROM physicality_entity_contract.form_admitted admitted
        JOIN physicality_entity_contract.forms forms USING(record_id) LOOP
        SELECT * INTO STRICT actual FROM physicality_entity_contract.admit_forms(ARRAY[expected.ordinal]);
        INSERT INTO physicality_entity_contract.form_single_receipts VALUES(expected.ordinal,actual.deposit_receipt_id);
        IF actual.view_id IS DISTINCT FROM expected.view_id
           OR actual.root_entity_id IS DISTINCT FROM expected.root_entity_id
           OR actual.root_witness IS DISTINCT FROM expected.root_witness
           OR actual.inserted_entity_count IS DISTINCT FROM 0::bigint
        THEN RAISE EXCEPTION 'same-E singleton/batch replay changed canonical descriptor or source selection'; END IF;
    END LOOP;
    -- Batch and single operations may own different deposition receipts. Their
    -- source-scoped descriptors and canonical/binding state must remain exact.
    IF physicality_entity_contract.counts()-ARRAY['deposit_receipts','deposition_links']
       IS DISTINCT FROM (SELECT value-ARRAY['deposit_receipts','deposition_links']
           FROM physicality_entity_contract.form_after) THEN
        RAISE EXCEPTION 'batch/single reflection replay amplified canonical or binding state';
    END IF;
    UPDATE physicality_entity_contract.form_after SET value=physicality_entity_contract.counts();
    FOR expected IN SELECT * FROM physicality_entity_contract.form_single_receipts LOOP
        SELECT * INTO STRICT actual FROM physicality_entity_contract.admit_forms(ARRAY[expected.ordinal]);
        IF actual.deposit_receipt_id IS DISTINCT FROM expected.deposit_receipt_id
           OR actual.inserted_entity_count IS DISTINCT FROM 0::bigint THEN
            RAISE EXCEPTION 'exact single-operation replay changed its real deposition receipt';
        END IF;
    END LOOP;
    IF NOT EXISTS(SELECT 1 FROM physicality_entity_contract.form_admitted admitted
        JOIN physicality_entity_contract.forms parent USING(record_id)
        JOIN laplace.physicality_entity_view owner USING(view_id)
        JOIN laplace.physicality_occurrence_binding_member member USING(binding_set_id)
        JOIN laplace.physicality_occurrence_binding binding USING(parent_receipt_id,parent_physicality_id)
        WHERE parent.ordinal=4 AND binding.parent_physicality_id=parent.record_id
        GROUP BY admitted.view_id HAVING count(*)=2 AND
            array_agg(binding.first_logical_ordinal ORDER BY binding.first_logical_ordinal)=ARRAY[1,2]::numeric[] AND
            array_agg(binding.logical_count ORDER BY binding.first_logical_ordinal)=ARRAY[1,1]::numeric[] AND
            array_agg(binding.selected_physicality_id::bytea ORDER BY binding.first_logical_ordinal)=
                ARRAY[(SELECT record_id FROM physicality_entity_contract.forms WHERE ordinal=1),
                      (SELECT record_id FROM physicality_entity_contract.forms WHERE ordinal=3)])
       OR NOT EXISTS(SELECT 1 FROM physicality_entity_contract.form_admitted admitted
        JOIN physicality_entity_contract.forms parent USING(record_id)
        JOIN laplace.physicality_entity_view owner USING(view_id)
        JOIN laplace.physicality_occurrence_binding_member member USING(binding_set_id)
        JOIN laplace.physicality_occurrence_binding binding USING(parent_receipt_id,parent_physicality_id)
        WHERE parent.ordinal=3 AND binding.parent_physicality_id=parent.record_id
            AND binding.first_logical_ordinal=1 AND binding.logical_count=1
            AND binding.selected_physicality_id=(SELECT record_id FROM physicality_entity_contract.forms WHERE ordinal=1))
    THEN RAISE EXCEPTION 'RLE merge or transparent singleton erased the exact selected physicality intervals'; END IF;
    BEGIN
        PERFORM physicality_entity_contract.admit_forms(ARRAY[4],true);
    EXCEPTION WHEN ambiguous_parameter THEN
        IF SQLERRM<>'Laplace requires explicit intervals for a child with multiple selected physicalities' THEN RAISE; END IF;
        omitted_rejected:=true;
    END;
    IF NOT omitted_rejected OR physicality_entity_contract.counts() IS DISTINCT FROM
        (SELECT value FROM physicality_entity_contract.form_after) THEN
        RAISE EXCEPTION 'missing same-E occurrence choices were guessed or replay amplified state';
    END IF;
END $form_admission_and_intervals$;

DO $form_warm$
DECLARE rejected boolean:=false;
BEGIN
    IF (SELECT count(*) FROM physicality_entity_contract.form_warm) IS DISTINCT FROM 8::bigint
       OR EXISTS(SELECT 1 FROM physicality_entity_contract.form_warm expected
            WHERE physicality_entity_contract.materialize_form(expected.ordinal,expected.canonical)
                IS DISTINCT FROM expected.result)
       OR EXISTS(SELECT 1 FROM physicality_entity_contract.form_warm
           WHERE (result).status IS DISTINCT FROM 0 OR (result).output IS NULL
               OR (result).output_bytes IS DISTINCT FROM octet_length((result).output)::numeric
               OR (result).materialization_id IS NULL OR (result).readset_fingerprint IS NULL
               OR (canonical AND (result).output IS DISTINCT FROM
                    convert_to(CASE WHEN ordinal=4 THEN 'ABAB' ELSE 'AB' END,'UTF8')))
       OR (SELECT count(*) FROM physicality_entity_contract.form_original_warm) IS DISTINCT FROM 4::bigint
       OR EXISTS(SELECT 1 FROM physicality_entity_contract.form_original_warm expected
           WHERE (expected.result).status IS DISTINCT FROM 0
               OR (expected.result).output IS DISTINCT FROM convert_to(CASE WHEN ordinal=4 THEN 'ABAB' ELSE 'AB' END,'UTF8')
               OR (expected.result).resolved_node_count IS DISTINCT FROM (CASE WHEN ordinal<=2 THEN 3 WHEN ordinal=3 THEN 4 ELSE 5 END)::numeric
               OR (expected.result).trajectory_carrier_count IS DISTINCT FROM (CASE WHEN ordinal<=2 THEN 2 WHEN ordinal=3 THEN 3 ELSE 4 END)::numeric
               OR physicality_entity_contract.materialize_form(expected.ordinal,true,true) IS DISTINCT FROM expected.result)
       OR physicality_entity_contract.counts() IS DISTINCT FROM (SELECT value FROM physicality_entity_contract.form_after)
    THEN RAISE EXCEPTION 'same-E canonical reference or exact descriptor warm readback failed'; END IF;
    BEGIN
        PERFORM laplace.content_materialize_utf8(context.value,source.entity_id,admitted.view_id,owner.descriptor_recipe,
            ROW(65536::numeric,262144::numeric,4194304::numeric,128,1)::laplace.cognition_materialization_request,
            other.record_id)
        FROM physicality_entity_contract.form_admitted admitted
        JOIN laplace.physicality_entity_view owner USING(view_id)
        JOIN physicality_entity_contract.forms source USING(record_id)
        CROSS JOIN physicality_entity_contract.forms other
        CROSS JOIN physicality_entity_contract.context context WHERE source.ordinal=1 AND other.ordinal=2;
    EXCEPTION WHEN data_corrupted THEN
        IF SQLERRM<>'Laplace materialization selected root is not the retained observation''s original source' THEN RAISE; END IF;
        rejected:=true;
    END;
    IF NOT rejected OR physicality_entity_contract.counts() IS DISTINCT FROM
        (SELECT value FROM physicality_entity_contract.form_after) THEN
        RAISE EXCEPTION 'original-P owner accepted a different valid same-E physicality or wrote state';
    END IF;
END $form_warm$;

-- The immutable parent physicality does not encode its historical selected-P
-- observation. A second valid interval owner must coexist without contaminating
-- the first owner's original-P traversal or generated descriptor identity.
CREATE TABLE physicality_entity_contract.form_reverse AS
SELECT * FROM physicality_entity_contract.admit_forms(ARRAY[4],false,true);
CREATE TABLE physicality_entity_contract.form_reverse_warm AS
SELECT reversed.view_id,reversed.root_entity_id,
    laplace.content_materialize_utf8(context.value,parent.entity_id,reversed.view_id,owner.descriptor_recipe,
        ROW(65536::numeric,262144::numeric,4194304::numeric,128,1)::laplace.cognition_materialization_request,
        parent.record_id) AS result
FROM physicality_entity_contract.form_reverse reversed
JOIN laplace.physicality_entity_view owner USING(view_id)
CROSS JOIN physicality_entity_contract.forms parent
CROSS JOIN physicality_entity_contract.context context WHERE parent.ordinal=4;
DO $different_valid_observation$
DECLARE original record; reversed record; actual record;
BEGIN
    SELECT admitted.* INTO STRICT original FROM physicality_entity_contract.form_admitted admitted
    JOIN physicality_entity_contract.forms form USING(record_id) WHERE form.ordinal=4;
    SELECT * INTO STRICT reversed FROM physicality_entity_contract.form_reverse;
    IF reversed.record_id IS DISTINCT FROM original.record_id OR reversed.view_id IS NOT DISTINCT FROM original.view_id
       OR reversed.root_entity_id IS DISTINCT FROM original.root_entity_id
       OR reversed.root_witness IS DISTINCT FROM original.root_witness
       OR reversed.inserted_entity_count IS DISTINCT FROM 0::bigint
       OR (SELECT binding_set_id FROM laplace.physicality_entity_view WHERE view_id=reversed.view_id) IS NOT DISTINCT FROM
          (SELECT binding_set_id FROM laplace.physicality_entity_view WHERE view_id=original.view_id)
       OR (SELECT count(*) FROM physicality_entity_contract.form_reverse_warm) IS DISTINCT FROM 1::bigint
       OR EXISTS(SELECT 1 FROM physicality_entity_contract.form_reverse_warm warm
            WHERE (warm.result).status IS DISTINCT FROM 0 OR (warm.result).output IS DISTINCT FROM convert_to('ABAB','UTF8')
                OR (warm.result).source_receipt_id IS DISTINCT FROM reversed.view_id
                OR (warm.result).resolved_node_count IS DISTINCT FROM 5::numeric
                OR (warm.result).trajectory_carrier_count IS DISTINCT FROM 4::numeric
                OR (warm.result).materialization_id IS NOT DISTINCT FROM
                    (SELECT (result).materialization_id FROM physicality_entity_contract.form_original_warm WHERE ordinal=4)
                OR (warm.result).readset_fingerprint IS NOT DISTINCT FROM
                    (SELECT (result).readset_fingerprint FROM physicality_entity_contract.form_original_warm WHERE ordinal=4))
       OR EXISTS(SELECT 1 FROM physicality_entity_contract.form_original_warm expected
            WHERE physicality_entity_contract.materialize_form(expected.ordinal,true,true) IS DISTINCT FROM expected.result)
       OR (physicality_entity_contract.counts()->>'views')::bigint IS DISTINCT FROM
          (SELECT (value->>'views')::bigint+1 FROM physicality_entity_contract.form_after)
       OR (physicality_entity_contract.counts()->>'nodes')::bigint IS DISTINCT FROM
          (SELECT (value->>'nodes')::bigint+reversed.derived_node_count FROM physicality_entity_contract.form_after)
       OR physicality_entity_contract.counts()-ARRAY['views','nodes','binding_sets','binding_parents','binding_members','bindings','deposit_receipts','deposition_links']
          IS DISTINCT FROM (SELECT value-ARRAY['views','nodes','binding_sets','binding_parents','binding_members','bindings','deposit_receipts','deposition_links']
              FROM physicality_entity_contract.form_after)
    THEN RAISE EXCEPTION 'distinct valid original-P observation changed canonical content or contaminated its other owner'; END IF;
    UPDATE physicality_entity_contract.form_after SET value=physicality_entity_contract.counts();
    SELECT * INTO STRICT actual FROM physicality_entity_contract.admit_forms(ARRAY[4],false,true);
    IF actual.view_id IS DISTINCT FROM reversed.view_id
       OR actual.deposit_receipt_id IS DISTINCT FROM reversed.deposit_receipt_id
       OR actual.inserted_entity_count IS DISTINCT FROM 0::bigint
       OR physicality_entity_contract.counts() IS DISTINCT FROM (SELECT value FROM physicality_entity_contract.form_after) THEN
        RAISE EXCEPTION 'reversed valid occurrence observation failed exact replay';
    END IF;
END $different_valid_observation$;

SELECT pg_backend_pid() AS form_warm_pid \gset
SELECT current_setting('statement_timeout') AS form_statement_timeout \gset
\connect :DBNAME
SELECT set_config('statement_timeout', :'form_statement_timeout', false);
SELECT pg_backend_pid() <> :form_warm_pid AS form_new_backend \gset
\if :form_new_backend
\else
    \echo 'same-E form cold read reused the original PostgreSQL backend'
    \quit 1
\endif
DO $form_cold$
DECLARE expected record; actual laplace.content_materialization_result; context laplace.execution_context;
BEGIN
    FOR expected IN SELECT * FROM physicality_entity_contract.form_warm LOOP
        actual:=physicality_entity_contract.materialize_form(expected.ordinal,expected.canonical);
        IF actual IS DISTINCT FROM expected.result THEN
            RAISE EXCEPTION 'new-backend same-E readback changed exact bytes or a native materialization receipt';
        END IF;
    END LOOP;
    FOR expected IN SELECT * FROM physicality_entity_contract.form_original_warm LOOP
        actual:=physicality_entity_contract.materialize_form(expected.ordinal,true,true);
        IF actual IS DISTINCT FROM expected.result THEN
            RAISE EXCEPTION 'new backend lost exact original-P content or materialization receipt';
        END IF;
    END LOOP;
    SELECT (value).* INTO STRICT context FROM physicality_entity_contract.context;
    FOR expected IN SELECT warm.*,parent.entity_id,parent.record_id,owner.descriptor_recipe
        FROM physicality_entity_contract.form_reverse_warm warm
        JOIN laplace.physicality_entity_view owner USING(view_id)
        CROSS JOIN physicality_entity_contract.forms parent WHERE parent.ordinal=4 LOOP
        actual:=laplace.content_materialize_utf8(context,expected.entity_id,expected.view_id,expected.descriptor_recipe,
            ROW(65536::numeric,262144::numeric,4194304::numeric,128,1)::laplace.cognition_materialization_request,expected.record_id);
        IF actual IS DISTINCT FROM expected.result THEN
            RAISE EXCEPTION 'new backend mixed the two valid historical observations of the same original P';
        END IF;
    END LOOP;
    IF physicality_entity_contract.counts() IS DISTINCT FROM (SELECT value FROM physicality_entity_contract.form_after) THEN
        RAISE EXCEPTION 'cold same-E materialization created canonical or reflection state';
    END IF;
END $form_cold$;

-- Retain an actual composer-produced AB body and its exact pinned A/B children.
-- Only the new view context changes; no physicality field is rewritten.
CREATE TABLE physicality_entity_contract.cross_epoch_context(value laplace.execution_context NOT NULL);
DO $cross_epoch_context$
DECLARE context laplace.execution_context;
BEGIN
    SELECT (value).* INTO STRICT context FROM physicality_entity_contract.context;
    context.epochs[@LAPLACE_PHYSICALITY_TEST_GEOMETRY_INDEX@]:=set_byte(
        context.epochs[@LAPLACE_PHYSICALITY_TEST_GEOMETRY_INDEX@],0,
        get_byte(context.epochs[@LAPLACE_PHYSICALITY_TEST_GEOMETRY_INDEX@],0)#1);
    INSERT INTO physicality_entity_contract.cross_epoch_context VALUES(context);
END $cross_epoch_context$;
CREATE TABLE physicality_entity_contract.cross_epoch_sources AS
SELECT p.physicality_id,pg_catalog.record_send(p) AS body
FROM laplace.physicality p
WHERE p.physicality_id IN (SELECT record_id FROM physicality_entity_contract.sources
    UNION ALL SELECT record_id FROM physicality_entity_contract.forms WHERE ordinal=1);
DO $cross_epoch_inputs$
BEGIN
    IF (SELECT count(*) FROM physicality_entity_contract.cross_epoch_sources) IS DISTINCT FROM 3::bigint
       OR EXISTS(SELECT 1 FROM physicality_entity_contract.cross_epoch_sources original
           JOIN laplace.physicality p USING(physicality_id)
           CROSS JOIN physicality_entity_contract.cross_epoch_context context
           WHERE p.geometry_epoch IS NOT DISTINCT FROM
               (context.value).epochs[@LAPLACE_PHYSICALITY_TEST_GEOMETRY_INDEX@]) THEN
        RAISE EXCEPTION 'cross-epoch view fixture did not retain three original bodies outside its new geometry';
    END IF;
END $cross_epoch_inputs$;

CREATE FUNCTION physicality_entity_contract.admit_cross_epoch()
RETURNS TABLE(record_id bytea,view_id bytea,root_entity_id bytea,root_witness bytea,
    entity_candidate_count bigint,inserted_entity_count bigint,derived_node_count bigint,
    deposit_receipt_id bytea,batch_database_operations bigint)
LANGUAGE SQL VOLATILE AS $admit_cross_epoch$
    SELECT admitted.* FROM physicality_entity_contract.cross_epoch_context context
    CROSS JOIN physicality_entity_contract.forms source
    CROSS JOIN physicality_entity_contract.form_selections choices
    CROSS JOIN LATERAL laplace.physicality_entity_admit_batch(context.value,
        ARRAY[source.record_id],
        ARRAY(SELECT selected FROM physicality_entity_contract.sources ORDER BY ordinal)||ARRAY[source.selected],
        16384::bigint,65536::bigint,16384::bigint,4194304::numeric,65536::bigint,512::bigint,
        ARRAY(SELECT choice FROM unnest(choices.value) choice
              WHERE choice.parent_physicality_id=source.record_id
              ORDER BY choice.first_logical_ordinal)) admitted
    WHERE source.ordinal=1
$admit_cross_epoch$;
CREATE TABLE physicality_entity_contract.cross_epoch_first AS
SELECT * FROM physicality_entity_contract.admit_cross_epoch();
CREATE TABLE physicality_entity_contract.cross_epoch_after AS
SELECT physicality_entity_contract.counts() AS value;

CREATE FUNCTION physicality_entity_contract.materialize_cross_epoch(original_selection boolean)
RETURNS laplace.content_materialization_result LANGUAGE SQL VOLATILE AS $materialize_cross_epoch$
    SELECT CASE WHEN original_selection THEN laplace.content_materialize_utf8(context.value,
        source.entity_id,first.view_id,owner.descriptor_recipe,
        ROW(65536::numeric,262144::numeric,4194304::numeric,128,1)::laplace.cognition_materialization_request,
        source.record_id) ELSE laplace.content_materialize_utf8(context.value,
        source.entity_id,first.view_id,owner.descriptor_recipe,
        ROW(65536::numeric,262144::numeric,4194304::numeric,128,1)::laplace.cognition_materialization_request) END
    FROM physicality_entity_contract.cross_epoch_context context
    CROSS JOIN physicality_entity_contract.cross_epoch_first first
    JOIN laplace.physicality_entity_view owner USING(view_id)
    CROSS JOIN physicality_entity_contract.forms source WHERE source.ordinal=1
$materialize_cross_epoch$;
CREATE TABLE physicality_entity_contract.cross_epoch_warm AS
SELECT original_selection,physicality_entity_contract.materialize_cross_epoch(original_selection) AS result
FROM unnest(ARRAY[false,true]) original_selection;
DO $cross_epoch_warm$
DECLARE actual record; first physicality_entity_contract.cross_epoch_first%ROWTYPE;
    owner laplace.physicality_entity_view%ROWTYPE;
BEGIN
    SELECT * INTO STRICT first FROM physicality_entity_contract.cross_epoch_first;
    SELECT * INTO STRICT owner FROM laplace.physicality_entity_view WHERE view_id=first.view_id;
    SELECT * INTO STRICT actual FROM physicality_entity_contract.admit_cross_epoch();
    IF actual.record_id IS DISTINCT FROM first.record_id
       OR actual.view_id IS DISTINCT FROM first.view_id
       OR actual.root_entity_id IS DISTINCT FROM first.root_entity_id
       OR actual.root_witness IS DISTINCT FROM first.root_witness
       OR actual.deposit_receipt_id IS DISTINCT FROM first.deposit_receipt_id
       OR actual.inserted_entity_count IS DISTINCT FROM 0::bigint
       OR owner.source_physicality_id IS DISTINCT FROM (SELECT record_id FROM physicality_entity_contract.forms WHERE ordinal=1)
       OR owner.view_id IS NOT DISTINCT FROM (SELECT admitted.view_id FROM physicality_entity_contract.form_admitted admitted
           JOIN physicality_entity_contract.forms source USING(record_id) WHERE source.ordinal=1)
       OR first.root_entity_id IS DISTINCT FROM (SELECT admitted.root_entity_id
           FROM physicality_entity_contract.form_admitted admitted
           JOIN physicality_entity_contract.forms source USING(record_id) WHERE source.ordinal=1)
       OR first.root_witness IS DISTINCT FROM (SELECT admitted.root_witness
           FROM physicality_entity_contract.form_admitted admitted
           JOIN physicality_entity_contract.forms source USING(record_id) WHERE source.ordinal=1)
       OR owner.binding_set_id IS DISTINCT FROM (SELECT previous.binding_set_id
           FROM physicality_entity_contract.form_admitted admitted
           JOIN physicality_entity_contract.forms source USING(record_id)
           JOIN laplace.physicality_entity_view previous USING(view_id) WHERE source.ordinal=1)
       OR record_send(owner.admission_context) IS DISTINCT FROM
           (SELECT record_send(value) FROM physicality_entity_contract.cross_epoch_context)
       OR NOT EXISTS(SELECT 1 FROM laplace.physicality_entity_node node
           CROSS JOIN physicality_entity_contract.cross_epoch_context context
           WHERE node.view_id=first.view_id
               AND node.entity_id=(SELECT entity_id FROM physicality_entity_contract.forms WHERE ordinal=1)
               AND (node.physicality_metadata).geometry_epoch=
                   (context.value).epochs[@LAPLACE_PHYSICALITY_TEST_GEOMETRY_INDEX@])
       OR (SELECT count(*) FROM physicality_entity_contract.cross_epoch_warm) IS DISTINCT FROM 2::bigint
       OR EXISTS(SELECT 1 FROM physicality_entity_contract.cross_epoch_warm warm
           WHERE (warm.result).status IS DISTINCT FROM 0
               OR (warm.result).output IS DISTINCT FROM convert_to('AB','UTF8')
               OR (warm.result).output_bytes IS DISTINCT FROM 2::numeric
               OR (warm.result).codepoint_count IS DISTINCT FROM 2::numeric
               OR (warm.result).root_content_id IS DISTINCT FROM
                   (SELECT entity_id FROM physicality_entity_contract.forms WHERE ordinal=1)
               OR (warm.result).source_receipt_id IS DISTINCT FROM first.view_id
               OR (warm.result).source_recipe_id IS DISTINCT FROM owner.descriptor_recipe
               OR physicality_entity_contract.materialize_cross_epoch(warm.original_selection) IS DISTINCT FROM warm.result)
       OR (SELECT (result).materialization_id FROM physicality_entity_contract.cross_epoch_warm WHERE original_selection)
           IS NOT DISTINCT FROM (SELECT (result).materialization_id FROM physicality_entity_contract.cross_epoch_warm WHERE NOT original_selection)
       OR (SELECT (result).readset_fingerprint FROM physicality_entity_contract.cross_epoch_warm WHERE original_selection)
           IS NOT DISTINCT FROM (SELECT (result).readset_fingerprint FROM physicality_entity_contract.cross_epoch_warm WHERE NOT original_selection)
       OR EXISTS(SELECT 1 FROM physicality_entity_contract.cross_epoch_sources original
           LEFT JOIN laplace.physicality p USING(physicality_id)
           WHERE pg_catalog.record_send(p) IS DISTINCT FROM original.body)
       OR (SELECT value->'physicalities' FROM physicality_entity_contract.cross_epoch_after)
           IS DISTINCT FROM (SELECT value->'physicalities' FROM physicality_entity_contract.form_after)
       OR (SELECT value->'attestations' FROM physicality_entity_contract.cross_epoch_after)
           IS DISTINCT FROM (SELECT value->'attestations' FROM physicality_entity_contract.form_after)
       OR physicality_entity_contract.counts() IS DISTINCT FROM
           (SELECT value FROM physicality_entity_contract.cross_epoch_after) THEN
        RAISE EXCEPTION 'cross-epoch original/generated read or replay changed exact content, receipts, original bodies, or stored state';
    END IF;
END $cross_epoch_warm$;

SELECT pg_backend_pid() AS cross_epoch_warm_pid \gset
SELECT current_setting('statement_timeout') AS cross_epoch_statement_timeout \gset
\connect :DBNAME
SELECT set_config('statement_timeout', :'cross_epoch_statement_timeout', false);
SELECT pg_backend_pid() <> :cross_epoch_warm_pid AS cross_epoch_new_backend \gset
\if :cross_epoch_new_backend
\else
    \echo 'cross-epoch cold read reused the original PostgreSQL backend'
    \quit 1
\endif
DO $cross_epoch_cold$
DECLARE expected record; actual laplace.content_materialization_result; replay record;
    first physicality_entity_contract.cross_epoch_first%ROWTYPE;
BEGIN
    FOR expected IN SELECT * FROM physicality_entity_contract.cross_epoch_warm LOOP
        actual:=physicality_entity_contract.materialize_cross_epoch(expected.original_selection);
        IF actual IS DISTINCT FROM expected.result THEN
            RAISE EXCEPTION 'new-backend cross-epoch read changed exact content or materialization receipt';
        END IF;
    END LOOP;
    SELECT * INTO STRICT first FROM physicality_entity_contract.cross_epoch_first;
    SELECT * INTO STRICT replay FROM physicality_entity_contract.admit_cross_epoch();
    IF replay.record_id IS DISTINCT FROM first.record_id
       OR replay.view_id IS DISTINCT FROM first.view_id
       OR replay.root_entity_id IS DISTINCT FROM first.root_entity_id
       OR replay.root_witness IS DISTINCT FROM first.root_witness
       OR replay.deposit_receipt_id IS DISTINCT FROM first.deposit_receipt_id
       OR replay.inserted_entity_count IS DISTINCT FROM 0::bigint
       OR EXISTS(SELECT 1 FROM physicality_entity_contract.cross_epoch_sources original
           LEFT JOIN laplace.physicality p USING(physicality_id)
           WHERE pg_catalog.record_send(p) IS DISTINCT FROM original.body)
       OR physicality_entity_contract.counts() IS DISTINCT FROM
           (SELECT value FROM physicality_entity_contract.cross_epoch_after) THEN
        RAISE EXCEPTION 'cold cross-epoch replay changed original bodies, retained owner, or stored state';
    END IF;
END $cross_epoch_cold$;

SELECT 'LAPLACE_QA_RECEIPT physicality_entity_reflection ' ||
    jsonb_build_object('schema','laplace.physicality-entity-contract/v1',
    'root_entity_id',encode(first.root_entity_id,'hex'),
    'view_id',encode(first.view_id,'hex'),
    'derived_node_count',first.derived_node_count,
    'deposit_receipt_id',encode(first.deposit_receipt_id,'hex'),
    'batch_database_operations',first.batch_database_operations,
    'framework_entity_only_deposition_verified',true,
    'same_entity_distinct_native_forms',3,
    'transparent_singleton_public_sink_verified',true,
    'singleton_constructor_refusals',3,
    'distinct_physicality_rle_intervals_verified',true,
    'same_entity_batch_single_replay_verified',true,
    'same_entity_cold_receipt_and_content_parity',true,
    'original_physicality_selected_reads',4,
    'original_physicality_owner_refusals',1,
    'canonical_and_original_atom_cold_reads',2,
    'canonical_atom_owner_read_verified',true,
    'original_atom_owner_read_verified',true,
    'cross_epoch_source_view_verified',true,
    'original_same_entity_rle_nodes',5,
    'original_same_entity_rle_carriers',4,
    'same_physicality_distinct_observation_replay',true,
    'operation_boundary_rollback_verified',true,
    'conflicting_entity_witness_refused',true,
    'native_rle_interval_split_replay_verified',true,
    'complete_interval_cold_backend_verified',true,
    'interval_corruption_refusals',4,
    'singleton_mixed_batch_scope_identity_verified',true,
    'unrelated_batch_neighbor_physicality_change_verified',true,
    'inserted_entity_count',first.inserted_entity_count,
    'ordinary_provider_rows',(search.result).provider_rows_fetched,
    'ordinary_provider_database_operations',((search.result).execution).database_operations,
    'capacity_refusals',5,'corruption_refusals',10,'content_binding_refusals',7,
    'read_only_context_refusal_verified',true,
    'mixed_batch_replay_new_rollback_verified',true,
    'shared_owner_candidate_selection_verified',true,
    'cold_backend_verified',true,'canonical_physicalities_unchanged',true)
FROM physicality_entity_contract.first first CROSS JOIN physicality_entity_contract.search search;

DROP SCHEMA physicality_entity_contract CASCADE;
\echo LAPLACE_PHYSICALITY_ENTITY_CONTRACT_OK
