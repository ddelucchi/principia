cmake_minimum_required(VERSION 3.25)

project(
    Principia
    VERSION 0.1.0
    DESCRIPTION "A typed, regime-aware runtime for a programmable physical universe"
    LANGUAGES CXX
)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(PRINCIPIA_BUILD_PRESENTATION "Build the SDL3/SDL_GPU presentation demo" ON)
option(PRINCIPIA_BUILD_TOOLS "Build developer inspection tools" ON)
option(PRINCIPIA_WARNINGS_AS_ERRORS "Treat Principia warnings as errors" ON)
option(
    PRINCIPIA_STRICT_FLOATING_POINT
    "Disable unsafe floating-point reassociation/contraction in deterministic simulation targets"
    ON
)
option(PRINCIPIA_FETCH_DEPENDENCIES "Fetch missing third-party dependencies" ON)
option(
    PRINCIPIA_USE_SYSTEM_DEPENDENCIES
    "Prefer compatible system packages over the repository-pinned dependency revisions"
    OFF
)
option(
    PRINCIPIA_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer where supported"
    OFF
)

if(PRINCIPIA_ENABLE_SANITIZERS AND MSVC)
    # MSVC's /RTC instrumentation is incompatible with AddressSanitizer. CMake
    # 3.25 has no target property for disabling the Debug default, so remove
    # only the incompatible runtime-check switch from the active flag set.
    string(REGEX REPLACE "(^| )[/-]RTC[1csu]+" "\\1" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(ProjectOptions)

principia_configure_dependencies()
principia_add_project_options()

add_subdirectory(reality)
add_subdirectory(game)

if(PRINCIPIA_BUILD_PRESENTATION)
    add_subdirectory(presentation)
endif()

if(PRINCIPIA_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Keep build-tree aliases and installed package names identical. Developer
# warning/sanitizer policy is intentionally build-only; installed consumers get
# only Principia's actual public dependencies and C++23 requirement.
function(principia_install_library target export_name)
    set_target_properties(${target} PROPERTIES EXPORT_NAME ${export_name})
    get_target_property(principia_target_type ${target} TYPE)
    if(principia_target_type STREQUAL "STATIC_LIBRARY")
        # Installed archives may be linked into shared libraries on ELF/Mach-O.
        # A distinct Debug name also lets multi-config installations retain both
        # MSVC runtime/iterator ABIs instead of silently overwriting one archive.
        set_target_properties(
            ${target}
            PROPERTIES
                POSITION_INDEPENDENT_CODE ON
                DEBUG_POSTFIX d
        )
    endif()
    target_compile_features(${target} INTERFACE cxx_std_23)
    install(
        TARGETS ${target}
        EXPORT PrincipiaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endfunction()

principia_install_library(principia_core Core)
principia_install_library(principia_units Units)
principia_install_library(principia_math Math)
principia_install_library(principia_conservation Conservation)
principia_install_library(principia_spacetime Spacetime)
principia_install_library(principia_state State)
principia_install_library(principia_fields Fields)
principia_install_library(principia_materials Materials)
principia_install_library(principia_boundaries Boundaries)
principia_install_library(principia_ontology Ontology)
principia_install_library(principia_operators Operators)
principia_install_library(principia_world World)
principia_install_library(principia_scheduler Scheduler)
principia_install_library(principia_diagnostics Diagnostics)
principia_install_library(principia_solvers Solvers)
principia_install_library(principia_serialization Serialization)
principia_install_library(principia_replay Replay)
principia_install_library(principia_progression Progression)
principia_install_library(principia_game Game)

set(
    principia_public_include_roots
    reality/core/include
    reality/units/include
    reality/math/include
    reality/conservation/include
    reality/spacetime/include
    reality/state/include
    reality/fields/include
    reality/materials/include
    reality/boundaries/include
    reality/ontology/include
    reality/operators/include
    reality/world/include
    reality/scheduler/include
    reality/diagnostics/include
    reality/solvers/include
    reality/serialization/include
    reality/replay/include
    game/progression/include
    game/entities/include
)
foreach(include_root IN LISTS principia_public_include_roots)
    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${include_root}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        FILES_MATCHING PATTERN "*.hpp"
    )
endforeach()

set(principia_package_directory "${CMAKE_INSTALL_LIBDIR}/cmake/Principia")
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PrincipiaConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PrincipiaConfig.cmake"
    INSTALL_DESTINATION "${principia_package_directory}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PrincipiaConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    EXPORT PrincipiaTargets
    FILE PrincipiaTargets.cmake
    NAMESPACE Principia::
    DESTINATION "${principia_package_directory}"
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/PrincipiaConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/PrincipiaConfigVersion.cmake"
    DESTINATION "${principia_package_directory}"
)
install(FILES README.md DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DOCDIR}")
