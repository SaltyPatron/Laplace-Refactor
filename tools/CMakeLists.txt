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
