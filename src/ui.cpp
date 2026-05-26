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

    style.Colors[ImGuiCol_Text]                  = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled]          = ImVec4(0.27450982f, 0.31764707f, 0.4509804f, 1.0f);
    style.Colors[ImGuiCol_WindowBg]              = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
    style.Colors[ImGuiCol_ChildBg]               = ImVec4(0.09411765f, 0.101960786f, 0.11764706f, 1.0f);
    style.Colors[ImGuiCol_PopupBg]               = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
    style.Colors[ImGuiCol_Border]                = ImVec4(0.15686275f, 0.16862746f, 0.19215687f, 1.0f);
    style.Colors[ImGuiCol_BorderShadow]          = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
    style.Colors[ImGuiCol_FrameBg]               = ImVec4(0.11372549f, 0.1254902f, 0.15294118f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.15686275f, 0.16862746f, 0.19215687f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive]         = ImVec4(0.15686275f, 0.16862746f, 0.19215687f, 1.0f);
    style.Colors[ImGuiCol_TitleBg]               = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive]         = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
    style.Colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
    style.Colors[ImGuiCol_MenuBarBg]             = ImVec4(0.09803922f, 0.105882354f, 0.12156863f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.15686275f, 0.16862746f, 0.19215687f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
    style.Colors[ImGuiCol_CheckMark]             = ImVec4(0.972549f, 1.0f, 0.49803922f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab]            = ImVec4(0.972549f, 1.0f, 0.49803922f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive]      = ImVec4(1.0f, 0.79607844f, 0.49803922f, 1.0f);
    style.Colors[ImGuiCol_Button]                = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered]         = ImVec4(0.18039216f, 0.1882353f, 0.19607843f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive]          = ImVec4(0.15294118f, 0.15294118f, 0.15294118f, 1.0f);
    style.Colors[ImGuiCol_Header]                = ImVec4(0.14117648f, 0.16470589f, 0.20784314f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered]         = ImVec4(0.105882354f, 0.105882354f, 0.105882354f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive]          = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
    style.Colors[ImGuiCol_Separator]             = ImVec4(0.12941177f, 0.14901961f, 0.19215687f, 1.0f);
    style.Colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.15686275f, 0.18431373f, 0.2509804f, 1.0f);
    style.Colors[ImGuiCol_SeparatorActive]       = ImVec4(0.15686275f, 0.18431373f, 0.2509804f, 1.0f);
    style.Colors[ImGuiCol_ResizeGrip]            = ImVec4(0.14509805f, 0.14509805f, 0.14509805f, 1.0f);
    style.Colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.972549f, 1.0f, 0.49803922f, 1.0f);
    style.Colors[ImGuiCol_ResizeGripActive]      = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_Tab]                   = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
    style.Colors[ImGuiCol_TabHovered]            = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
    style.Colors[ImGuiCol_TabActive]             = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocused]          = ImVec4(0.078431375f, 0.08627451f, 0.101960786f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.1254902f, 0.27450982f, 0.57254905f, 1.0f);
    style.Colors[ImGuiCol_PlotLines]             = ImVec4(0.52156866f, 0.6f, 0.7019608f, 1.0f);
    style.Colors[ImGuiCol_PlotLinesHovered]      = ImVec4(0.039215688f, 0.98039216f, 0.98039216f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram]         = ImVec4(0.88235295f, 0.79607844f, 0.56078434f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.95686275f, 0.95686275f, 0.95686275f, 1.0f);
    style.Colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
    style.Colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.047058824f, 0.05490196f, 0.07058824f, 1.0f);
    style.Colors[ImGuiCol_TableBorderLight]      = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_TableRowBg]            = ImVec4(0.11764706f, 0.13333334f, 0.14901961f, 1.0f);
    style.Colors[ImGuiCol_TableRowBgAlt]         = ImVec4(0.09803922f, 0.105882354f, 0.12156863f, 1.0f);
    style.Colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.9372549f, 0.9372549f, 0.9372549f, 1.0f);
    style.Colors[ImGuiCol_DragDropTarget]        = ImVec4(0.49803922f, 0.5137255f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_NavHighlight]          = ImVec4(0.26666668f, 0.2901961f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.49803922f, 0.5137255f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.19607843f, 0.1764706f, 0.54509807f, 0.5019608f);
    style.Colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.19607843f, 0.1764706f, 0.54509807f, 0.5019608f);

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
