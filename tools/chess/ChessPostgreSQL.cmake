# Qualification wiring only: semantic execution remains in the selected native
# adapter, common composition engine and its PostgreSQL extension.
set(native_targets "${LAPLACE_NATIVE_BUILD_ROOT}/tests/chess-postgres-targets.json")
if(NOT EXISTS "${native_targets}")
    message(FATAL_ERROR "The selected common build has no PostgreSQL target receipt")
endif()
file(READ "${native_targets}" native_target_json)
string(JSON native_target_schema GET "${native_target_json}" schema)
if(NOT native_target_schema STREQUAL "laplace.chess-postgres-native-targets/v1")
    message(FATAL_ERROR "Unknown native PostgreSQL target receipt")
endif()
foreach(field source_root native_engine blake3_library blake3_include pg_config
              pg_bindir pg_include control_root module source_probe sql unicode_root
              sanitizer_preload)
    string(JSON "selected_${field}" GET "${native_target_json}" "${field}")
endforeach()
if(NOT selected_source_root STREQUAL LAPLACE_REPOSITORY_ROOT)
    message(FATAL_ERROR "Native PostgreSQL targets belong to another checkout")
endif()
file(REAL_PATH "${selected_native_engine}" selected_native_engine)
file(REAL_PATH "${LAPLACE_NATIVE_ENGINE}" rules_native_engine)
if(NOT selected_native_engine STREQUAL rules_native_engine)
    message(FATAL_ERROR "Rules and PostgreSQL carriers selected different native engines")
endif()
foreach(path "${selected_native_engine}" "${selected_blake3_library}"
             "${selected_blake3_include}/blake3.h" "${selected_pg_config}"
             "${selected_pg_include}/libpq-fe.h" "${selected_module}"
             "${selected_source_probe}" "${selected_sql}"
             "${selected_control_root}/extension/laplace.control")
    if(NOT IS_ABSOLUTE "${path}" OR NOT EXISTS "${path}")
        message(FATAL_ERROR "Selected native PostgreSQL input is missing: ${path}")
    endif()
endforeach()
foreach(query version bindir includedir libdir)
    execute_process(COMMAND "${selected_pg_config}" "--${query}"
        RESULT_VARIABLE query_status OUTPUT_VARIABLE "pg_${query}"
        ERROR_VARIABLE query_error OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 10)
    if(NOT query_status EQUAL 0)
        message(FATAL_ERROR "Selected pg_config --${query} failed: ${query_error}")
    endif()
endforeach()
if(NOT pg_version MATCHES "^PostgreSQL 18([.]|$)"
   OR NOT pg_bindir STREQUAL selected_pg_bindir
   OR NOT pg_includedir STREQUAL selected_pg_include
   OR NOT IS_ABSOLUTE "${pg_libdir}" OR NOT IS_DIRECTORY "${pg_libdir}")
    message(FATAL_ERROR "Selected PostgreSQL client does not match the common native build")
endif()
find_file(selected_libpq_link NAMES libpq.so PATHS "${pg_libdir}" NO_DEFAULT_PATH NO_CACHE REQUIRED)
file(REAL_PATH "${selected_libpq_link}" selected_libpq)
file(REAL_PATH "${pg_libdir}" selected_pg_libdir)
cmake_path(IS_PREFIX selected_pg_libdir "${selected_libpq}" NORMALIZE libpq_in_selected_directory)
if(NOT libpq_in_selected_directory)
    message(FATAL_ERROR "Selected libpq link escapes its pg_config library directory")
endif()
add_library(laplace_chess_selected_libpq SHARED IMPORTED)
set_target_properties(laplace_chess_selected_libpq PROPERTIES
    IMPORTED_LOCATION "${selected_libpq}"
    INTERFACE_INCLUDE_DIRECTORIES "${selected_pg_include}")
add_library(laplace_chess_selected_blake3 STATIC IMPORTED)
set_target_properties(laplace_chess_selected_blake3 PROPERTIES
    IMPORTED_LOCATION "${selected_blake3_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${selected_blake3_include}")
add_executable(laplace_postgres_chess_line_probe
    "${LAPLACE_REPOSITORY_ROOT}/tests/postgres/chess_line_contract_probe.cpp")
target_link_libraries(laplace_postgres_chess_line_probe PRIVATE
    laplace_cutechess_trace_adapter laplace_chess_selected_libpq laplace_chess_selected_blake3)
target_compile_options(laplace_postgres_chess_line_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
get_filename_component(selected_engine_directory "${selected_native_engine}" DIRECTORY)
get_filename_component(selected_module_directory "${selected_module}" DIRECTORY)
set_target_properties(laplace_postgres_chess_line_probe PROPERTIES
    BUILD_RPATH "${LAPLACE_QT_PREFIX}/lib;${selected_pg_libdir};${selected_engine_directory}")
file(GENERATE OUTPUT "${LAPLACE_RULES_OUTPUT_ROOT}/postgres-line-linkage.json"
    CONTENT "{\n  \"schema\": \"laplace.chess-postgres-linkage/v1\",\n  \"pg_config\": \"${selected_pg_config}\",\n  \"libpq\": \"${selected_libpq}\",\n  \"libpq_header\": \"${selected_pg_include}/libpq-fe.h\",\n  \"blake3_library\": \"${selected_blake3_library}\",\n  \"blake3_header\": \"${selected_blake3_include}/blake3.h\",\n  \"module\": \"${selected_module}\",\n  \"source_probe\": \"${selected_source_probe}\",\n  \"probe\": \"$<TARGET_FILE:laplace_postgres_chess_line_probe>\",\n  \"sql\": \"${selected_sql}\"\n}\n")
find_program(LAPLACE_CHESS_BASH bash REQUIRED)
add_test(NAME postgres.chess-line-durable-replay-contract
    COMMAND "${CMAKE_COMMAND}" -E env
        "LAPLACE_UNICODE_SOURCE_ROOT=${selected_unicode_root}"
        "LAPLACE_CHESS_LINE_MANIFEST=${LAPLACE_REPOSITORY_ROOT}/tests/fixtures/cutechess-rules/manifest.json"
        "LAPLACE_CHESS_LINE_PGN=${LAPLACE_REPOSITORY_ROOT}/tests/fixtures/pgn-provider/upstream-117-ply.pgn"
        "LAPLACE_CHESS_LINE_NATIVE_ENGINE=${selected_native_engine}"
        "LAPLACE_CHESS_LINE_EVIDENCE_ROOT=${LAPLACE_RULES_OUTPUT_ROOT}"
        "LD_LIBRARY_PATH=${LAPLACE_QT_PREFIX}/lib:$ENV{LD_LIBRARY_PATH}"
        "${LAPLACE_CHESS_BASH}" "${LAPLACE_REPOSITORY_ROOT}/tests/postgres/run_spi_test.sh"
        chess-line "${selected_pg_bindir}" "${selected_control_root}"
        "${selected_module_directory}" "${selected_engine_directory}"
        "${selected_source_probe}" "$<TARGET_FILE:laplace_postgres_chess_line_probe>"
        "${selected_sql}" "${selected_sanitizer_preload}")
set_tests_properties(postgres.chess-line-durable-replay-contract PROPERTIES
    LABELS "implementation;chess;postgresql;composition;persistence;mutation"
    RUN_SERIAL TRUE TIMEOUT 900)
