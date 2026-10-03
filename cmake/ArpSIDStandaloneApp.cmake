# Copyright (C) 2024-2026 Ulf Bertilsson
# ArpSID standalone app for Linux and Windows (macOS has its own native app,
# ArpSID.app, built with ARPSID_BUILD_STANDALONE).
#
# The app runs the plug-ins' engine (Vst3KernelHost) and the cross-platform
# VSTGUI editor in a native window, with audio and MIDI through RtAudio and
# RtMidi (MIT-style licences). They are downloaded at configure time, or
# taken from local copies:
#   -DARPSID_RTAUDIO_DIR=/path/to/rtaudio  -DARPSID_RTMIDI_DIR=/path/to/rtmidi
# Linux needs the ALSA development package (libasound2-dev / alsa-lib-devel);
# PulseAudio (libpulse-dev) and JACK (libjack-jackd2-dev) are used when found.
#
# Included from CMakeLists.txt inside the VST3 branch, after the editor's
# VSTGUI targets exist.

set(ARPSID_RTAUDIO_TAG "6.0.1" CACHE STRING "RtAudio git tag for the standalone app")
set(ARPSID_RTMIDI_TAG "6.0.0" CACHE STRING "RtMidi git tag for the standalone app")

include(FetchContent)
if(NOT ARPSID_RTAUDIO_DIR)
    FetchContent_Declare(arpsid_rtaudio
        GIT_REPOSITORY https://github.com/thestk/rtaudio.git
        GIT_TAG ${ARPSID_RTAUDIO_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_GetProperties(arpsid_rtaudio)
    if(NOT arpsid_rtaudio_POPULATED)
        FetchContent_Populate(arpsid_rtaudio)
    endif()
    set(ARPSID_RTAUDIO_DIR "${arpsid_rtaudio_SOURCE_DIR}")
endif()
if(NOT ARPSID_RTMIDI_DIR)
    FetchContent_Declare(arpsid_rtmidi
        GIT_REPOSITORY https://github.com/thestk/rtmidi.git
        GIT_TAG ${ARPSID_RTMIDI_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_GetProperties(arpsid_rtmidi)
    if(NOT arpsid_rtmidi_POPULATED)
        FetchContent_Populate(arpsid_rtmidi)
    endif()
    set(ARPSID_RTMIDI_DIR "${arpsid_rtmidi_SOURCE_DIR}")
endif()
if(NOT EXISTS "${ARPSID_RTAUDIO_DIR}/RtAudio.cpp" OR NOT EXISTS "${ARPSID_RTMIDI_DIR}/RtMidi.cpp")
    message(FATAL_ERROR "RtAudio / RtMidi sources not found (ARPSID_RTAUDIO_DIR=${ARPSID_RTAUDIO_DIR}, "
                        "ARPSID_RTMIDI_DIR=${ARPSID_RTMIDI_DIR})")
endif()

add_library(arpsid_rtio STATIC "${ARPSID_RTAUDIO_DIR}/RtAudio.cpp" "${ARPSID_RTMIDI_DIR}/RtMidi.cpp")
target_include_directories(arpsid_rtio SYSTEM PUBLIC "${ARPSID_RTAUDIO_DIR}" "${ARPSID_RTMIDI_DIR}")
target_compile_features(arpsid_rtio PUBLIC cxx_std_17)
# Third-party: no warnings in our build log.
if(MSVC)
    target_compile_options(arpsid_rtio PRIVATE /w)
else()
    target_compile_options(arpsid_rtio PRIVATE -w)
endif()
set(_arpsid_rtio_apis "")
if(WIN32)
    target_include_directories(arpsid_rtio PRIVATE "${ARPSID_RTAUDIO_DIR}/include")
    target_compile_definitions(arpsid_rtio PRIVATE __WINDOWS_WASAPI__ __WINDOWS_DS__ __WINDOWS_MM__)
    target_link_libraries(arpsid_rtio PUBLIC ksuser mfplat mfuuid wmcodecdspuuid dsound winmm ole32)
    set(_arpsid_rtio_apis "WASAPI, DirectSound, WinMM MIDI")
elseif(UNIX AND NOT APPLE)
    find_package(ALSA)
    if(NOT ALSA_FOUND)
        message(FATAL_ERROR "The standalone app needs the ALSA development package (libasound2-dev / "
                            "alsa-lib-devel); install it with scripts/linux/install_build_deps.sh, or configure "
                            "with -DARPSID_BUILD_STANDALONE_APP=OFF.")
    endif()
    find_package(Threads REQUIRED)
    target_compile_definitions(arpsid_rtio PRIVATE __LINUX_ALSA__)
    target_link_libraries(arpsid_rtio PUBLIC ALSA::ALSA Threads::Threads)
    set(_arpsid_rtio_apis "ALSA")
    find_path(_arpsid_pulse_inc pulse/simple.h)
    find_library(_arpsid_pulse_lib pulse)
    find_library(_arpsid_pulse_simple_lib pulse-simple)
    if(_arpsid_pulse_inc AND _arpsid_pulse_lib AND _arpsid_pulse_simple_lib)
        target_compile_definitions(arpsid_rtio PRIVATE __LINUX_PULSE__)
        target_link_libraries(arpsid_rtio PUBLIC ${_arpsid_pulse_simple_lib} ${_arpsid_pulse_lib})
        string(APPEND _arpsid_rtio_apis ", PulseAudio")
    endif()
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(_arpsid_jack QUIET jack)
        if(_arpsid_jack_FOUND)
            target_compile_definitions(arpsid_rtio PRIVATE __UNIX_JACK__)
            target_include_directories(arpsid_rtio PRIVATE ${_arpsid_jack_INCLUDE_DIRS})
            target_link_libraries(arpsid_rtio PUBLIC ${_arpsid_jack_LINK_LIBRARIES})
            string(APPEND _arpsid_rtio_apis ", JACK")
        endif()
    endif()
endif()
message(STATUS "ArpSID standalone app: audio/MIDI through ${_arpsid_rtio_apis}")

add_executable(arpsid_standalone_app
    source/standalone/arpsid_standalone_main.cpp
    source/standalone/arpsid_standalone_app.cpp
    source/standalone/arpsid_standalone_engine.cpp
    source/standalone/arpsid_standalone_devices.cpp
    source/vst3/arpsid_vst3_kernel_host.cpp
    source/plugin_ids.cpp
    source/arpsid_file_bank.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
    ${ARPSID_VSTGUI_EDITOR_SOURCES})
if(WIN32)
    target_sources(arpsid_standalone_app PRIVATE source/standalone/arpsid_standalone_win32.cpp)
    set_target_properties(arpsid_standalone_app PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
        # A GUI program that still starts at main().
        target_link_options(arpsid_standalone_app PRIVATE /ENTRY:mainCRTStartup)
    endif()
    target_link_libraries(arpsid_standalone_app PRIVATE ole32 shell32)
else()
    target_sources(arpsid_standalone_app PRIVATE source/standalone/arpsid_standalone_x11.cpp)
endif()
set_target_properties(arpsid_standalone_app PROPERTIES OUTPUT_NAME "ArpSID"
                      RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/standalone>")
target_compile_features(arpsid_standalone_app PRIVATE cxx_std_17)
target_include_directories(arpsid_standalone_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_include_directories(arpsid_standalone_app SYSTEM PRIVATE ${vst3sdk_SOURCE_DIR} ${vst3sdk_SOURCE_DIR}/vstgui4)
target_compile_definitions(arpsid_standalone_app PRIVATE VSTGUI_ENABLE_DEPRECATED_METHODS=0)
target_link_libraries(arpsid_standalone_app PRIVATE arpsid_core arpsid_forensic_patchbank vstgui sdk_common
                                                    arpsid_rtio)
if(UNIX AND NOT APPLE)
    target_link_options(arpsid_standalone_app PRIVATE "LINKER:--no-undefined")
endif()

# Headless smoke test: start, open the window (Linux: needs DISPLAY), render
# the toolbar and editor to a PNG, save settings and session, quit.
add_custom_target(arpsid_standalone_check
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_BINARY_DIR}/standalone-check"
    COMMAND $<TARGET_FILE:arpsid_standalone_app> --no-audio --no-midi
            --config-dir "${CMAKE_BINARY_DIR}/standalone-check"
            --quit-after 2500 --screenshot "${CMAKE_BINARY_DIR}/standalone-check/window.png"
    COMMAND "${CMAKE_COMMAND}" -E echo "standalone smoke test passed"
    DEPENDS arpsid_standalone_app
    USES_TERMINAL
    VERBATIM)

install(TARGETS arpsid_standalone_app RUNTIME DESTINATION bin COMPONENT standalone)
