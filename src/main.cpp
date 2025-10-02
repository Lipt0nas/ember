#include <volk.h>

#include <spdlog/spdlog.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <tuple>
#include <vector>

const std::vector<const char *> validation_layer_names = {"VK_LAYER_KHRONOS_validation"};

#define VK_CHECK(call)                                                                                                 \
    do {                                                                                                               \
        VkResult result_ = call;                                                                                       \
        assert(result_ == VK_SUCCESS);                                                                                 \
    } while (0)

void glfw_error_callback(int error, const char *description) {
    spdlog::error("GLFW error {}: {}", error, description);
}

VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                                                     VkDebugUtilsMessageTypeFlagsEXT             messageType,
                                                     const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                     void                                       *pUserData) {
    spdlog::debug("VK validation: {}", pCallbackData->pMessage);

    return VK_FALSE;
}

std::tuple<VkPhysicalDeviceProperties, VkPhysicalDeviceFeatures>
get_physical_device_info(VkPhysicalDevice physical_device) {
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures   features;

    vkGetPhysicalDeviceProperties(physical_device, &properties);
    vkGetPhysicalDeviceFeatures(physical_device, &features);

    return {properties, features};
}

uint32_t find_queue_index(VkPhysicalDevice physical_device, VkQueueFlagBits queue_flags) {
    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_properties(queue_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_count, queue_properties.data());

    assert(queue_properties.size() > 0);

    uint32_t queue_index = UINT32_MAX;
    for (uint32_t i = 0; i < queue_properties.size(); i++) {
        const auto &queue = queue_properties[i];
        if (queue.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_index = i;
        }
    }

    assert(queue_index != UINT32_MAX);

    return queue_index;
}

std::tuple<VkSurfaceCapabilitiesKHR, VkSurfaceFormatKHR, VkPresentModeKHR, VkExtent2D>
get_swapchain_settings(GLFWwindow *window, VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
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
    for (const auto &format : surface_formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
            surface_format = format;
            break;
        }
    }

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto &mode : present_modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode = mode;
            break;
        }
    }

    int width;
    int height;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D extent = {.width = static_cast<uint32_t>(width), .height = static_cast<uint32_t>(height)};

    extent.width  = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return {capabilities, surface_format, present_mode, extent};
}

