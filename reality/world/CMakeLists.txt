add_library(principia_world INTERFACE)
add_library(Principia::World ALIAS principia_world)

target_include_directories(
    principia_world
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_world
    INTERFACE
        Principia::Boundaries
        Principia::Core
        Principia::Materials
        Principia::Math
        Principia::Ontology
        Principia::Spacetime
        Principia::Units
)
