#pragma once

#include "ember.hpp"
#include "resources.hpp"

VkDescriptorPool imgui_init(
    SDL_Window*      window,
    VkInstance       instance,
    VkPhysicalDevice physical_device,
    VkDevice         device,
    VkFormat         swapchain_format,
    uint32_t         graphics_family_index,
    VkQueue          graphics_queue,
    uint32_t         image_count
);

ImFont* generate_icon_font(float size);

// Register a image to be used with imgui draw image commands, this assumes that the image layout is
// SHADER_READ_ONLY_OPTIMAL
VkDescriptorSet imgui_image_handle(const Image& image, VkSampler sampler);

// Deallocate a handle previously acquired from imgui_image_handle()
void imgui_image_handle_free(VkDescriptorSet handle);

bool imgui_splitter(
    bool   split_vertically,
    float  thickness,
    float* size1,
    float* size2,
    float  min_size1,
    float  min_size2,
    float  splitter_long_axis_size = -1.0f
);

struct log_entry {
    std::string               text;
    spdlog::level::level_enum level = spdlog::level::info;
    uint64_t                  id    = 0;
};

class log_ring_buffer {
public:
    static constexpr std::size_t k_capacity = 2048;

    void push(log_entry entry) {
        std::lock_guard<std::mutex> lock(mutex_);
        entry.id = next_id_++;
        if (entries_.size() >= k_capacity) {
            entries_.pop_front();
        }
        entries_.push_back(std::move(entry));
    }

    uint64_t fetch_since(uint64_t after_id, std::vector<log_entry>& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t                    max_id = after_id;
        for (const auto& e : entries_) {
            if (e.id > after_id) {
                out.push_back(e);
                if (e.id > max_id)
                    max_id = e.id;
            }
        }
        return max_id;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }

private:
    mutable std::mutex    mutex_;
    std::deque<log_entry> entries_;
    uint64_t              next_id_ = 1;
};

template <typename Mutex> class imgui_console_sink : public spdlog::sinks::base_sink<Mutex> {
public:
    explicit imgui_console_sink(std::shared_ptr<log_ring_buffer> ring) : ring_(std::move(ring)) {
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t buf;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, buf);

        log_entry entry;
        entry.text = fmt::to_string(buf);

        if (!entry.text.empty() && entry.text.back() == '\n') {
            entry.text.pop_back();
        }
        entry.level = msg.level;

        ring_->push(std::move(entry));
    }

    void flush_() override {
    }

private:
    std::shared_ptr<log_ring_buffer> ring_;
};

using imgui_console_sink_mt = imgui_console_sink<std::mutex>;
using imgui_console_sink_st = imgui_console_sink<spdlog::details::null_mutex>;

using arg_completer_fn = std::function<std::vector<std::string>(const std::string& prefix)>;

struct console_command {
    std::string                                   name;
    std::string                                   description;
    std::function<void(std::vector<std::string>)> callback;
    arg_completer_fn                              arg_completer;
};

class imgui_console {
public:
    static constexpr int k_history_max = 64;

    imgui_console();
    ~imgui_console() = default;

    void attach_logger(std::shared_ptr<spdlog::logger> logger);

    void register_command(
        const std::string&                            name,
        const std::string&                            description,
        std::function<void(std::vector<std::string>)> callback,
        arg_completer_fn                              arg_completer = nullptr
    );

    bool draw(const char* title = "Console", bool allow_input_refocus = false, bool* p_open = nullptr);

    void add_log(const std::string& text, spdlog::level::level_enum level = spdlog::level::info);

private:
    void draw_log_region();
    void draw_input_bar(bool allow_input_refocus);
    void draw_autocomplete_popup();

    void                     execute_command(const std::string& raw);
    std::vector<std::string> tokenise(const std::string& line);

    void refresh_candidates();
    void apply_candidate(int index, ImGuiInputTextCallbackData* data);

    static int input_text_callback_stub(ImGuiInputTextCallbackData* data);
    int        input_text_callback(ImGuiInputTextCallbackData* data);

    std::shared_ptr<log_ring_buffer> ring;
    std::vector<log_entry>           render_entries;
    uint64_t                         last_fetched_id = 0;

    std::vector<console_command> commands;

    char                     input_buf[512]{};
    std::vector<std::string> history;
    int                      history_pos = -1;

    std::vector<std::string> candidates;
    int                      candidate_sel        = 0;
    bool                     autocomplete_open    = false;
    int                      complete_token_start = 0;
    ImVec2                   input_rect_min       = {};

    bool            scroll_to_bottom = true;
    ImGuiTextFilter filter;
};
