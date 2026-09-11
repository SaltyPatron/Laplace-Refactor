#ifndef LAPLACE_POSTGRES_PROMPT_ADMISSION_PG_H
#define LAPLACE_POSTGRES_PROMPT_ADMISSION_PG_H

#include "content_admission_pg.h"
#include "laplace/cognition_prompt_admission.h"

/* Compatibility adapter only. Prompt admission no longer owns Tier-0 atom
 * resolution; it consumes the generic PostgreSQL content-admission provider. */
typedef laplace_pg_content_atom_provider_state
    laplace_pg_prompt_atom_provider_state;

void laplace_pg_prompt_atom_provider_create(
    const laplace_framework_context* context,
    laplace_pg_prompt_atom_provider_state* state,
    laplace_cognition_prompt_atom_provider_v1* provider);

void laplace_pg_prompt_admission_providers_create(
    const laplace_framework_context* context,
    laplace_pg_prompt_atom_provider_state* atom_state,
    laplace_cognition_prompt_atom_provider_v1* atom_provider,
    laplace_composition_presence_provider_v1* presence_provider);

#endif
