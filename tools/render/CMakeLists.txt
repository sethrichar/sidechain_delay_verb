# tools/render — offline renderer: runs ClearSpaceProcessor on WAV or synthetic input.
# Split into a static library (used by the tests) and a thin CLI.

set(RENDER_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Sources.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Stats.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/WavIO.cpp)
set(RENDER_CLI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

# Library is INTERFACE for the same reason as ClearSpaceCore: every consumer
# compiles the JUCE-dependent sources with its own JUCE configuration.
add_library(renderlib INTERFACE)
target_sources(renderlib INTERFACE ${RENDER_LIB_SOURCES})
target_include_directories(renderlib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(renderlib INTERFACE ClearSpaceCore)

juce_add_console_app(render PRODUCT_NAME "render")
target_sources(render PRIVATE ${RENDER_CLI_SOURCES})
target_link_libraries(render PRIVATE renderlib)

# Export our source list so the top-level -Werror rule can find these files.
set(CLEARSPACE_OWN_SOURCES ${CLEARSPACE_OWN_SOURCES} ${RENDER_LIB_SOURCES} ${RENDER_CLI_SOURCES} PARENT_SCOPE)
