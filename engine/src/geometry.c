#include "laplace/geometry.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

typedef struct laplace_biguint {
    uint32_t limb[LAPLACE_GEOMETRY_ACCUMULATOR_LIMBS];
} laplace_biguint;

static int big_is_zero(const laplace_biguint* value) {
    size_t index;
    for (index = 0; index < LAPLACE_GEOMETRY_ACCUMULATOR_LIMBS; ++index) {
        if (value->limb[index] != 0u) {
            return 0;
        }
    }
    return 1;
}

static int big_compare(const laplace_biguint* left, const laplace_biguint* right) {
    size_t index = LAPLACE_GEOMETRY_ACCUMULATOR_LIMBS;
    while (index != 0u) {
        --index;
        if (left->limb[index] < right->limb[index]) {
            return -1;
        }
        if (left->limb[index] > right->limb[index]) {
            return 1;
        }
    }
    return 0;
}

static int big_add(laplace_biguint* target, const laplace_biguint* addend) {
    uint64_t carry = 0;
    size_t index;
    for (index = 0; index < LAPLACE_GEOMETRY_ACCUMULATOR_LIMBS; ++index) {
        const uint64_t total = (uint64_t)target->limb[index] +
            (uint64_t)addend->limb[index] + carry;
        target->limb[index] = (uint32_t)total;
        carry = total >> 32;
    }
    return carry == 0u;
}

static void big_subtract(laplace_biguint* target, const laplace_biguint* subtrahend) {
    uint64_t borrow = 0;
    size_t index;
    for (index = 0; index < LAPLACE_GEOMETRY_ACCUMULATOR_LIMBS; ++index) {
        const uint64_t subtrahend_value =
            (uint64_t)subtrahend->limb[index] + borrow;
        const uint64_t target_value = target->limb[index];
        target->limb[index] = (uint32_t)(target_value - subtrahend_value);
        borrow = target_value < subtrahend_value ? 1u : 0u;
    }
}

static int big_high_bit(const laplace_biguint* value) {
    size_t index = LAPLACE_GEOMETRY_ACCUMULATOR_LIMBS;
    while (index != 0u) {
        uint32_t limb;
        int bit = 31;
        --index;
        limb = value->limb[index];
        if (limb == 0u) {
            continue;
        }
        while ((limb & (UINT32_C(1) << (uint32_t)bit)) == 0u) {
            --bit;
        }
        return (int)(index * 32u) + bit;
    }
    return -1;
}

static int big_shift_left_one(
    const laplace_biguint* source,
    laplace_biguint* result) {
    uint32_t carry = 0;
    size_t index;
    for (index = 0; index < LAPLACE_GEOMETRY_ACCUMULATOR_LIMBS; ++index) {
        const uint32_t next_carry = source->limb[index] >> 31;
        result->limb[index] = (source->limb[index] << 1) | carry;
        carry = next_carry;
    }
    return carry == 0u;
}

static int big_multiply_shift(
    uint64_t left,
    uint64_t right,
    uint32_t shift,
    laplace_biguint* result) {
    uint32_t left_part[2];
    uint32_t right_part[2];
    uint32_t product[5] = {0u, 0u, 0u, 0u, 0u};
    const size_t word_shift = (size_t)(shift / 32u);
    const uint32_t bit_shift = shift % 32u;
    size_t left_index;
    size_t product_index;
    uint64_t carry = 0;

    memset(result, 0, sizeof(*result));
    left_part[0] = (uint32_t)left;
    left_part[1] = (uint32_t)(left >> 32);
    right_part[0] = (uint32_t)right;
    right_part[1] = (uint32_t)(right >> 32);

    for (left_index = 0; left_index < 2u; ++left_index) {
        size_t right_index;
        carry = 0;
        for (right_index = 0; right_index < 2u; ++right_index) {
            const size_t index = left_index + right_index;
            const uint64_t total = (uint64_t)left_part[left_index] *
                (uint64_t)right_part[right_index] +
                (uint64_t)product[index] + carry;
            product[index] = (uint32_t)total;
            carry = total >> 32;
        }
        product_index = left_index + 2u;
        while (carry != 0u) {
            const uint64_t total = (uint64_t)product[product_index] + carry;
            product[product_index] = (uint32_t)total;
            carry = total >> 32;
            ++product_index;
            if (product_index == 5u && carry != 0u) {
                return 0;
            }
        }
    }

    carry = 0;
    for (product_index = 0; product_index < 5u; ++product_index) {
        const size_t output_index = word_shift + product_index;
        const uint64_t shifted = ((uint64_t)product[product_index] << bit_shift) | carry;
        if (output_index >= LAPLACE_GEOMETRY_ACCUMULATOR_LIMBS) {
            if (shifted != 0u) {
                return 0;
            }
        } else {
            result->limb[output_index] = (uint32_t)shifted;
        }
        carry = shifted >> 32;
    }
    if (carry != 0u) {
        const size_t output_index = word_shift + 5u;
        if (output_index >= LAPLACE_GEOMETRY_ACCUMULATOR_LIMBS) {
            return 0;
        }
        result->limb[output_index] = (uint32_t)carry;
    }
    return 1;
}

