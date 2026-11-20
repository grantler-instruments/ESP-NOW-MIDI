# libpd (Pure Data as a library) built as an ESP-IDF component.
#
# Sources come from a libpd clone in libpd/ (with pure-data inside). CMake
# clones it on the first configure (needs git + network once); the folder is
# gitignored and survives `idf.py fullclean`. To change version, edit
# LIBPD_TAG and delete libpd/.
#
# Source list mirrors libpd's CMakeLists.txt, minus:
#   - pd~ and stdout extras (need fork/pipes)
#   - LIBPD_EXTRA (we call the remaining extras' setup functions ourselves,
#     see libpd_esp_extras_setup() in compat/libpd_esp_compat.c)
#   - dynamic loading (no HAVE_LIBDL: externals are not supported)

set(LIBPD_DIR "${CMAKE_CURRENT_LIST_DIR}/libpd")
set(PD_SRC "${LIBPD_DIR}/pure-data/src")
set(PD_EXTRA "${LIBPD_DIR}/pure-data/extra")

set(LIBPD_TAG "0.16.1") # Pd 0.56-5

if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT EXISTS "${PD_SRC}/m_pd.h")
    message(STATUS "Fetching libpd ${LIBPD_TAG} into ${LIBPD_DIR}")
    include(FetchContent)
    FetchContent_Populate(libpd
        GIT_REPOSITORY https://github.com/libpd/libpd
        GIT_TAG        ${LIBPD_TAG}
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES pure-data
        GIT_PROGRESS   TRUE
        SOURCE_DIR     "${LIBPD_DIR}"
        SUBBUILD_DIR   "${CMAKE_BINARY_DIR}/libpd-subbuild"
        BINARY_DIR     "${CMAKE_BINARY_DIR}/libpd-build"
    )
    if(NOT EXISTS "${PD_SRC}/m_pd.h")
        message(FATAL_ERROR "Fetching libpd failed: ${PD_SRC}/m_pd.h missing. "
                            "Delete ${LIBPD_DIR} and configure again.")
    endif()
endif()

set(PD_CORE_SOURCES
    d_arithmetic.c d_array.c d_ctl.c d_dac.c d_delay.c d_fft.c d_fft_fftsg.c
    d_filter.c d_global.c d_math.c d_misc.c d_osc.c d_resample.c d_soundfile.c
    d_soundfile_aiff.c d_soundfile_caf.c d_soundfile_next.c d_soundfile_wave.c
    d_ugen.c
    g_all_guis.c g_array.c g_bang.c g_canvas.c g_clone.c g_editor.c
    g_editor_extras.c g_graph.c g_guiconnect.c g_io.c g_mycanvas.c g_numbox.c
    g_radio.c g_readwrite.c g_rtext.c g_scalar.c g_slider.c g_template.c
    g_text.c g_toggle.c g_traversal.c g_undo.c g_vumeter.c
    m_atom.c m_binbuf.c m_class.c m_conf.c m_glob.c m_memory.c m_obj.c m_pd.c
    m_sched.c
    s_audio.c s_audio_dummy.c s_inter.c s_inter_gui.c s_loader.c s_main.c
    s_net.c s_path.c s_print.c s_utf8.c
    x_acoustics.c x_arithmetic.c x_array.c x_connective.c x_file.c x_gui.c
    x_interface.c x_list.c x_midi.c x_misc.c x_net.c x_scalar.c x_text.c
    x_time.c x_vexp.c x_vexp_fun.c x_vexp_if.c
)
list(TRANSFORM PD_CORE_SOURCES PREPEND "${PD_SRC}/")

set(PD_EXTRA_SOURCES
    "${PD_EXTRA}/bob~/bob~.c"
    "${PD_EXTRA}/bonk~/bonk~.c"
    "${PD_EXTRA}/choice/choice.c"
    "${PD_EXTRA}/fiddle~/fiddle~.c"
    "${PD_EXTRA}/loop~/loop~.c"
    "${PD_EXTRA}/lrshift~/lrshift~.c"
    "${PD_EXTRA}/pique/pique.c"
    "${PD_EXTRA}/sigmund~/sigmund~.c"
)

set(LIBPD_SOURCES
    "${LIBPD_DIR}/libpd_wrapper/s_libpdmidi.c"
    "${LIBPD_DIR}/libpd_wrapper/x_libpdreceive.c"
    "${LIBPD_DIR}/libpd_wrapper/z_hooks.c"
    "${LIBPD_DIR}/libpd_wrapper/z_libpd.c"
    "${LIBPD_DIR}/libpd_wrapper/util/z_print_util.c"
)

idf_component_register(
    SRCS ${PD_CORE_SOURCES} ${PD_EXTRA_SOURCES} ${LIBPD_SOURCES}
         "compat/libpd_esp_compat.c"
    INCLUDE_DIRS "${LIBPD_DIR}/libpd_wrapper" "${LIBPD_DIR}/libpd_wrapper/util"
                 "${PD_SRC}" "compat"
    REQUIRES lwip esp_timer
)

target_compile_definitions(${COMPONENT_LIB}
    PUBLIC  PD=1 USEAPI_DUMMY=1 HAVE_UNISTD_H=1 HAVE_MACHINE_ENDIAN_H=1
    PRIVATE PD_INTERNAL=1 HAVE_ALLOCA_H=1
)
# Pd is legacy C: silence warnings (IDF builds with -Werror for some classes)
# and optimise for speed regardless of the project's size/debug setting.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -w -O2 -ffast-math -Wno-error
    -include "${CMAKE_CURRENT_LIST_DIR}/compat/libpd_esp_prefix.h"
)
