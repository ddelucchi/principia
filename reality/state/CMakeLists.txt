add_library(principia_state STATIC src/channel.cpp)
add_library(Principia::State ALIAS principia_state)

target_include_directories(
    principia_state
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_state
    PUBLIC
        Principia::Core
    PRIVATE
        Principia::Math
        Principia::Units
        $<BUILD_INTERFACE:Principia::ProjectOptions>
)
