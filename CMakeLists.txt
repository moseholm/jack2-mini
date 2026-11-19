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
option(JACK_REALTIME_SANITIZER "Build with Clang RealtimeSanitizer (-fsanitize=realtime)" OFF)
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
    # An RTSan-instrumented shared object keeps an unresolved
    # __rtsan_ensure_initialized, resolved from the runtime linked into the
    # executable that loads it (including dlopen'd drivers).
    if(NOT JACK_REALTIME_SANITIZER)
        add_link_options(-Wl,--no-undefined)
    endif()
endif()

if(JACK_REALTIME_SANITIZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "JACK_REALTIME_SANITIZER needs Clang. Reconfigure with CC=clang CXX=clang++.")
    endif()
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20)
        message(FATAL_ERROR
            "JACK_REALTIME_SANITIZER needs Clang 20 or newer (found ${CMAKE_CXX_COMPILER_VERSION}).")
    endif()

    # RTSan only reports inside functions marked [[clang::nonblocking]], and the
    # runtime must be present at startup for dlopen'd drivers to be checked.
    # Prove both here: an un-instrumented build would otherwise pass CI silently.
    set(JACK_RTSAN_PROBE_DIR "${CMAKE_CURRENT_BINARY_DIR}/rtsan-probe")
    file(MAKE_DIRECTORY "${JACK_RTSAN_PROBE_DIR}")
    file(WRITE "${JACK_RTSAN_PROBE_DIR}/main.cpp" [=[
extern "C" void* malloc(unsigned long);
extern "C" void free(void*);
void violation() [[clang::nonblocking]] {
    void* p = malloc(8);
    free(p);
}
int main() { violation(); return 0; }
]=])
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -std=c++17 -fsanitize=realtime -g
            "${JACK_RTSAN_PROBE_DIR}/main.cpp" -o "${JACK_RTSAN_PROBE_DIR}/probe"
        RESULT_VARIABLE JACK_RTSAN_COMPILE_RESULT
        OUTPUT_VARIABLE JACK_RTSAN_COMPILE_OUTPUT
        ERROR_VARIABLE JACK_RTSAN_COMPILE_OUTPUT)
    if(NOT JACK_RTSAN_COMPILE_RESULT EQUAL 0)
        message(FATAL_ERROR "RealtimeSanitizer probe did not build:\n${JACK_RTSAN_COMPILE_OUTPUT}")
    endif()
    execute_process(
        COMMAND "${JACK_RTSAN_PROBE_DIR}/probe"
        RESULT_VARIABLE JACK_RTSAN_RUN_RESULT
        ERROR_VARIABLE JACK_RTSAN_RUN_OUTPUT)
    if(JACK_RTSAN_RUN_RESULT EQUAL 0 OR NOT JACK_RTSAN_RUN_OUTPUT MATCHES "RealtimeSanitizer")
        message(FATAL_ERROR
            "RealtimeSanitizer probe did not report a violation (status ${JACK_RTSAN_RUN_RESULT}):\n"
            "${JACK_RTSAN_RUN_OUTPUT}")
    endif()
    message(STATUS "RealtimeSanitizer                         : enabled (Clang ${CMAKE_CXX_COMPILER_VERSION})")

    # Link options as well as compile options: the runtime has to be loaded by
    # jackd and by each client executable before any driver is dlopen'd.
    add_compile_options(-fsanitize=realtime -fno-omit-frame-pointer)
    add_link_options(-fsanitize=realtime)
    add_compile_definitions(JACK_REALTIME_SANITIZER=1)
    # Function-effect analysis is now mandatory: every call from an annotated
    # realtime function is either verified nonblocking or carries a documented
    # JACK_RT_UNCHECKED exemption.
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Werror=function-effects>)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-perf-constraint-implies-noexcept>)
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
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/os"
    "${CMAKE_CURRENT_SOURCE_DIR}/client"
    "${CMAKE_CURRENT_SOURCE_DIR}/ipc/memory"
    "${CMAKE_CURRENT_SOURCE_DIR}/ipc/protocol"
    "${CMAKE_CURRENT_SOURCE_DIR}/ipc/socket"
    "${CMAKE_CURRENT_SOURCE_DIR}/ipc/sync"
    "${CMAKE_CURRENT_SOURCE_DIR}/server"
    "${CMAKE_CURRENT_SOURCE_DIR}/server/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/server/drivers"
    "${CMAKE_CURRENT_SOURCE_DIR}/server/drivers/alsa"
    "${CMAKE_CURRENT_SOURCE_DIR}/server/ipc"
    "${CMAKE_CURRENT_BINARY_DIR}")

