-- Actual selected provider, shared canonical source owner and durable readback.
-- The existing source fixture supplies the real Unicode/Highway context.
\ir source_admission_contract.sql

-- Reproduce the retained historical record/field constraints, then execute the
-- actual current reconciliation twice before admitting a zero-claim observation.
ALTER TABLE laplace.source_profile
 DROP CONSTRAINT source_profile_record_field_coverage,
 DROP CONSTRAINT source_profile_record_count_check,
 ADD CONSTRAINT source_profile_record_count_check CHECK(record_count>0),
 DROP CONSTRAINT source_profile_field_count_check,
 ADD CONSTRAINT source_profile_field_count_check CHECK(field_count>0);
DO $source_observation_upgrade$
DECLARE program text:=pg_catalog.pg_read_file('@CMAKE_BINARY_DIR@/integrations/postgresql/extension/source_observation_profile.sql');
BEGIN EXECUTE program; EXECUTE program; END
$source_observation_upgrade$;

CREATE TEMP TABLE cpp_input(ordinal integer PRIMARY KEY, name text UNIQUE, content bytea, media text);
INSERT INTO cpp_input VALUES
 (0,'clean.cpp',convert_to(E'// café\n#define INC(x) ((x)+1)\nnamespace n { template<class T> T f(T x) { if(x) { while(x) { x=INC(x-2); } } return x; } }\n','UTF8'),'text/x-c++'),
 (1,'duplicate-a.txt',convert_to(E'One exact retained observation.\n','UTF8'),'text/plain'),
 (2,'duplicate-b.txt',convert_to(E'One exact retained observation.\n','UTF8'),'text/plain'),
 (3,'missing.cpp',convert_to(E'int f() { if (1) }\n','UTF8'),'text/x-c++');

CREATE FUNCTION pg_temp.cpp_binding()
RETURNS laplace.source_grammar_binding LANGUAGE SQL STABLE AS $binding$
 SELECT ROW(convert_from(decode('@LAPLACE_CPP_GRAMMAR_LIBRARY_PATH_HEX@','hex'),'UTF8'),
   'tree_sitter_cpp','text/x-c++',4850464759608246272::numeric,
   decode('@LAPLACE_CPP_GRAMMAR_RECEIPT_SHA256@','hex'),
   decode('@LAPLACE_CPP_GRAMMAR_LIBRARY_SHA256@','hex'),
   @LAPLACE_CPP_GRAMMAR_LIBRARY_BYTES@::bigint,256)::laplace.source_grammar_binding
$binding$;

CREATE FUNCTION pg_temp.cpp_profile()
RETURNS laplace.source_profile_manifest LANGUAGE plpgsql STABLE AS $profile$
DECLARE result laplace.source_profile_manifest;
BEGIN
 result:=pg_temp.source_profile_declaration();
 result.artifact_graph_fingerprint:=decode(repeat('00',32),'hex');
 result.syntax_authority_fingerprint:=decode('@LAPLACE_CPP_GRAMMAR_RECEIPT_SHA256@','hex');
 result.selected_boundary_fingerprint:=sha256(convert_to('verified_cpp_source_contract/v1','UTF8'));
 result.flags:=50; result.reconstruction_class:=1;
 RETURN result;
END
$profile$;

CREATE FUNCTION pg_temp.cpp_artifacts()
RETURNS laplace.tabular_source_artifact[] LANGUAGE SQL STABLE AS $artifacts$
 SELECT array_agg(ROW(sha256(content),decode(repeat('00',32),'hex'),sha256(content),content,
   convert_to(name,'UTF8'),convert_to(media,'UTF8'),0::numeric,0::numeric,0::numeric,
   1,0,0,0,0,4,ARRAY[]::bytea[],0)::laplace.tabular_source_artifact ORDER BY ordinal)
 FROM cpp_input
$artifacts$;

CREATE TEMP TABLE cpp_invocation_count(value integer NOT NULL);
INSERT INTO cpp_invocation_count VALUES (0);
CREATE FUNCTION pg_temp.cpp_admit(binding laplace.source_grammar_binding DEFAULT pg_temp.cpp_binding(),
                                 batch numeric DEFAULT 65536,
                                 reference_rules laplace.tabular_reference_rule[] DEFAULT ARRAY[]::laplace.tabular_reference_rule[])
RETURNS laplace.tabular_source_admission_result LANGUAGE plpgsql VOLATILE AS $admit$
DECLARE context laplace.execution_context:=pg_temp.source_admission_context();
BEGIN
 UPDATE cpp_invocation_count SET value=value+1;
 RETURN laplace.source_admit_with_grammar(context,pg_temp.cpp_profile(),
   context.epochs[3],sha256(convert_to('verified C++ observation','UTF8')),
   pg_temp.cpp_artifacts(),reference_rules,
   ARRAY[]::laplace.tabular_mapping_rule[],batch,binding);
END
$admit$;

CREATE TEMP TABLE cpp_before AS SELECT
 (SELECT count(*) FROM laplace.entity) entities,
 (SELECT count(*) FROM laplace.physicality) physicalities,
 (SELECT count(*) FROM laplace.evidence_node) evidence,
 (SELECT count(*) FROM laplace.evidence_testimony) testimony;
CREATE TEMP TABLE cpp_first AS SELECT a.* FROM pg_temp.cpp_admit() AS a;
CREATE TEMP TABLE cpp_structural AS
 SELECT r.* FROM laplace.source_structural_witness_receipt r JOIN cpp_first f
 ON r.source_profile_id=f.profile_id AND r.composition_working_set_receipt=f.composition_working_set_receipt_id
 WHERE r.version=3;