int main() {
    spdlog::set_level(spdlog::level::trace);
    spdlog::info("Starting ember");

    VK_CHECK(volkInitialize());

    spdlog::info("Initializing GLFW");
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        spdlog::error("Failed to initialize GLFW");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    auto *window = glfwCreateWindow(1280, 720, "Ember", nullptr, nullptr);
    if (!window) {
        spdlog::error("Failed to create GLFW window");
        return 1;
    }

    uint32_t extension_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
    spdlog::debug("Instance supports {} extensions", extension_count);

    uint32_t     glfw_extension_count = 0;
    const char **glfw_extensions      = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    spdlog::debug("GLFW requires {} extensions", glfw_extension_count);

    std::vector<const char *> instance_extensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    for (uint32_t i = 0; i < glfw_extension_count; i++) {
        instance_extensions.push_back(glfw_extensions[i]);
    }

    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_info = {
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext           = nullptr,
        .flags           = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = vulkan_debug_callback,
        .pUserData       = nullptr};

    VkApplicationInfo app_info = {.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                  .pNext              = nullptr,
                                  .pApplicationName   = "Ember",
                                  .applicationVersion = 1,
                                  .pEngineName        = "Ember",
                                  .engineVersion      = 1,
                                  .apiVersion         = VK_MAKE_API_VERSION(0, 1, 4, 0)};

    VkInstanceCreateInfo instance_info = {.sType                 = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                          .pNext                 = &debug_messenger_info,
                                          .flags                 = 0,
                                          .pApplicationInfo      = &app_info,
                                          .enabledLayerCount     = static_cast<uint32_t>(validation_layer_names.size()),
                                          .ppEnabledLayerNames   = validation_layer_names.data(),
                                          .enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()),
                                          .ppEnabledExtensionNames = instance_extensions.data()};

    VkInstance instance = VK_NULL_HANDLE;
    VK_CHECK(vkCreateInstance(&instance_info, nullptr, &instance));
    volkLoadInstance(instance);

    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &debug_messenger_info, nullptr, &debug_messenger));

    spdlog::info("Picking first available physical device");
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);

    std::vector<VkPhysicalDevice> physical_devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data());

    assert(physical_devices.size() > 0);

    VkPhysicalDevice physical_device                           = physical_devices[0];
    auto [physical_device_properies, physical_device_features] = get_physical_device_info(physical_device);
    spdlog::info("Picked device {}", physical_device_properies.deviceName);

    uint32_t graphics_queue_index = find_queue_index(physical_device, VK_QUEUE_GRAPHICS_BIT);

    spdlog::info("Queue index that supports graphics operations: {}", graphics_queue_index);

    std::vector<const char *> device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    float                    queue_prorities   = 1.0f;
    VkDeviceQueueCreateInfo  device_queue_info = {.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                                  .pNext            = nullptr,
                                                  .flags            = 0,
                                                  .queueFamilyIndex = graphics_queue_index,
                                                  .queueCount       = 1,
                                                  .pQueuePriorities = &queue_prorities};
    VkPhysicalDeviceFeatures enabled_features  = {};
    VkDeviceCreateInfo       device_info       = {.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                                  .pNext                = nullptr,
                                                  .flags                = 0,
                                                  .queueCreateInfoCount = 1,
                                                  .pQueueCreateInfos    = &device_queue_info,
                                                  .enabledLayerCount   = static_cast<uint32_t>(validation_layer_names.size()),
                                                  .ppEnabledLayerNames = validation_layer_names.data(),
                                                  .enabledExtensionCount   = static_cast<uint32_t>(device_extensions.size()),
                                                  .ppEnabledExtensionNames = device_extensions.data(),
                                                  .pEnabledFeatures        = &enabled_features

    };

    VkDevice device = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDevice(physical_device, &device_info, nullptr, &device));

    VkQueue graphics_queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, graphics_queue_index, 0, &graphics_queue);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));

    auto [swapchain_capabilities, swapchain_format, swapchain_mode, swapchain_extent] =
        get_swapchain_settings(window, physical_device, surface);

    uint32_t image_count = swapchain_capabilities.minImageCount + 1;
    if (swapchain_capabilities.maxImageCount > 0 && image_count > swapchain_capabilities.maxImageCount) {
        image_count = swapchain_capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchain_info = {.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                               .pNext                 = nullptr,
                                               .flags                 = 0,
                                               .surface               = surface,
                                               .minImageCount         = image_count,
                                               .imageFormat           = swapchain_format.format,
                                               .imageColorSpace       = swapchain_format.colorSpace,
                                               .imageExtent           = swapchain_extent,
                                               .imageArrayLayers      = 1,
                                               .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                               .imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,
                                               .queueFamilyIndexCount = 0,
                                               .pQueueFamilyIndices   = nullptr,
                                               .preTransform          = swapchain_capabilities.currentTransform,
                                               .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                               .presentMode           = swapchain_mode,
                                               .clipped               = VK_TRUE,
                                               .oldSwapchain          = VK_NULL_HANDLE};

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &swapchain));

    uint32_t swapchain_image_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, nullptr);

    std::vector<VkImage> swapchain_images(swapchain_image_count);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, swapchain_images.data());

    std::vector<VkImageView> swapchain_image_views(swapchain_images.size());
    for (int i = 0; i < swapchain_image_views.size(); i++) {
        VkImageViewCreateInfo image_view_info = {.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                                 .pNext            = nullptr,
                                                 .flags            = 0,
                                                 .image            = swapchain_images[i],
                                                 .viewType         = VK_IMAGE_VIEW_TYPE_2D,
                                                 .format           = swapchain_format.format,
                                                 .components       = {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                      .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                      .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                                                                      .a = VK_COMPONENT_SWIZZLE_IDENTITY},
                                                 .subresourceRange = {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                                                      .baseMipLevel   = 0,
                                                                      .levelCount     = 1,
                                                                      .baseArrayLayer = 0,
                                                                      .layerCount     = 1}};

        VK_CHECK(vkCreateImageView(device, &image_view_info, nullptr, &swapchain_image_views[i]));
    }

    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};

    VkSemaphore image_available_semaphore = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available_semaphore));

    VkSemaphore render_finished_semaphore = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished_semaphore));

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        uint32_t image_index = 0;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_available_semaphore, VK_NULL_HANDLE, &image_index);

        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo         submit_info   = {.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                              .pNext                = nullptr,
                                              .waitSemaphoreCount   = 1,
                                              .pWaitSemaphores      = &image_available_semaphore,
                                              .pWaitDstStageMask    = &wait_stages[0],
                                              .commandBufferCount   = 0,
                                              .pCommandBuffers      = nullptr,
                                              .signalSemaphoreCount = 1,
                                              .pSignalSemaphores    = &render_finished_semaphore};
        VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

        VkPresentInfoKHR present_info = {.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                         .pNext              = nullptr,
                                         .waitSemaphoreCount = 1,
                                         .pWaitSemaphores    = &render_finished_semaphore,
                                         .swapchainCount     = 1,
                                         .pSwapchains        = &swapchain,
                                         .pImageIndices      = &image_index,
                                         .pResults           = nullptr};
        VK_CHECK(vkQueuePresentKHR(graphics_queue, &present_info));

        vkDeviceWaitIdle(device);
    }

    vkDestroySemaphore(device, image_available_semaphore, nullptr);
    vkDestroySemaphore(device, render_finished_semaphore, nullptr);
    for (auto view : swapchain_image_views) {
        vkDestroyImageView(device, view, nullptr);
    }
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
