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

static ImVec4 color_for_level(spdlog::level::level_enum level) {
    switch (level) {
    case spdlog::level::trace:
        return {0.40f, 0.40f, 0.40f, 1.00f};
    case spdlog::level::debug:
        return {0.60f, 0.80f, 1.00f, 1.00f};
    case spdlog::level::info:
        return {0.50f, 0.90f, 0.40f, 1.00f};
    case spdlog::level::warn:
        return {1.00f, 0.80f, 0.00f, 1.00f};
    case spdlog::level::err:
        return {1.00f, 0.30f, 0.30f, 1.00f};
    case spdlog::level::critical:
        return {1.00f, 0.20f, 0.80f, 1.00f};
    default:
        return {0.90f, 0.90f, 0.90f, 1.00f};
    }
}

imgui_console::imgui_console() : ring(std::make_shared<log_ring_buffer>()) {
    register_command("help", "List all registered commands", [this](std::vector<std::string>) {
        add_log("Available commands:", spdlog::level::info);
        for (const auto& cmd : commands) {
            std::string line = "  " + cmd.name;
            if (!cmd.description.empty()) {
                line += "  —  " + cmd.description;
            }
            add_log(line, spdlog::level::info);
        }
    });

    register_command("clear", "Clear the console", [this](std::vector<std::string>) {
        ring->clear();
        render_entries.clear();
        last_fetched_id = 0;
    });

    register_command("history", "Show command history", [this](std::vector<std::string>) {
        for (std::size_t i = 0; i < history.size(); ++i) {
            add_log(std::to_string(i) + ": " + history[i], spdlog::level::info);
        }
    });
}

void imgui_console::attach_logger(std::shared_ptr<spdlog::logger> logger) {
    auto sink = std::make_shared<imgui_console_sink_mt>(ring);
    logger->sinks().push_back(std::move(sink));
}

void imgui_console::register_command(
    const std::string&                            name,
    const std::string&                            description,
    std::function<void(std::vector<std::string>)> callback,
    arg_completer_fn                              arg_completer
) {
    for (auto& cmd : commands) {
        if (cmd.name == name) {
            cmd.description   = description;
            cmd.callback      = std::move(callback);
            cmd.arg_completer = std::move(arg_completer);
            return;
        }
    }
    commands.push_back({name, description, std::move(callback), std::move(arg_completer)});
}

void imgui_console::add_log(const std::string& text, spdlog::level::level_enum level) {
    ring->push({text, level, 0});
    scroll_to_bottom = true;
}

bool imgui_console::draw(const char* title, bool* p_open) {
    {
        std::vector<log_entry> fresh;
        uint64_t               new_id = ring->fetch_since(last_fetched_id, fresh);
        if (!fresh.empty()) {
            for (auto& e : fresh) {
                render_entries.push_back(std::move(e));
            }
            last_fetched_id  = new_id;
            scroll_to_bottom = true;
        }
    }

    ImGui::SetNextWindowSize(ImVec2(720, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, p_open)) {
        ImGui::End();
        return p_open ? *p_open : true;
    }

    filter.Draw("Filter", 200.0f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        ring->clear();
        render_entries.clear();
        last_fetched_id = 0;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Scroll to bottom")) {
        scroll_to_bottom = true;
    }
    ImGui::Separator();

    draw_log_region();
    ImGui::Separator();
    draw_input_bar();

    ImGui::End();
    return p_open ? *p_open : true;
}

