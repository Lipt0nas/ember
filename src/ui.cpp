#include "ui.hpp"

#include "embedded.hpp"
#include "imgui_internal.h"

PFN_vkVoidFunction imgui_load_function(const char* function_name, void* user_data) {
    return vkGetInstanceProcAddr((VkInstance)user_data, function_name);
}

VkDescriptorPool imgui_init(
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
            IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE + 1000,
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

    ImVec4* colors = style.Colors;

    const ImVec4 base     = ImVec4(0.117f, 0.117f, 0.172f, 1.0f); // #1e1e2e
    const ImVec4 mantle   = ImVec4(0.109f, 0.109f, 0.156f, 1.0f); // #181825
    const ImVec4 surface0 = ImVec4(0.200f, 0.207f, 0.286f, 1.0f); // #313244
    const ImVec4 surface1 = ImVec4(0.247f, 0.254f, 0.337f, 1.0f); // #3f4056
    const ImVec4 surface2 = ImVec4(0.290f, 0.301f, 0.388f, 1.0f); // #4a4d63
    const ImVec4 overlay0 = ImVec4(0.396f, 0.403f, 0.486f, 1.0f); // #65677c
    const ImVec4 overlay2 = ImVec4(0.576f, 0.584f, 0.654f, 1.0f); // #9399b2
    const ImVec4 text     = ImVec4(0.803f, 0.815f, 0.878f, 1.0f); // #cdd6f4
    const ImVec4 subtext0 = ImVec4(0.639f, 0.658f, 0.764f, 1.0f); // #a3a8c3
    const ImVec4 mauve    = ImVec4(0.796f, 0.698f, 0.972f, 1.0f); // #cba6f7
    const ImVec4 peach    = ImVec4(0.980f, 0.709f, 0.572f, 1.0f); // #fab387
    const ImVec4 yellow   = ImVec4(0.980f, 0.913f, 0.596f, 1.0f); // #f9e2af
    const ImVec4 green    = ImVec4(0.650f, 0.890f, 0.631f, 1.0f); // #a6e3a1
    const ImVec4 teal     = ImVec4(0.580f, 0.886f, 0.819f, 1.0f); // #94e2d5
    const ImVec4 sapphire = ImVec4(0.458f, 0.784f, 0.878f, 1.0f); // #74c7ec
    const ImVec4 blue     = ImVec4(0.533f, 0.698f, 0.976f, 1.0f); // #89b4fa
    const ImVec4 lavender = ImVec4(0.709f, 0.764f, 0.980f, 1.0f); // #b4befe

    colors[ImGuiCol_WindowBg]              = base;
    colors[ImGuiCol_ChildBg]               = base;
    colors[ImGuiCol_PopupBg]               = surface0;
    colors[ImGuiCol_Border]                = surface1;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg]               = surface0;
    colors[ImGuiCol_FrameBgHovered]        = surface1;
    colors[ImGuiCol_FrameBgActive]         = surface2;
    colors[ImGuiCol_TitleBg]               = mantle;
    colors[ImGuiCol_TitleBgActive]         = surface0;
    colors[ImGuiCol_TitleBgCollapsed]      = mantle;
    colors[ImGuiCol_MenuBarBg]             = mantle;
    colors[ImGuiCol_ScrollbarBg]           = surface0;
    colors[ImGuiCol_ScrollbarGrab]         = surface2;
    colors[ImGuiCol_ScrollbarGrabHovered]  = overlay0;
    colors[ImGuiCol_ScrollbarGrabActive]   = overlay2;
    colors[ImGuiCol_CheckMark]             = green;
    colors[ImGuiCol_SliderGrab]            = sapphire;
    colors[ImGuiCol_SliderGrabActive]      = blue;
    colors[ImGuiCol_Button]                = surface0;
    colors[ImGuiCol_ButtonHovered]         = surface1;
    colors[ImGuiCol_ButtonActive]          = surface2;
    colors[ImGuiCol_Header]                = surface0;
    colors[ImGuiCol_HeaderHovered]         = surface1;
    colors[ImGuiCol_HeaderActive]          = surface2;
    colors[ImGuiCol_Separator]             = surface1;
    colors[ImGuiCol_SeparatorHovered]      = mauve;
    colors[ImGuiCol_SeparatorActive]       = mauve;
    colors[ImGuiCol_ResizeGrip]            = surface2;
    colors[ImGuiCol_ResizeGripHovered]     = mauve;
    colors[ImGuiCol_ResizeGripActive]      = mauve;
    colors[ImGuiCol_Tab]                   = surface0;
    colors[ImGuiCol_TabHovered]            = surface2;
    colors[ImGuiCol_TabActive]             = surface1;
    colors[ImGuiCol_TabUnfocused]          = surface0;
    colors[ImGuiCol_TabUnfocusedActive]    = surface1;
    colors[ImGuiCol_DockingPreview]        = sapphire;
    colors[ImGuiCol_DockingEmptyBg]        = base;
    colors[ImGuiCol_PlotLines]             = blue;
    colors[ImGuiCol_PlotLinesHovered]      = peach;
    colors[ImGuiCol_PlotHistogram]         = teal;
    colors[ImGuiCol_PlotHistogramHovered]  = green;
    colors[ImGuiCol_TableHeaderBg]         = surface0;
    colors[ImGuiCol_TableBorderStrong]     = surface1;
    colors[ImGuiCol_TableBorderLight]      = surface0;
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
    colors[ImGuiCol_TextSelectedBg]        = surface2;
    colors[ImGuiCol_DragDropTarget]        = yellow;
    colors[ImGuiCol_NavHighlight]          = lavender;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
    colors[ImGuiCol_Text]                  = text;
    colors[ImGuiCol_TextDisabled]          = subtext0;

    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;

    style.WindowPadding    = ImVec2(8.0f, 8.0f);
    style.FramePadding     = ImVec2(5.0f, 3.0f);
    style.ItemSpacing      = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.IndentSpacing    = 21.0f;
    style.ScrollbarSize    = 14.0f;
    style.GrabMinSize      = 10.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize  = 1.0f;
    style.PopupBorderSize  = 1.0f;
    style.FrameBorderSize  = 0.0f;
    style.TabBorderSize    = 0.0f;

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding              = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImFontConfig config;
    io.Fonts->AddFontFromMemoryTTF((void*)&embedded::roboto_font[0], embedded::roboto_font_size, 13.0f, &config);

    config.MergeMode              = true;
    config.GlyphMinAdvanceX       = 13.0f;
    static ImWchar glyph_ranges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
    io.Fonts->AddFontFromMemoryTTF(
        (void*)&embedded::icon_font[0], embedded::icon_font_size, 13.0f, &config, glyph_ranges
    );

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
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance                  = instance;
    init_info.PhysicalDevice            = physical_device;
    init_info.Device                    = device;
    init_info.QueueFamily               = graphics_family_index;
    init_info.Queue                     = graphics_queue;
    init_info.PipelineCache             = nullptr;
    init_info.DescriptorPool            = imgui_descriptor_pool;
    init_info.MinImageCount             = image_count;
    init_info.ImageCount                = image_count;
    init_info.Allocator                 = nullptr;
    init_info.UseDynamicRendering       = true;
    init_info.CheckVkResultFn           = nullptr;
    init_info.PipelineInfoMain          = {
                 .RenderPass                  = VK_NULL_HANDLE,
                 .Subpass                     = 0,
                 .MSAASamples                 = VK_SAMPLE_COUNT_1_BIT,
                 .PipelineRenderingCreateInfo = imgui_rendering_info,
    };
    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_4, imgui_load_function, instance);
    ImGui_ImplVulkan_Init(&init_info);

    return imgui_descriptor_pool;
}

