#pragma once

#include "ember.hpp"
#include "resources.hpp"

VkDescriptorPool imgui_init(
    SDL_Window*      window,
    VkInstance       instance,
    VkPhysicalDevice physical_device,
    VkDevice         device,
    VkFormat         swapchain_format,
    uint32_t         graphics_family_index,
    VkQueue          graphics_queue,
    uint32_t         image_count
);

ImFont* generate_icon_font(float size);

// Register a image to be used with imgui draw image commands, this assumes that the image layout is
// SHADER_READ_ONLY_OPTIMAL
VkDescriptorSet imgui_image_handle(const Image& image, VkSampler sampler);

// Deallocate a handle previously acquired from imgui_image_handle()
void imgui_image_handle_free(VkDescriptorSet handle);
