add_library(principia_materials INTERFACE)
add_library(Principia::Materials ALIAS principia_materials)

target_include_directories(
    principia_materials
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_materials
    INTERFACE
        Principia::Core
        $<BUILD_INTERFACE:Principia::ProjectOptions>
)