DO $closure$
DECLARE p laplace.source_profile%ROWTYPE; f cpp_first%ROWTYPE; roots bigint;
BEGIN
 IF (SELECT value FROM cpp_invocation_count)<>1 THEN
   RAISE EXCEPTION 'C++ admission executed more than once for one selected result';
 END IF;
 SELECT * INTO STRICT f FROM cpp_first;
 SELECT * INTO STRICT p FROM laplace.source_profile WHERE profile_id=f.profile_id;
 IF p.file_count<>4 OR p.record_count<>0 OR p.field_count<>0 OR p.claim_count<>0 OR
    p.error_count<>1 OR f.testimony_count<>0 OR f.evidence_node_count<>0 OR
    f.evidence_lineage_receipt_id IS NOT NULL OR f.evidence_testimony_receipt_id IS NOT NULL OR
    f.evidence_lineage_isa_receipt_id IS NOT NULL OR f.evidence_testimony_isa_receipt_id IS NOT NULL OR
    (SELECT count(*) FROM laplace.evidence_node)<>(SELECT evidence FROM cpp_before) OR
    (SELECT count(*) FROM laplace.evidence_testimony)<>(SELECT testimony FROM cpp_before) THEN
   RAISE EXCEPTION 'C++ observation fabricated evidence or changed exact denominators';
 END IF;
 IF (SELECT count(*) FROM cpp_structural)<>1 OR
    NOT EXISTS(SELECT FROM laplace.world_admission WHERE admission_id=f.world_admission_id
       AND profile_claim_count=0 AND evidence_lineage_receipt_id IS NULL
       AND evidence_testimony_receipt_id IS NULL) THEN
   RAISE EXCEPTION 'C++ observation lacks exact structural/world closure';
 END IF;
 SELECT count(*) INTO roots FROM laplace.source_structural_witness
 WHERE source_profile_id=f.profile_id AND span_index=0 AND canonical_entity_id IS NOT NULL
   AND canonical_physicality_id IS NOT NULL;
 IF roots<>4 OR EXISTS(SELECT FROM laplace.source_structural_witness WHERE source_profile_id=f.profile_id
       AND artifact_index=0 AND (syntax_flags::bigint & 6)<>0) OR
    NOT EXISTS(SELECT FROM laplace.source_structural_witness WHERE source_profile_id=f.profile_id
       AND artifact_index=3 AND (syntax_flags::bigint & 2)<>0 AND canonical_entity_id IS NULL
       AND canonical_physicality_id IS NULL) OR
    NOT EXISTS(SELECT FROM laplace.source_structural_witness WHERE source_profile_id=f.profile_id
       AND artifact_index=3 AND (syntax_flags::bigint & 32)<>0 AND (syntax_flags::bigint & 2)=0
       AND byte_start=byte_end AND canonical_entity_id IS NULL AND canonical_physicality_id IS NULL) THEN
   RAISE EXCEPTION 'C++ valid/missing/empty syntax observations were dropped or relabeled';
 END IF;
 IF (SELECT canonical_entity_id FROM laplace.source_structural_witness
       WHERE source_profile_id=f.profile_id AND artifact_index=1 AND span_index=0) IS DISTINCT FROM
    (SELECT canonical_entity_id FROM laplace.source_structural_witness
       WHERE source_profile_id=f.profile_id AND artifact_index=2 AND span_index=0) THEN
   RAISE EXCEPTION 'distinct named identical files failed canonical content reuse';
 END IF;
END
$closure$;

DO $source_observation_schema_controls$
DECLARE rejected integer:=0; constraint_name text;
BEGIN
 FOR control IN 1..6 LOOP
   BEGIN
     UPDATE laplace.source_profile SET
       record_count=CASE WHEN control=1 THEN 1 ELSE record_count END,
       field_count=CASE WHEN control=2 THEN 1 ELSE field_count END,
       claim_count=CASE WHEN control=3 THEN 1 ELSE claim_count END,
       flags=CASE WHEN control=4 THEN (flags & ~15) | 1 ELSE flags END,
       not_applicable_mask=CASE WHEN control=5 THEN not_applicable_mask::bigint & ~16::bigint
                                WHEN control=6 THEN not_applicable_mask::bigint & ~32::bigint
                                ELSE not_applicable_mask END
     WHERE profile_id=(SELECT profile_id FROM cpp_first);
     RAISE EXCEPTION USING ERRCODE='LP001',MESSAGE='source profile accepted inconsistent RAW observation denominators';
   EXCEPTION WHEN check_violation THEN
     GET STACKED DIAGNOSTICS constraint_name=CONSTRAINT_NAME;
     IF constraint_name<>'source_profile_record_field_coverage' THEN RAISE; END IF;
     rejected:=rejected+1;
   END;
 END LOOP;
 IF rejected<>6 THEN RAISE EXCEPTION 'source profile schema controls were not all executed'; END IF;
END
$source_observation_schema_controls$;

CREATE TEMP TABLE cpp_physicality_choice AS
 SELECT w.source_profile_id,w.artifact_index,w.span_index,w.canonical_entity_id,
   w.canonical_physicality_id,other.physicality_id AS alternate_physicality_id
 FROM laplace.source_structural_witness w
 JOIN cpp_first f ON f.profile_id=w.source_profile_id
 JOIN laplace.physicality selected ON selected.physicality_id=w.canonical_physicality_id
 JOIN laplace.physicality other ON other.entity_id=w.canonical_entity_id
  AND other.physicality_type=selected.physicality_type
  AND other.geometry_epoch=selected.geometry_epoch
  AND other.recipe_fingerprint=selected.recipe_fingerprint
  AND other.recipe_version=selected.recipe_version
  AND other.physicality_id<>selected.physicality_id
 WHERE w.artifact_index=1 AND w.span_index=0;
DO $physicality_choice$
BEGIN
 IF NOT EXISTS(SELECT FROM cpp_physicality_choice) OR EXISTS(
   SELECT FROM laplace.source_structural_witness w JOIN cpp_first f ON f.profile_id=w.source_profile_id
   LEFT JOIN laplace.physicality p ON p.physicality_id=w.canonical_physicality_id
   WHERE w.byte_start<w.byte_end AND (p.physicality_id IS NULL OR p.entity_id IS DISTINCT FROM w.canonical_entity_id)) THEN
   RAISE EXCEPTION 'source fixture lacks exact selected physicalities and legitimate same-content alternatives';
 END IF;
END
$physicality_choice$;

CREATE FUNCTION pg_temp.cpp_read(artifact numeric, output_bound numeric DEFAULT 65536,
                               witness_bound numeric DEFAULT 10000,
                               context laplace.execution_context DEFAULT pg_temp.source_admission_context())
RETURNS laplace.source_readback_result LANGUAGE SQL VOLATILE AS $read$
 SELECT laplace.source_readback_utf8(context,f.profile_id,s.receipt_id,artifact,
    ROW(10000::numeric,100000::numeric,output_bound,256,1)::laplace.cognition_materialization_request,
    witness_bound) FROM cpp_first f CROSS JOIN cpp_structural s
