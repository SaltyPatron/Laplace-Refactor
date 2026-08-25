add_executable(laplace_identity_inspect
    identity_inspect.c)
target_link_libraries(laplace_identity_inspect PRIVATE Laplace::Identity)
target_compile_options(laplace_identity_inspect PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