set(JACK_COMMON_SOURCES
    ipc/memory/ActivationCount.cpp
    ipc/memory/ConnectionManager.cpp
    common/Error.cpp
    common/Exception.cpp
    ipc/memory/FrameTimer.cpp
    ipc/memory/GraphManager.cpp
    ipc/memory/ServerPort.cpp
    ipc/memory/PortType.cpp
    ipc/memory/AudioPort.cpp
    ipc/memory/EngineControl.cpp
    ipc/memory/ShmMem.cpp
    common/Globals.cpp
    ipc/memory/TransportEngine.cpp
    common/Tools.cpp
    common/Uuid.cpp
    common/MessageBuffer.cpp
    common/os/PosixThread.cpp
    common/os/PosixProcessSync.cpp
    common/os/PosixMutex.cpp
    common/os/LinuxTime.cpp
    ipc/socket/Promiscuous.cpp
    ipc/socket/Socket.cpp
    ipc/memory/FdSegment.cpp
    ipc/sync/LinuxFutex.cpp)

set(JACK_CLIENT_SOURCES
    ${JACK_COMMON_SOURCES}
    client/api/Client.cpp
    client/session/ProcessContext.cpp
    client/session/ClientSession.cpp)

set(JACK_SERVER_SOURCES
    ${JACK_COMMON_SOURCES}
    server/drivers/AudioDriver.cpp
    server/drivers/TimedDriver.cpp
    server/drivers/Driver.cpp
    server/core/Engine.cpp
    server/core/ExternalClient.cpp
    server/drivers/FreewheelDriver.cpp
    server/core/ServerCore.cpp
    server/drivers/ThreadedDriver.cpp
    server/drivers/DriverLoader.cpp
    server/core/ServerGlobals.cpp
    server/control/Control.cpp
    server/ipc/RequestDecoder.cpp
    server/ipc/SocketServerChannel.cpp
    server/ipc/SocketNotifyChannel.cpp
    server/ipc/SocketServerNotifyChannel.cpp
    server/core/ServerLock.cpp)

# The MIDI buffer (the format of the MIDI data in the port buffers, and the jack_midi_* functions that the drivers use).
# Built with the server definitions: its buffer methods are used by the server and by the client library.
add_library(jackmidi SHARED common/MidiPort.cpp common/MidiAPI.cpp)
target_include_directories(jackmidi PRIVATE ${JACK_INCLUDE_DIRS})
target_compile_definitions(jackmidi PRIVATE HAVE_CONFIG_H SERVER_SIDE)
target_link_libraries(jackmidi PRIVATE jacklog)
set_target_properties(jackmidi PROPERTIES VERSION "${JACK_API_VERSION}" SOVERSION 0)

# The basic logging (errors and messages, and where they go): a library of its own, with no dependencies.
add_library(jacklog SHARED common/Log.cpp)
target_include_directories(jacklog PRIVATE ${JACK_INCLUDE_DIRS})
set_target_properties(jacklog PROPERTIES VERSION "${JACK_API_VERSION}" SOVERSION 0)

add_library(jack SHARED ${JACK_CLIENT_SOURCES})
target_include_directories(jack PRIVATE ${JACK_INCLUDE_DIRS})
target_compile_definitions(jack PRIVATE HAVE_CONFIG_H)
target_link_libraries(jack PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt jacklog jackmidi)
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
target_link_libraries(jackserver PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt jacklog jackmidi)
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

jack_add_driver(dummy server/drivers/DummyDriver.cpp)

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
    list(APPEND JACK_RESAMPLER_SIMD_SOURCES server/drivers/alsa/resample/resample-native-neon.c)
    list(APPEND JACK_RESAMPLER_SIMD_DEFINITIONS HAVE_NEON)
    set(JACK_RESAMPLER_SIMD_ENABLED ON)
