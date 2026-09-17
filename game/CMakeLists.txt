add_subdirectory(progression)

add_library(principia_game STATIC entities/src/reality_test_001.cpp)
add_library(Principia::Game ALIAS principia_game)

target_include_directories(
    principia_game
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/entities/include>
)
target_link_libraries(
    principia_game
    PUBLIC
        Principia::Boundaries
        Principia::Materials
        Principia::Operators
        Principia::World
    PRIVATE
        $<BUILD_INTERFACE:Principia::ProjectOptions>
)
