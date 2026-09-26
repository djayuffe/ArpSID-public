cmake_minimum_required(VERSION 3.20)
project(ArpSID VERSION 0.0.690 LANGUAGES CXX)
include(cmake/ArpSIDSourceTreeGuard.cmake)

option(ARPSID_BUILD_ALL "Enable the stable ArpSID product set for the current platform" OFF)

# All build/install/package commands must be path-quoted.  Older RCs rejected
# parentheses in CMAKE_SOURCE_DIR/CMAKE_BINARY_DIR to paper over shell quoting bugs;
# release builds may legitimately come from Finder/Downloads names such as
# "ArpSID RC(1)".  Keep paths legal and fix quoting at the command boundary.
option(ARPSID_STRICT_PATH_QUOTING_AUDIT "Enable strict package-script path quoting checks" ON)

# ObjC++ is required for AUv3 (.mm) sources on macOS.
# Enable it here so CMake initialises CMAKE_OBJCXX_COMPILE_OBJECT before any
# sub-project (e.g. the Steinberg SDK) tries to use it.
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


if(APPLE)
    set(ARPSID_MACOS_USER_VST3_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3" CACHE PATH
        "Default per-user VST3 install directory on macOS")
    set(ARPSID_MACOS_USER_APPS_DIR "$ENV{HOME}/Applications" CACHE PATH
        "Default per-user Applications directory on macOS")
    set(ARPSID_MACOS_USER_AUV2_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components" CACHE PATH
        "Default per-user AUv2 component install directory on macOS")
    set(ARPSID_SYSTEM_AUV2_DIR "/Library/Audio/Plug-Ins/Components" CACHE PATH
        "System-wide AUv2 component install directory on macOS")
    set(ARPSID_LOGIC_APP_INSTALL_DIR "$ENV{HOME}/Applications" CACHE PATH
        "Install directory for the Logic-visible AUv3 wrapper app")
    set(ARPSID_USER_AUX_DIR "${CMAKE_BINARY_DIR}/install-user-aux" CACHE PATH
        "User-writable auxiliary install directory for raw AUv3 debug artifacts")

    option(ARPSID_ENABLE_CODESIGN "Codesign macOS app and plugin bundles after build/install" ON)
    set(ARPSID_CODESIGN_IDENTITY "-" CACHE STRING
        "codesign identity to use on macOS (use '-' for ad-hoc signing, or a Developer ID/Application identity)")
    set(ARPSID_CODESIGN_ENTITLEMENTS "" CACHE FILEPATH
        "Optional generic entitlements plist used when codesigning macOS bundles")
    option(ARPSID_CODESIGN_HARDENED_RUNTIME "Enable --options runtime during codesign" OFF)

    set(ARPSID_AUV2_BUNDLE_ID "com.arpsid.auv2" CACHE STRING
        "Bundle identifier for the classic AUv2 component bundle")
    set(ARPSID_AUV3_EXTENSION_BUNDLE_ID "com.arpsid.auv3" CACHE STRING
        "Bundle identifier for the AUv3 extension")
    set(ARPSID_AUV3_WRAPPER_BUNDLE_ID "com.arpsid.wrapper" CACHE STRING
        "Bundle identifier for the AUv3 development wrapper app")
    set(ARPSID_AUV3_LOGIC_APP_BUNDLE_ID "com.arpsid" CACHE STRING
        "Bundle identifier for the Logic-visible wrapper app")
    set(ARPSID_AUV3_TEAM_ID "" CACHE STRING
        "Optional Apple development team identifier for Xcode generators")
    set(ARPSID_AUV3_HOST_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/ArpSIDHost.entitlements" CACHE FILEPATH
        "Entitlements file for the AUv3 host / wrapper app")
    set(ARPSID_AUV3_EXTENSION_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/ArpSIDAUv3.entitlements" CACHE FILEPATH
        "Entitlements file for the AUv3 extension")
    # Non-sandbox audio-capture entitlements for the bundled in-process host apps
    # (wrapper + standalone). Grants com.apple.security.device.audio-input so the
    # hardened runtime permits DIGI microphone / Core Audio process-tap capture.
    set(ARPSID_HOST_AUDIO_CAPTURE_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/ArpSIDHostAudioCapture.entitlements" CACHE FILEPATH
        "Non-sandbox audio-input entitlements for the bundled host apps")
endif()

if(APPLE)
    if(ARPSID_CODESIGN_HARDENED_RUNTIME)
        set(_arpsid_xcode_hardened_runtime "YES")
    else()
        set(_arpsid_xcode_hardened_runtime "NO")
    endif()

    function(arpsid_collect_codesign_args out_var)
        set(_arpsid_codesign_args --force --sign "${ARPSID_CODESIGN_IDENTITY}" --timestamp=none)
        if(ARPSID_CODESIGN_HARDENED_RUNTIME)
            list(APPEND _arpsid_codesign_args --options runtime)
        endif()
        if(ARPSID_CODESIGN_ENTITLEMENTS)
            list(APPEND _arpsid_codesign_args --entitlements "${ARPSID_CODESIGN_ENTITLEMENTS}")
        endif()
        set(${out_var} ${_arpsid_codesign_args} PARENT_SCOPE)
    endfunction()

    # Optional 2nd arg: path to an entitlements plist to embed at signing. Bundled
    # host apps must pass packaging/macos/ArpSIDHostAudioCapture.entitlements so the
    # hardened runtime allows microphone / Core Audio process-tap capture (DIGI
    # "System Audio Capture"). Without it, capture is silently denied. pass249.
    function(arpsid_add_codesign_post_build target)
        if(NOT ARPSID_ENABLE_CODESIGN)
            return()
        endif()
        if(NOT EXISTS "/usr/bin/codesign")
            return()
        endif()

        arpsid_collect_codesign_args(_arpsid_codesign_args)
        set(_arpsid_extra_entitlements "${ARGN}")
        if(_arpsid_extra_entitlements AND EXISTS "${_arpsid_extra_entitlements}")
            # Replace any generic entitlements inherited from the helper with the
            # target-specific file.
            if(ARPSID_CODESIGN_ENTITLEMENTS)
                list(REMOVE_ITEM _arpsid_codesign_args --entitlements "${ARPSID_CODESIGN_ENTITLEMENTS}")
            endif()
            list(APPEND _arpsid_codesign_args --entitlements "${_arpsid_extra_entitlements}")
        endif()

        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND /usr/bin/codesign ${_arpsid_codesign_args} --deep "$<TARGET_BUNDLE_DIR:${target}>"
            VERBATIM)
    endfunction()
endif()

# ─── Compiler flags ───────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX- /permissive- /fp:fast)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    # Do not enable -ffast-math globally: ArpSID relies on NaN/Inf sanitation
    # in parameters, state restore, telemetry and render fail-closed paths.
    # Individual leaf DSP kernels may opt into narrower finite-safe flags only
    # after behavior/fingerprint tests prove that sanitation remains intact.
endif()

# Expand one-switch full-product builds before individual target options are declared.
if(ARPSID_BUILD_ALL)
    set(ARPSID_BUILD_TESTS ON CACHE BOOL "Build forensic acceptance tests" FORCE)
    set(ARPSID_BUILD_STANDALONE ON CACHE BOOL "Build standalone host" FORCE)
    if(APPLE)
        # RC/parity all-up builds must include every shipping wrapper. VST3 can
        # still fail early if no Steinberg SDK is configured, but BUILD_ALL may
        # not silently exclude it and still claim AU/VST/standalone parity.
        set(ARPSID_BUILD_AUV2 ON CACHE BOOL "Build classic AUv2 component" FORCE)
        set(ARPSID_BUILD_AUV3 ON CACHE BOOL "Build AUv3 app extension" FORCE)
        set(ARPSID_BUILD_LOGIC ON CACHE BOOL "Install AUv3 into a Logic-compatible app bundle" FORCE)
        set(ARPSID_BUILD_VST3 ON CACHE BOOL "Build VST3 plugin (needs VSTSDK_PATH or vst3sdk_SOURCE_DIR)" FORCE)
    else()
        set(ARPSID_BUILD_VST3 ON CACHE BOOL "Build VST3 plugin (needs VSTSDK_PATH or vst3sdk_SOURCE_DIR)" FORCE)
    endif()
endif()

# ─── Header-only core INTERFACE library ───────────────────────────────────────
add_library(arpsid_core INTERFACE)
target_include_directories(arpsid_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/source>
    $<INSTALL_INTERFACE:include>
)

# v970 (handoff 22.7): the forensic patch bank is a heavyweight translation unit
# that 14 separate test executables were each recompiling from source. Compile
# it once here and let those tests link it, cutting duplicate compilation and
# peak build memory without weakening isolation. Production wrappers (VST3, the
# AUv2 GUI smoke) keep compiling their own copy, so this changes no shipped
# binary. forensic_patch_bank.cpp has no per-target compile definitions, so a
# single shared object is ODR-correct for every consumer.
add_library(arpsid_forensic_patchbank STATIC source/forensic_patch_bank.cpp)
target_compile_features(arpsid_forensic_patchbank PRIVATE cxx_std_17)
target_include_directories(arpsid_forensic_patchbank PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source)

option(ARPSID_RELEASE_WARNING_GATE
    "Fail project-owned Release builds on memory/UB-significant compiler warnings"
    ON)
if(ARPSID_RELEASE_WARNING_GATE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(arpsid_core INTERFACE
            $<$<CONFIG:Release>:-Werror=return-type>
            $<$<CONFIG:Release>:-Werror=array-bounds>
            $<$<CONFIG:Release>:-Werror=stringop-overflow>
            $<$<CONFIG:Release>:-Werror=uninitialized>
            $<$<CONFIG:Release>:-Werror=maybe-uninitialized>)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(arpsid_core INTERFACE
            $<$<CONFIG:Release>:-Werror=return-type>
            $<$<CONFIG:Release>:-Werror=array-bounds>
            $<$<CONFIG:Release>:-Werror=uninitialized>)
    elseif(MSVC)
        target_compile_options(arpsid_core INTERFACE
            $<$<CONFIG:Release>:/we4700>
            $<$<CONFIG:Release>:/we4715>)
    endif()
endif()

# ─── Forensic acceptance tests (no SDK required) ──────────────────────────────
option(ARPSID_BUILD_TESTS "Build forensic acceptance tests" OFF)
option(ARPSID_C64_PHYSICAL_ONLY "Keep retired legacy Mos6510 compatibility playback paths unreachable" ON)
add_compile_definitions(ARPSID_C64_PHYSICAL_ONLY=$<BOOL:${ARPSID_C64_PHYSICAL_ONLY}>)

# Opt-in RSID/PSID boot/init/play tracing (include/arpsid/core/c64_boot_trace.h).
# OFF for release so the audio path stays allocation- and IO-free; turn on with
# -DARPSID_BOOT_TRACE=ON to stream cold-boot/load/init/first-play milestones to
# stderr while triaging why a tune fell back to legacy CPU or failed to init.
option(ARPSID_BOOT_TRACE "Enable RSID/PSID boot/init/play stderr tracing" OFF)
add_compile_definitions(ARPSID_BOOT_TRACE_ENABLE=$<BOOL:${ARPSID_BOOT_TRACE}>)

option(ARPSID_RUNTIME_PROFILE_COMPACT "Use compact SidRuntimeModel storage defaults" ON)
set(ARPSID_RUNTIME_INGRESS_CAPACITY "2048" CACHE STRING "SidRuntimeModel ingress node capacity")
set(ARPSID_RUNTIME_INGRESS_SPILL_CAPACITY "512" CACHE STRING "SidRuntimeModel ingress spill capacity")
set(ARPSID_RUNTIME_TIMED_EVENT_CAPACITY "4096" CACHE STRING "SidRuntimeModel timed event queue capacity")
set(ARPSID_RUNTIME_MERGE_LANE_CAPACITY "512" CACHE STRING "SidRuntimeModel merge-lane capacity")
if(NOT ARPSID_RUNTIME_PROFILE_COMPACT)
  if(ARPSID_RUNTIME_INGRESS_CAPACITY STREQUAL "2048")
    set(ARPSID_RUNTIME_INGRESS_CAPACITY "8192" CACHE STRING "SidRuntimeModel ingress node capacity" FORCE)
  endif()
  if(ARPSID_RUNTIME_INGRESS_SPILL_CAPACITY STREQUAL "512")
    set(ARPSID_RUNTIME_INGRESS_SPILL_CAPACITY "2048" CACHE STRING "SidRuntimeModel ingress spill capacity" FORCE)
  endif()
  if(ARPSID_RUNTIME_TIMED_EVENT_CAPACITY STREQUAL "4096")
    set(ARPSID_RUNTIME_TIMED_EVENT_CAPACITY "16384" CACHE STRING "SidRuntimeModel timed event queue capacity" FORCE)
  endif()
  if(ARPSID_RUNTIME_MERGE_LANE_CAPACITY STREQUAL "128")
    set(ARPSID_RUNTIME_MERGE_LANE_CAPACITY "512" CACHE STRING "SidRuntimeModel merge-lane capacity" FORCE)
  endif()
endif()

# VST3 desktop builds get wider merge-lane headroom under dense automation unless the
# user explicitly selected a larger value already.
if(ARPSID_BUILD_VST3)
  if(ARPSID_RUNTIME_MERGE_LANE_CAPACITY LESS 512)
    set(ARPSID_RUNTIME_MERGE_LANE_CAPACITY "512" CACHE STRING "SidRuntimeModel merge-lane capacity" FORCE)
  endif()
endif()
add_compile_definitions(
  ARPSID_RUNTIME_INGRESS_CAPACITY=${ARPSID_RUNTIME_INGRESS_CAPACITY}
  ARPSID_RUNTIME_INGRESS_SPILL_CAPACITY=${ARPSID_RUNTIME_INGRESS_SPILL_CAPACITY}
  ARPSID_RUNTIME_TIMED_EVENT_CAPACITY=${ARPSID_RUNTIME_TIMED_EVENT_CAPACITY}
  ARPSID_RUNTIME_MERGE_LANE_CAPACITY=${ARPSID_RUNTIME_MERGE_LANE_CAPACITY}
)

if(ARPSID_BUILD_TESTS)
    # Stub SDK headers so tests compile standalone
    set(STUB_DIR ${CMAKE_CURRENT_BINARY_DIR}/test_stubs)
    file(MAKE_DIRECTORY ${STUB_DIR}/public.sdk/source/vst)
    file(MAKE_DIRECTORY ${STUB_DIR}/pluginterfaces/vst)
    file(WRITE ${STUB_DIR}/public.sdk/source/vst/vstaudioeffect.h
         "#pragma once\nnamespace Steinberg { namespace Vst { struct AudioEffect {}; } }\n")
    file(WRITE ${STUB_DIR}/pluginterfaces/vst/ivstevents.h
         "#pragma once\n#include <cstdint>\nnamespace Steinberg { namespace Vst { using ParamID = uint32_t; using ParamValue = double; } }\n")
    file(WRITE ${STUB_DIR}/pluginterfaces/vst/ivstparameterchanges.h
         "#pragma once\n#include <cstdint>\nnamespace Steinberg { using TBool = int16_t; using int16 = int16_t; using int32 = int32_t; using uint32 = uint32_t; namespace Vst { using ParamID = uint32_t; using ParamValue = double; } }\n")

    add_executable(arpsid_forensic_tests
        source/tests/sid_forensic_acceptance.cpp
    )
    target_link_libraries(arpsid_forensic_tests PRIVATE arpsid_core)
    target_include_directories(arpsid_forensic_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${STUB_DIR}
    )
    target_compile_definitions(arpsid_forensic_tests PRIVATE ARPSID_TEST_BUILD=1)
    target_compile_options(arpsid_forensic_tests PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
    set_target_properties(arpsid_forensic_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    )

    # Fractional/subphase physics smoke test — no VST/AU SDK required
    add_executable(arpsid_fractional_smoke
        source/tests/sid_fractional_smoke.cpp
    )
    target_link_libraries(arpsid_fractional_smoke PRIVATE arpsid_core)
    target_include_directories(arpsid_fractional_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${STUB_DIR}
    )
    target_compile_definitions(arpsid_fractional_smoke PRIVATE ARPSID_TEST_BUILD=1)
    target_compile_options(arpsid_fractional_smoke PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
    set_target_properties(arpsid_fractional_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    )

    enable_testing()
    add_test(NAME ForensicAcceptance COMMAND arpsid_forensic_tests)
    add_test(NAME FractionalSmoke COMMAND arpsid_fractional_smoke)

    add_executable(arpsid_sidreg_smoke
        source/tests/sidreg_live_authority_smoke.cpp
    )
    add_executable(arpsid_closure_regression_tests
        source/tests/closure_regression_tests.cpp
        source/patchbank/sid_patchbank_io.cpp
    )
target_link_libraries(arpsid_closure_regression_tests PRIVATE arpsid_forensic_patchbank)
    target_link_libraries(arpsid_sidreg_smoke PRIVATE arpsid_core)
    target_include_directories(arpsid_sidreg_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${STUB_DIR}
    )
    target_compile_definitions(arpsid_sidreg_smoke PRIVATE ARPSID_TEST_BUILD=1)
    target_compile_options(arpsid_sidreg_smoke PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
    set_target_properties(arpsid_sidreg_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    )
    target_link_libraries(arpsid_closure_regression_tests PRIVATE arpsid_core)
    target_include_directories(arpsid_closure_regression_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${STUB_DIR}
    )
    target_compile_definitions(arpsid_closure_regression_tests PRIVATE ARPSID_TEST_BUILD=1)
    target_compile_options(arpsid_closure_regression_tests PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
    set_target_properties(arpsid_closure_regression_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    )
    add_executable(arpsid_register_authority_phase1_tests
        source/tests/register_authority_phase1_tests.cpp
    )
    target_link_libraries(arpsid_register_authority_phase1_tests PRIVATE arpsid_core)
    target_include_directories(arpsid_register_authority_phase1_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${STUB_DIR}
    )
    target_compile_definitions(arpsid_register_authority_phase1_tests PRIVATE ARPSID_TEST_BUILD=1)
    target_compile_options(arpsid_register_authority_phase1_tests PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
    set_target_properties(arpsid_register_authority_phase1_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    )
    add_test(NAME RegisterAuthorityPhase1Tests
             COMMAND $<TARGET_FILE:arpsid_register_authority_phase1_tests>)
    add_test(NAME SidRegLiveAuthority
             COMMAND $<TARGET_FILE:arpsid_sidreg_smoke>)
    add_test(NAME ClosureRegression
             COMMAND $<TARGET_FILE:arpsid_closure_regression_tests>)
    set_tests_properties(RegisterAuthorityPhase1Tests SidRegLiveAuthority ClosureRegression PROPERTIES TIMEOUT 30)

    add_executable(arpsid_runtime_parity_harness
        source/tests/runtime_parity_harness.cpp
    )
    target_link_libraries(arpsid_runtime_parity_harness PRIVATE arpsid_core)
    target_include_directories(arpsid_runtime_parity_harness PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${STUB_DIR}
    )
    target_compile_definitions(arpsid_runtime_parity_harness PRIVATE ARPSID_TEST_BUILD=1)
    target_compile_options(arpsid_runtime_parity_harness PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
    set_target_properties(arpsid_runtime_parity_harness PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    )
    add_test(NAME RuntimeParityHarness
             COMMAND $<TARGET_FILE:arpsid_runtime_parity_harness>)
    set_tests_properties(RuntimeParityHarness PROPERTIES TIMEOUT 30)

    add_executable(arpsid_release_gate_regression_tests
        source/tests/release_gate_regression_tests.cpp
    )
target_link_libraries(arpsid_release_gate_regression_tests PRIVATE arpsid_forensic_patchbank)
    target_link_libraries(arpsid_release_gate_regression_tests PRIVATE arpsid_core)
    target_include_directories(arpsid_release_gate_regression_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${STUB_DIR}
    )
    target_compile_definitions(arpsid_release_gate_regression_tests PRIVATE ARPSID_TEST_BUILD=1)
    set_target_properties(arpsid_release_gate_regression_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    )
    add_test(NAME ReleaseGateRegression
             COMMAND $<TARGET_FILE:arpsid_release_gate_regression_tests>)
    set_tests_properties(ReleaseGateRegression PROPERTIES TIMEOUT 30)

endif()

# ─── VST3 plugin (official Steinberg SDK CMake integration) ─────────────────
option(ARPSID_BUILD_VST3 "Build VST3 plugin (needs VSTSDK_PATH or vst3sdk_SOURCE_DIR)" OFF)
if(ARPSID_BUILD_VST3)
    if(NOT DEFINED vst3sdk_SOURCE_DIR AND DEFINED VSTSDK_PATH)
        set(vst3sdk_SOURCE_DIR "${VSTSDK_PATH}")
    endif()

    if(NOT DEFINED vst3sdk_SOURCE_DIR)
        message(FATAL_ERROR "Set -Dvst3sdk_SOURCE_DIR=/path/to/vst3sdk or -DVSTSDK_PATH=/path/to/vst3sdk")
    endif()

    if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/CMakeLists.txt" OR NOT EXISTS "${vst3sdk_SOURCE_DIR}/public.sdk")
        message(FATAL_ERROR "Invalid vst3sdk_SOURCE_DIR: ${vst3sdk_SOURCE_DIR}")
    endif()

    if(NOT TARGET sdk)
        # Suppress Xcode ZERO_CHECK re-run triggered by the SDK's CMake integration.
        if(CMAKE_GENERATOR MATCHES "Xcode")
            set(CMAKE_SUPPRESS_REGENERATION ON CACHE BOOL "" FORCE)
        endif()

        # Disable SDK extras that we don't need and that cause build issues:
        # - SMTG_BUILD_TESTING=OFF  : suppresses validator target whose PostBuild script
        #   fails when the build path contains parentheses (shell quoting issue).
        # - SMTG_CREATE_PLUGIN_LINK=OFF : suppresses the SDK's copy-to-VST-folder step
        #   which requires Xcode entitlements we haven't set up.
        # - SMTG_ENABLE_USE_OF_JACK=OFF : suppress JACK audio dependency check.
        # - SMTG_ADD_VSTGUI=OFF    : do not pull the SDK's VSTGUI designer/uidescription layer
        #   into the main graph. ArpSID's VST3 target wires its own editor sources and should
        #   not depend on the SDK's optional vstgui_support/uidescription helper targets.
        set(SMTG_BUILD_TESTING    OFF CACHE BOOL "" FORCE)
        set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
        set(SMTG_ENABLE_USE_OF_JACK OFF CACHE BOOL "" FORCE)
        set(SMTG_ADD_VSTGUI       OFF CACHE BOOL "" FORCE)
        set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)

        add_subdirectory(${vst3sdk_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/vst3sdk EXCLUDE_FROM_ALL)
    endif()

    if(COMMAND smtg_enable_vst3_sdk)
        smtg_enable_vst3_sdk()
    endif()

    if(COMMAND smtg_add_vst3plugin)
        smtg_add_vst3plugin(arpsid_vst3
            source/arpsid_processor_phase2.cpp
            source/arpsid_module_init.cpp
            source/factory.cpp
            source/plugin_ids.cpp
            source/forensic_patch_bank.cpp
            source/arpsid_file_bank.cpp
            source/gui/arpsid_vstgui_editor.cpp
            source/gui/arpsid_vst_cocoa_bridge.mm
            source/au3/ArpSIDViewController.mm
            source/au3/ArpSIDDigiAudioQueueCapture.mm
            source/au3/ArpSIDFileBankBridge.mm
            )
    else()
        message(FATAL_ERROR "Steinberg SDK CMake function smtg_add_vst3plugin is unavailable in this SDK checkout")
    endif()

    target_link_libraries(arpsid_vst3 PRIVATE arpsid_core sdk)
    target_include_directories(arpsid_vst3 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${vst3sdk_SOURCE_DIR}
        ${vst3sdk_SOURCE_DIR}/vstgui4
    )


    if(TARGET base)
        target_link_libraries(arpsid_vst3 PRIVATE base)
    endif()


    # Newer Apple Clang/Xcode toolchains break third-party VSTGUI targets with:
    # - warnings promoted to errors inside the SDK
    # - PCH/fast-math mismatches in vstgui_uidescription
    # Relax only the SDK GUI targets and disable PCH there; keep ArpSID sources strict.
    if(APPLE)
        target_link_libraries(arpsid_vst3 PRIVATE
            "-framework Cocoa"
            "-framework AppKit"
            "-framework QuartzCore"
            "-framework CoreVideo"
            "-framework Metal"
            "-framework MetalKit"
            "-framework AudioToolbox"
            "-framework AudioUnit"
            "-framework CoreAudio"
            "-framework AVFoundation"
            "-framework CoreAudioKit"
            "-framework UniformTypeIdentifiers")
        set_source_files_properties(
            source/gui/arpsid_vst_cocoa_bridge.mm
            source/au3/ArpSIDViewController.mm
            source/au3/ArpSIDDigiAudioQueueCapture.mm
            source/au3/ArpSIDFileBankBridge.mm
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        set_source_files_properties(source/au3/ArpSIDViewController.mm PROPERTIES COMPILE_DEFINITIONS "ARPSID_VST_COCOA_BRIDGE=1")

        # Keep the Steinberg target's native bundle name intact for linking, but also
        # materialize a friendly ArpSID.vst3 alias in the build tree for the standalone
        # host and user install flows. This avoids reintroducing OUTPUT_NAME drift while
        # still preserving the expected product-facing bundle name.
        if(ARPSID_ENABLE_CODESIGN AND EXISTS "/usr/bin/codesign")
            arpsid_collect_codesign_args(_arpsid_vst3_sign_args)
            add_custom_command(TARGET arpsid_vst3 POST_BUILD
                COMMAND /usr/bin/codesign ${_arpsid_vst3_sign_args}
                        --deep "$<TARGET_BUNDLE_DIR:arpsid_vst3>"
                COMMAND "${CMAKE_COMMAND}" -E rm -rf
                        "${CMAKE_CURRENT_BINARY_DIR}/VST3/Release/ArpSID.vst3"
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                        "$<TARGET_BUNDLE_DIR:arpsid_vst3>"
                        "${CMAKE_CURRENT_BINARY_DIR}/VST3/Release/ArpSID.vst3"
                COMMAND /usr/bin/codesign ${_arpsid_vst3_sign_args}
                        --deep "${CMAKE_CURRENT_BINARY_DIR}/VST3/Release/ArpSID.vst3"
                VERBATIM)
        else()
            add_custom_command(TARGET arpsid_vst3 POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E rm -rf
                        "${CMAKE_CURRENT_BINARY_DIR}/VST3/Release/ArpSID.vst3"
                COMMAND "${CMAKE_COMMAND}" -E copy_directory
                        "$<TARGET_BUNDLE_DIR:arpsid_vst3>"
                        "${CMAKE_CURRENT_BINARY_DIR}/VST3/Release/ArpSID.vst3"
                VERBATIM)
        endif()
    endif()

    # Custom VSTGUI editor is enabled; controller::createView(kEditor) returns
    # a concrete ArpSIDVSTGUIEditor and the target now compiles the editor TU.
endif()

# ─── Shared AU version integer (used in both AUv2 and AUv3 plists) ───────────
# Compute once at top-level scope: PROJECT_VERSION 0.0.1 -> 1
if(APPLE)
    string(REPLACE "." ";" _arpsid_ver_parts "${PROJECT_VERSION}")
    list(GET _arpsid_ver_parts 0 _arpsid_ver_major)
    list(GET _arpsid_ver_parts 1 _arpsid_ver_minor)
    list(GET _arpsid_ver_parts 2 _arpsid_ver_patch)
    math(EXPR ARPSID_AUV2_VERSION_INT
        "${_arpsid_ver_major} * 10000 + ${_arpsid_ver_minor} * 100 + ${_arpsid_ver_patch}")
endif()

# ─── AUv2 plugin (macOS only) ─────────────────────────────────────────────────
option(ARPSID_BUILD_AUV2 "Build classic AUv2 component bundle (macOS only)" OFF)

if(ARPSID_BUILD_AUV2)
    if(NOT APPLE)
        message(FATAL_ERROR "AUv2 requires macOS")
    endif()

    # ── Info.plist — built with string(CONFIGURE @ONLY) so @VAR@ tokens are
    #    expanded by CMake at configure time.  NEVER use bracket strings [=[ ]=]
    #    here: bracket strings suppress ${VAR} expansion, leaving literal
    #    "${ARPSID_AUV2_BUNDLE_ID}" strings in the installed plist, which causes
    #    auval and Logic to reject or fail to find the component.
    #
    #    XML comments are also forbidden in plists — plutil -lint rejects them.
    # ──────────────────────────────────────────────────────────────────────────
    set(ARPSID_AUV2_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/ArpSID_AUv2_Component_Info.plist")
    string(CONFIGURE
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n\
<plist version=\"1.0\">\n\
<dict>\n\
    <key>CFBundleDevelopmentRegion</key><string>en</string>\n\
    <key>CFBundleExecutable</key><string>ArpSID</string>\n\
    <key>CFBundleIdentifier</key><string>@ARPSID_AUV2_BUNDLE_ID@</string>\n\
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>\n\
    <key>CFBundleIconFile</key><string>ArpSIDHybrid.png</string>\n\
    <key>CFBundleName</key><string>ArpSID</string>\n\
    <key>CFBundleGetInfoString</key><string>ArpSID by Uber Sound Solutions</string>\n\
    <key>NSHumanReadableCopyright</key><string>Copyright (C) 2024-2026 Ulf Bertilsson</string>\n\
    <key>CFBundlePackageType</key><string>BNDL</string>\n\
    <key>CFBundleShortVersionString</key><string>@PROJECT_VERSION@</string>\n\
    <key>CFBundleVersion</key><string>@PROJECT_VERSION@</string>\n\
    <key>CFBundleSignature</key><string>????</string>\n\
    <key>LSMinimumSystemVersion</key><string>12.0</string>\n\
    <key>AudioComponents</key>\n\
    <array>\n\
        <dict>\n\
            <key>description</key><string>Uber Sound Solutions classic SID chip instrument with classic factory patches</string>\n\
            <key>factoryFunction</key><string>ArpSIDAUv2Factory</string>\n\
            <key>manufacturer</key><string>ASID</string>\n\
            <key>NSViewFactory</key><string>ArpSIDAUv2ViewFactory</string>\n\
            <key>name</key><string>Uber Sound Solutions: ArpSID</string>\n\
            <key>sandboxSafe</key><true/>\n\
            <key>subtype</key><string>ArpS</string>\n\
            <key>tags</key>\n\
            <array><string>Synthesizer</string><string>Instrument</string></array>\n\
            <key>type</key><string>aumu</string>\n\
            <key>version</key><integer>@ARPSID_AUV2_VERSION_INT@</integer>\n\
        </dict>\n\
        <dict>\n\
            <key>description</key><string>Uber Sound Solutions pure SID chip instrument without DrSID auto-drum mode</string>\n\
            <key>factoryFunction</key><string>ArpSIDAUv2Factory</string>\n\
            <key>manufacturer</key><string>ASID</string>\n\
            <key>NSViewFactory</key><string>ArpSIDAUv2ViewFactory</string>\n\
            <key>name</key><string>Uber Sound Solutions: Pure Instrument</string>\n\
            <key>sandboxSafe</key><true/>\n\
            <key>subtype</key><string>ArIn</string>\n\
            <key>tags</key>\n\
            <array><string>Synthesizer</string><string>Instrument</string></array>\n\
            <key>type</key><string>aumu</string>\n\
            <key>version</key><integer>@ARPSID_AUV2_VERSION_INT@</integer>\n\
        </dict>\n\
        <dict>\n\
            <key>description</key><string>Uber Sound Solutions dedicated DrSID GM drum machine instrument</string>\n\
            <key>factoryFunction</key><string>ArpSIDAUv2Factory</string>\n\
            <key>manufacturer</key><string>ASID</string>\n\
            <key>NSViewFactory</key><string>ArpSIDAUv2ViewFactory</string>\n\
            <key>name</key><string>Uber Sound Solutions: DrSID Drum Machine</string>\n\
            <key>sandboxSafe</key><true/>\n\
            <key>subtype</key><string>DrSD</string>\n\
            <key>tags</key>\n\
            <array><string>Drum Machine</string><string>Instrument</string></array>\n\
            <key>type</key><string>aumu</string>\n\
            <key>version</key><integer>@ARPSID_AUV2_VERSION_INT@</integer>\n\
        </dict>\n\
        <dict>\n\
            <key>description</key><string>Uber Sound Solutions dedicated SID-808 analog drum machine using the shared DrSID core</string>\n\
            <key>factoryFunction</key><string>ArpSIDAUv2Factory</string>\n\
            <key>manufacturer</key><string>ASID</string>\n\
            <key>NSViewFactory</key><string>ArpSIDAUv2ViewFactory</string>\n\
            <key>name</key><string>Uber Sound Solutions: SID-808</string>\n\
            <key>sandboxSafe</key><true/>\n\
            <key>subtype</key><string>S808</string>\n\
            <key>tags</key>\n\
            <array><string>Drum Machine</string><string>Analog</string><string>Instrument</string></array>\n\
            <key>type</key><string>aumu</string>\n\
            <key>version</key><integer>@ARPSID_AUV2_VERSION_INT@</integer>\n\
        </dict>\n        <dict>
\
            <key>description</key><string>Uber Sound Solutions C64 SID player for PSID/RSID files</string>
\
            <key>factoryFunction</key><string>ArpSIDAUv2Factory</string>
\
            <key>manufacturer</key><string>ASID</string>
\
            <key>NSViewFactory</key><string>ArpSIDAUv2ViewFactory</string>
\
            <key>name</key><string>Uber Sound Solutions: C64 SID Player</string>
\
            <key>sandboxSafe</key><true/>
\
            <key>subtype</key><string>C64P</string>
\
            <key>tags</key>
\
            <array><string>Music Device</string><string>C64</string><string>SID Player</string></array>
\
            <key>type</key><string>aumu</string>
\
            <key>version</key><integer>@ARPSID_AUV2_VERSION_INT@</integer>
\
        </dict>
\
\
    </array>\n\
    <key>NSMicrophoneUsageDescription</key><string>ArpSID's DIGI tab records and live-monitors audio from the selected input (microphone, line-in, or routed CoreAudio/loopback device) so you can capture your own samples. Audio is only captured while you start REC or MON.</string>\n\
</dict>\n\
</plist>\n"
        _arpsid_auv2_plist_content @ONLY)
    file(WRITE "${ARPSID_AUV2_INFO_PLIST}" "${_arpsid_auv2_plist_content}")

    # ── Entitlements — AUv2 .component bundles are loaded in-process by the
    #    host.  They must NOT carry com.apple.security.app-sandbox: that flag
    #    causes the dynamic linker to restrict the process sandbox to the
    #    component's entitlements, breaking non-sandboxed hosts and confusing
    #    auval.  Use a minimal, sandbox-free entitlements file. ──────────────
    set(ARPSID_AUV2_ENTITLEMENTS
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/ArpSIDAUv2.entitlements"
        CACHE FILEPATH "Entitlements for the AUv2 .component bundle")

    # ── Sources ──────────────────────────────────────────────────────────────
    # ArpSIDAUv2Component.mm is the AudioComponentPlugInInterface C-level bridge.
    # It wraps ArpSIDAudioUnit and implements the full classic AUv2 selector/property
    # surface. The legacy ObjC AUAudioUnitFactory shim has been removed to prevent
    # duplicate ArpSIDAUv2Factory symbols and incompatible factory return types.
    set(ARPSID_AUV2_SOURCES
        source/au2/ArpSIDAUv2Component.mm    # Bridge + embedded ArpSIDAUv2ViewFactory
        # ArpSIDAUv2CocoaView.mm removed: view factory is now ArpSIDAUv2ViewFactory in Component.mm
        source/au3/ArpSIDAudioUnit.mm
        source/au3/ArpSIDDSPKernelAdapter.mm
        source/au3/ArpSIDViewController.mm
        source/au3/ArpSIDDigiAudioQueueCapture.mm
        source/au3/ArpSIDFileBankBridge.mm
        source/forensic_patch_bank.cpp
        source/arpsid_file_bank.cpp
    )

    # ── Frameworks ────────────────────────────────────────────────────────────
    set(ARPSID_AUV2_FRAMEWORKS
        "-framework Cocoa"
        "-framework AppKit"
        "-framework QuartzCore"
        "-framework CoreVideo"
        "-framework Metal"
        "-framework MetalKit"
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreAudio"
        "-framework CoreMIDI"
        "-framework AVFoundation"
        "-framework CoreFoundation"
        "-framework CoreAudioKit"
        "-framework UniformTypeIdentifiers"
    )

    set(ARPSID_AUV2_ICON_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/auv2_icons")
    set(ARPSID_AUV2_HYBRID_ICON "${ARPSID_AUV2_ICON_OUTPUT_DIR}/ArpSIDHybrid.png")
    set(ARPSID_AUV2_INSTRUMENT_ICON "${ARPSID_AUV2_ICON_OUTPUT_DIR}/ArpSIDInstrument.png")
    set(ARPSID_AUV2_DRSID_ICON "${ARPSID_AUV2_ICON_OUTPUT_DIR}/ArpSIDDrumMachine.png")
    set(ARPSID_AUV2_SID808_ICON "${ARPSID_AUV2_ICON_OUTPUT_DIR}/ArpSIDSid808.png")
    set(ARPSID_AUV2_ICON_RESOURCES
        "${ARPSID_AUV2_HYBRID_ICON}"
        "${ARPSID_AUV2_INSTRUMENT_ICON}"
        "${ARPSID_AUV2_DRSID_ICON}"
        "${ARPSID_AUV2_SID808_ICON}")
    add_custom_command(
        OUTPUT ${ARPSID_AUV2_ICON_RESOURCES}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARPSID_AUV2_ICON_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARPSID_AUV2_ICON_OUTPUT_DIR}/ModuleCache"
        COMMAND /usr/bin/xcrun swift
                -module-cache-path "${ARPSID_AUV2_ICON_OUTPUT_DIR}/ModuleCache"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos/generate_auv2_flavor_icons.swift"
                "${ARPSID_AUV2_ICON_OUTPUT_DIR}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos/generate_auv2_flavor_icons.swift"
        COMMENT "Generating ArpSID AUv2 flavor icons"
        VERBATIM)

    # ── Target ────────────────────────────────────────────────────────────────
    add_library(arpsid_auv2 MODULE ${ARPSID_AUV2_SOURCES})
    target_compile_definitions(arpsid_auv2 PRIVATE
        ARPSID_AUV2_COMPONENT=1
        ARPSID_AUDIOCOMPONENT_VERSION=${ARPSID_AUV2_VERSION_INT}
    )
    target_link_libraries(arpsid_auv2 PRIVATE arpsid_core ${ARPSID_AUV2_FRAMEWORKS})
    target_include_directories(arpsid_auv2 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${CMAKE_CURRENT_SOURCE_DIR}/source/au3
        ${CMAKE_CURRENT_SOURCE_DIR}/source/au2
    )
    target_compile_options(arpsid_auv2 PRIVATE -fobjc-arc)
    set_source_files_properties(${ARPSID_AUV2_ICON_RESOURCES} PROPERTIES
        GENERATED TRUE
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(arpsid_auv2 PRIVATE ${ARPSID_AUV2_ICON_RESOURCES})
    set_target_properties(arpsid_auv2 PROPERTIES
        BUNDLE                                  TRUE
        BUNDLE_EXTENSION                        component
        PREFIX                                  ""
        OUTPUT_NAME                             "ArpSID"
        MACOSX_BUNDLE_ICON_FILE                 "ArpSIDHybrid.png"
        MACOSX_BUNDLE_INFO_PLIST                "${ARPSID_AUV2_INFO_PLIST}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${ARPSID_AUV2_BUNDLE_ID}"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY      "${ARPSID_CODESIGN_IDENTITY}"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS  "${ARPSID_AUV2_ENTITLEMENTS}"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "${_arpsid_xcode_hardened_runtime}"
    )

    # ── Post-build codesign with AUv2-specific entitlements ───────────────────
    if(ARPSID_ENABLE_CODESIGN AND EXISTS "/usr/bin/codesign")
        arpsid_collect_codesign_args(_arpsid_auv2_sign_args)
        # For AUv2 .component bundles, always sign with the AUv2-specific
        # entitlements (no sandbox) rather than the generic entitlements.
        # Remove any --entitlements flag inherited from the generic helper and
        # replace it with the AUv2 entitlements.
        list(REMOVE_ITEM _arpsid_auv2_sign_args --entitlements)
        if(EXISTS "${ARPSID_AUV2_ENTITLEMENTS}")
            list(APPEND _arpsid_auv2_sign_args
                --entitlements "${ARPSID_AUV2_ENTITLEMENTS}")
        endif()
        add_custom_command(TARGET arpsid_auv2 POST_BUILD
            COMMAND /usr/bin/codesign ${_arpsid_auv2_sign_args}
                    --deep "$<TARGET_BUNDLE_DIR:arpsid_auv2>"
            COMMENT "Codesigning ArpSID AUv2 .component bundle"
            VERBATIM)
    endif()

    # ── Scripts ───────────────────────────────────────────────────────────────
    set(ARPSID_AUV2_SIGN_SCRIPT
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos/sign_auv2_component.sh")
    set(ARPSID_AUV2_INSTALL_SCRIPT
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos/install_auv2_component.sh")
    set(ARPSID_AUV2_REFRESH_SCRIPT
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos/refresh_auv2_component.sh")
    set(ARPSID_AUV2_VERIFY_SCRIPT
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos/verify_auv2_component.sh")
    set(ARPSID_AUV2_BUILD_SCRIPT
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos_build_install_validate_auv2.sh")

    # ── Custom targets ────────────────────────────────────────────────────────
    add_custom_target(arpsid_auv2_sign
        COMMAND /bin/sh "${ARPSID_AUV2_SIGN_SCRIPT}"
            "$<TARGET_BUNDLE_DIR:arpsid_auv2>"
            "${ARPSID_CODESIGN_IDENTITY}"
            "${ARPSID_AUV2_ENTITLEMENTS}"
            "$<IF:$<BOOL:${ARPSID_CODESIGN_HARDENED_RUNTIME}>,ON,OFF>"
        DEPENDS arpsid_auv2
        COMMENT "Signing ArpSID AUv2 .component bundle"
        VERBATIM)

    add_custom_target(arpsid_auv2_install_user
        COMMAND /bin/sh "${ARPSID_AUV2_INSTALL_SCRIPT}"
            "$<TARGET_BUNDLE_DIR:arpsid_auv2>"
            "${ARPSID_MACOS_USER_AUV2_DIR}"
            "${ARPSID_CODESIGN_IDENTITY}"
        DEPENDS arpsid_auv2
        COMMENT "Installing ArpSID AUv2 component into the user Components folder"
        VERBATIM)

    add_custom_target(arpsid_auv2_install_system
        COMMAND /bin/sh "${ARPSID_AUV2_INSTALL_SCRIPT}"
            "$<TARGET_BUNDLE_DIR:arpsid_auv2>"
            "${ARPSID_SYSTEM_AUV2_DIR}"
            "${ARPSID_CODESIGN_IDENTITY}"
        DEPENDS arpsid_auv2
        COMMENT "Installing ArpSID AUv2 component into the system Components folder (requires sudo)"
        VERBATIM)

    add_custom_target(arpsid_auv2_refresh
        COMMAND /bin/sh "${ARPSID_AUV2_REFRESH_SCRIPT}"
            "${ARPSID_MACOS_USER_AUV2_DIR}/ArpSID.component"
        DEPENDS arpsid_auv2_install_user
        COMMENT "Refreshing AudioComponentRegistrar / Components cache for AUv2 discovery"
        VERBATIM)

    add_custom_target(arpsid_auv2_verify
        COMMAND /bin/sh "${ARPSID_AUV2_VERIFY_SCRIPT}"
            "${ARPSID_MACOS_USER_AUV2_DIR}/ArpSID.component"
        DEPENDS arpsid_auv2_refresh
        COMMENT "Verifying Logic-visible ArpSID AUv2 installation"
        VERBATIM)

    add_custom_target(arpsid_auv2_build_user
        COMMAND /usr/bin/env bash "${ARPSID_AUV2_BUILD_SCRIPT}"
            "${CMAKE_CURRENT_BINARY_DIR}"
            "${ARPSID_MACOS_USER_AUV2_DIR}"
            "${ARPSID_CODESIGN_IDENTITY}"
        COMMENT "Configuring, building, installing, refreshing, and verifying every AUv2 flavor"
        VERBATIM)

    message(STATUS "  AUv2 component:    enabled")
    message(STATUS "  AUv2 bundle ID:    ${ARPSID_AUV2_BUNDLE_ID}")
    message(STATUS "  AUv2 version int:  ${ARPSID_AUV2_VERSION_INT}")
    message(STATUS "  AUv2 entitlements: ${ARPSID_AUV2_ENTITLEMENTS}")
    message(STATUS "  AUv2 install dir:  ${ARPSID_MACOS_USER_AUV2_DIR}")

    # ── Host-side smoke test executable ──────────────────────────────────────
    # Discovers, instantiates, renders, and validates audio from the installed
    # ArpSID.component outside auval. Requires arpsid_auv2_install_user first.
    option(ARPSID_BUILD_AUV2_SMOKE "Build AUv2 host-side smoke render test" OFF)
    if(ARPSID_BUILD_AUV2_SMOKE OR ARPSID_BUILD_ALL)
        add_executable(arpsid_auv2_component_smoke
            source/tests/arpsid_auv2_component_smoke.mm
        )
        add_executable(arpsid_auv2_sid808_component_smoke_v862
            source/tests/arpsid_auv2_sid808_component_smoke_v862.mm
        )
        add_executable(au3_sid808_render_event_smoke_v862
            source/tests/au3_sid808_render_event_smoke_v862.mm
            source/au3/ArpSIDAudioUnit.mm
            source/au3/ArpSIDDSPKernelAdapter.mm
            source/au3/ArpSIDViewController.mm
            source/au3/ArpSIDDigiAudioQueueCapture.mm
            source/au3/ArpSIDFileBankBridge.mm
            source/forensic_patch_bank.cpp
            source/arpsid_file_bank.cpp
        )
        add_executable(arpsid_auv2_gui_lifecycle_smoke
            source/tests/arpsid_auv2_gui_lifecycle_smoke.mm
        )
        target_compile_options(arpsid_auv2_component_smoke PRIVATE -fobjc-arc)
        target_compile_options(arpsid_auv2_sid808_component_smoke_v862 PRIVATE -fobjc-arc)
        target_compile_options(au3_sid808_render_event_smoke_v862 PRIVATE -fobjc-arc)
        target_compile_definitions(au3_sid808_render_event_smoke_v862 PRIVATE
            ARPSID_AUV2_COMPONENT=1
        )
        target_compile_options(arpsid_auv2_gui_lifecycle_smoke PRIVATE -fobjc-arc)
        target_link_libraries(arpsid_auv2_component_smoke PRIVATE
            "-framework AudioUnit"
            "-framework AudioToolbox"
            "-framework CoreAudio"
            "-framework Foundation"
            "-framework CoreMIDI"
        )
        target_link_libraries(arpsid_auv2_sid808_component_smoke_v862 PRIVATE
            "-framework AudioUnit"
            "-framework AudioToolbox"
            "-framework CoreAudio"
            "-framework Foundation"
            "-framework CoreMIDI"
        )
        target_link_libraries(au3_sid808_render_event_smoke_v862 PRIVATE
            arpsid_core
            ${ARPSID_AUV2_FRAMEWORKS}
        )
        target_include_directories(au3_sid808_render_event_smoke_v862 PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/source
            ${CMAKE_CURRENT_SOURCE_DIR}/source/au3
        )
        target_link_libraries(arpsid_auv2_gui_lifecycle_smoke PRIVATE
            "-framework AppKit"
            "-framework AudioUnit"
            "-framework AudioToolbox"
            "-framework CoreAudio"
            "-framework Foundation"
        )
        set_target_properties(arpsid_auv2_component_smoke PROPERTIES
            MACOSX_RPATH ON
            BUILD_WITH_INSTALL_RPATH YES)
        set_target_properties(arpsid_auv2_sid808_component_smoke_v862 PROPERTIES
            MACOSX_RPATH ON
            BUILD_WITH_INSTALL_RPATH YES)
        set_target_properties(au3_sid808_render_event_smoke_v862 PROPERTIES
            MACOSX_RPATH ON
            BUILD_WITH_INSTALL_RPATH YES)
        set_target_properties(arpsid_auv2_gui_lifecycle_smoke PROPERTIES
            MACOSX_RPATH ON
            BUILD_WITH_INSTALL_RPATH YES)
        if(BUILD_TESTING)
            add_test(NAME ArpSID_AUv2_Smoke
                     COMMAND "$<TARGET_FILE:arpsid_auv2_component_smoke>")
            set_tests_properties(ArpSID_AUv2_Smoke PROPERTIES
                LABELS "auv2;smoke"
                TIMEOUT 60)
            add_test(NAME ArpSID_AUv2_SID808_Smoke_v862
                     COMMAND "$<TARGET_FILE:arpsid_auv2_sid808_component_smoke_v862>")
            set_tests_properties(ArpSID_AUv2_SID808_Smoke_v862 PROPERTIES
                LABELS "auv2;sid808;smoke"
                TIMEOUT 60)
            add_test(NAME ArpSID_AU3_SID808_RenderEvent_Smoke_v862
                     COMMAND "$<TARGET_FILE:au3_sid808_render_event_smoke_v862>")
            set_tests_properties(ArpSID_AU3_SID808_RenderEvent_Smoke_v862 PROPERTIES
                LABELS "au3;sid808;smoke"
                TIMEOUT 60)
        endif()
        message(STATUS "  AUv2 smoke test:   enabled")
    endif()
endif()

# ─── AUv3 plugin (macOS only) ─────────────────────────────────────────────────
option(ARPSID_BUILD_AUV3 "Build AUv3 plugin (macOS only)" OFF)

option(ARPSID_ENABLE_AUVAL_TESTS "Enable macOS auval validation test target" OFF)

set(ARPSID_AU_HOST_SOURCES
    source/au3/ArpSIDHostMain.mm
    source/au3/ArpSIDHostAppDelegate.mm
    source/au3/ArpSIDAudioUnit.mm
    source/au3/ArpSIDDSPKernelAdapter.mm
    source/au3/ArpSIDViewController.mm
    source/au3/ArpSIDDigiAudioQueueCapture.mm
    source/au3/ArpSIDFileBankBridge.mm
    source/forensic_patch_bank.cpp
    source/arpsid_file_bank.cpp
)
set(ARPSID_AU_HOST_FRAMEWORKS
    "-framework Cocoa"
    "-framework AppKit"
    "-framework QuartzCore"
    "-framework CoreVideo"
    "-framework Metal"
    "-framework MetalKit"
    "-framework AudioToolbox"
    "-framework AudioUnit"
    "-framework CoreAudio"
    "-framework CoreMIDI"
    "-framework AVFoundation"
    "-framework CoreFoundation"
    "-framework CoreAudioKit"
    "-framework UniformTypeIdentifiers"
)

if(ARPSID_BUILD_AUV3)
    if(NOT APPLE)
        message(FATAL_ERROR "AUv3 requires macOS")
    endif()
    set(AUV3_OBJCPP
        source/au3/ArpSIDAudioUnit.mm
        source/au3/ArpSIDDSPKernelAdapter.mm
        source/au3/ArpSIDAUAudioUnitFactory.mm
        source/au3/ArpSIDAUExtensionViewController.mm
        source/au3/ArpSIDFileBankBridge.mm
        source/au3/ArpSIDViewController.mm
        source/au3/ArpSIDDigiAudioQueueCapture.mm
    )

    set(ARPSID_AUV3_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/ArpSID_AUv3_Extension_Info.plist")
    string(CONFIGURE
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n\
<plist version=\"1.0\">\n\
<dict>\n\
    <key>CFBundleDevelopmentRegion</key><string>en</string>\n\
    <key>CFBundleExecutable</key><string>arpsid_auv3</string>\n\
    <key>CFBundleIdentifier</key><string>@ARPSID_AUV3_EXTENSION_BUNDLE_ID@</string>\n\
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>\n\
    <key>CFBundleName</key><string>ArpSID AUv3</string>\n\
    <key>CFBundleGetInfoString</key><string>ArpSID AUv3 by Uber Sound Solutions</string>\n\
    <key>NSHumanReadableCopyright</key><string>Copyright (C) 2024-2026 Ulf Bertilsson</string>\n\
    <key>CFBundlePackageType</key><string>XPC!</string>\n\
    <key>CFBundleShortVersionString</key><string>@PROJECT_VERSION@</string>\n\
    <key>CFBundleVersion</key><string>@PROJECT_VERSION@</string>\n\
    <key>LSMinimumSystemVersion</key><string>12.0</string>\n\
    <key>NSExtension</key>\n\
    <dict>\n\
        <key>NSExtensionAttributes</key>\n\
        <dict>\n\
            <key>AudioComponents</key>\n\
            <array>\n\
                <dict>\n\
                    <key>description</key><string>Uber Sound Solutions AUv3 classic SID chip instrument with classic factory patches</string>\n\
                    <key>manufacturer</key><string>ASID</string>\n\
                    <key>name</key><string>Uber Sound Solutions: ArpSID</string>\n\
                    <key>sandboxSafe</key><true/>\n\
                    <key>subtype</key><string>ArpS</string>\n\
                    <key>tags</key>\n\
                    <array><string>Synthesizer</string><string>Instrument</string></array>\n\
                    <key>type</key><string>aumu</string>\n\
                    <key>version</key><integer>@ARPSID_AUV2_VERSION_INT@</integer>\n\
                </dict>\n\
                <dict>\n\
                    <key>description</key><string>Uber Sound Solutions AUv3 pure instrument without DrSID auto-drum mode</string>\n\
                    <key>manufacturer</key><string>ASID</string>\n\
                    <key>name</key><string>Uber Sound Solutions: Pure Instrument</string>\n\
                    <key>sandboxSafe</key><true/>\n\
                    <key>subtype</key><string>ArIn</string>\n\
                    <key>tags</key>\n\
                    <array><string>Synthesizer</string><string>Instrument</string></array>\n\
                    <key>type</key><string>aumu</string>\n\
                    <key>version</key><integer>@ARPSID_AUV2_VERSION_INT@</integer>\n\
                </dict>\n\
                <dict>\n\
                    <key>description</key><string>Uber Sound Solutions AUv3 dedicated DrSID GM drum machine</string>\n\
                    <key>manufacturer</key><string>ASID</string>\n\
                    <key>name</key><string>Uber Sound Solutions: DrSID Drum Machine</string>\n\
                    <key>sandboxSafe</key><true/>\n\
                    <key>subtype</key><string>DrSD</string>\n\
                    <key>tags</key>\n\
                    <array><string>Drum Machine</string><string>Instrument</string></array>\n\
                    <key>type</key><string>aumu</string>\n\
                    <key>version</key><integer>@ARPSID_AUV2_VERSION_INT@</integer>\n\
                </dict>\n\
                <dict>\n\
                    <key>description</key><string>Uber Sound Solutions AUv3 dedicated SID-808 analog drum machine using the shared DrSID core</string>\n\
                    <key>manufacturer</key><string>ASID</string>\n\
                    <key>name</key><string>Uber Sound Solutions: SID-808</string>\n\
                    <key>sandboxSafe</key><true/>\n\
                    <key>subtype</key><string>S808</string>\n\
                    <key>tags</key>\n\
                    <array><string>Drum Machine</string><string>Analog</string><string>Instrument</string></array>\n\
                    <key>type</key><string>aumu</string>\n\
                    <key>version</key><integer>@ARPSID_AUV2_VERSION_INT@</integer>\n\
                </dict>\n\
            </array>\n\
        </dict>\n\
        <key>NSExtensionPointIdentifier</key><string>com.apple.AudioUnit</string>\n\
        <key>NSExtensionPrincipalClass</key><string>ArpSIDAUAudioUnitFactory</string>\n\
    </dict>\n\
    <key>NSMicrophoneUsageDescription</key><string>ArpSID's DIGI tab records and live-monitors audio from the selected input (microphone, line-in, or routed CoreAudio/loopback device) so you can capture your own samples. Audio is only captured while you start REC or MON.</string>\n\
</dict>\n\
</plist>\n"
        _arpsid_auv3_plist_content @ONLY)
    file(WRITE "${ARPSID_AUV3_INFO_PLIST}" "${_arpsid_auv3_plist_content}")

    add_library(arpsid_auv3 MODULE ${AUV3_OBJCPP}
        source/forensic_patch_bank.cpp
        source/arpsid_file_bank.cpp
    )
    target_link_libraries(arpsid_auv3 PRIVATE arpsid_core)
    target_include_directories(arpsid_auv3 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${CMAKE_CURRENT_SOURCE_DIR}/source/au3
    )
    target_compile_options(arpsid_auv3 PRIVATE -fobjc-arc)
    set_target_properties(arpsid_auv3 PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION appex
        MACOSX_BUNDLE_INFO_PLIST "${ARPSID_AUV3_INFO_PLIST}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${ARPSID_AUV3_EXTENSION_BUNDLE_ID}"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${ARPSID_AUV3_EXTENSION_ENTITLEMENTS}")
    target_compile_definitions(arpsid_auv3 PRIVATE ARPSID_AUV3_EXTENSION=1)
    target_link_libraries(arpsid_auv3 PRIVATE
        "-framework Foundation"   "-framework AudioToolbox" "-framework AudioUnit"
        "-framework CoreAudio"    "-framework CoreMIDI"
        "-framework AppKit"       "-framework AVFoundation"
        "-framework QuartzCore"   "-framework CoreVideo"
        "-framework Metal"        "-framework MetalKit"
        "-framework CoreAudioKit" "-framework UniformTypeIdentifiers")


    set(ARPSID_AUV3_WRAPPER_APP_NAME "ArpSID AUv3.app")

    # Host app Info.plist must carry NSMicrophoneUsageDescription, otherwise the
    # DIGI REC/MON AVAudioEngine input tap is denied/silenced by macOS TCC. The
    # CMake-default generated plist omits it, so generate an explicit one.
    set(ARPSID_AUV3_WRAPPER_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/ArpSID_AUv3_Wrapper_Info.plist")
    string(CONFIGURE
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n\
<plist version=\"1.0\">\n\
<dict>\n\
    <key>CFBundleDevelopmentRegion</key><string>en</string>\n\
    <key>CFBundleExecutable</key><string>ArpSID AUv3</string>\n\
    <key>CFBundleIdentifier</key><string>@ARPSID_AUV3_WRAPPER_BUNDLE_ID@</string>\n\
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>\n\
    <key>CFBundleName</key><string>ArpSID AUv3</string>\n\
    <key>CFBundlePackageType</key><string>APPL</string>\n\
    <key>CFBundleShortVersionString</key><string>@PROJECT_VERSION@</string>\n\
    <key>CFBundleVersion</key><string>@PROJECT_VERSION@</string>\n\
    <key>LSMinimumSystemVersion</key><string>12.0</string>\n\
    <key>NSHighResolutionCapable</key><true/>\n\
    <key>NSPrincipalClass</key><string>NSApplication</string>\n\
    <key>NSMicrophoneUsageDescription</key><string>ArpSID's DIGI tab records and live-monitors audio from the selected input (microphone, line-in, or routed CoreAudio/loopback device) so you can capture your own samples. Audio is only captured while you start REC or MON.</string>\n\
</dict>\n\
</plist>\n"
        _arpsid_auv3_wrapper_plist_content @ONLY)
    file(WRITE "${ARPSID_AUV3_WRAPPER_INFO_PLIST}" "${_arpsid_auv3_wrapper_plist_content}")

    add_executable(arpsid_auv3_wrapper MACOSX_BUNDLE ${ARPSID_AU_HOST_SOURCES})
    target_include_directories(arpsid_auv3_wrapper PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/source
        ${CMAKE_CURRENT_SOURCE_DIR}/source/au3
    )
    target_compile_options(arpsid_auv3_wrapper PRIVATE -fobjc-arc)
    target_link_libraries(arpsid_auv3_wrapper PRIVATE arpsid_core ${ARPSID_AU_HOST_FRAMEWORKS})
    set_target_properties(arpsid_auv3_wrapper PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${ARPSID_AUV3_WRAPPER_INFO_PLIST}"
        MACOSX_BUNDLE_BUNDLE_NAME "ArpSID AUv3"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${ARPSID_AUV3_WRAPPER_BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${ARPSID_AUV3_WRAPPER_BUNDLE_ID}"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${ARPSID_AUV3_HOST_ENTITLEMENTS}"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${ARPSID_CODESIGN_IDENTITY}"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${ARPSID_AUV3_TEAM_ID}"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "${_arpsid_xcode_hardened_runtime}"
        OUTPUT_NAME "ArpSID AUv3")
    add_dependencies(arpsid_auv3_wrapper arpsid_auv3)
    add_custom_command(TARGET arpsid_auv3_wrapper POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:arpsid_auv3_wrapper>/Contents/PlugIns"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_BUNDLE_DIR:arpsid_auv3_wrapper>/Contents/PlugIns/arpsid_auv3.appex"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:arpsid_auv3>" "$<TARGET_BUNDLE_DIR:arpsid_auv3_wrapper>/Contents/PlugIns/arpsid_auv3.appex"
        COMMENT "Embedding arpsid_auv3.appex into ArpSID AUv3.app"
        VERBATIM)
    add_custom_target(arpsid_auv3_wrapper_embed
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:arpsid_auv3_wrapper>/Contents/PlugIns"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_BUNDLE_DIR:arpsid_auv3_wrapper>/Contents/PlugIns/arpsid_auv3.appex"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:arpsid_auv3>" "$<TARGET_BUNDLE_DIR:arpsid_auv3_wrapper>/Contents/PlugIns/arpsid_auv3.appex"
        DEPENDS arpsid_auv3_wrapper arpsid_auv3
        COMMENT "Refreshing embedded arpsid_auv3.appex inside ArpSID AUv3.app"
        VERBATIM)
    arpsid_add_codesign_post_build(arpsid_auv3_wrapper "${ARPSID_HOST_AUDIO_CAPTURE_ENTITLEMENTS}")

    if(ARPSID_ENABLE_AUVAL_TESTS)
    if(APPLE AND TARGET arpsid_auv3_wrapper)
        find_program(ARPSID_AUVAL_TOOL NAMES auval auvaltool)
        if(ARPSID_AUVAL_TOOL)
            set(ARPSID_AUVAL_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/RunArpSIDAUVal.sh")
            # Use string(CONFIGURE @ONLY) — bracket strings [=[ ]=] suppress CMake
            # variable expansion, so ${ARPSID_AUV3_WRAPPER_APP_NAME} and
            # ${ARPSID_AUVAL_TOOL} would remain literal shell text at runtime.
            string(CONFIGURE
"#!/bin/sh
set -eu
APP=\"\$HOME/Applications/@ARPSID_AUV3_WRAPPER_APP_NAME@\"
if [ ! -d \"\$APP\" ]; then
  echo \"AUv3 wrapper app not installed: \$APP\" >&2
  exit 1
fi
/usr/bin/xattr -dr com.apple.quarantine \"\$APP\" || true
/usr/bin/pluginkit -v -a \"\$APP\" 2>/dev/null || true
sleep 1
exec \"@ARPSID_AUVAL_TOOL@\" -v aumu ArpS ASID
" _arpsid_auval_script_content @ONLY)
            file(WRITE "${ARPSID_AUVAL_SCRIPT}" "${_arpsid_auval_script_content}")
            execute_process(COMMAND /bin/chmod +x "${ARPSID_AUVAL_SCRIPT}")
            add_custom_target(arpsid_auval
                COMMAND "${ARPSID_AUVAL_SCRIPT}"
                DEPENDS arpsid_install_user
                COMMENT "Running auval for ArpSID AUv3"
                VERBATIM)
            if(BUILD_TESTING)
                add_test(NAME ArpSID_AUVal COMMAND "${ARPSID_AUVAL_SCRIPT}")
                set_tests_properties(ArpSID_AUVal PROPERTIES LABELS "auv3;integration")
            endif()
        else()
            message(STATUS "ARPSID_ENABLE_AUVAL_TESTS=ON but auval tool was not found; skipping auval target")
        endif()

        set(ARPSID_AUV3_SMOKE_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/RunArpSIDAUv3Smoke.sh")
        string(CONFIGURE
"#!/bin/sh
set -eu
APP=\"\$HOME/Applications/@ARPSID_AUV3_WRAPPER_APP_NAME@\"
APPEX=\"\$APP/Contents/PlugIns/arpsid_auv3.appex\"
PLIST=\"\$APPEX/Contents/Info.plist\"
if [ ! -d \"\$APP\" ]; then
  echo \"AUv3 wrapper app not installed: \$APP\" >&2
  exit 1
fi
if [ ! -d \"\$APPEX\" ]; then
  echo \"Embedded AUv3 appex missing: \$APPEX\" >&2
  exit 1
fi
/usr/bin/plutil -extract NSExtension.NSExtensionPointIdentifier raw -expect string \"\$PLIST\" | /usr/bin/grep -q '^com.apple.AudioUnit\$'
/usr/bin/plutil -extract NSExtension.NSExtensionPrincipalClass raw -expect string \"\$PLIST\" | /usr/bin/grep -q '^ArpSIDAUAudioUnitFactory\$'
/usr/bin/plutil -extract NSExtension.NSExtensionAttributes.AudioComponents.0.type raw -expect string \"\$PLIST\" | /usr/bin/grep -q '^aumu\$'
/usr/bin/plutil -extract NSExtension.NSExtensionAttributes.AudioComponents.0.subtype raw -expect string \"\$PLIST\" | /usr/bin/grep -q '^ArpS\$'
/usr/bin/plutil -extract NSExtension.NSExtensionAttributes.AudioComponents.0.manufacturer raw -expect string \"\$PLIST\" | /usr/bin/grep -q '^ASID\$'
" _arpsid_smoke_script_content @ONLY)
        file(WRITE "${ARPSID_AUV3_SMOKE_SCRIPT}" "${_arpsid_smoke_script_content}")
        execute_process(COMMAND /bin/chmod +x "${ARPSID_AUV3_SMOKE_SCRIPT}")
        add_custom_target(arpsid_auv3_smoke
            COMMAND "${ARPSID_AUV3_SMOKE_SCRIPT}"
            DEPENDS arpsid_install_user
            COMMENT "Verifying AUv3 wrapper bundle, appex embedding, and presentation plist wiring"
            VERBATIM)
        if(BUILD_TESTING)
            add_test(NAME ArpSID_AUv3Smoke COMMAND "${ARPSID_AUV3_SMOKE_SCRIPT}")
            set_tests_properties(ArpSID_AUv3Smoke PROPERTIES LABELS "auv3;smoke")
        endif()
    endif()
endif()
endif()

# ─── Standalone host (macOS only in this release tree) ───────────────────────
# Phase 7 deletes stale non-Apple target membership instead of advertising
# missing platform adapters as buildable release targets.
option(ARPSID_BUILD_STANDALONE "Build standalone host application" OFF)
if(ARPSID_BUILD_STANDALONE)
    # Phase 7: remove stale non-Apple standalone target membership.
    # This release tree only contains the macOS AU-host standalone adapter.
    # Reintroduce other platform adapters only with real sources and parity tests.
    if(NOT APPLE)
        message(FATAL_ERROR "ARPSID_BUILD_STANDALONE is currently supported only on macOS in this tree; non-Apple standalone source files were deleted from target membership in Phase 7.")
    endif()

    # Newer Apple Clang/Xcode toolchains break third-party VSTGUI targets with:
    # - warnings promoted to errors inside the SDK
    # - PCH/fast-math mismatches in vstgui_uidescription
    # Relax only the SDK GUI targets and disable PCH there; keep ArpSID sources strict.
    foreach(_arpsid_vstgui_target IN ITEMS vstgui)
        if(TARGET ${_arpsid_vstgui_target})
            set_target_properties(${_arpsid_vstgui_target} PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
            target_compile_options(${_arpsid_vstgui_target} PRIVATE
                -Wno-error
                -Wno-nullability-extension
                -Wno-deprecated-declarations
                -Wno-ignored-qualifiers
                -Wno-sign-compare
                -Wno-missing-field-initializers
                -Wno-gnu-zero-variadic-macro-arguments
            )
        endif()
    endforeach()

    if(APPLE)
        # Standalone host Info.plist must declare NSMicrophoneUsageDescription so
        # the DIGI REC/MON AVAudioEngine input tap is allowed by macOS TCC.
        set(ARPSID_STANDALONE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/ArpSID_Standalone_Info.plist")
        string(CONFIGURE
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n\
<plist version=\"1.0\">\n\
<dict>\n\
    <key>CFBundleDevelopmentRegion</key><string>en</string>\n\
    <key>CFBundleExecutable</key><string>ArpSID Standalone</string>\n\
    <key>CFBundleIdentifier</key><string>com.arpsid.standalone</string>\n\
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>\n\
    <key>CFBundleName</key><string>ArpSID Standalone</string>\n\
    <key>CFBundlePackageType</key><string>APPL</string>\n\
    <key>CFBundleShortVersionString</key><string>@PROJECT_VERSION@</string>\n\
    <key>CFBundleVersion</key><string>@PROJECT_VERSION@</string>\n\
    <key>LSMinimumSystemVersion</key><string>12.0</string>\n\
    <key>NSHighResolutionCapable</key><true/>\n\
    <key>NSPrincipalClass</key><string>NSApplication</string>\n\
    <key>NSMicrophoneUsageDescription</key><string>ArpSID's DIGI tab records and live-monitors audio from the selected input (microphone, line-in, or routed CoreAudio/loopback device) so you can capture your own samples. Audio is only captured while you start REC or MON.</string>\n\
</dict>\n\
</plist>\n"
            _arpsid_standalone_plist_content @ONLY)
        file(WRITE "${ARPSID_STANDALONE_INFO_PLIST}" "${_arpsid_standalone_plist_content}")

        add_executable(arpsid_standalone MACOSX_BUNDLE
            ${ARPSID_AU_HOST_SOURCES}
        )
        target_include_directories(arpsid_standalone PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/source
            ${CMAKE_CURRENT_SOURCE_DIR}/source/au3
        )
        target_compile_options(arpsid_standalone PRIVATE -fobjc-arc)
        target_link_libraries(arpsid_standalone PRIVATE
            arpsid_core
            ${ARPSID_AU_HOST_FRAMEWORKS}
        )
        set_target_properties(arpsid_standalone PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${ARPSID_STANDALONE_INFO_PLIST}"
            MACOSX_BUNDLE_BUNDLE_NAME "ArpSID Standalone"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.arpsid.standalone"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.arpsid.standalone"
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${ARPSID_CODESIGN_IDENTITY}"
            XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "${_arpsid_xcode_hardened_runtime}"
            OUTPUT_NAME "ArpSID Standalone"
        )
        arpsid_add_codesign_post_build(arpsid_standalone "${ARPSID_HOST_AUDIO_CAPTURE_ENTITLEMENTS}")
    endif()

    target_include_directories(arpsid_standalone PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/source
    )
    target_compile_definitions(arpsid_standalone PRIVATE
        ARPSID_STANDALONE_BUILD=1
    )
    set_target_properties(arpsid_standalone PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    )
    if(APPLE)
        add_custom_target(arpsid_run_standalone
            COMMAND /usr/bin/open "$<TARGET_BUNDLE_DIR:arpsid_standalone>"
            DEPENDS arpsid_standalone
            COMMENT "Launching ArpSID Standalone from the build tree"
            VERBATIM)
        if(TARGET arpsid_auv3_wrapper)
            add_custom_target(arpsid_run_auv3_wrapper
                COMMAND /usr/bin/open "$<TARGET_BUNDLE_DIR:arpsid_auv3_wrapper>"
                DEPENDS arpsid_auv3_wrapper
                COMMENT "Launching ArpSID AUv3 wrapper app from the build tree"
                VERBATIM)
        endif()
    endif()
    message(STATUS "  Standalone host: enabled")
endif()

# ─── Logic Pro / AUv3 app extension install ──────────────────────────────────
# After building arpsid_auv3, install it into the Logic-compatible AUv3 location.
# Logic requires AUv3 extensions to live inside a sandboxed .app bundle.
# This target copies the built .appex into a minimal wrapper app bundle
# that you can then codesign and distribute / install into ~/Music/Audio Units/.
option(ARPSID_BUILD_LOGIC "Install AUv3 into a Logic-compatible app bundle" OFF)
if(ARPSID_BUILD_LOGIC)
    if(NOT APPLE)
        message(FATAL_ERROR "Logic/AUv3 requires macOS")
    endif()
    if(NOT ARPSID_BUILD_AUV3)
        message(FATAL_ERROR "ARPSID_BUILD_LOGIC requires -DARPSID_BUILD_AUV3=ON")
    endif()

    # Wrapper app bundle Info.plist for the host app that contains the AUv3 extension.
    # Generate it as a real file and copy it into the wrapper bundle; do not use shell
    # redirection inside COMMAND lines because that corrupts generated Makefiles.
    set(LOGIC_APP_BUNDLE_DIR "${CMAKE_BINARY_DIR}/ArpSID.app")
    set(LOGIC_APPEX_DIR      "${LOGIC_APP_BUNDLE_DIR}/Contents/PlugIns/arpsid_auv3.appex")
    set(LOGIC_WRAPPER_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/ArpSIDLogicWrapper-Info.plist")
    string(CONFIGURE
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n\
<plist version=\"1.0\">\n\
<dict>\n\
    <key>CFBundleIdentifier</key><string>@ARPSID_AUV3_LOGIC_APP_BUNDLE_ID@</string>\n\
    <key>CFBundleShortVersionString</key><string>@PROJECT_VERSION@</string>\n\
    <key>CFBundleVersion</key><string>@PROJECT_VERSION@</string>\n\
    <key>CFBundleName</key><string>ArpSID</string>\n\
    <key>CFBundlePackageType</key><string>APPL</string>\n\
    <key>CFBundleSignature</key><string>????</string>\n\
    <key>NSPrincipalClass</key><string>NSApplication</string>\n\
    <key>NSMicrophoneUsageDescription</key><string>ArpSID's DIGI tab records and live-monitors audio from the selected input (microphone, line-in, or routed CoreAudio/loopback device) so you can capture your own samples. Audio is only captured while you start REC or MON.</string>\n\
</dict>\n\
</plist>\n"
        _arpsid_logic_plist_content @ONLY)
    file(WRITE "${LOGIC_WRAPPER_INFO_PLIST}" "${_arpsid_logic_plist_content}")

    set(ARPSID_LOGIC_SIGN_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos/sign_logic_bundle.sh")
    set(ARPSID_LOGIC_INSTALL_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos/install_logic_auv3.sh")
    set(ARPSID_LOGIC_REFRESH_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos/refresh_logic_auv3.sh")
    set(ARPSID_LOGIC_VERIFY_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos/verify_logic_auv3.sh")
    set(_arpsid_logic_post_assemble_sign_cmds)
    if(ARPSID_ENABLE_CODESIGN AND EXISTS "/usr/bin/codesign")
        list(APPEND _arpsid_logic_post_assemble_sign_cmds
            COMMAND "${ARPSID_LOGIC_SIGN_SCRIPT}"
                "${LOGIC_APP_BUNDLE_DIR}"
                "${ARPSID_CODESIGN_IDENTITY}"
                "${ARPSID_AUV3_HOST_ENTITLEMENTS}"
                "${ARPSID_AUV3_EXTENSION_ENTITLEMENTS}"
                "${ARPSID_CODESIGN_HARDENED_RUNTIME}")
    endif()

    add_custom_target(arpsid_logic ALL
        DEPENDS arpsid_auv3_wrapper_embed
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${LOGIC_APP_BUNDLE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:arpsid_auv3_wrapper>"
            "${LOGIC_APP_BUNDLE_DIR}"
        COMMAND /usr/bin/plutil -replace CFBundleIdentifier -string "${ARPSID_AUV3_LOGIC_APP_BUNDLE_ID}" "${LOGIC_APP_BUNDLE_DIR}/Contents/Info.plist"
        ${_arpsid_logic_post_assemble_sign_cmds}
        COMMENT "Assembling Logic-compatible ArpSID.app bundle with embedded AUv3 host presentation"
        VERBATIM
    )

    install(DIRECTORY "${LOGIC_APP_BUNDLE_DIR}"
        DESTINATION "${ARPSID_LOGIC_APP_INSTALL_DIR}"
        USE_SOURCE_PERMISSIONS
    )

    add_custom_target(arpsid_logic_sign
        COMMAND "${ARPSID_LOGIC_SIGN_SCRIPT}"
            "${LOGIC_APP_BUNDLE_DIR}"
            "${ARPSID_CODESIGN_IDENTITY}"
            "${ARPSID_AUV3_HOST_ENTITLEMENTS}"
            "${ARPSID_AUV3_EXTENSION_ENTITLEMENTS}"
            "${ARPSID_CODESIGN_HARDENED_RUNTIME}"
        DEPENDS arpsid_logic
        COMMENT "Signing Logic-compatible ArpSID.app and embedded AUv3 extension"
        VERBATIM)

    add_custom_target(arpsid_logic_install_user
        COMMAND "${ARPSID_LOGIC_INSTALL_SCRIPT}"
            "${LOGIC_APP_BUNDLE_DIR}"
            "${ARPSID_LOGIC_APP_INSTALL_DIR}"
        DEPENDS arpsid_logic_sign
        COMMENT "Installing Logic-compatible ArpSID.app into the Applications folder used by Logic"
        VERBATIM)

    add_custom_target(arpsid_logic_refresh
        COMMAND "${ARPSID_LOGIC_REFRESH_SCRIPT}"
            "${ARPSID_LOGIC_APP_INSTALL_DIR}/ArpSID.app"
            "${ARPSID_AUV3_EXTENSION_BUNDLE_ID}"
        COMMENT "Refreshing pluginkit and AudioComponentRegistrar for Logic AUv3 discovery"
        VERBATIM)

    add_custom_target(arpsid_logic_verify
        COMMAND "${ARPSID_LOGIC_VERIFY_SCRIPT}"
            "${ARPSID_LOGIC_APP_INSTALL_DIR}/ArpSID.app"
            "${ARPSID_AUV3_EXTENSION_BUNDLE_ID}"
            "${ARPSID_AUV3_LOGIC_APP_BUNDLE_ID}"
            "${CMAKE_CURRENT_BINARY_DIR}/ArpSID-AUv3-registration-verified.txt"
        DEPENDS arpsid_logic_refresh
        COMMENT "Verifying Logic-visible ArpSID AUv3 installation"
        VERBATIM)

    message(STATUS "  Logic app bundle: ${LOGIC_APP_BUNDLE_DIR}")
    message(STATUS "  Logic install dir: ${ARPSID_LOGIC_APP_INSTALL_DIR}")
    message(STATUS "  Install with: cmake --build build --target arpsid_logic_install_user && cmake --build build --target arpsid_logic_refresh")
    message(STATUS "  Refresh/verify operate on the installed /Applications bundle and do not rebuild the app bundle")
endif()


if(APPLE)
    # Normal install() rules remain for development/package installs under CMAKE_INSTALL_PREFIX.
    # The local user-install path must stay completely isolated from those rules so it never touches
    # protected locations like /usr/local/include during a non-sudo developer install.
    if(TARGET arpsid_vst3)
        install(TARGETS arpsid_vst3
            BUNDLE DESTINATION "${CMAKE_INSTALL_PREFIX}"
            LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}"
            RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}")
    endif()

    if(TARGET arpsid_auv3)
        install(TARGETS arpsid_auv3
            BUNDLE DESTINATION "${CMAKE_INSTALL_PREFIX}/ArpSID-AUv3-raw"
            LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/ArpSID-AUv3-raw"
            RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/ArpSID-AUv3-raw"
            OPTIONAL)
    endif()

    if(TARGET arpsid_standalone)
        install(TARGETS arpsid_standalone
            BUNDLE DESTINATION "${CMAKE_INSTALL_PREFIX}"
            RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}")
    endif()

    set(_arpsid_user_install_deps)
    set(_arpsid_user_install_cmds)
    if(ARPSID_ENABLE_CODESIGN AND EXISTS "/usr/bin/codesign")
        arpsid_collect_codesign_args(_arpsid_user_codesign_args)
        set(_arpsid_user_target_codesign_args ${_arpsid_user_codesign_args})
        if(ARPSID_CODESIGN_ENTITLEMENTS)
            list(REMOVE_ITEM _arpsid_user_target_codesign_args --entitlements "${ARPSID_CODESIGN_ENTITLEMENTS}")
        endif()
    else()
        set(_arpsid_user_codesign_args)
        set(_arpsid_user_target_codesign_args)
    endif()

    # ── AUv2: install to user Components directory ────────────────────────────
    if(TARGET arpsid_auv2)
        list(APPEND _arpsid_user_install_deps arpsid_auv2)
        list(APPEND _arpsid_user_install_cmds
            COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos/install_auv2_component.sh"
                "$<TARGET_BUNDLE_DIR:arpsid_auv2>"
                "${ARPSID_MACOS_USER_AUV2_DIR}"
                "${ARPSID_CODESIGN_IDENTITY}")
    endif()

    if(TARGET arpsid_vst3)
        list(APPEND _arpsid_user_install_deps arpsid_vst3)
        list(APPEND _arpsid_user_install_cmds
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARPSID_MACOS_USER_VST3_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ARPSID_MACOS_USER_VST3_DIR}/arpsid_vst3.vst3"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:arpsid_vst3>" "${ARPSID_MACOS_USER_VST3_DIR}/arpsid_vst3.vst3"
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ARPSID_MACOS_USER_VST3_DIR}/ArpSID.vst3"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:arpsid_vst3>" "${ARPSID_MACOS_USER_VST3_DIR}/ArpSID.vst3"
            COMMAND /usr/bin/xattr -dr com.apple.quarantine "${ARPSID_MACOS_USER_VST3_DIR}/arpsid_vst3.vst3"
            COMMAND /usr/bin/xattr -dr com.apple.quarantine "${ARPSID_MACOS_USER_VST3_DIR}/ArpSID.vst3"
        )
        if(ARPSID_ENABLE_CODESIGN)
            list(APPEND _arpsid_user_install_cmds
                COMMAND /usr/bin/codesign ${_arpsid_user_codesign_args} --deep "${ARPSID_MACOS_USER_VST3_DIR}/arpsid_vst3.vst3"
                COMMAND /usr/bin/codesign ${_arpsid_user_codesign_args} --deep "${ARPSID_MACOS_USER_VST3_DIR}/ArpSID.vst3")
        endif()
    endif()

    if(TARGET arpsid_standalone)
        list(APPEND _arpsid_user_install_deps arpsid_standalone)
        list(APPEND _arpsid_user_install_cmds
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARPSID_MACOS_USER_APPS_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ARPSID_MACOS_USER_APPS_DIR}/ArpSID Standalone.app"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:arpsid_standalone>" "${ARPSID_MACOS_USER_APPS_DIR}/ArpSID Standalone.app"
            COMMAND /usr/bin/xattr -dr com.apple.quarantine "${ARPSID_MACOS_USER_APPS_DIR}/ArpSID Standalone.app"
        )
        if(ARPSID_ENABLE_CODESIGN)
            list(APPEND _arpsid_user_install_cmds
                COMMAND /usr/bin/codesign ${_arpsid_user_target_codesign_args}
                    --entitlements "${ARPSID_HOST_AUDIO_CAPTURE_ENTITLEMENTS}"
                    --deep "${ARPSID_MACOS_USER_APPS_DIR}/ArpSID Standalone.app")
        endif()
    endif()

    if(TARGET arpsid_auv3)
        list(APPEND _arpsid_user_install_deps arpsid_auv3)
        list(APPEND _arpsid_user_install_cmds
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARPSID_USER_AUX_DIR}/ArpSID-AUv3-raw"
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ARPSID_USER_AUX_DIR}/ArpSID-AUv3-raw/arpsid_auv3.appex"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:arpsid_auv3>" "${ARPSID_USER_AUX_DIR}/ArpSID-AUv3-raw/arpsid_auv3.appex"
        )
        if(_arpsid_user_codesign_args)
            list(APPEND _arpsid_user_install_cmds
                COMMAND /usr/bin/codesign ${_arpsid_user_target_codesign_args}
                    --entitlements "${ARPSID_AUV3_EXTENSION_ENTITLEMENTS}"
                    "${ARPSID_USER_AUX_DIR}/ArpSID-AUv3-raw/arpsid_auv3.appex/Contents/MacOS/arpsid_auv3"
                COMMAND /usr/bin/codesign ${_arpsid_user_target_codesign_args}
                    --entitlements "${ARPSID_AUV3_EXTENSION_ENTITLEMENTS}"
                    --deep "${ARPSID_USER_AUX_DIR}/ArpSID-AUv3-raw/arpsid_auv3.appex")
        endif()
    endif()

    if(TARGET arpsid_auv3_wrapper)
        if(TARGET arpsid_auv3_wrapper_embed)
            list(APPEND _arpsid_user_install_deps arpsid_auv3_wrapper_embed)
        else()
            list(APPEND _arpsid_user_install_deps arpsid_auv3_wrapper)
        endif()
        list(APPEND _arpsid_user_install_cmds
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARPSID_MACOS_USER_APPS_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ARPSID_MACOS_USER_APPS_DIR}/${ARPSID_AUV3_WRAPPER_APP_NAME}"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:arpsid_auv3_wrapper>" "${ARPSID_MACOS_USER_APPS_DIR}/${ARPSID_AUV3_WRAPPER_APP_NAME}"
            COMMAND /usr/bin/xattr -dr com.apple.quarantine "${ARPSID_MACOS_USER_APPS_DIR}/${ARPSID_AUV3_WRAPPER_APP_NAME}"
        )
        if(_arpsid_user_codesign_args)
            list(APPEND _arpsid_user_install_cmds
                COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos/sign_logic_bundle.sh"
                    "${ARPSID_MACOS_USER_APPS_DIR}/${ARPSID_AUV3_WRAPPER_APP_NAME}"
                    "${ARPSID_CODESIGN_IDENTITY}"
                    "${ARPSID_HOST_AUDIO_CAPTURE_ENTITLEMENTS}"
                    "${ARPSID_AUV3_EXTENSION_ENTITLEMENTS}"
                    "${ARPSID_CODESIGN_HARDENED_RUNTIME}")
        endif()
    elseif(TARGET arpsid_auv3)
        list(APPEND _arpsid_user_install_deps arpsid_auv3)
    endif()

    if(TARGET arpsid_logic)
        if(TARGET arpsid_logic_sign)
            list(APPEND _arpsid_user_install_deps arpsid_logic_sign)
        else()
            list(APPEND _arpsid_user_install_deps arpsid_logic)
        endif()
        list(APPEND _arpsid_user_install_cmds
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARPSID_LOGIC_APP_INSTALL_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${ARPSID_LOGIC_APP_INSTALL_DIR}/ArpSID.app"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${LOGIC_APP_BUNDLE_DIR}" "${ARPSID_LOGIC_APP_INSTALL_DIR}/ArpSID.app"
            COMMAND /usr/bin/xattr -dr com.apple.quarantine "${ARPSID_LOGIC_APP_INSTALL_DIR}/ArpSID.app"
        )
        if(_arpsid_user_codesign_args)
            list(APPEND _arpsid_user_install_cmds
                COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/macos/sign_logic_bundle.sh"
                    "${ARPSID_LOGIC_APP_INSTALL_DIR}/ArpSID.app"
                    "${ARPSID_CODESIGN_IDENTITY}"
                    "${ARPSID_AUV3_HOST_ENTITLEMENTS}"
                    "${ARPSID_AUV3_EXTENSION_ENTITLEMENTS}"
                    "${ARPSID_CODESIGN_HARDENED_RUNTIME}")
        endif()
    endif()

    if(_arpsid_user_install_deps)
        add_custom_target(arpsid_install_user
            ${_arpsid_user_install_cmds}
            DEPENDS ${_arpsid_user_install_deps}
            COMMENT "Installing ArpSID user bundles to macOS plug-in and Applications folders"
            VERBATIM)
    endif()

    set(_arpsid_all_build_deps)
    if(TARGET arpsid_auv2)
        list(APPEND _arpsid_all_build_deps arpsid_auv2)
    endif()
    if(TARGET arpsid_auv2_component_smoke)
        list(APPEND _arpsid_all_build_deps arpsid_auv2_component_smoke)
    endif()
    if(TARGET arpsid_forensic_tests)
        list(APPEND _arpsid_all_build_deps arpsid_forensic_tests)
    endif()
    if(TARGET arpsid_fractional_smoke)
        list(APPEND _arpsid_all_build_deps arpsid_fractional_smoke)
    endif()
    if(TARGET arpsid_sidreg_smoke)
        list(APPEND _arpsid_all_build_deps arpsid_sidreg_smoke)
    endif()
    if(TARGET arpsid_vst3)
        list(APPEND _arpsid_all_build_deps arpsid_vst3)
    endif()
    if(TARGET arpsid_auv3)
        list(APPEND _arpsid_all_build_deps arpsid_auv3)
    endif()
    if(TARGET arpsid_auv3_wrapper_embed)
        list(APPEND _arpsid_all_build_deps arpsid_auv3_wrapper_embed)
    elseif(TARGET arpsid_auv3_wrapper)
        list(APPEND _arpsid_all_build_deps arpsid_auv3_wrapper)
    endif()
    if(TARGET arpsid_standalone)
        list(APPEND _arpsid_all_build_deps arpsid_standalone)
    endif()
    if(TARGET arpsid_logic_sign)
        list(APPEND _arpsid_all_build_deps arpsid_logic_sign)
    elseif(TARGET arpsid_logic)
        list(APPEND _arpsid_all_build_deps arpsid_logic)
    endif()
    if(_arpsid_all_build_deps)
        add_custom_target(arpsid_build_all
            DEPENDS ${_arpsid_all_build_deps}
            COMMENT "Building all enabled ArpSID artifacts for this platform")
    endif()

    if(TARGET arpsid_install_user)
        add_custom_target(arpsid_install_all_user
            DEPENDS arpsid_build_all arpsid_install_user
            COMMENT "Building and installing all enabled ArpSID user artifacts")
    endif()
endif()

install(TARGETS arpsid_core EXPORT ArpSIDTargets)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT ArpSIDTargets FILE ArpSIDConfig.cmake
    NAMESPACE ArpSID:: DESTINATION lib/cmake/ArpSID)

message(STATUS "ArpSID v${PROJECT_VERSION} | all=${ARPSID_BUILD_ALL} tests=${ARPSID_BUILD_TESTS} vst3=${ARPSID_BUILD_VST3} auv3=${ARPSID_BUILD_AUV3} standalone=${ARPSID_BUILD_STANDALONE} logic=${ARPSID_BUILD_LOGIC}")


# DSP kernel compile flags:
# Debug builds: -O1 + no-inline for stack-frame debuggability and auval stack depth safety.
# Release/RelWithDebInfo: full optimisation — -O1 on the audio hot path costs ~40% throughput.
if(APPLE)
  set_source_files_properties(
    source/au3/ArpSIDDSPKernelAdapter.mm
    PROPERTIES COMPILE_OPTIONS
      "$<IF:$<CONFIG:Debug>,-O1;-g;-fno-inline;-fno-inline-functions;-fno-unroll-loops;-fno-vectorize;-fno-slp-vectorize;-fno-omit-frame-pointer,-O3;-DNDEBUG>"
  )
endif()

# Everything below this boundary is test-only. Keep release/consumer
# configurations lean even though the historical test inventory is split across
# several independently guarded sections later in this file.
if(NOT ARPSID_BUILD_TESTS)
  return()
endif()

if (ARPSID_BUILD_TESTS)
  add_executable(arpsid_timing_phase2_tests source/tests/timing_phase2_tests.cpp)
  target_link_libraries(arpsid_timing_phase2_tests PRIVATE arpsid_core)
  add_test(NAME TimingPhase2Tests COMMAND arpsid_timing_phase2_tests)
endif()

if (ARPSID_BUILD_TESTS)
  add_executable(arpsid_wrapper_thinning_phase3_tests source/tests/wrapper_thinning_phase3_tests.cpp)
  target_link_libraries(arpsid_wrapper_thinning_phase3_tests PRIVATE arpsid_core)
  add_test(NAME WrapperThinningPhase3Tests COMMAND arpsid_wrapper_thinning_phase3_tests)
endif()

if (ARPSID_BUILD_TESTS)
  add_executable(arpsid_persistence_phase4_tests source/tests/persistence_phase4_tests.cpp)
  target_link_libraries(arpsid_persistence_phase4_tests PRIVATE arpsid_core)
  add_test(NAME PersistencePhase4Tests COMMAND arpsid_persistence_phase4_tests)
endif()


if(ARPSID_BUILD_TESTS)
  add_executable(arpsid_mono_unison_legato_v181_tests
    source/tests/mono_unison_legato_v181_tests.cpp
  )
  target_link_libraries(arpsid_mono_unison_legato_v181_tests PRIVATE arpsid_core)
  add_test(NAME MonoUnisonLegatoV181Tests COMMAND arpsid_mono_unison_legato_v181_tests)
endif()

if(ARPSID_BUILD_TESTS)
  add_executable(arpsid_mono_unison_pedal_v182_tests
    source/tests/mono_unison_pedal_v182_tests.cpp
  )
  target_link_libraries(arpsid_mono_unison_pedal_v182_tests PRIVATE arpsid_core)
  add_test(NAME MonoUnisonPedalV182Tests COMMAND arpsid_mono_unison_pedal_v182_tests)
endif()


if (ARPSID_BUILD_TESTS)
    add_executable(arpsid_mono_unison_pedal_active_count_v184_tests
        source/tests/mono_unison_pedal_active_count_v184_tests.cpp
    )
    target_link_libraries(arpsid_mono_unison_pedal_active_count_v184_tests PRIVATE arpsid_core)
    add_test(NAME MonoUnisonPedalActiveCountV184Tests COMMAND arpsid_mono_unison_pedal_active_count_v184_tests)
endif()

if(ARPSID_BUILD_TESTS)
  add_executable(arpsid_mono_unison_ledger_eviction_v185_tests
    source/tests/mono_unison_ledger_eviction_v185_tests.cpp
  )
  target_link_libraries(arpsid_mono_unison_ledger_eviction_v185_tests PRIVATE arpsid_core)
  add_test(NAME MonoUnisonLedgerEvictionV185Tests COMMAND arpsid_mono_unison_ledger_eviction_v185_tests)
endif()

if(ARPSID_BUILD_TESTS)
  add_executable(arpsid_mono_unison_steal_ledger_v186_tests
    source/tests/mono_unison_steal_ledger_v186_tests.cpp
  )
  target_link_libraries(arpsid_mono_unison_steal_ledger_v186_tests PRIVATE arpsid_core)
  add_test(NAME MonoUnisonStealLedgerV186Tests COMMAND arpsid_mono_unison_steal_ledger_v186_tests)
endif()

if(ARPSID_BUILD_TESTS)
  add_executable(arpsid_poly_sustain_gateoff_v187_tests source/tests/poly_sustain_gateoff_v187_tests.cpp)
  target_link_libraries(arpsid_poly_sustain_gateoff_v187_tests PRIVATE arpsid_core)
  add_test(NAME PolySustainGateOffV187Tests COMMAND arpsid_poly_sustain_gateoff_v187_tests)
endif()


if(ARPSID_BUILD_TESTS)

  add_executable(arpsid_drsid_gm_full_kit_v202_tests source/tests/drsid_gm_full_kit_v202_tests.cpp)
  target_compile_features(arpsid_drsid_gm_full_kit_v202_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_drsid_gm_full_kit_v202_tests PRIVATE arpsid_core)
  add_test(NAME DrSidGMFullKitV202Tests COMMAND arpsid_drsid_gm_full_kit_v202_tests)

  # Audit #5/#39/#74 — DrSID ↔ SID-808 context separation contract.
  # Pins DrumContext / DrumKitIdentity / factory-slot range invariants in
  # lock-step with the legacy `isAuthoredDrSidProjectionFactorySlot()` authority.
  add_executable(arpsid_drum_context_separation_v520_tests source/tests/drum_context_separation_v520_tests.cpp)
  target_compile_features(arpsid_drum_context_separation_v520_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_drum_context_separation_v520_tests PRIVATE arpsid_core)
  add_test(NAME DrumContextSeparationV520Tests COMMAND arpsid_drum_context_separation_v520_tests)

  # Audit #1/#14/#15 — RenderEpochCounter contract + wait-free SPSC scope
  # triple-buffer. Pins both data structures' single-thread and concurrent
  # invariants. Links pthread for the concurrent stress sections.
  add_executable(arpsid_render_epoch_and_scope_triple_buffer_v521_tests source/tests/render_epoch_and_scope_triple_buffer_v521_tests.cpp)
  target_compile_features(arpsid_render_epoch_and_scope_triple_buffer_v521_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_render_epoch_and_scope_triple_buffer_v521_tests PRIVATE arpsid_core)
  find_package(Threads REQUIRED)
  target_link_libraries(arpsid_render_epoch_and_scope_triple_buffer_v521_tests PRIVATE Threads::Threads)
  add_test(NAME RenderEpochAndScopeTripleBufferV521Tests COMMAND arpsid_render_epoch_and_scope_triple_buffer_v521_tests)

  # audit P0.4 — ScopeTripleBuffer non-destructive seqlock read must stay coherent
  # and starvation-free under multiple concurrent GUI telemetry consumers.
  add_executable(arpsid_scope_triple_buffer_multiconsumer_v687_tests source/tests/scope_triple_buffer_multiconsumer_v687_tests.cpp)
  target_compile_features(arpsid_scope_triple_buffer_multiconsumer_v687_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_scope_triple_buffer_multiconsumer_v687_tests PRIVATE arpsid_core)
  find_package(Threads REQUIRED)
  target_link_libraries(arpsid_scope_triple_buffer_multiconsumer_v687_tests PRIVATE Threads::Threads)
  add_test(NAME ScopeTripleBufferMultiConsumerV687Tests COMMAND arpsid_scope_triple_buffer_multiconsumer_v687_tests)

  # audit #10: the legacy try-lock LockFreeRing was removed (ingress moved to the
  # correct BoundedMpscRing). Its contention test went with it. See
  # bounded_mpsc_ring_v688_tests for the current ingress-ring coverage.

  # audit #8/#9/#10/#11 — correct bounded MPSC ingress ring: multi-producer,
  # single consumer, lossless except genuine fullness, FIFO per producer.
  add_executable(arpsid_bounded_mpsc_ring_v688_tests source/tests/bounded_mpsc_ring_v688_tests.cpp)
  target_compile_features(arpsid_bounded_mpsc_ring_v688_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_bounded_mpsc_ring_v688_tests PRIVATE Threads::Threads)
  target_include_directories(arpsid_bounded_mpsc_ring_v688_tests PRIVATE source)
  add_test(NAME BoundedMpscRingV688Tests COMMAND arpsid_bounded_mpsc_ring_v688_tests)

  # audit P0.1 regression — a NoteOn delivered through the midiQueue_ ingress ring
  # (enqueueMidiIntent: the host MusicDevice / UI MIDI path) must still trigger
  # audio after the generation-gate change. Guards against the ring epoch silently
  # dropping live MIDI. Links the factory patch bank for the DrSID test kit.
  add_executable(arpsid_midi_ingress_ring_path_v687_tests
                 source/tests/midi_ingress_ring_path_v687_tests.cpp
)
target_link_libraries(arpsid_midi_ingress_ring_path_v687_tests PRIVATE arpsid_forensic_patchbank)
  target_compile_features(arpsid_midi_ingress_ring_path_v687_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_midi_ingress_ring_path_v687_tests PRIVATE arpsid_core)
  target_link_libraries(arpsid_midi_ingress_ring_path_v687_tests PRIVATE Threads::Threads)
  target_include_directories(arpsid_midi_ingress_ring_path_v687_tests PRIVATE source source/au3)
  add_test(NAME MidiIngressRingPathV687Tests COMMAND arpsid_midi_ingress_ring_path_v687_tests)

  # Default-bank health gate: every factory slot must be audible on its
  # mode-appropriate note and (for sustained patches) release on NoteOff.
  add_executable(arpsid_factory_bank_audio_audit_v687_tests
                 source/tests/factory_bank_audio_audit_v687_tests.cpp
)
target_link_libraries(arpsid_factory_bank_audio_audit_v687_tests PRIVATE arpsid_forensic_patchbank)
  target_compile_features(arpsid_factory_bank_audio_audit_v687_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_factory_bank_audio_audit_v687_tests PRIVATE arpsid_core)
  target_link_libraries(arpsid_factory_bank_audio_audit_v687_tests PRIVATE Threads::Threads)
  target_include_directories(arpsid_factory_bank_audio_audit_v687_tests PRIVATE source source/au3)
  add_test(NAME FactoryBankAudioAuditV687Tests COMMAND arpsid_factory_bank_audio_audit_v687_tests)

  # Verifies the embedded stock C64 ROMs load and clear the MissingRealRoms /
  # RomIdentityUnverified physical-exactness blockers on both a bare platform and
  # an RSID runtime.
  add_executable(arpsid_c64_embedded_rom_integration_v687_tests
                 source/tests/c64_embedded_rom_integration_v687_tests.cpp)
  target_compile_features(arpsid_c64_embedded_rom_integration_v687_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_c64_embedded_rom_integration_v687_tests PRIVATE arpsid_core)
  target_link_libraries(arpsid_c64_embedded_rom_integration_v687_tests PRIVATE Threads::Threads)
  target_include_directories(arpsid_c64_embedded_rom_integration_v687_tests PRIVATE source source/au3)
  add_test(NAME C64EmbeddedRomIntegrationV687Tests COMMAND arpsid_c64_embedded_rom_integration_v687_tests)

  # Audit #10/#11 — SidWriteQueue + SidTimedEventQueue O(n²) → O(n log n)
  # sort, plus SidWriteQueue capacity raised to 65 536 to cover the
  # audit-stated worst-case ceiling.
  add_executable(arpsid_sid_write_queue_bucket_sort_v522_tests source/tests/sid_write_queue_bucket_sort_v522_tests.cpp)
  target_compile_features(arpsid_sid_write_queue_bucket_sort_v522_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_sid_write_queue_bucket_sort_v522_tests PRIVATE arpsid_core)
  add_test(NAME SidWriteQueueBucketSortV522Tests COMMAND arpsid_sid_write_queue_bucket_sort_v522_tests)

  # Audit DrSID §3/§4B — DrSidInstrumentProgram register-microprogram
  # contract. Pins layout (16-byte step, ≤kMaxSteps program), constexpr
  # validators, exemplar Kick/ClosedHat/Clap programs, and rejection of
  # every audit-listed malformation (non-monotonic cycle order, missing
  # terminal flag, schemaVersion drift, …).
  add_executable(arpsid_drsid_instrument_program_v523_tests source/tests/drsid_instrument_program_v523_tests.cpp)
  target_compile_features(arpsid_drsid_instrument_program_v523_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_drsid_instrument_program_v523_tests PRIVATE arpsid_core)
  add_test(NAME DrSidInstrumentProgramV523Tests COMMAND arpsid_drsid_instrument_program_v523_tests)

  # Audit #2/#3 — AUv2 render-notify RT-safety. Pins paired release/acquire
  # ordering on (proc, userData) so the seqlock-protected snapshot never
  # surfaces a torn pair, and verifies per-callback realtime-guard
  # violation attribution mirrors what `callAuv2RenderNotifyTable` does.
  add_executable(arpsid_auv2_render_notify_rt_safety_v524_tests source/tests/auv2_render_notify_rt_safety_v524_tests.cpp)
  target_compile_features(arpsid_auv2_render_notify_rt_safety_v524_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_auv2_render_notify_rt_safety_v524_tests PRIVATE arpsid_core)
  target_link_libraries(arpsid_auv2_render_notify_rt_safety_v524_tests PRIVATE Threads::Threads)
  add_test(NAME Auv2RenderNotifyRtSafetyV524Tests COMMAND arpsid_auv2_render_notify_rt_safety_v524_tests)

  # Audit #24/#25 — IngressFallbackEdgeRing ordering-critical MPSC ring +
  # pending_events_ drain-side push success checking. Pins sustain stomp
  # patterns, RPN/NRPN handshakes, transport play→stop→play edges, MPSC
  # producer-seq monotonicity, and overflow self-healing.
  add_executable(arpsid_ingress_fallback_edge_ring_v525_tests source/tests/ingress_fallback_edge_ring_v525_tests.cpp)
  target_compile_features(arpsid_ingress_fallback_edge_ring_v525_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_ingress_fallback_edge_ring_v525_tests PRIVATE arpsid_core)
  target_link_libraries(arpsid_ingress_fallback_edge_ring_v525_tests PRIVATE Threads::Threads)
  add_test(NAME IngressFallbackEdgeRingV525Tests COMMAND arpsid_ingress_fallback_edge_ring_v525_tests)

  # Slice 1/4/5 follow-up — closes the "ikke gjort" items:
  #   * DrumContext gate in factory-loader (slot→context refusal of cross-context defaults)
  #   * DrSidKitCompiler + CRC32 register-trace fingerprint
  #   * AUv2 wrapper followups (#4 raw bridged block lifetime, #5 scratch pre-render guard)
  #     are exercised through the wrapper build target, not unit-testable from core.
  add_executable(arpsid_audit_followups_v526_tests source/tests/audit_followups_v526_tests.cpp)
  target_compile_features(arpsid_audit_followups_v526_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_audit_followups_v526_tests PRIVATE arpsid_core)
  add_test(NAME AuditFollowupsV526Tests COMMAND arpsid_audit_followups_v526_tests)

  # P2 audit sweep — forensic engine staleness check + parameter block
  # contiguity + RO SID register filter consistency + DrumContext over the
  # canonical 180-slot factory bank + SID-808/Digi/DrSID kit invariants.
  # Audits: #56, #58, #59, #60, #67, #74.
  add_executable(arpsid_forensic_engine_sanity_v527_tests
                 source/tests/forensic_engine_sanity_v527_tests.cpp
)
target_link_libraries(arpsid_forensic_engine_sanity_v527_tests PRIVATE arpsid_forensic_patchbank)
  target_compile_features(arpsid_forensic_engine_sanity_v527_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_forensic_engine_sanity_v527_tests PRIVATE arpsid_core)
  add_test(NAME ForensicEngineSanityV527Tests COMMAND arpsid_forensic_engine_sanity_v527_tests)

  # v845 factory-bank "true C64-auth" pass — internal ring/sync sources run,
  # BP/HP/multi filter modes used, explicit $D417 route, synth-slot register
  # authorship, and SID-808 canonical 120..149 range. Audit items 1-8, 10.
  add_executable(arpsid_factory_bank_c64_auth_v845_tests
                 source/tests/factory_bank_c64_auth_v845_tests.cpp
)
target_link_libraries(arpsid_factory_bank_c64_auth_v845_tests PRIVATE arpsid_forensic_patchbank)
  target_compile_features(arpsid_factory_bank_c64_auth_v845_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_factory_bank_c64_auth_v845_tests PRIVATE arpsid_core)
  add_test(NAME FactoryBankC64AuthV845Tests COMMAND arpsid_factory_bank_c64_auth_v845_tests)

  # v846 realtime-safe C64 disassembly formatter — proves the render-thread
  # formatter is byte-identical to the previous snprintf layout and uses no
  # snprintf (locale-lock risk on the audio thread).
  add_executable(arpsid_c64_disasm_rt_format_v846_tests
                 source/tests/c64_disasm_rt_format_v846_tests.cpp)
  target_compile_features(arpsid_c64_disasm_rt_format_v846_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_c64_disasm_rt_format_v846_tests PRIVATE arpsid_core)
  add_test(NAME C64DisasmRtFormatV846Tests COMMAND arpsid_c64_disasm_rt_format_v846_tests)

  # v847 poly stuck-note loose-release fallback — an anonymous note-off can no
  # longer leave an id-bearing voice gated forever; sustain and note/channel
  # scoping are still honoured.
  add_executable(arpsid_poly_stuck_note_loose_release_v847_tests
                 source/tests/poly_stuck_note_loose_release_v847_tests.cpp)
  target_compile_features(arpsid_poly_stuck_note_loose_release_v847_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_poly_stuck_note_loose_release_v847_tests PRIVATE arpsid_core)
  add_test(NAME PolyStuckNoteLooseReleaseV847Tests COMMAND arpsid_poly_stuck_note_loose_release_v847_tests)

  # v872 P1-8: pinned anonymous same-note release policy (LIFO, no stuck gate).
  add_executable(arpsid_synthmode_anonymous_release_lifo_v872_tests
                 source/tests/synthmode_anonymous_release_lifo_v872_tests.cpp)
  target_compile_features(arpsid_synthmode_anonymous_release_lifo_v872_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_synthmode_anonymous_release_lifo_v872_tests PRIVATE arpsid_core)
  add_test(NAME SynthModeAnonymousReleaseLifoV872Tests COMMAND arpsid_synthmode_anonymous_release_lifo_v872_tests)

  # v872 P1-7: canonical held-note replay + release law (no stuck voice).
  add_executable(arpsid_synthmode_held_replay_release_v872_tests
                 source/tests/synthmode_held_replay_release_v872_tests.cpp)
  target_compile_features(arpsid_synthmode_held_replay_release_v872_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_synthmode_held_replay_release_v872_tests PRIVATE arpsid_core)
  add_test(NAME SynthModeHeldReplayReleaseV872Tests COMMAND arpsid_synthmode_held_replay_release_v872_tests)

  # v848 GM drum key-map reference helpers surfaced on the drum tabs.
  add_executable(arpsid_gm_drum_map_reference_v848_tests
                 source/tests/gm_drum_map_reference_v848_tests.cpp)
  target_compile_features(arpsid_gm_drum_map_reference_v848_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_gm_drum_map_reference_v848_tests PRIVATE arpsid_core)
  add_test(NAME GmDrumMapReferenceV848Tests COMMAND arpsid_gm_drum_map_reference_v848_tests)

  # v849 low-level PSID/RSID parser robustness — no crash/over-read on malformed
  # or truncated files, exact rejection codes, and psidLoadIntoRam RAM clamp.
  add_executable(arpsid_psid_parser_robustness_v849_tests
                 source/tests/psid_parser_robustness_v849_tests.cpp)
  target_compile_features(arpsid_psid_parser_robustness_v849_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_psid_parser_robustness_v849_tests PRIVATE arpsid_core)
  add_test(NAME PsidParserRobustnessV849Tests COMMAND arpsid_psid_parser_robustness_v849_tests)

  # v850 factory patch load->apply round-trip — every audio-authority param the
  # factory authors survives into the applied state root (no silent drop).
  add_executable(arpsid_factory_patch_apply_roundtrip_v850_tests
                 source/tests/factory_patch_apply_roundtrip_v850_tests.cpp
)
target_link_libraries(arpsid_factory_patch_apply_roundtrip_v850_tests PRIVATE arpsid_forensic_patchbank)
  target_compile_features(arpsid_factory_patch_apply_roundtrip_v850_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_factory_patch_apply_roundtrip_v850_tests PRIVATE arpsid_core)
  add_test(NAME FactoryPatchApplyRoundtripV850Tests COMMAND arpsid_factory_patch_apply_roundtrip_v850_tests)

  # v852 poly orphan-voice reconciliation — stuck-note safety net grace period,
  # pedal/held preservation and grace reset.
  add_executable(arpsid_poly_orphan_reconcile_v852_tests
                 source/tests/poly_orphan_reconcile_v852_tests.cpp)
  target_compile_features(arpsid_poly_orphan_reconcile_v852_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_poly_orphan_reconcile_v852_tests PRIVATE arpsid_core)
  add_test(NAME PolyOrphanReconcileV852Tests COMMAND arpsid_poly_orphan_reconcile_v852_tests)

  # v853 exhaustive 6510 micro-CPU opcode matrix + documented cycle-count audit.
  add_executable(arpsid_cpu6510_micro_opcode_matrix_v853_tests
                 source/tests/cpu6510_micro_opcode_matrix_v853_tests.cpp)
  target_compile_features(arpsid_cpu6510_micro_opcode_matrix_v853_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_cpu6510_micro_opcode_matrix_v853_tests PRIVATE arpsid_core)
  add_test(NAME Cpu6510MicroOpcodeMatrixV853Tests COMMAND arpsid_cpu6510_micro_opcode_matrix_v853_tests)

  # v854 ultra-low-level SID core exactness — envelope rate-15 period 31251,
  # exponential divisor boundary latching, PW=$FFF spike comparator.
  add_executable(arpsid_sid_core_exactness_v854_tests
                 source/tests/sid_core_exactness_v854_tests.cpp)
  target_compile_features(arpsid_sid_core_exactness_v854_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_sid_core_exactness_v854_tests PRIVATE arpsid_core)
  add_test(NAME SidCoreExactnessV854Tests COMMAND arpsid_sid_core_exactness_v854_tests)

  # v855 P0 timing fix — PSID VBI/CIA play-routine SID writes must reach the
  # audio timed-write bridge with PHI2 stamps; $D41B readback PW=$FFF spike.
  add_executable(arpsid_c64_play_bridge_routing_v855_tests
                 source/tests/c64_play_bridge_routing_v855_tests.cpp)
  target_compile_features(arpsid_c64_play_bridge_routing_v855_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_c64_play_bridge_routing_v855_tests PRIVATE arpsid_core)
  add_test(NAME C64PlayBridgeRoutingV855Tests COMMAND arpsid_c64_play_bridge_routing_v855_tests)

  # v864 SID-core audit closure — TEST/noise reset parity, hard-restart gate
  # timing, sync no-cascade readback parity, filter mode/cutoff law,
  # $D418 volume-zero DC behavior, native cycle/subcycle advancement, and
  # half-open timed-write interval semantics.
  add_executable(arpsid_sid_core_audit_v864_tests
                 source/tests/sid_core_audit_v864_tests.cpp)
  target_compile_features(arpsid_sid_core_audit_v864_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_sid_core_audit_v864_tests PRIVATE arpsid_core)
  add_test(NAME SidCoreAuditV864Tests COMMAND arpsid_sid_core_audit_v864_tests)

  # P2 audit AUv2/AUv3 wrapper fixes — #49 doubled-notify removal, #50 host
  # buffer mutation diagnostic, #51 AUv3 scratch-resize epoch + lock,
  # #52 AUv3 scratch under-capacity silent fail-closed, #55 chunked render
  # event-slicing + tempo determinism.
  add_executable(arpsid_auv2_wrapper_p2_fixes_v528_tests source/tests/auv2_wrapper_p2_fixes_v528_tests.cpp)
  target_compile_features(arpsid_auv2_wrapper_p2_fixes_v528_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_auv2_wrapper_p2_fixes_v528_tests PRIVATE arpsid_core)
  add_test(NAME Auv2WrapperP2FixesV528Tests COMMAND arpsid_auv2_wrapper_p2_fixes_v528_tests)

  # Audit #69 + multi-sample-rate completeness — true rendered-audio
  # fingerprint test that instantiates a SIDChip, drives a deterministic
  # voice setup at 44.1/48/88.2/96/176.4/192 kHz, and pins:
  #   * canonicalizeHostSampleRate handles every documented rate
  #   * host_transport_snapshot sanitizes edge SRs
  #   * c64_timing_math::psidCiaPlayPeriodSamples scales linearly with SR
  #   * SIDChip output is finite, deterministic, RMS-consistent across SRs
  #   * Audible pitch is invariant under host SR (SID register state
  #     produces ~440 Hz at every rate)
  add_executable(arpsid_multi_sample_rate_render_fingerprint_v529_tests
                 source/tests/multi_sample_rate_render_fingerprint_v529_tests.cpp)
  target_compile_features(arpsid_multi_sample_rate_render_fingerprint_v529_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_multi_sample_rate_render_fingerprint_v529_tests PRIVATE arpsid_core)
  add_test(NAME MultiSampleRateRenderFingerprintV529Tests
           COMMAND arpsid_multi_sample_rate_render_fingerprint_v529_tests)

  # Audit #29 — single-SID 3-voice engine. Parallel architecture that delivers
  # cycle-accurate single-chip C64 behavior (1 SIDChip, 3 voices, voice
  # stealing) alongside the existing N-chip "synth polyphony" path. Pins:
  #   * SidVoiceAllocator (3/6/9 slots, all 4 stealing policies, choke groups)
  #   * SingleSidThreeVoiceEngine deterministic render at all 6 canonical SRs
  #   * 3-voice cap honored; 4th note steals
  #   * Engine sizeof < 2× SIDChip (catches accidental per-voice-chip array drift)
  add_executable(arpsid_single_sid_three_voice_engine_v530_tests
                 source/tests/single_sid_three_voice_engine_v530_tests.cpp)
  target_compile_features(arpsid_single_sid_three_voice_engine_v530_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_single_sid_three_voice_engine_v530_tests PRIVATE arpsid_core)
  add_test(NAME SingleSidThreeVoiceEngineV530Tests
           COMMAND arpsid_single_sid_three_voice_engine_v530_tests)

  # Audit #30/#34/#35/#36/#38 — BitPerfectEngine + SIDChip DSP-authenticity
  # batch. Pins: scope averaging removal, uint64 limiter counter widths,
  # L/R RNG independence, output-stage dither, ±8 filter abs-clamp.
  add_executable(arpsid_bitperfect_dsp_authenticity_v531_tests
                 source/tests/bitperfect_dsp_authenticity_v531_tests.cpp)
  target_compile_features(arpsid_bitperfect_dsp_authenticity_v531_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_bitperfect_dsp_authenticity_v531_tests PRIVATE arpsid_core)
  add_test(NAME BitperfectDspAuthenticityV531Tests
           COMMAND arpsid_bitperfect_dsp_authenticity_v531_tests)

  # Audit #31/#32 filter unification + #29 BitPerfectEngine topology-mode
  # wire-up. Pins: FilterCore helpers, SIDFilter and SidRegisterEngine both
  # using the same nonlinearity / feedback / leak laws, BitPerfectEngine
  # SidChipTopologyMode switching, single-SID 3-voice rendering through
  # the engine's processBlock, multi-chip path unaffected by the wire-up.
  add_executable(arpsid_filter_core_unification_and_topology_mode_v532_tests
                 source/tests/filter_core_unification_and_topology_mode_v532_tests.cpp)
  target_compile_features(arpsid_filter_core_unification_and_topology_mode_v532_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_filter_core_unification_and_topology_mode_v532_tests PRIVATE arpsid_core)
  add_test(NAME FilterCoreUnificationAndTopologyModeV532Tests
           COMMAND arpsid_filter_core_unification_and_topology_mode_v532_tests)

  # Audit #33 zero-cycle sub-cycle interpolation + #37 canonical
  # parameter smoothing. Pins the new helpers + transient detector +
  # SIDChip's zero-cycle counter at very-high SRs.
  add_executable(arpsid_zero_cycle_and_param_smoothing_v533_tests
                 source/tests/zero_cycle_and_param_smoothing_v533_tests.cpp)
  target_compile_features(arpsid_zero_cycle_and_param_smoothing_v533_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_zero_cycle_and_param_smoothing_v533_tests PRIVATE arpsid_core)
  add_test(NAME ZeroCycleAndParamSmoothingV533Tests
           COMMAND arpsid_zero_cycle_and_param_smoothing_v533_tests)

  # Audit #45 (silent-PAL-fallback fix) + #41/#42 (typed voice allocator
  # wire-helper on DrSidEngine). Later v535+ tests complete the DrSID/SID-808
  # split; this target keeps the original clock/allocation contract pinned.
  add_executable(arpsid_drsid_clock_and_voice_alloc_v534_tests
                 source/tests/drsid_clock_and_voice_alloc_v534_tests.cpp)
  target_compile_features(arpsid_drsid_clock_and_voice_alloc_v534_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_drsid_clock_and_voice_alloc_v534_tests PRIVATE arpsid_core)
  add_test(NAME DrSidClockAndVoiceAllocV534Tests
           COMMAND arpsid_drsid_clock_and_voice_alloc_v534_tests)

  # Audit #39 / #74 engine-split — new Sid808Engine (analog x0x projection,
  # parallel to legacy DrSidEngine) + DrumEngineRouter dispatching via
  # DrumContext. Closes the architectural split-brain audit identified
  # without rewriting the legacy DrSidEngine path (zero audio regression).
  add_executable(arpsid_sid808_engine_and_router_v535_tests
                 source/tests/sid808_engine_and_router_v535_tests.cpp)
  target_compile_features(arpsid_sid808_engine_and_router_v535_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_sid808_engine_and_router_v535_tests PRIVATE arpsid_core)
  add_test(NAME Sid808EngineAndRouterV535Tests
           COMMAND arpsid_sid808_engine_and_router_v535_tests)

  # Engine-split wire-up — factory SID-808 kit configs for slots 120-149
  # + DrSidWavetableProgramRunner (audit #43/#44 register-microprogram
  # rendering). Closes the architectural gap left after skive 535.
  add_executable(arpsid_factory_sid808_kits_and_wavetable_runner_v536_tests
                 source/tests/factory_sid808_kits_and_wavetable_runner_v536_tests.cpp)
  target_compile_features(arpsid_factory_sid808_kits_and_wavetable_runner_v536_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_factory_sid808_kits_and_wavetable_runner_v536_tests PRIVATE arpsid_core)
  add_test(NAME FactorySid808KitsAndWavetableRunnerV536Tests
           COMMAND arpsid_factory_sid808_kits_and_wavetable_runner_v536_tests)

  # Audit #40 (DrSID restore overlay-driven model surprise) + B (wire-up
  # of engine-split architecture via DrumEngineHostBridge — host-side
  # reference integration). Pins serialized-model-as-ground-truth on
  # restore + bridge dispatch over factory slots.
  add_executable(arpsid_drsid_restore_and_host_bridge_v537_tests
                 source/tests/drsid_restore_and_host_bridge_v537_tests.cpp)
  target_compile_features(arpsid_drsid_restore_and_host_bridge_v537_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_drsid_restore_and_host_bridge_v537_tests PRIVATE arpsid_core)
  add_test(NAME DrSidRestoreAndHostBridgeV537Tests
           COMMAND arpsid_drsid_restore_and_host_bridge_v537_tests)

  # AUv2 bridge embedding pattern + P2 pluck batch (#61 host-MIDI bridge
  # params runtime-only, #62 unique sequencer step names, #63 dense vs
  # semantic param-space consistency).
  add_executable(arpsid_auv2_bridge_and_p2_pluck_v538_tests
                 source/tests/auv2_bridge_and_p2_pluck_v538_tests.cpp)
  target_compile_features(arpsid_auv2_bridge_and_p2_pluck_v538_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_auv2_bridge_and_p2_pluck_v538_tests PRIVATE arpsid_core)
  add_test(NAME Auv2BridgeAndP2PluckV538Tests
           COMMAND arpsid_auv2_bridge_and_p2_pluck_v538_tests)

  # A — AUv2 lifecycle wire-up of DrumEngineHostBridge (bridge.prepare()
  # called on every lifecycle event); B — audit #65/#66 PSID diagnostic
  # counters (video-standard fallback + render-handoff).
  add_executable(arpsid_auv2_bridge_lifecycle_and_psid_diag_v539_tests
                 source/tests/auv2_bridge_lifecycle_and_psid_diag_v539_tests.cpp)
  target_compile_features(arpsid_auv2_bridge_lifecycle_and_psid_diag_v539_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_auv2_bridge_lifecycle_and_psid_diag_v539_tests PRIVATE arpsid_core)
  add_test(NAME Auv2BridgeLifecycleAndPsidDiagV539Tests
           COMMAND arpsid_auv2_bridge_lifecycle_and_psid_diag_v539_tests)

  # AUv2 audio-divert gate (audit #39/#74 release-candidate wire-up).
  # Pins the two-AND-gate logic that componentRender now uses to choose
  # between bridge.processBlock and the wrapped renderBlock.
  add_executable(arpsid_bridge_divert_gate_v540_tests
                 source/tests/bridge_divert_gate_v540_tests.cpp)
  target_compile_features(arpsid_bridge_divert_gate_v540_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_bridge_divert_gate_v540_tests PRIVATE arpsid_core)
  add_test(NAME BridgeDivertGateV540Tests
           COMMAND arpsid_bridge_divert_gate_v540_tests)

  # Audit #46 — AuditedMutex<Policy>: compile-time RT-policy tags +
  # runtime detection of render-thread mutex acquisition.
  add_executable(arpsid_audited_mutex_v541_tests
                 source/tests/audited_mutex_v541_tests.cpp)
  target_compile_features(arpsid_audited_mutex_v541_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_audited_mutex_v541_tests PRIVATE arpsid_core)
  target_link_libraries(arpsid_audited_mutex_v541_tests PRIVATE Threads::Threads)
  add_test(NAME AuditedMutexV541Tests
           COMMAND arpsid_audited_mutex_v541_tests)

  # Audit #46 migration pin — AUv2's 4 instance mutexes are now
  # AuditedXxxMutex (drop-in API-compatible). Pins end-to-end behavior:
  # each AUv2 mutex pattern (lock_guard, unique_lock, cv_any wait/notify,
  # nested mutex acquisition) works with the audited types AND detects
  # render-thread acquisitions.
  add_executable(arpsid_audited_mutex_migration_v542_tests
                 source/tests/audited_mutex_migration_v542_tests.cpp)
  target_compile_features(arpsid_audited_mutex_migration_v542_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_audited_mutex_migration_v542_tests PRIVATE arpsid_core)
  target_link_libraries(arpsid_audited_mutex_migration_v542_tests PRIVATE Threads::Threads)
  add_test(NAME AuditedMutexMigrationV542Tests
           COMMAND arpsid_audited_mutex_migration_v542_tests)

  # v0.0.444 — GUI tab architecture contract (9-tab inventory, all implemented).
  # Pins the canonical tab order, status flags, HUD labels,
  # primary DrumContext per tab, and implementation contract strings.
  add_executable(arpsid_gui_tab_architecture_v543_tests
                 source/tests/gui_tab_architecture_v543_tests.cpp)
  target_compile_features(arpsid_gui_tab_architecture_v543_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_gui_tab_architecture_v543_tests PRIVATE arpsid_core)
  add_test(NAME GuiTabArchitectureV543Tests
           COMMAND arpsid_gui_tab_architecture_v543_tests)

  # v544 — SETTINGS tab data model (per TAB_ARCHITECTURE.md §8). Pins POD
  # layout, enum domains, defaults, sanitization, round-trip persistence,
  # and the BitPerfectEngine topology-mode wire-up.
  add_executable(arpsid_settings_panel_model_v544_tests
                 source/tests/settings_panel_model_v544_tests.cpp)
  target_compile_features(arpsid_settings_panel_model_v544_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_settings_panel_model_v544_tests PRIVATE arpsid_core)
  add_test(NAME SettingsPanelModelV544Tests
           COMMAND arpsid_settings_panel_model_v544_tests)

  # v545 — SIDCORE tab data model + v544 tab inventory wire-up evidence.
  add_executable(arpsid_sidcore_panel_model_and_tab_wire_v545_tests
                 source/tests/sidcore_panel_model_and_tab_wire_v545_tests.cpp)
  target_compile_features(arpsid_sidcore_panel_model_and_tab_wire_v545_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_sidcore_panel_model_and_tab_wire_v545_tests PRIVATE arpsid_core)
  add_test(NAME SidCorePanelModelAndTabWireV545Tests
           COMMAND arpsid_sidcore_panel_model_and_tab_wire_v545_tests)

  # v546 — wire-up completeness pin for the v544 tabs in ArpSIDViewController.
  # Pins that the AUv2 build target succeeded with all tooltip/label switches
  # extended to cover indices 14 (SETTINGS) and 15 (C64-STATE), the new tab
  # enum constants don't collide with existing tabs, and the model layers
  # (SettingsPanelModel + SidCorePanelModel) are wire-up-ready.
  add_executable(arpsid_v544_tab_wire_completeness_v546_tests
                 source/tests/v544_tab_wire_completeness_v546_tests.cpp)
  target_compile_features(arpsid_v544_tab_wire_completeness_v546_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_v544_tab_wire_completeness_v546_tests PRIVATE arpsid_core)
  add_test(NAME V544TabWireCompletenessV546Tests
           COMMAND arpsid_v544_tab_wire_completeness_v546_tests)

  # v547 — MIX tab data model + FX-chain architecture (TAB_ARCHITECTURE.md §5).
  # Pins MixChannel/MixSendBus/MixMaster/MixPanelModel layouts, defaults,
  # solo law, audible/mute interaction, FX-slot type configuration,
  # validator behavior.
  add_executable(arpsid_mix_panel_model_v547_tests
                 source/tests/mix_panel_model_v547_tests.cpp)
  target_compile_features(arpsid_mix_panel_model_v547_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_mix_panel_model_v547_tests PRIVATE arpsid_core)
  add_test(NAME MixPanelModelV547Tests
           COMMAND arpsid_mix_panel_model_v547_tests)

  add_executable(arpsid_mix_panel_tab_v548_tests
                 source/tests/mix_panel_tab_v548_tests.cpp)
  target_compile_features(arpsid_mix_panel_tab_v548_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_mix_panel_tab_v548_tests PRIVATE arpsid_core)
  add_test(NAME MixPanelTabV548Tests
           COMMAND arpsid_mix_panel_tab_v548_tests)

  add_executable(arpsid_diag_counter_snapshot_v549_tests
                 source/tests/diag_counter_snapshot_v549_tests.cpp)
  target_compile_features(arpsid_diag_counter_snapshot_v549_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_diag_counter_snapshot_v549_tests PRIVATE arpsid_core)
  add_test(NAME DiagCounterSnapshotV549Tests
           COMMAND arpsid_diag_counter_snapshot_v549_tests)

  # v550 — SIDCORE register-write feed contract tests.
  # Exercises SidCoreRegisterWriteRing push/drain, overflow detection,
  # SidCorePanelModel publish/peek round-trip, and reset semantics.
  add_executable(arpsid_sidcore_feed_v550_tests
                 source/tests/sidcore_feed_v550_tests.cpp)
  target_compile_features(arpsid_sidcore_feed_v550_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_sidcore_feed_v550_tests PRIVATE arpsid_core)
  add_test(NAME SidCoreFeedV550Tests
           COMMAND arpsid_sidcore_feed_v550_tests)

  # v551 — SettingsPanelModel persistence contract tests.
  # Exercises serialize/deserialize round-trip, sanitize clamping,
  # schema-mismatch handling, and byte-layout spot-check.
  add_executable(arpsid_settings_persistence_v551_tests
                 source/tests/settings_persistence_v551_tests.cpp)
  target_compile_features(arpsid_settings_persistence_v551_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_settings_persistence_v551_tests PRIVATE arpsid_core)
  add_test(NAME SettingsPersistenceV551Tests
           COMMAND arpsid_settings_persistence_v551_tests)

  add_executable(arpsid_settings_theme_v552_tests
                 source/tests/settings_theme_v552_tests.cpp)
  target_compile_features(arpsid_settings_theme_v552_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_settings_theme_v552_tests PRIVATE arpsid_core)
  add_test(NAME SettingsThemeV552Tests
           COMMAND arpsid_settings_theme_v552_tests)

  add_executable(arpsid_settings_language_v553_tests
                 source/tests/settings_language_v553_tests.cpp)
  target_compile_features(arpsid_settings_language_v553_tests PRIVATE cxx_std_17)
  target_link_libraries(arpsid_settings_language_v553_tests PRIVATE arpsid_core)
  add_test(NAME SettingsLanguageV553Tests
           COMMAND arpsid_settings_language_v553_tests)

  # v559 — KIT assign config model contract tests.
  # Covers: layout pin (KitAssignConfig=8B, Grid=80B), trivially-copyable,
  # schema version, default config (slot=dc/tune=0/start=0/len=0/no-flags),
  # well-formedness guards (slot OOB, tuneShiftBias OOB, flags upper bits),
  # slot round-trip + clamp, tune shift bias encoding + round-trip + clamp ±12,
  # start/length full uint8 range, flag bit independence, full 9-class coverage.
  add_executable(arpsid_kit_assign_config_v559_tests
                 source/tests/kit_assign_config_v559_tests.cpp)
  target_compile_features(arpsid_kit_assign_config_v559_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_kit_assign_config_v559_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_kit_assign_config_v559_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_kit_assign_config_v559_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME KitAssignConfigV559Tests
           COMMAND arpsid_kit_assign_config_v559_tests)

  # v561 — MIX tab state persistence contract tests.
  # Covers: layout pin (MixPanelModel=1264B, trivially copyable, schema version=1,
  # sub-struct sizes), default model well-formed + field spot-checks, sanitize
  # unchanged on well-formed model, sanitize schema mismatch → full reset,
  # sanitize bad per-channel field (enabled>1, FX type OOR, selectedChannel OOR),
  # memcpy round-trip via raw byte array with field preservation.
  # v564 — DIGI tab NSView builder + wire-up contract tests.
  # Covers: ArpSIDTabDigiV563=18 enum pin, visible-tab array counts (18/16/17/17),
  # DigiPanelModel handle-aware layout, DIGI tag range non-collision with KIT/MIX
  # (0xC000+ above 0xBFFF), intra-DIGI tag uniqueness (56 distinct tags), loop
  # bounds (8 slots × 32 steps), default model matches builder initial state.
  add_executable(arpsid_digi_panel_tab_v564_tests
                 source/tests/digi_panel_tab_v564_tests.cpp)
  target_compile_features(arpsid_digi_panel_tab_v564_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_digi_panel_tab_v564_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_digi_panel_tab_v564_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_digi_panel_tab_v564_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME DigiPanelTabV564Tests
           COMMAND arpsid_digi_panel_tab_v564_tests)

  # v563 — DigiPanelModel data model contract tests.
  # Covers: static layout pins (DigiSampleSlot=12B, DigiPanelModel=360B,
  # trivially copyable, all constants), default slot+model well-formed,
  # digiSampleSlotIsWellFormed per-field guards (sourceType OOR, factorySlot
  # OOR, tuneShiftBias OOR, reserved flags), digiPanelIsWellFormed guards
  # (schema/activeSlot/step-velocity), accessor round-trips (tuneShift,
  # loop/reverse, step OOB-safety), mutator contracts (setTuneShift clamp,
  # setFactorySlot clamp, flag independence, step set/inactive/toggle/clearSlot,
  # full 8×32 coverage), sanitize no-op on well-formed, sanitize bad model →
  # defaults, memcpy round-trip with full field preservation.
  add_executable(arpsid_digi_panel_model_v563_tests
                 source/tests/digi_panel_model_v563_tests.cpp)
  target_compile_features(arpsid_digi_panel_model_v563_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_digi_panel_model_v563_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_digi_panel_model_v563_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_digi_panel_model_v563_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME DigiPanelModelV563Tests
           COMMAND arpsid_digi_panel_model_v563_tests)

  # v565 — DIGI AU state persistence contract tests.
  # Covers: handle-aware model blob size, legacy 328-byte migration, saved
  # sample-bank blob size, default/mutated/step round-trips, sanitize
  # idempotence/reset behavior, and saved sample-bank payload round-trip.
  add_executable(arpsid_digi_state_persistence_v565_tests
                 source/tests/digi_state_persistence_v565_tests.cpp)
  target_compile_features(arpsid_digi_state_persistence_v565_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_digi_state_persistence_v565_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_digi_state_persistence_v565_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_digi_state_persistence_v565_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME DigiStatePersistenceV565Tests
           COMMAND arpsid_digi_state_persistence_v565_tests)

  # v596 - DIGI saved user-sample bank contract tests.
  add_executable(arpsid_digi_sample_bank_v596_tests
                 source/tests/digi_sample_bank_v596_tests.cpp)
  target_compile_features(arpsid_digi_sample_bank_v596_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_digi_sample_bank_v596_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_digi_sample_bank_v596_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_digi_sample_bank_v596_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME DigiSampleBankV596Tests
           COMMAND arpsid_digi_sample_bank_v596_tests)

  add_executable(arpsid_mix_state_persistence_v561_tests
                 source/tests/mix_state_persistence_v561_tests.cpp)
  target_compile_features(arpsid_mix_state_persistence_v561_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_mix_state_persistence_v561_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_mix_state_persistence_v561_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_mix_state_persistence_v561_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME MixStatePersistenceV561Tests
           COMMAND arpsid_mix_state_persistence_v561_tests)

  # v562 — Tab architecture status promotion contract tests.
  # Covers: compile-time count pins, per-tab status static_asserts for all 9
  # tabs, runtime inventory consistency
  # (ids match enum order, tabSpec accessor parity), HUD label length invariant.
  add_executable(arpsid_tab_architecture_promotion_v562_tests
                 source/tests/tab_architecture_promotion_v562_tests.cpp)
  target_compile_features(arpsid_tab_architecture_promotion_v562_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_tab_architecture_promotion_v562_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_tab_architecture_promotion_v562_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_tab_architecture_promotion_v562_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME TabArchitecturePromotionV562Tests
           COMMAND arpsid_tab_architecture_promotion_v562_tests)

  # v566/v581 — Tab architecture status promotion contract tests.
  # Covers: DIGI and C64STATE promoted to Implemented, all 9 tabs individually
  # asserted, runtime inventory consistency,
  # HUD label length invariant.
  add_executable(arpsid_tab_architecture_promotion_v566_tests
                 source/tests/tab_architecture_promotion_v566_tests.cpp)
  target_compile_features(arpsid_tab_architecture_promotion_v566_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_tab_architecture_promotion_v566_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_tab_architecture_promotion_v566_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_tab_architecture_promotion_v566_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME TabArchitecturePromotionV566Tests
           COMMAND arpsid_tab_architecture_promotion_v566_tests)

  # v573 — Output limiter state-restore contract tests.
  # Fixes resetRenderModeOutputNormalizer_() in arpsid_processor_phase2.cpp:
  # limiterEnabled, limiterThreshold, limiterAttackMs, limiterReleaseMs (and the
  # SimpleLimiter object time constants) were not re-projected from paramValues[]
  # on setState() / preset restore.  reverbMix was already recovered on line 1676;
  # the four limiter fields are now projected using the same transformations as
  # runtimePolicySetLimiter* in sid_runtime_parameter_services.h.
  # C++ field defaults diverged from param defaults: threshold 0.97 vs 0.94,
  # attack 0.5 ms vs 1.6 ms (3.2× too fast), release 200 ms vs 356.5 ms (56% short).
  # Pins: enabled formula (>0.5), threshold clamp [0.5,1], attack [0,20] ms,
  # release [10,1000] ms, default param alignment, pre-fix stale default
  # documentation, monotonicity, clamp guards, SimpleLimiter coeff contracts.
  add_executable(arpsid_limiter_state_restore_v573_tests
                 source/tests/limiter_state_restore_v573_tests.cpp)
  target_compile_features(arpsid_limiter_state_restore_v573_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_limiter_state_restore_v573_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_link_libraries(arpsid_limiter_state_restore_v573_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_limiter_state_restore_v573_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME LimiterStateRestoreV573Tests
           COMMAND arpsid_limiter_state_restore_v573_tests)

  # v574 — C64 PSID play instruction budget contract tests.
  # Fixes ArpSIDDSPKernel::kC64PsidMaxInstructionsPerPlay: raised from 2048 to 4096.
  # Complex PSID tunes (Rob Hubbard, Jeroen Tel) require 3000-6000+ 6502 instructions
  # per VBI play call.  With the 2048-instruction cap, runPlay() returned false
  # (budget exhausted) on every VBI frame: SID registers were only partially updated
  # (whatever writes occurred before the cap) → missing gate-ons, dropped frequency
  # and envelope updates → choppy audio.  4096 matches C64Runtime::runPlay()'s own
  # default parameter and covers the vast majority of real-world PSID tunes.
  # Pins: budget constant == 4096 (not 2048), simple play completes within budget,
  # medium-complexity (~2040 NOP) routine completes at 4096 but may fail at 2048,
  # infinite-loop guard still fires at limit, partial play produces SID writes (not
  # silent), return-value semantics (complete=true/partial=false), CPU real-time
  # budget (4096 instr ≈ 61 µs << 11.6 ms block deadline), monotone budget effect,
  # budget matches C64Runtime default parameter.
  add_executable(arpsid_c64_psid_play_budget_v574_tests
                 source/tests/c64_psid_play_budget_v574_tests.cpp)
  target_compile_features(arpsid_c64_psid_play_budget_v574_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_c64_psid_play_budget_v574_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_link_libraries(arpsid_c64_psid_play_budget_v574_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_c64_psid_play_budget_v574_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME C64PsidPlayBudgetV574Tests
           COMMAND arpsid_c64_psid_play_budget_v574_tests)

  # v575 — C64 PSID VBI PlayBase timing contract tests.
  # Fixes ArpSIDDSPKernel::renderC64PsidBlockIfActive_() VBI path: PlayBase was
  # recorded BEFORE the 8192-cycle passive PHI2 advancement, displacing every
  # VBI SID write by kC64PsidMaxPassiveCatchupCyclesPerPlay cycles (8192 PAL
  # cycles ≈ 367 audio samples at 44.1 kHz PAL).  Notes started/stopped 8.3 ms
  # too late every VBI frame.  When playSample + 367 exceeded the block size,
  # writes were clamped to the last sample → irregular VBI update intervals
  # (656, 1024 samples instead of the correct 882) → audible choppiness.
  # Fix: move PlayBase recording to AFTER platform.runCycles() in the VBI path.
  # CIA path is unaffected (PlayBase is correctly recorded before CIA service).
  # Pins: passive-displacement constant (8192 cycles == 367 samples at PAL
  # 44.1 kHz), pre-fix math documents 367-sample error, post-fix writes land at
  # ~playSample, block-boundary clamping behaviour, first-frame silence fix,
  # fixed timing (first SID write at ~playSample ± 4), multi-block VBI period
  # regularity (~882 samples), displacement formula at PAL/48kHz/NTSC, CIA path
  # unchanged.
  add_executable(arpsid_c64_psid_playbase_timing_v575_tests
                 source/tests/c64_psid_playbase_timing_v575_tests.cpp)
  target_compile_features(arpsid_c64_psid_playbase_timing_v575_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_c64_psid_playbase_timing_v575_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_link_libraries(arpsid_c64_psid_playbase_timing_v575_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_c64_psid_playbase_timing_v575_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME C64PsidPlayBaseTimingV575Tests
           COMMAND arpsid_c64_psid_playbase_timing_v575_tests)

  # v576 — C64 PSID VBI passive cycle budget contract tests.
  # Fixes ArpSIDDSPKernel VBI path: passive advancement capped at 8192 cycles
  # (kC64PsidMaxPassiveCatchupCyclesPerPlay = 41.7% of PAL VBI frame 19656).
  # CIA Timer A and VIC counters only advanced 41.7% of a VBI period between
  # play calls.  Tunes using CIA timers for arpeggios/vibrato inside the play
  # routine saw the timer at wrong phase (11513 cycles from fire instead of 49),
  # producing wrong arpeggio/vibrato rates or missed timer events.
  # Fix: advance playPhi2Cycles (full VBI frame: 19656 PAL / 17095 NTSC) per
  # play call.  CIA path unchanged — already uses playPhi2Cycles+2048 > 19656.
  # Pins: VBI cycle constants PAL=19656/NTSC=17095 > old cap 8192; old cap
  # fraction 41.7% PAL / 47.9% NTSC (both < 50%); CIA residual post-fix < 50
  # cycles (< 1% error vs 58% pre-fix); debt arithmetic stable (add≈consume per
  # VBI); pre-fix debt hits cap in < 20 VBIs; post-fix debt stays < one VBI
  # period; CIA timer phase post-fix < 100 cycles from fire; CIA path unaffected;
  # NTSC variant residual 50 cycles / < 1% error.
  add_executable(arpsid_c64_psid_vbi_cycles_v576_tests
                 source/tests/c64_psid_vbi_cycles_v576_tests.cpp)
  target_compile_features(arpsid_c64_psid_vbi_cycles_v576_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_c64_psid_vbi_cycles_v576_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_link_libraries(arpsid_c64_psid_vbi_cycles_v576_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_c64_psid_vbi_cycles_v576_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME C64PsidVbiCyclesV576Tests
           COMMAND arpsid_c64_psid_vbi_cycles_v576_tests)

  # v577 — C64 PSID play jam + budget contract tests.
  # Fixes two compounding bugs causing remaining choppiness after v576:
  #   Bug A — CPU continuation during passive: runPlay() returning false
  #   (budget hit) left jammed=false and PC mid-play.  Next block's passive
  #   platform.runCycles(playPhi2Cycles) continued executing the play routine;
  #   those SID writes had phi2Cycle < PlayBase.cycle so all mapped to
  #   playSample (deltaCycle=0 guard) — burst of note events at wrong sample.
  #   Explicit runPlay() then re-ran with contaminated machine state (arp/
  #   vibrato pointers already advanced) → wrong notes every VBI frame.
  #   Fix: set platform.cpu().state().jammed=true before passive runCycles();
  #   bootFromResetVectorPreservingMachine() inside runPlay() clears it.
  #   Bug B — Instruction budget 4096 too low: Rob Hubbard / Jeroen Tel tunes
  #   require 5000-8000+ instr/play, triggering Bug A every VBI frame.
  #   Fix: raise kC64PsidMaxInstructionsPerPlay from 4096 to 16384 (4×).
  #   CPU cost: 16384 × 15 ns × 50/s = 12.3 ms/s — within real-time budget.
  # Pins: budget 16384 covers all listed tunes; old budget causes exhaustion;
  # passive with jammed=true executes 0 CPU instructions; passive SID writes
  # clamp to playSample (deltaCycle ≤ 0); arp-pointer contamination sequence;
  # CPU cost < 15 ms/s; jammed cleared by bootFromResetVector; op order
  # jam→passive→base→play; CIA path unchanged.
  add_executable(arpsid_c64_psid_play_jam_v577_tests
                 source/tests/c64_psid_play_jam_v577_tests.cpp)
  target_compile_features(arpsid_c64_psid_play_jam_v577_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_c64_psid_play_jam_v577_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_link_libraries(arpsid_c64_psid_play_jam_v577_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_c64_psid_play_jam_v577_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME C64PsidPlayJamV577Tests
           COMMAND arpsid_c64_psid_play_jam_v577_tests)

  # v578 — C64 PSID full-VBI passive advancement contract tests.
  # Root cause of remaining choppiness after v576+v577: passive cycle debt
  # is insufficient for consecutive play calls.  When two VBI fires fall in
  # the same audio block (common with 1024-sample blocks at 44.1 kHz PAL)
  # the first play consumes 19656 cycles of debt and the second receives only
  # ~3222 cycles (16% of VBI).  For 512-sample blocks, consecutive-block plays
  # get 11439 cycles (58%).  Both cases leave CIA Timer A at a grossly wrong
  # phase at play entry (83% / 42% of VBI period from fire instead of 0.25%).
  # Fix: unconditionally advance playPhi2Cycles before every VBI play call;
  # debt accounting uses min(debt, playPhi2Cycles) as before.  Audio timing
  # is unaffected (SID write offsets are deltas from PlayBase.cycle).  C64
  # clock may temporarily run < one VBI period ahead; steady-state drift
  # < 0.02% at PAL 44.1 kHz.
  # Pins: two-plays-per-1024-block second-play shortfall < 20% of VBI (old);
  # fix: both plays advance full 19656; consecutive 512-block plays get 58%
  # VBI (old) vs 100% (fix); CIA residual 49 cycles after 19656 passive;
  # steady-state drift < 0.02% at PAL 44.1 kHz; overshoot < one VBI period
  # (< 2×VBI max); SID write offsets delta-relative (absolute phi2Cycle
  # irrelevant); CIA service path unchanged (always 21753 cycles).
  add_executable(arpsid_c64_psid_full_vbi_passive_v578_tests
                 source/tests/c64_psid_full_vbi_passive_v578_tests.cpp)
  target_compile_features(arpsid_c64_psid_full_vbi_passive_v578_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_c64_psid_full_vbi_passive_v578_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_link_libraries(arpsid_c64_psid_full_vbi_passive_v578_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_c64_psid_full_vbi_passive_v578_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME C64PsidFullVbiPassiveV578Tests
           COMMAND arpsid_c64_psid_full_vbi_passive_v578_tests)

  # v579 — C64 PSID init-register seeding contract tests.
  # Root cause of ordinary PSID choppiness after v576+v577+v578:
  # the PSID init routine runs off-thread via C64RuntimeSidSink (telemetry
  # only); sreg_() never receives init-time SID writes.  With ADSR=0 in
  # the audio engine, the SID envelope decays immediately to zero after
  # gate-on (zero sustain), producing a brief click per note at 50 Hz VBI
  # rate — continuous choppiness for any tune that sets ADSR only in init
  # (Rob Hubbard, Jeroen Tel, and the vast majority of ordinary PSID tunes).
  # Fix: at render-thread handoff (drainPendingPsidHandoff_), after
  # c64SidBridgeInstallWithSink(), seed sreg_() and c64SidBridge_.regs
  # from incoming->platform().sidRegisterImage().  That array is updated
  # by writeMapped_() on every SID write regardless of sidSink_, so it
  # holds the complete authoritative post-init register state.
  # Pins: read-only window (0x19-0x1C) skipped; 28 writeable regs seeded;
  # unseeded engine ADSR=0 → zero sustain → silence; seeded engine ADSR
  # matches init state → correct sustain; volume reg 0x18 seeded (prevents
  # total silence); bridge regs seeded for correct read-back; platform
  # image always updated regardless of active sink; play routine does not
  # re-write ADSR per frame; first play call overwrites per-frame registers.
  add_executable(arpsid_c64_psid_init_reg_seed_v579_tests
                 source/tests/c64_psid_init_reg_seed_v579_tests.cpp)
  target_compile_features(arpsid_c64_psid_init_reg_seed_v579_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_c64_psid_init_reg_seed_v579_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_link_libraries(arpsid_c64_psid_init_reg_seed_v579_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_c64_psid_init_reg_seed_v579_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME C64PsidInitRegSeedV579Tests
           COMMAND arpsid_c64_psid_init_reg_seed_v579_tests)

  # v580 — C64 PSID SIDCORE timeline/shadow contract tests.
  # Fixes the C64/PSID early-return path bypassing the v550 SIDCORE
  # end-of-block publication.  Pre-fix, C64 register-write events used
  # sidCoreBlockIndex_*512 + sampleOffset while sidCoreBlockIndex_ never
  # advanced for C64 blocks, so timeline stamps repeated forever.  The fixed
  # path uses a render-thread sample cursor, advances it before C64 returns,
  # and mirrors C64 init/timed writes into sidQueuedShadow_ so live SIDCORE
  # snapshots match the PSID audio authority.  Pins variable host block sizes,
  # active C64 early returns, idle cursor advancement, read-only skip behavior,
  # gate flag ordering, cycle-offset provenance, and saturation behavior.
  add_executable(arpsid_c64_psid_sidcore_timeline_v580_tests
                 source/tests/c64_psid_sidcore_timeline_v580_tests.cpp)
  target_compile_features(arpsid_c64_psid_sidcore_timeline_v580_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_c64_psid_sidcore_timeline_v580_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_link_libraries(arpsid_c64_psid_sidcore_timeline_v580_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_c64_psid_sidcore_timeline_v580_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME C64PsidSidCoreTimelineV580Tests
           COMMAND arpsid_c64_psid_sidcore_timeline_v580_tests)

  # v581 — Final GUI tab promotion: C64 STATE is implemented.
  # Pins the architecture count at 9/0 and verifies the C64 snapshot carries
  # PSID load/init/play addresses plus active SID base topology for the
  # read-only CPU/SID/CIA/memory inspector.
  add_executable(arpsid_c64_state_tab_promotion_v581_tests
                 source/tests/c64_state_tab_promotion_v581_tests.cpp)
  target_compile_features(arpsid_c64_state_tab_promotion_v581_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_c64_state_tab_promotion_v581_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_link_libraries(arpsid_c64_state_tab_promotion_v581_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_c64_state_tab_promotion_v581_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME C64StateTabPromotionV581Tests
           COMMAND arpsid_c64_state_tab_promotion_v581_tests)

  # v583 — C64 PSID bridge transaction contract tests.
  # Pre-v583, a PSID play routine that failed mid-execution (budget
  # exhausted, malformed tune, runaway loop) committed half-updated SID
  # state to the bridge: missing gate-offs, wrong frequency/ADSR
  # transitions, incomplete arpeggio steps.  Fix: wrap every runPlay() and
  # runPsidCiaPlaybackServiceTicks() call in a BridgeTransactionSnapshot.
  # On failure roll back timedWriteCount, regs[], and zero stale timed-write
  # entries for telemetry cleanliness; set jammed=true.  CIA-specific: roll
  # back only when play was entered but CIA IRQ was not acknowledged.  Pins:
  # successful play commits; failed play rolls back count+regs; stale entries
  # zeroed on rollback; CIA commits when playAddressEntered&&ciaAckObserved;
  # CIA rolls back when playAddressEntered&&!ciaAckObserved; no rollback when
  # !playAddressEntered; multi-play block second-play rollback isolates first;
  # overflow-safe rollback; beginTx is non-mutating; ADSR integrity preserved.
  add_executable(arpsid_c64_psid_bridge_transaction_v583_tests
                 source/tests/c64_psid_bridge_transaction_v583_tests.cpp)
  target_compile_features(arpsid_c64_psid_bridge_transaction_v583_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_c64_psid_bridge_transaction_v583_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_link_libraries(arpsid_c64_psid_bridge_transaction_v583_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_c64_psid_bridge_transaction_v583_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME C64PsidBridgeTransactionV583Tests
           COMMAND arpsid_c64_psid_bridge_transaction_v583_tests)

  # v584 — C64 PSID block-start timed-write storage clear contract tests.
  # resetTimedWrites() cleared timedWriteCount but left the timedWrites[]
  # array populated with stale data from the previous block.  The render loop
  # was safe (iterates only to timedWriteCount), but diagnostics and the v583
  # rollback could observe abandoned writes.  Fix: add timedWrites = {} after
  # resetTimedWrites() at block start; regs[] mirror is preserved.  Pins:
  # reset-alone leaves stale data; block-start clear zeroes all entries;
  # regs[] preserved across block boundary; new block writes land at index 0;
  # BridgeTransaction rollback on a clean block is a no-op.
  add_executable(arpsid_c64_psid_block_timedwrite_clear_v584_tests
                 source/tests/c64_psid_block_timedwrite_clear_v584_tests.cpp)
  target_compile_features(arpsid_c64_psid_block_timedwrite_clear_v584_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_c64_psid_block_timedwrite_clear_v584_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_link_libraries(arpsid_c64_psid_block_timedwrite_clear_v584_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_c64_psid_block_timedwrite_clear_v584_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME C64PsidBlockTimedWriteClearV584Tests
           COMMAND arpsid_c64_psid_block_timedwrite_clear_v584_tests)

  # v585 — C64 PSID scoped sidSink_ contract tests.
  # Pre-v585, C64Platform::runCycles(cycles, sid) and executeInstruction(sid)
  # permanently replaced sidSink_ when sid != nullptr, silently leaking the
  # temporary sink into subsequent write callbacks.  Fix (c64_platform.h):
  # save sidSink_ before the override and restore on return, making both
  # functions safe for temporary sinks.  attachSid() retains unconditional
  # permanent semantics.  Pins: scoped runCycles restores previous sink; scoped
  # executeInstruction restores sink; null sid leaves sidSink_ unchanged;
  # temporary sink receives writes during call only; multiple consecutive calls
  # each restore independently; jammed passive advance restores sink; attachSid
  # is unconditionally permanent.
  add_executable(arpsid_c64_psid_scoped_sink_v585_tests
                 source/tests/c64_psid_scoped_sink_v585_tests.cpp)
  target_compile_features(arpsid_c64_psid_scoped_sink_v585_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_c64_psid_scoped_sink_v585_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_link_libraries(arpsid_c64_psid_scoped_sink_v585_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_c64_psid_scoped_sink_v585_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME C64PsidScopedSinkV585Tests
           COMMAND arpsid_c64_psid_scoped_sink_v585_tests)

  # v586 — Tab tooltip coverage audit contract tests.
  # Pre-v586, ArpSIDTabTooltipForMode had four bugs:
  #   (a) Default block case 4 (ArpSIDTabSeq) returned "DRSID tab…" — wrong
  #       identity: copy belonged to tab 13 (DRSID), not tab 4 (SEQ).
  #   (b) Instrument block: missing cases 11 (C64) and 12 (HiFi) — both are
  #       in kArpSIDInstrumentVisibleTabs; hover fell to "Navigation tab".
  #   (c) Sid808 block: same + missing case 13 (DRSID).
  #   (d) DrumMachine block: same + missing case 13.
  # Fix: added cases 11/12 to Instrument; 11/12/13 to Sid808 and DrumMachine;
  # fixed case 4 and added 11/12/13 to the default block.
  # Pins (plain C++ simulation — no ObjC/AUv3 linkage): every tab in each
  # flavor's visible-tab array returns a non-Navigation tooltip; default block
  # case 4 returns kSimTooltip_Seq not kSimTooltip_Drsid; DRSID (13) and SEQ
  # (4) map to distinct codes in every flavor; modeIndex==2 matches DrumMachine
  # block for all covered tabs; every enum value 0–18 except the legacy
  # SidProjection (2) resolves to a real tooltip in the Hybrid/default path.
  add_executable(arpsid_tab_tooltip_coverage_v586_tests
                 source/tests/tab_tooltip_coverage_v586_tests.cpp)
  target_compile_features(arpsid_tab_tooltip_coverage_v586_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_tab_tooltip_coverage_v586_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_link_libraries(arpsid_tab_tooltip_coverage_v586_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_tab_tooltip_coverage_v586_tests PRIVATE
      -Wno-unused-variable -Wno-unused-function)
  add_test(NAME TabTooltipCoverageV586Tests
           COMMAND arpsid_tab_tooltip_coverage_v586_tests)

  # v587 — Drum base-frequency and canonical-note coherence tests.
  # Covers five bugs found during the v587 audit:
  #   Bug1: analogKickBaseHz_() formula 4× too narrow (overlay diverged from SID core)
  #   Bug2: analogKickSweepHz_() sweep-delta mismatched SID startReg formula
  #   Bug3: trigger(Tom) used hard-coded note 45; canonical note is 47 (High Mid Tom)
  #   Bug4: triggerOpenHat() missing per-chip (6581/8580) base/range compensation
  #   Bug5: kKitDrumClassMidiNote[Tom] was 41 (Low Floor Tom); corrected to 47
  # Tests include: pure formula invariant checks (Bug1/2), canonical-note API
  # assertion + live render (Bug3), chip-model render divergence (Bug4), static_assert
  # + runtime cross-check against engine canonical note (Bug5), and kick render
  # sanity across all tune values in both X0X8 and SidAuthentic modes.
  add_executable(arpsid_drum_base_coherence_v587_tests
                 source/tests/drum_base_coherence_v587_tests.cpp)
  target_compile_features(arpsid_drum_base_coherence_v587_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_drum_base_coherence_v587_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source
      ${CMAKE_CURRENT_SOURCE_DIR}/source/tests)
  target_link_libraries(arpsid_drum_base_coherence_v587_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_drum_base_coherence_v587_tests PRIVATE
      -Wno-unused-variable -Wno-unused-function)
  add_test(NAME DrumBaseCoherenceV587Tests
           COMMAND arpsid_drum_base_coherence_v587_tests)

  # v588 - GUI realtime projection contract tests.
  # Pins a bounded, allocation-free projection from GUI-owned MIX/KIT/DIGI
  # models into render-friendly control intent and telemetry counts.
  # Covers MIX solo/mute/pan/send/FX projection, KIT step/slot/SID-808 voice
  # projection, DIGI sample-step projection, wrapped sequencer steps, and
  # sanitize-before-projection behavior for corrupt restored GUI state.
  add_executable(arpsid_gui_realtime_projection_v588_tests
                 source/tests/gui_realtime_projection_v588_tests.cpp)
  target_compile_features(arpsid_gui_realtime_projection_v588_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_gui_realtime_projection_v588_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source
      ${CMAKE_CURRENT_SOURCE_DIR}/source/tests)
  target_link_libraries(arpsid_gui_realtime_projection_v588_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_gui_realtime_projection_v588_tests PRIVATE
      -Wno-unused-variable -Wno-unused-function)
  add_test(NAME GuiRealtimeProjectionV588Tests
           COMMAND arpsid_gui_realtime_projection_v588_tests)

  # v595/v596 - DIGI sampler render/telemetry closure tests.
  # Pins the GUI-projected DIGI factory/user sample path as an RT-safe audible layer:
  # factory slot triggers, same-step de-duping, step-edge retriggers, 4-bit
  # sampler scope output, user-import unavailable telemetry, and saved-clip playback.
  add_executable(arpsid_digi_sampler_engine_v595_tests
                 source/tests/digi_sampler_engine_v595_tests.cpp)
  target_compile_features(arpsid_digi_sampler_engine_v595_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_digi_sampler_engine_v595_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source
      ${CMAKE_CURRENT_SOURCE_DIR}/source/tests)
  target_link_libraries(arpsid_digi_sampler_engine_v595_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_digi_sampler_engine_v595_tests PRIVATE
      -Wno-unused-variable -Wno-unused-function)
  add_test(NAME DigiSamplerEngineV595Tests
           COMMAND arpsid_digi_sampler_engine_v595_tests)

  # v698 - Authentic DIGI $D418 stream engine / C64 bus contract tests.
  add_executable(arpsid_digi_d418_stream_engine_v698_tests
                 source/tests/digi_d418_stream_engine_v698_tests.cpp)
  target_compile_features(arpsid_digi_d418_stream_engine_v698_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_digi_d418_stream_engine_v698_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source
      ${CMAKE_CURRENT_SOURCE_DIR}/source/tests)
  target_link_libraries(arpsid_digi_d418_stream_engine_v698_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_digi_d418_stream_engine_v698_tests PRIVATE
      -Wno-unused-variable -Wno-unused-function)
  add_test(NAME DigiD418StreamEngineV698Tests
           COMMAND arpsid_digi_d418_stream_engine_v698_tests)

  # v699 - Authentic DIGI $D418 audible volume-DAC contract.
  add_executable(arpsid_digi_d418_sid_volume_dac_v699_tests
                 source/tests/digi_d418_sid_volume_dac_v699_tests.cpp)
  target_compile_features(arpsid_digi_d418_sid_volume_dac_v699_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_digi_d418_sid_volume_dac_v699_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source
      ${CMAKE_CURRENT_SOURCE_DIR}/source/tests)
  target_link_libraries(arpsid_digi_d418_sid_volume_dac_v699_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_digi_d418_sid_volume_dac_v699_tests PRIVATE
      -Wno-unused-variable -Wno-unused-function)
  add_test(NAME DigiD418SidVolumeDacV699Tests
           COMMAND arpsid_digi_d418_sid_volume_dac_v699_tests)

  # v589 - DrSID/SID-808 kit-data and live kick-coherence closure tests.
  # Pins the live DrSID kick overlay base/sweep telemetry against the SID-core
  # formulas across tune/accent values, keeps Tom at canonical GM note 47 in
  # engine + KIT data, compiles the full 8-class DrSID register-program kit,
  # and verifies every canonical SID-808 slot 120..149 has concrete kit data.
  add_executable(arpsid_drsid_808_kit_data_v589_tests
                 source/tests/drsid_808_kit_data_v589_tests.cpp)
  target_compile_features(arpsid_drsid_808_kit_data_v589_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_drsid_808_kit_data_v589_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source
      ${CMAKE_CURRENT_SOURCE_DIR}/source/tests)
  target_link_libraries(arpsid_drsid_808_kit_data_v589_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_drsid_808_kit_data_v589_tests PRIVATE
      -Wno-unused-variable -Wno-unused-function)
  add_test(NAME Drsid808KitDataV589Tests
           COMMAND arpsid_drsid_808_kit_data_v589_tests)

  # v590 - GUI top-bar / DRSID panel source-shape wiring tests.
  # Pins the four mounted top-bar controls and the dedicated DRSID panel's
  # live telemetry ivars/poll-loop/overlay wiring.
  add_executable(arpsid_gui_viewcontroller_wiring_v590_tests
                 source/tests/gui_viewcontroller_wiring_v590_tests.cpp)
  target_compile_features(arpsid_gui_viewcontroller_wiring_v590_tests PRIVATE cxx_std_17)
  target_compile_definitions(arpsid_gui_viewcontroller_wiring_v590_tests PRIVATE
      ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_options(arpsid_gui_viewcontroller_wiring_v590_tests PRIVATE
      -Wno-unused-variable -Wno-unused-function)
  add_test(NAME GuiViewControllerWiringV590Tests
           COMMAND arpsid_gui_viewcontroller_wiring_v590_tests)

  # v611 — Mode-transition panic/reset tests (Fix #9).
  # All 6 {BitPerfect, SidRegister, DrSid} transition pairs + RT sanitizer +
  # v612 — PSID/RSID exactness, run-failure, multi-SID, PAL/NTSC, DIGI stress (Fixes #1–#10).
  # 20 tests covering: approximate/unsupported opcode downgrade, strict/compat BRK,
  # CLI flag, legacy fallback, overflow, DIGI no-drop, secondary SID, chunking,
  # rollback, PAL/NTSC, SID model, subtune clamp, multi-SID clip, CIA, VIC, PSID,
  # all-chip snapshot, load/unload handoff.
  add_executable(arpsid_c64_psid_rsid_exactness_v612_tests
                 source/tests/c64_psid_rsid_exactness_v612_tests.cpp)
  target_compile_features(arpsid_c64_psid_rsid_exactness_v612_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_c64_psid_rsid_exactness_v612_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source
      ${CMAKE_CURRENT_SOURCE_DIR}/source/au3)
  target_link_libraries(arpsid_c64_psid_rsid_exactness_v612_tests PRIVATE arpsid_core)
  target_compile_definitions(arpsid_c64_psid_rsid_exactness_v612_tests PRIVATE
      ARPSID_TEST_BUILD=1
      ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_options(arpsid_c64_psid_rsid_exactness_v612_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME C64PsidRsidExactnessV612Tests
           COMMAND arpsid_c64_psid_rsid_exactness_v612_tests)
  set_tests_properties(C64PsidRsidExactnessV612Tests PROPERTIES TIMEOUT 120)

  # resolver single-source contract + state-root canonicalization.
  add_executable(arpsid_mode_transition_panic_reset_v611_tests
                 source/tests/mode_transition_panic_reset_v611_tests.cpp)
  target_compile_features(arpsid_mode_transition_panic_reset_v611_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_mode_transition_panic_reset_v611_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source)
  target_link_libraries(arpsid_mode_transition_panic_reset_v611_tests PRIVATE arpsid_core)
  target_compile_definitions(arpsid_mode_transition_panic_reset_v611_tests PRIVATE
      ARPSID_TEST_BUILD=1
      ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME ModeTransitionPanicResetV611Tests
           COMMAND arpsid_mode_transition_panic_reset_v611_tests)

  # v609 — Forensic single-authority consolidation guard.
  # Pins that the retired experimental ForensicSIDEngine backend stays gone and
  # the embedded forensic system remains the sole forensic authority.
  add_executable(arpsid_forensic_single_authority_v609_tests
                 source/tests/forensic_single_authority_v609_tests.cpp)
  target_compile_features(arpsid_forensic_single_authority_v609_tests PRIVATE cxx_std_17)
  target_compile_definitions(arpsid_forensic_single_authority_v609_tests PRIVATE
      ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME ForensicSingleAuthorityV609Tests
           COMMAND arpsid_forensic_single_authority_v609_tests)

  # v572 — Sequencer step note range consistency contract tests.
  # Fixes processSequencer() in arpsid_processor_phase2.cpp (AUv2 path):
  # old: noteNorm × 48 + 36 → range [36, 84] (4-octave C2-C6 window).
  # new: noteNorm × 127 → range [0, 127], matching DSPKernel (AUv3) and UI.
  # At default param 0.5: old gave MIDI 60 (Middle C); new gives MIDI 64 (E4)
  # consistent with DSPKernel + UI formula round(param × 127). Middle C (MIDI 60)
  # maps to param 60/127 ≈ 0.4724 (= UI defaultCenter hint).
  # Pins: canonical formula, old divergence documented (range 48 vs 127),
  # two-path consistency at 21 points, endpoints 0/127, default 0.5 → 64,
  # middle-C alignment 60/127 → 60, monotone non-decreasing, clamp guards.
  add_executable(arpsid_seq_note_range_v572_tests
                 source/tests/seq_note_range_v572_tests.cpp)
  target_compile_features(arpsid_seq_note_range_v572_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_seq_note_range_v572_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_seq_note_range_v572_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_seq_note_range_v572_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME SeqNoteRangeV572Tests
           COMMAND arpsid_seq_note_range_v572_tests)

  # v571 — Sequencer swing tempo-preservation contract tests.
  # Fixes processSequencer() stepDuration lambda in arpsid_processor_phase2.cpp:
  # even step used seqSwing * 0.5 * 0.5 (= *0.25), so pair sum was 2 + 0.25s
  # (up to 12.5% tempo drift at full swing). Fixed to symmetric seqSwing * 0.5.
  # Pins: pair sum=2.0 at swing=0/0.5/1.0, old formula drift documented,
  # odd≥even, unity at swing=0, max swing 75/25 triplet (3:1), floor guard.
  add_executable(arpsid_seq_swing_tempo_v571_tests
                 source/tests/seq_swing_tempo_v571_tests.cpp)
  target_compile_features(arpsid_seq_swing_tempo_v571_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_seq_swing_tempo_v571_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_seq_swing_tempo_v571_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_seq_swing_tempo_v571_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME SeqSwingTempoV571Tests
           COMMAND arpsid_seq_swing_tempo_v571_tests)

  # v570 — Portamento time formula consistency contract tests.
  # Fixes sid_runtime_synth_register_scheduler.h canonicalScheduleSynthModeNoteOn():
  # old portaTime × 4.0 → canonical ArpSID_normToPortamentoSeconds (v² × 5s).
  # Pins: canonical formula v² × 5s at key values, old-vs-new divergence
  # documented, two-path consistency (bitperfect_engine.h == scheduler),
  # endpoints 0s/5s, monotone increase, clamp guards (neg/over/NaN).
  add_executable(arpsid_portamento_time_formula_v570_tests
                 source/tests/portamento_time_formula_v570_tests.cpp)
  target_compile_features(arpsid_portamento_time_formula_v570_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_portamento_time_formula_v570_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_portamento_time_formula_v570_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_portamento_time_formula_v570_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME PortamentoTimeFormulaV570Tests
           COMMAND arpsid_portamento_time_formula_v570_tests)

  # v569 — LFO rate bulk-projection path contract tests.
  # Fixes the second call site in sid_runtime_backend_projection.h
  # (projectRuntimeStateToBackends force-reload/preset-restore path).
  # Pins: two-path consistency at 21 sample points, endpoints 0.1/20 Hz,
  # midpoint cross-path agreement at √2 ≈ 1.414 Hz, LFO object observes
  # correct phase increment, monotone increase, clamp guards.
  add_executable(arpsid_lfo_rate_projection_v569_tests
                 source/tests/lfo_rate_projection_v569_tests.cpp)
  target_compile_features(arpsid_lfo_rate_projection_v569_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_lfo_rate_projection_v569_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_lfo_rate_projection_v569_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_lfo_rate_projection_v569_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME LFORateProjectionV569Tests
           COMMAND arpsid_lfo_rate_projection_v569_tests)

  # v568 — LFO rate-curve contract tests (linear → exponential fix).
  # Covers: endpoints (value=0 → 0.1 Hz, value=1 → 20 Hz), geometric midpoint
  # (value=0.5 → √2 ≈ 1.414 Hz, not 10 Hz), monotone increase, clamp guards
  # (value <0 and >1 clamp to endpoints), setRateTempo BPM path unchanged,
  # phase-increment consistency at 44100 and 48000 Hz.
  add_executable(arpsid_lfo_rate_exp_v568_tests
                 source/tests/lfo_rate_exp_v568_tests.cpp)
  target_compile_features(arpsid_lfo_rate_exp_v568_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_lfo_rate_exp_v568_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_lfo_rate_exp_v568_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_lfo_rate_exp_v568_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME LFORateExpV568Tests
           COMMAND arpsid_lfo_rate_exp_v568_tests)

  # v567 — Arpeggiator rate-curve contract tests (linear → exponential fix).
  # Covers: endpoints (value=0 → 0.1 Hz, value=1 → 50 Hz), geometric midpoint
  # (value=0.5 → √5 ≈ 2.236 Hz, not 25 Hz), monotone increase, clamp guards
  # (value <0 and >1 clamp to endpoints), setRateTempo BPM path unchanged,
  # stepSamplesForStep consistency at 44100 and 48000 Hz.
  add_executable(arpsid_arpeggiator_rate_exp_v567_tests
                 source/tests/arpeggiator_rate_exp_v567_tests.cpp)
  target_compile_features(arpsid_arpeggiator_rate_exp_v567_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_arpeggiator_rate_exp_v567_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_arpeggiator_rate_exp_v567_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_arpeggiator_rate_exp_v567_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME ArpeggiatorRateExpV567Tests
           COMMAND arpsid_arpeggiator_rate_exp_v567_tests)

  # v560 — KIT state blob serialization contract tests.
  # Covers: layout pin (KitStateBlob=1388B, trivially-copyable, schema version=1),
  # default blob sub-models match canonical defaults, pack→unpack round-trip
  # (bit-identical for all four sub-models), isWellFormed guards (schema mismatch,
  # bad panelModel/stepGrid/voiceConfigGrid/assignConfigGrid in turn), sanitize
  # field-by-field independence (bad sub-model reset, good sub-models unchanged),
  # memcpy round-trip via raw byte array.
  add_executable(arpsid_kit_state_blob_v560_tests
                 source/tests/kit_state_blob_v560_tests.cpp)
  target_compile_features(arpsid_kit_state_blob_v560_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_kit_state_blob_v560_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_kit_state_blob_v560_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_kit_state_blob_v560_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME KitStateBlobV560Tests
           COMMAND arpsid_kit_state_blob_v560_tests)

  # v558 — KIT voice config model contract tests.
  # Covers: layout pin (KitVoiceConfig=8B, Grid=80B), trivially-copyable, schema
  # version, default config values (noise/A0D8S0R4/PW2048/no-flags), well-formed
  # guards (waveform low nibble, PW hi nibble, flags upper bits, schema mismatch),
  # ADSR nibble round-trip + clamp, PW byte-split round-trip + clamp, waveform
  # bit set/clear/toggle/mask, flag bit independence, full 9-class coverage.
  add_executable(arpsid_kit_voice_config_v558_tests
                 source/tests/kit_voice_config_v558_tests.cpp)
  target_compile_features(arpsid_kit_voice_config_v558_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_kit_voice_config_v558_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_kit_voice_config_v558_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_kit_voice_config_v558_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME KitVoiceConfigV558Tests
           COMMAND arpsid_kit_voice_config_v558_tests)

  # v557 — KIT step grid model contract tests.
  # Covers: layout pin (296 bytes), schema version, default grid (all inactive),
  # well-formedness guard (bad schema/stepCount/vel>127), read accessors OOB-safe,
  # setActive velocity clamping, setInactive, toggle round-trip,
  # clearDrumClass (siblings untouched), clearAll, full 9×32 coverage.
  add_executable(arpsid_kit_step_grid_v557_tests
                 source/tests/kit_step_grid_v557_tests.cpp)
  target_compile_features(arpsid_kit_step_grid_v557_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_kit_step_grid_v557_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_kit_step_grid_v557_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_kit_step_grid_v557_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME KitStepGridV557Tests
           COMMAND arpsid_kit_step_grid_v557_tests)

  # v556 — KIT EDIT tab wire-up completeness tests.
  # Verifies that ArpSIDTabKitV555 (index 17) implementation is complete: layout pin,
  # schema version, kitSlotCount per engine target, kitAbsoluteSlot addressing,
  # drum-assignment grid OOB-free, display names non-empty, and well-formedness
  # guards cover all mutations that _kitDrumClassChanged_v556_ et al. can make.
  add_executable(arpsid_kit_tab_wire_v556_tests
                 source/tests/kit_tab_wire_v556_tests.cpp)
  target_compile_features(arpsid_kit_tab_wire_v556_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_kit_tab_wire_v556_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_kit_tab_wire_v556_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_kit_tab_wire_v556_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME KitTabWireV556Tests
           COMMAND arpsid_kit_tab_wire_v556_tests)

  # v555 — KIT tab data model contract tests.
  # Exercises KitPanelModel layout, drum-class/engine-target counts, MIDI note
  # table, default assignments, slot count + absolute-slot helpers, well-formedness
  # validation (all OOB fields rejected), and user-slot name round-trip.
  add_executable(arpsid_kit_panel_model_v555_tests
                 source/tests/kit_panel_model_v555_tests.cpp)
  target_compile_features(arpsid_kit_panel_model_v555_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_kit_panel_model_v555_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_kit_panel_model_v555_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_kit_panel_model_v555_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME KitPanelModelV555Tests
           COMMAND arpsid_kit_panel_model_v555_tests)

  # v554 — MIX FX processor contract tests.
  # Exercises BiquadState/BiquadCoeffs primitives, all 5 processor types
  # (Eq3Band, Transient, Compressor, Saturator, Bitcrusher), MixFxProcessor
  # tagged-struct dispatch, bypass flag, and applyMixFxChain serial chain.
  add_executable(arpsid_mix_fx_processors_v554_tests
                 source/tests/mix_fx_processors_v554_tests.cpp)
  target_compile_features(arpsid_mix_fx_processors_v554_tests PRIVATE cxx_std_17)
  target_include_directories(arpsid_mix_fx_processors_v554_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(arpsid_mix_fx_processors_v554_tests PRIVATE arpsid_core)
  target_compile_options(arpsid_mix_fx_processors_v554_tests PRIVATE
      -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function)
  add_test(NAME MixFxProcessorsV554Tests
           COMMAND arpsid_mix_fx_processors_v554_tests)

  # DR-808 / SID-808 drum-machine voice regression tests.
  # These share dr808_test_utils.h and exercise voice scheduling, accent law,
  # hat-choke behaviour, deterministic render, and peak-headroom invariants.
  foreach(_dr808_tgt voice_smoke accent hat_choke determinism peak_headroom)
    set(_dr808_target "arpsid_dr808_${_dr808_tgt}_tests")
    add_executable(${_dr808_target} source/tests/dr808_${_dr808_tgt}_tests.cpp)
    target_compile_features(${_dr808_target} PRIVATE cxx_std_17)
    target_include_directories(${_dr808_target} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/source
      ${CMAKE_CURRENT_SOURCE_DIR}/source/tests)
    target_link_libraries(${_dr808_target} PRIVATE arpsid_core)
    target_compile_definitions(${_dr808_target} PRIVATE
      ARPSID_TEST_BUILD=1
      ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    string(REPLACE "_" "" _dr808_test_name "Dr808${_dr808_tgt}Tests")
    add_test(NAME ${_dr808_test_name} COMMAND ${_dr808_target})
    set_tests_properties(${_dr808_test_name} PROPERTIES TIMEOUT 60)
  endforeach()

endif()


# removed stale/dead closure gate: BuildCloseV215Tests source was intentionally deleted from release package

# ─── Restored test coverage ──────────────────────────────────────────────────
# These tests were previously orphaned from the CMake build system by an
# over-eager stale-target cleanup pass. The source files were always present
# and the tests are still valid regressions for current behaviour. They are
# all standard arpsid_core-linked C++17 tests using std test main / require().
if(ARPSID_BUILD_TESTS)
    set(_arpsid_restored_tests
        # DrSID kit / SID-808 / Logic transport
        factory_drsid_kit_bank_sweep_v238
        logic_stop_start_drsid_kit_v238
        authentic_bass_sid808_logic_v241
        factory_patch_audio_fingerprint_reset_parity_v240
        state_apply_reason_policy_v240
        telemetry_all_modes_forensic_scope_v242
        # PSID/RSID runtime
        psid_rsid_c64_runtime_v261
        psid_rsid_runtime_comprehensive_v265
        c64_psid_cia_irq_service_v499
        c64_psid_full_memory_runtime_v435
        # C64 platform / emulator
        c64_platform_sidcore_v255
        c64_platform_completion_v256
        c64_true_chip_replication_v273
        c64_emulator_scope_v274
        c64_sidcore_realtime_boot_v275
        c64_system_enhancement_v411
        c64_system_integration_v412
        c64_system_completion_v413
        c64_cpu6510_complete_v414
        c64_full_system_surface_v415
        c64_phi2_processor_port
        c64_phi2_pla_write_through
        c64_phi2_sid_write_bus_timing
        c64_phi2_reset_audio_scheduler
        c64_phi2_rsid_playbase_cycle_v605
        c64_phi2_rsid_exact_init_v605
        c64_phi2_rdy_vectors_v606
        sid_envelope_dac_6581_v607
        c64_audit_closure_v608
        c64_phi2_trace_v610
        c64_boot_sequence_sid_init_v430
        c64_sid_play_bootstrap_v431
        c64_rom_cache_manager_v434
        # Render / Hi-Fi
        render_complete_identity_fifo_v304
        hifi_audible_authority_v397
        hifi_perfection_v398
        # SID Pro JSON / forensic
        sid_pro_json_trace_v409
        sid_pro_json_c64_bridge_v410
        # Release closure
        release_final_closure_v404
        release_runtime_closure_v408
        sidcore_remaining_audit_closure_v388
        audit_behavior_closure_v500
        # New v504/v505/v506/v507 regressions for SID-808 identity + transport
        # integration + 6581 bass authority.
        sid808_factory_definition_v504
        host_transport_partial_state_v505
        sid808_transport_integration_v506
        bass_6581_authority_v507
        # v969: the one genuinely-orphaned test from the same cleanup. It
        # compiles and passes against current code (audited) — the AUv3
        # render-scratch/transport source-contract guard. (The dr808_* voice
        # tests were already registered separately above via their own foreach.)
        auv3_render_scratch_transport_v591
    )
    foreach(_test ${_arpsid_restored_tests})
        set(_target "arpsid_${_test}_tests")
        set(_src "source/tests/${_test}_tests.cpp")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
            # Tests that touch factory patch metadata also need the patch bank
            # translation unit because getFactoryPatchDefinition() lives there.
            set(_extra_srcs)
            file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${_src}" _src_text)
            if(_src_text MATCHES "getFactoryPatchDefinition|makeFactoryPatchStateRootForSlot|loadFactoryPatchNormalizedParamsForSlot|isAuthoredDrSidProjectionFactorySlot|kFactoryPatchSlotCount|forensic_patch_bank|forensic_patch_bank.h|PatchDefinition")
                list(APPEND _extra_srcs source/forensic_patch_bank.cpp)
            endif()
            if(_src_text MATCHES "sid_patchbank_io|SidPatchbankIO")
                list(APPEND _extra_srcs source/patchbank/sid_patchbank_io.cpp)
            endif()
            add_executable(${_target} ${_src} ${_extra_srcs})
            target_compile_features(${_target} PRIVATE cxx_std_17)
            target_include_directories(${_target} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/source
                ${STUB_DIR})
            target_link_libraries(${_target} PRIVATE arpsid_core)
            target_compile_definitions(${_target} PRIVATE
                ARPSID_TEST_BUILD=1
                ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
            target_compile_options(${_target} PRIVATE
                -Wno-unused-variable
                -Wno-unused-but-set-variable
                -Wno-unused-function)
            # Build target name → CTest test name: drop "arpsid_", title-case segments.
            string(REPLACE "_" ";" _parts "${_test}_tests")
            set(_test_name "")
            foreach(_p ${_parts})
                string(SUBSTRING "${_p}" 0 1 _first)
                string(SUBSTRING "${_p}" 1 -1 _rest)
                string(TOUPPER "${_first}" _first)
                set(_test_name "${_test_name}${_first}${_rest}")
            endforeach()
            add_test(NAME ${_test_name} COMMAND ${_target})
            set_tests_properties(${_test_name} PROPERTIES TIMEOUT 60)
        endif()
    endforeach()
endif()

add_executable(arpsid_drum_bridge_no_silence_v613_tests source/tests/drum_bridge_no_silence_v613_tests.cpp)
target_compile_definitions(arpsid_drum_bridge_no_silence_v613_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME DrumBridgeNoSilenceV613Tests COMMAND arpsid_drum_bridge_no_silence_v613_tests)

add_executable(arpsid_c64_runtime_load_reset_v614_tests source/tests/c64_runtime_load_reset_v614_tests.cpp)
target_include_directories(arpsid_c64_runtime_load_reset_v614_tests PRIVATE include)
add_test(NAME C64RuntimeLoadResetV614Tests COMMAND arpsid_c64_runtime_load_reset_v614_tests)

add_executable(arpsid_c64_cpu_retire_counter_v615_tests source/tests/c64_cpu_retire_counter_v615_tests.cpp)
target_include_directories(arpsid_c64_cpu_retire_counter_v615_tests PRIVATE include)
add_test(NAME C64CpuRetireCounterV615Tests COMMAND arpsid_c64_cpu_retire_counter_v615_tests)

add_executable(arpsid_c64_control_plane_v616_tests source/tests/c64_control_plane_v616_tests.cpp)
target_include_directories(arpsid_c64_control_plane_v616_tests PRIVATE include)
add_test(NAME C64ControlPlaneV616Tests COMMAND arpsid_c64_control_plane_v616_tests)

add_executable(arpsid_c64_final_correctness_v617_tests source/tests/c64_final_correctness_v617_tests.cpp)
target_include_directories(arpsid_c64_final_correctness_v617_tests PRIVATE include)
target_compile_definitions(arpsid_c64_final_correctness_v617_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME C64FinalCorrectnessV617Tests COMMAND arpsid_c64_final_correctness_v617_tests)

add_executable(arpsid_c64_cia_proper_v618_tests source/tests/c64_cia_proper_v618_tests.cpp)
target_include_directories(arpsid_c64_cia_proper_v618_tests PRIVATE include)
add_test(NAME C64CiaProperV618Tests COMMAND arpsid_c64_cia_proper_v618_tests)

add_executable(arpsid_c64_cia_extended_v619_tests source/tests/c64_cia_extended_v619_tests.cpp)
target_include_directories(arpsid_c64_cia_extended_v619_tests PRIVATE include)
add_test(NAME C64CiaExtendedV619Tests COMMAND arpsid_c64_cia_extended_v619_tests)

add_executable(arpsid_c64_cia_phi2_integration_v620_tests source/tests/c64_cia_phi2_integration_v620_tests.cpp)
target_include_directories(arpsid_c64_cia_phi2_integration_v620_tests PRIVATE include)
add_test(NAME C64CiaPhi2IntegrationV620Tests COMMAND arpsid_c64_cia_phi2_integration_v620_tests)

add_executable(arpsid_drum_bridge_behavior_v621_tests source/tests/drum_bridge_behavior_v621_tests.cpp)
target_compile_features(arpsid_drum_bridge_behavior_v621_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_drum_bridge_behavior_v621_tests PRIVATE arpsid_core)
add_test(NAME DrumBridgeBehaviorV621Tests COMMAND arpsid_drum_bridge_behavior_v621_tests)

add_executable(arpsid_digi_kit_bridge_policy_v622_tests source/tests/digi_kit_bridge_policy_v622_tests.cpp)
target_compile_features(arpsid_digi_kit_bridge_policy_v622_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_digi_kit_bridge_policy_v622_tests PRIVATE arpsid_core)
add_test(NAME DigiKitBridgePolicyV622Tests COMMAND arpsid_digi_kit_bridge_policy_v622_tests)

add_executable(arpsid_drum_bridge_mix_policy_v623_tests source/tests/drum_bridge_mix_policy_v623_tests.cpp)
target_compile_features(arpsid_drum_bridge_mix_policy_v623_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_drum_bridge_mix_policy_v623_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME DrumBridgeMixPolicyV623Tests COMMAND arpsid_drum_bridge_mix_policy_v623_tests)

add_executable(arpsid_drsid_policy_v624_tests source/tests/drsid_policy_v624_tests.cpp)
target_compile_features(arpsid_drsid_policy_v624_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_drsid_policy_v624_tests PRIVATE arpsid_core)
add_test(NAME DrSidPolicyV624Tests COMMAND arpsid_drsid_policy_v624_tests)

add_executable(arpsid_drsid_replacing_contract_v625_tests source/tests/drsid_replacing_contract_v625_tests.cpp)
target_compile_features(arpsid_drsid_replacing_contract_v625_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_drsid_replacing_contract_v625_tests PRIVATE arpsid_core)
add_test(NAME DrSidReplacingContractV625Tests COMMAND arpsid_drsid_replacing_contract_v625_tests)

add_executable(arpsid_sid808_hit_override_v626_tests source/tests/sid808_hit_override_v626_tests.cpp)
target_compile_features(arpsid_sid808_hit_override_v626_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_sid808_hit_override_v626_tests PRIVATE arpsid_core)
add_test(NAME Sid808HitOverrideV626Tests COMMAND arpsid_sid808_hit_override_v626_tests)

add_executable(arpsid_kit_selected_slot_consumption_v627_tests source/tests/kit_selected_slot_consumption_v627_tests.cpp)
target_compile_features(arpsid_kit_selected_slot_consumption_v627_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_kit_selected_slot_consumption_v627_tests PRIVATE arpsid_core)
add_test(NAME KitSelectedSlotConsumptionV627Tests COMMAND arpsid_kit_selected_slot_consumption_v627_tests)

add_executable(arpsid_kit_mixed_target_compile_v628_tests source/tests/kit_mixed_target_compile_v628_tests.cpp)
target_compile_features(arpsid_kit_mixed_target_compile_v628_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_kit_mixed_target_compile_v628_tests PRIVATE arpsid_core)
add_test(NAME KitMixedTargetCompileV628Tests COMMAND arpsid_kit_mixed_target_compile_v628_tests)

add_executable(arpsid_kit_assign_migration_v629_tests source/tests/kit_assign_migration_v629_tests.cpp)
target_compile_features(arpsid_kit_assign_migration_v629_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_kit_assign_migration_v629_tests PRIVATE arpsid_core)
add_test(NAME KitAssignMigrationV629Tests COMMAND arpsid_kit_assign_migration_v629_tests)

add_executable(arpsid_resid_fallback_contract_v630_tests source/tests/resid_fallback_contract_v630_tests.cpp)
target_compile_features(arpsid_resid_fallback_contract_v630_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_resid_fallback_contract_v630_tests PRIVATE include external)
add_test(NAME ResidFallbackContractV630Tests COMMAND arpsid_resid_fallback_contract_v630_tests)

add_executable(arpsid_tab_no_scaffold_v631_tests source/tests/tab_no_scaffold_v631_tests.cpp)
target_compile_features(arpsid_tab_no_scaffold_v631_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_tab_no_scaffold_v631_tests PRIVATE arpsid_core)
add_test(NAME TabNoScaffoldV631Tests COMMAND arpsid_tab_no_scaffold_v631_tests)

add_executable(arpsid_kit_hash_field_contract_v632_tests source/tests/kit_hash_field_contract_v632_tests.cpp)
target_compile_features(arpsid_kit_hash_field_contract_v632_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_kit_hash_field_contract_v632_tests PRIVATE arpsid_core)
add_test(NAME KitHashFieldContractV632Tests COMMAND arpsid_kit_hash_field_contract_v632_tests)

add_executable(arpsid_dsp_kernel_include_smoke_v633_tests source/tests/dsp_kernel_include_smoke_v633_tests.cpp)
target_compile_features(arpsid_dsp_kernel_include_smoke_v633_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_dsp_kernel_include_smoke_v633_tests PRIVATE
    source/au3
    source
    include
    external
)
target_link_libraries(arpsid_dsp_kernel_include_smoke_v633_tests PRIVATE arpsid_core)
add_test(NAME DspKernelIncludeSmokeV633Tests COMMAND arpsid_dsp_kernel_include_smoke_v633_tests)

add_executable(arpsid_dsp_kernel_multi_tu_smoke_v634_tests
    source/tests/dsp_kernel_multi_tu_smoke_v634_tests.cpp
    source/tests/dsp_kernel_multi_tu_smoke_v634_auv2_component.cpp
    source/tests/dsp_kernel_multi_tu_smoke_v634_au3_audio_unit.cpp
    source/tests/dsp_kernel_multi_tu_smoke_v634_adapter.cpp
    source/tests/dsp_kernel_multi_tu_smoke_v634_view_controller.cpp
    source/tests/dsp_kernel_multi_tu_smoke_v634_file_bank.cpp
)
target_compile_features(arpsid_dsp_kernel_multi_tu_smoke_v634_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_dsp_kernel_multi_tu_smoke_v634_tests PRIVATE
    source/au3
    source
    include
    external
    source/tests
)
target_link_libraries(arpsid_dsp_kernel_multi_tu_smoke_v634_tests PRIVATE arpsid_core)
add_test(NAME DspKernelMultiTuSmokeV634Tests COMMAND arpsid_dsp_kernel_multi_tu_smoke_v634_tests)

add_executable(arpsid_source_tree_pass48_guard_v635_tests source/tests/source_tree_pass48_guard_v635_tests.cpp)
target_compile_features(arpsid_source_tree_pass48_guard_v635_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_source_tree_pass48_guard_v635_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME SourceTreePass48GuardV635Tests COMMAND arpsid_source_tree_pass48_guard_v635_tests)

add_custom_target(arpsid_verify_source_tree
    COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/verify_source_tree.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Verify ArpSID current source tree guard"
)

add_executable(arpsid_source_tree_pass49_cmake_guard_v636_tests source/tests/source_tree_pass49_cmake_guard_v636_tests.cpp)
target_compile_features(arpsid_source_tree_pass49_cmake_guard_v636_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_source_tree_pass49_cmake_guard_v636_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME SourceTreePass49CMakeGuardV636Tests COMMAND arpsid_source_tree_pass49_cmake_guard_v636_tests)

add_executable(arpsid_kit_sid808_factory_voice_precedence_v637_tests source/tests/kit_sid808_factory_voice_precedence_v637_tests.cpp)
target_compile_features(arpsid_kit_sid808_factory_voice_precedence_v637_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_kit_sid808_factory_voice_precedence_v637_tests PRIVATE arpsid_core)
add_test(NAME KitSid808FactoryVoicePrecedenceV637Tests COMMAND arpsid_kit_sid808_factory_voice_precedence_v637_tests)

add_executable(arpsid_drsid_kit_payload_v638_tests source/tests/drsid_kit_payload_v638_tests.cpp)
target_compile_features(arpsid_drsid_kit_payload_v638_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_drsid_kit_payload_v638_tests PRIVATE arpsid_core)
add_test(NAME DrSidKitPayloadV638Tests COMMAND arpsid_drsid_kit_payload_v638_tests)

add_executable(arpsid_digi_slot_bridge_contract_v639_tests source/tests/digi_slot_bridge_contract_v639_tests.cpp)
target_compile_features(arpsid_digi_slot_bridge_contract_v639_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_digi_slot_bridge_contract_v639_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME DigiSlotBridgeContractV639Tests COMMAND arpsid_digi_slot_bridge_contract_v639_tests)

add_executable(arpsid_drsid_factory_slot_audio_shape_v640_tests source/tests/drsid_factory_slot_audio_shape_v640_tests.cpp)
target_compile_features(arpsid_drsid_factory_slot_audio_shape_v640_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_drsid_factory_slot_audio_shape_v640_tests PRIVATE arpsid_core)
add_test(NAME DrSidFactorySlotAudioShapeV640Tests COMMAND arpsid_drsid_factory_slot_audio_shape_v640_tests)

add_executable(arpsid_digi_nonzero_slot_runtime_v641_tests source/tests/digi_nonzero_slot_runtime_v641_tests.cpp)
target_compile_features(arpsid_digi_nonzero_slot_runtime_v641_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_digi_nonzero_slot_runtime_v641_tests PRIVATE arpsid_core)
add_test(NAME DigiNonzeroSlotRuntimeV641Tests COMMAND arpsid_digi_nonzero_slot_runtime_v641_tests)

add_executable(arpsid_sid808_projection_sync_contract_v642_tests source/tests/sid808_projection_sync_contract_v642_tests.cpp)
target_compile_features(arpsid_sid808_projection_sync_contract_v642_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_sid808_projection_sync_contract_v642_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME Sid808ProjectionSyncContractV642Tests COMMAND arpsid_sid808_projection_sync_contract_v642_tests)

add_executable(arpsid_kit_target_authority_v643_tests source/tests/kit_target_authority_v643_tests.cpp)
target_compile_features(arpsid_kit_target_authority_v643_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_kit_target_authority_v643_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME KitTargetAuthorityV643Tests COMMAND arpsid_kit_target_authority_v643_tests)

add_executable(arpsid_drum_bridge_runtime_authority_v644_tests source/tests/drum_bridge_runtime_authority_v644_tests.cpp)
target_compile_features(arpsid_drum_bridge_runtime_authority_v644_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_drum_bridge_runtime_authority_v644_tests PRIVATE arpsid_core)
add_test(NAME DrumBridgeRuntimeAuthorityV644Tests COMMAND arpsid_drum_bridge_runtime_authority_v644_tests)

add_executable(arpsid_full_closure_validation_manifest_v645_tests source/tests/full_closure_manifest_v645_tests.cpp)
target_compile_features(arpsid_full_closure_validation_manifest_v645_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_full_closure_validation_manifest_v645_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME FullClosureManifestV645Tests COMMAND arpsid_full_closure_validation_manifest_v645_tests)

add_custom_target(arpsid_run_full_closure_validation
    COMMAND ${CMAKE_COMMAND} -E echo "Run scripts/run_full_closure_validation.sh for full external closure validation"
    COMMAND ${CMAKE_COMMAND} -E env bash ${CMAKE_SOURCE_DIR}/scripts/run_full_closure_validation.sh ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Run ArpSID full closure validation"
)

add_executable(arpsid_audit_closure_matrix_v646_tests source/tests/audit_closure_matrix_v646_tests.cpp)
target_compile_features(arpsid_audit_closure_matrix_v646_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_audit_closure_matrix_v646_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME AuditClosureMatrixV646Tests COMMAND arpsid_audit_closure_matrix_v646_tests)

add_executable(arpsid_drsid_kit_voice_deep_override_v647_tests source/tests/drsid_kit_voice_deep_override_v647_tests.cpp)
target_compile_features(arpsid_drsid_kit_voice_deep_override_v647_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_drsid_kit_voice_deep_override_v647_tests PRIVATE arpsid_core)
add_test(NAME DrSidKitVoiceDeepOverrideV647Tests COMMAND arpsid_drsid_kit_voice_deep_override_v647_tests)

add_executable(arpsid_digi_kit_dense_projection_v648_tests source/tests/digi_kit_dense_projection_v648_tests.cpp)
target_compile_features(arpsid_digi_kit_dense_projection_v648_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_digi_kit_dense_projection_v648_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME DigiKitDenseProjectionV648Tests COMMAND arpsid_digi_kit_dense_projection_v648_tests)

add_executable(arpsid_bridge_drsid_accessor_removed_v649_tests source/tests/bridge_drsid_accessor_removed_v649_tests.cpp)
target_compile_features(arpsid_bridge_drsid_accessor_removed_v649_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_bridge_drsid_accessor_removed_v649_tests PRIVATE arpsid_core)
add_test(NAME BridgeDrsidAccessorRemovedV649Tests COMMAND arpsid_bridge_drsid_accessor_removed_v649_tests)

add_executable(arpsid_kit_mixed_target_runtime_v650_tests source/tests/kit_mixed_target_runtime_v650_tests.cpp)
target_compile_features(arpsid_kit_mixed_target_runtime_v650_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_kit_mixed_target_runtime_v650_tests PRIVATE arpsid_core)
add_test(NAME KitMixedTargetRuntimeV650Tests COMMAND arpsid_kit_mixed_target_runtime_v650_tests)

add_executable(arpsid_drum_stem_mixer_v651_tests source/tests/drum_stem_mixer_v651_tests.cpp)
target_compile_features(arpsid_drum_stem_mixer_v651_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_drum_stem_mixer_v651_tests PRIVATE arpsid_core)
add_test(NAME DrumStemMixerV651Tests COMMAND arpsid_drum_stem_mixer_v651_tests)

add_executable(arpsid_dsp_kernel_stem_policy_contract_v652_tests source/tests/dsp_kernel_stem_policy_contract_v652_tests.cpp)
target_compile_features(arpsid_dsp_kernel_stem_policy_contract_v652_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_dsp_kernel_stem_policy_contract_v652_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME DspKernelStemPolicyContractV652Tests COMMAND arpsid_dsp_kernel_stem_policy_contract_v652_tests)

add_executable(arpsid_macos_production_script_v653_tests source/tests/macos_production_script_v653_tests.cpp)
target_compile_features(arpsid_macos_production_script_v653_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_macos_production_script_v653_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME MacOSProductionScriptV653Tests COMMAND arpsid_macos_production_script_v653_tests)

add_executable(arpsid_bridge_drsid_legacy_callsite_guard_v654_tests source/tests/bridge_drsid_legacy_callsite_guard_v654_tests.cpp)
target_compile_features(arpsid_bridge_drsid_legacy_callsite_guard_v654_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_bridge_drsid_legacy_callsite_guard_v654_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME BridgeDrsidLegacyCallsiteGuardV654Tests COMMAND arpsid_bridge_drsid_legacy_callsite_guard_v654_tests)

add_executable(arpsid_macos_build_wrapper_paths_v655_tests source/tests/macos_build_wrapper_paths_v655_tests.cpp)
target_compile_features(arpsid_macos_build_wrapper_paths_v655_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_macos_build_wrapper_paths_v655_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME MacOSBuildWrapperPathsV655Tests COMMAND arpsid_macos_build_wrapper_paths_v655_tests)

add_executable(arpsid_drsid_all_notes_off_allocator_reset_v656_tests source/tests/drsid_all_notes_off_allocator_reset_v656_tests.cpp)
target_compile_features(arpsid_drsid_all_notes_off_allocator_reset_v656_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_drsid_all_notes_off_allocator_reset_v656_tests PRIVATE arpsid_core)
add_test(NAME DrSidAllNotesOffAllocatorResetV656Tests COMMAND arpsid_drsid_all_notes_off_allocator_reset_v656_tests)

add_executable(arpsid_kit_runtime_resolver_v657_tests source/tests/kit_runtime_resolver_v657_tests.cpp)
target_compile_features(arpsid_kit_runtime_resolver_v657_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_kit_runtime_resolver_v657_tests PRIVATE arpsid_core)
add_test(NAME KitRuntimeResolverV657Tests COMMAND arpsid_kit_runtime_resolver_v657_tests)

add_executable(arpsid_factory_canonical_drum_slots_v658_tests source/tests/factory_canonical_drum_slots_v658_tests.cpp)
target_link_libraries(arpsid_factory_canonical_drum_slots_v658_tests PRIVATE arpsid_forensic_patchbank)
target_compile_features(arpsid_factory_canonical_drum_slots_v658_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_factory_canonical_drum_slots_v658_tests PRIVATE arpsid_core)
add_test(NAME FactoryCanonicalDrumSlotsV658Tests COMMAND arpsid_factory_canonical_drum_slots_v658_tests)

add_executable(arpsid_factory_slot_schema_migration_v659_tests source/tests/factory_slot_schema_migration_v659_tests.cpp)
target_link_libraries(arpsid_factory_slot_schema_migration_v659_tests PRIVATE arpsid_forensic_patchbank)
target_compile_features(arpsid_factory_slot_schema_migration_v659_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_factory_slot_schema_migration_v659_tests PRIVATE arpsid_core)
add_test(NAME FactorySlotSchemaMigrationV659Tests COMMAND arpsid_factory_slot_schema_migration_v659_tests)

add_executable(arpsid_dsp_reset_canonical_slot_contract_v660_tests source/tests/dsp_reset_canonical_slot_contract_v660_tests.cpp)
target_compile_features(arpsid_dsp_reset_canonical_slot_contract_v660_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_dsp_reset_canonical_slot_contract_v660_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME DspResetCanonicalSlotContractV660Tests COMMAND arpsid_dsp_reset_canonical_slot_contract_v660_tests)

add_executable(arpsid_gui_factory_grid_range_v661_tests source/tests/gui_factory_grid_range_v661_tests.cpp)
target_compile_features(arpsid_gui_factory_grid_range_v661_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_gui_factory_grid_range_v661_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME GuiFactoryGridRangeV661Tests COMMAND arpsid_gui_factory_grid_range_v661_tests)

add_executable(arpsid_drsid_register_image_ring_sync_v662_tests source/tests/drsid_register_image_ring_sync_v662_tests.cpp)
target_compile_features(arpsid_drsid_register_image_ring_sync_v662_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_drsid_register_image_ring_sync_v662_tests PRIVATE arpsid_core)
add_test(NAME DrSidRegisterImageRingSyncV662Tests COMMAND arpsid_drsid_register_image_ring_sync_v662_tests)

add_executable(arpsid_sid808_param_voice_parity_v663_tests source/tests/sid808_param_voice_parity_v663_tests.cpp)
target_link_libraries(arpsid_sid808_param_voice_parity_v663_tests PRIVATE arpsid_forensic_patchbank)
target_compile_features(arpsid_sid808_param_voice_parity_v663_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_sid808_param_voice_parity_v663_tests PRIVATE arpsid_core)
add_test(NAME Sid808ParamVoiceParityV663Tests COMMAND arpsid_sid808_param_voice_parity_v663_tests)

add_executable(arpsid_drsid_kit_pre_authored_trigger_v664_tests source/tests/drsid_kit_pre_authored_trigger_v664_tests.cpp)
target_compile_features(arpsid_drsid_kit_pre_authored_trigger_v664_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_drsid_kit_pre_authored_trigger_v664_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME DrSidKitPreAuthoredTriggerV664Tests COMMAND arpsid_drsid_kit_pre_authored_trigger_v664_tests)

add_executable(arpsid_factory_slot_encoding_roundtrip_v665_tests source/tests/factory_slot_encoding_roundtrip_v665_tests.cpp)
target_compile_features(arpsid_factory_slot_encoding_roundtrip_v665_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_factory_slot_encoding_roundtrip_v665_tests PRIVATE arpsid_core)
add_test(NAME FactorySlotEncodingRoundtripV665Tests COMMAND arpsid_factory_slot_encoding_roundtrip_v665_tests)

add_executable(arpsid_factory_state_root_extended_identity_v666_tests source/tests/factory_state_root_extended_identity_v666_tests.cpp)
target_link_libraries(arpsid_factory_state_root_extended_identity_v666_tests PRIVATE arpsid_forensic_patchbank)
target_compile_features(arpsid_factory_state_root_extended_identity_v666_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_factory_state_root_extended_identity_v666_tests PRIVATE arpsid_core)
add_test(NAME FactoryStateRootExtendedIdentityV666Tests COMMAND arpsid_factory_state_root_extended_identity_v666_tests)

add_executable(arpsid_au_extended_factory_slot_contract_v667_tests source/tests/au_extended_factory_slot_contract_v667_tests.cpp)
target_compile_features(arpsid_au_extended_factory_slot_contract_v667_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_au_extended_factory_slot_contract_v667_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME AuExtendedFactorySlotContractV667Tests COMMAND arpsid_au_extended_factory_slot_contract_v667_tests)

add_executable(arpsid_gui_extended_factory_slot_contract_v668_tests source/tests/gui_extended_factory_slot_contract_v668_tests.cpp)
target_compile_features(arpsid_gui_extended_factory_slot_contract_v668_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_gui_extended_factory_slot_contract_v668_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME GuiExtendedFactorySlotContractV668Tests COMMAND arpsid_gui_extended_factory_slot_contract_v668_tests)

add_executable(arpsid_factory_no_legacy_127_alias_v669_tests source/tests/factory_no_legacy_127_alias_v669_tests.cpp)
target_compile_features(arpsid_factory_no_legacy_127_alias_v669_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_factory_no_legacy_127_alias_v669_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME FactoryNoLegacy127AliasV669Tests COMMAND arpsid_factory_no_legacy_127_alias_v669_tests)

add_executable(arpsid_full_ctest_preflight_script_v670_tests source/tests/full_ctest_preflight_script_v670_tests.cpp)
target_compile_features(arpsid_full_ctest_preflight_script_v670_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_full_ctest_preflight_script_v670_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME FullCTestPreflightScriptV670Tests COMMAND arpsid_full_ctest_preflight_script_v670_tests)

add_executable(arpsid_gui_apply_readback_extended_slot_v671_tests source/tests/gui_apply_readback_extended_slot_v671_tests.cpp)
target_compile_features(arpsid_gui_apply_readback_extended_slot_v671_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_gui_apply_readback_extended_slot_v671_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME GuiApplyReadbackExtendedSlotV671Tests COMMAND arpsid_gui_apply_readback_extended_slot_v671_tests)

add_executable(arpsid_auv2_extended_factory_slot_identity_v672_tests source/tests/auv2_extended_factory_slot_identity_v672_tests.cpp)
target_compile_features(arpsid_auv2_extended_factory_slot_identity_v672_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_auv2_extended_factory_slot_identity_v672_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME Auv2ExtendedFactorySlotIdentityV672Tests COMMAND arpsid_auv2_extended_factory_slot_identity_v672_tests)

add_executable(arpsid_factory_program_metadata_policy_v673_tests source/tests/factory_program_metadata_policy_v673_tests.cpp)
target_compile_features(arpsid_factory_program_metadata_policy_v673_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_factory_program_metadata_policy_v673_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME FactoryProgramMetadataPolicyV673Tests COMMAND arpsid_factory_program_metadata_policy_v673_tests)

add_executable(arpsid_factory_identity_final_sweep_v674_tests source/tests/factory_identity_final_sweep_v674_tests.cpp)
target_compile_features(arpsid_factory_identity_final_sweep_v674_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_factory_identity_final_sweep_v674_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME FactoryIdentityFinalSweepV674Tests COMMAND arpsid_factory_identity_final_sweep_v674_tests)

add_executable(arpsid_filebank_extended_factory_boundary_v675_tests source/tests/filebank_extended_factory_boundary_v675_tests.cpp)
target_compile_features(arpsid_filebank_extended_factory_boundary_v675_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_filebank_extended_factory_boundary_v675_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME FileBankExtendedFactoryBoundaryV675Tests COMMAND arpsid_filebank_extended_factory_boundary_v675_tests)

add_executable(arpsid_gui_bank_label_refresh_v676_tests source/tests/gui_bank_label_refresh_v676_tests.cpp)
target_compile_features(arpsid_gui_bank_label_refresh_v676_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_gui_bank_label_refresh_v676_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME GuiBankLabelRefreshV676Tests COMMAND arpsid_gui_bank_label_refresh_v676_tests)

add_executable(arpsid_bank_ui_text_and_tooltip_v677_tests source/tests/bank_ui_text_and_tooltip_v677_tests.cpp)
target_compile_features(arpsid_bank_ui_text_and_tooltip_v677_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_bank_ui_text_and_tooltip_v677_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME BankUITextAndTooltipV677Tests COMMAND arpsid_bank_ui_text_and_tooltip_v677_tests)

add_executable(arpsid_gui_color_and_boundary_compile_guard_v678_tests source/tests/gui_color_and_boundary_compile_guard_v678_tests.cpp)
target_compile_features(arpsid_gui_color_and_boundary_compile_guard_v678_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_gui_color_and_boundary_compile_guard_v678_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME GuiColorAndBoundaryCompileGuardV678Tests COMMAND arpsid_gui_color_and_boundary_compile_guard_v678_tests)

add_executable(arpsid_legacy_program_expectation_closed_v679_tests source/tests/legacy_program_expectation_closed_v679_tests.cpp)
target_compile_features(arpsid_legacy_program_expectation_closed_v679_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_legacy_program_expectation_closed_v679_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME LegacyProgramExpectationClosedV679Tests COMMAND arpsid_legacy_program_expectation_closed_v679_tests)

add_executable(arpsid_gui_bank_segment_canonical_layout_v680_tests source/tests/gui_bank_segment_canonical_layout_v680_tests.cpp)
target_compile_features(arpsid_gui_bank_segment_canonical_layout_v680_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_gui_bank_segment_canonical_layout_v680_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME GuiBankSegmentCanonicalLayoutV680Tests COMMAND arpsid_gui_bank_segment_canonical_layout_v680_tests)

add_executable(arpsid_warning_clean_test_contract_v681_tests source/tests/warning_clean_test_contract_v681_tests.cpp)
target_compile_features(arpsid_warning_clean_test_contract_v681_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_warning_clean_test_contract_v681_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME WarningCleanTestContractV681Tests COMMAND arpsid_warning_clean_test_contract_v681_tests)

add_executable(arpsid_gui_bank_segment_ux_polish_v682_tests source/tests/gui_bank_segment_ux_polish_v682_tests.cpp)
target_compile_features(arpsid_gui_bank_segment_ux_polish_v682_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_gui_bank_segment_ux_polish_v682_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME GuiBankSegmentUxPolishV682Tests COMMAND arpsid_gui_bank_segment_ux_polish_v682_tests)
add_executable(arpsid_factory_drsid_canonical_payload_v683_tests source/tests/factory_drsid_canonical_payload_v683_tests.cpp)
target_link_libraries(arpsid_factory_drsid_canonical_payload_v683_tests PRIVATE arpsid_forensic_patchbank)
target_compile_features(arpsid_factory_drsid_canonical_payload_v683_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_factory_drsid_canonical_payload_v683_tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/source)
add_test(NAME FactoryDrSidCanonicalPayloadV683Tests COMMAND arpsid_factory_drsid_canonical_payload_v683_tests)

add_executable(arpsid_factory_digi_canonical_payload_v684_tests source/tests/factory_digi_canonical_payload_v684_tests.cpp)
target_link_libraries(arpsid_factory_digi_canonical_payload_v684_tests PRIVATE arpsid_forensic_patchbank)
target_compile_features(arpsid_factory_digi_canonical_payload_v684_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_factory_digi_canonical_payload_v684_tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/source)
add_test(NAME FactoryDigiCanonicalPayloadV684Tests COMMAND arpsid_factory_digi_canonical_payload_v684_tests)

add_executable(arpsid_kit_default_assignments_payload_v685_tests source/tests/kit_default_assignments_payload_v685_tests.cpp)
target_link_libraries(arpsid_kit_default_assignments_payload_v685_tests PRIVATE arpsid_forensic_patchbank)
target_compile_features(arpsid_kit_default_assignments_payload_v685_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_kit_default_assignments_payload_v685_tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/source)
add_test(NAME KitDefaultAssignmentsPayloadV685Tests COMMAND arpsid_kit_default_assignments_payload_v685_tests)

add_executable(arpsid_vst3_factory_preset_count_v686_tests source/tests/vst3_factory_preset_count_v686_tests.cpp)
target_compile_features(arpsid_vst3_factory_preset_count_v686_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_vst3_factory_preset_count_v686_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME Vst3FactoryPresetCountV686Tests COMMAND arpsid_vst3_factory_preset_count_v686_tests)

add_executable(arpsid_factory_bank_export_boundary_v687_tests source/tests/factory_bank_export_boundary_v687_tests.cpp)
target_compile_features(arpsid_factory_bank_export_boundary_v687_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_factory_bank_export_boundary_v687_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME FactoryBankExportBoundaryV687Tests COMMAND arpsid_factory_bank_export_boundary_v687_tests)

add_executable(arpsid_kit_sequencer_model_validation_v688_tests source/tests/kit_sequencer_model_validation_v688_tests.cpp)
target_compile_features(arpsid_kit_sequencer_model_validation_v688_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_kit_sequencer_model_validation_v688_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME KitSequencerModelValidationV688Tests COMMAND arpsid_kit_sequencer_model_validation_v688_tests)

add_executable(arpsid_factory_payload_final_closure_guard_v689_tests source/tests/factory_payload_final_closure_guard_v689_tests.cpp)
target_compile_features(arpsid_factory_payload_final_closure_guard_v689_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_factory_payload_final_closure_guard_v689_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME FactoryPayloadFinalClosureGuardV689Tests COMMAND arpsid_factory_payload_final_closure_guard_v689_tests)

add_executable(arpsid_legacy_drum_tests_context_aware_v690_tests source/tests/legacy_drum_tests_context_aware_v690_tests.cpp)
target_compile_features(arpsid_legacy_drum_tests_context_aware_v690_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_legacy_drum_tests_context_aware_v690_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME LegacyDrumTestsContextAwareV690Tests COMMAND arpsid_legacy_drum_tests_context_aware_v690_tests)

add_executable(arpsid_factory_digi_payload_projection_v691_tests source/tests/factory_digi_payload_projection_v691_tests.cpp)
target_compile_features(arpsid_factory_digi_payload_projection_v691_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_factory_digi_payload_projection_v691_tests PRIVATE include source)
add_test(NAME FactoryDigiPayloadProjectionV691Tests COMMAND arpsid_factory_digi_payload_projection_v691_tests)

add_executable(arpsid_factory_drsid_microprogram_payload_v692_tests source/tests/factory_drsid_microprogram_payload_v692_tests.cpp)
target_compile_features(arpsid_factory_drsid_microprogram_payload_v692_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_factory_drsid_microprogram_payload_v692_tests PRIVATE include source)
add_test(NAME FactoryDrSidMicroprogramPayloadV692Tests COMMAND arpsid_factory_drsid_microprogram_payload_v692_tests)

add_executable(arpsid_drum_bridge_digi_policy_v693_tests source/tests/drum_bridge_digi_policy_v693_tests.cpp)
target_compile_features(arpsid_drum_bridge_digi_policy_v693_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_drum_bridge_digi_policy_v693_tests PRIVATE include source)
add_test(NAME DrumBridgeDigiPolicyV693Tests COMMAND arpsid_drum_bridge_digi_policy_v693_tests)

add_executable(arpsid_no_dead_127_bank_slot_decode_v694_tests source/tests/no_dead_127_bank_slot_decode_v694_tests.cpp)
target_compile_features(arpsid_no_dead_127_bank_slot_decode_v694_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_no_dead_127_bank_slot_decode_v694_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME NoDead127BankSlotDecodeV694Tests COMMAND arpsid_no_dead_127_bank_slot_decode_v694_tests)

add_executable(arpsid_factory_payload_final_deep_guard_v695_tests source/tests/factory_payload_final_deep_guard_v695_tests.cpp)
target_compile_features(arpsid_factory_payload_final_deep_guard_v695_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_factory_payload_final_deep_guard_v695_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME FactoryPayloadFinalDeepGuardV695Tests COMMAND arpsid_factory_payload_final_deep_guard_v695_tests)

add_executable(arpsid_voice_policy_all_notes_off_channel_bounds_v696_tests source/tests/voice_policy_all_notes_off_channel_bounds_v696_tests.cpp)
target_compile_features(arpsid_voice_policy_all_notes_off_channel_bounds_v696_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_voice_policy_all_notes_off_channel_bounds_v696_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME VoicePolicyAllNotesOffChannelBoundsV696Tests COMMAND arpsid_voice_policy_all_notes_off_channel_bounds_v696_tests)

add_executable(arpsid_full_preflight_pass83_and_stale_128_v697_tests source/tests/full_preflight_pass83_and_stale_128_v697_tests.cpp)
target_compile_features(arpsid_full_preflight_pass83_and_stale_128_v697_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_full_preflight_pass83_and_stale_128_v697_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME FullPreflightPass83AndStale128V697Tests COMMAND arpsid_full_preflight_pass83_and_stale_128_v697_tests)


add_executable(arpsid_digi_gui_pad_audition_v700_tests source/tests/digi_gui_pad_audition_v700_tests.cpp)
target_compile_features(arpsid_digi_gui_pad_audition_v700_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_digi_gui_pad_audition_v700_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME DigiGuiPadAuditionV700Tests COMMAND arpsid_digi_gui_pad_audition_v700_tests)

add_executable(arpsid_digi_midi_pad_transport_stop_v745_tests source/tests/digi_midi_pad_transport_stop_v745_tests.cpp)
target_compile_features(arpsid_digi_midi_pad_transport_stop_v745_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_digi_midi_pad_transport_stop_v745_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME DigiMidiPadTransportStopV745Tests COMMAND arpsid_digi_midi_pad_transport_stop_v745_tests)

add_executable(arpsid_release_closure_v701_tests source/tests/release_closure_v701_tests.cpp)
target_compile_features(arpsid_release_closure_v701_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_release_closure_v701_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME ReleaseClosureV701Tests COMMAND arpsid_release_closure_v701_tests)

add_executable(arpsid_release_packaging_v702_tests source/tests/release_packaging_v702_tests.cpp)
target_compile_features(arpsid_release_packaging_v702_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_release_packaging_v702_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME ReleasePackagingV702Tests COMMAND arpsid_release_packaging_v702_tests)

add_executable(arpsid_release_no_junk_placeholder_v703_tests source/tests/release_no_junk_placeholder_v703_tests.cpp)
target_compile_features(arpsid_release_no_junk_placeholder_v703_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_release_no_junk_placeholder_v703_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME ReleaseNoJunkPlaceholderV703Tests COMMAND arpsid_release_no_junk_placeholder_v703_tests)

add_executable(arpsid_release_manifest_v704_tests source/tests/release_manifest_v704_tests.cpp)
target_compile_features(arpsid_release_manifest_v704_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_release_manifest_v704_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME ReleaseManifestV704Tests COMMAND arpsid_release_manifest_v704_tests)

add_executable(arpsid_source_comment_code_merge_guard_v704_tests source/tests/source_comment_code_merge_guard_v704_tests.cpp)
target_compile_features(arpsid_source_comment_code_merge_guard_v704_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_source_comment_code_merge_guard_v704_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME SourceCommentCodeMergeGuardV704Tests COMMAND arpsid_source_comment_code_merge_guard_v704_tests)

add_executable(arpsid_release_root_clean_v705_tests source/tests/release_root_clean_v705_tests.cpp)
target_compile_features(arpsid_release_root_clean_v705_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_release_root_clean_v705_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" ARPSID_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME ReleaseRootCleanV705Tests COMMAND arpsid_release_root_clean_v705_tests)

add_executable(arpsid_c64_d418_digi_capture_v706_tests source/tests/c64_d418_digi_capture_v706_tests.cpp)
target_compile_features(arpsid_c64_d418_digi_capture_v706_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_d418_digi_capture_v706_tests PRIVATE include source)
add_test(NAME C64D418DigiCaptureV706Tests COMMAND arpsid_c64_d418_digi_capture_v706_tests)

add_executable(arpsid_c64_open_bus_vic_hle_v707_tests source/tests/c64_open_bus_vic_hle_v707_tests.cpp)
target_compile_features(arpsid_c64_open_bus_vic_hle_v707_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_open_bus_vic_hle_v707_tests PRIVATE include source)
add_test(NAME C64OpenBusVicHleV707Tests COMMAND arpsid_c64_open_bus_vic_hle_v707_tests)

add_executable(arpsid_c64_full_sidcore_integration_v708_tests source/tests/c64_full_sidcore_integration_v708_tests.cpp)
target_compile_features(arpsid_c64_full_sidcore_integration_v708_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_full_sidcore_integration_v708_tests PRIVATE include source)
add_test(NAME C64FullSidcoreIntegrationV708Tests COMMAND arpsid_c64_full_sidcore_integration_v708_tests)
add_executable(arpsid_c64_sidcore_complete_integration_v709_tests source/tests/c64_sidcore_complete_integration_v709_tests.cpp)
target_compile_features(arpsid_c64_sidcore_complete_integration_v709_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_sidcore_complete_integration_v709_tests PRIVATE include source)
add_test(NAME C64SidcoreCompleteIntegrationV709Tests COMMAND arpsid_c64_sidcore_complete_integration_v709_tests)

add_executable(arpsid_c64_sid_trace_import_v710_tests source/tests/c64_sid_trace_import_v710_tests.cpp)
target_compile_features(arpsid_c64_sid_trace_import_v710_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_sid_trace_import_v710_tests PRIVATE include source)
add_test(NAME C64SidTraceImportV710Tests COMMAND arpsid_c64_sid_trace_import_v710_tests)

add_executable(arpsid_c64_sid_readback_physical_v711_tests source/tests/c64_sid_readback_physical_v711_tests.cpp)
target_compile_features(arpsid_c64_sid_readback_physical_v711_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_sid_readback_physical_v711_tests PRIVATE include source)
add_test(NAME C64SidReadbackPhysicalV711Tests COMMAND arpsid_c64_sid_readback_physical_v711_tests)

add_executable(arpsid_c64_cia_tod_write_protocol_v712_tests source/tests/c64_cia_tod_write_protocol_v712_tests.cpp)
target_compile_features(arpsid_c64_cia_tod_write_protocol_v712_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_cia_tod_write_protocol_v712_tests PRIVATE include source)
add_test(NAME C64CiaTodWriteProtocolV712Tests COMMAND arpsid_c64_cia_tod_write_protocol_v712_tests)
add_executable(arpsid_c64_sid_mirror_multisid_v713_tests source/tests/c64_sid_mirror_multisid_v713_tests.cpp)
target_compile_features(arpsid_c64_sid_mirror_multisid_v713_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_sid_mirror_multisid_v713_tests PRIVATE include source)
add_test(NAME C64SidMirrorMultiSidV713Tests COMMAND arpsid_c64_sid_mirror_multisid_v713_tests)
add_executable(arpsid_c64_sid_bridge_reset_stale_v714_tests source/tests/c64_sid_bridge_reset_stale_v714_tests.cpp)
target_compile_features(arpsid_c64_sid_bridge_reset_stale_v714_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_sid_bridge_reset_stale_v714_tests PRIVATE include source)
add_test(NAME C64SidBridgeResetStaleV714Tests COMMAND arpsid_c64_sid_bridge_reset_stale_v714_tests)

add_executable(arpsid_c64_sid_unmapped_holes_v715_tests source/tests/c64_sid_unmapped_holes_v715_tests.cpp)
target_compile_features(arpsid_c64_sid_unmapped_holes_v715_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_sid_unmapped_holes_v715_tests PRIVATE include source)
add_test(NAME C64SidUnmappedHolesV715Tests COMMAND arpsid_c64_sid_unmapped_holes_v715_tests)

add_executable(arpsid_c64_vic_raster_compare_v716_tests source/tests/c64_vic_raster_compare_v716_tests.cpp)
target_compile_features(arpsid_c64_vic_raster_compare_v716_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_vic_raster_compare_v716_tests PRIVATE include source)
add_test(NAME C64VicRasterCompareV716Tests COMMAND arpsid_c64_vic_raster_compare_v716_tests)

add_executable(arpsid_c64_sid_hole_write_gate_v717_tests source/tests/c64_sid_hole_write_gate_v717_tests.cpp)
target_compile_features(arpsid_c64_sid_hole_write_gate_v717_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_sid_hole_write_gate_v717_tests PRIVATE include source)
add_test(NAME C64SidHoleWriteGateV717Tests COMMAND arpsid_c64_sid_hole_write_gate_v717_tests)

add_executable(arpsid_c64_legacy_decimal_parity_v718_tests source/tests/c64_legacy_decimal_parity_v718_tests.cpp)
target_compile_features(arpsid_c64_legacy_decimal_parity_v718_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_legacy_decimal_parity_v718_tests PRIVATE include source)
add_test(NAME C64LegacyDecimalParityV718Tests COMMAND arpsid_c64_legacy_decimal_parity_v718_tests)

add_executable(arpsid_c64_strict_rsid_phi2_only_v719_tests source/tests/c64_strict_rsid_phi2_only_v719_tests.cpp)
target_compile_features(arpsid_c64_strict_rsid_phi2_only_v719_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_strict_rsid_phi2_only_v719_tests PRIVATE include source)
add_test(NAME C64StrictRsidPhi2OnlyV719Tests COMMAND arpsid_c64_strict_rsid_phi2_only_v719_tests)

add_executable(arpsid_c64_exactness_ledger_v720_tests source/tests/c64_exactness_ledger_v720_tests.cpp)
target_compile_features(arpsid_c64_exactness_ledger_v720_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_exactness_ledger_v720_tests PRIVATE include source)
add_test(NAME C64ExactnessLedgerV720Tests COMMAND arpsid_c64_exactness_ledger_v720_tests)

add_executable(arpsid_c64_legacy_approximate_opcode_ledger_v721_tests source/tests/c64_legacy_approximate_opcode_ledger_v721_tests.cpp)
target_compile_features(arpsid_c64_legacy_approximate_opcode_ledger_v721_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_legacy_approximate_opcode_ledger_v721_tests PRIVATE include source)
add_test(NAME C64LegacyApproximateOpcodeLedgerV721Tests COMMAND arpsid_c64_legacy_approximate_opcode_ledger_v721_tests)
add_executable(arpsid_c64_strict_direct_phi2_refusal_v722_tests source/tests/c64_strict_direct_phi2_refusal_v722_tests.cpp)
target_compile_features(arpsid_c64_strict_direct_phi2_refusal_v722_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_strict_direct_phi2_refusal_v722_tests PRIVATE include source)
add_test(NAME C64StrictDirectPhi2RefusalV722Tests COMMAND arpsid_c64_strict_direct_phi2_refusal_v722_tests)

add_executable(arpsid_c64_cia2_vic_bank_pins_v723_tests source/tests/c64_cia2_vic_bank_pins_v723_tests.cpp)
target_compile_features(arpsid_c64_cia2_vic_bank_pins_v723_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_cia2_vic_bank_pins_v723_tests PRIVATE include source)
add_test(NAME C64Cia2VicBankPinsV723Tests COMMAND arpsid_c64_cia2_vic_bank_pins_v723_tests)

add_executable(arpsid_c64_rsid_exactness_naming_v724_tests source/tests/c64_rsid_exactness_naming_v724_tests.cpp)
target_compile_features(arpsid_c64_rsid_exactness_naming_v724_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_rsid_exactness_naming_v724_tests PRIVATE include source)
add_test(NAME C64RsidExactnessNamingV724Tests COMMAND arpsid_c64_rsid_exactness_naming_v724_tests)

add_executable(arpsid_c64_cpu_jam_reason_v725_tests source/tests/c64_cpu_jam_reason_v725_tests.cpp)
target_compile_features(arpsid_c64_cpu_jam_reason_v725_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_cpu_jam_reason_v725_tests PRIVATE include source)
add_test(NAME C64CpuJamReasonV725Tests COMMAND arpsid_c64_cpu_jam_reason_v725_tests)

add_executable(arpsid_c64_rsid_status_code_v726_tests source/tests/c64_rsid_status_code_v726_tests.cpp)
target_compile_features(arpsid_c64_rsid_status_code_v726_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_rsid_status_code_v726_tests PRIVATE include source)
add_test(NAME C64RsidStatusCodeV726Tests COMMAND arpsid_c64_rsid_status_code_v726_tests)

add_executable(arpsid_c64_remaining_findings_guard_v727_tests source/tests/c64_remaining_findings_guard_v727_tests.cpp)
target_compile_features(arpsid_c64_remaining_findings_guard_v727_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_remaining_findings_guard_v727_tests PRIVATE include source)
add_test(NAME C64RemainingFindingsGuardV727Tests COMMAND arpsid_c64_remaining_findings_guard_v727_tests)

add_executable(arpsid_c64_strict_status_policy_v728_tests source/tests/c64_strict_status_policy_v728_tests.cpp)
target_compile_features(arpsid_c64_strict_status_policy_v728_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_strict_status_policy_v728_tests PRIVATE include source)
add_test(NAME C64StrictStatusPolicyV728Tests COMMAND arpsid_c64_strict_status_policy_v728_tests)


if(ARPSID_BUILD_TESTS)
  # pass111 - convenience target for the small, high-signal DIGI/$D418 contract suite.
  # This target is intentionally separate from the full test build so CI and
  # macOS validation scripts can prove the current D418 authority path quickly
  # before the much larger legacy regression inventory is compiled.
  add_custom_target(arpsid_digi_d418_contract_suite
    DEPENDS
      arpsid_digi_d418_stream_engine_v698_tests
      arpsid_digi_d418_sid_volume_dac_v699_tests
      arpsid_dsp_kernel_include_smoke_v633_tests
      arpsid_full_preflight_pass83_and_stale_128_v697_tests
      arpsid_digi_gui_pad_audition_v700_tests)

  # pass125 - full DIGI/GUI/MIDI/Factory regression suite.  This closes the
  # false-confidence gap where --contract-only proved only the small D418 smoke
  # subset while GUI pads, MIDI mapping, factory payload projection and
  # DrumBridge/DIGI policy targets were not built before CTest selection.
  add_custom_target(arpsid_digi_gui_full_contract_suite
    DEPENDS
      arpsid_digi_d418_stream_engine_v698_tests
      arpsid_digi_d418_sid_volume_dac_v699_tests
      arpsid_dsp_kernel_include_smoke_v633_tests
      arpsid_full_preflight_pass83_and_stale_128_v697_tests
      arpsid_digi_panel_model_v563_tests
      arpsid_digi_panel_tab_v564_tests
      arpsid_digi_state_persistence_v565_tests
      arpsid_digi_sampler_engine_v595_tests
      arpsid_digi_sample_bank_v596_tests
      arpsid_gui_realtime_projection_v588_tests
      arpsid_gui_viewcontroller_wiring_v590_tests
      arpsid_digi_kit_bridge_policy_v622_tests
      arpsid_digi_slot_bridge_contract_v639_tests
      arpsid_digi_nonzero_slot_runtime_v641_tests
      arpsid_digi_kit_dense_projection_v648_tests
      arpsid_factory_digi_canonical_payload_v684_tests
      arpsid_factory_digi_payload_projection_v691_tests
      arpsid_drum_bridge_digi_policy_v693_tests
      arpsid_gui_apply_readback_extended_slot_v671_tests
      arpsid_gui_bank_label_refresh_v676_tests
      arpsid_gui_bank_segment_canonical_layout_v680_tests
      arpsid_gui_bank_segment_ux_polish_v682_tests
      arpsid_gui_color_and_boundary_compile_guard_v678_tests
      arpsid_gui_extended_factory_slot_contract_v668_tests
      arpsid_gui_factory_grid_range_v661_tests
      arpsid_digi_gui_pad_audition_v700_tests)

  # pass126 - release closure suite. This is intentionally broader than
  # --digi-full: it also builds tab/no-scaffold, tooltip, factory deep-guard and
  # dead-slot decode tests so the release entrypoint catches scaffolding/junk
  # regressions before a zip is shipped.
  add_custom_target(arpsid_release_closure_suite
    DEPENDS
      arpsid_digi_gui_full_contract_suite
      arpsid_gui_tab_architecture_v543_tests
      arpsid_sidcore_panel_model_and_tab_wire_v545_tests
      arpsid_v544_tab_wire_completeness_v546_tests
      arpsid_mix_panel_model_v547_tests
      arpsid_mix_panel_tab_v548_tests
      arpsid_tab_architecture_promotion_v562_tests
      arpsid_tab_architecture_promotion_v566_tests
      arpsid_c64_state_tab_promotion_v581_tests
      arpsid_tab_tooltip_coverage_v586_tests
      arpsid_kit_tab_wire_v556_tests
      arpsid_tab_no_scaffold_v631_tests
      arpsid_bank_ui_text_and_tooltip_v677_tests
      arpsid_no_dead_127_bank_slot_decode_v694_tests
      arpsid_factory_payload_final_closure_guard_v689_tests
      arpsid_factory_payload_final_deep_guard_v695_tests
      arpsid_release_closure_v701_tests
      arpsid_release_packaging_v702_tests
      arpsid_release_no_junk_placeholder_v703_tests
      arpsid_release_manifest_v704_tests
      arpsid_source_comment_code_merge_guard_v704_tests
      arpsid_release_root_clean_v705_tests
      arpsid_c64_d418_digi_capture_v706_tests
      arpsid_c64_sidcore_complete_integration_v709_tests
      arpsid_c64_sid_trace_import_v710_tests
      arpsid_c64_sid_readback_physical_v711_tests
      arpsid_c64_cia_tod_write_protocol_v712_tests
      arpsid_c64_sid_mirror_multisid_v713_tests
      arpsid_c64_sid_bridge_reset_stale_v714_tests
      arpsid_c64_sid_unmapped_holes_v715_tests
      arpsid_c64_vic_raster_compare_v716_tests
      arpsid_c64_open_bus_vic_hle_v707_tests
      arpsid_c64_sid_hole_write_gate_v717_tests
      arpsid_c64_legacy_decimal_parity_v718_tests
      arpsid_c64_strict_rsid_phi2_only_v719_tests
      arpsid_c64_exactness_ledger_v720_tests
      arpsid_c64_legacy_approximate_opcode_ledger_v721_tests
      arpsid_c64_strict_direct_phi2_refusal_v722_tests
      arpsid_c64_cia2_vic_bank_pins_v723_tests
      arpsid_c64_ts_reference_integration_v735_tests
      arpsid_c64_rom_identity_v737_tests
      arpsid_state_root_mailbox_ownership_v748_tests
      arpsid_template_blob_mailbox_ownership_v749_tests
      arpsid_render_state_restore_alloc_trap_v750_tests
      arpsid_render_drum_bridge_deferral_v751_tests
      arpsid_ingress_reset_epoch_boundary_v752_tests
      arpsid_c64_rom_helper_range_safe_v753_tests
      arpsid_auv2_cocoa_view_timeout_orphan_v754_tests
      arpsid_auv2_attached_view_replacement_v755_tests
      arpsid_cvdisplaylink_callback_lifetime_v756_tests
      arpsid_param_observer_no_bridge_reentrancy_v757_tests
      arpsid_c64_no_sink_potxy_blocker_v758_tests
      arpsid_vst_cocoa_factory_preset_range_v759_tests
      arpsid_drum_context_legacy_canonical_split_v760_tests
      arpsid_c64_physical_only_policy_v738_tests
      arpsid_c64_psid_rsid_boot_contract_v739_tests
      arpsid_c64_psid_cia_phi2_service_v740_tests
      arpsid_c64_cycle_exact_closure_v741_tests
      arpsid_split_brain_authority_closure_v742_tests
      arpsid_auv2_pure_sid_d418_zoh_continuity_v736_tests
      arpsid_sid_core_audit_v864_tests
      # C64/SIDCORE closure: first-class $D418 capture and HLE/open-bus/VIC guards.
      # pass132 - close the broad release/Factory/DrumBridge CTest gap: every
      # release/DIGI/GUI/factory/drum test matched by the release-check regex is
      # built before CTest, so a clean tree cannot report Not Run because the
      # executable was never compiled.
      arpsid_release_gate_regression_tests
      arpsid_factory_sid808_kits_and_wavetable_runner_v536_tests
      arpsid_factory_drsid_kit_bank_sweep_v238_tests
      arpsid_factory_patch_audio_fingerprint_reset_parity_v240_tests
      arpsid_release_final_closure_v404_tests
      arpsid_release_runtime_closure_v408_tests
      arpsid_sid808_factory_definition_v504_tests
      arpsid_drum_bridge_no_silence_v613_tests
      arpsid_drum_bridge_behavior_v621_tests
      arpsid_drum_bridge_mix_policy_v623_tests
      arpsid_kit_sid808_factory_voice_precedence_v637_tests
      arpsid_drsid_factory_slot_audio_shape_v640_tests
      arpsid_drum_bridge_runtime_authority_v644_tests
      arpsid_factory_canonical_drum_slots_v658_tests
      arpsid_factory_slot_schema_migration_v659_tests
      arpsid_factory_slot_encoding_roundtrip_v665_tests
      arpsid_factory_state_root_extended_identity_v666_tests
      arpsid_au_extended_factory_slot_contract_v667_tests
      arpsid_factory_no_legacy_127_alias_v669_tests
      arpsid_full_ctest_preflight_script_v670_tests
      arpsid_auv2_extended_factory_slot_identity_v672_tests
      arpsid_factory_program_metadata_policy_v673_tests
      arpsid_factory_identity_final_sweep_v674_tests
      arpsid_filebank_extended_factory_boundary_v675_tests
      arpsid_factory_drsid_canonical_payload_v683_tests
      arpsid_vst3_factory_preset_count_v686_tests
      arpsid_factory_bank_export_boundary_v687_tests
      arpsid_factory_drsid_microprogram_payload_v692_tests)
endif()

if(ARPSID_BUILD_TESTS)
add_executable(arpsid_c64_strict_status_gui_contract_v729_tests source/tests/c64_strict_status_gui_contract_v729_tests.cpp)
target_compile_features(arpsid_c64_strict_status_gui_contract_v729_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_strict_status_gui_contract_v729_tests PRIVATE include source)
add_test(NAME C64StrictStatusGuiContractV729Tests COMMAND arpsid_c64_strict_status_gui_contract_v729_tests)

add_executable(arpsid_auv2_digi_adapter_compile_guard_v730_tests source/tests/auv2_digi_adapter_compile_guard_v730_tests.cpp)
target_compile_features(arpsid_auv2_digi_adapter_compile_guard_v730_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_auv2_digi_adapter_compile_guard_v730_tests PRIVATE include source)
target_compile_definitions(arpsid_auv2_digi_adapter_compile_guard_v730_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Auv2DigiAdapterCompileGuardV730Tests COMMAND arpsid_auv2_digi_adapter_compile_guard_v730_tests)

add_executable(arpsid_auv2_view_controller_compile_guard_v731_tests source/tests/auv2_view_controller_compile_guard_v731_tests.cpp)
target_compile_features(arpsid_auv2_view_controller_compile_guard_v731_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_auv2_view_controller_compile_guard_v731_tests PRIVATE include source)
target_compile_definitions(arpsid_auv2_view_controller_compile_guard_v731_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Auv2ViewControllerCompileGuardV731Tests COMMAND arpsid_auv2_view_controller_compile_guard_v731_tests)

add_executable(arpsid_auv2_view_warning_free_v732_tests source/tests/auv2_view_warning_free_v732_tests.cpp)
target_compile_features(arpsid_auv2_view_warning_free_v732_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_auv2_view_warning_free_v732_tests PRIVATE include source)
target_compile_definitions(arpsid_auv2_view_warning_free_v732_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Auv2ViewWarningFreeV732Tests COMMAND arpsid_auv2_view_warning_free_v732_tests)


add_executable(arpsid_auv2_source_hygiene_v733_tests source/tests/auv2_source_hygiene_v733_tests.cpp)
target_compile_features(arpsid_auv2_source_hygiene_v733_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_auv2_source_hygiene_v733_tests PRIVATE include source)
target_compile_definitions(arpsid_auv2_source_hygiene_v733_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Auv2SourceHygieneV733Tests COMMAND arpsid_auv2_source_hygiene_v733_tests)

add_executable(arpsid_auv2_editor_sync_build_no_defer_v835_tests source/tests/auv2_editor_sync_build_no_defer_v835_tests.cpp)
target_compile_features(arpsid_auv2_editor_sync_build_no_defer_v835_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_auv2_editor_sync_build_no_defer_v835_tests PRIVATE include source)
target_compile_definitions(arpsid_auv2_editor_sync_build_no_defer_v835_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Auv2EditorSyncBuildNoDeferV835Tests COMMAND arpsid_auv2_editor_sync_build_no_defer_v835_tests)

add_executable(arpsid_auv2_kernel_overaligned_no_make_shared_v836_tests source/tests/auv2_kernel_overaligned_no_make_shared_v836_tests.cpp)
target_compile_features(arpsid_auv2_kernel_overaligned_no_make_shared_v836_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_auv2_kernel_overaligned_no_make_shared_v836_tests PRIVATE include source)
target_compile_definitions(arpsid_auv2_kernel_overaligned_no_make_shared_v836_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Auv2KernelOveralignedNoMakeSharedV836Tests COMMAND arpsid_auv2_kernel_overaligned_no_make_shared_v836_tests)

add_executable(arpsid_dsp_kernel_allocation_shape_v842_tests source/tests/dsp_kernel_allocation_shape_v842_tests.cpp)
target_compile_features(arpsid_dsp_kernel_allocation_shape_v842_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_dsp_kernel_allocation_shape_v842_tests PRIVATE include source)
target_compile_definitions(arpsid_dsp_kernel_allocation_shape_v842_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME DspKernelAllocationShapeV842Tests COMMAND arpsid_dsp_kernel_allocation_shape_v842_tests)

add_executable(arpsid_dsp_kernel_gui_realtime_publish_stack_v843_tests source/tests/dsp_kernel_gui_realtime_publish_stack_v843_tests.cpp)
target_compile_features(arpsid_dsp_kernel_gui_realtime_publish_stack_v843_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_dsp_kernel_gui_realtime_publish_stack_v843_tests PRIVATE include source)
target_compile_definitions(arpsid_dsp_kernel_gui_realtime_publish_stack_v843_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME DspKernelGuiRealtimePublishStackV843Tests COMMAND arpsid_dsp_kernel_gui_realtime_publish_stack_v843_tests)

add_executable(arpsid_c64_vic_fast_toggle_v838_tests source/tests/c64_vic_fast_toggle_v838_tests.cpp)
target_compile_features(arpsid_c64_vic_fast_toggle_v838_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_c64_vic_fast_toggle_v838_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME C64VicFastToggleV838Tests COMMAND arpsid_c64_vic_fast_toggle_v838_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_c64_vic_fast_toggle_v838_tests)
endif()

add_executable(arpsid_c64_6510_fast_toggle_v839_tests source/tests/c64_6510_fast_toggle_v839_tests.cpp)
target_compile_features(arpsid_c64_6510_fast_toggle_v839_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_c64_6510_fast_toggle_v839_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME C646510FastToggleV839Tests COMMAND arpsid_c64_6510_fast_toggle_v839_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_c64_6510_fast_toggle_v839_tests)
endif()

add_executable(arpsid_c64_render_stall_instrumentation_v840_tests source/tests/c64_render_stall_instrumentation_v840_tests.cpp)
target_compile_features(arpsid_c64_render_stall_instrumentation_v840_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_c64_render_stall_instrumentation_v840_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME C64RenderStallInstrumentationV840Tests COMMAND arpsid_c64_render_stall_instrumentation_v840_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_c64_render_stall_instrumentation_v840_tests)
endif()

add_executable(arpsid_sid808_target_routed_drum_midi_v862_tests source/tests/sid808_target_routed_drum_midi_v862_tests.cpp)
target_compile_features(arpsid_sid808_target_routed_drum_midi_v862_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_sid808_target_routed_drum_midi_v862_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Sid808TargetRoutedDrumMidiV862Tests COMMAND arpsid_sid808_target_routed_drum_midi_v862_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_sid808_target_routed_drum_midi_v862_tests)
endif()

add_executable(arpsid_sid808_audible_authority_v865_tests source/tests/sid808_audible_authority_v865_tests.cpp)
target_compile_features(arpsid_sid808_audible_authority_v865_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_sid808_audible_authority_v865_tests PRIVATE arpsid_core)
target_compile_definitions(arpsid_sid808_audible_authority_v865_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Sid808AudibleAuthorityV865Tests COMMAND arpsid_sid808_audible_authority_v865_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_sid808_audible_authority_v865_tests)
endif()

add_executable(arpsid_sid808_snare_one_shot_v867_tests source/tests/sid808_snare_one_shot_v867_tests.cpp)
target_compile_features(arpsid_sid808_snare_one_shot_v867_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_sid808_snare_one_shot_v867_tests PRIVATE arpsid_core)
add_test(NAME Sid808SnareOneShotV867Tests COMMAND arpsid_sid808_snare_one_shot_v867_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_sid808_snare_one_shot_v867_tests)
endif()

add_executable(arpsid_sid808_snare_complete_closure_v868_tests source/tests/sid808_snare_complete_closure_v868_tests.cpp)
target_compile_features(arpsid_sid808_snare_complete_closure_v868_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_sid808_snare_complete_closure_v868_tests PRIVATE arpsid_core)
target_compile_definitions(arpsid_sid808_snare_complete_closure_v868_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Sid808SnareCompleteClosureV868Tests COMMAND arpsid_sid808_snare_complete_closure_v868_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_sid808_snare_complete_closure_v868_tests)
endif()

add_executable(arpsid_synthmode_held_replay_identity_v869_tests source/tests/synthmode_held_replay_identity_v869_tests.cpp)
target_compile_features(arpsid_synthmode_held_replay_identity_v869_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_synthmode_held_replay_identity_v869_tests PRIVATE arpsid_core)
target_compile_definitions(arpsid_synthmode_held_replay_identity_v869_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME SynthModeHeldReplayIdentityV869Tests COMMAND arpsid_synthmode_held_replay_identity_v869_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_synthmode_held_replay_identity_v869_tests)
endif()

add_executable(arpsid_sid808_snare_body_routing_v869_tests source/tests/sid808_snare_body_routing_v869_tests.cpp)
target_compile_features(arpsid_sid808_snare_body_routing_v869_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_sid808_snare_body_routing_v869_tests PRIVATE arpsid_core)
target_compile_definitions(arpsid_sid808_snare_body_routing_v869_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Sid808SnareBodyRoutingV869Tests COMMAND arpsid_sid808_snare_body_routing_v869_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_sid808_snare_body_routing_v869_tests)
endif()

add_executable(arpsid_sid808_musical_shape_v870_tests source/tests/sid808_musical_shape_v870_tests.cpp)
target_compile_features(arpsid_sid808_musical_shape_v870_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_sid808_musical_shape_v870_tests PRIVATE arpsid_core)
target_compile_definitions(arpsid_sid808_musical_shape_v870_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Sid808MusicalShapeV870Tests COMMAND arpsid_sid808_musical_shape_v870_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_sid808_musical_shape_v870_tests)
endif()

add_executable(arpsid_sid808_factory_musical_shape_v871_tests source/tests/sid808_factory_musical_shape_v871_tests.cpp)
target_compile_features(arpsid_sid808_factory_musical_shape_v871_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_sid808_factory_musical_shape_v871_tests PRIVATE arpsid_core)
add_test(NAME Sid808FactoryMusicalShapeV871Tests COMMAND arpsid_sid808_factory_musical_shape_v871_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_sid808_factory_musical_shape_v871_tests)
endif()

add_executable(arpsid_sid808_spectral_authenticity_v872_tests source/tests/sid808_spectral_authenticity_v872_tests.cpp)
target_compile_features(arpsid_sid808_spectral_authenticity_v872_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_sid808_spectral_authenticity_v872_tests PRIVATE arpsid_core)
add_test(NAME Sid808SpectralAuthenticityV872Tests COMMAND arpsid_sid808_spectral_authenticity_v872_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_sid808_spectral_authenticity_v872_tests)
endif()

add_executable(arpsid_sid_core_golden_trace_v872_tests source/tests/sid_core_golden_trace_v872_tests.cpp)
target_compile_features(arpsid_sid_core_golden_trace_v872_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_sid_core_golden_trace_v872_tests PRIVATE arpsid_core)
add_test(NAME SidCoreGoldenTraceV872Tests COMMAND arpsid_sid_core_golden_trace_v872_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_sid_core_golden_trace_v872_tests)
endif()

add_executable(arpsid_kit_sid808_class_map_v872_tests source/tests/kit_sid808_class_map_v872_tests.cpp)
target_compile_features(arpsid_kit_sid808_class_map_v872_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_kit_sid808_class_map_v872_tests PRIVATE arpsid_core)
add_test(NAME KitSid808ClassMapV872Tests COMMAND arpsid_kit_sid808_class_map_v872_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_kit_sid808_class_map_v872_tests)
endif()

add_executable(arpsid_c64_psid_cia_latch_reuse_v872_tests source/tests/c64_psid_cia_latch_reuse_v872_tests.cpp)
target_compile_features(arpsid_c64_psid_cia_latch_reuse_v872_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_c64_psid_cia_latch_reuse_v872_tests PRIVATE arpsid_core)
add_test(NAME C64PsidCiaLatchReuseV872Tests COMMAND arpsid_c64_psid_cia_latch_reuse_v872_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_c64_psid_cia_latch_reuse_v872_tests)
endif()

add_executable(arpsid_c64_psid_cia_render_scheduling_v872_tests source/tests/c64_psid_cia_render_scheduling_v872_tests.cpp)
target_compile_features(arpsid_c64_psid_cia_render_scheduling_v872_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_c64_psid_cia_render_scheduling_v872_tests PRIVATE arpsid_core)
target_compile_definitions(arpsid_c64_psid_cia_render_scheduling_v872_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME C64PsidCiaRenderSchedulingV872Tests COMMAND arpsid_c64_psid_cia_render_scheduling_v872_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_c64_psid_cia_render_scheduling_v872_tests)
endif()

add_executable(arpsid_transport_start_backend_reprojection_v872_tests source/tests/transport_start_backend_reprojection_v872_tests.cpp)
target_compile_features(arpsid_transport_start_backend_reprojection_v872_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_transport_start_backend_reprojection_v872_tests PRIVATE arpsid_core)
add_test(NAME TransportStartBackendReprojectionV872Tests COMMAND arpsid_transport_start_backend_reprojection_v872_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_transport_start_backend_reprojection_v872_tests)
endif()

add_executable(arpsid_c64_runplay_transaction_safety_v872_tests source/tests/c64_runplay_transaction_safety_v872_tests.cpp)
target_compile_features(arpsid_c64_runplay_transaction_safety_v872_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_c64_runplay_transaction_safety_v872_tests PRIVATE arpsid_core)
add_test(NAME C64RunPlayTransactionSafetyV872Tests COMMAND arpsid_c64_runplay_transaction_safety_v872_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_c64_runplay_transaction_safety_v872_tests)
endif()

add_executable(arpsid_sid808_prepare_resets_active_state_v872_tests source/tests/sid808_prepare_resets_active_state_v872_tests.cpp)
target_compile_features(arpsid_sid808_prepare_resets_active_state_v872_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_sid808_prepare_resets_active_state_v872_tests PRIVATE arpsid_core)
add_test(NAME Sid808PrepareResetsActiveStateV872Tests COMMAND arpsid_sid808_prepare_resets_active_state_v872_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_sid808_prepare_resets_active_state_v872_tests)
endif()

add_executable(arpsid_c64_render_transaction_v871_tests source/tests/c64_render_transaction_v871_tests.cpp)
target_compile_features(arpsid_c64_render_transaction_v871_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_c64_render_transaction_v871_tests PRIVATE arpsid_core)
target_compile_definitions(arpsid_c64_render_transaction_v871_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME C64RenderTransactionV871Tests COMMAND arpsid_c64_render_transaction_v871_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_c64_render_transaction_v871_tests)
endif()

add_executable(arpsid_c64_sidplay_rollback_behavioral_v872_tests source/tests/c64_sidplay_rollback_behavioral_v872_tests.cpp)
target_compile_features(arpsid_c64_sidplay_rollback_behavioral_v872_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_c64_sidplay_rollback_behavioral_v872_tests PRIVATE arpsid_core)
target_compile_definitions(arpsid_c64_sidplay_rollback_behavioral_v872_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME C64SidplayRollbackBehavioralV872Tests COMMAND arpsid_c64_sidplay_rollback_behavioral_v872_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_c64_sidplay_rollback_behavioral_v872_tests)
endif()

add_executable(arpsid_c64_sidplay_cia_multisid_rollback_v872_tests source/tests/c64_sidplay_cia_multisid_rollback_v872_tests.cpp)
target_compile_features(arpsid_c64_sidplay_cia_multisid_rollback_v872_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_c64_sidplay_cia_multisid_rollback_v872_tests PRIVATE arpsid_core)
add_test(NAME C64SidplayCiaMultiSidRollbackV872Tests COMMAND arpsid_c64_sidplay_cia_multisid_rollback_v872_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_c64_sidplay_cia_multisid_rollback_v872_tests)
endif()

add_executable(arpsid_synthmode_voice_token_v871_tests source/tests/synthmode_voice_token_v871_tests.cpp)
target_compile_features(arpsid_synthmode_voice_token_v871_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_synthmode_voice_token_v871_tests PRIVATE arpsid_core)
target_compile_definitions(arpsid_synthmode_voice_token_v871_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME SynthModeVoiceTokenV871Tests COMMAND arpsid_synthmode_voice_token_v871_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_synthmode_voice_token_v871_tests)
endif()

add_executable(arpsid_sidplay_register_engine_v867_tests source/tests/sidplay_register_engine_v867_tests.cpp)
target_compile_features(arpsid_sidplay_register_engine_v867_tests PRIVATE cxx_std_17)
target_link_libraries(arpsid_sidplay_register_engine_v867_tests PRIVATE arpsid_core)
target_compile_definitions(arpsid_sidplay_register_engine_v867_tests PRIVATE ARPSID_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME SidplayRegisterEngineV867Tests COMMAND arpsid_sidplay_register_engine_v867_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_sidplay_register_engine_v867_tests)
endif()

add_executable(arpsid_auv2_version_metadata_v734_tests source/tests/auv2_version_metadata_v734_tests.cpp)
target_compile_features(arpsid_auv2_version_metadata_v734_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_auv2_version_metadata_v734_tests PRIVATE include source)
target_compile_definitions(arpsid_auv2_version_metadata_v734_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Auv2VersionMetadataV734Tests COMMAND arpsid_auv2_version_metadata_v734_tests)

add_executable(arpsid_c64_ts_reference_integration_v735_tests source/tests/c64_ts_reference_integration_v735_tests.cpp)
target_compile_features(arpsid_c64_ts_reference_integration_v735_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_ts_reference_integration_v735_tests PRIVATE include source)
add_test(NAME C64TsReferenceIntegrationV735Tests COMMAND arpsid_c64_ts_reference_integration_v735_tests)

add_executable(arpsid_c64_rom_identity_v737_tests source/tests/c64_rom_identity_v737_tests.cpp)
target_compile_features(arpsid_c64_rom_identity_v737_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_rom_identity_v737_tests PRIVATE include source)
add_test(NAME C64RomIdentityV737Tests COMMAND arpsid_c64_rom_identity_v737_tests)

# audit P0-1: state-root ownership mailbox (ABA / writer-laps-reader). Behavioral
# concurrency test of the RT-safe OwnershipMailbox that replaced the seqlock swap.
add_executable(arpsid_state_root_mailbox_ownership_v748_tests source/tests/state_root_mailbox_ownership_v748_tests.cpp)
target_compile_features(arpsid_state_root_mailbox_ownership_v748_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_state_root_mailbox_ownership_v748_tests PRIVATE include source)
find_package(Threads REQUIRED)
target_link_libraries(arpsid_state_root_mailbox_ownership_v748_tests PRIVATE Threads::Threads)
add_test(NAME StateRootMailboxOwnershipV748Tests COMMAND arpsid_state_root_mailbox_ownership_v748_tests)

# audit P0-2: serializable-template blob ownership mailbox (ABA / torn-read).
# Behavioral concurrency test of the OwnershipMailbox<FixedBlobSlot> that replaced
# the seqlock the UI decoder could read while the writer overwrote the slot.
add_executable(arpsid_template_blob_mailbox_ownership_v749_tests source/tests/template_blob_mailbox_ownership_v749_tests.cpp)
target_compile_features(arpsid_template_blob_mailbox_ownership_v749_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_template_blob_mailbox_ownership_v749_tests PRIVATE include source)
target_link_libraries(arpsid_template_blob_mailbox_ownership_v749_tests PRIVATE Threads::Threads)
add_test(NAME TemplateBlobMailboxOwnershipV749Tests COMMAND arpsid_template_blob_mailbox_ownership_v749_tests)

# audit P0-3: render-thread state restore must not allocate. Allocation-trap test
# proving SidRuntimeModel::applyStateRootBySwap (audio-thread apply) makes zero heap
# allocations now that canonicalization is pre-run on the non-RT producer.
add_executable(arpsid_render_state_restore_alloc_trap_v750_tests source/tests/render_state_restore_alloc_trap_v750_tests.cpp)
target_compile_features(arpsid_render_state_restore_alloc_trap_v750_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_render_state_restore_alloc_trap_v750_tests PRIVATE include source)
add_test(NAME RenderStateRestoreAllocTrapV750Tests COMMAND arpsid_render_state_restore_alloc_trap_v750_tests)

# audit P0-4: render-drained restore must not call the non-RT DrumEngineHostBridge
# loader. Behavioral test of the queue/drain mechanism + guard that the render apply
# path queues (never loads inline) and the non-RT teardown drains.
add_executable(arpsid_render_drum_bridge_deferral_v751_tests source/tests/render_drum_bridge_deferral_v751_tests.cpp)
target_compile_features(arpsid_render_drum_bridge_deferral_v751_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_render_drum_bridge_deferral_v751_tests PRIVATE include source)
target_compile_definitions(arpsid_render_drum_bridge_deferral_v751_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME RenderDrumBridgeDeferralV751Tests COMMAND arpsid_render_drum_bridge_deferral_v751_tests)

# audit P0-6 / fix-order #5: ingress reset epoch boundary. Behavioral test that a
# BoundedMpscRing slot reserved before clearEnqueuedBeforeNow() but published after
# is rejected (stale-straggler), via a white-box reserve/publish test seam.
add_executable(arpsid_ingress_reset_epoch_boundary_v752_tests source/tests/ingress_reset_epoch_boundary_v752_tests.cpp)
target_compile_features(arpsid_ingress_reset_epoch_boundary_v752_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_ingress_reset_epoch_boundary_v752_tests PRIVATE include source)
add_test(NAME IngressResetEpochBoundaryV752Tests COMMAND arpsid_ingress_reset_epoch_boundary_v752_tests)

# audit P0-8 / fix-order #6: range-safe C64 ROM helper indexing. Behavioral test
# sweeps the whole uint16_t address space through read/poke (ASAN-proof no OOB) and
# checks boundary round-trips. Built warning-clean (the old raw subtraction tripped
# -Warray-bounds / UB — also closes audit P2 "warning-clean ROM helper" item).
add_executable(arpsid_c64_rom_helper_range_safe_v753_tests source/tests/c64_rom_helper_range_safe_v753_tests.cpp)
target_compile_features(arpsid_c64_rom_helper_range_safe_v753_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_rom_helper_range_safe_v753_tests PRIVATE include source)
add_test(NAME C64RomHelperRangeSafeV753Tests COMMAND arpsid_c64_rom_helper_range_safe_v753_tests)

# audit P0-9 / fix-order #7: AUv2 Cocoa view factory orphan-after-timeout guard.
# Structural regression guard that the off-main build path coordinates abandonment
# and disposes the orphan editor on both the late-build and raced-timeout paths.
add_executable(arpsid_auv2_cocoa_view_timeout_orphan_v754_tests source/tests/auv2_cocoa_view_timeout_orphan_v754_tests.cpp)
target_compile_features(arpsid_auv2_cocoa_view_timeout_orphan_v754_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_auv2_cocoa_view_timeout_orphan_v754_tests PRIVATE include source)
target_compile_definitions(arpsid_auv2_cocoa_view_timeout_orphan_v754_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Auv2CocoaViewTimeoutOrphanV754Tests COMMAND arpsid_auv2_cocoa_view_timeout_orphan_v754_tests)

# audit P0-10 / fix-order #8: AUv2 attached-view replacement clean-detach guard.
# Structural regression guard that the replacement path detaches a still-attached
# outgoing view before disposing its controller and clears stale associations
# before building the replacement (runtime coverage in the AUv2 GUI smoke).
add_executable(arpsid_auv2_attached_view_replacement_v755_tests source/tests/auv2_attached_view_replacement_v755_tests.cpp)
target_compile_features(arpsid_auv2_attached_view_replacement_v755_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_auv2_attached_view_replacement_v755_tests PRIVATE include source)
target_compile_definitions(arpsid_auv2_attached_view_replacement_v755_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Auv2AttachedViewReplacementV755Tests COMMAND arpsid_auv2_attached_view_replacement_v755_tests)

# audit P0-11 / fix-order #9: CVDisplayLink callback lifetime (UAF). Structural guard
# that the callback context is a retained weak-box (zeroing __weak controller), the
# callback weak-loads (never raw-casts to the controller), and the box is freed only
# after the link is stopped/released.
add_executable(arpsid_cvdisplaylink_callback_lifetime_v756_tests source/tests/cvdisplaylink_callback_lifetime_v756_tests.cpp)
target_compile_features(arpsid_cvdisplaylink_callback_lifetime_v756_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_cvdisplaylink_callback_lifetime_v756_tests PRIVATE include source)
target_compile_definitions(arpsid_cvdisplaylink_callback_lifetime_v756_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME CVDisplayLinkCallbackLifetimeV756Tests COMMAND arpsid_cvdisplaylink_callback_lifetime_v756_tests)

# audit P0-12 / fix-order #10: parameter observer must not re-enter the AU bridge.
# Guard that the observer block caches the observed value + defers to the main-thread
# sync (never calls getParameterValue from the observer callback).
add_executable(arpsid_param_observer_no_bridge_reentrancy_v757_tests source/tests/param_observer_no_bridge_reentrancy_v757_tests.cpp)
target_compile_features(arpsid_param_observer_no_bridge_reentrancy_v757_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_param_observer_no_bridge_reentrancy_v757_tests PRIVATE include source)
target_compile_definitions(arpsid_param_observer_no_bridge_reentrancy_v757_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME ParamObserverNoBridgeReentrancyV757Tests COMMAND arpsid_param_observer_no_bridge_reentrancy_v757_tests)

# audit P1-13 / fix-order #11: no-sink POTX/POTY reads set PotXYApprox. Behavioral
# test of the no-sink platform counter + end-to-end runtime blocker.
add_executable(arpsid_c64_no_sink_potxy_blocker_v758_tests source/tests/c64_no_sink_potxy_blocker_v758_tests.cpp)
target_compile_features(arpsid_c64_no_sink_potxy_blocker_v758_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_no_sink_potxy_blocker_v758_tests PRIVATE include source)
add_test(NAME C64NoSinkPotxyBlockerV758Tests COMMAND arpsid_c64_no_sink_potxy_blocker_v758_tests)

# VST Cocoa preset list must expose the canonical extended 180-slot factory bank,
# not the legacy 128-slot range. The VST target is SDK-gated, so keep this as a
# default source guard.
add_executable(arpsid_vst_cocoa_factory_preset_range_v759_tests source/tests/vst_cocoa_factory_preset_range_v759_tests.cpp)
target_compile_features(arpsid_vst_cocoa_factory_preset_range_v759_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_vst_cocoa_factory_preset_range_v759_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME VstCocoaFactoryPresetRangeV759Tests COMMAND arpsid_vst_cocoa_factory_preset_range_v759_tests)


# fix-order #13: legacy DrSID compatibility classifier and canonical 180-slot
# factory ownership are intentionally separate policies; keep comments/tests honest.
add_executable(arpsid_drum_context_legacy_canonical_split_v760_tests source/tests/drum_context_legacy_canonical_split_v760_tests.cpp)
target_compile_features(arpsid_drum_context_legacy_canonical_split_v760_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_drum_context_legacy_canonical_split_v760_tests PRIVATE include source)
target_compile_definitions(arpsid_drum_context_legacy_canonical_split_v760_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME DrumContextLegacyCanonicalSplitV760Tests COMMAND arpsid_drum_context_legacy_canonical_split_v760_tests)

# fix-order #14: keep SID-core mutable aggregate surfaces out of the generic
# compatibility API. Mutation must flow through canonical runtime APIs or clearly
# marked internal/non-realtime escape hatches.
add_executable(arpsid_sidcore_single_authority_surface_v761_tests source/tests/sidcore_single_authority_surface_v761_tests.cpp)
target_compile_features(arpsid_sidcore_single_authority_surface_v761_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_sidcore_single_authority_surface_v761_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME SidcoreSingleAuthoritySurfaceV761Tests COMMAND arpsid_sidcore_single_authority_surface_v761_tests)

# fix-order #15: state-apply authority mapping is core-owned, not AU/VST-wrapper
# owned. Wrappers may name reasons, but must include/call the canonical policy.
add_executable(arpsid_wrapper_state_apply_policy_core_v762_tests source/tests/wrapper_state_apply_policy_core_v762_tests.cpp)
target_compile_features(arpsid_wrapper_state_apply_policy_core_v762_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_wrapper_state_apply_policy_core_v762_tests PRIVATE include)
target_compile_definitions(arpsid_wrapper_state_apply_policy_core_v762_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME WrapperStateApplyPolicyCoreV762Tests COMMAND arpsid_wrapper_state_apply_policy_core_v762_tests)

# fix-order #16: DIGI model + sample-bank state is an atomic pair. Keep legacy
# split selectors only as private fail-closed adapter compatibility stubs; do
# not advertise them through the public GUI/debug protocol.
add_executable(arpsid_digi_split_protocol_surface_v763_tests source/tests/digi_split_protocol_surface_v763_tests.cpp)
target_compile_features(arpsid_digi_split_protocol_surface_v763_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_digi_split_protocol_surface_v763_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME DigiSplitProtocolSurfaceV763Tests COMMAND arpsid_digi_split_protocol_surface_v763_tests)

# fix-order #17: parameter automation queue-drop telemetry must distinguish
# sample-accurate queue loss from dirty-flush fallback value preservation.
add_executable(arpsid_parameter_dirty_flush_fallback_telemetry_v764_tests source/tests/parameter_dirty_flush_fallback_telemetry_v764_tests.cpp)
target_compile_features(arpsid_parameter_dirty_flush_fallback_telemetry_v764_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_parameter_dirty_flush_fallback_telemetry_v764_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME ParameterDirtyFlushFallbackTelemetryV764Tests COMMAND arpsid_parameter_dirty_flush_fallback_telemetry_v764_tests)

# fix-order #18: AUv2 ramp expansion must publish anchor scheduled/drop
# telemetry instead of hiding timed-anchor loss behind generic parameter drops.
add_executable(arpsid_auv2_ramp_anchor_drop_telemetry_v765_tests source/tests/auv2_ramp_anchor_drop_telemetry_v765_tests.cpp)
target_compile_features(arpsid_auv2_ramp_anchor_drop_telemetry_v765_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_auv2_ramp_anchor_drop_telemetry_v765_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Auv2RampAnchorDropTelemetryV765Tests COMMAND arpsid_auv2_ramp_anchor_drop_telemetry_v765_tests)

# fix-order #19: post-#18 status must not claim completed telemetry work is deferred.
add_executable(arpsid_post18_closure_status_consistency_v766_tests source/tests/post18_closure_status_consistency_v766_tests.cpp)
target_compile_features(arpsid_post18_closure_status_consistency_v766_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_post18_closure_status_consistency_v766_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Post18ClosureStatusConsistencyV766Tests COMMAND arpsid_post18_closure_status_consistency_v766_tests)

add_executable(arpsid_c64_physical_only_policy_v738_tests source/tests/c64_physical_only_policy_v738_tests.cpp)
target_compile_features(arpsid_c64_physical_only_policy_v738_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_physical_only_policy_v738_tests PRIVATE include source)
target_compile_definitions(arpsid_c64_physical_only_policy_v738_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME C64PhysicalOnlyPolicyV738Tests COMMAND arpsid_c64_physical_only_policy_v738_tests)

# Exactness audit #7 — dedicated SID open-bus physical-exactness blocker.
add_executable(arpsid_c64_sid_open_bus_blocker_v689_tests source/tests/c64_sid_open_bus_blocker_v689_tests.cpp)
target_compile_features(arpsid_c64_sid_open_bus_blocker_v689_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_sid_open_bus_blocker_v689_tests PRIVATE include source)
target_compile_definitions(arpsid_c64_sid_open_bus_blocker_v689_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME C64SidOpenBusBlockerV689Tests COMMAND arpsid_c64_sid_open_bus_blocker_v689_tests)

# Exactness audit #8 — dedicated Color-RAM open-bus physical-exactness blocker.
add_executable(arpsid_c64_color_ram_open_bus_blocker_v690_tests source/tests/c64_color_ram_open_bus_blocker_v690_tests.cpp)
target_compile_features(arpsid_c64_color_ram_open_bus_blocker_v690_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_color_ram_open_bus_blocker_v690_tests PRIVATE include source)
target_compile_definitions(arpsid_c64_color_ram_open_bus_blocker_v690_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME C64ColorRamOpenBusBlockerV690Tests COMMAND arpsid_c64_color_ram_open_bus_blocker_v690_tests)

# Exactness audit #11 — dedicated POTX/POTY (paddle A-D) physical-exactness blocker.
add_executable(arpsid_c64_potxy_blocker_v691_tests source/tests/c64_potxy_blocker_v691_tests.cpp)
target_compile_features(arpsid_c64_potxy_blocker_v691_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_potxy_blocker_v691_tests PRIVATE include source)
target_compile_definitions(arpsid_c64_potxy_blocker_v691_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME C64PotXYBlockerV691Tests COMMAND arpsid_c64_potxy_blocker_v691_tests)

# Concurrency audit #1 (atomic ingress clear) + #2 (NoteOn/NoteOff drop safety).
add_executable(arpsid_ingress_noteoff_safety_v692_tests source/tests/ingress_noteoff_safety_v692_tests.cpp)
target_compile_features(arpsid_ingress_noteoff_safety_v692_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_ingress_noteoff_safety_v692_tests PRIVATE include source)
target_compile_definitions(arpsid_ingress_noteoff_safety_v692_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME IngressNoteOffSafetyV692Tests COMMAND arpsid_ingress_noteoff_safety_v692_tests)


add_executable(arpsid_c64_psid_rsid_boot_contract_v739_tests source/tests/c64_psid_rsid_boot_contract_v739_tests.cpp)
target_compile_features(arpsid_c64_psid_rsid_boot_contract_v739_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_psid_rsid_boot_contract_v739_tests PRIVATE include source)
add_test(NAME C64PsidRsidBootContractV739Tests COMMAND arpsid_c64_psid_rsid_boot_contract_v739_tests)

add_executable(arpsid_c64_psid_cia_phi2_service_v740_tests source/tests/c64_psid_cia_phi2_service_v740_tests.cpp)
target_compile_features(arpsid_c64_psid_cia_phi2_service_v740_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_psid_cia_phi2_service_v740_tests PRIVATE include source)
add_test(NAME C64PsidCiaPhi2ServiceV740Tests COMMAND arpsid_c64_psid_cia_phi2_service_v740_tests)

add_executable(arpsid_c64_cycle_exact_closure_v741_tests source/tests/c64_cycle_exact_closure_v741_tests.cpp)
target_compile_features(arpsid_c64_cycle_exact_closure_v741_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_cycle_exact_closure_v741_tests PRIVATE include source)
add_test(NAME C64CycleExactClosureV741Tests COMMAND arpsid_c64_cycle_exact_closure_v741_tests)

add_executable(arpsid_split_brain_authority_closure_v742_tests source/tests/split_brain_authority_closure_v742_tests.cpp)
target_compile_features(arpsid_split_brain_authority_closure_v742_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_split_brain_authority_closure_v742_tests PRIVATE include source)
target_compile_definitions(arpsid_split_brain_authority_closure_v742_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME SplitBrainAuthorityClosureV742Tests COMMAND arpsid_split_brain_authority_closure_v742_tests)

add_executable(arpsid_gui_telemetry_scope_closure_v743_tests source/tests/gui_telemetry_scope_closure_v743_tests.cpp)
target_compile_features(arpsid_gui_telemetry_scope_closure_v743_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_gui_telemetry_scope_closure_v743_tests PRIVATE include source)
target_compile_definitions(arpsid_gui_telemetry_scope_closure_v743_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME GuiTelemetryScopeClosureV743Tests COMMAND arpsid_gui_telemetry_scope_closure_v743_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_gui_telemetry_scope_closure_v743_tests)
endif()

add_executable(arpsid_c64_scope_telemetry_fix_closure_v744_tests source/tests/c64_scope_telemetry_fix_closure_v744_tests.cpp)
target_compile_features(arpsid_c64_scope_telemetry_fix_closure_v744_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_scope_telemetry_fix_closure_v744_tests PRIVATE include source)
target_compile_definitions(arpsid_c64_scope_telemetry_fix_closure_v744_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME C64ScopeTelemetryFixClosureV744Tests COMMAND arpsid_c64_scope_telemetry_fix_closure_v744_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_c64_scope_telemetry_fix_closure_v744_tests)
endif()

add_executable(arpsid_telemetry_runtime_coherency_v745_tests source/tests/telemetry_runtime_coherency_v745_tests.cpp)
target_compile_features(arpsid_telemetry_runtime_coherency_v745_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_telemetry_runtime_coherency_v745_tests PRIVATE include source)
target_link_libraries(arpsid_telemetry_runtime_coherency_v745_tests PRIVATE arpsid_core)
add_test(NAME TelemetryRuntimeCoherencyV745Tests COMMAND arpsid_telemetry_runtime_coherency_v745_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_telemetry_runtime_coherency_v745_tests)
endif()

add_executable(arpsid_c64_sid_player_and_ui_closure_v746_tests source/tests/c64_sid_player_and_ui_closure_v746_tests.cpp)
target_compile_features(arpsid_c64_sid_player_and_ui_closure_v746_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_c64_sid_player_and_ui_closure_v746_tests PRIVATE include source)
target_compile_definitions(arpsid_c64_sid_player_and_ui_closure_v746_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME C64SidPlayerAndUiClosureV746Tests COMMAND arpsid_c64_sid_player_and_ui_closure_v746_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_c64_sid_player_and_ui_closure_v746_tests)
endif()

add_executable(arpsid_auv2_pure_sid_d418_zoh_continuity_v736_tests source/tests/auv2_pure_sid_d418_zoh_continuity_v736_tests.cpp)
target_compile_features(arpsid_auv2_pure_sid_d418_zoh_continuity_v736_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_auv2_pure_sid_d418_zoh_continuity_v736_tests PRIVATE include source)
target_compile_definitions(arpsid_auv2_pure_sid_d418_zoh_continuity_v736_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Auv2PureSidD418ZohContinuityV736Tests COMMAND arpsid_auv2_pure_sid_d418_zoh_continuity_v736_tests)

add_executable(arpsid_audit_complete_closure_v683_tests source/tests/audit_complete_closure_v683_tests.cpp)
target_compile_features(arpsid_audit_complete_closure_v683_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_audit_complete_closure_v683_tests PRIVATE include source)
target_link_libraries(arpsid_audit_complete_closure_v683_tests PRIVATE arpsid_core)
target_compile_definitions(arpsid_audit_complete_closure_v683_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME AuditCompleteClosureV683Tests COMMAND arpsid_audit_complete_closure_v683_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_audit_complete_closure_v683_tests)
endif()

add_executable(arpsid_gui_keyboard_tuning_brand_closure_v684_tests
               source/tests/gui_keyboard_tuning_brand_closure_v684_tests.cpp)
target_compile_features(arpsid_gui_keyboard_tuning_brand_closure_v684_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_gui_keyboard_tuning_brand_closure_v684_tests PRIVATE include source)
target_link_libraries(arpsid_gui_keyboard_tuning_brand_closure_v684_tests PRIVATE arpsid_core)
target_compile_definitions(arpsid_gui_keyboard_tuning_brand_closure_v684_tests PRIVATE
    ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME GuiKeyboardTuningBrandClosureV684Tests
         COMMAND arpsid_gui_keyboard_tuning_brand_closure_v684_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite
                   arpsid_gui_keyboard_tuning_brand_closure_v684_tests)
endif()

add_executable(arpsid_openbus_vic_matrix_authentic_v960_tests
               source/tests/openbus_vic_matrix_authentic_v960_tests.cpp)
target_compile_features(arpsid_openbus_vic_matrix_authentic_v960_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_openbus_vic_matrix_authentic_v960_tests PRIVATE include source)
target_link_libraries(arpsid_openbus_vic_matrix_authentic_v960_tests PRIVATE arpsid_core)
target_compile_definitions(arpsid_openbus_vic_matrix_authentic_v960_tests PRIVATE
    ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME OpenBusVicMatrixAuthenticV960Tests
         COMMAND arpsid_openbus_vic_matrix_authentic_v960_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite
                   arpsid_openbus_vic_matrix_authentic_v960_tests)
endif()

add_executable(ArpGateOffTimingV961Tests
               source/tests/arp_gate_off_timing_v961_tests.cpp
               source/forensic_patch_bank.cpp)
target_compile_features(ArpGateOffTimingV961Tests PRIVATE cxx_std_17)
target_include_directories(ArpGateOffTimingV961Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_link_libraries(ArpGateOffTimingV961Tests PRIVATE arpsid_core)
add_test(NAME ArpGateOffTimingV961Tests COMMAND ArpGateOffTimingV961Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite ArpGateOffTimingV961Tests)
endif()

add_executable(GuiUserKitAndPopoutWiringV962Tests
               source/tests/gui_user_kit_and_popout_wiring_v962_tests.cpp)
target_compile_features(GuiUserKitAndPopoutWiringV962Tests PRIVATE cxx_std_17)
target_include_directories(GuiUserKitAndPopoutWiringV962Tests PRIVATE include source)
target_link_libraries(GuiUserKitAndPopoutWiringV962Tests PRIVATE arpsid_core)
target_compile_definitions(GuiUserKitAndPopoutWiringV962Tests PRIVATE
    ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME GuiUserKitAndPopoutWiringV962Tests COMMAND GuiUserKitAndPopoutWiringV962Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite GuiUserKitAndPopoutWiringV962Tests)
endif()

add_executable(GuiA11yTooltipCoverageV963Tests
               source/tests/gui_a11y_tooltip_coverage_v963_tests.cpp)
target_compile_features(GuiA11yTooltipCoverageV963Tests PRIVATE cxx_std_17)
target_include_directories(GuiA11yTooltipCoverageV963Tests PRIVATE include source)
target_link_libraries(GuiA11yTooltipCoverageV963Tests PRIVATE arpsid_core)
target_compile_definitions(GuiA11yTooltipCoverageV963Tests PRIVATE
    ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME GuiA11yTooltipCoverageV963Tests COMMAND GuiA11yTooltipCoverageV963Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite GuiA11yTooltipCoverageV963Tests)
endif()

add_executable(C64SidplayTelemetryV964Tests
               source/tests/c64_sidplay_telemetry_v964_tests.cpp)
target_compile_features(C64SidplayTelemetryV964Tests PRIVATE cxx_std_17)
target_include_directories(C64SidplayTelemetryV964Tests PRIVATE include source)
target_link_libraries(C64SidplayTelemetryV964Tests PRIVATE arpsid_core)
target_compile_definitions(C64SidplayTelemetryV964Tests PRIVATE
    ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME C64SidplayTelemetryV964Tests COMMAND C64SidplayTelemetryV964Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64SidplayTelemetryV964Tests)
endif()

add_executable(arpsid_remaining_audit_closure_v747_tests
               source/tests/remaining_audit_closure_v747_tests.cpp)
target_compile_features(arpsid_remaining_audit_closure_v747_tests PRIVATE cxx_std_17)
target_include_directories(arpsid_remaining_audit_closure_v747_tests PRIVATE include source)
target_compile_definitions(arpsid_remaining_audit_closure_v747_tests PRIVATE
    ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME RemainingAuditClosureV747Tests
         COMMAND arpsid_remaining_audit_closure_v747_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite
                   arpsid_remaining_audit_closure_v747_tests)
endif()
add_executable(arpsid_source_only_closure_v767_tests source/tests/source_only_closure_v767_tests.cpp)
target_compile_features(arpsid_source_only_closure_v767_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_source_only_closure_v767_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME SourceOnlyClosureV767Tests COMMAND arpsid_source_only_closure_v767_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_source_only_closure_v767_tests)
endif()

add_executable(arpsid_root_build_script_release_v768_tests source/tests/root_build_script_release_v768_tests.cpp)
target_compile_features(arpsid_root_build_script_release_v768_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_root_build_script_release_v768_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME RootBuildScriptReleaseV768Tests COMMAND arpsid_root_build_script_release_v768_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_root_build_script_release_v768_tests)
endif()

add_executable(arpsid_root_build_script_release_modes_v769_tests source/tests/root_build_script_release_modes_v769_tests.cpp)
target_compile_features(arpsid_root_build_script_release_modes_v769_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_root_build_script_release_modes_v769_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME RootBuildScriptReleaseModesV769Tests COMMAND arpsid_root_build_script_release_modes_v769_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_root_build_script_release_modes_v769_tests)
endif()

add_executable(arpsid_macos_install_status_handoff_v770_tests source/tests/macos_install_status_handoff_v770_tests.cpp)
target_compile_features(arpsid_macos_install_status_handoff_v770_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_macos_install_status_handoff_v770_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME MacosInstallStatusHandoffV770Tests COMMAND arpsid_macos_install_status_handoff_v770_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_macos_install_status_handoff_v770_tests)
endif()

add_executable(arpsid_root_build_script_auval_mode_v771_tests source/tests/root_build_script_auval_mode_v771_tests.cpp)
target_compile_features(arpsid_root_build_script_auval_mode_v771_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_root_build_script_auval_mode_v771_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME RootBuildScriptAuvalModeV771Tests COMMAND arpsid_root_build_script_auval_mode_v771_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_root_build_script_auval_mode_v771_tests)
endif()

endif()

add_executable(arpsid_root_build_script_macos_closure_v772_tests source/tests/root_build_script_macos_closure_v772_tests.cpp)
target_compile_features(arpsid_root_build_script_macos_closure_v772_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_root_build_script_macos_closure_v772_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME RootBuildScriptMacosClosureV772Tests COMMAND arpsid_root_build_script_macos_closure_v772_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_root_build_script_macos_closure_v772_tests)
endif()

add_executable(arpsid_root_build_script_macos_closure_logging_v773_tests source/tests/root_build_script_macos_closure_logging_v773_tests.cpp)
target_compile_features(arpsid_root_build_script_macos_closure_logging_v773_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_root_build_script_macos_closure_logging_v773_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME RootBuildScriptMacosClosureLoggingV773Tests COMMAND arpsid_root_build_script_macos_closure_logging_v773_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_root_build_script_macos_closure_logging_v773_tests)
endif()

add_executable(arpsid_macos_build_green_status_v774_tests source/tests/macos_build_green_status_v774_tests.cpp)
target_compile_features(arpsid_macos_build_green_status_v774_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_macos_build_green_status_v774_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME MacosBuildGreenStatusV774Tests COMMAND arpsid_macos_build_green_status_v774_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_macos_build_green_status_v774_tests)
endif()

add_executable(arpsid_root_build_script_closure_log_command_v775_tests source/tests/root_build_script_closure_log_command_v775_tests.cpp)
target_compile_features(arpsid_root_build_script_closure_log_command_v775_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_root_build_script_closure_log_command_v775_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME RootBuildScriptClosureLogCommandV775Tests COMMAND arpsid_root_build_script_closure_log_command_v775_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_root_build_script_closure_log_command_v775_tests)
endif()

add_executable(arpsid_readme_current_version_note_v776_tests source/tests/readme_current_version_note_v776_tests.cpp)
target_compile_features(arpsid_readme_current_version_note_v776_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_readme_current_version_note_v776_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME ReadmeCurrentVersionNoteV776Tests COMMAND arpsid_readme_current_version_note_v776_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_readme_current_version_note_v776_tests)
endif()

add_executable(arpsid_digi_audioqueue_weak_context_v777_tests source/tests/digi_audioqueue_weak_context_v777_tests.cpp)
target_compile_features(arpsid_digi_audioqueue_weak_context_v777_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_digi_audioqueue_weak_context_v777_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME DigiAudioQueueWeakContextV777Tests COMMAND arpsid_digi_audioqueue_weak_context_v777_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_digi_audioqueue_weak_context_v777_tests)
endif()

add_executable(arpsid_standalone_coremidi_weak_context_v778_tests source/tests/standalone_coremidi_weak_context_v778_tests.cpp)
target_compile_features(arpsid_standalone_coremidi_weak_context_v778_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_standalone_coremidi_weak_context_v778_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME StandaloneCoreMIDIWeakContextV778Tests COMMAND arpsid_standalone_coremidi_weak_context_v778_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_standalone_coremidi_weak_context_v778_tests)
endif()

add_executable(arpsid_digi_aq_backend_callback_context_v779_tests source/tests/digi_aq_backend_callback_context_v779_tests.cpp)
target_compile_features(arpsid_digi_aq_backend_callback_context_v779_tests PRIVATE cxx_std_17)
target_compile_definitions(arpsid_digi_aq_backend_callback_context_v779_tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME DigiAQBackendCallbackContextV779Tests COMMAND arpsid_digi_aq_backend_callback_context_v779_tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite arpsid_digi_aq_backend_callback_context_v779_tests)
endif()

add_executable(PsidAsyncLoadKernelLifetimeV780Tests source/tests/psid_async_load_kernel_lifetime_v780_tests.cpp)
target_compile_definitions(PsidAsyncLoadKernelLifetimeV780Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME PsidAsyncLoadKernelLifetimeV780Tests COMMAND PsidAsyncLoadKernelLifetimeV780Tests)

add_executable(BankDocumentWorkerPureHelpersV781Tests source/tests/bank_document_worker_pure_helpers_v781_tests.cpp)
target_compile_definitions(BankDocumentWorkerPureHelpersV781Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME BankDocumentWorkerPureHelpersV781Tests COMMAND BankDocumentWorkerPureHelpersV781Tests)

add_executable(BankWorkerURLSnapshotV782Tests source/tests/bank_worker_url_snapshot_v782_tests.cpp)
target_compile_definitions(BankWorkerURLSnapshotV782Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME BankWorkerURLSnapshotV782Tests COMMAND BankWorkerURLSnapshotV782Tests)

add_executable(StandalonePresetPanelSnapshotV783Tests source/tests/standalone_preset_panel_snapshot_v783_tests.cpp)
target_compile_definitions(StandalonePresetPanelSnapshotV783Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME StandalonePresetPanelSnapshotV783Tests COMMAND StandalonePresetPanelSnapshotV783Tests)

add_executable(StandaloneCoreMIDIMainQueueWeakContinuationsV784Tests source/tests/standalone_coremidi_mainqueue_weak_continuations_v784_tests.cpp)
target_compile_features(StandaloneCoreMIDIMainQueueWeakContinuationsV784Tests PRIVATE cxx_std_17)
target_compile_definitions(StandaloneCoreMIDIMainQueueWeakContinuationsV784Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME StandaloneCoreMIDIMainQueueWeakContinuationsV784Tests COMMAND StandaloneCoreMIDIMainQueueWeakContinuationsV784Tests)

add_executable(BankPanelCompletionURLSnapshotV785Tests source/tests/bank_panel_completion_url_snapshot_v785_tests.cpp)
target_compile_features(BankPanelCompletionURLSnapshotV785Tests PRIVATE cxx_std_17)
target_compile_definitions(BankPanelCompletionURLSnapshotV785Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME BankPanelCompletionURLSnapshotV785Tests COMMAND BankPanelCompletionURLSnapshotV785Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite BankPanelCompletionURLSnapshotV785Tests)
endif()
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite StandaloneCoreMIDIMainQueueWeakContinuationsV784Tests)
endif()

add_executable(C64SidPanelURLSnapshotV786Tests source/tests/c64_sid_panel_url_snapshot_v786_tests.cpp)
target_compile_features(C64SidPanelURLSnapshotV786Tests PRIVATE cxx_std_17)
target_compile_definitions(C64SidPanelURLSnapshotV786Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME C64SidPanelURLSnapshotV786Tests COMMAND C64SidPanelURLSnapshotV786Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64SidPanelURLSnapshotV786Tests)
endif()

add_executable(DigiRecordAutostopWeakContinuationV787Tests source/tests/digi_record_autostop_weak_continuation_v787_tests.cpp)
target_compile_features(DigiRecordAutostopWeakContinuationV787Tests PRIVATE cxx_std_17)
target_compile_definitions(DigiRecordAutostopWeakContinuationV787Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME DigiRecordAutostopWeakContinuationV787Tests COMMAND DigiRecordAutostopWeakContinuationV787Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite DigiRecordAutostopWeakContinuationV787Tests)
endif()

add_executable(CVDisplayLinkMainQueueWeakContinuationV788Tests source/tests/cvdisplaylink_mainqueue_weak_continuation_v788_tests.cpp)
target_compile_features(CVDisplayLinkMainQueueWeakContinuationV788Tests PRIVATE cxx_std_17)
target_compile_definitions(CVDisplayLinkMainQueueWeakContinuationV788Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME CVDisplayLinkMainQueueWeakContinuationV788Tests COMMAND CVDisplayLinkMainQueueWeakContinuationV788Tests)

add_executable(BankStatusMainQueueWeakContinuationV789Tests source/tests/bank_status_mainqueue_weak_continuation_v789_tests.cpp)
target_compile_definitions(BankStatusMainQueueWeakContinuationV789Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME BankStatusMainQueueWeakContinuationV789Tests COMMAND BankStatusMainQueueWeakContinuationV789Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite CVDisplayLinkMainQueueWeakContinuationV788Tests)
endif()

add_executable(StandaloneMenuMIDINotifyWeakContinuationsV790Tests source/tests/standalone_menu_midi_notify_weak_continuations_v790_tests.cpp)
target_compile_features(StandaloneMenuMIDINotifyWeakContinuationsV790Tests PRIVATE cxx_std_17)
target_compile_definitions(StandaloneMenuMIDINotifyWeakContinuationsV790Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME StandaloneMenuMIDINotifyWeakContinuationsV790Tests COMMAND StandaloneMenuMIDINotifyWeakContinuationsV790Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite StandaloneMenuMIDINotifyWeakContinuationsV790Tests)
endif()

add_executable(AU3RequestViewControllerWeakDispatchV791Tests source/tests/au3_request_view_controller_weak_dispatch_v791_tests.cpp)
target_compile_features(AU3RequestViewControllerWeakDispatchV791Tests PRIVATE cxx_std_17)
target_compile_definitions(AU3RequestViewControllerWeakDispatchV791Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME AU3RequestViewControllerWeakDispatchV791Tests COMMAND AU3RequestViewControllerWeakDispatchV791Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite AU3RequestViewControllerWeakDispatchV791Tests)
endif()

add_executable(EmbeddedVSTPresentationWeakContinuationV792Tests source/tests/embedded_vst_presentation_weak_continuation_v792_tests.cpp)
target_compile_features(EmbeddedVSTPresentationWeakContinuationV792Tests PRIVATE cxx_std_17)
target_compile_definitions(EmbeddedVSTPresentationWeakContinuationV792Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME EmbeddedVSTPresentationWeakContinuationV792Tests COMMAND EmbeddedVSTPresentationWeakContinuationV792Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite EmbeddedVSTPresentationWeakContinuationV792Tests)
endif()

add_executable(EmbeddedVSTFocusWeakContinuationV793Tests source/tests/embedded_vst_focus_weak_continuation_v793_tests.cpp)
target_compile_features(EmbeddedVSTFocusWeakContinuationV793Tests PRIVATE cxx_std_17)
target_compile_definitions(EmbeddedVSTFocusWeakContinuationV793Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME EmbeddedVSTFocusWeakContinuationV793Tests COMMAND EmbeddedVSTFocusWeakContinuationV793Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite EmbeddedVSTFocusWeakContinuationV793Tests)
endif()


add_executable(AUv2ViewFactoryWeakAUContinuationV795Tests source/tests/auv2_view_factory_weak_au_continuation_v795_tests.cpp)
target_compile_features(AUv2ViewFactoryWeakAUContinuationV795Tests PRIVATE cxx_std_17)
target_compile_definitions(AUv2ViewFactoryWeakAUContinuationV795Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME AUv2ViewFactoryWeakAUContinuationV795Tests COMMAND AUv2ViewFactoryWeakAUContinuationV795Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite AUv2ViewFactoryWeakAUContinuationV795Tests)
endif()

add_executable(DrumUserKitSaveWeakContinuationV796Tests source/tests/drum_user_kit_save_weak_continuation_v796_tests.cpp)
target_compile_features(DrumUserKitSaveWeakContinuationV796Tests PRIVATE cxx_std_17)
target_compile_definitions(DrumUserKitSaveWeakContinuationV796Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME DrumUserKitSaveWeakContinuationV796Tests COMMAND DrumUserKitSaveWeakContinuationV796Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite DrumUserKitSaveWeakContinuationV796Tests)
endif()

add_executable(StandaloneStartupWindowWeakContinuationsV794Tests source/tests/standalone_startup_window_weak_continuations_v794_tests.cpp)
target_compile_features(StandaloneStartupWindowWeakContinuationsV794Tests PRIVATE cxx_std_17)
target_compile_definitions(StandaloneStartupWindowWeakContinuationsV794Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME StandaloneStartupWindowWeakContinuationsV794Tests COMMAND StandaloneStartupWindowWeakContinuationsV794Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite StandaloneStartupWindowWeakContinuationsV794Tests)
endif()

add_executable(AbsoluteP0ClosureV797Tests source/tests/absolute_p0_closure_v797_tests.cpp)
target_compile_features(AbsoluteP0ClosureV797Tests PRIVATE cxx_std_17)
target_compile_definitions(AbsoluteP0ClosureV797Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME AbsoluteP0ClosureV797Tests COMMAND AbsoluteP0ClosureV797Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite AbsoluteP0ClosureV797Tests)
endif()

add_executable(AbsoluteP1ClosureV798Tests source/tests/absolute_p1_closure_v798_tests.cpp)
target_compile_features(AbsoluteP1ClosureV798Tests PRIVATE cxx_std_17)
target_compile_definitions(AbsoluteP1ClosureV798Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME AbsoluteP1ClosureV798Tests COMMAND AbsoluteP1ClosureV798Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite AbsoluteP1ClosureV798Tests)
endif()

add_executable(AbsoluteP2ClosureV799Tests source/tests/absolute_p2_closure_v799_tests.cpp)
target_compile_features(AbsoluteP2ClosureV799Tests PRIVATE cxx_std_17)
target_compile_definitions(AbsoluteP2ClosureV799Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME AbsoluteP2ClosureV799Tests COMMAND AbsoluteP2ClosureV799Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite AbsoluteP2ClosureV799Tests)
endif()

add_executable(StaleVersionGuardSweepV800Tests source/tests/stale_version_guard_sweep_v800_tests.cpp)
target_compile_features(StaleVersionGuardSweepV800Tests PRIVATE cxx_std_17)
target_compile_definitions(StaleVersionGuardSweepV800Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME StaleVersionGuardSweepV800Tests COMMAND StaleVersionGuardSweepV800Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite StaleVersionGuardSweepV800Tests)
endif()

add_executable(FinalSourceClosureV801Tests source/tests/final_source_closure_v801_tests.cpp)
target_compile_features(FinalSourceClosureV801Tests PRIVATE cxx_std_17)
target_compile_definitions(FinalSourceClosureV801Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME FinalSourceClosureV801Tests COMMAND FinalSourceClosureV801Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite FinalSourceClosureV801Tests)
endif()

add_executable(C64SidPlayerFiveFlavorPolicyV802Tests source/tests/c64_sid_player_five_flavor_policy_v802_tests.cpp)
target_compile_features(C64SidPlayerFiveFlavorPolicyV802Tests PRIVATE cxx_std_17)
target_compile_definitions(C64SidPlayerFiveFlavorPolicyV802Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME C64SidPlayerFiveFlavorPolicyV802Tests COMMAND C64SidPlayerFiveFlavorPolicyV802Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64SidPlayerFiveFlavorPolicyV802Tests)
endif()

add_executable(Sid808RestorePreloadDrainV803Tests source/tests/sid808_restore_preload_drain_v803_tests.cpp)
target_compile_features(Sid808RestorePreloadDrainV803Tests PRIVATE cxx_std_17)
target_compile_definitions(Sid808RestorePreloadDrainV803Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME Sid808RestorePreloadDrainV803Tests COMMAND Sid808RestorePreloadDrainV803Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite Sid808RestorePreloadDrainV803Tests)
endif()

add_executable(RenderApplyRTOnlyHelpersV804Tests source/tests/render_apply_rt_only_helpers_v804_tests.cpp)
target_compile_features(RenderApplyRTOnlyHelpersV804Tests PRIVATE cxx_std_17)
target_compile_definitions(RenderApplyRTOnlyHelpersV804Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME RenderApplyRTOnlyHelpersV804Tests COMMAND RenderApplyRTOnlyHelpersV804Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite RenderApplyRTOnlyHelpersV804Tests)
endif()

add_executable(PostSwapAppliedRootGuardV805Tests source/tests/post_swap_applied_root_guard_v805_tests.cpp)
target_compile_features(PostSwapAppliedRootGuardV805Tests PRIVATE cxx_std_17)
target_compile_definitions(PostSwapAppliedRootGuardV805Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME PostSwapAppliedRootGuardV805Tests COMMAND PostSwapAppliedRootGuardV805Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite PostSwapAppliedRootGuardV805Tests)
endif()

add_executable(RenderBridgeSnapshotStackGuardV806Tests source/tests/render_bridge_snapshot_stack_guard_v806_tests.cpp)
target_compile_features(RenderBridgeSnapshotStackGuardV806Tests PRIVATE cxx_std_17)
target_compile_definitions(RenderBridgeSnapshotStackGuardV806Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME RenderBridgeSnapshotStackGuardV806Tests COMMAND RenderBridgeSnapshotStackGuardV806Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite RenderBridgeSnapshotStackGuardV806Tests)
endif()

add_executable(PsidPlayZeroContinuousGuardV807Tests source/tests/psid_playzero_continuous_guard_v807_tests.cpp)
target_compile_features(PsidPlayZeroContinuousGuardV807Tests PRIVATE cxx_std_17)
target_compile_definitions(PsidPlayZeroContinuousGuardV807Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME PsidPlayZeroContinuousGuardV807Tests COMMAND PsidPlayZeroContinuousGuardV807Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite PsidPlayZeroContinuousGuardV807Tests)
endif()

add_executable(StrictRsidFallbackTruthGuardV808Tests source/tests/strict_rsid_fallback_truth_guard_v808_tests.cpp)
target_compile_features(StrictRsidFallbackTruthGuardV808Tests PRIVATE cxx_std_17)
target_compile_definitions(StrictRsidFallbackTruthGuardV808Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME StrictRsidFallbackTruthGuardV808Tests COMMAND StrictRsidFallbackTruthGuardV808Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite StrictRsidFallbackTruthGuardV808Tests)
endif()

add_executable(SidD41dSystemByteRoutingV809Tests source/tests/sid_d41d_system_byte_routing_v809_tests.cpp)
target_include_directories(SidD41dSystemByteRoutingV809Tests PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/source)
target_compile_features(SidD41dSystemByteRoutingV809Tests PRIVATE cxx_std_17)
add_test(NAME SidD41dSystemByteRoutingV809Tests COMMAND SidD41dSystemByteRoutingV809Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SidD41dSystemByteRoutingV809Tests)
endif()

add_executable(PsidObservedRiskParityV810Tests source/tests/psid_observed_risk_parity_v810_tests.cpp)
target_compile_features(PsidObservedRiskParityV810Tests PRIVATE cxx_std_17)
target_compile_definitions(PsidObservedRiskParityV810Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME PsidObservedRiskParityV810Tests COMMAND PsidObservedRiskParityV810Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite PsidObservedRiskParityV810Tests)
endif()

add_executable(C64LowlevelEdgeContractV811Tests source/tests/c64_lowlevel_edge_contract_v811_tests.cpp)
target_compile_features(C64LowlevelEdgeContractV811Tests PRIVATE cxx_std_17)
target_compile_definitions(C64LowlevelEdgeContractV811Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME C64LowlevelEdgeContractV811Tests COMMAND C64LowlevelEdgeContractV811Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64LowlevelEdgeContractV811Tests)
endif()

add_executable(C64PsidSentinelAndDocsV812Tests source/tests/c64_psid_sentinel_and_docs_v812_tests.cpp)
target_compile_features(C64PsidSentinelAndDocsV812Tests PRIVATE cxx_std_17)
target_compile_definitions(C64PsidSentinelAndDocsV812Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME C64PsidSentinelAndDocsV812Tests COMMAND C64PsidSentinelAndDocsV812Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64PsidSentinelAndDocsV812Tests)
endif()

add_executable(C64RenderMutationJournalV813Tests source/tests/c64_render_mutation_journal_v813_tests.cpp)
target_compile_features(C64RenderMutationJournalV813Tests PRIVATE cxx_std_17)
target_include_directories(C64RenderMutationJournalV813Tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME C64RenderMutationJournalV813Tests COMMAND C64RenderMutationJournalV813Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64RenderMutationJournalV813Tests)
endif()

add_executable(StateApplyOwnershipSplitV814Tests source/tests/state_apply_ownership_split_v814_tests.cpp)
target_compile_features(StateApplyOwnershipSplitV814Tests PRIVATE cxx_std_17)
target_compile_definitions(StateApplyOwnershipSplitV814Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME StateApplyOwnershipSplitV814Tests COMMAND StateApplyOwnershipSplitV814Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite StateApplyOwnershipSplitV814Tests)
endif()

add_executable(SidRuntimeRestoreDualEngineV815Tests source/tests/sid_runtime_restore_dual_engine_v815_tests.cpp)
target_compile_features(SidRuntimeRestoreDualEngineV815Tests PRIVATE cxx_std_17)
target_compile_definitions(SidRuntimeRestoreDualEngineV815Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME SidRuntimeRestoreDualEngineV815Tests COMMAND SidRuntimeRestoreDualEngineV815Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SidRuntimeRestoreDualEngineV815Tests)
endif()

add_executable(Sid808RestoreFlavorIntegrationV816Tests source/tests/sid808_restore_flavor_integration_v816_tests.cpp)
target_compile_features(Sid808RestoreFlavorIntegrationV816Tests PRIVATE cxx_std_17)
target_compile_definitions(Sid808RestoreFlavorIntegrationV816Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME Sid808RestoreFlavorIntegrationV816Tests COMMAND Sid808RestoreFlavorIntegrationV816Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite Sid808RestoreFlavorIntegrationV816Tests)
endif()

add_executable(AuEventTranslatorMidi2UmpV861Tests source/tests/au_event_translator_midi2_ump_v861_tests.cpp)
target_compile_features(AuEventTranslatorMidi2UmpV861Tests PRIVATE cxx_std_17)
target_compile_definitions(AuEventTranslatorMidi2UmpV861Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME AuEventTranslatorMidi2UmpV861Tests COMMAND AuEventTranslatorMidi2UmpV861Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite AuEventTranslatorMidi2UmpV861Tests)
endif()

add_executable(Sid808StrictAuSlotPolicyV861Tests source/tests/sid808_strict_au_slot_policy_v861_tests.cpp)
target_compile_features(Sid808StrictAuSlotPolicyV861Tests PRIVATE cxx_std_17)
target_compile_definitions(Sid808StrictAuSlotPolicyV861Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME Sid808StrictAuSlotPolicyV861Tests COMMAND Sid808StrictAuSlotPolicyV861Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite Sid808StrictAuSlotPolicyV861Tests)
endif()

add_executable(Sid808GMProjectionV873Tests source/tests/sid808_gm_projection_v873_tests.cpp)
target_compile_features(Sid808GMProjectionV873Tests PRIVATE cxx_std_17)
target_link_libraries(Sid808GMProjectionV873Tests PRIVATE arpsid_core)
add_test(NAME Sid808GMProjectionV873Tests COMMAND Sid808GMProjectionV873Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite Sid808GMProjectionV873Tests)
endif()

add_executable(Sid808GMProfilesV873Tests source/tests/sid808_gm_profiles_v873_tests.cpp)
target_compile_features(Sid808GMProfilesV873Tests PRIVATE cxx_std_17)
target_link_libraries(Sid808GMProfilesV873Tests PRIVATE arpsid_core)
add_test(NAME Sid808GMProfilesV873Tests COMMAND Sid808GMProfilesV873Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite Sid808GMProfilesV873Tests)
endif()

add_executable(Sid808GMFamiliesV873Tests source/tests/sid808_gm_families_v873_tests.cpp)
target_compile_features(Sid808GMFamiliesV873Tests PRIVATE cxx_std_17)
target_link_libraries(Sid808GMFamiliesV873Tests PRIVATE arpsid_core)
add_test(NAME Sid808GMFamiliesV873Tests COMMAND Sid808GMFamiliesV873Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite Sid808GMFamiliesV873Tests)
endif()

add_executable(PsidMultiSidVersionSemanticsV873Tests source/tests/psid_multisid_version_semantics_v873_tests.cpp)
target_compile_features(PsidMultiSidVersionSemanticsV873Tests PRIVATE cxx_std_17)
target_link_libraries(PsidMultiSidVersionSemanticsV873Tests PRIVATE arpsid_core)
add_test(NAME PsidMultiSidVersionSemanticsV873Tests COMMAND PsidMultiSidVersionSemanticsV873Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite PsidMultiSidVersionSemanticsV873Tests)
endif()

add_executable(PsidInitIomapV873Tests source/tests/psid_init_iomap_v873_tests.cpp)
target_compile_features(PsidInitIomapV873Tests PRIVATE cxx_std_17)
target_link_libraries(PsidInitIomapV873Tests PRIVATE arpsid_core)
add_test(NAME PsidInitIomapV873Tests COMMAND PsidInitIomapV873Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite PsidInitIomapV873Tests)
endif()

add_executable(PsidBootstrapRelocationV873Tests source/tests/psid_bootstrap_relocation_v873_tests.cpp)
target_compile_features(PsidBootstrapRelocationV873Tests PRIVATE cxx_std_17)
target_link_libraries(PsidBootstrapRelocationV873Tests PRIVATE arpsid_core)
add_test(NAME PsidBootstrapRelocationV873Tests COMMAND PsidBootstrapRelocationV873Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite PsidBootstrapRelocationV873Tests)
endif()

add_executable(PsidContractValidationV873Tests source/tests/psid_contract_validation_v873_tests.cpp)
target_compile_features(PsidContractValidationV873Tests PRIVATE cxx_std_17)
target_link_libraries(PsidContractValidationV873Tests PRIVATE arpsid_core)
add_test(NAME PsidContractValidationV873Tests COMMAND PsidContractValidationV873Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite PsidContractValidationV873Tests)
endif()

add_executable(PsidLoadFailureTaxonomyV873Tests source/tests/psid_load_failure_taxonomy_v873_tests.cpp)
target_compile_features(PsidLoadFailureTaxonomyV873Tests PRIVATE cxx_std_17)
target_link_libraries(PsidLoadFailureTaxonomyV873Tests PRIVATE arpsid_core)
add_test(NAME PsidLoadFailureTaxonomyV873Tests COMMAND PsidLoadFailureTaxonomyV873Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite PsidLoadFailureTaxonomyV873Tests)
endif()

add_executable(C64ExtraSidDe00RoutingV874Tests source/tests/c64_extra_sid_de00_routing_v874_tests.cpp)
target_compile_features(C64ExtraSidDe00RoutingV874Tests PRIVATE cxx_std_17)
target_link_libraries(C64ExtraSidDe00RoutingV874Tests PRIVATE arpsid_core)
add_test(NAME C64ExtraSidDe00RoutingV874Tests COMMAND C64ExtraSidDe00RoutingV874Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64ExtraSidDe00RoutingV874Tests)
endif()

add_executable(SidProjectionAppliedWriteObserverV874Tests source/tests/sid_projection_applied_write_observer_v874_tests.cpp)
target_compile_features(SidProjectionAppliedWriteObserverV874Tests PRIVATE cxx_std_17)
target_link_libraries(SidProjectionAppliedWriteObserverV874Tests PRIVATE arpsid_core)
add_test(NAME SidProjectionAppliedWriteObserverV874Tests COMMAND SidProjectionAppliedWriteObserverV874Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SidProjectionAppliedWriteObserverV874Tests)
endif()


add_executable(C64ProjectionQueueClearV875Tests source/tests/c64_projection_queue_clear_v875_tests.cpp)
add_executable(C64ProjectionMirrorQueueSelectiveV877Tests source/tests/c64_projection_mirror_queue_selective_v877_tests.cpp)
target_compile_features(C64ProjectionMirrorQueueSelectiveV877Tests PRIVATE cxx_std_17)
target_link_libraries(C64ProjectionMirrorQueueSelectiveV877Tests PRIVATE arpsid_core)
add_test(NAME C64ProjectionMirrorQueueSelectiveV877Tests COMMAND C64ProjectionMirrorQueueSelectiveV877Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64ProjectionMirrorQueueSelectiveV877Tests)
endif()
target_compile_features(C64ProjectionQueueClearV875Tests PRIVATE cxx_std_17)
target_link_libraries(C64ProjectionQueueClearV875Tests PRIVATE arpsid_core)
add_test(NAME C64ProjectionQueueClearV875Tests COMMAND C64ProjectionQueueClearV875Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64ProjectionQueueClearV875Tests)
endif()

add_executable(C64ProjectionMirrorAuthorityV876SourceTests source/tests/c64_projection_mirror_authority_v876_source_tests.cpp)
target_compile_features(C64ProjectionMirrorAuthorityV876SourceTests PRIVATE cxx_std_17)
add_test(NAME C64ProjectionMirrorAuthorityV876SourceTests COMMAND C64ProjectionMirrorAuthorityV876SourceTests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64ProjectionMirrorAuthorityV876SourceTests)
endif()

add_executable(C64ProjectionMirrorAuthorityV878SourceTests source/tests/c64_projection_mirror_authority_v878_source_tests.cpp)
target_compile_features(C64ProjectionMirrorAuthorityV878SourceTests PRIVATE cxx_std_17)
add_test(NAME C64ProjectionMirrorAuthorityV878SourceTests COMMAND C64ProjectionMirrorAuthorityV878SourceTests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64ProjectionMirrorAuthorityV878SourceTests)
endif()

add_executable(RealtimeSourceLintV817Tests source/tests/realtime_source_lint_v817_tests.cpp)
target_compile_features(RealtimeSourceLintV817Tests PRIVATE cxx_std_17)
target_compile_definitions(RealtimeSourceLintV817Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME RealtimeSourceLintV817Tests COMMAND RealtimeSourceLintV817Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite RealtimeSourceLintV817Tests)
endif()

add_executable(ReleaseCleanupClosureV818Tests source/tests/release_cleanup_closure_v818_tests.cpp)
target_compile_features(ReleaseCleanupClosureV818Tests PRIVATE cxx_std_17)
target_compile_definitions(ReleaseCleanupClosureV818Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME ReleaseCleanupClosureV818Tests COMMAND ReleaseCleanupClosureV818Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite ReleaseCleanupClosureV818Tests)
endif()

add_executable(ReleaseDeadFileClosureV819Tests source/tests/release_dead_file_closure_v819_tests.cpp)
target_compile_features(ReleaseDeadFileClosureV819Tests PRIVATE cxx_std_17)
target_compile_definitions(ReleaseDeadFileClosureV819Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME ReleaseDeadFileClosureV819Tests COMMAND ReleaseDeadFileClosureV819Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite ReleaseDeadFileClosureV819Tests)
endif()

add_executable(C64ProjectionMirrorQueueV879Tests source/tests/c64_projection_mirror_queue_v879_tests.cpp)
target_compile_features(C64ProjectionMirrorQueueV879Tests PRIVATE cxx_std_17)
target_include_directories(C64ProjectionMirrorQueueV879Tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME C64ProjectionMirrorQueueV879Tests COMMAND C64ProjectionMirrorQueueV879Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64ProjectionMirrorQueueV879Tests)
endif()

add_executable(C64ProjectionMirrorAuthorityV880SourceTests source/tests/c64_projection_mirror_authority_v880_source_tests.cpp)
target_compile_features(C64ProjectionMirrorAuthorityV880SourceTests PRIVATE cxx_std_17)
add_test(NAME C64ProjectionMirrorAuthorityV880SourceTests COMMAND C64ProjectionMirrorAuthorityV880SourceTests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64ProjectionMirrorAuthorityV880SourceTests)
endif()

add_executable(C64ProjectionMirrorQueueOrderV881Tests source/tests/c64_projection_mirror_queue_order_v881_tests.cpp)
target_compile_features(C64ProjectionMirrorQueueOrderV881Tests PRIVATE cxx_std_17)
target_include_directories(C64ProjectionMirrorQueueOrderV881Tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME C64ProjectionMirrorQueueOrderV881Tests COMMAND C64ProjectionMirrorQueueOrderV881Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64ProjectionMirrorQueueOrderV881Tests)
endif()

add_executable(C64ProjectionMirrorAuthorityV882SourceTests source/tests/c64_projection_mirror_authority_v882_source_tests.cpp)
target_compile_features(C64ProjectionMirrorAuthorityV882SourceTests PRIVATE cxx_std_17)
add_test(NAME C64ProjectionMirrorAuthorityV882SourceTests COMMAND C64ProjectionMirrorAuthorityV882SourceTests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64ProjectionMirrorAuthorityV882SourceTests)
endif()

add_executable(C64ProjectionMirrorQueueCompactionV883Tests source/tests/c64_projection_mirror_queue_compaction_v883_tests.cpp)
target_compile_features(C64ProjectionMirrorQueueCompactionV883Tests PRIVATE cxx_std_17)
target_include_directories(C64ProjectionMirrorQueueCompactionV883Tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME C64ProjectionMirrorQueueCompactionV883Tests COMMAND C64ProjectionMirrorQueueCompactionV883Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64ProjectionMirrorQueueCompactionV883Tests)
endif()

add_executable(C64ProjectionMirrorAuthorityV884SourceTests source/tests/c64_projection_mirror_authority_v884_source_tests.cpp)
target_compile_features(C64ProjectionMirrorAuthorityV884SourceTests PRIVATE cxx_std_17)
add_test(NAME C64ProjectionMirrorAuthorityV884SourceTests COMMAND C64ProjectionMirrorAuthorityV884SourceTests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64ProjectionMirrorAuthorityV884SourceTests)
endif()

add_executable(C64ProjectionMirrorBlockLocalV885Tests source/tests/c64_projection_mirror_block_local_v885_tests.cpp)
target_compile_features(C64ProjectionMirrorBlockLocalV885Tests PRIVATE cxx_std_17)
target_link_libraries(C64ProjectionMirrorBlockLocalV885Tests PRIVATE arpsid_core)
add_test(NAME C64ProjectionMirrorBlockLocalV885Tests COMMAND C64ProjectionMirrorBlockLocalV885Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64ProjectionMirrorBlockLocalV885Tests)
endif()

add_executable(C64ProjectionMirrorAuthorityV885SourceTests source/tests/c64_projection_mirror_authority_v885_source_tests.cpp)
target_compile_features(C64ProjectionMirrorAuthorityV885SourceTests PRIVATE cxx_std_17)
add_test(NAME C64ProjectionMirrorAuthorityV885SourceTests COMMAND C64ProjectionMirrorAuthorityV885SourceTests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64ProjectionMirrorAuthorityV885SourceTests)
endif()

add_executable(C64SidplayAudioClockV886SourceTests source/tests/c64_sidplay_audio_clock_v886_source_tests.cpp)
target_compile_features(C64SidplayAudioClockV886SourceTests PRIVATE cxx_std_17)
add_test(NAME C64SidplayAudioClockV886SourceTests COMMAND C64SidplayAudioClockV886SourceTests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64SidplayAudioClockV886SourceTests)
endif()

add_executable(SidRuntimePhysicalClockLawV886SourceTests source/tests/sid_runtime_physical_clock_law_v886_source_tests.cpp)
target_compile_features(SidRuntimePhysicalClockLawV886SourceTests PRIVATE cxx_std_17)
add_test(NAME SidRuntimePhysicalClockLawV886SourceTests COMMAND SidRuntimePhysicalClockLawV886SourceTests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SidRuntimePhysicalClockLawV886SourceTests)
endif()

add_executable(SidRuntimePhysicalClockLawV887Tests source/tests/sid_runtime_physical_clock_law_v887_tests.cpp)
target_compile_features(SidRuntimePhysicalClockLawV887Tests PRIVATE cxx_std_17)
target_link_libraries(SidRuntimePhysicalClockLawV887Tests PRIVATE arpsid_core)
add_test(NAME SidRuntimePhysicalClockLawV887Tests COMMAND SidRuntimePhysicalClockLawV887Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SidRuntimePhysicalClockLawV887Tests)
endif()

add_executable(SidRuntimeMixedUnresolvedClockLawV888Tests source/tests/sid_runtime_mixed_unresolved_clock_law_v888_tests.cpp)
target_compile_features(SidRuntimeMixedUnresolvedClockLawV888Tests PRIVATE cxx_std_17)
target_link_libraries(SidRuntimeMixedUnresolvedClockLawV888Tests PRIVATE arpsid_core)
add_test(NAME SidRuntimeMixedUnresolvedClockLawV888Tests COMMAND SidRuntimeMixedUnresolvedClockLawV888Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SidRuntimeMixedUnresolvedClockLawV888Tests)
endif()

add_executable(SidRuntimeUnresolvedSample0OrderV889Tests source/tests/sid_runtime_unresolved_sample0_order_v889_tests.cpp)
target_compile_features(SidRuntimeUnresolvedSample0OrderV889Tests PRIVATE cxx_std_17)
target_link_libraries(SidRuntimeUnresolvedSample0OrderV889Tests PRIVATE arpsid_core)
add_test(NAME SidRuntimeUnresolvedSample0OrderV889Tests COMMAND SidRuntimeUnresolvedSample0OrderV889Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SidRuntimeUnresolvedSample0OrderV889Tests)
endif()

add_executable(SidRuntimeUnresolvedSample0SampleOnlyV890Tests source/tests/sid_runtime_unresolved_sample0_sampleonly_v890_tests.cpp)
target_compile_features(SidRuntimeUnresolvedSample0SampleOnlyV890Tests PRIVATE cxx_std_17)
target_link_libraries(SidRuntimeUnresolvedSample0SampleOnlyV890Tests PRIVATE arpsid_core)
add_test(NAME SidRuntimeUnresolvedSample0SampleOnlyV890Tests COMMAND SidRuntimeUnresolvedSample0SampleOnlyV890Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SidRuntimeUnresolvedSample0SampleOnlyV890Tests)
endif()


add_executable(SidRuntimeSampleOnlySubphaseV891Tests source/tests/sid_runtime_sampleonly_subphase_v891_tests.cpp)
target_compile_features(SidRuntimeSampleOnlySubphaseV891Tests PRIVATE cxx_std_17)
target_link_libraries(SidRuntimeSampleOnlySubphaseV891Tests PRIVATE arpsid_core)
add_test(NAME SidRuntimeSampleOnlySubphaseV891Tests COMMAND SidRuntimeSampleOnlySubphaseV891Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SidRuntimeSampleOnlySubphaseV891Tests)
endif()

add_executable(SidRuntimeSameSamplePolicyV891Tests source/tests/sid_runtime_same_sample_policy_v891_tests.cpp)
target_compile_features(SidRuntimeSameSamplePolicyV891Tests PRIVATE cxx_std_17)
target_link_libraries(SidRuntimeSameSamplePolicyV891Tests PRIVATE arpsid_core)
add_test(NAME SidRuntimeSameSamplePolicyV891Tests COMMAND SidRuntimeSameSamplePolicyV891Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SidRuntimeSameSamplePolicyV891Tests)
endif()

add_executable(PsidRsidNormalizationV891Tests source/tests/psid_rsid_normalization_v891_tests.cpp)
target_compile_features(PsidRsidNormalizationV891Tests PRIVATE cxx_std_17)
target_link_libraries(PsidRsidNormalizationV891Tests PRIVATE arpsid_core)
add_test(NAME PsidRsidNormalizationV891Tests COMMAND PsidRsidNormalizationV891Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite PsidRsidNormalizationV891Tests)
endif()

add_executable(PsidRelocationLoadFailureV891Tests source/tests/psid_relocation_load_failure_v891_tests.cpp)
target_compile_features(PsidRelocationLoadFailureV891Tests PRIVATE cxx_std_17)
target_link_libraries(PsidRelocationLoadFailureV891Tests PRIVATE arpsid_core)
add_test(NAME PsidRelocationLoadFailureV891Tests COMMAND PsidRelocationLoadFailureV891Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite PsidRelocationLoadFailureV891Tests)
endif()

add_executable(SidRuntimeSampleOnlyPriorityV892Tests source/tests/sid_runtime_sampleonly_priority_v892_tests.cpp)
target_compile_features(SidRuntimeSampleOnlyPriorityV892Tests PRIVATE cxx_std_17)
target_link_libraries(SidRuntimeSampleOnlyPriorityV892Tests PRIVATE arpsid_core)
add_test(NAME SidRuntimeSampleOnlyPriorityV892Tests COMMAND SidRuntimeSampleOnlyPriorityV892Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SidRuntimeSampleOnlyPriorityV892Tests)
endif()

add_executable(C64SidBitExactV893Tests source/tests/c64_sid_bitexact_v893_tests.cpp)
target_compile_features(C64SidBitExactV893Tests PRIVATE cxx_std_17)
target_link_libraries(C64SidBitExactV893Tests PRIVATE arpsid_core)
add_test(NAME C64SidBitExactV893Tests COMMAND C64SidBitExactV893Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64SidBitExactV893Tests)
endif()

add_executable(SidWriteQueueRebaseV894Tests source/tests/sid_write_queue_rebase_v894_tests.cpp)
target_compile_features(SidWriteQueueRebaseV894Tests PRIVATE cxx_std_17)
target_link_libraries(SidWriteQueueRebaseV894Tests PRIVATE arpsid_core)
add_test(NAME SidWriteQueueRebaseV894Tests COMMAND SidWriteQueueRebaseV894Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SidWriteQueueRebaseV894Tests)
endif()

add_executable(SidPulseComparatorParityV895Tests source/tests/sid_pulse_comparator_parity_v895_tests.cpp)
target_compile_features(SidPulseComparatorParityV895Tests PRIVATE cxx_std_17)
target_link_libraries(SidPulseComparatorParityV895Tests PRIVATE arpsid_core)
add_test(NAME SidPulseComparatorParityV895Tests COMMAND SidPulseComparatorParityV895Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SidPulseComparatorParityV895Tests)
endif()

add_executable(KernelE2EAudibilityV898Tests
               source/tests/kernel_e2e_audibility_v898_tests.cpp
               source/forensic_patch_bank.cpp)
target_compile_features(KernelE2EAudibilityV898Tests PRIVATE cxx_std_17)
target_include_directories(KernelE2EAudibilityV898Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_link_libraries(KernelE2EAudibilityV898Tests PRIVATE arpsid_core)
add_test(NAME KernelE2EAudibilityV898Tests COMMAND KernelE2EAudibilityV898Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite KernelE2EAudibilityV898Tests)
endif()

add_executable(DefaultPatchClassicAudibilityV953Tests
               source/tests/default_patch_classic_audibility_v953_tests.cpp
               source/forensic_patch_bank.cpp)
target_compile_features(DefaultPatchClassicAudibilityV953Tests PRIVATE cxx_std_17)
target_include_directories(DefaultPatchClassicAudibilityV953Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_link_libraries(DefaultPatchClassicAudibilityV953Tests PRIVATE arpsid_core)
add_test(NAME DefaultPatchClassicAudibilityV953Tests COMMAND DefaultPatchClassicAudibilityV953Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite DefaultPatchClassicAudibilityV953Tests)
endif()

add_executable(ArpAudioRenderV954Tests
               source/tests/arp_audio_render_v954_tests.cpp
               source/forensic_patch_bank.cpp)
target_compile_features(ArpAudioRenderV954Tests PRIVATE cxx_std_17)
target_include_directories(ArpAudioRenderV954Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_link_libraries(ArpAudioRenderV954Tests PRIVATE arpsid_core)
add_test(NAME ArpAudioRenderV954Tests COMMAND ArpAudioRenderV954Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite ArpAudioRenderV954Tests)
endif()

add_executable(DrumTransportStartAudibilityV955Tests
               source/tests/drum_transport_start_audibility_v955_tests.cpp
               source/forensic_patch_bank.cpp)
target_compile_features(DrumTransportStartAudibilityV955Tests PRIVATE cxx_std_17)
target_include_directories(DrumTransportStartAudibilityV955Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_link_libraries(DrumTransportStartAudibilityV955Tests PRIVATE arpsid_core)
add_test(NAME DrumTransportStartAudibilityV955Tests COMMAND DrumTransportStartAudibilityV955Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite DrumTransportStartAudibilityV955Tests)
endif()

add_executable(DrSidFactoryKitDistinctnessV956Tests
               source/tests/drsid_factory_kit_distinctness_v956_tests.cpp
               source/forensic_patch_bank.cpp)
target_compile_features(DrSidFactoryKitDistinctnessV956Tests PRIVATE cxx_std_17)
target_include_directories(DrSidFactoryKitDistinctnessV956Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_link_libraries(DrSidFactoryKitDistinctnessV956Tests PRIVATE arpsid_core)
add_test(NAME DrSidFactoryKitDistinctnessV956Tests COMMAND DrSidFactoryKitDistinctnessV956Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite DrSidFactoryKitDistinctnessV956Tests)
endif()

add_executable(DrSidSidAuthenticKnobResponseV957Tests
               source/tests/drsid_sidauthentic_knob_response_v957_tests.cpp
               source/forensic_patch_bank.cpp)
target_compile_features(DrSidSidAuthenticKnobResponseV957Tests PRIVATE cxx_std_17)
target_include_directories(DrSidSidAuthenticKnobResponseV957Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_link_libraries(DrSidSidAuthenticKnobResponseV957Tests PRIVATE arpsid_core)
add_test(NAME DrSidSidAuthenticKnobResponseV957Tests COMMAND DrSidSidAuthenticKnobResponseV957Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite DrSidSidAuthenticKnobResponseV957Tests)
endif()

add_executable(DrSidColdFirstBlockAudibilityV958Tests
               source/tests/drsid_cold_first_block_audibility_v958_tests.cpp
               source/forensic_patch_bank.cpp)
target_compile_features(DrSidColdFirstBlockAudibilityV958Tests PRIVATE cxx_std_17)
target_include_directories(DrSidColdFirstBlockAudibilityV958Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_link_libraries(DrSidColdFirstBlockAudibilityV958Tests PRIVATE arpsid_core)
add_test(NAME DrSidColdFirstBlockAudibilityV958Tests COMMAND DrSidColdFirstBlockAudibilityV958Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite DrSidColdFirstBlockAudibilityV958Tests)
endif()

add_executable(FactoryGmDefaultsSplitBrainV959Tests
               source/tests/factory_gm_defaults_split_brain_v959_tests.cpp
               source/forensic_patch_bank.cpp)
target_compile_features(FactoryGmDefaultsSplitBrainV959Tests PRIVATE cxx_std_17)
target_include_directories(FactoryGmDefaultsSplitBrainV959Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_link_libraries(FactoryGmDefaultsSplitBrainV959Tests PRIVATE arpsid_core)
add_test(NAME FactoryGmDefaultsSplitBrainV959Tests COMMAND FactoryGmDefaultsSplitBrainV959Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite FactoryGmDefaultsSplitBrainV959Tests)
endif()

add_executable(C64ProjectionMirrorBehaviorV899Tests source/tests/c64_projection_mirror_behavior_v899_tests.cpp)
target_compile_features(C64ProjectionMirrorBehaviorV899Tests PRIVATE cxx_std_17)
target_link_libraries(C64ProjectionMirrorBehaviorV899Tests PRIVATE arpsid_core)
add_test(NAME C64ProjectionMirrorBehaviorV899Tests COMMAND C64ProjectionMirrorBehaviorV899Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite C64ProjectionMirrorBehaviorV899Tests)
endif()

add_executable(ProjectionMirrorFinalClosureV902Tests source/tests/projection_mirror_final_closure_v902_tests.cpp)
target_compile_features(ProjectionMirrorFinalClosureV902Tests PRIVATE cxx_std_17)
target_link_libraries(ProjectionMirrorFinalClosureV902Tests PRIVATE arpsid_core)
target_compile_definitions(ProjectionMirrorFinalClosureV902Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME ProjectionMirrorFinalClosureV902Tests COMMAND ProjectionMirrorFinalClosureV902Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite ProjectionMirrorFinalClosureV902Tests)
endif()

add_executable(ProjectionMirrorFinalClosureV903Tests source/tests/projection_mirror_final_closure_v903_tests.cpp)
target_compile_features(ProjectionMirrorFinalClosureV903Tests PRIVATE cxx_std_17)
target_link_libraries(ProjectionMirrorFinalClosureV903Tests PRIVATE arpsid_core)
target_compile_definitions(ProjectionMirrorFinalClosureV903Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME ProjectionMirrorFinalClosureV903Tests COMMAND ProjectionMirrorFinalClosureV903Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite ProjectionMirrorFinalClosureV903Tests)
endif()

add_executable(ProjectionMirrorFinalClosureV904Tests source/tests/projection_mirror_final_closure_v904_tests.cpp)
target_compile_features(ProjectionMirrorFinalClosureV904Tests PRIVATE cxx_std_17)
target_link_libraries(ProjectionMirrorFinalClosureV904Tests PRIVATE arpsid_core)
target_compile_definitions(ProjectionMirrorFinalClosureV904Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME ProjectionMirrorFinalClosureV904Tests COMMAND ProjectionMirrorFinalClosureV904Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite ProjectionMirrorFinalClosureV904Tests)
endif()

add_executable(ProjectionMirrorFinalClosureV905Tests source/tests/projection_mirror_final_closure_v905_tests.cpp)
target_compile_features(ProjectionMirrorFinalClosureV905Tests PRIVATE cxx_std_17)
target_compile_definitions(ProjectionMirrorFinalClosureV905Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME ProjectionMirrorFinalClosureV905Tests COMMAND ProjectionMirrorFinalClosureV905Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite ProjectionMirrorFinalClosureV905Tests)
endif()

add_executable(Phase2TimingMusicClosureV906Tests source/tests/phase2_timing_music_closure_v906_tests.cpp)
target_compile_features(Phase2TimingMusicClosureV906Tests PRIVATE cxx_std_17)
target_compile_definitions(Phase2TimingMusicClosureV906Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Phase2TimingMusicClosureV906Tests COMMAND Phase2TimingMusicClosureV906Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite Phase2TimingMusicClosureV906Tests)
endif()

add_executable(TimingMusicSweepClosureV907Tests source/tests/timing_music_sweep_closure_v907_tests.cpp)
target_compile_features(TimingMusicSweepClosureV907Tests PRIVATE cxx_std_17)
target_compile_definitions(TimingMusicSweepClosureV907Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME TimingMusicSweepClosureV907Tests COMMAND TimingMusicSweepClosureV907Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite TimingMusicSweepClosureV907Tests)
endif()

add_executable(Phase2NoOutputFxClosureV908Tests source/tests/phase2_nooutput_fx_closure_v908_tests.cpp)
target_compile_features(Phase2NoOutputFxClosureV908Tests PRIVATE cxx_std_17)
target_compile_definitions(Phase2NoOutputFxClosureV908Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Phase2NoOutputFxClosureV908Tests COMMAND Phase2NoOutputFxClosureV908Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite Phase2NoOutputFxClosureV908Tests)
endif()

add_executable(IngressParityTimingAuthorityV910Tests
    source/tests/ingress_parity_timing_authority_v910_tests.cpp
    source/forensic_patch_bank.cpp)
target_compile_features(IngressParityTimingAuthorityV910Tests PRIVATE cxx_std_17)
target_link_libraries(IngressParityTimingAuthorityV910Tests PRIVATE arpsid_core)
target_include_directories(IngressParityTimingAuthorityV910Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/source/au3)
target_compile_definitions(IngressParityTimingAuthorityV910Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME IngressParityTimingAuthorityV910Tests COMMAND IngressParityTimingAuthorityV910Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite IngressParityTimingAuthorityV910Tests)
endif()

add_executable(ClassicModeAuthorityClosureV909Tests source/tests/classic_mode_authority_closure_v909_tests.cpp)
target_compile_features(ClassicModeAuthorityClosureV909Tests PRIVATE cxx_std_17)
target_link_libraries(ClassicModeAuthorityClosureV909Tests PRIVATE arpsid_core)
target_include_directories(ClassicModeAuthorityClosureV909Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(ClassicModeAuthorityClosureV909Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME ClassicModeAuthorityClosureV909Tests COMMAND ClassicModeAuthorityClosureV909Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite ClassicModeAuthorityClosureV909Tests)
endif()

add_executable(BitPerfectClassicAuthorityV939Tests source/tests/bitperfect_classic_authority_v939_tests.cpp)
target_compile_features(BitPerfectClassicAuthorityV939Tests PRIVATE cxx_std_17)
target_link_libraries(BitPerfectClassicAuthorityV939Tests PRIVATE arpsid_core)
target_include_directories(BitPerfectClassicAuthorityV939Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(BitPerfectClassicAuthorityV939Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME BitPerfectClassicAuthorityV939Tests COMMAND BitPerfectClassicAuthorityV939Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite BitPerfectClassicAuthorityV939Tests)
endif()

add_executable(RenderModeTransitionKitPreservationV942Tests source/tests/render_mode_transition_kit_preservation_v942_tests.cpp)
target_compile_features(RenderModeTransitionKitPreservationV942Tests PRIVATE cxx_std_17)
target_include_directories(RenderModeTransitionKitPreservationV942Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(RenderModeTransitionKitPreservationV942Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME RenderModeTransitionKitPreservationV942Tests COMMAND RenderModeTransitionKitPreservationV942Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite RenderModeTransitionKitPreservationV942Tests)
endif()

add_executable(AuthoritySplitBrainClosureV943Tests source/tests/authority_splitbrain_closure_v943_tests.cpp)
target_compile_features(AuthoritySplitBrainClosureV943Tests PRIVATE cxx_std_17)
target_link_libraries(AuthoritySplitBrainClosureV943Tests PRIVATE arpsid_core)
target_include_directories(AuthoritySplitBrainClosureV943Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(AuthoritySplitBrainClosureV943Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME AuthoritySplitBrainClosureV943Tests COMMAND AuthoritySplitBrainClosureV943Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite AuthoritySplitBrainClosureV943Tests)
endif()

add_executable(AuthorityPostBatchCanonicalizationV944Tests source/tests/authority_postbatch_canonicalization_v944_tests.cpp)
target_compile_features(AuthorityPostBatchCanonicalizationV944Tests PRIVATE cxx_std_17)
target_link_libraries(AuthorityPostBatchCanonicalizationV944Tests PRIVATE arpsid_core)
target_include_directories(AuthorityPostBatchCanonicalizationV944Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(AuthorityPostBatchCanonicalizationV944Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME AuthorityPostBatchCanonicalizationV944Tests COMMAND AuthorityPostBatchCanonicalizationV944Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite AuthorityPostBatchCanonicalizationV944Tests)
endif()

add_executable(AuthorityStagingPolicyV945Tests source/tests/authority_staging_policy_v945_tests.cpp)
target_compile_features(AuthorityStagingPolicyV945Tests PRIVATE cxx_std_17)
target_link_libraries(AuthorityStagingPolicyV945Tests PRIVATE arpsid_core)
target_include_directories(AuthorityStagingPolicyV945Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(AuthorityStagingPolicyV945Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME AuthorityStagingPolicyV945Tests COMMAND AuthorityStagingPolicyV945Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite AuthorityStagingPolicyV945Tests)
endif()

add_executable(AuthorityFinalStagingV946Tests source/tests/authority_final_staging_v946_tests.cpp)
target_compile_features(AuthorityFinalStagingV946Tests PRIVATE cxx_std_17)
target_link_libraries(AuthorityFinalStagingV946Tests PRIVATE arpsid_core)
target_include_directories(AuthorityFinalStagingV946Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(AuthorityFinalStagingV946Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME AuthorityFinalStagingV946Tests COMMAND AuthorityFinalStagingV946Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite AuthorityFinalStagingV946Tests)
endif()

add_executable(AuthoritySanitizedSideEffectsV947Tests source/tests/authority_sanitized_side_effects_v947_tests.cpp)
target_compile_features(AuthoritySanitizedSideEffectsV947Tests PRIVATE cxx_std_17)
target_link_libraries(AuthoritySanitizedSideEffectsV947Tests PRIVATE arpsid_core)
target_include_directories(AuthoritySanitizedSideEffectsV947Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(AuthoritySanitizedSideEffectsV947Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME AuthoritySanitizedSideEffectsV947Tests COMMAND AuthoritySanitizedSideEffectsV947Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite AuthoritySanitizedSideEffectsV947Tests)
endif()

add_executable(AuthorityDirtyFlushStagingV948Tests source/tests/authority_dirty_flush_staging_v948_tests.cpp)
target_compile_features(AuthorityDirtyFlushStagingV948Tests PRIVATE cxx_std_17)
target_link_libraries(AuthorityDirtyFlushStagingV948Tests PRIVATE arpsid_core)
target_include_directories(AuthorityDirtyFlushStagingV948Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(AuthorityDirtyFlushStagingV948Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME AuthorityDirtyFlushStagingV948Tests COMMAND AuthorityDirtyFlushStagingV948Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite AuthorityDirtyFlushStagingV948Tests)
endif()

add_executable(SeqDrSidAuthorityV949Tests source/tests/seq_drsid_authority_v949_tests.cpp)
target_compile_features(SeqDrSidAuthorityV949Tests PRIVATE cxx_std_17)
target_link_libraries(SeqDrSidAuthorityV949Tests PRIVATE arpsid_core)
target_include_directories(SeqDrSidAuthorityV949Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/source
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(SeqDrSidAuthorityV949Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME SeqDrSidAuthorityV949Tests COMMAND SeqDrSidAuthorityV949Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SeqDrSidAuthorityV949Tests)
endif()

add_executable(SeqDrSidResetPreservationV950Tests source/tests/seq_drsid_reset_preservation_v950_tests.cpp)
target_compile_features(SeqDrSidResetPreservationV950Tests PRIVATE cxx_std_17)
target_include_directories(SeqDrSidResetPreservationV950Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/source
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(SeqDrSidResetPreservationV950Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME SeqDrSidResetPreservationV950Tests COMMAND SeqDrSidResetPreservationV950Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite SeqDrSidResetPreservationV950Tests)
endif()

add_executable(StateRootStagingParityV951Tests source/tests/state_root_staging_parity_v951_tests.cpp)
target_compile_features(StateRootStagingParityV951Tests PRIVATE cxx_std_17)
target_include_directories(StateRootStagingParityV951Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/source
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(StateRootStagingParityV951Tests PRIVATE ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME StateRootStagingParityV951Tests COMMAND StateRootStagingParityV951Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite StateRootStagingParityV951Tests)
endif()

add_executable(RuntimeBehaviorClosureV952Tests
  source/tests/runtime_behavior_closure_v952_tests.cpp
  source/forensic_patch_bank.cpp)
target_compile_features(RuntimeBehaviorClosureV952Tests PRIVATE cxx_std_17)
target_link_libraries(RuntimeBehaviorClosureV952Tests PRIVATE arpsid_core)
target_include_directories(RuntimeBehaviorClosureV952Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/source
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME RuntimeBehaviorClosureV952Tests COMMAND RuntimeBehaviorClosureV952Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite RuntimeBehaviorClosureV952Tests)
endif()

add_executable(CanonicalRenderPipelineClosureV965Tests
  source/tests/canonical_render_pipeline_closure_v965_tests.cpp
  source/forensic_patch_bank.cpp)
target_compile_features(CanonicalRenderPipelineClosureV965Tests PRIVATE cxx_std_17)
target_link_libraries(CanonicalRenderPipelineClosureV965Tests PRIVATE arpsid_core)
target_include_directories(CanonicalRenderPipelineClosureV965Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/source
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(CanonicalRenderPipelineClosureV965Tests PRIVATE
  ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME CanonicalRenderPipelineClosureV965Tests
  COMMAND CanonicalRenderPipelineClosureV965Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite CanonicalRenderPipelineClosureV965Tests)
endif()

add_executable(ParameterPresentationAuthorityV966Tests
  source/tests/parameter_presentation_authority_v966_tests.cpp)
target_compile_features(ParameterPresentationAuthorityV966Tests PRIVATE cxx_std_17)
target_include_directories(ParameterPresentationAuthorityV966Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/source
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(ParameterPresentationAuthorityV966Tests PRIVATE
  ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME ParameterPresentationAuthorityV966Tests
  COMMAND ParameterPresentationAuthorityV966Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite ParameterPresentationAuthorityV966Tests)
endif()

add_executable(DrSidQuickKitBaseProfileParityV967Tests
  source/tests/drsid_quick_kit_base_profile_parity_v967_tests.cpp)
target_compile_features(DrSidQuickKitBaseProfileParityV967Tests PRIVATE cxx_std_17)
target_include_directories(DrSidQuickKitBaseProfileParityV967Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/source
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(DrSidQuickKitBaseProfileParityV967Tests PRIVATE
  ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME DrSidQuickKitBaseProfileParityV967Tests
  COMMAND DrSidQuickKitBaseProfileParityV967Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite DrSidQuickKitBaseProfileParityV967Tests)
endif()

add_executable(DrSidUserKitSaveModeNormalizationV968Tests
  source/tests/drsid_user_kit_save_mode_normalization_v968_tests.cpp)
target_compile_features(DrSidUserKitSaveModeNormalizationV968Tests PRIVATE cxx_std_17)
target_include_directories(DrSidUserKitSaveModeNormalizationV968Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/source
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(DrSidUserKitSaveModeNormalizationV968Tests PRIVATE
  ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME DrSidUserKitSaveModeNormalizationV968Tests
  COMMAND DrSidUserKitSaveModeNormalizationV968Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite DrSidUserKitSaveModeNormalizationV968Tests)
endif()

add_executable(TestSuiteIntegrityV969Tests
  source/tests/test_suite_integrity_v969_tests.cpp)
target_compile_features(TestSuiteIntegrityV969Tests PRIVATE cxx_std_17)
target_include_directories(TestSuiteIntegrityV969Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/source
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(TestSuiteIntegrityV969Tests PRIVATE
  ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME TestSuiteIntegrityV969Tests
  COMMAND TestSuiteIntegrityV969Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite TestSuiteIntegrityV969Tests)
endif()

add_executable(BuildGraphForensicPatchbankLibV970Tests
  source/tests/build_graph_forensic_patchbank_lib_v970_tests.cpp)
target_compile_features(BuildGraphForensicPatchbankLibV970Tests PRIVATE cxx_std_17)
target_include_directories(BuildGraphForensicPatchbankLibV970Tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/source
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(BuildGraphForensicPatchbankLibV970Tests PRIVATE
  ARPSID_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME BuildGraphForensicPatchbankLibV970Tests
  COMMAND BuildGraphForensicPatchbankLibV970Tests)
if(TARGET arpsid_release_closure_suite)
  add_dependencies(arpsid_release_closure_suite BuildGraphForensicPatchbankLibV970Tests)
endif()

add_test(NAME VersionCoherenceV970Tests
  COMMAND ${CMAKE_COMMAND} -E env python3
          ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_version_coherence.py)
