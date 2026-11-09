cmake_minimum_required(VERSION 3.20)

project(jack2 VERSION 1.9.23 LANGUAGES C CXX)

set(JACK_API_VERSION "0.1.0")

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This JACK build supports Linux only.")
endif()

include(GNUInstallDirs)

option(JACK_BUILD_TESTS "Build test programs" OFF)
option(JACK_TOOLS "Build the command-line tools (jackmini_<tool>)" ON)
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
    "${CMAKE_CURRENT_SOURCE_DIR}/client"
    "${CMAKE_CURRENT_SOURCE_DIR}/server"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/jack"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform"
    "${CMAKE_CURRENT_BINARY_DIR}")

set(JACK_COMMON_SOURCES
    common/JackActivationCount.cpp
    common/JackConnectionManager.cpp
    common/JackError.cpp
    common/JackException.cpp
    common/JackFrameTimer.cpp
    common/JackGraphManager.cpp
    common/JackPort.cpp
    common/JackPortType.cpp
    common/JackAudioPort.cpp
    common/JackEngineControl.cpp
    common/JackShmMem.cpp
    common/JackGlobals.cpp
    common/JackTransportEngine.cpp
    common/JackTools.cpp
    common/JackUuid.cpp
    common/JackMessageBuffer.cpp
    platform/promiscuous.cpp
    platform/JackPosixThread.cpp
    platform/JackPosixProcessSync.cpp
    platform/JackPosixMutex.cpp
    platform/JackSocket.cpp
    platform/JackFdSegment.cpp
    platform/JackLinuxFutex.cpp
    platform/JackLinuxTime.cpp)

set(JACK_CLIENT_SOURCES
    ${JACK_COMMON_SOURCES}
    client/jackmini/client.cpp
    client/process_context.cpp
    client/jackmini/uuid.cpp
    client/client_session.cpp
    client/JackServerLaunch.cpp)

set(JACK_SERVER_SOURCES
    ${JACK_COMMON_SOURCES}
    server/JackAudioDriver.cpp
    server/JackTimedDriver.cpp
    server/JackDriver.cpp
    server/JackEngine.cpp
    server/JackExternalClient.cpp
    server/JackFreewheelDriver.cpp
    server/JackServer.cpp
    server/JackThreadedDriver.cpp
    server/JackDriverLoader.cpp
    server/JackServerGlobals.cpp
    server/control.cpp
    server/JackRequestDecoder.cpp
    platform/JackSocketServerChannel.cpp
    platform/JackSocketNotifyChannel.cpp
    platform/JackSocketServerNotifyChannel.cpp
    platform/JackServerLock.cpp)

