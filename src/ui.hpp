#pragma once

#include "ember.hpp"

VkDescriptorPool init_imgui(
    SDL_Window*      window,
    VkInstance       instance,
    VkPhysicalDevice physical_device,
    VkDevice         device,
    VkFormat         swapchain_format,
    uint32_t         graphics_family_index,
    VkQueue          graphics_queue,
    uint32_t         image_count
);
