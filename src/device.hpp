#pragma once

#include "ember.hpp"

VkInstance create_instance(bool enable_validation, VkDebugUtilsMessengerEXT& debug_messenger);

VkPhysicalDevice pick_physical_device(VkInstance instance);
uint32_t         get_graphics_family_index(VkPhysicalDevice physical_device);

VkDevice create_device(
    VkInstance instance, VkPhysicalDevice physical_device, uint32_t graphics_family_index, bool enable_validation
);