$read$;
CREATE TEMP TABLE cpp_readback AS
 SELECT r.artifact_index::integer AS ordinal,r.* FROM cpp_first f CROSS JOIN cpp_structural s
 CROSS JOIN LATERAL laplace.source_readback_utf8_batch(pg_temp.source_admission_context(),
   f.profile_id,s.receipt_id,ARRAY[0,1,2,3]::numeric[],
   ROW(10000::numeric,100000::numeric,65536::numeric,256,1)::laplace.cognition_materialization_request,
   10000,(SELECT sum(octet_length(content)) FROM cpp_input)) r;
DO $readback$
BEGIN
 IF (SELECT array_agg(ordinal ORDER BY ordinal) FROM cpp_readback)
      IS DISTINCT FROM (SELECT array_agg(ordinal ORDER BY ordinal) FROM cpp_input) OR EXISTS(
     SELECT FROM cpp_input i JOIN cpp_readback r ON r.ordinal=i.ordinal
     WHERE r.content IS DISTINCT FROM i.content OR
       r.output_bytes IS DISTINCT FROM octet_length(i.content) OR
       r.verified_witnesses IS DISTINCT FROM (SELECT witness_count FROM cpp_structural) OR
       octet_length(r.materialization_receipt_id) IS DISTINCT FROM 32) THEN
   RAISE EXCEPTION 'exact C++ source readback differs from retained input bytes';
 END IF;
END
$readback$;

CREATE TEMP TABLE cpp_after AS SELECT
 (SELECT count(*) FROM laplace.entity) entities,
 (SELECT count(*) FROM laplace.physicality) physicalities,
 (SELECT count(*) FROM laplace.attestation) occurrences;
CREATE TEMP TABLE cpp_replay AS SELECT a.* FROM pg_temp.cpp_admit(pg_temp.cpp_binding(),1048576) AS a;
DO $replay$
BEGIN
 IF (SELECT value FROM cpp_invocation_count)<>2 THEN
   RAISE EXCEPTION 'C++ replay executed more than once for one selected result';
 END IF;
 IF (SELECT profile_id FROM cpp_first)<>(SELECT profile_id FROM cpp_replay) OR
    (SELECT root_entity_id FROM cpp_first)<>(SELECT root_entity_id FROM cpp_replay) OR
    (SELECT count(*) FROM laplace.entity)<>(SELECT entities FROM cpp_after) OR
    (SELECT count(*) FROM laplace.physicality)<>(SELECT physicalities FROM cpp_after) OR
    (SELECT count(*) FROM laplace.attestation)<>(SELECT occurrences FROM cpp_after) OR EXISTS(
      SELECT FROM cpp_readback r WHERE (pg_temp.cpp_read(r.ordinal)).materialization_receipt_id<>r.materialization_receipt_id) THEN
   RAISE EXCEPTION 'C++ replay or physical batch choice changed canonical state/readback';
 END IF;
END
$replay$;

DO $physicality_binding_controls$
DECLARE rejected integer:=0; expected text; expected_state text;
BEGIN
 FOR control IN 1..4 LOOP
   expected_state:='XX001';
   BEGIN
     IF control=1 THEN
       expected:='Laplace retained structural witnesses no longer match their receipt';
       UPDATE laplace.source_structural_witness SET canonical_physicality_id=(
         SELECT alternate_physicality_id FROM cpp_physicality_choice ORDER BY alternate_physicality_id LIMIT 1)
       WHERE source_profile_id=(SELECT profile_id FROM cpp_first) AND artifact_index=1 AND span_index=0;
     ELSIF control=2 THEN
       expected:='Laplace v3 structural witness lacks its exact physicality binding';
       UPDATE laplace.source_structural_witness SET canonical_physicality_id=NULL
       WHERE source_profile_id=(SELECT profile_id FROM cpp_first) AND artifact_index=1 AND span_index=0;
     ELSIF control=3 THEN
       expected_state:='P0002';
       expected:='Laplace materialization composition is absent in the pinned geometry epoch';
       UPDATE laplace.physicality SET geometry_epoch=decode(repeat('ff',32),'hex')
       WHERE physicality_id=(SELECT canonical_physicality_id FROM cpp_physicality_choice LIMIT 1);
     ELSE
       expected:='Laplace materialization physicality body differs from its native identity';
       UPDATE laplace.physicality SET
        (entity_id,physicality_type,vertex_class,recipe_version,structural_form,dimension_count,flags,
         recipe_fingerprint,geometry_epoch,trajectory_fingerprint,centroid_x,centroid_y,centroid_z,centroid_m,
         radius,logical_count,vertex_count,trajectory)=(
          SELECT p.entity_id,p.physicality_type,p.vertex_class,p.recipe_version,p.structural_form,p.dimension_count,p.flags,
           p.recipe_fingerprint,p.geometry_epoch,p.trajectory_fingerprint,p.centroid_x,p.centroid_y,p.centroid_z,p.centroid_m,
           p.radius,p.logical_count,p.vertex_count,p.trajectory
          FROM laplace.physicality p WHERE p.physicality_id=(SELECT alternate_physicality_id
           FROM cpp_physicality_choice ORDER BY alternate_physicality_id LIMIT 1))
        WHERE physicality_id=(SELECT canonical_physicality_id FROM cpp_physicality_choice LIMIT 1);
     END IF;
     PERFORM pg_temp.cpp_read(1);
     RAISE EXCEPTION USING ERRCODE='LP001',MESSAGE='source physicality binding control unexpectedly succeeded';
   EXCEPTION WHEN SQLSTATE 'XX001' OR SQLSTATE 'P0002' THEN
     IF SQLSTATE IS DISTINCT FROM expected_state OR SQLERRM IS DISTINCT FROM expected THEN
       RAISE;
     END IF;
     rejected:=rejected+1;
   END;
 END LOOP;
 IF rejected<>4 OR (SELECT content FROM cpp_readback WHERE ordinal=1)
      IS DISTINCT FROM (pg_temp.cpp_read(1)).content THEN
   RAISE EXCEPTION 'source physicality binding controls did not preserve exact readback';
 END IF;
END
$physicality_binding_controls$;

DO $reference_rule_arrays$
DECLARE rule laplace.tabular_reference_rule:=ROW(0::numeric,0::numeric,
  decode(repeat('11',16),'hex'),1,0)::laplace.tabular_reference_rule;
 rejected integer:=0;
