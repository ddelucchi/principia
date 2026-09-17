add_executable(
    principia_tests
    test_main.cpp
    unit/test_foundation.cpp
    unit/test_progression_and_audits.cpp
    unit/test_replay.cpp
    unit/test_solver_contracts.cpp
    physics/test_reality_001.cpp
    physics/test_reality_002_contact.cpp
)
target_link_libraries(
    principia_tests
    PRIVATE
        Principia::Game
        Principia::Ontology
        Principia::Operators
        Principia::Progression
        Principia::Replay
        Principia::Scheduler
        Principia::Serialization
        Principia::Solvers
        Principia::State
        Principia::World
        Principia::ProjectOptions
)

add_test(NAME principia_tests COMMAND principia_tests)

add_executable(
    principia_persistence_edge_tests
    unit/test_persistence_edge_cases.cpp
)
target_link_libraries(
    principia_persistence_edge_tests
    PRIVATE
        Principia::Replay
        Principia::Serialization
        Principia::ProjectOptions
)
add_test(NAME principia_persistence_edge_tests COMMAND principia_persistence_edge_tests)

add_executable(
    principia_checkpoint_v2_tests
    unit/test_checkpoint_v2.cpp
)
target_link_libraries(
    principia_checkpoint_v2_tests
    PRIVATE
        Principia::Serialization
        Principia::Solvers
        Principia::ProjectOptions
)
add_test(NAME principia_checkpoint_v2_tests COMMAND principia_checkpoint_v2_tests)

add_executable(
    principia_contact_system_tests
    physics/test_frictionless_contact_system.cpp
)
target_link_libraries(
    principia_contact_system_tests
    PRIVATE
        Principia::Solvers
        Principia::ProjectOptions
)
add_test(NAME principia_contact_system_tests COMMAND principia_contact_system_tests)

add_executable(
    principia_progression_permission_tests
    unit/test_progression_permissions.cpp
)
target_link_libraries(
    principia_progression_permission_tests
    PRIVATE
        Principia::Progression
        Principia::ProjectOptions
)
add_test(NAME principia_progression_permission_tests COMMAND principia_progression_permission_tests)

if(PRINCIPIA_BUILD_TOOLS)
    add_test(NAME reality_test_001_inspector COMMAND principia_inspect)
endif()
