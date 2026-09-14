#ifndef LAPLACE_COGNITION_WHY_NOT_H
#define LAPLACE_COGNITION_WHY_NOT_H

#include <stdint.h>

#include "laplace/cognition_observation_request.h"
#include "laplace/machine_exception.h"
#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_COGNITION_WHY_NOT_VERSION = 1
};

typedef enum laplace_cognition_why_not_status {
    LAPLACE_COGNITION_WHY_NOT_OK = 0,
    LAPLACE_COGNITION_WHY_NOT_INVALID_ARGUMENT = 1,
    LAPLACE_COGNITION_WHY_NOT_NOT_APPLICABLE = 2,
    LAPLACE_COGNITION_WHY_NOT_FORWARD_RESULT_FAILURE = 3,
    LAPLACE_COGNITION_WHY_NOT_REQUEST_FAILURE = 4,
    LAPLACE_COGNITION_WHY_NOT_MEMORY_FAILURE = 5,
    LAPLACE_COGNITION_WHY_NOT_MACHINE_EXCEPTION_FAILURE = 6
} laplace_cognition_why_not_status;

/*
 * First-class incomplete cognition outcome. This is derived from the retained
 * native forward result, not from transport status text. The exact open required
 * obligations are fingerprinted, the limiting state is classified through the
 * common machine-exception registry, and the continuation condition binds the
 * request, final guidance state and finite execution receipt.
 */
typedef struct laplace_cognition_why_not_result {
    laplace_digest256 outcome_id;
    laplace_digest256 request_fingerprint;
    laplace_digest256 forward_receipt_id;
    laplace_digest256 open_obligations_fingerprint;
    laplace_digest256 continuation_condition_fingerprint;
    laplace_machine_exception_receipt exception_receipt;
    laplace_machine_why_not why_not;
    uint32_t forward_disposition;
    uint32_t forward_completion;
    uint32_t version;
    uint32_t reserved;
} laplace_cognition_why_not_result;

/*
 * Convert a valid but semantically incomplete native cognition execution into
 * an exact typed WHY_NOT result. COMPLETE cognition is NOT_APPLICABLE. The
 * function reads the receipt/header/obligations owned by `forward_result`; a
 * caller-supplied receipt cannot manufacture incompleteness or completion.
 */
LAPLACE_API laplace_cognition_why_not_status
laplace_cognition_forward_why_not(
    const laplace_cognition_observation_request* request,
    const laplace_cognition_forward_result* forward_result,
    laplace_cognition_why_not_result* result);

#ifdef __cplusplus
}
#endif

#endif
