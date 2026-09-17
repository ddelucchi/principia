add_library(
    principia_scheduler
    STATIC
        src/solver_contract.cpp
        src/solver_graph.cpp
)
add_library(Principia::Scheduler ALIAS principia_scheduler)

target_include_directories(
    principia_scheduler
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_scheduler
    PUBLIC
        Principia::Core
        Principia::Ontology
        Principia::Spacetime
        Principia::State
        Principia::Units
    PRIVATE
        $<BUILD_INTERFACE:Principia::ProjectOptions>
)

if(BUILD_TESTING)
    add_executable(principia_scheduler_graph_tests tests/solver_graph_tests.cpp)
    target_link_libraries(
        principia_scheduler_graph_tests
        PRIVATE
            Principia::Scheduler
            Principia::ProjectOptions
    )
    add_test(NAME principia_scheduler_graph_tests COMMAND principia_scheduler_graph_tests)

    add_executable(principia_scheduler_contract_tests tests/solver_contract_tests.cpp)
    target_link_libraries(
        principia_scheduler_contract_tests
        PRIVATE
            Principia::Scheduler
            Principia::ProjectOptions
    )
    add_test(NAME principia_scheduler_contract_tests COMMAND principia_scheduler_contract_tests)
endif()
