# tests — Catch2 v3 via FetchContent, discovered into ctest.

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        ${CLEARSPACE_CATCH2_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   FALSE
    SYSTEM)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

set(TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/JuceTestInit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_smoke.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_parameters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_render_roundtrip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ducker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_routing.cpp)

# A JUCE console app so the message manager / leak detector are set up the
# same way as in tools/render.
juce_add_console_app(ClearSpaceTests PRODUCT_NAME "ClearSpaceTests")
target_sources(ClearSpaceTests PRIVATE ${TEST_SOURCES})
target_link_libraries(ClearSpaceTests PRIVATE renderlib Catch2::Catch2WithMain)
target_compile_definitions(ClearSpaceTests PRIVATE
    CLEARSPACE_TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/scratch")
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/scratch)

include(Catch)
catch_discover_tests(ClearSpaceTests)

set(CLEARSPACE_OWN_SOURCES ${CLEARSPACE_OWN_SOURCES} ${TEST_SOURCES} PARENT_SCOPE)
