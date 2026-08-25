function(laplace_discover_postgresql pg_config_path)
    if(NOT IS_ABSOLUTE "${pg_config_path}" OR NOT EXISTS "${pg_config_path}")
        message(FATAL_ERROR "LAPLACE_PG_CONFIG must name an absolute pg_config executable")
    endif()

    foreach(query version includedir-server includedir bindir pkglibdir sharedir)
        string(REPLACE "-" "_" variable_suffix "${query}")
        execute_process(
            COMMAND "${pg_config_path}" "--${query}"
            RESULT_VARIABLE query_status
            OUTPUT_VARIABLE query_output
            ERROR_VARIABLE query_error
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT query_status EQUAL 0 OR query_output STREQUAL "")
            message(FATAL_ERROR "pg_config --${query} failed: ${query_error}")
        endif()
        set("pg_${variable_suffix}" "${query_output}")
    endforeach()

    if(NOT pg_version MATCHES "^PostgreSQL ([0-9]+)(\\.[0-9]+)?")
        message(FATAL_ERROR "Unrecognized PostgreSQL version: ${pg_version}")
    endif()
    set(pg_major "${CMAKE_MATCH_1}")
    if(NOT pg_major EQUAL 18)
        message(FATAL_ERROR "The clean extension profile currently requires PostgreSQL 18")
    endif()
    if(NOT EXISTS "${pg_includedir_server}/postgres.h")
        message(FATAL_ERROR "PostgreSQL server headers are absent from ${pg_includedir_server}")
    endif()

    set(LAPLACE_POSTGRES_ENABLED ON PARENT_SCOPE)
    set(LAPLACE_POSTGRES_VERSION "${pg_version}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_MAJOR "${pg_major}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_INCLUDE_SERVER "${pg_includedir_server}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_INCLUDE_CLIENT "${pg_includedir}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_BINDIR "${pg_bindir}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_PKGLIBDIR "${pg_pkglibdir}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_SHAREDIR "${pg_sharedir}" PARENT_SCOPE)
endfunction()
