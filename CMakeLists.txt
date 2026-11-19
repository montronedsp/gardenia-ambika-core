cmake_minimum_required(VERSION 3.22)

project(GardeniaAmbikaCore VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Hide symbols by default on non-MSVC shared builds
if(NOT MSVC)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_C_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()


# Project-owned source and tests must compile warning-clean on every supported
# compiler. No blanket suppression or silent configuration-specific exemption.
option(GARDENIA_WARNINGS_AS_ERRORS "Treat all warnings in Gardenia Ambika Core targets as errors" ON)
function(gardenia_core_warnings target)
    if(NOT GARDENIA_WARNINGS_AS_ERRORS)
        message(FATAL_ERROR "Strict warning enforcement is required for this project")
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Wpedantic;-Werror>
            $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Werror>)
    else()
        message(FATAL_ERROR "Unqualified compiler: ${CMAKE_CXX_COMPILER_ID}; configure strict warning policy before building")
    endif()
endfunction()

set(GARDENIA_CORE_PUBLIC_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Internal static library (unit tests / header-only DSP link)
add_library(GardeniaCoreStatic STATIC
    src/internal/gardenia_core_dummy.cpp
)

target_include_directories(GardeniaCoreStatic
    PUBLIC
        "${GARDENIA_CORE_PUBLIC_INCLUDE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/resources"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/oscillator"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/envelope"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/modulation"
)

target_compile_definitions(GardeniaCoreStatic PUBLIC GARDENIA_CORE_STATIC=1)
target_compile_features(GardeniaCoreStatic PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(GardeniaCoreStatic PRIVATE /W4 /bigobj /FS
        $<$<CONFIG:Release>:/Ox>
        $<$<CONFIG:Debug>:/Od>
    )
endif()

# Shared library with C ABI
add_library(GardeniaCoreShared SHARED
    src/api/gardenia_core_api.cpp
)

set_target_properties(GardeniaCoreShared PROPERTIES
    OUTPUT_NAME "gardenia-core"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
)

target_include_directories(GardeniaCoreShared
    PUBLIC
        "${GARDENIA_CORE_PUBLIC_INCLUDE}"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/api"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/resources"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/oscillator"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/envelope"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/modulation"
)

target_compile_definitions(GardeniaCoreShared
    PRIVATE GARDENIA_CORE_BUILD_SHARED=1
)

target_compile_features(GardeniaCoreShared PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(GardeniaCoreShared PRIVATE /W4 /bigobj /FS
        $<$<CONFIG:Release>:/Ox>
        $<$<CONFIG:Debug>:/Od>
    )
endif()

# Static link of API for in-tree tests that do not LoadLibrary
add_library(GardeniaCoreAbiStatic STATIC
    src/api/gardenia_core_api.cpp
)
target_include_directories(GardeniaCoreAbiStatic
    PUBLIC "${GARDENIA_CORE_PUBLIC_INCLUDE}"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/api"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/resources"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/oscillator"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/envelope"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/modulation"
)
target_compile_definitions(GardeniaCoreAbiStatic PUBLIC GARDENIA_CORE_STATIC=1)
if(MSVC)
    target_compile_options(GardeniaCoreAbiStatic PRIVATE /W4 /bigobj /FS)
endif()

enable_testing()
add_subdirectory(tests)

# Include all library and test targets in the same strict warning policy.
foreach(_gardenia_target
    GardeniaCoreStatic
    GardeniaCoreShared
    GardeniaCoreAbiStatic
    GardeniaCoreFuzzGoldenTests
    GardeniaCoreMixerTests
    GardeniaCoreJuceScan
    GardeniaCoreCompileSmoke
    GardeniaCoreSynthesisGoldenTests
    GardeniaCoreCApiSmokeTest
    GardeniaCoreAbiLayoutTests
    GardeniaCoreAbiInvalidInputTests
    GardeniaCoreAbiGoldenTests
    GardeniaCoreDynamicLibrarySmokeTest
    GardeniaCoreSymbolAudit
)
    if(TARGET ${_gardenia_target})
        gardenia_core_warnings(${_gardenia_target})
    endif()
endforeach()
