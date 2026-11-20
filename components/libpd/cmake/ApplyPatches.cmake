# Also usable independently: cmake -DPD_SOURCE_DIR=/path/to/pure-data -P ...
if(NOT DEFINED PD_SOURCE_DIR)
    message(FATAL_ERROR "PD_SOURCE_DIR is required")
endif()
find_package(Git REQUIRED)
set(_pd_expected_revision "f009fd8d7b537e209e09898d487fdf1bf547da2b")
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PD_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE _pd_revision OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _pd_revision_result)
if(NOT _pd_revision_result EQUAL 0 OR NOT _pd_revision STREQUAL _pd_expected_revision)
    message(FATAL_ERROR "Pd optimisation patches require Pd 0.56-5 revision ${_pd_expected_revision}; found ${_pd_revision}")
endif()
set(_pd_patch_dir "${CMAKE_CURRENT_LIST_DIR}/../patches")
foreach(_pd_patch IN ITEMS
        0001-float-math.patch
        0002-float-oscillators.patch
        0003-float-table-index.patch
        0004-vline-spans.patch)
    set(_pd_patch_path "${_pd_patch_dir}/${_pd_patch}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PD_SOURCE_DIR}"
        apply --check "${_pd_patch_path}"
        RESULT_VARIABLE _pd_check OUTPUT_QUIET ERROR_VARIABLE _pd_error)
    if(_pd_check EQUAL 0)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PD_SOURCE_DIR}"
            apply "${_pd_patch_path}" RESULT_VARIABLE _pd_apply ERROR_VARIABLE _pd_error)
        if(NOT _pd_apply EQUAL 0)
            message(FATAL_ERROR "Cannot apply ${_pd_patch}: ${_pd_error}")
        endif()
        message(STATUS "Applied Pd patch: ${_pd_patch}")
    else()
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${PD_SOURCE_DIR}"
            apply --reverse --check "${_pd_patch_path}"
            RESULT_VARIABLE _pd_reverse OUTPUT_QUIET ERROR_QUIET)
        if(NOT _pd_reverse EQUAL 0)
            message(FATAL_ERROR "Pd patch ${_pd_patch} does not match the source (nor an already-applied patch). Preserve local edits and restore the pinned sources before retrying. ${_pd_error}")
        endif()
        message(STATUS "Pd patch already applied: ${_pd_patch}")
    endif()
endforeach()
