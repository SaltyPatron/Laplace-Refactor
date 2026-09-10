-- Public PostgreSQL runtime surface for native cognition operator construction and solve.
-- Typed target compilation accepts whole QK/VO job sets, derives consumer weights,
-- and can emit the exact projected E/Q/K/V/O SafeTensors artifact.

CREATE TYPE laplace.cognition_operator_program AS (
    program_id bytea,
    boundary_id bytea,
    context_fingerprint bytea,
    evidence_epoch bytea,
    result_contract_fingerprint bytea,
    eligible_relation_families integer[],
    eligible_source_mask integer,
    flags integer,
    numeric_tolerance double precision,
    version integer
);

CREATE TYPE laplace.cognition_operator_field AS (
    field_id bytea,
    entity_id bytea,
    physicality_id bytea,
    role_id bytea,
    recipe_fingerprint bytea,
    ordinal numeric(20, 0),
    value_dimension integer,
    flags integer
);

CREATE TYPE laplace.cognition_operator_constraint AS (
    constraint_id bytea,
    plane_id bytea,
    law_fingerprint bytea,
    units_fingerprint bytea,
    evidence_root_id bytea,
    calculation_receipt_id bytea,
    source_field_index numeric(20, 0),
    target_field_index numeric(20, 0),
    transport_scale double precision,
    transport_offset double precision,
    target_value double precision,
    precision double precision,
    relation_family integer,
    source_class integer,
    direction integer,
    transport_kind integer,
    flags integer
);

CREATE TYPE laplace.cognition_solver_program AS (
    program_id bytea,
    result_contract_fingerprint bytea,
    max_iterations numeric(20, 0),
    absolute_residual_tolerance double precision,
    relative_residual_tolerance double precision,
    regularization double precision,
    method integer,
    flags integer,
    version integer
);

CREATE TYPE laplace.cognition_solve_result AS (
    solution double precision[],
    operator_receipt_id bytea,
    operator_id bytea,
    operator_program_fingerprint bytea,
    operator_field_set_fingerprint bytea,
    operator_constraint_set_fingerprint bytea,
    solver_receipt_id bytea,
    solver_program_fingerprint bytea,
    solver_input_fingerprint bytea,
    solver_iteration_trace_fingerprint bytea,
    solver_output_fingerprint bytea,
    iteration_count numeric(20, 0),
    initial_residual_l2 double precision,
    final_residual_l2 double precision,
    final_energy double precision,
    disposition integer,
    status integer,
    isa_receipt_id bytea
);

CREATE TYPE laplace.cognition_packet_result AS (
    packet bytea,
    receipt_id bytea,
    context_fingerprint bytea,
    program_fingerprint bytea,
    input_fingerprint bytea,
    output_fingerprint bytea,
    instruction_count bigint,
    executed_instruction_count bigint,
    isa_major smallint,
    isa_minor smallint,
    receipt_detail integer,
    status integer,
    request_word_count bigint
);

CREATE TYPE laplace.target_operator_job AS (
    target_role integer,
    layer_index integer,
    head_index integer,
    expert_index integer,
    role_fingerprint bytea,
    operator_program laplace.cognition_operator_program,
    fields laplace.cognition_operator_field[],
    constraints laplace.cognition_operator_constraint[],
    head_rank numeric(20, 0)
);

CREATE TYPE laplace.target_attention_compile_result AS (
    embedding double precision[],
    q double precision[],
    k double precision[],
    v double precision[],
    o double precision[],
    head_offsets numeric(20, 0)[],
    head_ranks numeric(20, 0)[],
    layer_indices integer[],
    head_indices integer[],
    expert_indices integer[],
    head_receipt_ids bytea[],
    qk_factorization_ids bytea[],
    vo_factorization_ids bytea[],
    qk_relative_residuals double precision[],
    vo_relative_residuals double precision[],
    compile_receipt_id bytea,
    compile_request_fingerprint bytea,
    projection_id bytea,
    embedding_fingerprint bytea,
    head_set_fingerprint bytea,
    field_count numeric(20, 0),
    hidden_width numeric(20, 0),
    semantic_basis_rank numeric(20, 0),
    null_basis_rank numeric(20, 0),
    max_relative_residual double precision,
    compile_status integer,
    projection_status integer
);

