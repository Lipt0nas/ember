#define VOLK_IMPLEMENTATION
#include <volk.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <vector>

const std::vector<const char*> validation_layer_names = {"VK_LAYER_KHRONOS_validation"};

#define VK_CHECK(call)                                                                                                 \
    do {                                                                                                               \
        VkResult result_ = call;                                                                                       \
        assert(result_ == VK_SUCCESS);                                                                                 \
    } while (0)

VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*                                       pUserData
) {
    spdlog::debug("VK validation: {}", pCallbackData->pMessage);

    return VK_FALSE;
}

PFN_vkVoidFunction imgui_load_function(const char* function_name, void* user_data) {
    return vkGetInstanceProcAddr((VkInstance)user_data, function_name);
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
        const auto& queue = queue_properties[i];
        if (queue.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_index = i;
        }
    }

    assert(queue_index != UINT32_MAX);

    return queue_index;
}

std::tuple<VkSurfaceCapabilitiesKHR, VkSurfaceFormatKHR, VkPresentModeKHR, VkExtent2D>
get_swapchain_settings(SDL_Window* window, VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
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
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
            surface_format = format;
            break;
        }
    }

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto& mode : present_modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode = mode;
            break;
        }
    }

    int width;
    int height;
    SDL_GetWindowSize(window, &width, &height);

    VkExtent2D extent = {.width = static_cast<uint32_t>(width), .height = static_cast<uint32_t>(height)};

    extent.width  = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return {capabilities, surface_format, present_mode, extent};
}

VkShaderModule shader_module_from_file(VkDevice device, const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::in | std::ios::ate);

    if (file.is_open()) {
        size_t                 length = static_cast<size_t>(file.tellg());
        std::vector<std::byte> buffer(length);

        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), length);

        VkShaderModuleCreateInfo module_info = {
            .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext    = nullptr,
            .flags    = 0,
            .codeSize = buffer.size(),
            .pCode    = reinterpret_cast<const uint32_t*>(buffer.data())
        };

        VkShaderModule module = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(device, &module_info, nullptr, &module));

        return module;
    }

    return VK_NULL_HANDLE;
}

