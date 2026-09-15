-- Actual selected provider, shared canonical source owner and durable readback.
-- The existing source fixture supplies the real Unicode/Highway context.
\ir source_admission_contract.sql

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
                                 batch numeric DEFAULT 65536)
RETURNS laplace.tabular_source_admission_result LANGUAGE plpgsql VOLATILE AS $admit$
BEGIN
 UPDATE cpp_invocation_count SET value=value+1;
 RETURN laplace.source_admit_with_grammar(pg_temp.source_admission_context(),pg_temp.cpp_profile(),
   decode(repeat('c0',32),'hex'),sha256(convert_to('verified C++ observation','UTF8')),
   pg_temp.cpp_artifacts(),ARRAY[]::laplace.tabular_reference_rule[],
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
 WHERE r.version=2;

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
 WHERE source_profile_id=f.profile_id AND span_index=0 AND canonical_entity_id IS NOT NULL;
 IF roots<>4 OR EXISTS(SELECT FROM laplace.source_structural_witness WHERE source_profile_id=f.profile_id
       AND artifact_index=0 AND (syntax_flags::bigint & 6)<>0) OR
    NOT EXISTS(SELECT FROM laplace.source_structural_witness WHERE source_profile_id=f.profile_id
       AND artifact_index=3 AND (syntax_flags::bigint & 2)<>0 AND canonical_entity_id IS NULL) OR
    NOT EXISTS(SELECT FROM laplace.source_structural_witness WHERE source_profile_id=f.profile_id
       AND artifact_index=3 AND (syntax_flags::bigint & 32)<>0 AND (syntax_flags::bigint & 2)=0
       AND byte_start=byte_end AND canonical_entity_id IS NULL) THEN
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
 IF (SELECT count(*) FROM cpp_readback)<>4 OR EXISTS(
     SELECT FROM cpp_input i JOIN cpp_readback r ON r.ordinal=i.ordinal
     WHERE r.content<>i.content OR r.output_bytes<>octet_length(i.content) OR
       r.verified_witnesses<>(SELECT witness_count FROM cpp_structural) OR
       octet_length(r.materialization_receipt_id)<>32) THEN
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

SELECT 'LAPLACE_QA_RECEIPT verified_cpp_source_admission ' || json_build_object(
 'schema','laplace.verified-cpp-source-acceptance/v1','files',4,'exact_readback_files',4,
 'provider_receipt_sha256','@LAPLACE_CPP_GRAMMAR_RECEIPT_SHA256@',
 'profile_id',encode(f.profile_id,'hex'),'structural_receipt_id',encode(s.receipt_id,'hex'),
 'world_receipt_id',encode(f.world_admission_receipt_id,'hex'),'witnesses',s.witness_count,
 'semantic_testimony',0,'negative_controls',13,'reconciliation_controls',2,'repeat_no_amplification',true,
 'executable_semantics_verified',false)::text FROM cpp_first f CROSS JOIN cpp_structural s;
