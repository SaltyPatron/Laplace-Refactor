add_executable(laplace_identity_inspect
    identity_inspect.c)
set_property(TARGET laplace_identity_inspect PROPERTY LINKER_LANGUAGE CXX)
target_link_libraries(laplace_identity_inspect PRIVATE Laplace::Identity)
target_compile_options(laplace_identity_inspect PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_resource_observe
    resource_observe.cpp)
target_link_libraries(laplace_resource_observe PRIVATE
    Laplace::Execution
    BLAKE3::blake3)
target_compile_options(laplace_resource_observe PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
set_target_properties(laplace_resource_observe PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH FALSE)
install(TARGETS laplace_resource_observe
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

add_executable(laplace_cognition_firmware_compile
    cognition_firmware_compile.cpp)
target_link_libraries(laplace_cognition_firmware_compile PRIVATE
    Laplace::Engine
    BLAKE3::blake3)
target_compile_options(laplace_cognition_firmware_compile PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
set_target_properties(laplace_cognition_firmware_compile PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH FALSE)
install(TARGETS laplace_cognition_firmware_compile
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

add_executable(laplace_source_profile_compile
    source_profile_compile.cpp)
target_include_directories(laplace_source_profile_compile PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_source_profile_compile PRIVATE
    Laplace::TabularSource)
target_compile_options(laplace_source_profile_compile PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
set_target_properties(laplace_source_profile_compile PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH FALSE)
install(TARGETS laplace_source_profile_compile
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

add_executable(laplace_unicode_activation_identify
    unicode_activation_identify.cpp)
target_link_libraries(laplace_unicode_activation_identify PRIVATE
    BLAKE3::blake3)
target_compile_options(laplace_unicode_activation_identify PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
install(TARGETS laplace_unicode_activation_identify
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

add_executable(laplace_unicode_decompose
    unicode_decompose.cpp)
target_link_libraries(laplace_unicode_decompose PRIVATE
    Laplace::Uax29
    Laplace::UnicodeRoot)
target_compile_options(laplace_unicode_decompose PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
set_target_properties(laplace_unicode_decompose PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH FALSE)
install(TARGETS laplace_unicode_decompose
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

add_executable(laplace_decompose
    decompose.cpp)
target_link_libraries(laplace_decompose PRIVATE
    Laplace::Decomposition
    Laplace::Uax29
    Laplace::UnicodeRoot
    BLAKE3::blake3)
target_compile_options(laplace_decompose PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
set_target_properties(laplace_decompose PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH FALSE)
install(TARGETS laplace_decompose
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

add_executable(laplace_ucdxml_query
    ucdxml_query.cpp)
target_link_libraries(laplace_ucdxml_query PRIVATE
    Laplace::Decomposition
    BLAKE3::blake3)
target_compile_options(laplace_ucdxml_query PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
set_target_properties(laplace_ucdxml_query PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH FALSE)
install(TARGETS laplace_ucdxml_query
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

add_executable(laplace_receipt_store
    receipt_store.cpp)
target_link_libraries(laplace_receipt_store PRIVATE
    BLAKE3::blake3)
target_compile_options(laplace_receipt_store PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
install(TARGETS laplace_receipt_store
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/model_export.py"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    RENAME laplace-model-export)
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/cognition_client.py"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    RENAME laplace-cognition)
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/cognition_service.py"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    RENAME laplace-cognition-service)
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/openai_api_service.py"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    RENAME laplace-openai-api)
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/laplace_inspect.py"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    RENAME laplace-inspect)
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/admit_source.py"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    RENAME laplace-admit-source)

if(BUILD_TESTING)
    add_test(
        NAME delivery.receipt-store-contract
        COMMAND "${CMAKE_COMMAND}"
            "-DSTORE=$<TARGET_FILE:laplace_receipt_store>"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/receipt-store-test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../tests/receipt_store_test.cmake")
    set_tests_properties(delivery.receipt-store-contract PROPERTIES
        LABELS "delivery;receipt;blake3;package;mutation")
endif()

add_executable(laplace_unicode_artifact_verify unicode_artifact_verify.cpp)
target_link_libraries(laplace_unicode_artifact_verify PRIVATE Laplace::Perfcache)
target_compile_options(laplace_unicode_artifact_verify PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
set_target_properties(laplace_unicode_artifact_verify PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    INSTALL_RPATH_USE_LINK_PATH FALSE)
install(TARGETS laplace_unicode_artifact_verify
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/live_substrate.py"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    RENAME laplace-live-substrate)
