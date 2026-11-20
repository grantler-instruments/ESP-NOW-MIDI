cmake_minimum_required(VERSION 3.25)
project(enomik_pd_dsp_test C)
if(NOT LIBPD_SOURCE_DIR)
    get_filename_component(LIBPD_SOURCE_DIR "../../components/libpd/libpd" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
option(ENOMIK_FLOAT_DSP "Test the optimised routines" OFF)
set(PD_EXTRA OFF CACHE BOOL "" FORCE)
set(PD_UTILS OFF CACHE BOOL "" FORCE)
set(LIBPD_STATIC OFF CACHE BOOL "" FORCE)
set(LIBPD_SHARED ON CACHE BOOL "" FORCE)
add_subdirectory("${LIBPD_SOURCE_DIR}" libpd)
if(ENOMIK_FLOAT_DSP)
    target_compile_definitions(libpd PRIVATE PD_FLOAT_DSP=1)
endif()
# Use identical IEEE settings for the reference and candidate.
set_source_files_properties(
        "${LIBPD_SOURCE_DIR}/pure-data/src/d_array.c"
        "${LIBPD_SOURCE_DIR}/pure-data/src/d_osc.c"
        "${LIBPD_SOURCE_DIR}/pure-data/src/d_ctl.c"
        DIRECTORY "${LIBPD_SOURCE_DIR}"
        PROPERTIES COMPILE_OPTIONS "-fno-fast-math")
