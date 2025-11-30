#include "ui.hpp"

PFN_vkVoidFunction imgui_load_function(const char* function_name, void* user_data) {
    return vkGetInstanceProcAddr((VkInstance)user_data, function_name);
}

VkDescriptorPool init_imgui(
    SDL_Window*      window,
    VkInstance       instance,
    VkPhysicalDevice physical_device,
    VkDevice         device,
    VkFormat         swapchain_format,
    uint32_t         graphics_family_index,
    VkQueue          graphics_queue,
    uint32_t         image_count
) {
    VkDescriptorPoolSize imgui_pool_sizes[] = {
        {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE + 100,
        },
    };

    uint32_t max_imgui_sets = 0;
    for (VkDescriptorPoolSize& size : imgui_pool_sizes) {
        max_imgui_sets += size.descriptorCount;
    }

    VkDescriptorPoolCreateInfo imgui_pool_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets       = max_imgui_sets,
        .poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(imgui_pool_sizes)),
        .pPoolSizes    = imgui_pool_sizes

    };

    VkDescriptorPool imgui_descriptor_pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(device, &imgui_pool_info, nullptr, &imgui_descriptor_pool));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGuiStyle& style          = ImGui::GetStyle();
    io.ConfigDpiScaleFonts     = true;
    io.ConfigDpiScaleViewports = true;

    ImGui::StyleColorsDark();

    constexpr auto accent             = IM_COL32(236, 158, 36, 255);
    constexpr auto highlight          = IM_COL32(39, 185, 242, 120);
    constexpr auto niceBlue           = IM_COL32(83, 232, 254, 120);
    constexpr auto compliment         = IM_COL32(78, 151, 166, 100);
    constexpr auto background         = IM_COL32(36, 36, 36, 255);
    constexpr auto backgroundDark     = IM_COL32(26, 26, 26, 255);
    constexpr auto titlebar           = IM_COL32(21, 21, 21, 255);
    constexpr auto propertyField      = IM_COL32(15, 15, 15, 255);
    constexpr auto text               = IM_COL32(192, 192, 192, 255);
    constexpr auto textBrighter       = IM_COL32(210, 210, 210, 255);
    constexpr auto textDarker         = IM_COL32(128, 128, 128, 255);
    constexpr auto muted              = IM_COL32(77, 77, 77, 255);
    constexpr auto groupHeader        = IM_COL32(47, 47, 47, 255);
    constexpr auto groupHeaderHovered = IM_COL32(56, 56, 56, 255);
    constexpr auto groupHeaderActive  = IM_COL32(62, 62, 62, 255);
    constexpr auto selection          = IM_COL32(191, 177, 155, 255);
    constexpr auto selectionMuted     = IM_COL32(59, 57, 45, 255);
    // constexpr auto selection          = IM_COL32(237, 192, 119, 255);
    // constexpr auto selectionMuted     = IM_COL32(237, 201, 142, 23);
    constexpr auto backgroundPopup = background;

    auto& colors                   = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Header]        = ImGui::ColorConvertU32ToFloat4(groupHeader);
    colors[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(groupHeaderHovered);
    colors[ImGuiCol_HeaderActive]  = ImGui::ColorConvertU32ToFloat4(groupHeaderActive);

    colors[ImGuiCol_Button]        = ImColor(56, 56, 56, 200);
    colors[ImGuiCol_ButtonHovered] = ImColor(70, 70, 70, 255);
    colors[ImGuiCol_ButtonActive]  = ImColor(56, 56, 56, 150);

    colors[ImGuiCol_FrameBg]        = ImGui::ColorConvertU32ToFloat4(propertyField);
    colors[ImGuiCol_FrameBgHovered] = ImGui::ColorConvertU32ToFloat4(propertyField);
    colors[ImGuiCol_FrameBgActive]  = ImGui::ColorConvertU32ToFloat4(propertyField);

    colors[ImGuiCol_Tab]                = ImGui::ColorConvertU32ToFloat4(titlebar);
    colors[ImGuiCol_TabHovered]         = ImGui::ColorConvertU32ToFloat4(niceBlue);
    colors[ImGuiCol_TabActive]          = ImGui::ColorConvertU32ToFloat4(highlight);
    colors[ImGuiCol_TabUnfocused]       = ImGui::ColorConvertU32ToFloat4(titlebar);
    colors[ImGuiCol_TabUnfocusedActive] = ImGui::ColorConvertU32ToFloat4(compliment);

    colors[ImGuiCol_TitleBg]          = ImGui::ColorConvertU32ToFloat4(titlebar);
    colors[ImGuiCol_TitleBgActive]    = ImGui::ColorConvertU32ToFloat4(titlebar);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

    colors[ImGuiCol_ResizeGrip]        = ImVec4(0.91f, 0.91f, 0.91f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.81f, 0.81f, 0.81f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]  = ImVec4(0.46f, 0.46f, 0.46f, 0.95f);

    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.31f, 0.31f, 0.31f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.51f, 0.51f, 0.51f, 1.0f);

    colors[ImGuiCol_CheckMark] = ImColor(200, 200, 200, 255);

    colors[ImGuiCol_SliderGrab]       = ImVec4(0.51f, 0.51f, 0.51f, 0.7f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.66f, 0.66f, 0.66f, 1.0f);

    colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(text);

    colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(text);

    colors[ImGuiCol_Separator]        = ImGui::ColorConvertU32ToFloat4(backgroundDark);
    colors[ImGuiCol_SeparatorActive]  = ImGui::ColorConvertU32ToFloat4(highlight);
    colors[ImGuiCol_SeparatorHovered] = ImColor(39, 185, 242, 150);

    colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(titlebar);
    colors[ImGuiCol_ChildBg]  = ImGui::ColorConvertU32ToFloat4(background);
    colors[ImGuiCol_PopupBg]  = ImGui::ColorConvertU32ToFloat4(backgroundPopup);
    colors[ImGuiCol_Border]   = ImGui::ColorConvertU32ToFloat4(backgroundDark);

    colors[ImGuiCol_TableHeaderBg]    = ImGui::ColorConvertU32ToFloat4(groupHeader);
    colors[ImGuiCol_TableBorderLight] = ImGui::ColorConvertU32ToFloat4(backgroundDark);

    colors[ImGuiCol_MenuBarBg] = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};

    style.FrameRounding               = 2.5f;
    style.FrameBorderSize             = 1.0f;
    style.IndentSpacing               = 11.0f;
    style.WindowRounding              = 0.0f;
    style.FrameBorderSize             = 0.0f;
    style.WindowBorderSize            = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding              = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // NOTE: Little hack to make the ui look a little better when under sRGB
    // for (int i = 0; i < ImGuiCol_COUNT; i++) {
    //     ImVec4& col = style.Colors[i];
    //     col.x       = col.x <= 0.04045f ? col.x / 12.92f : pow((col.x + 0.055f) / 1.055f, 2.4f);
    //     col.y       = col.y <= 0.04045f ? col.y / 12.92f : pow((col.y + 0.055f) / 1.055f, 2.4f);
    //     col.z       = col.z <= 0.04045f ? col.z / 12.92f : pow((col.z + 0.055f) / 1.055f, 2.4f);
    // }

    VkPipelineRenderingCreateInfo imgui_rendering_info = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext                   = nullptr,
        .viewMask                = 0,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &swapchain_format,
        .depthAttachmentFormat   = VK_FORMAT_UNDEFINED,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };

    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo init_info   = {};
    init_info.Instance                    = instance;
    init_info.PhysicalDevice              = physical_device;
    init_info.Device                      = device;
    init_info.QueueFamily                 = graphics_family_index;
    init_info.Queue                       = graphics_queue;
    init_info.PipelineCache               = nullptr;
    init_info.DescriptorPool              = imgui_descriptor_pool;
    init_info.MinImageCount               = image_count;
    init_info.ImageCount                  = image_count;
    init_info.Subpass                     = 0;
    init_info.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator                   = nullptr;
    init_info.UseDynamicRendering         = true;
    init_info.RenderPass                  = VK_NULL_HANDLE;
    init_info.PipelineRenderingCreateInfo = imgui_rendering_info;
    init_info.CheckVkResultFn             = nullptr;
    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_4, imgui_load_function, instance);
    ImGui_ImplVulkan_Init(&init_info);

    return imgui_descriptor_pool;
}
