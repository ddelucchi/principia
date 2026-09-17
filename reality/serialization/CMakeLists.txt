add_library(
    principia_serialization
    STATIC
        src/checkpoint.cpp
        src/snapshot.cpp
)
add_library(Principia::Serialization ALIAS principia_serialization)

target_include_directories(
    principia_serialization
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_serialization
    PUBLIC
        Principia::Core
        Principia::Operators
        Principia::Solvers
        Principia::Units
        Principia::World
    PRIVATE
        $<BUILD_INTERFACE:Principia::ProjectOptions>
)
