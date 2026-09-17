function(principia_add_project_options)
    add_library(principia_project_options INTERFACE)
    add_library(Principia::ProjectOptions ALIAS principia_project_options)

    target_compile_features(principia_project_options INTERFACE cxx_std_23)

    if(MSVC)
        target_compile_options(
            principia_project_options
            INTERFACE
                /W4
                /FS
                /permissive-
                /Zc:__cplusplus
                /utf-8
        )
        if(PRINCIPIA_WARNINGS_AS_ERRORS)
            target_compile_options(principia_project_options INTERFACE /WX)
        endif()
    else()
        target_compile_options(
            principia_project_options
            INTERFACE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wshadow
        )
        if(PRINCIPIA_WARNINGS_AS_ERRORS)
            target_compile_options(principia_project_options INTERFACE -Werror)
        endif()
    endif()

    if(PRINCIPIA_STRICT_FLOATING_POINT)
        if(MSVC)
            target_compile_options(principia_project_options INTERFACE /fp:strict)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
            target_compile_options(principia_project_options INTERFACE -fno-fast-math -ffp-contract=off)
        endif()
        target_compile_definitions(principia_project_options INTERFACE PRINCIPIA_STRICT_FLOATING_POINT=1)
    else()
        target_compile_definitions(principia_project_options INTERFACE PRINCIPIA_STRICT_FLOATING_POINT=0)
    endif()

    if(PRINCIPIA_ENABLE_SANITIZERS)
        if(MSVC)
            # MSVC provides AddressSanitizer but no UndefinedBehaviorSanitizer.
            # Incremental linking is incompatible with the ASan runtime.
            target_compile_options(principia_project_options INTERFACE /fsanitize=address /Oy-)
            target_link_options(principia_project_options INTERFACE /INCREMENTAL:NO)
            message(STATUS "Principia sanitizers: AddressSanitizer enabled (UBSan is unavailable with MSVC)")
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
            target_compile_options(
                principia_project_options
                INTERFACE
                    -fsanitize=address,undefined
                    -fno-omit-frame-pointer
            )
            target_link_options(principia_project_options INTERFACE -fsanitize=address,undefined)
            message(STATUS "Principia sanitizers: AddressSanitizer and UndefinedBehaviorSanitizer enabled")
        else()
            message(FATAL_ERROR "PRINCIPIA_ENABLE_SANITIZERS is unsupported by ${CMAKE_CXX_COMPILER_ID}")
        endif()
    endif()
endfunction()