BEGIN
 -- First admission and replay above exercise PostgreSQL's canonical empty
 -- typed array. Preserve exact nonempty shape and NULL-element rejection.
 BEGIN
   PERFORM pg_temp.cpp_admit(pg_temp.cpp_binding(),65536,ARRAY[[rule]]);
   RAISE EXCEPTION USING ERRCODE='LP001',MESSAGE='multidimensional reference rule array was accepted';
 EXCEPTION WHEN datatype_mismatch THEN
   IF SQLERRM<>'Laplace tabular reference rules must be an exact one-dimensional rule array' THEN RAISE; END IF;
   rejected:=rejected+1;
 END;
 BEGIN
   PERFORM pg_temp.cpp_admit(pg_temp.cpp_binding(),65536,ARRAY[NULL]::laplace.tabular_reference_rule[]);
   RAISE EXCEPTION USING ERRCODE='LP001',MESSAGE='NULL reference rule element was accepted';
 EXCEPTION WHEN null_value_not_allowed THEN
   IF SQLERRM<>'Laplace tabular reference rule array cannot contain nulls' THEN RAISE; END IF;
   rejected:=rejected+1;
 END;
 IF rejected<>2 OR (SELECT value FROM cpp_invocation_count)<>2 OR
    (SELECT count(*) FROM laplace.entity)<>(SELECT entities FROM cpp_after) OR
    (SELECT count(*) FROM laplace.physicality)<>(SELECT physicalities FROM cpp_after) OR
    (SELECT count(*) FROM laplace.attestation)<>(SELECT occurrences FROM cpp_after) THEN
   RAISE EXCEPTION 'reference rule array controls changed admitted state';
 END IF;
END
$reference_rule_arrays$;

DO $negative$
DECLARE binding laplace.source_grammar_binding; rejected integer:=0; context laplace.execution_context;
BEGIN
 FOR control IN 1..13 LOOP
   BEGIN
     binding:=pg_temp.cpp_binding();
     IF control=1 THEN binding.library_sha256:=decode(repeat('ff',32),'hex'); PERFORM pg_temp.cpp_admit(binding);
     ELSIF control=2 THEN binding.language_symbol:='tree_sitter_unavailable'; PERFORM pg_temp.cpp_admit(binding);
     ELSIF control=3 THEN binding.maximum_depth:=2; PERFORM pg_temp.cpp_admit(binding);
     ELSIF control=4 THEN PERFORM pg_temp.cpp_read(0,1);
     ELSIF control=5 THEN PERFORM pg_temp.cpp_read(0,65536,1);
     ELSIF control=6 THEN
       UPDATE laplace.source_structural_witness SET field_kind=field_kind+1 WHERE
        source_profile_id=(SELECT profile_id FROM cpp_first) AND artifact_index=0 AND span_index=1;
       PERFORM pg_temp.cpp_read(0);
     ELSIF control=7 THEN
       UPDATE laplace.source_structural_witness SET canonical_entity_id=(SELECT canonical_entity_id
        FROM laplace.source_structural_witness WHERE source_profile_id=(SELECT profile_id FROM cpp_first)
        AND artifact_index=1 AND span_index=0) WHERE source_profile_id=(SELECT profile_id FROM cpp_first)
        AND artifact_index=0 AND span_index=0;
       PERFORM pg_temp.cpp_read(0);
     ELSIF control=8 THEN
       UPDATE laplace.source_structural_witness_receipt SET witness_fingerprint=decode(repeat('fe',32),'hex')
        WHERE receipt_id=(SELECT receipt_id FROM cpp_structural);
       PERFORM pg_temp.cpp_read(0);
     ELSIF control=9 THEN
       UPDATE laplace.source_profile SET error_count=error_count+1 WHERE profile_id=(SELECT profile_id FROM cpp_first);
       PERFORM pg_temp.cpp_read(0);
     ELSIF control=10 THEN
       context:=pg_temp.source_admission_context(); context.epochs[8]:=decode(repeat('ff',32),'hex');
       PERFORM pg_temp.cpp_read(0,65536,10000,context);
     ELSE
       PERFORM laplace.source_readback_utf8_batch(pg_temp.source_admission_context(),
         f.profile_id,s.receipt_id,
         CASE WHEN control=11 THEN ARRAY[1,0]::numeric[] WHEN control=12 THEN ARRAY[0,0]::numeric[]
              ELSE ARRAY[0,1,2,3]::numeric[] END,
         ROW(10000::numeric,100000::numeric,65536::numeric,256,1)::laplace.cognition_materialization_request,
         10000,CASE WHEN control=13 THEN 1 ELSE 65536 END)
       FROM cpp_first f CROSS JOIN cpp_structural s;
     END IF;
     RAISE EXCEPTION USING ERRCODE='LP001',MESSAGE='C++ negative control unexpectedly succeeded';
   EXCEPTION WHEN SQLSTATE 'LP001' THEN RAISE;
     WHEN syntax_error_or_access_rule_violation THEN RAISE;
     WHEN OTHERS THEN rejected:=rejected+1;
   END;
 END LOOP;
 IF rejected<>13 OR (SELECT content FROM cpp_readback WHERE ordinal=0)<>(pg_temp.cpp_read(0)).content THEN
   RAISE EXCEPTION 'C++ failed controls did not roll back or preserve exact readback';
 END IF;
END
$negative$;

-- Execute the actual configured reconciliation program; every counterfeit
-- function mutation is rolled back by its expected collision exception.
DO $grammar_reconciliation$
DECLARE
 program text:=pg_catalog.pg_read_file('@CMAKE_BINARY_DIR@/integrations/postgresql/extension/source_grammar_admission.sql');
 selected oid:='laplace.source_admit_with_grammar(laplace.execution_context,laplace.source_profile_manifest,bytea,bytea,laplace.tabular_source_artifact[],laplace.tabular_reference_rule[],laplace.tabular_mapping_rule[],numeric,laplace.source_grammar_binding)'::regprocedure;
 original_definition text;
 original_record text;
 detected boolean;
