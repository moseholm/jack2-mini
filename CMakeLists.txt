cmake_minimum_required(VERSION 3.16)

project(jack2 VERSION 1.9.23 LANGUAGES C CXX)

set(JACK_API_VERSION "0.1.0")

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This JACK build supports Linux only.")
endif()

include(GNUInstallDirs)

option(JACK_BUILD_TESTS "Build test programs" OFF)
set(JACK_MAX_CLIENTS 256 CACHE STRING "Maximum number of JACK clients")
set(JACK_PORTS_PER_APPLICATION 2048 CACHE STRING "Maximum number of ports per application")

find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa>=1.0.18)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

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
    common/shm.c
    common/JackGlobals.cpp
    common/JackTransportEngine.cpp
    common/JackTools.cpp
    common/JackMessageBuffer.cpp
    common/promiscuous.c
    posix/JackPosixThread.cpp
    posix/JackPosixProcessSync.cpp
    posix/JackPosixMutex.cpp
    posix/JackSocket.cpp
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
    posix/JackSocketServerNotifyChannel.cpp)

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

add_library(jackserver SHARED ${JACK_SERVER_SOURCES})
target_include_directories(jackserver PRIVATE ${JACK_INCLUDE_DIRS})
target_compile_definitions(jackserver PRIVATE HAVE_CONFIG_H SERVER_SIDE)
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
    linux/alsa/resample/JackResampler.cpp
    linux/alsa/resample/pipewire_resample_bridge.c
    linux/alsa/resample/resample-native.c
    linux/alsa/resample/resample-native-c.c)

set(JACK_ALSARAWMIDI_SOURCES
    linux/alsarawmidi/JackALSARawMidiDriver.cpp
    linux/alsarawmidi/JackALSARawMidiInputPort.cpp
    linux/alsarawmidi/JackALSARawMidiOutputPort.cpp
    linux/alsarawmidi/JackALSARawMidiPort.cpp
    linux/alsarawmidi/JackALSARawMidiReceiveQueue.cpp
    linux/alsarawmidi/JackALSARawMidiSendQueue.cpp
    linux/alsarawmidi/JackALSARawMidiUtil.cpp)

jack_add_driver(alsa ${JACK_ALSA_SOURCES})
target_link_libraries(jack_alsa PRIVATE PkgConfig::ALSA m)
# The vendored PipeWire resampler is built with its precomputed filter tables
# disabled and a small SPA compatibility shim in place of the SPA headers.
target_include_directories(jack_alsa PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/linux/alsa/resample"
    "${CMAKE_CURRENT_SOURCE_DIR}/linux/alsa/resample/spa-shim")
target_compile_definitions(jack_alsa PRIVATE RESAMPLE_DISABLE_PRECOMP)

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
endif()

message(STATUS "JACK ${JACK_VERSION}")
message(STATUS "Maximum JACK clients                        : ${JACK_MAX_CLIENTS}")
message(STATUS "Maximum ports per application               : ${JACK_PORTS_PER_APPLICATION}")
message(STATUS "Install prefix                              : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Library directory                           : ${CMAKE_INSTALL_FULL_LIBDIR}")
message(STATUS "Drivers directory                           : ${JACK_ADDON_DIR}")
message(STATUS "Build tests                                 : ${JACK_BUILD_TESTS}")