int main() {
    spdlog::set_level(spdlog::level::trace);
    spdlog::info("Starting ember");

    VK_CHECK(volkInitialize());

    spdlog::info("Initializing SDL");
    if (!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO)) {
        spdlog::error("Failed to initialize SDL");
        return 1;
    }

    auto* window = SDL_CreateWindow("Ember", 1280, 720, SDL_WINDOW_VULKAN);
    if (!window) {
        spdlog::error("Failed to create SDL window");
        return 1;
    }

    uint32_t extension_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
    spdlog::debug("Instance supports {} extensions", extension_count);

    uint32_t sdl_extension_count = 0;
    auto     sdl_extensions      = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);

    spdlog::debug("SDL requires {} extensions", sdl_extension_count);

    std::vector<const char*> instance_extensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    for (uint32_t i = 0; i < sdl_extension_count; i++) {
        instance_extensions.push_back(sdl_extensions[i]);
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
        .pUserData       = nullptr
    };

    VkApplicationInfo app_info = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = nullptr,
        .pApplicationName   = "Ember",
        .applicationVersion = 1,
        .pEngineName        = "Ember",
        .engineVersion      = 1,
        .apiVersion         = VK_MAKE_API_VERSION(0, 1, 4, 0)
    };

    VkInstanceCreateInfo instance_info = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = &debug_messenger_info,
        .flags                   = 0,
        .pApplicationInfo        = &app_info,
        .enabledLayerCount       = static_cast<uint32_t>(validation_layer_names.size()),
        .ppEnabledLayerNames     = validation_layer_names.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.data()
    };

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

    std::vector<const char*> device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    float                   queue_prorities   = 1.0f;
    VkDeviceQueueCreateInfo device_queue_info = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .queueFamilyIndex = graphics_queue_index,
        .queueCount       = 1,
        .pQueuePriorities = &queue_prorities
    };

    VkPhysicalDeviceVulkan13Features enabled_features_13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = nullptr
    };
    enabled_features_13.dynamicRendering = VK_TRUE;
    enabled_features_13.synchronization2 = VK_TRUE;

    VkDeviceCreateInfo device_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &enabled_features_13,
        .flags                   = 0,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &device_queue_info,
        .enabledLayerCount       = static_cast<uint32_t>(validation_layer_names.size()),
        .ppEnabledLayerNames     = validation_layer_names.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
        .pEnabledFeatures        = nullptr
    };

    VkDevice device = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDevice(physical_device, &device_info, nullptr, &device));

    VkQueue graphics_queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, graphics_queue_index, 0, &graphics_queue);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        spdlog::error("Failed to create SDL window surface");
        return 1;
    }

    auto [swapchain_capabilities, swapchain_format, swapchain_mode, swapchain_extent] =
        get_swapchain_settings(window, physical_device, surface);

    uint32_t image_count = swapchain_capabilities.minImageCount + 1;
    if (swapchain_capabilities.maxImageCount > 0 && image_count > swapchain_capabilities.maxImageCount) {
        image_count = swapchain_capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchain_info = {
        .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
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
            .format   = swapchain_format.format,
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

    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0
    };

    VkSemaphore image_available_semaphore = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available_semaphore));

    VkSemaphore render_finished_semaphore = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished_semaphore));

    VkCommandPoolCreateInfo command_pool_info = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphics_queue_index
    };

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(device, &command_pool_info, nullptr, &command_pool));

    VkCommandBufferAllocateInfo command_buffer_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device, &command_buffer_info, &command_buffer));

    VkPipelineRenderingCreateInfo pipeline_rendering_info = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext                   = nullptr,
        .viewMask                = 0,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &swapchain_format.format,
        .depthAttachmentFormat   = VK_FORMAT_UNDEFINED,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };

    VkShaderModule vertex_module   = shader_module_from_file(device, "data/shaders/vert.spv");
    VkShaderModule fragment_module = shader_module_from_file(device, "data/shaders/frag.spv");

    VkPipelineShaderStageCreateInfo shader_stage_infos[] = {
        {.sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext               = nullptr,
         .flags               = 0,
         .stage               = VK_SHADER_STAGE_VERTEX_BIT,
         .module              = vertex_module,
         .pName               = "main",
         .pSpecializationInfo = nullptr},
        {.sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext               = nullptr,
         .flags               = 0,
         .stage               = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module              = fragment_module,
         .pName               = "main",
         .pSpecializationInfo = nullptr}
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext                           = nullptr,
        .flags                           = 0,
        .vertexBindingDescriptionCount   = 0,
        .pVertexBindingDescriptions      = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions    = nullptr
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    VkViewport viewport = {
        .x        = 0,
        .y        = 0,
        .width    = static_cast<float>(swapchain_extent.width),
        .height   = static_cast<float>(swapchain_extent.height),
        .minDepth = 0.0,
        .maxDepth = 1.0
    };

    VkRect2D scissor = {
        .offset = {.x = 0, .y = 0},
        .extent = {.width = swapchain_extent.width, .height = swapchain_extent.height},
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = 0,
        .viewportCount = 1,
        .pViewports    = &viewport,
        .scissorCount  = 1,
        .pScissors     = &scissor
    };

    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable   = VK_FALSE,
        .minSampleShading      = 0.0f,
        .pSampleMask           = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable      = VK_FALSE
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_FRONT_BIT,
        .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp          = 0.0f,
        .depthBiasSlopeFactor    = 0.0f,
        .lineWidth               = 1.0f
    };

    VkPipelineColorBlendAttachmentState color_blend_attachment_state = {
        .blendEnable         = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments    = &color_blend_attachment_state,
        .blendConstants  = {0.0f, 0.0f, 0.0f, 0.0f}
    };

    VkPipelineLayoutCreateInfo layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .setLayoutCount         = 0,
        .pSetLayouts            = nullptr,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges    = nullptr
    };

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout));

    VkGraphicsPipelineCreateInfo graphics_pipeline_info = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = &pipeline_rendering_info,
        .flags               = 0,
        .stageCount          = 2,
        .pStages             = &shader_stage_infos[0],
        .pVertexInputState   = &vertex_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pTessellationState  = nullptr,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState   = &multisample_state,
        .pDepthStencilState  = nullptr,
        .pColorBlendState    = &color_blend_state,
        .pDynamicState       = nullptr,
        .layout              = pipeline_layout,
        .renderPass          = VK_NULL_HANDLE,
        .subpass             = 0,
        .basePipelineHandle  = VK_NULL_HANDLE,
        .basePipelineIndex   = 0
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphics_pipeline_info, nullptr, &pipeline));

    VkDescriptorPoolSize imgui_pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE}
    };

    uint32_t max_imgui_sets = 0;
    for (VkDescriptorPoolSize& size : imgui_pool_sizes) {
        max_imgui_sets += size.descriptorCount;
    }

    VkDescriptorPoolCreateInfo imgui_pool_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = 0,
        .maxSets       = max_imgui_sets,
        .poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(imgui_pool_sizes)),
        .pPoolSizes    = imgui_pool_sizes

    };

    VkDescriptorPool imgui_descriptor_pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(device, &imgui_pool_info, nullptr, &imgui_descriptor_pool));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsLight();

    ImGuiStyle& style          = ImGui::GetStyle();
    io.ConfigDpiScaleFonts     = true;
    io.ConfigDpiScaleViewports = true;

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding              = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    VkPipelineRenderingCreateInfoKHR imgui_rendering_info = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext                   = nullptr,
        .viewMask                = 0,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &swapchain_format.format,
        .depthAttachmentFormat   = VK_FORMAT_UNDEFINED,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };

    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo init_info   = {};
    init_info.Instance                    = instance;
    init_info.PhysicalDevice              = physical_device;
    init_info.Device                      = device;
    init_info.QueueFamily                 = graphics_queue_index;
    init_info.Queue                       = graphics_queue;
    init_info.PipelineCache               = nullptr;
    init_info.DescriptorPool              = imgui_descriptor_pool;
    init_info.MinImageCount               = 2;
    init_info.ImageCount                  = 2;
    init_info.Subpass                     = 0;
    init_info.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator                   = nullptr;
    init_info.UseDynamicRendering         = true;
    init_info.RenderPass                  = VK_NULL_HANDLE;
    init_info.PipelineRenderingCreateInfo = imgui_rendering_info;
    init_info.CheckVkResultFn             = nullptr;
    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, imgui_load_function, instance);
    ImGui_ImplVulkan_Init(&init_info);

    bool running = true;
    while (running) {
        SDL_Event window_event;
        while (SDL_PollEvent(&window_event)) {
            ImGui_ImplSDL3_ProcessEvent(&window_event);
            switch (window_event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            }
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::ShowDemoWindow();

        ImGui::Render();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();

        uint32_t image_index = 0;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_available_semaphore, VK_NULL_HANDLE, &image_index);

        VkCommandBufferBeginInfo begin_info = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .pInheritanceInfo = nullptr
        };
        vkBeginCommandBuffer(command_buffer, &begin_info);

        VkImageMemoryBarrier2 to_render_barrier = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext               = nullptr,
            .srcStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .srcAccessMask       = 0,
            .dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = swapchain_images[image_index],
            .subresourceRange    = {
                   .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                   .baseMipLevel   = 0,
                   .levelCount     = 1,
                   .baseArrayLayer = 0,
                   .layerCount     = 1
            }
        };

        VkDependencyInfo to_dependency = {
            .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext                    = nullptr,
            .dependencyFlags          = 0,
            .memoryBarrierCount       = 0,
            .pMemoryBarriers          = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers    = nullptr,
            .imageMemoryBarrierCount  = 1,
            .pImageMemoryBarriers     = &to_render_barrier
        };
        vkCmdPipelineBarrier2(command_buffer, &to_dependency);

        VkRenderingAttachmentInfo attachment_info = {
            .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext              = nullptr,
            .imageView          = swapchain_image_views[image_index],
            .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode        = VK_RESOLVE_MODE_NONE,
            .resolveImageView   = VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue         = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}}
        };

        VkRenderingInfo rendering_info = {
            .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .pNext                = nullptr,
            .flags                = 0,
            .renderArea           = {.offset = {.x = 0, .y = 0}, .extent = swapchain_extent},
            .layerCount           = 1,
            .viewMask             = 0,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &attachment_info,
            .pDepthAttachment     = nullptr,
            .pStencilAttachment   = nullptr
        };

        vkCmdBeginRendering(command_buffer, &rendering_info);

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdDraw(command_buffer, 3, 1, 0, 0);

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);

        vkCmdEndRendering(command_buffer);

        VkImageMemoryBarrier2 to_present_barrier = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext               = nullptr,
            .srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask        = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            .dstAccessMask       = 0,
            .oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = swapchain_images[image_index],
            .subresourceRange    = {
                   .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                   .baseMipLevel   = 0,
                   .levelCount     = 1,
                   .baseArrayLayer = 0,
                   .layerCount     = 1
            }
        };

        VkDependencyInfo present_dependency = {
            .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext                    = nullptr,
            .dependencyFlags          = 0,
            .memoryBarrierCount       = 0,
            .pMemoryBarriers          = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers    = nullptr,
            .imageMemoryBarrierCount  = 1,
            .pImageMemoryBarriers     = &to_present_barrier
        };
        vkCmdPipelineBarrier2(command_buffer, &present_dependency);
        VK_CHECK(vkEndCommandBuffer(command_buffer));

        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo         submit_info   = {
                      .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                      .pNext                = nullptr,
                      .waitSemaphoreCount   = 1,
                      .pWaitSemaphores      = &image_available_semaphore,
                      .pWaitDstStageMask    = &wait_stages[0],
                      .commandBufferCount   = 1,
                      .pCommandBuffers      = &command_buffer,
                      .signalSemaphoreCount = 1,
                      .pSignalSemaphores    = &render_finished_semaphore
        };
        VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

        VkPresentInfoKHR present_info = {
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext              = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &render_finished_semaphore,
            .swapchainCount     = 1,
            .pSwapchains        = &swapchain,
            .pImageIndices      = &image_index,
            .pResults           = nullptr
        };
        VK_CHECK(vkQueuePresentKHR(graphics_queue, &present_info));

        vkDeviceWaitIdle(device);
    }

    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplVulkan_Shutdown();
    vkDestroyDescriptorPool(device, imgui_descriptor_pool, nullptr);
    vkDestroyShaderModule(device, vertex_module, nullptr);
    vkDestroyShaderModule(device, fragment_module, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyCommandPool(device, command_pool, nullptr);
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
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
