add_library(principia_operators STATIC src/gravity_operator.cpp)
add_library(Principia::Operators ALIAS principia_operators)

target_include_directories(
    principia_operators
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
    principia_operators
    PUBLIC
        Principia::Core
        Principia::Boundaries
        Principia::Fields
        Principia::Math
        Principia::Spacetime
        Principia::Units
    PRIVATE
        $<BUILD_INTERFACE:Principia::ProjectOptions>
)