static int exact_component_valid(const laplace_exact_component* component) {
    laplace_biguint magnitude;
    memcpy(&magnitude, component->magnitude, sizeof(magnitude));
    if (component->sign < -1 || component->sign > 1) {
        return 0;
    }
    if (component->sign == 0) {
        return big_is_zero(&magnitude);
    }
    return !big_is_zero(&magnitude);
}

static int exact_component_add(
    laplace_exact_component* target,
    const laplace_biguint* magnitude,
    int32_t sign) {
    laplace_biguint target_magnitude;
    int comparison;

    if (big_is_zero(magnitude)) {
        return 1;
    }
    memcpy(&target_magnitude, target->magnitude, sizeof(target_magnitude));
    comparison = big_compare(&target_magnitude, magnitude);
    if (target->sign == 0) {
        memcpy(target->magnitude, magnitude, sizeof(*magnitude));
        target->sign = sign;
        return 1;
    }
    if (target->sign == sign) {
        if (!big_add(&target_magnitude, magnitude)) {
            return 0;
        }
        memcpy(target->magnitude, &target_magnitude, sizeof(target_magnitude));
        return 1;
    }
    if (comparison == 0) {
        memset(target->magnitude, 0, sizeof(target->magnitude));
        target->sign = 0;
        return 1;
    }
    if (comparison > 0) {
        big_subtract(&target_magnitude, magnitude);
        memcpy(target->magnitude, &target_magnitude, sizeof(target_magnitude));
        return 1;
    }

    target_magnitude = *magnitude;
    big_subtract(&target_magnitude, (const laplace_biguint*)target->magnitude);
    memcpy(target->magnitude, &target_magnitude, sizeof(target_magnitude));
    target->sign = sign;
    return 1;
}

static laplace_geometry_status point_term(
    double value,
    uint64_t multiplicity,
    laplace_biguint* magnitude,
    int32_t* sign) {
    uint64_t bits;
    uint64_t fraction;
    uint64_t significand;
    uint32_t exponent;
    uint32_t shift;

    memcpy(&bits, &value, sizeof(bits));
    fraction = bits & UINT64_C(0x000fffffffffffff);
    exponent = (uint32_t)((bits >> 52) & UINT64_C(0x7ff));
    *sign = (bits >> 63) == 0u ? 1 : -1;

    if (exponent == UINT32_C(0x7ff)) {
        return LAPLACE_GEOMETRY_COORDINATE_OUT_OF_RANGE;
    }
    if (exponent > UINT32_C(1023) ||
        (exponent == UINT32_C(1023) && fraction != 0u)) {
        return LAPLACE_GEOMETRY_COORDINATE_OUT_OF_RANGE;
    }
    if (exponent == 0u) {
        significand = fraction;
        shift = 0u;
    } else {
        significand = UINT64_C(0x0010000000000000) | fraction;
        shift = exponent - 1u;
    }
    if (significand == 0u) {
        memset(magnitude, 0, sizeof(*magnitude));
        *sign = 0;
        return LAPLACE_GEOMETRY_OK;
    }
    if (!big_multiply_shift(significand, multiplicity, shift, magnitude)) {
        return LAPLACE_GEOMETRY_ACCUMULATOR_OVERFLOW;
    }
    return LAPLACE_GEOMETRY_OK;
}