ImFont* generate_icon_font(float size) {
    static ImWchar glyph_ranges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};

    ImFontConfig config;
    config.GlyphMinAdvanceX = 48.0f;

    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
        (void*)&embedded::icon_font[0], embedded::icon_font_size, 48.0f, &config, glyph_ranges
    );
}

VkDescriptorSet imgui_image_handle(const Image& image, VkSampler sampler) {
    return ImGui_ImplVulkan_AddTexture(sampler, image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void imgui_image_handle_free(VkDescriptorSet handle) {
    ImGui_ImplVulkan_RemoveTexture(handle);
}

bool imgui_splitter(
    bool   split_vertically,
    float  thickness,
    float* size1,
    float* size2,
    float  min_size1,
    float  min_size2,
    float  splitter_long_axis_size
) {
    ImGuiContext& g      = *GImGui;
    ImGuiWindow*  window = g.CurrentWindow;
    ImGuiID       id     = window->GetID("##Splitter");
    ImRect        bb;

    bb.Min = window->DC.CursorPos + (split_vertically ? ImVec2(*size1, 0.0f) : ImVec2(0.0f, *size1));
    bb.Max = bb.Min + ImGui::CalcItemSize(
                          split_vertically ? ImVec2(thickness, splitter_long_axis_size)
                                           : ImVec2(splitter_long_axis_size, thickness),
                          0.0f,
                          0.0f
                      );

    return ImGui::SplitterBehavior(
        bb, id, split_vertically ? ImGuiAxis_X : ImGuiAxis_Y, size1, size2, min_size1, min_size2, 0.0f
    );
}
