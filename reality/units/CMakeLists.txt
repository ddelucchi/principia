add_library(principia_units INTERFACE)
add_library(Principia::Units ALIAS principia_units)

target_include_directories(
    principia_units
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_units
    INTERFACE
        $<BUILD_INTERFACE:Principia::ProjectOptions>
        $<BUILD_INTERFACE:mp-units::mp-units>
)
