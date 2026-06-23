cmake_minimum_required(VERSION 3.28)
project(main LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT APPLE)
    message(FATAL_ERROR "This project targets Apple Silicon (Metal) exclusively.")
endif()

# --- Dependencies ---
include(FetchContent)

# Pin to a real tag once you've verified SDL_GPU's Metal backend is stable
# enough for your needs on this SDL version. Don't track a moving branch.
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.10
)
FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

# --- Executable ---
add_executable(BlackHoleSim
    src/main.cpp
    src/renderer.cpp
    src/solver.cpp
)

target_include_directories(BlackHoleSim PRIVATE src)
target_link_libraries(BlackHoleSim PRIVATE SDL3::SDL3 glm::glm)
target_compile_options(BlackHoleSim PRIVATE -Wall -Wextra -Wpedantic)

# --- Native Metal shader compilation ---
# Deliberately NOT using SDL_shadercross / glslc here. We are Metal-only,
# so we write MSL directly and skip the cross-compiler entirely:
#   xcrun metal     : .metal -> .air   (intermediate representation)
#   xcrun metallib  : .air   -> .metallib (what SDL_GPU_SHADERFORMAT_METALLIB loads)
set(SHADER_SRC ${CMAKE_SOURCE_DIR}/src/shaders/geodesic.metal)
set(SHADER_AIR ${CMAKE_BINARY_DIR}/geodesic.air)
set(SHADER_LIB ${CMAKE_BINARY_DIR}/assets/geodesic.metallib)

add_custom_command(
    OUTPUT ${SHADER_LIB}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/assets
    COMMAND xcrun -sdk macosx metal -c ${SHADER_SRC} -o ${SHADER_AIR} -std=metal3.1 -O3
    COMMAND xcrun -sdk macosx metallib ${SHADER_AIR} -o ${SHADER_LIB}
    DEPENDS ${SHADER_SRC}
    COMMENT "Compiling geodesic.metal -> geodesic.metallib"
    VERBATIM
)

add_custom_target(Shaders DEPENDS ${SHADER_LIB})
add_dependencies(BlackHoleSim Shaders)

file(COPY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION ${CMAKE_BINARY_DIR}/assets/)