CREATE TYPE laplace.target_attention_export_result AS (
    artifact bytea,
    artifact_id bytea,
    compile_receipt_id bytea,
    compile_request_fingerprint bytea,
    projection_id bytea,
    embedding_fingerprint bytea,
    head_set_fingerprint bytea,
    byte_count numeric(20, 0),
    header_byte_count numeric(20, 0),
    data_byte_count numeric(20, 0),
    tensor_count numeric(20, 0),
    head_count numeric(20, 0),
    field_count numeric(20, 0),
    hidden_width numeric(20, 0),
    semantic_basis_rank numeric(20, 0),
    null_basis_rank numeric(20, 0),
    max_relative_residual double precision,
    compile_status integer,
    projection_status integer,
    codec_status integer
);

CREATE FUNCTION laplace.execution_context_fingerprint(
    laplace.execution_context)
RETURNS bytea
AS 'MODULE_PATHNAME', 'laplace_pg_execution_context_fingerprint'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION laplace.cognition_solve(
    laplace.execution_context,
    laplace.cognition_operator_program,
    laplace.cognition_operator_field[],
    laplace.cognition_operator_constraint[],
    laplace.cognition_solver_program,
    double precision[])
RETURNS laplace.cognition_solve_result
AS 'MODULE_PATHNAME', 'laplace_pg_cognition_solve'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

CREATE FUNCTION laplace.cognition_execute_packet(
    laplace.execution_context,
    bytea)
RETURNS laplace.cognition_packet_result
AS 'MODULE_PATHNAME', 'laplace_pg_cognition_execute_packet'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

CREATE FUNCTION laplace.target_attention_compile(
    laplace.execution_context,
    bytea,
    bytea,
    bytea,
    bytea,
    laplace.target_operator_job[],
    numeric(20, 0),
    double precision,
    boolean)
RETURNS laplace.target_attention_compile_result
AS 'MODULE_PATHNAME', 'laplace_pg_target_attention_compile'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

CREATE FUNCTION laplace.target_attention_export(
    laplace.execution_context,
    bytea,
    bytea,
    bytea,
    bytea,
    laplace.target_operator_job[],
    numeric(20, 0),
    double precision,
    boolean)
RETURNS laplace.target_attention_export_result
AS 'MODULE_PATHNAME', 'laplace_pg_target_attention_export'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

REVOKE EXECUTE ON FUNCTION laplace.execution_context_fingerprint(
    laplace.execution_context)
FROM PUBLIC;

REVOKE EXECUTE ON FUNCTION laplace.cognition_solve(
    laplace.execution_context,
    laplace.cognition_operator_program,
    laplace.cognition_operator_field[],
    laplace.cognition_operator_constraint[],
    laplace.cognition_solver_program,
    double precision[])
FROM PUBLIC;

REVOKE EXECUTE ON FUNCTION laplace.cognition_execute_packet(
    laplace.execution_context,
    bytea)
FROM PUBLIC;

REVOKE EXECUTE ON FUNCTION laplace.target_attention_compile(
    laplace.execution_context,
    bytea,
    bytea,
    bytea,
    bytea,
    laplace.target_operator_job[],
    numeric(20, 0),
    double precision,
    boolean)
FROM PUBLIC;

REVOKE EXECUTE ON FUNCTION laplace.target_attention_export(
    laplace.execution_context,
    bytea,
    bytea,
    bytea,
    bytea,
    laplace.target_operator_job[],
    numeric(20, 0),
    double precision,
    boolean)
FROM PUBLIC;