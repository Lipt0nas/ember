cmake_minimum_required(VERSION 4.0)
project(Ember)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG v1.4.335
    GIT_SHALLOW TRUE
)

FetchContent_Declare(VulkanUtilityLibraries
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Utility-Libraries.git
    GIT_TAG v1.4.335
    GIT_SHALLOW TRUE
)

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
    GIT_TAG v1.92.6-docking
    GIT_SHALLOW TRUE
)

FetchContent_Declare(imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG df1c30142e7c3fb13c171aaeb328bb338fa7aaa6
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
    GIT_TAG v1.0
    GIT_SHALLOW TRUE
)

FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb
  GIT_TAG 5c205738c191bcb0abc65c4febfa9bd25ff35234
)

FetchContent_Declare(implot
  GIT_REPOSITORY https://github.com/epezent/implot
  GIT_TAG 3da8bd34299965d3b0ab124df743fe3e076fa222
)

set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
set(TRACY_VK_USE_SYMBOL_TABLE ON CACHE BOOL "" FORCE)
set(TRACY_NO_BROADCAST ON CACHE BOOL "" FORCE)
set(TRACY_CALLSTACK ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  tracy
  GIT_REPOSITORY https://github.com/wolfpld/tracy.git
  GIT_TAG v0.12.2
  GIT_SHALLOW TRUE
)

set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_JS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_VK_UPLOAD OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_LOADTEST_APPS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    ktx
    GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software.git
    GIT_TAG v4.4.2
    GIT_SHALLOW TRUE
)

# https://github.com/jrouwe/JoltPhysicsHelloWorld/blob/main/Build/CMakeLists.txt
set(DOUBLE_PRECISION OFF)
set(GENERATE_DEBUG_SYMBOLS ON)
set(CROSS_PLATFORM_DETERMINISTIC OFF)
set(INTERPROCEDURAL_OPTIMIZATION OFF)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF)
set(CPP_EXCEPTIONS_ENABLED OFF)
set(CPP_RTTI_ENABLED ON)
set(OBJECT_LAYER_BITS 16)

set(USE_SSE4_1 ON)
set(USE_SSE4_2 ON)
set(USE_AVX ON)
set(USE_AVX2 ON)
set(USE_AVX512 OFF)
set(USE_LZCNT ON)
set(USE_TZCNT ON)
set(USE_F16C ON)
set(USE_FMADD ON)

if (MSVC)
    set(CMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE "x64")
    if (USE_STATIC_MSVC_RUNTIME_LIBRARY)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus /Gm- /MP /nologo /diagnostics:classic /FC /fp:except- /Zc:inline")

    if (NOT CPP_RTTI_ENABLED)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR-")
    endif()

    if (NOT CPP_EXCEPTIONS_ENABLED)
        string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4577")
    endif()

    if (OVERRIDE_CXX_FLAGS)
        set(CMAKE_CXX_FLAGS_DEBUG "/GS /Od /Ob0 /RTC1")
        set(CMAKE_CXX_FLAGS_RELEASE "/GS- /Gy /O2 /Oi /Ot")
    endif()

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
        if (CROSS_PLATFORM_DETERMINISTIC)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:precise")
        else()
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:fast")
        endif()
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /showFilenames")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Qunused-arguments")
    endif()
endif()

FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY "https://github.com/jrouwe/JoltPhysics"
    GIT_TAG "v5.5.0"
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR "Build"
)

FetchContent_Declare(
  entt
  GIT_REPOSITORY https://github.com/skypjack/entt
  GIT_TAG v3.16.0
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  angelscript
  GIT_REPOSITORY https://github.com/anjo76/angelscript
  GIT_TAG v2.38.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR sdk/angelscript/projects/cmake
)

set(BUILD_DOC OFF)
set(BUILD_SANDBOX OFF)
set(SKIP_PERFORMANCE_COMPARISON ON)
FetchContent_Declare(
  cereal
  GIT_REPOSITORY https://github.com/USCiLab/cereal
  GIT_TAG v1.3.2
  GIT_SHALLOW TRUE
)

set(MINIAUDIO_NO_LIBVORBIS "Disable miniaudio_libvorbis" ON)
set(MINIAUDIO_NO_LIBOPUS "Disable miniaudio_libopus" ON)
FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.24
  GIT_SHALLOW TRUE
)

FetchContent_Declare(imgui_node_editor
    GIT_REPOSITORY https://github.com/thedmd/imgui-node-editor.git
    GIT_TAG develop
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(
    glm
    sdl
    vma
    spdlog
    volk
    tinygltf
    meshoptimizer
    stb
    imgui
    imguizmo
    implot
    tracy
    JoltPhysics
    entt
    angelscript
    cereal
    VulkanHeaders
    VulkanUtilityLibraries
    JoltPhysics
    ktx
    miniaudio
    imgui_node_editor
)

target_link_libraries(volk PUBLIC Vulkan-Headers)

set(AS_ADDON_DIR "${angelscript_SOURCE_DIR}/sdk/add_on")

add_library(angelscript_addons STATIC
    ${AS_ADDON_DIR}/scriptbuilder/scriptbuilder.cpp
    ${AS_ADDON_DIR}/scriptstdstring/scriptstdstring.cpp
    ${AS_ADDON_DIR}/scriptstdstring/scriptstdstring_utils.cpp
    ${AS_ADDON_DIR}/scriptarray/scriptarray.cpp
    ${AS_ADDON_DIR}/scripthandle/scripthandle.cpp
    ${AS_ADDON_DIR}/weakref/weakref.cpp
    ${AS_ADDON_DIR}/scriptdictionary/scriptdictionary.cpp
    ${AS_ADDON_DIR}/scriptgrid/scriptgrid.cpp
)

target_include_directories(angelscript_addons PUBLIC
    ${AS_ADDON_DIR}/scriptbuilder
    ${AS_ADDON_DIR}/scriptstdstring
    ${AS_ADDON_DIR}/scriptarray
    ${AS_ADDON_DIR}/scripthandle
    ${AS_ADDON_DIR}/weakref
    ${AS_ADDON_DIR}/scriptdictionary
    ${AS_ADDON_DIR}/scriptgrid
)

target_link_libraries(angelscript_addons PUBLIC angelscript)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
)
target_compile_definitions(imgui PUBLIC
    "IMGUI_IMPL_VULKAN_NO_PROTOTYPES"
)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static Vulkan-Headers)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