void imgui_console::draw_log_region() {
    const float footer_h = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();

    ImGui::BeginChild("##log", ImVec2(0.0f, -footer_h), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));

    static const char* k_level_tags[] = {"[trace]", "[debug]", "[info]", "[warning]", "[error]", "[critical]", "[off]"};

    const ImVec4 col_default = {0.90f, 0.90f, 0.90f, 1.00f};

    for (const auto& entry : render_entries) {
        if (!filter.PassFilter(entry.text.c_str())) {
            continue;
        }

        const char* p = entry.text.c_str();

        while (*p == '[') {
            const char* close = strchr(p, ']');
            if (!close) {
                break;
            }

            bool is_level_tag = false;
            for (const char* tag : k_level_tags) {
                const std::size_t tag_len = strlen(tag);
                if (strncmp(p, tag, tag_len) == 0) {
                    is_level_tag = true;
                    break;
                }
            }

            const ImVec4& col = is_level_tag ? color_for_level(entry.level) : col_default;
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(p, close + 1);
            ImGui::PopStyleColor();

            p = close + 1;
            while (*p == ' ') {
                ++p;
            }
            ImGui::SameLine(0.0f, *p == '[' ? 4.0f : 4.0f);
        }

        if (*p != '\0') {
            ImGui::PushStyleColor(ImGuiCol_Text, col_default);
            ImGui::TextUnformatted(p);
            ImGui::PopStyleColor();
        }
    }

    if (scroll_to_bottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
        scroll_to_bottom = false;
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();
}

void imgui_console::draw_input_bar() {
    bool reclaim_focus = false;

    constexpr ImGuiInputTextFlags input_flags =
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll |
        ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;

    ImGui::SetNextItemWidth(-1.0f);

    if (ImGui::InputText("##input", input_buf, IM_ARRAYSIZE(input_buf), input_flags, &input_text_callback_stub, this)) {
        const std::string raw(input_buf);
        if (!raw.empty()) {
            add_log("> " + raw, spdlog::level::debug);
            execute_command(raw);

            if (history.empty() || history.back() != raw) {
                if (static_cast<int>(history.size()) >= k_history_max) {
                    history.erase(history.begin());
                }
                history.push_back(raw);
            }
            history_pos = -1;
        }
        input_buf[0]      = '\0';
        reclaim_focus     = true;
        autocomplete_open = false;
    }

    input_rect_min = ImGui::GetItemRectMin();

    ImGui::SetItemDefaultFocus();
    if (reclaim_focus) {
        ImGui::SetKeyboardFocusHere(-1);
    }

    if (autocomplete_open && !candidates.empty()) {
        draw_autocomplete_popup();
    }

    if (autocomplete_open && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        autocomplete_open = false;
    }
}

void imgui_console::draw_autocomplete_popup() {
    const int    visible_rows = std::min(static_cast<int>(candidates.size()), 8);
    const float  row_h        = ImGui::GetTextLineHeightWithSpacing();
    const float  popup_h      = visible_rows * row_h + ImGui::GetStyle().WindowPadding.y * 2.0f;
    const ImVec2 popup_pos    = {input_rect_min.x, input_rect_min.y - popup_h - 2.0f};

    ImGui::SetNextWindowPos(popup_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize({320.0f, popup_h}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);

    constexpr ImGuiWindowFlags popup_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    if (!ImGui::Begin("##autocomplete", nullptr, popup_flags)) {
        ImGui::End();
        return;
    }

    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        const bool selected = (i == candidate_sel);
        ImGui::PushID(i);
        if (ImGui::Selectable(candidates[i].c_str(), selected, ImGuiSelectableFlags_None, {0.0f, row_h})) {
            strncpy(
                input_buf + complete_token_start,
                candidates[i].c_str(),
                IM_ARRAYSIZE(input_buf) - complete_token_start - 1
            );
            input_buf[IM_ARRAYSIZE(input_buf) - 1] = '\0';
            autocomplete_open                      = false;
            ImGui::SetKeyboardFocusHere(-1);
        }
        if (selected)
            ImGui::SetScrollHereY();
        ImGui::PopID();
    }

    ImGui::End();
}

void imgui_console::execute_command(const std::string& raw) {
    auto tokens = tokenise(raw);
    if (tokens.empty()) {
        return;
    }

    for (const auto& cmd : commands) {
        if (cmd.name == tokens[0]) {
            std::vector<std::string> args(tokens.begin() + 1, tokens.end());
            cmd.callback(std::move(args));
            return;
        }
    }

    add_log("Unknown command: '" + tokens[0] + "'", spdlog::level::warn);
}

