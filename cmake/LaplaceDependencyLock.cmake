find_package(Git REQUIRED)

function(laplace_verify_git_dependency dependency_name source_path lock_path)
    file(READ "${lock_path}" lock_json)
    string(JSON expected_revision GET "${lock_json}" dependencies "${dependency_name}" revision)
    string(JSON expected_archive_sha GET "${lock_json}" dependencies "${dependency_name}" git_archive_sha256)

    execute_process(
        COMMAND "${GIT_EXECUTABLE}"
            -c "safe.directory=${source_path}"
            -C "${source_path}" rev-parse HEAD
        RESULT_VARIABLE revision_result
        OUTPUT_VARIABLE actual_revision
        ERROR_VARIABLE revision_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT revision_result EQUAL 0)
        message(FATAL_ERROR
            "${dependency_name}: cannot resolve source revision: ${revision_error}")
    endif()
    if(NOT actual_revision STREQUAL expected_revision)
        message(FATAL_ERROR
            "${dependency_name}: revision ${actual_revision} does not match lock ${expected_revision}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}"
            -c "safe.directory=${source_path}"
            -C "${source_path}" status --porcelain=v1 --untracked-files=all
        RESULT_VARIABLE status_result
        OUTPUT_VARIABLE source_status
        ERROR_VARIABLE status_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT status_result EQUAL 0)
        message(FATAL_ERROR
            "${dependency_name}: cannot inspect source state: ${status_error}")
    endif()
    if(NOT source_status STREQUAL "")
        message(FATAL_ERROR
            "${dependency_name}: source tree contains changes and cannot enter the build")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}"
            -c "safe.directory=${source_path}"
            -C "${source_path}" archive --format=tar "${actual_revision}"
        COMMAND sha256sum
        RESULT_VARIABLE archive_result
        OUTPUT_VARIABLE archive_output
        ERROR_VARIABLE archive_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT archive_result EQUAL 0)
        message(FATAL_ERROR
            "${dependency_name}: cannot hash source archive: ${archive_error}")
    endif()
    string(REGEX MATCH "^[0-9a-f]+" actual_archive_sha "${archive_output}")
    if(NOT actual_archive_sha STREQUAL expected_archive_sha)
        message(FATAL_ERROR
            "${dependency_name}: source archive ${actual_archive_sha} does not match lock ${expected_archive_sha}")
    endif()

    string(JSON license_count LENGTH
        "${lock_json}" dependencies "${dependency_name}" licenses)
    if(license_count EQUAL 0)
        message(FATAL_ERROR "${dependency_name}: dependency lock has no license files")
    endif()
    math(EXPR last_license_index "${license_count} - 1")
    foreach(license_index RANGE 0 ${last_license_index})
        string(JSON license_path GET
            "${lock_json}" dependencies "${dependency_name}" licenses ${license_index} path)
        string(JSON expected_license_sha GET
            "${lock_json}" dependencies "${dependency_name}" licenses ${license_index} sha256)
        set(license_file "${source_path}/${license_path}")
        if(NOT EXISTS "${license_file}")
            message(FATAL_ERROR
                "${dependency_name}: locked license file is missing: ${license_path}")
        endif()
        file(SHA256 "${license_file}" actual_license_sha)
        if(NOT actual_license_sha STREQUAL expected_license_sha)
            message(FATAL_ERROR
                "${dependency_name}: license ${license_path} digest ${actual_license_sha} does not match lock ${expected_license_sha}")
        endif()
    endforeach()

    message(STATUS
        "Verified ${dependency_name}: revision=${actual_revision} archive_sha256=${actual_archive_sha}")
endfunction()