BEGIN
 EXECUTE program;
 EXECUTE program;
 SELECT pg_catalog.pg_get_functiondef(selected),row_to_json(p)::text
 INTO STRICT original_definition,original_record FROM pg_catalog.pg_proc p WHERE p.oid=selected;
 IF position('laplace_pg_source_admit_tabular' IN original_definition)=0 THEN
   RAISE EXCEPTION 'grammar reconciliation control lacks its actual native symbol';
 END IF;
 FOR control IN 1..2 LOOP
   detected:=false;
   BEGIN
     IF control=1 THEN
       EXECUTE replace(original_definition,'laplace_pg_source_admit_tabular','laplace_pg_source_readback_utf8');
     ELSE
       EXECUTE pg_catalog.format('ALTER FUNCTION %s SET work_mem TO %L',selected::regprocedure,'64kB');
     END IF;
     EXECUTE program;
   EXCEPTION WHEN raise_exception THEN
     IF SQLERRM<>'source grammar function has an incompatible owner or implementation' THEN RAISE; END IF;
     detected:=true;
   END;
   IF NOT detected THEN RAISE EXCEPTION 'grammar reconciliation accepted counterfeit native binding control %',control; END IF;
   IF (SELECT row_to_json(p)::text FROM pg_catalog.pg_proc p WHERE p.oid=selected) IS DISTINCT FROM original_record THEN
     RAISE EXCEPTION 'grammar reconciliation control changed installed function metadata';
   END IF;
 END LOOP;
 IF (SELECT content FROM cpp_readback WHERE ordinal=0) IS DISTINCT FROM (pg_temp.cpp_read(0)).content THEN
   RAISE EXCEPTION 'grammar reconciliation controls changed exact source readback';
 END IF;
END
$grammar_reconciliation$;

CREATE FUNCTION pg_temp.cpp_unbound_content_read(
 laplace.execution_context,bytea,bytea,bytea)
RETURNS bytea AS 'laplace_pg','laplace_pg_test_content_materialize_utf8'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

-- Exercise an ambiguity reached only after the generic provider prefetches
-- a nested child. Both parents are real native compositions; no cognition
-- result is manufactured. Roll back this isolated test deposition afterward.
DO $prefetch_ambiguity$
DECLARE context laplace.execution_context:=pg_temp.source_admission_context();
 known laplace.composition_known_entity_record[];
 deposited laplace.composition_deposit_result;
 recipe bytea:=(SELECT recipe_program_fingerprint FROM laplace.source_profile
   WHERE profile_id=(SELECT profile_id FROM cpp_first));
 expected bytea:=(SELECT content||content FROM cpp_input WHERE ordinal=0);
 positive_passed boolean:=false; ambiguity_rejected boolean:=false;
 before_entities bigint:=(SELECT count(*) FROM laplace.entity);
 before_physicalities bigint:=(SELECT count(*) FROM laplace.physicality);
 before_occurrences bigint:=(SELECT count(*) FROM laplace.attestation);
 before_receipts bigint:=(SELECT count(*) FROM laplace.composition_execution_receipt);
BEGIN
 SELECT array_agg(ROW(w.canonical_entity_id,e.identity_witness,w.canonical_physicality_id,
   p.centroid_x,p.centroid_y,p.centroid_z,p.centroid_m,0::bigint,1::smallint,false)
   ::laplace.composition_known_entity_record ORDER BY w.artifact_index)
 INTO known FROM laplace.source_structural_witness w
 JOIN cpp_first f ON f.profile_id=w.source_profile_id
 JOIN laplace.entity e ON e.entity_id=w.canonical_entity_id
 JOIN laplace.physicality p ON p.physicality_id=w.canonical_physicality_id
 WHERE w.span_index=0 AND w.artifact_index IN (0,1);
 IF cardinality(known) IS DISTINCT FROM 2 OR
    (SELECT count(*) FROM laplace.physicality WHERE entity_id=(known[1]).entity_id
      AND physicality_type=1 AND geometry_epoch=context.epochs[3])<>1 THEN
   RAISE EXCEPTION 'generic prefetch fixture lacks its exact unambiguous Unicode child';
 END IF;
 BEGIN
   deposited:=laplace.composition_deposit_batch(context,
    sha256(convert_to('verified CPP nested prefetch source/v1','UTF8')),recipe,known,
    ARRAY[
     ROW(0::numeric,1::numeric,0::bigint,1,0)::laplace.composition_operand_record,
     ROW(0::numeric,1::numeric,0::bigint,1,0)::laplace.composition_operand_record,
     ROW(1::numeric,1::numeric,0::bigint,1,0)::laplace.composition_operand_record,
     ROW(0::numeric,1::numeric,0::bigint,1,0)::laplace.composition_operand_record],
    ARRAY[
     ROW(0::numeric,2::numeric,1::numeric,1,0,recipe,context.epochs[3],
      sha256(convert_to('verified CPP positive nested readback/v1','UTF8')))::laplace.composition_request_record,
     ROW(2::numeric,2::numeric,2::numeric,1,0,recipe,context.epochs[3],
      sha256(convert_to('verified CPP ambiguous nested readback/v1','UTF8')))::laplace.composition_request_record],
    65536::numeric);
   IF deposited.status IS DISTINCT FROM 0 OR cardinality(deposited.result_entity_ids) IS DISTINCT FROM 2 OR
      deposited.result_tier_floors IS DISTINCT FROM ARRAY[2,2]::smallint[] THEN
     RAISE EXCEPTION 'generic prefetch fixture failed native parent composition';
   END IF;
   IF pg_temp.cpp_unbound_content_read(context,deposited.result_entity_ids[1],
      deposited.working_set_receipt,recipe) IS DISTINCT FROM expected THEN
     RAISE EXCEPTION 'unambiguous nested generic content readback changed exact UTF8 bytes';
   END IF;
   positive_passed:=true;
   BEGIN
     PERFORM pg_temp.cpp_unbound_content_read(context,deposited.result_entity_ids[2],
      deposited.working_set_receipt,recipe);
     RAISE EXCEPTION USING ERRCODE='LP001',MESSAGE='prefetched unbound ambiguous child was accepted';
   EXCEPTION WHEN SQLSTATE 'XX001' THEN
     IF SQLERRM<>'Laplace materialization composition is not unique in the pinned geometry epoch' THEN RAISE; END IF;
     ambiguity_rejected:=true;
   END;
   RAISE EXCEPTION USING ERRCODE='LP002',MESSAGE='rollback successful nested prefetch fixture';
 EXCEPTION WHEN SQLSTATE 'LP002' THEN
   IF SQLERRM<>'rollback successful nested prefetch fixture' THEN RAISE; END IF;
 END;
 IF NOT positive_passed OR NOT ambiguity_rejected OR
    (SELECT count(*) FROM laplace.entity)<>before_entities OR
    (SELECT count(*) FROM laplace.physicality)<>before_physicalities OR
    (SELECT count(*) FROM laplace.attestation)<>before_occurrences OR
    (SELECT count(*) FROM laplace.composition_execution_receipt)<>before_receipts THEN
   RAISE EXCEPTION 'nested prefetch controls did not run or roll back their native deposition';
 END IF;
END
$prefetch_ambiguity$;

