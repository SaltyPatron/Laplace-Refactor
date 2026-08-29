# Source admission requires the same verified Unicode 17 source estate used by
# the Unicode product until the UAX #29 property module is materialized into the
# active perfcache epoch.
add_compile_definitions(
    LAPLACE_UNICODE_SOURCE_ROOT="${LAPLACE_UNICODE_SOURCE_ROOT}")

include("${CMAKE_CURRENT_LIST_DIR}/CMakeLists.legacy.cmake")
