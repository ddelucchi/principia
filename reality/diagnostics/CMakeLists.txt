add_library(principia_diagnostics INTERFACE)
add_library(Principia::Diagnostics ALIAS principia_diagnostics)

target_include_directories(
    principia_diagnostics
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_diagnostics
    INTERFACE
        Principia::Boundaries
        Principia::Fields
        Principia::Materials
        Principia::Math
        Principia::Spacetime
        Principia::Units
        Principia::World
        $<BUILD_INTERFACE:Principia::ProjectOptions>
)
