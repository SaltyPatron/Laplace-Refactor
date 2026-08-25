#include "laplace/geometry.h"

#include <cmath>
#include <cstdio>

int main() {
    const laplace_point4d x{{1.0, 0.0, 0.0, 0.0}};
    const laplace_point4d y{{0.0, 1.0, 0.0, 0.0}};
    laplace_geometry_accumulator accumulator{};
    laplace_geometry_summary summary{};
    laplace_geometry_accumulator_init(&accumulator);
    if (laplace_geometry_accumulator_add(&accumulator, &x, 1u) !=
            LAPLACE_GEOMETRY_OK ||
        laplace_geometry_accumulator_add(&accumulator, &y, 1u) !=
            LAPLACE_GEOMETRY_OK ||
        laplace_geometry_accumulator_finish(&accumulator, &summary) !=
            LAPLACE_GEOMETRY_OK) {
        std::fputs("geometry-contract-execution-error\n", stderr);
        return 4;
    }
    if (summary.radius == 1.0 &&
        (summary.centroid.component[0] != 0.5 ||
         summary.centroid.component[1] != 0.5)) {
        std::fputs("geometry-centroid-normalized\n", stderr);
        return 2;
    }
    if (summary.centroid.component[0] != 0.5 ||
        summary.centroid.component[1] != 0.5 ||
        summary.centroid.component[2] != 0.0 ||
        summary.centroid.component[3] != 0.0 ||
        summary.radius != std::sqrt(0.5)) {
        std::fputs("geometry-contract-value-mismatch\n", stderr);
        return 3;
    }
    return 0;
}