# The ring buffer is a library of its own: the client and server libraries and the ALSA drivers use it.
add_library(jackring SHARED common/ringbuffer.cpp)
target_include_directories(jackring PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
set_target_properties(jackring PROPERTIES VERSION "${JACK_API_VERSION}" SOVERSION 0)

# The MIDI buffer (the format of the MIDI data in the port buffers, and the jack_midi_* functions that the drivers use).
# Built with the server definitions: its buffer methods are used by the server and by the client library.
add_library(jackmidi SHARED common/JackMidiPort.cpp common/JackMidiAPI.cpp)
target_include_directories(jackmidi PRIVATE ${JACK_INCLUDE_DIRS})
target_compile_definitions(jackmidi PRIVATE HAVE_CONFIG_H SERVER_SIDE)
target_link_libraries(jackmidi PRIVATE jacklog)
set_target_properties(jackmidi PROPERTIES VERSION "${JACK_API_VERSION}" SOVERSION 0)

# The basic logging (errors and messages, and where they go): a library of its own, with no dependencies.
add_library(jacklog SHARED common/JackLog.cpp)
target_include_directories(jacklog PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
set_target_properties(jacklog PROPERTIES VERSION "${JACK_API_VERSION}" SOVERSION 0)

add_library(jack SHARED ${JACK_CLIENT_SOURCES})
target_include_directories(jack PRIVATE ${JACK_INCLUDE_DIRS})
target_compile_definitions(jack PRIVATE HAVE_CONFIG_H)
target_link_libraries(jack PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt jackring jacklog jackmidi)
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
target_link_libraries(jackserver PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt jackring jacklog jackmidi)
set_target_properties(jackserver PROPERTIES
    OUTPUT_NAME jackserver
    VERSION "${JACK_API_VERSION}"
    SOVERSION 0
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

add_executable(jackd server/Jackdmp.cpp)
target_include_directories(jackd PRIVATE ${JACK_INCLUDE_DIRS})
target_compile_definitions(jackd PRIVATE HAVE_CONFIG_H SERVER_SIDE)
target_link_libraries(jackd PRIVATE jackserver jacklog Threads::Threads ${CMAKE_DL_LIBS} rt m)

function(jack_add_driver name)
    add_library(jack_${name} MODULE ${ARGN})
    target_include_directories(jack_${name} PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_${name} PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_${name} PRIVATE jackserver jacklog jackmidi)
    set_target_properties(jack_${name} PROPERTIES
        PREFIX ""
        OUTPUT_NAME jack_${name}
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/jack")
    install(TARGETS jack_${name} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/jack)
endfunction()

jack_add_driver(dummy server/JackDummyDriver.cpp)

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
    list(APPEND JACK_RESAMPLER_SIMD_SOURCES platform/alsa/resample/resample-native-neon.c)
    list(APPEND JACK_RESAMPLER_SIMD_DEFINITIONS HAVE_NEON)
    set(JACK_RESAMPLER_SIMD_ENABLED ON)
elseif(JACK_RESAMPLER_SIMD AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|i[3-6]86)$")
    check_c_compiler_flag("-msse" JACK_HAVE_MSSE)
    if(JACK_HAVE_MSSE)
        list(APPEND JACK_RESAMPLER_SIMD_SOURCES platform/alsa/resample/resample-native-sse.c)
        list(APPEND JACK_RESAMPLER_SIMD_DEFINITIONS HAVE_SSE)
        set_source_files_properties(platform/alsa/resample/resample-native-sse.c
            PROPERTIES COMPILE_OPTIONS "-msse")
        set(JACK_RESAMPLER_SIMD_ENABLED ON)
    endif()

    check_c_compiler_flag("-mavx2" JACK_HAVE_MAVX2)
    check_c_compiler_flag("-mfma" JACK_HAVE_MFMA)
    if(JACK_HAVE_MAVX2 AND JACK_HAVE_MFMA)
        list(APPEND JACK_RESAMPLER_SIMD_SOURCES platform/alsa/resample/resample-native-avx2.c)
        list(APPEND JACK_RESAMPLER_SIMD_DEFINITIONS HAVE_AVX2 HAVE_FMA)
        set_source_files_properties(platform/alsa/resample/resample-native-avx2.c
            PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
        set(JACK_RESAMPLER_SIMD_ENABLED ON)
    endif()
endif()
message(STATUS "Resampler SIMD kernels                    : ${JACK_RESAMPLER_SIMD_ENABLED}")

set(JACK_ALSA_SOURCES
    common/memops.cpp
    platform/alsa/JackAlsaDriver.cpp
    platform/alsa/pcm_alsa.cpp
    platform/alsa/pcm_session.cpp
    platform/alsa/JackAlsaRingBuffer.cpp
    platform/alsa/JackAlsaSecondaryConfig.cpp
    platform/alsa/JackAlsaSecondaryCard.cpp
    platform/alsa/JackAlsaRateConverter.cpp
    platform/alsa/JackAlsaDriftController.cpp
    platform/alsa/JackAlsaControl.cpp
    platform/alsa/JackAlsaControlMonitor.cpp
    platform/alsa/JackAlsaReconfigWorker.cpp
    platform/alsa/JackAlsaEnumerate.cpp
    platform/alsa/JackAlsaHotplug.cpp
    platform/alsa/resample/JackResampler.cpp
    platform/alsa/resample/pipewire_resample_bridge.c
    platform/alsa/resample/resample-native.c
    platform/alsa/resample/resample-native-c.c
    ${JACK_RESAMPLER_SIMD_SOURCES})

jack_add_driver(alsa ${JACK_ALSA_SOURCES})
target_link_libraries(jack_alsa PRIVATE PkgConfig::ALSA PkgConfig::UDEV m ${CMAKE_DL_LIBS} jackring jacklog jackmidi)
# The vendored PipeWire resampler is built with its precomputed filter tables
# disabled and a small SPA compatibility shim in place of the SPA headers.
target_include_directories(jack_alsa PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/alsa/resample"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/alsa/resample/spa-shim")
target_compile_definitions(jack_alsa PRIVATE RESAMPLE_DISABLE_PRECOMP ${JACK_RESAMPLER_SIMD_DEFINITIONS})


install(TARGETS jack jackserver jackring jacklog jackmidi
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS jackd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# The client API: the jackmini headers (jackmini/...).
install(FILES
    client/jackmini/client.hpp
    client/jackmini/control.hpp
    client/jackmini/error.hpp
    client/jackmini/jackmini.hpp
    client/jackmini/midi.hpp
    client/jackmini/options.hpp
    client/jackmini/port.hpp
    client/jackmini/ringbuffer.hpp
    client/jackmini/transport.hpp
    client/jackmini/uuid.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/jackmini)

# The public headers of the libraries that have an interface of their own: the server control, the MIDI buffer and the
# ring buffer.  The other headers of common/jack are internal.
install(FILES
    common/jack/jslist.h
    common/jack/midiport.h
    common/jack/ringbuffer.h
    common/jack/systemdeps.h
    common/jack/types.h
    common/jack/weakmacros.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/jack)

# The parts of jackmini that need no server: ring buffer, Uuid, error text.  The tools link it too.
add_library(jackmini_base STATIC client/jackmini/uuid.cpp client/jackmini/error.cpp)
target_include_directories(jackmini_base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/client)
target_compile_features(jackmini_base PUBLIC cxx_std_23)

if(JACK_TOOLS)
    add_subdirectory(tools)
endif()

if(JACK_BUILD_TESTS)
    # Benchmarks and interactive test clients, on the jackmini client API.  They need a running server.
    set(JACK_BENCH_PROGRAMS
        jack_cpu:tests/cpu.cpp
        jack_iodelay:tests/iodelay.cpp
        jack_multiple_metro:tests/external_metro.cpp
        jack_rtbench:tests/perf/rtbench.cpp)

    foreach(entry IN LISTS JACK_BENCH_PROGRAMS)
        string(REPLACE ":" ";" parts "${entry}")
        list(GET parts 0 program)
        list(GET parts 1 source)
        add_executable(${program} ${source})
        target_link_libraries(${program} PRIVATE jack jackmini_base Threads::Threads)
        install(TARGETS ${program} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endforeach()

    # Unit tests for the secondary ALSA card building blocks.  They use the
    # real sources, not a library, and need no ALSA hardware.
    enable_testing()
    set(JACK_ALSA_UNIT_SOURCES
        tests/alsa_secondary_unit.cpp
        platform/alsa/JackAlsaRingBuffer.cpp
        platform/alsa/JackAlsaSecondaryConfig.cpp
        platform/alsa/JackAlsaRateConverter.cpp
        platform/alsa/JackAlsaDriftController.cpp
        platform/alsa/resample/JackResampler.cpp
        platform/alsa/resample/pipewire_resample_bridge.c
        platform/alsa/resample/resample-native.c
        platform/alsa/resample/resample-native-c.c
        ${JACK_RESAMPLER_SIMD_SOURCES})
    add_executable(jack_alsa_unit ${JACK_ALSA_UNIT_SOURCES})
    target_include_directories(jack_alsa_unit PRIVATE
        ${JACK_INCLUDE_DIRS}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/alsa"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/alsa/resample"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/alsa/resample/spa-shim")
    target_compile_definitions(jack_alsa_unit PRIVATE
        HAVE_CONFIG_H SERVER_SIDE RESAMPLE_DISABLE_PRECOMP
        ${JACK_RESAMPLER_SIMD_DEFINITIONS})
    target_link_libraries(jack_alsa_unit PRIVATE jackserver m)
    add_test(NAME alsa_secondary_unit COMMAND jack_alsa_unit)

    # The ALSA cycle core on a scripted fake backend: avail, xrun recovery,
    # poll timeout, start/stop sync, and the channel copies.  Header-only.
    add_executable(jack_pcm_cycle_unit tests/pcm_cycle_unit.cpp)
    target_include_directories(jack_pcm_cycle_unit PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/alsa"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_test(NAME pcm_cycle_unit COMMAND jack_pcm_cycle_unit)

    # The realtime message buffer, run against the real singleton and flush thread.
    # The server's internals are hidden in libjackserver, so the test links
    # the server objects directly.
    add_executable(jack_message_unit tests/message_buffer_unit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_link_libraries(jack_message_unit PRIVATE jackring jacklog jackmidi)
    target_include_directories(jack_message_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_message_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_message_unit PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt m)
    add_test(NAME message_buffer_unit COMMAND jack_message_unit)

    add_executable(jack_cstring_unit tests/cstring_unit.cpp)
    target_include_directories(jack_cstring_unit PRIVATE ${JACK_INCLUDE_DIRS})
    add_test(NAME cstring_unit COMMAND jack_cstring_unit)

    # The byte ringbuffer behind jack_ringbuffer_*: sizes, wrap-around, vectors, and a transfer between two threads.
    add_executable(jack_ringbuffer_unit tests/ringbuffer_unit.cpp)
    target_include_directories(jack_ringbuffer_unit PRIVATE ${JACK_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}/common")
    target_link_libraries(jack_ringbuffer_unit PRIVATE jackring Threads::Threads)
    add_test(NAME ringbuffer_unit COMMAND jack_ringbuffer_unit)

    # The sample conversions and the interleaved copies, against the bytes of the C implementation they replaced.
    add_executable(jack_memops_unit tests/memops_unit.cpp common/memops.cpp)
    target_include_directories(jack_memops_unit PRIVATE ${JACK_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}/common")
    target_link_libraries(jack_memops_unit PRIVATE m)
    add_test(NAME memops_unit COMMAND jack_memops_unit)

    # Compile-time checks of the jackmini client API headers.  No implementation is linked.
    add_executable(jack_api_unit tests/jackmini_api_unit.cpp)
    target_include_directories(jack_api_unit PRIVATE ${JACK_INCLUDE_DIRS})
    add_test(NAME jackmini_api_unit COMMAND jack_api_unit)

    # Static layout checks of the shared-memory structures.  Compile-only checks; nothing is linked from the server.
    add_executable(jack_shm_layout_unit tests/shm_layout_unit.cpp)
    target_include_directories(jack_shm_layout_unit PRIVATE ${JACK_INCLUDE_DIRS}
        "${CMAKE_CURRENT_SOURCE_DIR}/platform")
    target_compile_definitions(jack_shm_layout_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    add_test(NAME shm_layout_unit COMMAND jack_shm_layout_unit)

    # Descriptor passing over a Unix socket and anonymous shared-memory segments (P2 step 2).
    add_executable(jack_fd_segment_unit tests/fd_segment_unit.cpp platform/JackFdSegment.cpp)
    target_include_directories(jack_fd_segment_unit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/platform")
    add_test(NAME fd_segment_unit COMMAND jack_fd_segment_unit)

    # Message framing of the control socket: the header, the exact payload length, and the reply writer and reader.
    add_executable(jack_message_frame_unit tests/message_frame_unit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_link_libraries(jack_message_frame_unit PRIVATE jackring jacklog jackmidi)
    target_include_directories(jack_message_frame_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_message_frame_unit PRIVATE HAVE_CONFIG_H)
    add_test(NAME message_frame_unit COMMAND jack_message_frame_unit)

    # Activation counters under concurrency: the cycle and the connection changes (protocol.md section 4.5).  Run it under TSAN.
    add_executable(jack_counter_unit tests/counter_tsan_unit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_link_libraries(jack_counter_unit PRIVATE jackring jacklog jackmidi)
    target_include_directories(jack_counter_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_counter_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_counter_unit PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt m)
    add_test(NAME counter_unit COMMAND jack_counter_unit)

    # Deactivation keeps the connections: silence for an inactive client, and the connection is still there (protocol.md section 6).
    add_executable(jack_deactivate_graph_unit tests/deactivate_graph_unit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_link_libraries(jack_deactivate_graph_unit PRIVATE jackring jacklog jackmidi)
    target_include_directories(jack_deactivate_graph_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_deactivate_graph_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_deactivate_graph_unit PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt m)
    add_test(NAME deactivate_graph_unit COMMAND jack_deactivate_graph_unit)

    # Conformance of the control socket: requests, replies, malformed and version-mismatched messages, with a fake server.
    add_executable(jack_protocol_conformance_unit tests/protocol_conformance_unit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_link_libraries(jack_protocol_conformance_unit PRIVATE jackring jacklog jackmidi)
    target_include_directories(jack_protocol_conformance_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_protocol_conformance_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_protocol_conformance_unit PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt m)
    add_test(NAME protocol_conformance_unit COMMAND jack_protocol_conformance_unit)

    # The client's notification queue on the server when the client does not read: every notification is delivered or
    # counted as dropped.  Uses the server objects with a socket pair.
    add_executable(jack_notify_overflow_unit tests/notify_overflow_unit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_link_libraries(jack_notify_overflow_unit PRIVATE jackring jacklog jackmidi)
    target_include_directories(jack_notify_overflow_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_notify_overflow_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_notify_overflow_unit PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt m)
    add_test(NAME notify_overflow_unit COMMAND jack_notify_overflow_unit)

    add_executable(jack_jackmini_unit tests/jackmini_unit.cpp)
    target_link_libraries(jack_jackmini_unit PRIVATE jackmini_base Threads::Threads)
    add_test(NAME jackmini_unit COMMAND jack_jackmini_unit)

    # The client lifecycle of the jackmini API with a running server.  Not a test: ctest has no server.  Run it after
    # starting one, for example `jackd -d dummy`.
    add_executable(jack_jackmini_client_e2e tests/jackmini_client_e2e.cpp)
    target_include_directories(jack_jackmini_client_e2e PRIVATE ${JACK_INCLUDE_DIRS})
    target_link_libraries(jack_jackmini_client_e2e PRIVATE jack jackmini_base Threads::Threads)

    # The graph of the jackmini API (chains, ports at scale, connection rules, naming, several clients), with a server.
    add_executable(jack_jackmini_graph_e2e tests/jackmini_graph_e2e.cpp)
    target_link_libraries(jack_jackmini_graph_e2e PRIVATE jack jackmini_base Threads::Threads)

    # The client session of the new client code (open, names, close), with a server.
    add_executable(jack_client_session_e2e tests/client_session_e2e.cpp)
    target_include_directories(jack_client_session_e2e PRIVATE ${JACK_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}/client")
    target_link_libraries(jack_client_session_e2e PRIVATE jack jackmini_base Threads::Threads)

    # The port listing (Client::ports, total latency, UUIDs, connections), with a server.
    add_executable(jack_jackmini_ports_e2e tests/jackmini_ports_e2e.cpp)
    target_include_directories(jack_jackmini_ports_e2e PRIVATE ${JACK_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(jack_jackmini_ports_e2e PRIVATE jack jackmini_base Threads::Threads)

endif()

message(STATUS "JACK ${JACK_VERSION}")
message(STATUS "Maximum JACK clients                        : ${JACK_MAX_CLIENTS}")
message(STATUS "Maximum ports per application               : ${JACK_PORTS_PER_APPLICATION}")
message(STATUS "Install prefix                              : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Library directory                           : ${CMAKE_INSTALL_FULL_LIBDIR}")
message(STATUS "Drivers directory                           : ${JACK_ADDON_DIR}")
message(STATUS "Build tests                                 : ${JACK_BUILD_TESTS}")