std::vector<std::string> imgui_console::tokenise(const std::string& line) {
    std::vector<std::string> tokens;
    std::string              token;
    bool                     in_quotes = false;

    for (char c : line) {
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ' ' && !in_quotes) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token += c;
        }
    }
    if (!token.empty()) {
        tokens.push_back(token);
    }

    return tokens;
}

void imgui_console::refresh_candidates() {
    candidates.clear();
    candidate_sel = 0;

    const std::string buf(input_buf);

    const std::size_t space_pos = buf.rfind(' ');
    const bool        has_space = (space_pos != std::string::npos);

    if (!has_space) {
        complete_token_start      = 0;
        const std::string& prefix = buf;
        if (prefix.empty()) {
            return;
        }

        for (const auto& cmd : commands)
            if (cmd.name.rfind(prefix, 0) == 0) {
                candidates.push_back(cmd.name);
            }
    } else {
        complete_token_start         = static_cast<int>(space_pos) + 1;
        const std::string cmd_name   = buf.substr(0, buf.find(' '));
        const std::string arg_prefix = buf.substr(space_pos + 1);

        const console_command* found_cmd = nullptr;
        for (const auto& cmd : commands) {
            if (cmd.name == cmd_name) {
                found_cmd = &cmd;
                break;
            }
        }

        if (!found_cmd || !found_cmd->arg_completer) {
            return;
        }

        candidates = found_cmd->arg_completer(arg_prefix);

        candidates.erase(
            std::remove_if(
                candidates.begin(),
                candidates.end(),
                [&arg_prefix](const std::string& s) {
                    return s.rfind(arg_prefix, 0) != 0;
                }
            ),
            candidates.end()
        );
    }

    std::sort(candidates.begin(), candidates.end());
}

void imgui_console::apply_candidate(int index, ImGuiInputTextCallbackData* data) {
    assert(index >= 0 && index < static_cast<int>(candidates_.size()));
    const std::string& chosen = candidates[index];

    data->DeleteChars(complete_token_start, data->BufTextLen - complete_token_start);
    data->InsertChars(complete_token_start, chosen.c_str());
}

int imgui_console::input_text_callback_stub(ImGuiInputTextCallbackData* data) {
    return static_cast<imgui_console*>(data->UserData)->input_text_callback(data);
}

int imgui_console::input_text_callback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        if (autocomplete_open && !candidates.empty()) {
            candidate_sel = (candidate_sel + 1) % static_cast<int>(candidates.size());
            apply_candidate(candidate_sel, data);
            return 0;
        }

        refresh_candidates();

        if (candidates.empty()) {
        } else if (candidates.size() == 1) {
            apply_candidate(0, data);
            autocomplete_open = false;
        } else {
            std::string common = candidates[0];
            for (std::size_t i = 1; i < candidates.size(); ++i) {
                std::size_t j = 0;
                while (j < common.size() && j < candidates[i].size() && common[j] == candidates[i][j])
                    ++j;
                common = common.substr(0, j);
            }

            const std::string current_token(data->Buf + complete_token_start, data->BufTextLen - complete_token_start);
            if (common.size() > current_token.size()) {
                data->DeleteChars(complete_token_start, data->BufTextLen - complete_token_start);
                data->InsertChars(complete_token_start, common.c_str());
            }

            autocomplete_open = true;
            candidate_sel     = 0;
        }

        return 0;
    }

    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        autocomplete_open = false;

        const int prev_pos = history_pos;

        if (data->EventKey == ImGuiKey_UpArrow) {
            if (history_pos == -1) {
                history_pos = static_cast<int>(history.size()) - 1;
            } else if (history_pos > 0) {
                --history_pos;
            }
        } else if (data->EventKey == ImGuiKey_DownArrow) {
            if (history_pos != -1 && ++history_pos >= static_cast<int>(history.size())) {
                history_pos = -1;
            }
        }

        if (prev_pos != history_pos) {
            const char* entry = (history_pos >= 0) ? history[history_pos].c_str() : "";
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, entry);
        }

        return 0;
    }

    return 0;
}
