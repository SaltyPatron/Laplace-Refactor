#ifndef LAPLACE_GEOMETRY_H
#define LAPLACE_GEOMETRY_H

#include <stdint.h>

#include "laplace/export.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LAPLACE_GEOMETRY_COMPONENTS = 4,
    LAPLACE_GEOMETRY_ACCUMULATOR_LIMBS = 36
};

typedef struct laplace_point4d {
    double component[LAPLACE_GEOMETRY_COMPONENTS];
} laplace_point4d;

typedef struct laplace_exact_component {
    uint32_t magnitude[LAPLACE_GEOMETRY_ACCUMULATOR_LIMBS];
    int32_t sign;
} laplace_exact_component;

typedef struct laplace_geometry_accumulator {
    laplace_exact_component sum[LAPLACE_GEOMETRY_COMPONENTS];
    uint64_t logical_count;
} laplace_geometry_accumulator;

typedef struct laplace_geometry_summary {
    laplace_point4d centroid;
    double radius;
    uint64_t logical_count;
} laplace_geometry_summary;

typedef enum laplace_geometry_status {
    LAPLACE_GEOMETRY_OK = 0,
    LAPLACE_GEOMETRY_INVALID_ARGUMENT = 1,
    LAPLACE_GEOMETRY_EMPTY = 2,
    LAPLACE_GEOMETRY_ZERO_MULTIPLICITY = 3,
    LAPLACE_GEOMETRY_COUNT_OVERFLOW = 4,
    LAPLACE_GEOMETRY_COORDINATE_OUT_OF_RANGE = 5,
    LAPLACE_GEOMETRY_ACCUMULATOR_OVERFLOW = 6
} laplace_geometry_status;

LAPLACE_API void laplace_geometry_accumulator_init(
    laplace_geometry_accumulator* accumulator);

LAPLACE_API laplace_geometry_status laplace_geometry_accumulator_add(
    laplace_geometry_accumulator* accumulator,
    const laplace_point4d* point,
    uint64_t multiplicity);

LAPLACE_API laplace_geometry_status laplace_geometry_accumulator_merge(
    laplace_geometry_accumulator* accumulator,
    const laplace_geometry_accumulator* source);

LAPLACE_API laplace_geometry_status laplace_geometry_accumulator_finish(
    const laplace_geometry_accumulator* accumulator,
    laplace_geometry_summary* summary);

#ifdef __cplusplus
}
#endif

#endif
