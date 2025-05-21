cmake_minimum_required(VERSION 3.12)
project(model_viewer)

set(CMAKE_CXX_STANDARD 17)

# --- GLFW / OpenGL ---
find_package(PkgConfig REQUIRED)
pkg_search_module(GLFW REQUIRED glfw3)
find_package(OpenGL REQUIRED)

# --- GLM ---
include(FetchContent)
FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        0.9.9.8
)
FetchContent_MakeAvailable(glm)

# --- Assimp ---
find_package(assimp REQUIRED)

# --- GLAD ---
add_subdirectory(glad)

# --- spdlog ---
find_package(spdlog REQUIRED)

add_executable(model_viewer main.cpp)


target_link_libraries(model_viewer
        PRIVATE
        ${GLFW_LIBRARIES}
        OpenGL::GL
        glm
        glad
        assimp::assimp
        spdlog::spdlog
)


target_include_directories(model_viewer
        PRIVATE
        ${GLFW_INCLUDE_DIRS}
        ${ASSIMP_INCLUDE_DIRS}
)


add_custom_target(copy_shaders ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        ${CMAKE_BINARY_DIR}/shaders
        COMMENT "Copying shaders to build directory"
)
add_dependencies(model_viewer copy_shaders)