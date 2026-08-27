function(laplace_map_postgresql_path logical_path output_variable)
    if(NOT IS_ABSOLUTE "${logical_path}")
        message(FATAL_ERROR "PostgreSQL reported a non-absolute path: ${logical_path}")
    endif()
    if(LAPLACE_PG_PHYSICAL_ROOT)
        if(NOT IS_ABSOLUTE "${LAPLACE_PG_PHYSICAL_ROOT}"
           OR NOT IS_DIRECTORY "${LAPLACE_PG_PHYSICAL_ROOT}")
            message(FATAL_ERROR
                "LAPLACE_PG_PHYSICAL_ROOT must name an existing absolute staging root")
        endif()
        cmake_path(NORMAL_PATH LAPLACE_PG_PHYSICAL_ROOT
            OUTPUT_VARIABLE normalized_physical_root)
        cmake_path(IS_PREFIX normalized_physical_root "${logical_path}"
            NORMALIZE path_is_already_physical)
        if(path_is_already_physical)
            set(mapped_path "${logical_path}")
        else()
            string(REGEX REPLACE "^/" "" relative_logical_path "${logical_path}")
            set(mapped_path "${normalized_physical_root}/${relative_logical_path}")
        endif()
    else()
        set(mapped_path "${logical_path}")
    endif()
    if(NOT EXISTS "${mapped_path}")
        message(FATAL_ERROR
            "PostgreSQL logical path ${logical_path} is absent at physical path ${mapped_path}")
    endif()
    set(${output_variable} "${mapped_path}" PARENT_SCOPE)
endfunction()

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
    laplace_map_postgresql_path("${pg_includedir_server}" pg_includedir_server_physical)
    laplace_map_postgresql_path("${pg_includedir}" pg_includedir_physical)
    laplace_map_postgresql_path("${pg_bindir}" pg_bindir_physical)
    laplace_map_postgresql_path("${pg_pkglibdir}" pg_pkglibdir_physical)
    laplace_map_postgresql_path("${pg_sharedir}" pg_sharedir_physical)
    if(NOT EXISTS "${pg_includedir_server_physical}/postgres.h")
        message(FATAL_ERROR
            "PostgreSQL server headers are absent from ${pg_includedir_server_physical}")
    endif()

    set(LAPLACE_POSTGRES_ENABLED ON PARENT_SCOPE)
    set(LAPLACE_POSTGRES_VERSION "${pg_version}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_MAJOR "${pg_major}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_INCLUDE_SERVER "${pg_includedir_server_physical}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_INCLUDE_CLIENT "${pg_includedir_physical}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_BINDIR "${pg_bindir_physical}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_PKGLIBDIR "${pg_pkglibdir_physical}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_SHAREDIR "${pg_sharedir_physical}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_LOGICAL_BINDIR "${pg_bindir}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_LOGICAL_PKGLIBDIR "${pg_pkglibdir}" PARENT_SCOPE)
    set(LAPLACE_POSTGRES_LOGICAL_SHAREDIR "${pg_sharedir}" PARENT_SCOPE)
endfunction()
