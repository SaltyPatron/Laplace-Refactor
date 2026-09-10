#ifndef LAPLACE_POSTGRES_CONTENT_ADMISSION_PG_H
#define LAPLACE_POSTGRES_CONTENT_ADMISSION_PG_H

#include "laplace/content_admission.h"
#include "laplace/framework.h"

typedef struct laplace_pg_content_atom_provider_state {
    laplace_framework_context context;
    laplace_digest256 provider_fingerprint;
} laplace_pg_content_atom_provider_state;

/* Resolve the canonical Unicode floor through the active persistent Tier-0
 * generation.  The provider belongs to generic content admission; prompt,
 * source, document and future recipe adapters share it rather than owning
 * parallel atom-resolution SQL/identity paths. */
void laplace_pg_content_atom_provider_create(
    const laplace_framework_context* context,
    laplace_pg_content_atom_provider_state* state,
    laplace_content_atom_provider_v1* provider);

void laplace_pg_content_admission_providers_create(
    const laplace_framework_context* context,
    laplace_pg_content_atom_provider_state* atom_state,
    laplace_content_atom_provider_v1* atom_provider,
    laplace_composition_presence_provider_v1* presence_provider);

#endif
