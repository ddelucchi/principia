add_library(principia_spacetime INTERFACE)
add_library(Principia::Spacetime ALIAS principia_spacetime)

target_include_directories(
    principia_spacetime
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_spacetime
    INTERFACE
        Principia::Core
        Principia::Math
        Principia::Units
)

