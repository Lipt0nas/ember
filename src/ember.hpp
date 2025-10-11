#pragma once

#include <volk.h>

#include <vulkan/vk_enum_string_helper.h>

#include <tiny_gltf.h>
#include <vk_mem_alloc.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <spdlog/spdlog.h>

#include <meshoptimizer.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <unordered_map>
#include <vector>

#define VK_CHECK(call)                                                                                                 \
    do {                                                                                                               \
        VkResult result_ = call;                                                                                       \
        assert(result_ == VK_SUCCESS);                                                                                 \
    } while (0)
