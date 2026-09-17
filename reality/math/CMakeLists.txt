add_library(principia_math INTERFACE)
add_library(Principia::Math ALIAS principia_math)

target_include_directories(
    principia_math
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_math
    INTERFACE
        $<BUILD_INTERFACE:Principia::ProjectOptions>
)
