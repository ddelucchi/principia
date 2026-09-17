add_library(principia_progression STATIC src/learning_graph.cpp)
add_library(Principia::Progression ALIAS principia_progression)

target_include_directories(
    principia_progression
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_progression
    PUBLIC
        Principia::Core
        Principia::Ontology
        Principia::State
    PRIVATE
        $<BUILD_INTERFACE:Principia::ProjectOptions>
)
