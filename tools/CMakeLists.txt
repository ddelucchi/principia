add_executable(principia_inspect world_inspector/main.cpp)
target_link_libraries(
    principia_inspect
    PRIVATE
        Principia::Game
        Principia::Diagnostics
        Principia::Solvers
        Principia::Units
        Principia::ProjectOptions
)

add_executable(principia_theory_graph theory_graph/main.cpp)
target_link_libraries(
    principia_theory_graph
    PRIVATE
        Principia::Ontology
        Principia::ProjectOptions
)
