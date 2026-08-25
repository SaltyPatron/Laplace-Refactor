foreach(required_variable
        BUILD_DIRECTORY
        SOURCE_DIRECTORY
        INSTALL_DIRECTORY
        CONSUMER_BUILD_DIRECTORY)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} was not provided")
    endif()
endforeach()

if(NOT DEFINED CONSUMER_C_FLAGS)
    set(CONSUMER_C_FLAGS "")
endif()
if(NOT DEFINED CONSUMER_LINK_FLAGS)
    set(CONSUMER_LINK_FLAGS "")
endif()

function(installed_tree_fingerprint root output_variable)
    file(GLOB_RECURSE installed_files
        LIST_DIRECTORIES FALSE
        RELATIVE "${root}"
        "${root}/*")
    list(SORT installed_files)
    set(manifest "")
    foreach(installed_file IN LISTS installed_files)
        file(SHA256 "${root}/${installed_file}" file_digest)
        string(APPEND manifest "${installed_file}\t${file_digest}\n")
    endforeach()
    string(SHA256 tree_digest "${manifest}")
    set(${output_variable} "${tree_digest}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${INSTALL_DIRECTORY}" "${CONSUMER_BUILD_DIRECTORY}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIRECTORY}"
        --prefix "${INSTALL_DIRECTORY}"
    RESULT_VARIABLE first_install_result)
if(NOT first_install_result EQUAL 0)
    message(FATAL_ERROR "First native engine installation failed")
endif()
installed_tree_fingerprint("${INSTALL_DIRECTORY}" first_fingerprint)

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIRECTORY}"
        --prefix "${INSTALL_DIRECTORY}"
    RESULT_VARIABLE second_install_result)
if(NOT second_install_result EQUAL 0)
    message(FATAL_ERROR "Repeated native engine installation failed")
endif()
installed_tree_fingerprint("${INSTALL_DIRECTORY}" second_fingerprint)
if(NOT first_fingerprint STREQUAL second_fingerprint)
    message(FATAL_ERROR
        "Repeated native engine installation changed installed bytes")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${SOURCE_DIRECTORY}/tests/consumer"
        -B "${CONSUMER_BUILD_DIRECTORY}"
        -G Ninja
        "-DCMAKE_PREFIX_PATH=${INSTALL_DIRECTORY}"
        -DCMAKE_BUILD_TYPE=Release
        "-DCMAKE_C_FLAGS=${CONSUMER_C_FLAGS}"
        "-DCMAKE_EXE_LINKER_FLAGS=${CONSUMER_LINK_FLAGS}"
    RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Installed-package consumer configuration failed")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${CONSUMER_BUILD_DIRECTORY}"
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Installed-package consumer build failed")
endif()
execute_process(
    COMMAND "${CONSUMER_BUILD_DIRECTORY}/laplace_consumer"
    RESULT_VARIABLE consumer_result)
if(NOT consumer_result EQUAL 0)
    message(FATAL_ERROR
        "Installed-package consumer returned ${consumer_result}")
endif()
