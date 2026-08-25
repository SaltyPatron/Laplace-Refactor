function(laplace_require_external_output source_directory binary_directory)
    file(RELATIVE_PATH output_relative_to_source
        "${source_directory}" "${binary_directory}")
    if(NOT IS_ABSOLUTE "${output_relative_to_source}"
       AND NOT output_relative_to_source MATCHES "^\\.\\./")
        message(FATAL_ERROR
            "Laplace build output must remain outside the repository; use /opt/laplace or a runner-temporary root")
    endif()
endfunction()
