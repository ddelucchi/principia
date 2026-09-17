add_library(principia_conservation INTERFACE)
add_library(Principia::Conservation ALIAS principia_conservation)

target_include_directories(
    principia_conservation
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_conservation
    INTERFACE
        Principia::Math
        Principia::Units
        $<BUILD_INTERFACE:Principia::ProjectOptions>
)
