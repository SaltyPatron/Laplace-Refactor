-- Public PostgreSQL runtime surface for native cognition operator construction and solve.
-- This is an execution surface, not a test fixture.  The caller supplies the exact
-- typed operator/solver state and receives the native solution and receipts.

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
    status integer
);

CREATE FUNCTION laplace.cognition_solve(
    laplace.execution_context,
    laplace.cognition_operator_program,
    laplace.cognition_operator_field[],
    laplace.cognition_operator_constraint[],
    laplace.cognition_solver_program,
    double precision[])
RETURNS laplace.cognition_solve_result
AS 'MODULE_PATHNAME', 'laplace_pg_cognition_solve'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

REVOKE EXECUTE ON FUNCTION laplace.cognition_solve(
    laplace.execution_context,
    laplace.cognition_operator_program,
    laplace.cognition_operator_field[],
    laplace.cognition_operator_constraint[],
    laplace.cognition_solver_program,
    double precision[])
FROM PUBLIC;
