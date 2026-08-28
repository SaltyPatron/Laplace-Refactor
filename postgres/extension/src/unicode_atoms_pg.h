#ifndef LAPLACE_POSTGRES_UNICODE_ATOMS_PG_H
#define LAPLACE_POSTGRES_UNICODE_ATOMS_PG_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/composition.h"
#include "laplace/framework.h"

typedef struct laplace_pg_active_unicode_root {
    laplace_digest256 root_receipt;
    laplace_id128 activation_epoch_id;
    laplace_digest256 activation_epoch_fingerprint;
} laplace_pg_active_unicode_root;

void laplace_pg_resolve_active_unicode_atoms(
    const laplace_framework_context* context,
    const uint32_t* positions,
    size_t count,
    laplace_composition_known_entity* known,
    laplace_pg_active_unicode_root* active);

#endif
