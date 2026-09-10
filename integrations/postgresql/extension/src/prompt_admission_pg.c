#include "postgres.h"

#include <string.h>

#include "composition_pg.h"
#include "content_admission_pg.h"
#include "prompt_admission_pg.h"

void laplace_pg_prompt_atom_provider_create(
    const laplace_framework_context* context,
    laplace_pg_prompt_atom_provider_state* state,
    laplace_cognition_prompt_atom_provider_v1* provider) {
    laplace_content_atom_provider_v1 content_provider;

    if (provider == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace prompt atom provider output is null")));
    }

    memset(&content_provider, 0, sizeof(content_provider));
    laplace_pg_content_atom_provider_create(context, state, &content_provider);
    memset(provider, 0, sizeof(*provider));
    provider->state = content_provider.state;
    provider->provider_fingerprint = content_provider.provider_fingerprint;
    provider->resolve = content_provider.resolve;
    provider->abi_major = LAPLACE_COGNITION_PROMPT_ATOM_PROVIDER_ABI_MAJOR;
    provider->abi_minor = LAPLACE_COGNITION_PROMPT_ATOM_PROVIDER_ABI_MINOR;
}

void laplace_pg_prompt_admission_providers_create(
    const laplace_framework_context* context,
    laplace_pg_prompt_atom_provider_state* atom_state,
    laplace_cognition_prompt_atom_provider_v1* atom_provider,
    laplace_composition_presence_provider_v1* presence_provider) {
    if (presence_provider == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace prompt admission presence provider output is null")));
    }
    laplace_pg_prompt_atom_provider_create(context, atom_state, atom_provider);
    laplace_pg_composition_presence_provider(presence_provider);
}
