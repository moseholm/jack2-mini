cmake_minimum_required(VERSION 3.20)

project(jack2 VERSION 1.9.23 LANGUAGES C CXX)

set(JACK_API_VERSION "0.1.0")

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This JACK build supports Linux only.")
endif()

include(GNUInstallDirs)

option(JACK_BUILD_TESTS "Build test programs" OFF)
option(JACK_RESAMPLER_SIMD "Build the x86 SSE/AVX2 resampler kernels and select one at runtime" ON)
set(JACK_MAX_CLIENTS 256 CACHE STRING "Maximum number of JACK clients")
set(JACK_PORTS_PER_APPLICATION 2048 CACHE STRING "Maximum number of ports per application")

find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa>=1.0.18)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# C++23 support varies by compiler release.  Fail early with a useful message
# instead of letting a partial implementation produce confusing build errors.
if(NOT "cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    message(FATAL_ERROR
        "The C++ compiler does not support C++23.  jack2-mini requires "
        "GCC 13 or newer, or Clang 16 or newer.")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
        message(FATAL_ERROR
            "jack2-mini requires GCC 13 or newer for C++23 "
            "(found ${CMAKE_CXX_COMPILER_VERSION}).")
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
        message(FATAL_ERROR
            "jack2-mini requires Clang 16 or newer for C++23 "
            "(found ${CMAKE_CXX_COMPILER_VERSION}).")
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-invalid-offsetof>)
    add_link_options(-Wl,--no-undefined)
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(JACK_ADDON_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/jack")
set(JACK_LOCATION "${CMAKE_INSTALL_FULL_BINDIR}")
set(JACK_VERSION "${PROJECT_VERSION}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
    @ONLY)

set(JACK_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/jack"
    "${CMAKE_CURRENT_SOURCE_DIR}/posix"
    "${CMAKE_CURRENT_SOURCE_DIR}/linux"
    "${CMAKE_CURRENT_BINARY_DIR}")

set(JACK_COMMON_SOURCES
    common/JackActivationCount.cpp
    common/JackAPI.cpp
    common/JackClient.cpp
    common/JackConnectionManager.cpp
    common/ringbuffer.c
    common/JackError.cpp
    common/JackException.cpp
    common/JackFrameTimer.cpp
    common/JackGraphManager.cpp
    common/JackPort.cpp
    common/JackPortType.cpp
    common/JackAudioPort.cpp
    common/JackMidiPort.cpp
    common/JackMidiAPI.cpp
    common/JackEngineControl.cpp
    common/JackShmMem.cpp
    common/JackGenericClientChannel.cpp
    common/JackGlobals.cpp
    common/JackTransportEngine.cpp
    common/JackTools.cpp
    common/JackMessageBuffer.cpp
    common/promiscuous.c
    common/jackmini/client.cpp
    posix/JackPosixThread.cpp
    posix/JackPosixProcessSync.cpp
    posix/JackPosixMutex.cpp
    posix/JackSocket.cpp
    posix/JackFdSegment.cpp
    linux/JackLinuxFutex.cpp
    linux/JackLinuxTime.c)

set(JACK_CLIENT_SOURCES
    ${JACK_COMMON_SOURCES}
    common/JackLibClient.cpp
    common/JackLibAPI.cpp
    common/JackMetadata.cpp
    posix/JackSocketClientChannel.cpp
    posix/JackPosixServerLaunch.cpp)

set(JACK_SERVER_SOURCES
    ${JACK_COMMON_SOURCES}
    common/JackAudioDriver.cpp
    common/JackTimedDriver.cpp
    common/JackMidiDriver.cpp
    common/JackDriver.cpp
    common/JackEngine.cpp
    common/JackExternalClient.cpp
    common/JackFreewheelDriver.cpp
    common/JackInternalClient.cpp
    common/JackInternalSessionLoader.cpp
    common/JackServer.cpp
    common/JackThreadedDriver.cpp
    common/JackWaitThreadedDriver.cpp
    common/JackWaitCallbackDriver.cpp
    common/JackServerAPI.cpp
    common/JackDriverLoader.cpp
    common/JackServerGlobals.cpp
    common/JackControlAPI.cpp
    common/JackRequestDecoder.cpp
    common/JackMidiAsyncQueue.cpp
    common/JackMidiAsyncWaitQueue.cpp
    common/JackMidiBufferReadQueue.cpp
    common/JackMidiBufferWriteQueue.cpp
    common/JackMidiRawInputWriteQueue.cpp
    common/JackMidiRawOutputWriteQueue.cpp
    common/JackMidiReadQueue.cpp
    common/JackMidiReceiveQueue.cpp
    common/JackMidiSendQueue.cpp
    common/JackMidiUtil.cpp
    common/JackMidiWriteQueue.cpp
    common/JackMetadata.cpp
    posix/JackSocketServerChannel.cpp
    posix/JackSocketNotifyChannel.cpp
    posix/JackSocketServerNotifyChannel.cpp
    posix/JackServerLock.cpp)

add_library(jack SHARED ${JACK_CLIENT_SOURCES})
target_include_directories(jack PRIVATE ${JACK_INCLUDE_DIRS})
target_compile_definitions(jack PRIVATE HAVE_CONFIG_H)
target_link_libraries(jack PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt)
set_target_properties(jack PROPERTIES
    OUTPUT_NAME jack
    VERSION "${JACK_API_VERSION}"
    SOVERSION 0
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

# The server objects are built once and shared by libjackserver and the unit
# tests, so the sources are not compiled twice.
add_library(jackserver_objects OBJECT ${JACK_SERVER_SOURCES})
target_include_directories(jackserver_objects PRIVATE ${JACK_INCLUDE_DIRS})
target_compile_definitions(jackserver_objects PRIVATE HAVE_CONFIG_H SERVER_SIDE)
set_target_properties(jackserver_objects PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

add_library(jackserver SHARED $<TARGET_OBJECTS:jackserver_objects>)
target_link_libraries(jackserver PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt)
set_target_properties(jackserver PROPERTIES
    OUTPUT_NAME jackserver
    VERSION "${JACK_API_VERSION}"
    SOVERSION 0
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

add_executable(jackd common/Jackdmp.cpp)
target_include_directories(jackd PRIVATE ${JACK_INCLUDE_DIRS})
target_compile_definitions(jackd PRIVATE HAVE_CONFIG_H SERVER_SIDE)
target_link_libraries(jackd PRIVATE jackserver Threads::Threads ${CMAKE_DL_LIBS} rt m)

function(jack_add_driver name)
    add_library(jack_${name} MODULE ${ARGN})
    target_include_directories(jack_${name} PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_${name} PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_${name} PRIVATE jackserver)
    set_target_properties(jack_${name} PROPERTIES
        PREFIX ""
        OUTPUT_NAME jack_${name}
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/jack")
    install(TARGETS jack_${name} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/jack)
endfunction()

jack_add_driver(dummy common/JackDummyDriver.cpp)
jack_add_driver(loopback common/JackLoopbackDriver.cpp)
jack_add_driver(proxy common/JackProxyDriver.cpp)

# The vendored PipeWire resampler is portable C by default.  On x86 we also
# build its SSE and AVX2 kernels, and on AArch64 its NEON kernel; the bridge
# selects one at runtime (x86) or statically (AArch64, where NEON is baseline)
# and keeps the C version as the fallback.  The filter tables are still
# computed at setup time because the precomputed table is not vendored.
include(CheckCCompilerFlag)
set(JACK_RESAMPLER_SIMD_SOURCES)
set(JACK_RESAMPLER_SIMD_DEFINITIONS)
set(JACK_RESAMPLER_SIMD_ENABLED OFF)
if(JACK_RESAMPLER_SIMD AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    list(APPEND JACK_RESAMPLER_SIMD_SOURCES linux/alsa/resample/resample-native-neon.c)
    list(APPEND JACK_RESAMPLER_SIMD_DEFINITIONS HAVE_NEON)
    set(JACK_RESAMPLER_SIMD_ENABLED ON)
elseif(JACK_RESAMPLER_SIMD AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|i[3-6]86)$")
    check_c_compiler_flag("-msse" JACK_HAVE_MSSE)
    if(JACK_HAVE_MSSE)
        list(APPEND JACK_RESAMPLER_SIMD_SOURCES linux/alsa/resample/resample-native-sse.c)
        list(APPEND JACK_RESAMPLER_SIMD_DEFINITIONS HAVE_SSE)
        set_source_files_properties(linux/alsa/resample/resample-native-sse.c
            PROPERTIES COMPILE_OPTIONS "-msse")
        set(JACK_RESAMPLER_SIMD_ENABLED ON)
    endif()

    check_c_compiler_flag("-mavx2" JACK_HAVE_MAVX2)
    check_c_compiler_flag("-mfma" JACK_HAVE_MFMA)
    if(JACK_HAVE_MAVX2 AND JACK_HAVE_MFMA)
        list(APPEND JACK_RESAMPLER_SIMD_SOURCES linux/alsa/resample/resample-native-avx2.c)
        list(APPEND JACK_RESAMPLER_SIMD_DEFINITIONS HAVE_AVX2 HAVE_FMA)
        set_source_files_properties(linux/alsa/resample/resample-native-avx2.c
            PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
        set(JACK_RESAMPLER_SIMD_ENABLED ON)
    endif()
endif()
message(STATUS "Resampler SIMD kernels                    : ${JACK_RESAMPLER_SIMD_ENABLED}")

set(JACK_ALSA_SOURCES
    common/memops.c
    linux/alsa/JackAlsaDriver.cpp
    linux/alsa/alsa_rawmidi.c
    linux/alsa/alsa_seqmidi.c
    linux/alsa/alsa_midi_jackmp.cpp
    linux/alsa/generic_hw.c
    linux/alsa/hdsp.c
    linux/alsa/alsa_driver.c
    linux/alsa/hammerfall.c
    linux/alsa/ice1712.c
    linux/alsa/JackAlsaRingBuffer.cpp
    linux/alsa/JackAlsaSecondaryConfig.cpp
    linux/alsa/JackAlsaSecondaryCard.cpp
    linux/alsa/JackAlsaRateConverter.cpp
    linux/alsa/JackAlsaDriftController.cpp
    linux/alsa/JackAlsaControl.cpp
    linux/alsa/JackAlsaControlMonitor.cpp
    linux/alsa/JackAlsaReconfigWorker.cpp
    linux/alsa/JackAlsaEnumerate.cpp
    linux/alsa/JackAlsaHotplug.cpp
    linux/alsa/resample/JackResampler.cpp
    linux/alsa/resample/pipewire_resample_bridge.c
    linux/alsa/resample/resample-native.c
    linux/alsa/resample/resample-native-c.c
    ${JACK_RESAMPLER_SIMD_SOURCES})

set(JACK_ALSARAWMIDI_SOURCES
    linux/alsarawmidi/JackALSARawMidiDriver.cpp
    linux/alsarawmidi/JackALSARawMidiInputPort.cpp
    linux/alsarawmidi/JackALSARawMidiOutputPort.cpp
    linux/alsarawmidi/JackALSARawMidiPort.cpp
    linux/alsarawmidi/JackALSARawMidiReceiveQueue.cpp
    linux/alsarawmidi/JackALSARawMidiSendQueue.cpp
    linux/alsarawmidi/JackALSARawMidiUtil.cpp)

jack_add_driver(alsa ${JACK_ALSA_SOURCES})
target_link_libraries(jack_alsa PRIVATE PkgConfig::ALSA PkgConfig::UDEV m ${CMAKE_DL_LIBS})
# The vendored PipeWire resampler is built with its precomputed filter tables
# disabled and a small SPA compatibility shim in place of the SPA headers.
target_include_directories(jack_alsa PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/linux/alsa/resample"
    "${CMAKE_CURRENT_SOURCE_DIR}/linux/alsa/resample/spa-shim")
target_compile_definitions(jack_alsa PRIVATE RESAMPLE_DISABLE_PRECOMP ${JACK_RESAMPLER_SIMD_DEFINITIONS})

jack_add_driver(alsarawmidi ${JACK_ALSARAWMIDI_SOURCES})
target_link_libraries(jack_alsarawmidi PRIVATE PkgConfig::ALSA)

install(TARGETS jack jackserver
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS jackd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY common/jack/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/jack
    FILES_MATCHING PATTERN "*.h")

set(PREFIX "${CMAKE_INSTALL_PREFIX}")
set(LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
set(INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
set(CLIENTLIB "jack")
set(SERVERLIB "jackserver")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/jack.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/jack.pc"
    @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/jack.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(JACK_BUILD_TESTS)
    set(JACK_TEST_PROGRAMS
        jack_test:tests/test.cpp
        jack_cpu:tests/cpu.c
        jack_iodelay:tests/iodelay.cpp
        jack_multiple_metro:tests/external_metro.cpp)

    foreach(entry IN LISTS JACK_TEST_PROGRAMS)
        string(REPLACE ":" ";" parts "${entry}")
        list(GET parts 0 program)
        list(GET parts 1 source)
        add_executable(${program} ${source})
        target_include_directories(${program} PRIVATE ${JACK_INCLUDE_DIRS})
        target_link_libraries(${program} PRIVATE jack rt)
        install(TARGETS ${program} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endforeach()

    # Unit tests for the secondary ALSA card building blocks.  They use the
    # real sources, not a library, and need no ALSA hardware.
    enable_testing()
    set(JACK_ALSA_UNIT_SOURCES
        tests/alsa_secondary_unit.cpp
        linux/alsa/JackAlsaRingBuffer.cpp
        linux/alsa/JackAlsaSecondaryConfig.cpp
        linux/alsa/JackAlsaRateConverter.cpp
        linux/alsa/JackAlsaDriftController.cpp
        linux/alsa/resample/JackResampler.cpp
        linux/alsa/resample/pipewire_resample_bridge.c
        linux/alsa/resample/resample-native.c
        linux/alsa/resample/resample-native-c.c
        ${JACK_RESAMPLER_SIMD_SOURCES})
    add_executable(jack_alsa_unit ${JACK_ALSA_UNIT_SOURCES})
    target_include_directories(jack_alsa_unit PRIVATE
        ${JACK_INCLUDE_DIRS}
        "${CMAKE_CURRENT_SOURCE_DIR}/linux/alsa"
        "${CMAKE_CURRENT_SOURCE_DIR}/linux/alsa/resample"
        "${CMAKE_CURRENT_SOURCE_DIR}/linux/alsa/resample/spa-shim")
    target_compile_definitions(jack_alsa_unit PRIVATE
        HAVE_CONFIG_H SERVER_SIDE RESAMPLE_DISABLE_PRECOMP
        ${JACK_RESAMPLER_SIMD_DEFINITIONS})
    target_link_libraries(jack_alsa_unit PRIVATE jackserver m)
    add_test(NAME alsa_secondary_unit COMMAND jack_alsa_unit)

    # The realtime message buffer, run against the real singleton and flush thread.
    # The server's internals are hidden in libjackserver, so the test links
    # the server objects directly.
    add_executable(jack_message_unit tests/message_buffer_unit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_include_directories(jack_message_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_message_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_message_unit PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt m)
    add_test(NAME message_buffer_unit COMMAND jack_message_unit)

    add_executable(jack_cstring_unit tests/cstring_unit.cpp)
    target_include_directories(jack_cstring_unit PRIVATE ${JACK_INCLUDE_DIRS})
    add_test(NAME cstring_unit COMMAND jack_cstring_unit)

    # Compile-time checks of the jackmini client API headers.  No implementation is linked.
    add_executable(jack_api_unit tests/jackmini_api_unit.cpp)
    target_include_directories(jack_api_unit PRIVATE ${JACK_INCLUDE_DIRS})
    add_test(NAME jackmini_api_unit COMMAND jack_api_unit)

    # Static layout checks of the shared-memory structures.  Compile-only checks; nothing is linked from the server.
    add_executable(jack_shm_layout_unit tests/shm_layout_unit.cpp)
    target_include_directories(jack_shm_layout_unit PRIVATE ${JACK_INCLUDE_DIRS}
        "${CMAKE_CURRENT_SOURCE_DIR}/linux" "${CMAKE_CURRENT_SOURCE_DIR}/posix")
    target_compile_definitions(jack_shm_layout_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    add_test(NAME shm_layout_unit COMMAND jack_shm_layout_unit)

    # Descriptor passing over a Unix socket and anonymous shared-memory segments (P2 step 2).
    add_executable(jack_fd_segment_unit tests/fd_segment_unit.cpp posix/JackFdSegment.cpp)
    target_include_directories(jack_fd_segment_unit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/posix")
    add_test(NAME fd_segment_unit COMMAND jack_fd_segment_unit)

    # Message framing of the control socket: the header, the exact payload length, and the reply writer and reader.
    add_executable(jack_message_frame_unit tests/message_frame_unit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_include_directories(jack_message_frame_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_message_frame_unit PRIVATE HAVE_CONFIG_H)
    add_test(NAME message_frame_unit COMMAND jack_message_frame_unit)

    # Activation counters under concurrency: the cycle and the connection changes (protocol.md section 4.5).  Run it under TSAN.
    add_executable(jack_counter_unit tests/counter_tsan_unit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_include_directories(jack_counter_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_counter_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_counter_unit PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt m)
    add_test(NAME counter_unit COMMAND jack_counter_unit)

    # Deactivation keeps the connections: silence for an inactive client, and the connection is still there (protocol.md section 6).
    add_executable(jack_deactivate_graph_unit tests/deactivate_graph_unit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_include_directories(jack_deactivate_graph_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_deactivate_graph_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_deactivate_graph_unit PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt m)
    add_test(NAME deactivate_graph_unit COMMAND jack_deactivate_graph_unit)

    # Conformance of the control socket: requests, replies, malformed and version-mismatched messages, with a fake server.
    add_executable(jack_protocol_conformance_unit tests/protocol_conformance_unit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_include_directories(jack_protocol_conformance_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_protocol_conformance_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_protocol_conformance_unit PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt m)
    add_test(NAME protocol_conformance_unit COMMAND jack_protocol_conformance_unit)

    # The parts of jackmini that need no server: ring buffer, Uuid, error text.
    add_library(jackmini_base STATIC common/jackmini/uuid.cpp common/jackmini/error.cpp)
    target_include_directories(jackmini_base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
    target_compile_features(jackmini_base PUBLIC cxx_std_23)
    add_executable(jack_jackmini_unit tests/jackmini_unit.cpp)
    target_link_libraries(jack_jackmini_unit PRIVATE jackmini_base Threads::Threads)
    add_test(NAME jackmini_unit COMMAND jack_jackmini_unit)

    # The client lifecycle of the jackmini API with a running server.  Not a test: ctest has no server.  Run it after
    # starting one, for example `jackd -d dummy`.
    add_executable(jack_jackmini_client_e2e tests/jackmini_client_e2e.cpp)
    target_include_directories(jack_jackmini_client_e2e PRIVATE ${JACK_INCLUDE_DIRS})
    target_link_libraries(jack_jackmini_client_e2e PRIVATE jack jackmini_base Threads::Threads)

    # A test-only resampler plugin, loaded by name from JACK_RESAMPLER_PATH.
    add_library(passthrough_test MODULE tests/alsa_test_resampler_plugin.c)
    target_include_directories(passthrough_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/linux/alsa")
    set_target_properties(passthrough_test PROPERTIES
        PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/plugins")
    add_dependencies(jack_alsa_unit passthrough_test)
    target_compile_definitions(jack_alsa_unit PRIVATE
        JACK_TEST_PLUGIN_DIR="${CMAKE_BINARY_DIR}/tests/plugins")
endif()

message(STATUS "JACK ${JACK_VERSION}")
message(STATUS "Maximum JACK clients                        : ${JACK_MAX_CLIENTS}")
message(STATUS "Maximum ports per application               : ${JACK_PORTS_PER_APPLICATION}")
message(STATUS "Install prefix                              : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Library directory                           : ${CMAKE_INSTALL_FULL_LIBDIR}")
message(STATUS "Drivers directory                           : ${JACK_ADDON_DIR}")
message(STATUS "Build tests                                 : ${JACK_BUILD_TESTS}")
