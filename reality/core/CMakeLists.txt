add_library(principia_core INTERFACE)
add_library(Principia::Core ALIAS principia_core)

target_include_directories(
    principia_core
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(principia_core INTERFACE $<BUILD_INTERFACE:Principia::ProjectOptions>)
