add_library(
    principia_ontology
    STATIC
        src/dot.cpp
        src/registry.cpp
        src/standard_registry.cpp
)
add_library(Principia::Ontology ALIAS principia_ontology)

target_include_directories(
    principia_ontology
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_ontology
    PUBLIC
        Principia::Core
        Principia::State
    PRIVATE
        $<BUILD_INTERFACE:Principia::ProjectOptions>
)
