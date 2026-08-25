function(laplace_configure_dotnet_bindings contract_path generator_path project_path)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_program(LAPLACE_DOTNET_EXECUTABLE NAMES dotnet REQUIRED)
    execute_process(
        COMMAND "${LAPLACE_DOTNET_EXECUTABLE}" --version
        OUTPUT_VARIABLE dotnet_version
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE dotnet_version_result)
    if(NOT dotnet_version_result EQUAL 0 OR NOT dotnet_version MATCHES "^10\\.")
        message(FATAL_ERROR
            "LAPLACE_ENABLE_DOTNET_BINDINGS requires .NET SDK 10; found '${dotnet_version}'")
    endif()

    set(generated_source
        "${CMAKE_CURRENT_BINARY_DIR}/generated/dotnet/IsaContract.g.cs")
    set(managed_root "${CMAKE_CURRENT_BINARY_DIR}/managed")
    set(managed_dll
        "${managed_root}/Laplace.Managed/bin/Release/net10.0/Laplace.Managed.dll")
    set(nuget_config "${CMAKE_CURRENT_SOURCE_DIR}/managed/NuGet.Config")

    add_custom_command(
        OUTPUT "${generated_source}"
        COMMAND "${Python3_EXECUTABLE}" "${generator_path}"
            --contract "${contract_path}"
            --output "${generated_source}"
        DEPENDS "${contract_path}" "${generator_path}"
        COMMENT "Generating the contract-owned .NET ISA surface"
        VERBATIM)
    add_custom_target(laplace_dotnet_generate DEPENDS "${generated_source}")

    add_custom_command(
        OUTPUT "${managed_dll}"
        COMMAND "${CMAKE_COMMAND}" -E env
            "DOTNET_CLI_TELEMETRY_OPTOUT=1"
            "DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1"
            "NUGET_PACKAGES=${managed_root}/packages"
            "${LAPLACE_DOTNET_EXECUTABLE}" build "${project_path}"
            --configuration Release
            --nologo
            "-p:GeneratedIsaSource=${generated_source}"
            "-p:LaplaceManagedBuildRoot=${managed_root}"
            "-p:RestoreConfigFile=${nuget_config}"
        DEPENDS
            "${generated_source}"
            "${project_path}"
            "${CMAKE_CURRENT_SOURCE_DIR}/Directory.Build.props"
            "${CMAKE_CURRENT_SOURCE_DIR}/managed/Laplace.Managed/Abi.cs"
            "${CMAKE_CURRENT_SOURCE_DIR}/managed/Laplace.Managed/Transport.cs"
            "${nuget_config}"
        COMMENT "Building dependency-free .NET 10 ISA bindings"
        VERBATIM)
    add_custom_target(laplace_dotnet_bindings ALL DEPENDS "${managed_dll}")
    add_dependencies(laplace_dotnet_bindings laplace_dotnet_generate)

    set(LAPLACE_DOTNET_EXECUTABLE "${LAPLACE_DOTNET_EXECUTABLE}" PARENT_SCOPE)
    set(LAPLACE_DOTNET_GENERATED_SOURCE "${generated_source}" PARENT_SCOPE)
    set(LAPLACE_DOTNET_MANAGED_ROOT "${managed_root}" PARENT_SCOPE)
    set(LAPLACE_DOTNET_MANAGED_DLL "${managed_dll}" PARENT_SCOPE)
    set(LAPLACE_DOTNET_NUGET_CONFIG "${nuget_config}" PARENT_SCOPE)
endfunction()
