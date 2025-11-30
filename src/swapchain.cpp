#include "swapchain.hpp"

Swapchain create_swapchain(
    SDL_Window* window, VkInstance instance, VkDevice device, VkPhysicalDevice physical_device, bool vsync
) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        spdlog::error("Failed to create SDL window surface");
        exit(1);
    }

    VkSurfaceCapabilitiesKHR capabilities = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);

    std::vector<VkSurfaceFormatKHR> surface_formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, surface_formats.data());

    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, nullptr);

    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, present_modes.data());

    VkSurfaceFormatKHR surface_format = surface_formats[0];
    for (const auto& format : surface_formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
            surface_format = format;
            break;
        }
    }

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync) {
        for (const auto& mode : present_modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                present_mode = mode;
                break;
            }
        }
    }

    int width;
    int height;
    SDL_GetWindowSize(window, &width, &height);

    VkExtent2D extent = {.width = static_cast<uint32_t>(width), .height = static_cast<uint32_t>(height)};

    extent.width  = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    spdlog::info("Swapchain present mode: {}", string_VkPresentModeKHR(present_mode));
    spdlog::info("Swapchain format: {}", string_VkFormat(surface_format.format));
    spdlog::info("Swapchain colorspace: {}", string_VkColorSpaceKHR(surface_format.colorSpace));

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchain_info = {
        .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext                 = nullptr,
        .flags                 = 0,
        .surface               = surface,
        .minImageCount         = image_count,
        .imageFormat           = surface_format.format,
        .imageColorSpace       = surface_format.colorSpace,
        .imageExtent           = extent,
        .imageArrayLayers      = 1,
        .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
        .preTransform          = capabilities.currentTransform,
        .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode           = present_mode,
        .clipped               = VK_TRUE,
        .oldSwapchain          = VK_NULL_HANDLE
    };

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &swapchain));

    uint32_t swapchain_image_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, nullptr);

    std::vector<VkImage> swapchain_images(swapchain_image_count);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, swapchain_images.data());

    std::vector<VkImageView> swapchain_image_views(swapchain_images.size());
    for (int i = 0; i < swapchain_image_views.size(); i++) {
        VkImageViewCreateInfo image_view_info = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext    = nullptr,
            .flags    = 0,
            .image    = swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = surface_format.format,
            .components =
                {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
                 .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                 .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                 .a = VK_COMPONENT_SWIZZLE_IDENTITY},
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1
            }
        };

        VK_CHECK(vkCreateImageView(device, &image_view_info, nullptr, &swapchain_image_views[i]));
    }

    return Swapchain{
        .handle       = swapchain,
        .images       = swapchain_images,
        .image_views  = swapchain_image_views,
        .width        = extent.width,
        .height       = extent.height,
        .format       = surface_format.format,
        .color_space  = surface_format.colorSpace,
        .present_mode = present_mode,
        .surface      = surface
    };
}

void destroy_swapchain(const Swapchain& swapchain, SDL_Window* window, VkInstance instance, VkDevice device) {
    if (swapchain.handle != VK_NULL_HANDLE) {
        for (auto view : swapchain.image_views) {
            vkDestroyImageView(device, view, nullptr);
        }

        vkDestroySwapchainKHR(device, swapchain.handle, nullptr);
        SDL_Vulkan_DestroySurface(instance, swapchain.surface, nullptr);
    }
}