SELECT 'LAPLACE_QA_RECEIPT verified_cpp_source_admission ' || json_build_object(
 'schema','laplace.verified-cpp-source-acceptance/v1','files',4,'exact_readback_files',4,
 'provider_receipt_sha256','@LAPLACE_CPP_GRAMMAR_RECEIPT_SHA256@',
 'profile_id',encode(f.profile_id,'hex'),'structural_receipt_id',encode(s.receipt_id,'hex'),
 'world_receipt_id',encode(f.world_admission_receipt_id,'hex'),'witnesses',s.witness_count,
 'semantic_testimony',0,'negative_controls',13,'reconciliation_controls',2,'profile_schema_controls',6,
 'reference_rule_array_controls',2,
 'physicality_binding_controls',4,'same_content_physicality_selection',true,
 'prefetch_ambiguity_controls',1,
 'repeat_no_amplification',true,
 'executable_semantics_verified',false)::text FROM cpp_first f CROSS JOIN cpp_structural s;


-- A second real build of the same authenticated parser deliberately retains
-- build-directory __FILE__ bytes. Only execution identity may differ.
CREATE FUNCTION pg_temp.cpp_second_binding()
RETURNS laplace.source_grammar_binding LANGUAGE SQL STABLE AS $binding$
 SELECT ROW(convert_from(decode('@LAPLACE_CPP_GRAMMAR_SECOND_LIBRARY_PATH_HEX@','hex'),'UTF8'),
   'tree_sitter_cpp','text/x-c++',4850464759608246272::numeric,
   decode('@LAPLACE_CPP_GRAMMAR_RECEIPT_SHA256@','hex'),
   decode('@LAPLACE_CPP_GRAMMAR_SECOND_LIBRARY_SHA256@','hex'),
   @LAPLACE_CPP_GRAMMAR_SECOND_LIBRARY_BYTES@::bigint,256)::laplace.source_grammar_binding
$binding$;

CREATE TEMP TABLE cpp_execution_before AS SELECT
 (SELECT jsonb_agg(to_jsonb(w) ORDER BY artifact_index,span_index)
    FROM laplace.source_structural_witness w WHERE source_profile_id=f.profile_id) historical_rows,
 (SELECT jsonb_agg(to_jsonb(r) ORDER BY receipt_id)
    FROM laplace.source_structural_witness_receipt r
    WHERE source_profile_id=f.profile_id AND version<4) historical_receipts,
 (SELECT count(*) FROM laplace.entity) entities,
 (SELECT count(*) FROM laplace.physicality) physicalities,
 (SELECT count(*) FROM laplace.attestation) occurrences,
 (SELECT count(*) FROM laplace.source_structural_witness_execution
    WHERE source_profile_id=f.profile_id) observations,
 (SELECT count(*) FROM laplace.source_structural_witness_receipt
    WHERE source_profile_id=f.profile_id AND version=4) execution_receipts
 FROM cpp_first f;
