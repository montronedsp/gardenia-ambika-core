cmake_minimum_required(VERSION 3.22)

set(GARDENIA_BASELINE_OUTPUT "" CACHE PATH
    "Directory containing golden baseline-output scenarios (optional for non-golden tests)")

set(GARDENIA_DSP_INCLUDES
    "${CMAKE_SOURCE_DIR}/src/dsp/oscillator"
    "${CMAKE_SOURCE_DIR}/src/dsp/envelope"
    "${CMAKE_SOURCE_DIR}/src/dsp/modulation"
    "${CMAKE_SOURCE_DIR}/src/resources"
)

# --- Existing Phase-3 tests ---
add_executable(GardeniaCoreFuzzGoldenTests FuzzGoldenTests.cpp)
target_link_libraries(GardeniaCoreFuzzGoldenTests PRIVATE GardeniaCoreStatic)
target_compile_definitions(GardeniaCoreFuzzGoldenTests PRIVATE
    GARDENIA_BASELINE_OUTPUT_DIR="${GARDENIA_BASELINE_OUTPUT}")
if(MSVC)
    target_compile_options(GardeniaCoreFuzzGoldenTests PRIVATE /W4 /bigobj)
endif()
add_test(NAME GardeniaCoreFuzzGoldenTests COMMAND GardeniaCoreFuzzGoldenTests)

add_executable(GardeniaCoreMixerTests MixerTests.cpp)
target_link_libraries(GardeniaCoreMixerTests PRIVATE GardeniaCoreStatic)
if(MSVC)
    target_compile_options(GardeniaCoreMixerTests PRIVATE /W4)
endif()
add_test(NAME GardeniaCoreMixerTests COMMAND GardeniaCoreMixerTests)

add_executable(GardeniaCoreVoiceOrderingTests VoiceOrderingTests.cpp)
target_link_libraries(GardeniaCoreVoiceOrderingTests PRIVATE GardeniaCoreStatic)
target_include_directories(GardeniaCoreVoiceOrderingTests PRIVATE ${GARDENIA_DSP_INCLUDES})
if(MSVC)
    target_compile_options(GardeniaCoreVoiceOrderingTests PRIVATE /W4 /bigobj)
endif()
add_test(NAME GardeniaCoreVoiceOrderingTests COMMAND GardeniaCoreVoiceOrderingTests)

add_executable(GardeniaCoreJuceScan JuceScan.cpp)
target_link_libraries(GardeniaCoreJuceScan PRIVATE GardeniaCoreStatic)
add_test(NAME GardeniaCoreJuceScan COMMAND GardeniaCoreJuceScan)

add_executable(GardeniaCoreCompileSmoke CompileSmoke.cpp)
target_link_libraries(GardeniaCoreCompileSmoke PRIVATE GardeniaCoreStatic)
target_include_directories(GardeniaCoreCompileSmoke PRIVATE ${GARDENIA_DSP_INCLUDES})
if(MSVC)
    target_compile_options(GardeniaCoreCompileSmoke PRIVATE /bigobj)
endif()
add_test(NAME GardeniaCoreCompileSmoke COMMAND GardeniaCoreCompileSmoke)

add_executable(GardeniaCoreSynthesisGoldenTests SynthesisGoldenTests.cpp)
target_link_libraries(GardeniaCoreSynthesisGoldenTests PRIVATE GardeniaCoreStatic)
target_include_directories(GardeniaCoreSynthesisGoldenTests PRIVATE ${GARDENIA_DSP_INCLUDES})
target_compile_definitions(GardeniaCoreSynthesisGoldenTests PRIVATE
    GARDENIA_BASELINE_OUTPUT_DIR="${GARDENIA_BASELINE_OUTPUT}")
if(MSVC)
    target_compile_options(GardeniaCoreSynthesisGoldenTests PRIVATE /W4 /bigobj)
endif()
add_test(NAME GardeniaCoreSynthesisGoldenTests COMMAND GardeniaCoreSynthesisGoldenTests)

