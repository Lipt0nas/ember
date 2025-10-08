cmake_minimum_required(VERSION 4.0)
project(Ember)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)

include(FetchContent)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    sdl
    GIT_REPOSITORY https://github.com/libsdl-org/SDL
    GIT_TAG release-3.2.24
    GIT_SHALLOW TRUE
)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.3-docking
    GIT_SHALLOW TRUE
)

FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.3.0
    GIT_SHALLOW TRUE
)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.3
    GIT_SHALLOW TRUE
)

FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG 1.4.304
    GIT_SHALLOW TRUE
)

FetchContent_Declare(tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.6
    GIT_SHALLOW TRUE
)

FetchContent_Declare(meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG v0.25
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glm sdl vma spdlog volk tinygltf meshoptimizer)

FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_MAkeAvailable(imgui)
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    )
    target_include_directories(imgui PUBLIC 
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_compile_definitions(imgui PUBLIC
        "IMGUI_IMPL_VULKAN_NO_PROTOTYPES"
    )
    target_link_libraries(imgui PUBLIC SDL3::SDL3-static Vulkan::Vulkan)
endif()

set(SOURCES
    "src/main.cpp"
    "src/ember.cpp"
    "src/camera.cpp"
    "src/resources.cpp"
    "src/swapchain.cpp"
    "src/device.cpp"
    "src/pipeline.cpp"
)
add_executable(Ember ${SOURCES})

target_link_libraries(Ember PRIVATE
    Vulkan::Vulkan
    glm::glm
    SDL3::SDL3-static
    imgui
    VulkanMemoryAllocator
    spdlog::spdlog
    volk
    tinygltf
    meshoptimizer
)

# TODO: temporary convenience
find_program(GLSLC glslc)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found! Make sure Vulkan SDK is installed and in PATH")
endif()

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders")
file(GLOB_RECURSE SHADER_SOURCES 
    "${SHADER_SOURCE_DIR}/*"
)
list(FILTER SHADER_SOURCES EXCLUDE REGEX ".*\\.(spv|glsl)$")

set(SHADER_OUTPUTS)
foreach(SHADER_SOURCE ${SHADER_SOURCES})
    set(SHADER_OUTPUT "${SHADER_SOURCE}.spv")
    
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLC} ${SHADER_SOURCE} -o ${SHADER_OUTPUT} --target-spv=spv1.4
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling shader: ${SHADER_SOURCE}"
        VERBATIM
    )
    
    list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUTS})
add_dependencies(Ember shaders)
