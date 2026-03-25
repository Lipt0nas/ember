#pragma once

#include "ember.hpp"

struct Swapchain {
    VkSwapchainKHR handle = VK_NULL_HANDLE;

    std::vector<VkImage>     images;
    std::vector<VkImageView> image_views;

    uint32_t width;
    uint32_t height;

    VkFormat        format;
    VkColorSpaceKHR color_space;

    VkPresentModeKHR present_mode;
    VkSurfaceKHR     surface;
};

Swapchain create_swapchain(
    SDL_Window*      window,
    VkInstance       instance,
    VkDevice         device,
    VkPhysicalDevice physical_device,
    bool             vsync,
    bool&            hdr_enabled
);
void destroy_swapchain(const Swapchain& swapchain, SDL_Window* window, VkInstance instance, VkDevice device);
