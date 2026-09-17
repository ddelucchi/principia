add_library(principia_replay STATIC src/replay.cpp)
add_library(Principia::Replay ALIAS principia_replay)

target_include_directories(
    principia_replay
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_replay
    PUBLIC
        Principia::Operators
        Principia::Serialization
        Principia::Solvers
        Principia::World
    PRIVATE
        Principia::Fields
        $<BUILD_INTERFACE:Principia::ProjectOptions>
        Principia::Units
)
