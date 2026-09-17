include(FetchContent)

function(principia_configure_dependencies)
    if(PRINCIPIA_USE_SYSTEM_DEPENDENCIES)
        find_package(mp-units 2.5 CONFIG QUIET)
    endif()

    if(NOT TARGET mp-units::mp-units)
        if(NOT PRINCIPIA_FETCH_DEPENDENCIES)
            message(FATAL_ERROR "mp-units 2.5 is required. Enable PRINCIPIA_FETCH_DEPENDENCIES or provide the package.")
        endif()

        set(MP_UNITS_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
        set(MP_UNITS_BUILD_CXX_MODULES OFF CACHE BOOL "" FORCE)
        set(MP_UNITS_API_CONTRACTS NONE CACHE STRING "" FORCE)
        # VS 17.11's standard library does not satisfy mp-units 2.5's complete
        # std::format probe. Formatting is presentation-only, so keep it out of
        # the physical type substrate.
        set(MP_UNITS_API_STD_FORMAT OFF CACHE BOOL "" FORCE)
        set(MP_UNITS_API_NATURAL_UNITS OFF CACHE BOOL "" FORCE)

        # mp-units uses fmt when its std::format integration is disabled.
        if(PRINCIPIA_USE_SYSTEM_DEPENDENCIES)
            find_package(fmt 12 CONFIG QUIET)
        endif()
        if(NOT TARGET fmt::fmt)
            set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
            set(FMT_TEST OFF CACHE BOOL "" FORCE)
            FetchContent_Declare(
                fmt
                GIT_REPOSITORY https://github.com/fmtlib/fmt.git
                GIT_TAG 407c905e45ad75fc29bf0f9bb7c5c2fd3475976f
                GIT_SHALLOW FALSE
                SYSTEM
                EXCLUDE_FROM_ALL
            )
            FetchContent_MakeAvailable(fmt)
        endif()

        FetchContent_Declare(
            mp_units
            GIT_REPOSITORY https://github.com/mpusz/mp-units.git
            GIT_TAG 27d2def9082ce00d7eb4f75695dbead4a748f23f
            GIT_SHALLOW FALSE
            SOURCE_SUBDIR src
            SYSTEM
            EXCLUDE_FROM_ALL
        )
        FetchContent_MakeAvailable(mp_units)
    endif()

    if(PRINCIPIA_BUILD_PRESENTATION)
        if(PRINCIPIA_USE_SYSTEM_DEPENDENCIES)
            find_package(SDL3 3.4 CONFIG QUIET)
        endif()

        if(NOT TARGET SDL3::SDL3)
            if(NOT PRINCIPIA_FETCH_DEPENDENCIES)
                message(FATAL_ERROR "SDL 3.4 is required for presentation. Enable dependency fetching or disable presentation.")
            endif()

            set(SDL_SHARED OFF CACHE BOOL "" FORCE)
            set(SDL_STATIC ON CACHE BOOL "" FORCE)
            set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
            set(SDL_TESTS OFF CACHE BOOL "" FORCE)
            set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

            FetchContent_Declare(
                SDL3
                GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                GIT_TAG 8e37db5e797b6167f3a00d697d816a684bd259c7
                GIT_SHALLOW FALSE
                SYSTEM
                EXCLUDE_FROM_ALL
            )
            FetchContent_MakeAvailable(SDL3)
        endif()
    endif()
endfunction()