CREATE TEMP TABLE cpp_second AS SELECT a.* FROM pg_temp.cpp_admit(pg_temp.cpp_second_binding(),1048576) AS a;
CREATE TEMP TABLE cpp_current_structural AS
 SELECT r.* FROM laplace.source_structural_witness_receipt r
 WHERE receipt_id=decode(laplace.source_admission_last_execution_metrics()::jsonb
     #>>'{last,structural_execution_receipt_id}','hex');

CREATE FUNCTION pg_temp.cpp_current_read(artifact numeric)
RETURNS laplace.source_readback_result LANGUAGE SQL VOLATILE AS $read$
 SELECT laplace.source_readback_utf8(pg_temp.source_admission_context(),f.profile_id,s.receipt_id,artifact,
    ROW(10000::numeric,100000::numeric,65536::numeric,256,1)::laplace.cognition_materialization_request,
    10000) FROM cpp_second f CROSS JOIN cpp_current_structural s
$read$;
DO $execution_separation$
DECLARE f cpp_first%ROWTYPE; second cpp_second%ROWTYPE; receipt cpp_current_structural%ROWTYPE;
 before_state cpp_execution_before%ROWTYPE;
BEGIN
 SELECT * INTO STRICT f FROM cpp_first;
 SELECT * INTO STRICT second FROM cpp_second;
 SELECT * INTO STRICT receipt FROM cpp_current_structural;
 SELECT * INTO STRICT before_state FROM cpp_execution_before;
 IF '@LAPLACE_CPP_GRAMMAR_LIBRARY_SHA256@'='@LAPLACE_CPP_GRAMMAR_SECOND_LIBRARY_SHA256@' OR
    second.profile_id<>f.profile_id OR second.root_entity_id<>f.root_entity_id OR
    second.root_physicality_id<>f.root_physicality_id OR
    second.composition_working_set_receipt_id<>(SELECT composition_working_set_receipt_id FROM cpp_replay) OR
    receipt.version<>4 OR receipt.source_profile_id<>f.profile_id OR
    receipt.composition_working_set_receipt<>second.composition_working_set_receipt_id OR
    receipt.witness_fingerprint=(SELECT witness_fingerprint FROM laplace.source_structural_witness_receipt
                                WHERE receipt_id=receipt.baseline_receipt_id) OR
    (SELECT count(*) FROM laplace.source_structural_witness_execution WHERE receipt_id=receipt.receipt_id)
       <>receipt.witness_count OR
    (SELECT count(*) FROM laplace.entity)<>before_state.entities OR
    (SELECT count(*) FROM laplace.physicality)<>before_state.physicalities OR
    (SELECT count(*) FROM laplace.attestation)<>before_state.occurrences OR
    (SELECT count(*) FROM laplace.source_structural_witness_execution WHERE source_profile_id=f.profile_id)
       <>before_state.observations+receipt.witness_count OR
    (SELECT count(*) FROM laplace.source_structural_witness_receipt WHERE source_profile_id=f.profile_id AND version=4)
       <>before_state.execution_receipts+1 OR
    (SELECT jsonb_agg(to_jsonb(w) ORDER BY artifact_index,span_index)
       FROM laplace.source_structural_witness w WHERE source_profile_id=f.profile_id)
       IS DISTINCT FROM before_state.historical_rows OR
    (SELECT jsonb_agg(to_jsonb(r) ORDER BY receipt_id)
       FROM laplace.source_structural_witness_receipt r WHERE source_profile_id=f.profile_id AND version<4)
       IS DISTINCT FROM before_state.historical_receipts OR
    EXISTS(SELECT FROM laplace.source_structural_witness_receipt r
       WHERE source_profile_id=f.profile_id AND version=4
         AND canonical_witness_fingerprint<>receipt.canonical_witness_fingerprint) OR
    NOT EXISTS(SELECT FROM laplace.source_structural_witness_execution e
       JOIN laplace.source_structural_witness w USING(source_profile_id,artifact_index,span_index)
       WHERE e.receipt_id=receipt.receipt_id AND e.artifact_index=0
         AND e.provider_fingerprint<>w.provider_fingerprint) THEN
   RAISE EXCEPTION 'same authenticated syntax under a different physical build changed canonical state or lost execution provenance';
 END IF;
 FOR artifact IN 0..3 LOOP
   IF (pg_temp.cpp_current_read(artifact)).content IS DISTINCT FROM
      (SELECT content FROM cpp_input WHERE ordinal=artifact) OR
      (pg_temp.cpp_read(artifact)).content IS DISTINCT FROM
      (SELECT content FROM cpp_input WHERE ordinal=artifact) THEN
     RAISE EXCEPTION 'current or historical structural receipt failed exact source readback';
   END IF;
 END LOOP;
END
$execution_separation$;

CREATE TEMP TABLE cpp_execution_after AS SELECT
 (SELECT count(*) FROM laplace.source_structural_witness_execution) observations,
 (SELECT count(*) FROM laplace.source_structural_witness_receipt) receipts;
CREATE TEMP TABLE cpp_second_replay AS SELECT a.* FROM pg_temp.cpp_admit(pg_temp.cpp_second_binding(),1048576) AS a;
DO $execution_replay$
BEGIN
 IF (laplace.source_admission_last_execution_metrics()::jsonb#>>'{last,structural_execution_receipt_id}')
       IS DISTINCT FROM (SELECT encode(receipt_id,'hex') FROM cpp_current_structural) OR
    (SELECT profile_id FROM cpp_second_replay)<>(SELECT profile_id FROM cpp_second) OR
    (SELECT root_entity_id FROM cpp_second_replay)<>(SELECT root_entity_id FROM cpp_second) OR
    (SELECT root_physicality_id FROM cpp_second_replay)<>(SELECT root_physicality_id FROM cpp_second) OR
    (SELECT count(*) FROM laplace.source_structural_witness_execution)<>(SELECT observations FROM cpp_execution_after) OR
    (SELECT count(*) FROM laplace.source_structural_witness_receipt)<>(SELECT receipts FROM cpp_execution_after) OR
    (SELECT count(*) FROM laplace.entity)<>(SELECT entities FROM cpp_execution_before) OR
    (SELECT count(*) FROM laplace.physicality)<>(SELECT physicalities FROM cpp_execution_before) OR
    (SELECT count(*) FROM laplace.attestation)<>(SELECT occurrences FROM cpp_execution_before) THEN
   RAISE EXCEPTION 'identical execution replay amplified canonical state or execution observations';
 END IF;
END
$execution_replay$;

DO $execution_corruption$
DECLARE rejected integer:=0; selected_profile bytea:=(SELECT profile_id FROM cpp_second);
 current_receipt bytea:=(SELECT receipt_id FROM cpp_current_structural);
 baseline_receipt bytea:=(SELECT baseline_receipt_id FROM cpp_current_structural);
BEGIN
 FOR control IN 1..15 LOOP
   BEGIN
     IF control IN(1,2,14) THEN
       UPDATE laplace.source_structural_witness SET
        trace_fingerprint=CASE WHEN control IN(1,14) THEN set_byte(trace_fingerprint,0,get_byte(trace_fingerprint,0)#1) ELSE trace_fingerprint END,
        provider_fingerprint=CASE WHEN control=2 THEN set_byte(provider_fingerprint,0,get_byte(provider_fingerprint,0)#1) ELSE provider_fingerprint END
       WHERE source_profile_id=selected_profile AND artifact_index=0 AND span_index=0;
     ELSIF control IN(3,4,13) THEN
       UPDATE laplace.source_structural_witness_execution SET
        trace_fingerprint=CASE WHEN control IN(3,13) THEN set_byte(trace_fingerprint,0,get_byte(trace_fingerprint,0)#1) ELSE trace_fingerprint END,
        provider_fingerprint=CASE WHEN control=4 THEN set_byte(provider_fingerprint,0,get_byte(provider_fingerprint,0)#1) ELSE provider_fingerprint END
       WHERE receipt_id=current_receipt AND artifact_index=0 AND span_index=0;
     ELSIF control IN(5,6) THEN
       UPDATE laplace.source_structural_witness SET
        syntax_flags=syntax_flags+CASE WHEN control=5 THEN 1 ELSE 0 END,
        field_kind=field_kind+CASE WHEN control=6 THEN 1 ELSE 0 END
       WHERE source_profile_id=selected_profile AND artifact_index=0 AND span_index=0;
     ELSIF control=7 THEN
       UPDATE laplace.source_structural_witness SET canonical_entity_id=(
        SELECT canonical_entity_id FROM laplace.source_structural_witness
        WHERE source_profile_id=selected_profile AND artifact_index=1 AND span_index=0)
       WHERE source_profile_id=selected_profile AND artifact_index=0 AND span_index=0;
     ELSIF control=8 THEN
       UPDATE laplace.source_structural_witness SET canonical_physicality_id=(
        SELECT alternate_physicality_id FROM cpp_physicality_choice ORDER BY alternate_physicality_id LIMIT 1)
       WHERE source_profile_id=selected_profile AND artifact_index=1 AND span_index=0;
     ELSIF control=9 THEN
       UPDATE laplace.physicality SET
        (entity_id,physicality_type,vertex_class,recipe_version,structural_form,dimension_count,flags,
         recipe_fingerprint,geometry_epoch,trajectory_fingerprint,centroid_x,centroid_y,centroid_z,centroid_m,
         radius,logical_count,vertex_count,trajectory)=(
          SELECT p.entity_id,p.physicality_type,p.vertex_class,p.recipe_version,p.structural_form,p.dimension_count,p.flags,
           p.recipe_fingerprint,p.geometry_epoch,p.trajectory_fingerprint,p.centroid_x,p.centroid_y,p.centroid_z,p.centroid_m,
           p.radius,p.logical_count,p.vertex_count,p.trajectory
          FROM laplace.physicality p WHERE p.physicality_id=(SELECT alternate_physicality_id
           FROM cpp_physicality_choice ORDER BY alternate_physicality_id LIMIT 1))
       WHERE physicality_id=(SELECT canonical_physicality_id FROM cpp_physicality_choice LIMIT 1);
     ELSIF control=10 THEN
       UPDATE laplace.source_structural_witness_receipt SET baseline_receipt_id=receipt_id
       WHERE receipt_id=current_receipt;
     ELSIF control=11 THEN
       INSERT INTO laplace.source_structural_witness_execution
        (receipt_id,source_profile_id,artifact_index,span_index,trace_fingerprint,provider_fingerprint)
       SELECT baseline_receipt,source_profile_id,artifact_index,span_index,trace_fingerprint,provider_fingerprint
       FROM laplace.source_structural_witness WHERE source_profile_id=selected_profile AND artifact_index=0 AND span_index=0;
     ELSIF control=12 THEN
       UPDATE laplace.source_structural_witness_receipt
       SET canonical_witness_fingerprint=set_byte(canonical_witness_fingerprint,0,get_byte(canonical_witness_fingerprint,0)#1)
       WHERE receipt_id=current_receipt;
     ELSE
       UPDATE laplace.source_structural_witness_receipt
       SET witness_fingerprint=set_byte(witness_fingerprint,0,get_byte(witness_fingerprint,0)#1)
       WHERE receipt_id=baseline_receipt;
     END IF;
     IF control IN(13,14) THEN
       PERFORM pg_temp.cpp_admit(pg_temp.cpp_second_binding(),1048576);
     ELSE
       PERFORM pg_temp.cpp_current_read(CASE WHEN control IN(8,9) THEN 1 ELSE 0 END);
     END IF;
     RAISE EXCEPTION USING ERRCODE='LP001',MESSAGE='structural execution corruption was accepted';
   EXCEPTION WHEN SQLSTATE 'XX001' THEN rejected:=rejected+1;
   END;
 END LOOP;
 IF rejected<>15 OR
    (SELECT count(*) FROM laplace.source_structural_witness_execution)<>(SELECT observations FROM cpp_execution_after) OR
    (SELECT count(*) FROM laplace.source_structural_witness_receipt)<>(SELECT receipts FROM cpp_execution_after) OR
    (SELECT jsonb_agg(to_jsonb(w) ORDER BY artifact_index,span_index)
       FROM laplace.source_structural_witness w WHERE source_profile_id=selected_profile)
       IS DISTINCT FROM (SELECT historical_rows FROM cpp_execution_before) OR
    (SELECT jsonb_agg(to_jsonb(r) ORDER BY receipt_id)
       FROM laplace.source_structural_witness_receipt r WHERE source_profile_id=selected_profile AND version<4)
       IS DISTINCT FROM (SELECT historical_receipts FROM cpp_execution_before) THEN
   RAISE EXCEPTION 'structural execution corruption controls failed to preserve historical and current state';
 END IF;
END
$execution_corruption$;

DO $execution_schema_reconciliation$
DECLARE program text:=pg_catalog.pg_read_file('@CMAKE_BINARY_DIR@/integrations/postgresql/extension/source_structural_witness_execution.sql');
 receipts_before jsonb; observations_before jsonb; rejected integer:=0;
BEGIN
 SELECT jsonb_agg(to_jsonb(r) ORDER BY receipt_id) INTO receipts_before
 FROM laplace.source_structural_witness_receipt r WHERE source_profile_id=(SELECT profile_id FROM cpp_first);
 SELECT jsonb_agg(to_jsonb(e) ORDER BY receipt_id,artifact_index,span_index) INTO observations_before
 FROM laplace.source_structural_witness_execution e WHERE source_profile_id=(SELECT profile_id FROM cpp_first);
 EXECUTE program; EXECUTE program;
 FOR control IN 1..2 LOOP
   BEGIN
     IF control=1 THEN
       ALTER TABLE laplace.source_structural_witness_execution ALTER CONSTRAINT source_witness_execution_receipt_fk NOT DEFERRABLE;
     ELSE
       ALTER TABLE laplace.source_structural_witness_receipt DROP CONSTRAINT source_witness_receipt_execution_presence;
       ALTER TABLE laplace.source_structural_witness_receipt ADD CONSTRAINT source_witness_receipt_execution_presence CHECK(version>0);
     END IF;
     EXECUTE program;
     RAISE EXCEPTION USING ERRCODE='LP001',MESSAGE='execution schema accepted a counterfeit contract';
   EXCEPTION WHEN raise_exception THEN
     IF SQLERRM NOT IN('source witness execution table keys have incompatible definitions',
                      'source witness execution receipt presence guard has an incompatible definition') THEN RAISE; END IF;
     rejected:=rejected+1;
   END;
 END LOOP;
 IF rejected<>2 OR receipts_before IS DISTINCT FROM (
      SELECT jsonb_agg(to_jsonb(r) ORDER BY receipt_id)
      FROM laplace.source_structural_witness_receipt r WHERE source_profile_id=(SELECT profile_id FROM cpp_first)) OR
    observations_before IS DISTINCT FROM (
      SELECT jsonb_agg(to_jsonb(e) ORDER BY receipt_id,artifact_index,span_index)
      FROM laplace.source_structural_witness_execution e WHERE source_profile_id=(SELECT profile_id FROM cpp_first)) THEN
   RAISE EXCEPTION 'execution schema reconciliation changed retained evidence';
 END IF;
END
$execution_schema_reconciliation$;

SELECT 'LAPLACE_QA_RECEIPT structural_execution_separation ' || json_build_object(
 'schema','laplace.structural-execution-separation/v1',
 'first_provider_sha256','@LAPLACE_CPP_GRAMMAR_LIBRARY_SHA256@',
 'second_provider_sha256','@LAPLACE_CPP_GRAMMAR_SECOND_LIBRARY_SHA256@',
 'same_profile',true,'same_entity_physicality',true,
 'historical_rows_and_receipts_unchanged',true,'exact_readback_files',4,
 'execution_receipt_id',encode(receipt_id,'hex'),
 'historical_receipt_id',encode(baseline_receipt_id,'hex'),
 'canonical_witness_fingerprint',encode(canonical_witness_fingerprint,'hex'),
 'current_witness_fingerprint',encode(witness_fingerprint,'hex'),
 'execution_rows_created_once',witness_count,'repeat_execution_row_delta',0,
 'repeat_execution_receipt_delta',0,'repeat_entity_delta',0,'repeat_physicality_delta',0,
 'repeat_attestation_delta',0,'corruption_controls',15,'schema_controls',2)::text FROM cpp_current_structural;
