# Publish transport only. The native library owns all Laplace operations.
function(laplace_configure_source_admission)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The source admission installation requires the Linux file provider")
    endif()
    if(NOT CMAKE_INSTALL_BINDIR STREQUAL "bin" OR NOT CMAKE_INSTALL_DATADIR STREQUAL "share")
        message(FATAL_ERROR "Source admission uses the canonical bin/share package layout")
    endif()
    set(runtime_version "${LAPLACE_DOTNET_RUNTIME_VERSION}")
    if(NOT runtime_version MATCHES "^10\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR "Source admission requires an explicit .NET 10 runtime version")
    endif()
    file(REAL_PATH "${LAPLACE_DOTNET_EXECUTABLE}" dotnet_host)
    get_filename_component(dotnet_root "${dotnet_host}" DIRECTORY)
    set(runtime_root "${dotnet_root}/shared/Microsoft.NETCore.App/${runtime_version}")
    set(hostfxr_root "${dotnet_root}/host/fxr/${runtime_version}")
    foreach(required
            "${runtime_root}/libcoreclr.so" "${hostfxr_root}/libhostfxr.so"
            "${dotnet_root}/LICENSE.txt" "${dotnet_root}/ThirdPartyNotices.txt")
        if(NOT EXISTS "${required}")
            message(FATAL_ERROR "Selected .NET runtime input is absent: ${required}")
        endif()
    endforeach()
    set(project "${CMAKE_CURRENT_SOURCE_DIR}/managed/Laplace.SourceAdmission/Laplace.SourceAdmission.csproj")
    set(publish_root "${LAPLACE_DOTNET_MANAGED_ROOT}/source-admission-publish")
    file(GLOB source_contracts CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/contracts/sources/*.json")
    file(GLOB transport_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/managed/Laplace.SourceAdmission/*.cs")
    file(GLOB transport_sql CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/managed/Laplace.SourceAdmission/sql/*.sql")
    add_custom_command(
        OUTPUT "${publish_root}/laplace-source-admit.dll"
        COMMAND "${CMAKE_COMMAND}" -E env
            "DOTNET_CLI_TELEMETRY_OPTOUT=1"
            "DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1"
            "NUGET_PACKAGES=${LAPLACE_DOTNET_MANAGED_ROOT}/packages"
            "${LAPLACE_DOTNET_EXECUTABLE}" publish "${project}"
            --configuration Release --nologo --self-contained false
            --output "${publish_root}"
            "-p:UseAppHost=false"
            "-p:RuntimeFrameworkVersion=${runtime_version}"
            "-p:RollForward=Disable"
            "-p:GeneratedIsaSource=${LAPLACE_DOTNET_GENERATED_SOURCE}"
            "-p:GeneratedHighwaySource=${LAPLACE_DOTNET_HIGHWAY_SOURCE}"
            "-p:LaplaceManagedBuildRoot=${LAPLACE_DOTNET_MANAGED_ROOT}"
            "-p:RestoreConfigFile=${LAPLACE_DOTNET_NUGET_CONFIG}"
        DEPENDS "${project}" ${transport_sources} ${transport_sql} ${source_contracts}
            "${LAPLACE_DOTNET_MANAGED_DLL}"
            "${LAPLACE_DOTNET_NUGET_CONFIG}"
            "${CMAKE_CURRENT_SOURCE_DIR}/Directory.Build.props"
            "${CMAKE_CURRENT_SOURCE_DIR}/contracts/framework.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/contracts/postgresql-cluster.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/contracts/source-profile-execution.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/contracts/unicode-product-activation.json"
        COMMENT "Publishing the C# source transport over the shared native engine"
        VERBATIM)
    add_custom_target(laplace_source_admission_transport ALL
        DEPENDS "${publish_root}/laplace-source-admit.dll")
    add_dependencies(laplace_source_admission_transport laplace_dotnet_bindings)
    add_executable(laplace_source_admit tools/sources/source_admit_main.c)
    set_target_properties(laplace_source_admit PROPERTIES OUTPUT_NAME "laplace-source-admit")
    add_dependencies(laplace_source_admit laplace_source_admission_transport)
    install(TARGETS laplace_source_admit RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    set(destination "${CMAKE_INSTALL_DATADIR}/laplace/source-admission")
    install(DIRECTORY "${publish_root}/" DESTINATION "${destination}")
    # Carry the selected runtime and notices, without deploying the SDK or
    # requiring a machine-wide dotnet installation at execution time.
    install(PROGRAMS "${dotnet_host}" DESTINATION "${destination}/runtime")
    install(FILES "${dotnet_root}/LICENSE.txt" "${dotnet_root}/ThirdPartyNotices.txt"
        DESTINATION "${destination}/runtime")
    install(DIRECTORY "${runtime_root}/"
        DESTINATION "${destination}/runtime/shared/Microsoft.NETCore.App/${runtime_version}")
    install(DIRECTORY "${hostfxr_root}/" DESTINATION "${destination}/runtime/host/fxr/${runtime_version}")
    install(FILES tools/sources/README.md DESTINATION "${destination}")
endfunction()