static uint64_t big_quotient_bounded(
    const laplace_biguint* numerator,
    uint64_t divisor,
    uint32_t shift,
    uint64_t lower,
    uint64_t upper) {
    uint64_t low = lower;
    uint64_t high = upper;
    while (low < high) {
        const uint64_t midpoint = low + ((high - low) / 2u) + 1u;
        laplace_biguint product;
        const int made_product = big_multiply_shift(
            divisor, midpoint, shift, &product);
        if (made_product && big_compare(&product, numerator) <= 0) {
            low = midpoint;
        } else {
            high = midpoint - 1u;
        }
    }
    return low;
}

static int uint64_high_bit(uint64_t value) {
    int bit = -1;
    while (value != 0u) {
        value >>= 1;
        ++bit;
    }
    return bit;
}

static uint64_t rounded_quotient(
    const laplace_biguint* numerator,
    uint64_t divisor,
    uint32_t shift,
    uint64_t lower,
    uint64_t upper) {
    uint64_t quotient = big_quotient_bounded(
        numerator, divisor, shift, lower, upper);
    laplace_biguint twice_numerator;
    laplace_biguint midpoint;
    const uint64_t midpoint_multiplier = quotient * 2u + 1u;
    int comparison;

    if (!big_shift_left_one(numerator, &twice_numerator) ||
        !big_multiply_shift(divisor, midpoint_multiplier, shift, &midpoint)) {
        return quotient;
    }
    comparison = big_compare(&twice_numerator, &midpoint);
    if (comparison > 0 || (comparison == 0 && (quotient & 1u) != 0u)) {
        ++quotient;
    }
    return quotient;
}

static double exact_component_mean(
    const laplace_exact_component* component,
    uint64_t count) {
    laplace_biguint numerator_value;
    const laplace_biguint* numerator = &numerator_value;
    laplace_biguint minimum_normal;
    uint64_t result_bits;

    memcpy(&numerator_value, component->magnitude, sizeof(numerator_value));
    if (component->sign == 0) {
        return 0.0;
    }
    if (!big_multiply_shift(count, UINT64_C(1), 52u, &minimum_normal)) {
        return 0.0;
    }
    if (big_compare(numerator, &minimum_normal) < 0) {
        const uint64_t significand = rounded_quotient(
            numerator, count, 0u, 0u, UINT64_C(0x0010000000000000));
        if (significand == 0u) {
            result_bits = 0u;
        } else if (significand == UINT64_C(0x0010000000000000)) {
            result_bits = UINT64_C(0x0010000000000000);
        } else {
            result_bits = significand;
        }
    } else {
        int ratio_high_bit = big_high_bit(numerator) - uint64_high_bit(count);
        laplace_biguint threshold;
        uint32_t shift;
        uint64_t significand;
        uint64_t exponent_bits;

        if (!big_multiply_shift(count, UINT64_C(1),
                                (uint32_t)ratio_high_bit, &threshold)) {
            return 0.0;
        }
        if (big_compare(numerator, &threshold) < 0) {
            --ratio_high_bit;
        }
        shift = (uint32_t)(ratio_high_bit - 52);
        significand = rounded_quotient(
            numerator,
            count,
            shift,
            UINT64_C(0x0010000000000000),
            UINT64_C(0x001fffffffffffff));
        if (significand == UINT64_C(0x0020000000000000)) {
            significand = UINT64_C(0x0010000000000000);
            ++ratio_high_bit;
        }
        exponent_bits = (uint64_t)(ratio_high_bit - 51);
        result_bits = (exponent_bits << 52) |
            (significand - UINT64_C(0x0010000000000000));
    }
    if (component->sign < 0 && result_bits != 0u) {
        result_bits |= UINT64_C(0x8000000000000000);
    }
    {
        double result;
        memcpy(&result, &result_bits, sizeof(result));
        return result;
    }
}

void laplace_geometry_accumulator_init(laplace_geometry_accumulator* accumulator) {
    if (accumulator != NULL) {
        memset(accumulator, 0, sizeof(*accumulator));
    }
}

