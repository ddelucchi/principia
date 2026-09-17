add_executable(
    principia_demo
    renderer/cpu_canvas.cpp
    renderer/cpu_canvas.hpp
    renderer/demo_app.cpp
    renderer/demo_app.hpp
    renderer/gpu_window.cpp
    renderer/gpu_window.hpp
    renderer/main.cpp
    renderer/world_view.cpp
    renderer/world_view.hpp
)
target_link_libraries(
    principia_demo
    PRIVATE
        Principia::Boundaries
        Principia::Diagnostics
        Principia::Fields
        Principia::Game
        Principia::Operators
        Principia::Progression
        Principia::Solvers
        Principia::Spacetime
        Principia::Units
        Principia::World
        SDL3::SDL3
        Principia::ProjectOptions
)
target_compile_definitions(principia_demo PRIVATE SDL_MAIN_HANDLED)

if(BUILD_TESTING)
    add_executable(
        principia_cpu_canvas_tests
        renderer/cpu_canvas.cpp
        renderer/cpu_canvas.hpp
        ../tests/presentation/test_cpu_canvas.cpp
    )
    target_include_directories(
        principia_cpu_canvas_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/renderer
    )
    target_link_libraries(principia_cpu_canvas_tests PRIVATE Principia::ProjectOptions)
    add_test(NAME principia_cpu_canvas_tests COMMAND principia_cpu_canvas_tests)

    add_executable(
        principia_world_view_tests
        renderer/cpu_canvas.cpp
        renderer/cpu_canvas.hpp
        renderer/world_view.cpp
        renderer/world_view.hpp
        ../tests/presentation/test_world_view.cpp
    )
    target_include_directories(
        principia_world_view_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/renderer
    )
    target_link_libraries(
        principia_world_view_tests
        PRIVATE
            Principia::Boundaries
            Principia::Diagnostics
            Principia::Fields
            Principia::Game
            Principia::Operators
            Principia::Spacetime
            Principia::Units
            Principia::World
            Principia::ProjectOptions
    )
    add_test(NAME principia_world_view_tests COMMAND principia_world_view_tests)

    add_test(
        NAME principia_demo_rejects_invalid_arguments
        COMMAND
            ${CMAKE_COMMAND}
            "-DPRINCIPIA_DEMO=$<TARGET_FILE:principia_demo>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../tests/presentation/check_rejected_cli.cmake"
    )
endif()
