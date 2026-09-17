cmake_minimum_required(VERSION 3.25)

project(PrincipiaPackageConsumer LANGUAGES CXX)

include(FetchContent)

find_package(mp-units 2.5 CONFIG QUIET)
if(NOT TARGET mp-units::mp-units)
    set(MP_UNITS_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
    set(MP_UNITS_BUILD_CXX_MODULES OFF CACHE BOOL "" FORCE)
    set(MP_UNITS_API_CONTRACTS NONE CACHE STRING "" FORCE)
    set(MP_UNITS_API_STD_FORMAT OFF CACHE BOOL "" FORCE)
    set(MP_UNITS_API_NATURAL_UNITS OFF CACHE BOOL "" FORCE)

    find_package(fmt 12 CONFIG QUIET)
    if(NOT TARGET fmt::fmt)
        set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
        set(FMT_TEST OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            fmt
            GIT_REPOSITORY https://github.com/fmtlib/fmt.git
            GIT_TAG 407c905e45ad75fc29bf0f9bb7c5c2fd3475976f
            GIT_SHALLOW FALSE
        )
        FetchContent_MakeAvailable(fmt)
    endif()

    FetchContent_Declare(
        mp_units
        GIT_REPOSITORY https://github.com/mpusz/mp-units.git
        GIT_TAG 27d2def9082ce00d7eb4f75695dbead4a748f23f
        GIT_SHALLOW FALSE
        SOURCE_SUBDIR src
    )
    FetchContent_MakeAvailable(mp_units)
endif()

find_package(Principia 0.1 CONFIG REQUIRED)

add_executable(principia_package_consumer main.cpp)
target_link_libraries(principia_package_consumer PRIVATE Principia::Game)
