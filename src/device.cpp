#include "device.hpp"

const std::vector<const char*> validation_layer_names = {"VK_LAYER_KHRONOS_validation"};

VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severety,
    VkDebugUtilsMessageTypeFlagsEXT             type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void*                                       user_data
) {
    if (severety & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        spdlog::error("[VK error]: {}", callback_data->pMessage);
    } else if (severety & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        spdlog::warn("[VK warn]: {}", callback_data->pMessage);
    } else {
        spdlog::info("[VK info]: {}", callback_data->pMessage);
    }

    if (severety & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        assert(false && "Validation layer error encountered");
    }

    return VK_FALSE;
}

VkInstance create_instance(bool enable_validation, VkDebugUtilsMessengerEXT& debug_messenger) {
    uint32_t extension_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
    spdlog::debug("Instance supports {} extensions", extension_count);

    uint32_t sdl_extension_count = 0;
    auto     sdl_extensions      = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);

    spdlog::debug("SDL requires {} extensions", sdl_extension_count);

    std::vector<const char*> instance_extensions = {};
    for (uint32_t i = 0; i < sdl_extension_count; i++) {
        instance_extensions.push_back(sdl_extensions[i]);
    }

    VkValidationFeatureEnableEXT validation_enables[] = {
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT, VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT
    };
    VkValidationFeaturesEXT validation_features = {
        .sType                          = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext                          = nullptr,
        .enabledValidationFeatureCount  = 2,
        .pEnabledValidationFeatures     = validation_enables,
        .disabledValidationFeatureCount = 0,
        .pDisabledValidationFeatures    = nullptr
    };

    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_info = {
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext           = &validation_features,
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
        .pNext                   = enable_validation ? &debug_messenger_info : nullptr,
        .flags                   = 0,
        .pApplicationInfo        = &app_info,
        .enabledLayerCount       = enable_validation ? static_cast<uint32_t>(validation_layer_names.size()) : 0,
        .ppEnabledLayerNames     = enable_validation ? validation_layer_names.data() : nullptr,
        .enabledExtensionCount   = static_cast<uint32_t>(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.data()
    };

    VkInstance instance = VK_NULL_HANDLE;
    VK_CHECK(vkCreateInstance(&instance_info, nullptr, &instance));
    volkLoadInstance(instance);

    if (enable_validation) {
        debug_messenger = VK_NULL_HANDLE;
        VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &debug_messenger_info, nullptr, &debug_messenger));
    }

    return instance;
}

VkPhysicalDevice pick_physical_device(VkInstance instance) {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);

    std::vector<VkPhysicalDevice> physical_devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data());

    assert(physical_devices.size() > 0);

    // TODO: actually pick a device
    VkPhysicalDevice physical_device = physical_devices[0];

    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures   features;

    vkGetPhysicalDeviceProperties(physical_device, &properties);
    vkGetPhysicalDeviceFeatures(physical_device, &features);

    spdlog::info("Picked device {}", properties.deviceName);

    return physical_device;
}

uint32_t get_graphics_family_index(VkPhysicalDevice physical_device) {
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

VkDevice create_device(
    VkInstance       instance,
    VkPhysicalDevice physical_device,
    uint32_t         graphics_family_index,
    bool             enable_validation,
    bool             use_meshlets,
    bool             use_hardware_rt
) {
    std::vector<const char*> device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    if (use_meshlets) {
        device_extensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    }

    if (use_hardware_rt) {
        device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        device_extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);

        device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    }

    float                   queue_prorities   = 1.0f;
    VkDeviceQueueCreateInfo device_queue_info = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .queueFamilyIndex = graphics_family_index,
        .queueCount       = 1,
        .pQueuePriorities = &queue_prorities
    };

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
    };
    ray_tracing_pipeline_features.rayTracingPipeline = VK_TRUE;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext = &ray_tracing_pipeline_features,
    };
    acceleration_structure_features.accelerationStructure = VK_TRUE;

    VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_features = {
        .sType                                  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
        .pNext                                  = use_hardware_rt ? &acceleration_structure_features : nullptr,
        .taskShader                             = VK_TRUE,
        .meshShader                             = VK_TRUE,
        .multiviewMeshShader                    = VK_FALSE,
        .primitiveFragmentShadingRateMeshShader = VK_FALSE,
        .meshShaderQueries                      = VK_FALSE
    };

    void* feature_chain =
        (use_meshlets ? (void*)&mesh_shader_features : (use_hardware_rt ? &acceleration_structure_features : nullptr));

    VkPhysicalDeviceFeatures features  = {};
    features.samplerAnisotropy         = VK_TRUE;
    features.multiDrawIndirect         = VK_TRUE;
    features.drawIndirectFirstInstance = VK_TRUE;

    VkPhysicalDeviceVulkan11Features vulkan_features_11 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = feature_chain,
    };
    vulkan_features_11.shaderDrawParameters = VK_TRUE;

    VkPhysicalDeviceVulkan12Features vulkan_features_12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &vulkan_features_11,
    };
    vulkan_features_12.storageBuffer8BitAccess                      = VK_TRUE;
    vulkan_features_12.drawIndirectCount                            = VK_TRUE;
    vulkan_features_12.scalarBlockLayout                            = VK_TRUE;
    vulkan_features_12.bufferDeviceAddress                          = VK_TRUE;
    vulkan_features_12.descriptorIndexing                           = VK_TRUE;
    vulkan_features_12.runtimeDescriptorArray                       = VK_TRUE;
    vulkan_features_12.descriptorBindingPartiallyBound              = VK_TRUE;
    vulkan_features_12.descriptorBindingVariableDescriptorCount     = VK_TRUE;
    vulkan_features_12.shaderSampledImageArrayNonUniformIndexing    = VK_TRUE;
    vulkan_features_12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    vulkan_features_12.hostQueryReset                               = VK_TRUE;
    vulkan_features_12.uniformAndStorageBuffer8BitAccess            = VK_TRUE;
    vulkan_features_12.samplerFilterMinmax                          = VK_TRUE;

    VkPhysicalDeviceVulkan13Features vulkan_features_13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &vulkan_features_12,
    };
    vulkan_features_13.dynamicRendering = VK_TRUE;
    vulkan_features_13.synchronization2 = VK_TRUE;
    vulkan_features_13.maintenance4     = VK_TRUE;

    VkPhysicalDeviceVulkan14Features enabled_features_14 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .pNext = &vulkan_features_13,
    };
    enabled_features_14.pushDescriptor = VK_TRUE;

    VkDeviceCreateInfo device_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &enabled_features_14,
        .flags                   = 0,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &device_queue_info,
        .enabledLayerCount       = enable_validation ? static_cast<uint32_t>(validation_layer_names.size()) : 0,
        .ppEnabledLayerNames     = enable_validation ? validation_layer_names.data() : nullptr,
        .enabledExtensionCount   = static_cast<uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
        .pEnabledFeatures        = &features
    };

    VkDevice device = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDevice(physical_device, &device_info, nullptr, &device));

    return device;
}