add_library(implot STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${implot_SOURCE_DIR}/implot_demo.cpp
)
target_link_libraries(implot PUBLIC imgui)
target_include_directories(implot INTERFACE ${implot_SOURCE_DIR})

add_library(imguizmo STATIC
    ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
)
target_link_libraries(imguizmo PUBLIC imgui)
target_include_directories(imguizmo INTERFACE ${imguizmo_SOURCE_DIR})

# It's either this or a fork, even though this is extremely cursed
set(NODE_EDITOR_CPP "${imgui_node_editor_SOURCE_DIR}/imgui_node_editor.cpp")
file(READ "${NODE_EDITOR_CPP}" SRC)

foreach(VAR
    "node->m_Bounds"
    "node->m_GroupBounds"
    "newBounds"
    "m_NodeRect"
    "m_CurrentPin->m_Bounds"
    "m_GroupBounds"
)
    string(REPLACE
        "${VAR}.Floor();"
        "${VAR} = ImRect(ImFloor(${VAR}.Min), ImFloor(${VAR}.Max));"
        SRC "${SRC}"
    )
endforeach()

file(WRITE "${NODE_EDITOR_CPP}" "${SRC}")

add_library(imgui_node_editor STATIC
    ${imgui_node_editor_SOURCE_DIR}/imgui_node_editor.cpp
    ${imgui_node_editor_SOURCE_DIR}/imgui_node_editor_api.cpp
    ${imgui_node_editor_SOURCE_DIR}/imgui_canvas.cpp
    ${imgui_node_editor_SOURCE_DIR}/crude_json.cpp
)
target_include_directories(imgui_node_editor PUBLIC ${imgui_node_editor_SOURCE_DIR})
target_link_libraries(imgui_node_editor PUBLIC imgui)

SET_INTERPROCEDURAL_OPTIMIZATION()

set(SOURCES
    "src/main.cpp"
    "src/ember.cpp"
    "src/camera.cpp"
    "src/resources.cpp"
    "src/swapchain.cpp"
    "src/device.cpp"
    "src/pipeline.cpp"
    "src/ui.cpp"
    "src/rt_scene.cpp"
    "src/framegraph.cpp"
    "src/geometry.cpp"
    "src/scene.cpp"
    "src/embedded.cpp"
    "src/script_system.cpp"
    "src/editor.cpp"
    "src/input_system.cpp"
    "src/scene_serializer.cpp"
    "src/world.cpp"
    "src/component_registry.cpp"
    "src/asset_registry.cpp"
    "src/asset_importer.cpp"
    "src/asset.cpp"
    "src/renderer.cpp"
    "src/sound_system.cpp"
    "src/particle_editor.cpp"
    "src/particle_effect.cpp"
    "src/asset_exporter.cpp"
    "src/physics.cpp"
    "src/cvars.cpp"
)
add_executable(Ember ${SOURCES})
if(MINGW)
    target_compile_options(Ember PRIVATE -Wa,-mbig-obj)
endif()
target_compile_definitions(Ember PRIVATE
    TRACY_ENABLE
    TRACY_ON_DEMAND
    TRACY_ONLY_LOCALHOST
    TRACY_VK_USE_SYMBOL_TABLE
    TRACY_NO_BROADCAST
    TRACY_CALLSTACK
)

target_link_libraries(Ember PRIVATE
    Vulkan-Headers
    VulkanUtilityHeaders
    glm::glm
    SDL3::SDL3-static
    imgui
    VulkanMemoryAllocator
    spdlog::spdlog
    volk
    tinygltf
    meshoptimizer
    stb
    implot
    imguizmo
    TracyClient
    Jolt
    EnTT::EnTT
    angelscript
    angelscript_addons
    cereal
    ktx
    miniaudio
    imgui_node_editor
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
list(FILTER SHADER_SOURCES EXCLUDE REGEX ".*\\.(spv|glsl|d)$")

set(SHADER_OUTPUTS)
foreach(SHADER_SOURCE ${SHADER_SOURCES})
    set(SHADER_OUTPUT "${SHADER_SOURCE}.spv")
    set(SHADER_DEPFILE "${SHADER_OUTPUT}.d")

    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLC} ${SHADER_SOURCE} -o ${SHADER_OUTPUT} --target-env=vulkan1.4 --target-spv=spv1.6 -g -MD -MF ${SHADER_DEPFILE}
        DEPENDS ${SHADER_SOURCE}
        DEPFILE ${SHADER_DEPFILE}
        COMMENT "Compiling shader: ${SHADER_SOURCE}"
        VERBATIM
    )

    list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUTS})
add_dependencies(Ember shaders)
