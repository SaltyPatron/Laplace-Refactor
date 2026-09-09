#ifndef LAPLACE_POSTGRES_PROMPT_ADMISSION_PG_H
#define LAPLACE_POSTGRES_PROMPT_ADMISSION_PG_H

#include "laplace/cognition_prompt_admission.h"
#include "laplace/framework.h"

/* Real installed Unicode-floor provider for raw prompt admission. The descriptor
 * is caller-owned; its state copies the framework context so prompt admission
 * resolves codepoints through the active mapped Tier-0 generation instead of a
 * test alphabet or a second identity implementation. */
typedef struct laplace_pg_prompt_atom_provider_state {
    laplace_framework_context context;
    laplace_digest256 provider_fingerprint;
} laplace_pg_prompt_atom_provider_state;

void laplace_pg_prompt_atom_provider_create(
    const laplace_framework_context* context,
    laplace_pg_prompt_atom_provider_state* state,
    laplace_cognition_prompt_atom_provider_v1* provider);

#endif
