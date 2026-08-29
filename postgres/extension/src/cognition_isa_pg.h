#ifndef LAPLACE_COGNITION_ISA_PG_H
#define LAPLACE_COGNITION_ISA_PG_H

#include <stddef.h>
#include <stdint.h>

#include "laplace/framework.h"
#include "laplace/isa.h"

void laplace_pg_cognition_execute_words(
    const laplace_framework_context* context,
    const uint32_t* request_words,
    size_t request_word_count,
    uint32_t* result_words,
    size_t result_word_capacity,
    size_t* result_word_count,
    laplace_isa_receipt* receipt);

#endif
