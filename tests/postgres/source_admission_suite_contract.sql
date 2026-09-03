\if :{?source_skip_unicode}
\else
\ir unicode_root_contract.sql
\endif

\set source_skip_unicode 1
BEGIN;
\ir source_admission_contract.sql
\ir source_admission_active_control_mutation.sql
ROLLBACK;
BEGIN;
\ir iso_639_source_admission_contract.sql
ROLLBACK;
BEGIN;
\ir cili_source_admission_contract.sql

-- Preserve the repaired real-CILI canonical-cardinality/replay proof inside the
-- terminal custom-stack receipt before this disposable PostgreSQL transaction and
-- workspace disappear. postgres_resource_guard.py emits the corresponding wall,
-- disk/WAL/workspace and peak PostgreSQL process-tree RSS receipt for this physical
-- source-admission execution. source_admission_last_execution_metrics() retains the
-- first CILI publication as `previous` and its immediate replay as `last`, exposing
-- actual provider rounds, persistence plan work, source-stage SPI calls, and receipt
-- publication calls rather than inferring crossings from SQL text.
\pset format unaligned
\pset tuples_only on
SELECT
    'LAPLACE_QA_RECEIPT cili_admission_cardinality ' ||
    json_build_object(
        'schema', 'laplace.cili-admission-cardinality/v1',
        'profile_id', encode(first.profile_id, 'hex'),
        'source_profile_receipt_id', encode(first.source_profile_receipt_id, 'hex'),
        'composition_working_set_receipt_id',
            encode(first.composition_working_set_receipt_id, 'hex'),
        'composition_stream_receipt_id',
            encode(first.composition_stream_receipt_id, 'hex'),
        'world_admission_receipt_id',
            encode(first.world_admission_receipt_id, 'hex'),
        'expected_request_count', expected.request_count,
        'reported_request_count', first.request_count,
        'reported_occurrence_count', first.occurrence_count,
        'reported_logical_occurrence_count', first.logical_occurrence_count,
        'reported_reference_occurrence_count', first.reference_occurrence_count,
        'reported_reference_coordinate_count', first.reference_coordinate_count,
        'reported_reference_persistence_batch_count',
            first.reference_persistence_batch_count,
        'reported_reference_maximum_persistence_batch_records',
            first.reference_maximum_persistence_batch_records,
        'reported_reference_maximum_encoded_persistence_batch_bytes',
            first.reference_maximum_encoded_persistence_batch_bytes,
        'reported_reference_mapping_persistence_batch_count',
            first.reference_mapping_persistence_batch_count,
        'reported_reference_mapping_maximum_persistence_batch_records',
            first.reference_mapping_maximum_persistence_batch_records,
        'reported_reference_mapping_maximum_encoded_persistence_batch_bytes',
            first.reference_mapping_maximum_encoded_persistence_batch_bytes,
        'reported_evidence_node_count', first.evidence_node_count,
        'reported_testimony_count', first.testimony_count,
        'reported_durable_stream_record_count', first.durable_stream_record_count,
        'canonical_entity_delta', after_first.entity_count - before.entity_count,
        'canonical_physicality_delta',
            after_first.physicality_count - before.physicality_count,
        'explicit_occurrence_delta',
            after_first.occurrence_count - before.occurrence_count,
        'source_profile_delta', after_first.profile_count - before.profile_count,
        'reference_coordinate_delta',
            after_first.reference_coordinate_count - before.reference_coordinate_count,
        'reference_occurrence_delta',
            after_first.reference_occurrence_count - before.reference_occurrence_count,
        'mapping_proposition_delta',
            after_first.mapping_proposition_count - before.mapping_proposition_count,
        'mapping_occurrence_delta',
            after_first.mapping_occurrence_count - before.mapping_occurrence_count,
        'evidence_node_delta', after_first.evidence_count - before.evidence_count,
        'testimony_delta', after_first.testimony_count - before.testimony_count,
        'world_admission_delta', after_first.world_count - before.world_count,
        'replay_entity_growth', after_replay.entity_count - after_first.entity_count,
        'replay_physicality_growth',
            after_replay.physicality_count - after_first.physicality_count,
        'replay_occurrence_growth',
            after_replay.occurrence_count - after_first.occurrence_count,
        'replay_profile_growth',
            after_replay.profile_count - after_first.profile_count,
        'replay_reference_coordinate_growth',
            after_replay.reference_coordinate_count -
            after_first.reference_coordinate_count,
        'replay_reference_occurrence_growth',
            after_replay.reference_occurrence_count -
            after_first.reference_occurrence_count,
        'replay_mapping_proposition_growth',
            after_replay.mapping_proposition_count -
            after_first.mapping_proposition_count,
        'replay_mapping_occurrence_growth',
            after_replay.mapping_occurrence_count -
            after_first.mapping_occurrence_count,
        'replay_evidence_node_growth',
            after_replay.evidence_count - after_first.evidence_count,
        'replay_testimony_growth',
            after_replay.testimony_count - after_first.testimony_count,
        'replay_world_admission_growth',
            after_replay.world_count - after_first.world_count,
        'execution_metrics', metrics.execution_metrics,
        'defect_baseline', json_build_object(
            'request_count', 24163435,
            'reported_approximate_peak_memory_gib', 20.7,
            'meaning',
                'historical recursive-request defect baseline from issue 102; not a measurement of this run'
        )
    )::text
FROM cili_expected AS expected
CROSS JOIN cili_before AS before
CROSS JOIN cili_first AS first
CROSS JOIN cili_after_first AS after_first
CROSS JOIN cili_after_replay AS after_replay
CROSS JOIN LATERAL (
    SELECT laplace.source_admission_last_execution_metrics()::jsonb AS execution_metrics
) AS metrics;
\pset tuples_only off
\pset format aligned
ROLLBACK;