elseif(JACK_RESAMPLER_SIMD AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|i[3-6]86)$")
    check_c_compiler_flag("-msse" JACK_HAVE_MSSE)
    if(JACK_HAVE_MSSE)
        list(APPEND JACK_RESAMPLER_SIMD_SOURCES server/drivers/alsa/resample/resample-native-sse.c)
        list(APPEND JACK_RESAMPLER_SIMD_DEFINITIONS HAVE_SSE)
        set_source_files_properties(server/drivers/alsa/resample/resample-native-sse.c
            PROPERTIES COMPILE_OPTIONS "-msse")
        set(JACK_RESAMPLER_SIMD_ENABLED ON)
    endif()

    check_c_compiler_flag("-mavx2" JACK_HAVE_MAVX2)
    check_c_compiler_flag("-mfma" JACK_HAVE_MFMA)
    if(JACK_HAVE_MAVX2 AND JACK_HAVE_MFMA)
        list(APPEND JACK_RESAMPLER_SIMD_SOURCES server/drivers/alsa/resample/resample-native-avx2.c)
        list(APPEND JACK_RESAMPLER_SIMD_DEFINITIONS HAVE_AVX2 HAVE_FMA)
        set_source_files_properties(server/drivers/alsa/resample/resample-native-avx2.c
            PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
        set(JACK_RESAMPLER_SIMD_ENABLED ON)
    endif()
endif()
message(STATUS "Resampler SIMD kernels                    : ${JACK_RESAMPLER_SIMD_ENABLED}")

set(JACK_ALSA_SOURCES
    server/drivers/Memops.cpp
    server/drivers/alsa/AlsaDriver.cpp
    server/drivers/alsa/PcmAlsa.cpp
    server/drivers/alsa/PcmSession.cpp
    server/drivers/alsa/AlsaRingBuffer.cpp
    server/drivers/alsa/AlsaSecondaryConfig.cpp
    server/drivers/alsa/AlsaSecondaryCard.cpp
    server/drivers/alsa/AlsaRateConverter.cpp
    server/drivers/alsa/AlsaDriftController.cpp
    server/drivers/alsa/AlsaControl.cpp
    server/drivers/alsa/AlsaControlMonitor.cpp
    server/drivers/alsa/AlsaReconfigWorker.cpp
    server/drivers/alsa/AlsaEnumerate.cpp
    server/drivers/alsa/AlsaHotplug.cpp
    server/drivers/alsa/resample/Resampler.cpp
    server/drivers/alsa/resample/pipewire_resample_bridge.c
    server/drivers/alsa/resample/resample-native.c
    server/drivers/alsa/resample/resample-native-c.c
    ${JACK_RESAMPLER_SIMD_SOURCES})

jack_add_driver(alsa ${JACK_ALSA_SOURCES})
target_link_libraries(jack_alsa PRIVATE PkgConfig::ALSA PkgConfig::UDEV m ${CMAKE_DL_LIBS} jacklog jackmidi)
# The vendored PipeWire resampler is built with its precomputed filter tables
# disabled and a small SPA compatibility shim in place of the SPA headers.
target_include_directories(jack_alsa PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/server/drivers/alsa/resample"
    "${CMAKE_CURRENT_SOURCE_DIR}/server/drivers/alsa/resample/spa-shim")
target_compile_definitions(jack_alsa PRIVATE RESAMPLE_DISABLE_PRECOMP ${JACK_RESAMPLER_SIMD_DEFINITIONS})


install(TARGETS jack jackserver jacklog jackmidi
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS jackd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Public C++ headers.  The installed paths mirror the source tree, so an
# integrator writes <client/api/Jackmini.hpp> or <server/control/Control.hpp>.
install(FILES
    client/api/Client.hpp
    client/api/Error.hpp
    client/api/Jackmini.hpp
    client/api/Midi.hpp
    client/api/Options.hpp
    client/api/Port.hpp
    client/api/Ringbuffer.hpp
    client/api/Transport.hpp
    client/api/Uuid.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/client/api)

# client/api/Uuid.hpp forwards to this one definition of the public UUID type.
install(FILES common/Uuid.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/common)

# Server management is not part of the client API; only embedders need it.
install(FILES
    server/control/Control.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/server/control)

# The parts of the client API that need no server: Uuid and error text.  The tools link it too.
add_library(jackmini_base STATIC common/Uuid.cpp client/api/Error.cpp)
target_include_directories(jackmini_base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(jackmini_base PUBLIC cxx_std_23)

if(JACK_TOOLS)
    add_subdirectory(tools)
endif()

if(JACK_BUILD_TESTS)
    # Benchmarks and interactive test clients, on the jackmini client API.  They need a running server.
    set(JACK_BENCH_PROGRAMS
        jack_cpu:tests/Cpu.cpp
        jack_iodelay:tests/Iodelay.cpp
        jack_multiple_metro:tests/ExternalMetro.cpp
        jack_rtbench:tests/perf/Rtbench.cpp)

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
        tests/AlsaSecondaryUnit.cpp
        server/drivers/alsa/AlsaRingBuffer.cpp
        server/drivers/alsa/AlsaSecondaryConfig.cpp
        server/drivers/alsa/AlsaRateConverter.cpp
        server/drivers/alsa/AlsaDriftController.cpp
        server/drivers/alsa/resample/Resampler.cpp
        server/drivers/alsa/resample/pipewire_resample_bridge.c
        server/drivers/alsa/resample/resample-native.c
        server/drivers/alsa/resample/resample-native-c.c
        ${JACK_RESAMPLER_SIMD_SOURCES})
    add_executable(jack_alsa_unit ${JACK_ALSA_UNIT_SOURCES})
    target_include_directories(jack_alsa_unit PRIVATE
        ${JACK_INCLUDE_DIRS}
        "${CMAKE_CURRENT_SOURCE_DIR}/server/drivers/alsa/resample"
        "${CMAKE_CURRENT_SOURCE_DIR}/server/drivers/alsa/resample/spa-shim")
    target_compile_definitions(jack_alsa_unit PRIVATE
        HAVE_CONFIG_H SERVER_SIDE RESAMPLE_DISABLE_PRECOMP
        ${JACK_RESAMPLER_SIMD_DEFINITIONS})
    target_link_libraries(jack_alsa_unit PRIVATE jackserver m)
    add_test(NAME alsa_secondary_unit COMMAND jack_alsa_unit)

    # The ALSA cycle core on a scripted fake backend: avail, xrun recovery,
    # poll timeout, start/stop sync, and the channel copies.  Header-only.
    add_executable(jack_pcm_cycle_unit tests/PcmCycleUnit.cpp)
    target_include_directories(jack_pcm_cycle_unit PRIVATE ${JACK_INCLUDE_DIRS}
        "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_test(NAME pcm_cycle_unit COMMAND jack_pcm_cycle_unit)

    # The realtime message buffer, run against the real singleton and flush thread.
    # The server's internals are hidden in libjackserver, so the test links
    # the server objects directly.
    add_executable(jack_message_unit tests/MessageBufferUnit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_link_libraries(jack_message_unit PRIVATE jacklog jackmidi)
    target_include_directories(jack_message_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_message_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_message_unit PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt m)
    add_test(NAME message_buffer_unit COMMAND jack_message_unit)

    add_executable(jack_cstring_unit tests/CstringUnit.cpp)
    target_include_directories(jack_cstring_unit PRIVATE ${JACK_INCLUDE_DIRS})
    add_test(NAME cstring_unit COMMAND jack_cstring_unit)

    # The sample conversions and the interleaved copies, against the bytes of the C implementation they replaced.
    add_executable(jack_memops_unit tests/MemopsUnit.cpp server/drivers/Memops.cpp)
    target_include_directories(jack_memops_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_link_libraries(jack_memops_unit PRIVATE m)
    add_test(NAME memops_unit COMMAND jack_memops_unit)

    # Compile-time checks of the jackmini client API headers.  No implementation is linked.
    add_executable(jack_api_unit tests/JackminiApiUnit.cpp)
    target_include_directories(jack_api_unit PRIVATE ${JACK_INCLUDE_DIRS})
    add_test(NAME jackmini_api_unit COMMAND jack_api_unit)

    # Static layout checks of the shared-memory structures.  Compile-only checks; nothing is linked from the server.
    add_executable(jack_shm_layout_unit tests/ShmLayoutUnit.cpp)
    target_include_directories(jack_shm_layout_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_shm_layout_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    add_test(NAME shm_layout_unit COMMAND jack_shm_layout_unit)

    # Descriptor passing over a Unix socket and anonymous shared-memory segments (P2 step 2).
    add_executable(jack_fd_segment_unit tests/FdSegmentUnit.cpp ipc/memory/FdSegment.cpp)
    target_include_directories(jack_fd_segment_unit PRIVATE ${JACK_INCLUDE_DIRS})
    add_test(NAME fd_segment_unit COMMAND jack_fd_segment_unit)

    # Message framing of the control socket: the header, the exact payload length, and the reply writer and reader.
    add_executable(jack_message_frame_unit tests/MessageFrameUnit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_link_libraries(jack_message_frame_unit PRIVATE jacklog jackmidi)
    target_include_directories(jack_message_frame_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_message_frame_unit PRIVATE HAVE_CONFIG_H)
    add_test(NAME message_frame_unit COMMAND jack_message_frame_unit)

    # Activation counters under concurrency: the cycle and the connection changes (protocol.md section 4.5).  Run it under TSAN.
    add_executable(jack_counter_unit tests/CounterTsanUnit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_link_libraries(jack_counter_unit PRIVATE jacklog jackmidi)
    target_include_directories(jack_counter_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_counter_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_counter_unit PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt m)
    add_test(NAME counter_unit COMMAND jack_counter_unit)

    # Deactivation keeps the connections: silence for an inactive client, and the connection is still there (protocol.md section 6).
    add_executable(jack_deactivate_graph_unit tests/DeactivateGraphUnit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_link_libraries(jack_deactivate_graph_unit PRIVATE jacklog jackmidi)
    target_include_directories(jack_deactivate_graph_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_deactivate_graph_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_deactivate_graph_unit PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt m)
    add_test(NAME deactivate_graph_unit COMMAND jack_deactivate_graph_unit)

    # Conformance of the control socket: requests, replies, malformed and version-mismatched messages, with a fake server.
    add_executable(jack_protocol_conformance_unit tests/ProtocolConformanceUnit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_link_libraries(jack_protocol_conformance_unit PRIVATE jacklog jackmidi)
    target_include_directories(jack_protocol_conformance_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_protocol_conformance_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_protocol_conformance_unit PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt m)
    add_test(NAME protocol_conformance_unit COMMAND jack_protocol_conformance_unit)

    # The client's notification queue on the server when the client does not read: every notification is delivered or
    # counted as dropped.  Uses the server objects with a socket pair.
    add_executable(jack_notify_overflow_unit tests/NotifyOverflowUnit.cpp $<TARGET_OBJECTS:jackserver_objects>)
    target_link_libraries(jack_notify_overflow_unit PRIVATE jacklog jackmidi)
    target_include_directories(jack_notify_overflow_unit PRIVATE ${JACK_INCLUDE_DIRS})
    target_compile_definitions(jack_notify_overflow_unit PRIVATE HAVE_CONFIG_H SERVER_SIDE)
    target_link_libraries(jack_notify_overflow_unit PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt m)
    add_test(NAME notify_overflow_unit COMMAND jack_notify_overflow_unit)

    add_executable(jack_jackmini_unit tests/JackminiUnit.cpp)
    target_link_libraries(jack_jackmini_unit PRIVATE jackmini_base Threads::Threads)
    add_test(NAME jackmini_unit COMMAND jack_jackmini_unit)

    # The client lifecycle of the jackmini API with a running server.  Not a test: ctest has no server.  Run it after
    # starting one, for example `jackd -d dummy`.
    add_executable(jack_jackmini_client_e2e tests/JackminiClientE2e.cpp)
    target_include_directories(jack_jackmini_client_e2e PRIVATE ${JACK_INCLUDE_DIRS})
    target_link_libraries(jack_jackmini_client_e2e PRIVATE jack jackmini_base Threads::Threads)

    # The graph of the jackmini API (chains, ports at scale, connection rules, naming, several clients), with a server.
    add_executable(jack_jackmini_graph_e2e tests/JackminiGraphE2e.cpp)
    target_link_libraries(jack_jackmini_graph_e2e PRIVATE jack jackmini_base Threads::Threads)

    # The client session of the new client code (open, names, close), with a server.
    add_executable(jack_client_session_e2e tests/ClientSessionE2e.cpp)
    target_include_directories(jack_client_session_e2e PRIVATE ${JACK_INCLUDE_DIRS})
    target_link_libraries(jack_client_session_e2e PRIVATE jack jackmini_base Threads::Threads)

    # A MIDI source for the tests of jackmini_midi_dump: a note on in every cycle, until it is stopped.
    add_executable(jack_midi_writer tests/MidiWriter.cpp)
    target_link_libraries(jack_midi_writer PRIVATE jack jackmini_base Threads::Threads)

    # The port listing (Client::ports, total latency, UUIDs, connections), with a server.
    add_executable(jack_jackmini_ports_e2e tests/JackminiPortsE2e.cpp)
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
message(STATUS "RealtimeSanitizer                           : ${JACK_REALTIME_SANITIZER}")
