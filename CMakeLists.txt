cmake_minimum_required(VERSION 3.24)
project(main CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
cmake_policy(SET CMP0135 NEW)

# 1. FORCE TRUE RELEASE MODE WITH OPTIMIZATION FIELDS
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. GLOBAL INTERPROCEDURAL OPTIMIZATION (LINK-TIME OPTIMIZATION / LTO)
# This enables aggressive cross-file inlining for both your code and Raylib.
include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT lto_error)
if(lto_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    message(STATUS "Link-Time Optimization (LTO) enabled successfully.")
else()
    message(STATUS "LTO not supported: ${lto_error}")
endif()

# 3. COMPILER OPTIONS FOR PERFORMANCE, ACCURACY, AND STABILITY
if(MSVC)
    # Windows Architecture Optimizations
    add_compile_options(/O2 /Oi /Ot /fp:precise)
else()
    # macOS (Apple Clang) / Linux (GCC) Optimizations
    # -O3: Maximum standard optimizations (loop unrolling, auto-vectorization)
    # -fno-math-errno: Disables setting global errno on math functions (massive speedup for sqrt/pow)
    # -fno-trapping-math: Assumes math operations won't cause hardware traps
    add_compile_options(-O3 -fno-math-errno -fno-trapping-math)

    # Fused Multiply-Add (FMA): High performance AND higher mathematical precision
    add_compile_options(-ffp-contract=fast)

    if(APPLE)
        # Target Apple Silicon Pipeline explicitly
        add_compile_options(-mcpu=apple-m1 -m64)
    else()
        # Non-Apple Unix
        add_compile_options(-march=native)
    endif()

    # STABILITY: Enable strict warnings to catch implicit double->float precision losses
    add_compile_options(-Wall -Wextra -Wconversion -Wno-sign-conversion)
endif()

# 4. FETCH AND BUILD RAYLIB
include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
set(FETCHCONTENT_QUIET TRUE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.0
    GIT_PROGRESS   TRUE
)

FetchContent_MakeAvailable(raylib)

# 5. SOURCE MANAGMENT & TARGET CONFIGURATION
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp")
set(PROJECT_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/src/")

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_INCLUDE})
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

# 6. MACOS SPECIFIC GRAPHICS BINDINGS (STABILITY FIX)
if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
endif()
