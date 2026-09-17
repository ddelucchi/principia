add_library(principia_boundaries INTERFACE)
add_library(Principia::Boundaries ALIAS principia_boundaries)

target_include_directories(
    principia_boundaries
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_boundaries
    INTERFACE
        Principia::Core
        Principia::Spacetime
        Principia::State
        Principia::Units
        $<BUILD_INTERFACE:Principia::ProjectOptions>
)
