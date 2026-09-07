cmake_minimum_required(VERSION 3.25)
project(blackhole LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR "This project targets Apple Silicon (Metal) exclusively.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo MinSizeRel)
endif()

# DEPENDENCIES: SDL3 WINDOW AND METAL LAYER ONLY.
# MATH USES APPLE SIMD. GLM NOT NEEDED.
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)
foreach(subsystem HAPTIC SENSOR CAMERA HIDAPI DIALOG POWER)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()

FetchContent_Declare(SDL3
    URL      https://github.com/libsdl-org/SDL/releases/download/release-3.2.30/SDL3-3.2.30.tar.gz
    URL_HASH SHA256=4c3b09330d866dc52eb65b66259a6684ad387252ca8c57901b3a2b534eb42e3d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# APPLE METAL-CPP HEADERS MATCHED TO MACOS SDK. NO CMAKE CONFIG.
FetchContent_Declare(metal_cpp
    GIT_REPOSITORY    https://github.com/apple/metal-cpp.git
    GIT_TAG           release/metal-cpp_macOS15.2_iOS18.2
    GIT_SHALLOW       TRUE
    GIT_PROGRESS      TRUE
    GIT_SUBMODULES    ""
    SOURCE_SUBDIR     headers-only-no-cmake
)

FetchContent_MakeAvailable(SDL3 metal_cpp)

# SYSTEM INCLUDES SUPPRESS THIRD-PARTY WARNINGS.
add_library(metal_cpp INTERFACE)
target_include_directories(metal_cpp SYSTEM INTERFACE ${metal_cpp_SOURCE_DIR})
target_link_libraries(metal_cpp INTERFACE
    "-framework Foundation"
    "-framework Metal"
    "-framework QuartzCore"
)

# EXECUTABLE TARGET.
add_executable(blackhole
    src/main.cpp
    src/app/window.cpp
    src/gfx/metal_impl.cpp
    src/gfx/device.cpp
    src/gfx/gpu_timer.cpp
    src/gfx/renderer.cpp
    src/gfx/hud.cpp
    src/physics/luts.cpp
    src/physics/freefall.cpp
    src/tools/validate.cpp
)

target_include_directories(blackhole PRIVATE src)
# CORETEXT AND COREGRAPHICS RASTERIZE HUD FONT ATLAS. NO OBJC++ NEEDED.
target_link_libraries(blackhole PRIVATE
    SDL3::SDL3
    metal_cpp
    "-framework CoreText"
    "-framework CoreGraphics"
)
set_target_properties(blackhole PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# NO FAST-MATH. PRESERVES GEODESIC CONSERVED QUANTITIES AND FINITE FLOATS.
target_compile_options(blackhole PRIVATE
    -Wall -Wextra -Wpedantic
    -mcpu=apple-m1
    "$<$<CONFIG:Release,RelWithDebInfo>:-O3;-fno-math-errno;-fno-trapping-math>"
)

# CHECK LTO ON CXX ONLY TO AVOID SDL3 C COMPILER MISDETECTION.
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error LANGUAGES CXX)
if(ipo_supported)
    set_target_properties(blackhole PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE        TRUE
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    message(STATUS "LTO enabled for optimised configurations.")
else()
    message(STATUS "LTO unavailable: ${ipo_error}")
endif()

# COMPILE METAL SOURCES TO AIR AND BUNDLE METALLIB.
# FRECORD-SOURCES ENABLES XCODE SHADER DEBUGGER AND PROFILER.
set(SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/lens.metal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/composite.metal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/bloom.metal
)
set(SHADER_LIB ${CMAKE_BINARY_DIR}/assets/shaders.metallib)

set(SHADER_AIRS "")
foreach(shader ${SHADER_SOURCES})
    get_filename_component(shader_name ${shader} NAME_WE)
    set(air ${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader_name}.air)
    add_custom_command(
        OUTPUT  ${air}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND xcrun -sdk macosx metal -c ${shader} -o ${air}
                -std=metal3.1 -O3 -frecord-sources
                -I ${CMAKE_CURRENT_SOURCE_DIR}/src
        DEPENDS ${shader}
                ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/common.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/physics/kerr.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/physics/disk.h
                ${CMAKE_CURRENT_SOURCE_DIR}/src/physics/tonemap.h
        COMMENT "metal: ${shader_name}.metal -> ${shader_name}.air"
        VERBATIM
    )
    list(APPEND SHADER_AIRS ${air})
endforeach()

add_custom_command(
    OUTPUT  ${SHADER_LIB}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/assets
    COMMAND xcrun -sdk macosx metallib ${SHADER_AIRS} -o ${SHADER_LIB}
    DEPENDS ${SHADER_AIRS}
    COMMENT "metallib: -> assets/shaders.metallib"
    VERBATIM
)

add_custom_target(shaders DEPENDS ${SHADER_LIB})
add_dependencies(blackhole shaders)
