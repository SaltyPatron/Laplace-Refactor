#ifndef LAPLACE_POSTGRES_COGNITION_FIRMWARE_H
#define LAPLACE_POSTGRES_COGNITION_FIRMWARE_H

#include "postgres.h"
#include "laplace/cognition_firmware.h"
#include "cognition_provider_pg.h"

/* Concrete indexed physicality provider for an already-admitted observation.
 * Native firmware still owns all calculation and realization. The host supplies
 * independently admitted realization/materialization providers and retains
 * ownership of admission and publication. This is not a semantic testimony
 * provider and cannot advertise unsupported relation families.
 *
 * PostgreSQL errors are caught inside the C callback and returned after native
 * unwinding. The outer C host must release its other native owners, then rethrow
 * *database_error. A result is never retained with a database error.
 */
laplace_cognition_firmware_status laplace_pg_cognition_firmware_execute_indexed(
    const laplace_cognition_firmware_program* program,
    const laplace_cognition_firmware_request* request,
    const laplace_framework_context* context,
    laplace_cognition_prompt_admission* admission,
    const uint8_t* previous_checkpoint, size_t previous_checkpoint_bytes,
    const laplace_cognition_realization_provider_v1* realization,
    const laplace_cognition_materialization_provider_v1* materialization,
    uint64_t provider_workspace_bytes,
    laplace_framework_cancel_requested_fn cancel, void* cancel_state,
    laplace_cognition_firmware_result** result,
    laplace_cognition_firmware_error* error,
    laplace_pg_cognition_provider_report* reads,
    ErrorData** database_error);

#endif