# --- Phase-4 ABI tests ---
add_executable(GardeniaCoreCApiSmokeTest CApiSmokeTest.c)
target_link_libraries(GardeniaCoreCApiSmokeTest PRIVATE GardeniaCoreAbiStatic)
target_compile_definitions(GardeniaCoreCApiSmokeTest PRIVATE GARDENIA_CORE_STATIC=1)
set_target_properties(GardeniaCoreCApiSmokeTest PROPERTIES LINKER_LANGUAGE C)
if(MSVC)
    # Link C++ runtime for static ABI that contains C++
    set_property(TARGET GardeniaCoreCApiSmokeTest PROPERTY LINKER_LANGUAGE CXX)
endif()
add_test(NAME GardeniaCoreCApiSmokeTest COMMAND GardeniaCoreCApiSmokeTest)

add_executable(GardeniaCoreAbiLayoutTests AbiLayoutTests.cpp)
target_link_libraries(GardeniaCoreAbiLayoutTests PRIVATE GardeniaCoreAbiStatic)
target_compile_definitions(GardeniaCoreAbiLayoutTests PRIVATE
    GARDENIA_CORE_STATIC=1
    GARDENIA_CORE_BUILD_DIR="${CMAKE_BINARY_DIR}")
add_test(NAME GardeniaCoreAbiLayoutTests COMMAND GardeniaCoreAbiLayoutTests)

add_executable(GardeniaCoreAbiInvalidInputTests AbiInvalidInputTests.cpp)
target_link_libraries(GardeniaCoreAbiInvalidInputTests PRIVATE GardeniaCoreAbiStatic)
target_compile_definitions(GardeniaCoreAbiInvalidInputTests PRIVATE GARDENIA_CORE_STATIC=1)
add_test(NAME GardeniaCoreAbiInvalidInputTests COMMAND GardeniaCoreAbiInvalidInputTests)

add_executable(GardeniaCoreAbiGoldenTests AbiGoldenTests.cpp)
target_link_libraries(GardeniaCoreAbiGoldenTests PRIVATE GardeniaCoreAbiStatic)
target_include_directories(GardeniaCoreAbiGoldenTests PRIVATE ${GARDENIA_DSP_INCLUDES})
target_compile_definitions(GardeniaCoreAbiGoldenTests PRIVATE
    GARDENIA_CORE_STATIC=1
    GARDENIA_BASELINE_OUTPUT_DIR="${GARDENIA_BASELINE_OUTPUT}")
if(MSVC)
    target_compile_options(GardeniaCoreAbiGoldenTests PRIVATE /W4 /bigobj)
endif()
add_test(NAME GardeniaCoreAbiGoldenTests COMMAND GardeniaCoreAbiGoldenTests)

# These tests inspect Windows PE/DLL exports and require Win32 APIs.
if(WIN32)
add_executable(GardeniaCoreDynamicLibrarySmokeTest DynamicLibrarySmokeTest.cpp)
target_include_directories(GardeniaCoreDynamicLibrarySmokeTest PRIVATE
    "${CMAKE_SOURCE_DIR}/include")
target_compile_definitions(GardeniaCoreDynamicLibrarySmokeTest PRIVATE
    GARDENIA_CORE_STATIC=1
    GARDENIA_CORE_DLL_DIR_RELEASE="${CMAKE_BINARY_DIR}/Release"
    GARDENIA_CORE_DLL_DIR_DEBUG="${CMAKE_BINARY_DIR}/Debug")
add_dependencies(GardeniaCoreDynamicLibrarySmokeTest GardeniaCoreShared)
add_test(NAME GardeniaCoreDynamicLibrarySmokeTest COMMAND GardeniaCoreDynamicLibrarySmokeTest)

add_executable(GardeniaCoreSymbolAudit SymbolAudit.cpp)
target_compile_definitions(GardeniaCoreSymbolAudit PRIVATE
    GARDENIA_CORE_DLL_DIR_RELEASE="${CMAKE_BINARY_DIR}/Release"
    GARDENIA_CORE_DLL_DIR_DEBUG="${CMAKE_BINARY_DIR}/Debug")
add_dependencies(GardeniaCoreSymbolAudit GardeniaCoreShared)
add_test(NAME GardeniaCoreSymbolAudit COMMAND GardeniaCoreSymbolAudit)
endif()
