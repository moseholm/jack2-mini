# The command-line tools, on the jackmini client API.  One program per tool, named jackmini_<tool>, built from one source
# file per tool (lsp.cpp builds jackmini_lsp).  Each tool links the jack library and jackmini_base, and uses only the
# headers in client/jackmini.  The tools are installed with the other binaries.
#
# A tool is added with add_jackmini_tool(<name> <source>...).  The name is the program name.
function(add_jackmini_tool name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE jack jackmini_base Threads::Threads)
    target_compile_definitions(${name} PRIVATE JACK_VERSION="${JACK_VERSION}")
    install(TARGETS ${name} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endfunction()

add_jackmini_tool(jackmini_lsp lsp.cpp)

# jackmini_connect and jackmini_disconnect: one source file, the disconnect program defines JACKMINI_DISCONNECT.
add_jackmini_tool(jackmini_connect connect.cpp)
add_jackmini_tool(jackmini_disconnect connect.cpp)
target_compile_definitions(jackmini_disconnect PRIVATE JACKMINI_DISCONNECT)

add_jackmini_tool(jackmini_samplerate samplerate.cpp)
add_jackmini_tool(jackmini_wait wait.cpp)

add_jackmini_tool(jackmini_midi_dump midi_dump.cpp)