laplace_geometry_status laplace_geometry_accumulator_add(
    laplace_geometry_accumulator* accumulator,
    const laplace_point4d* point,
    uint64_t multiplicity) {
    laplace_biguint term[LAPLACE_GEOMETRY_COMPONENTS];
    int32_t sign[LAPLACE_GEOMETRY_COMPONENTS];
    laplace_geometry_accumulator next;
    size_t component;

    if (accumulator == NULL || point == NULL) {
        return LAPLACE_GEOMETRY_INVALID_ARGUMENT;
    }
    if (multiplicity == 0u) {
        return LAPLACE_GEOMETRY_ZERO_MULTIPLICITY;
    }
    if (UINT64_MAX - accumulator->logical_count < multiplicity) {
        return LAPLACE_GEOMETRY_COUNT_OVERFLOW;
    }
    for (component = 0; component < LAPLACE_GEOMETRY_COMPONENTS; ++component) {
        const laplace_geometry_status status = point_term(
            point->component[component], multiplicity, &term[component], &sign[component]);
        if (status != LAPLACE_GEOMETRY_OK) {
            return status;
        }
        if (!exact_component_valid(&accumulator->sum[component])) {
            return LAPLACE_GEOMETRY_INVALID_ARGUMENT;
        }
    }

    next = *accumulator;
    for (component = 0; component < LAPLACE_GEOMETRY_COMPONENTS; ++component) {
        if (!exact_component_add(&next.sum[component], &term[component], sign[component])) {
            return LAPLACE_GEOMETRY_ACCUMULATOR_OVERFLOW;
        }
    }
    next.logical_count += multiplicity;
    *accumulator = next;
    return LAPLACE_GEOMETRY_OK;
}

laplace_geometry_status laplace_geometry_accumulator_merge(
    laplace_geometry_accumulator* accumulator,
    const laplace_geometry_accumulator* source) {
    laplace_geometry_accumulator next;
    size_t component;

    if (accumulator == NULL || source == NULL) {
        return LAPLACE_GEOMETRY_INVALID_ARGUMENT;
    }
    if (UINT64_MAX - accumulator->logical_count < source->logical_count) {
        return LAPLACE_GEOMETRY_COUNT_OVERFLOW;
    }
    next = *accumulator;
    for (component = 0; component < LAPLACE_GEOMETRY_COMPONENTS; ++component) {
        const laplace_exact_component* source_component = &source->sum[component];
        laplace_biguint source_magnitude;
        if (!exact_component_valid(&next.sum[component]) ||
            !exact_component_valid(source_component)) {
            return LAPLACE_GEOMETRY_INVALID_ARGUMENT;
        }
        memcpy(&source_magnitude,
               source_component->magnitude,
               sizeof(source_magnitude));
        if (!exact_component_add(
                &next.sum[component],
                &source_magnitude,
                source_component->sign)) {
            return LAPLACE_GEOMETRY_ACCUMULATOR_OVERFLOW;
        }
    }
    next.logical_count += source->logical_count;
    *accumulator = next;
    return LAPLACE_GEOMETRY_OK;
}

laplace_geometry_status laplace_geometry_accumulator_finish(
    const laplace_geometry_accumulator* accumulator,
    laplace_geometry_summary* summary) {
    laplace_geometry_summary result;
    size_t component;
    double squared_radius;

    if (accumulator == NULL || summary == NULL) {
        return LAPLACE_GEOMETRY_INVALID_ARGUMENT;
    }
    if (accumulator->logical_count == 0u) {
        return LAPLACE_GEOMETRY_EMPTY;
    }
    for (component = 0; component < LAPLACE_GEOMETRY_COMPONENTS; ++component) {
        if (!exact_component_valid(&accumulator->sum[component])) {
            return LAPLACE_GEOMETRY_INVALID_ARGUMENT;
        }
        result.centroid.component[component] = exact_component_mean(
            &accumulator->sum[component], accumulator->logical_count);
    }
    result.logical_count = accumulator->logical_count;
    squared_radius =
        result.centroid.component[0] * result.centroid.component[0] +
        result.centroid.component[1] * result.centroid.component[1] +
        result.centroid.component[2] * result.centroid.component[2] +
        result.centroid.component[3] * result.centroid.component[3];
    result.radius = sqrt(squared_radius);
#if defined(LAPLACE_TEST_NORMALIZE_CENTROID)
    if (result.radius != 0.0) {
        for (component = 0; component < LAPLACE_GEOMETRY_COMPONENTS; ++component) {
            result.centroid.component[component] /= result.radius;
        }
        result.radius = 1.0;
    }
#endif
    *summary = result;
    return LAPLACE_GEOMETRY_OK;
}
