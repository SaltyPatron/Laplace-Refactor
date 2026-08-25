#include "laplace/unicode_root.h"

#include <string.h>

laplace_unicode_status laplace_unicode_numeric_oneapi_provider(
    laplace_unicode_numeric_provider_v1* provider) {
    if (provider == NULL) {
        return LAPLACE_UNICODE_INVALID_ARGUMENT;
    }
    memset(provider, 0, sizeof(*provider));
    return LAPLACE_UNICODE_PROVIDER_UNAVAILABLE;
}
