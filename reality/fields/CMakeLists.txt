add_library(principia_fields INTERFACE)
add_library(Principia::Fields ALIAS principia_fields)

target_include_directories(
    principia_fields
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_fields
    INTERFACE
        Principia::Core
        Principia::Math
        Principia::Spacetime
        Principia::State
        Principia::Units
)

