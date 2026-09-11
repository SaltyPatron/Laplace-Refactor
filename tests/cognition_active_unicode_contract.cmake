file(READ "${SOURCE}" source)
file(READ "${UAX_HEADER}" header)
file(READ "${UAX_ENGINE}" engine)

foreach(forbidden IN ITEMS
    "LAPLACE_UNICODE_SOURCE_ROOT"
    "laplace_unicode_source_bundle_open"
    "laplace_uax29_tables_create(\n        owners->unicode_bundle")
    string(FIND "${source}" "${forbidden}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "live cognition still depends on source-filesystem UAX authority: ${forbidden}")
    endif()
endforeach()

foreach(required IN ITEMS
    "uax29_active_pg.h"
    "laplace_pg_uax29_tables_from_active_unicode"
    "uax_authority.activation_epoch_fingerprint"
    "product_uax29_fingerprint(&uax_authority)")
    string(FIND "${source}" "${required}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "live cognition is missing active Unicode binding: ${required}")
    endif()
endforeach()

foreach(required IN ITEMS
    "laplace_uax29_atom_table_builder_create"
    "laplace_uax29_atom_table_builder_consume"
    "laplace_uax29_atom_table_builder_finish")
    string(FIND "${header}" "${required}" header_position)
    string(FIND "${engine}" "${required}" engine_position)
    if(header_position EQUAL -1 OR engine_position EQUAL -1)
        message(FATAL_ERROR "atom-backed UAX API is incomplete: ${required}")
    endif()
endforeach()
