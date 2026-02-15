#pragma once

#include "ember.hpp"
#include "resources.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <unordered_map>

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

struct ImageBarrier {
    VkImage image;

    VkImageLayout old_layout;
    VkImageLayout new_layout;

    // What stage are we waiting for to finish
    VkPipelineStageFlagBits2 src_stage_mask;
    // What work for that stage are we waiting for to finish
    VkAccessFlagBits2 src_access_mask;

    // Which stage is waiting for this resource
    VkPipelineStageFlagBits2 dst_stage_mask;
    // What work will this stage do on the resource
    VkAccessFlagBits2 dst_access_mask;

    // What region of the resource does this barrier apply to
    VkImageSubresourceRange subresource_range;
};

struct BufferBarrier {
    VkBuffer buffer;

    // What stage are we waiting for to finish
    VkPipelineStageFlagBits2 src_stage_mask;
    // What work for that stage are we waiting for to finish
    VkAccessFlagBits2 src_access_mask;

    // Which stage is waiting for this resource
    VkPipelineStageFlagBits2 dst_stage_mask;
    // What work will this stage do on the resource
    VkAccessFlagBits2 dst_access_mask;

    // What region of the resource does this barrier apply to
    VkDeviceSize offset;

    // What region of the resource does this barrier apply to
    VkDeviceSize size;

    bool dynamic;
};

struct ImageResourceUse {
    VkImage image;

    VkPipelineStageFlagBits2 stage;
    VkAccessFlagBits2        access;

    VkImageLayout           layout;
    VkImageSubresourceRange subresource_range;
};

struct BufferResourceUse {
    VkBuffer buffer;

    VkPipelineStageFlagBits2 stage;
    VkAccessFlagBits2        access;

    VkDeviceSize offset;
    VkDeviceSize size;

    bool dynamic;
};

struct TrackedImage {
    VkImageLayout image_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    bool clear_on_first_write = true;
};

struct RenderPass {
    std::string name;

    std::function<void(VkCommandBuffer, uint32_t)> on_render = nullptr;

    uint32_t query_start_index = 0;
    uint32_t query_end_index   = 0;

    std::vector<ImageResourceUse> image_writes;
    std::vector<ImageResourceUse> image_reads;

    std::vector<BufferResourceUse> buffer_writes;
    std::vector<BufferResourceUse> buffer_reads;

    RenderPass& reads_image(
        const Image&             image,
        VkPipelineStageFlagBits2 stage,
        VkAccessFlagBits2        access,
        VkImageLayout            layout,
        VkImageSubresourceRange  subresource_range = {0}
    );
    RenderPass& samples_image(const Image& image, VkPipelineStageFlagBits2 stage);
    RenderPass& reads_storage_image(const Image& image, VkPipelineStageFlagBits2 stage);
    RenderPass& writes_image(
        const Image&             image,
        VkPipelineStageFlagBits2 stage,
        VkAccessFlagBits2        access,
        VkImageLayout            layout,
        VkImageSubresourceRange  subresource_range = {0}
    );
    RenderPass& writes_color_attachment(const Image& image);
    RenderPass& writes_depth_attachment(const Image& image);
    RenderPass& writes_storage_image(const Image& image, VkPipelineStageFlagBits2 stage);
    RenderPass& reads_buffer(
        const Buffer&            buffer,
        VkPipelineStageFlagBits2 stage,
        VkAccessFlagBits2        access,
        VkDeviceSize             offset = 0,
        VkDeviceSize             size   = 0
    );
    RenderPass& writes_buffer(
        const Buffer&            buffer,
        VkPipelineStageFlagBits2 stage,
        VkAccessFlagBits2        access,
        VkDeviceSize             offset = 0,
        VkDeviceSize             size   = 0
    );

    // Assumes that the buffer is split into equal `size` chunks
    RenderPass& reads_buffer_dynamic(
        const Buffer& buffer, VkPipelineStageFlagBits2 stage, VkAccessFlagBits2 access, VkDeviceSize size
    );

    // Assumes that the buffer is split into equal `size` chunks
    RenderPass& writes_buffer_dynamic(
        const Buffer& buffer, VkPipelineStageFlagBits2 stage, VkAccessFlagBits2 access, VkDeviceSize size
    );
    RenderPass& render_func(std::function<void(VkCommandBuffer command_buffer, uint32_t frame_index)> func);
};

constexpr uint32_t PASS_TIMING_COUNT = 500;
struct PassTiming {
    std::array<float, PASS_TIMING_COUNT> timings;
    uint32_t                             current_timing_index = 0;

    void  add_sample(double sample_ms);
    float get_avg_timing_ms() const;
};

constexpr uint32_t QUERY_COUNT = 128;
struct Framegraph {
    std::vector<RenderPass> passes;

    std::unordered_map<VkImage, TrackedImage> tracked_layouts;

    // Image barriers to submit before rendering the respective pass:
    // index 0 - image barriers before render pass 0
    // index 1 - image barriers before render pass 1
    // ...
    std::vector<std::vector<ImageBarrier>> image_barriers;

    // Buffer barriers to submit before rendering the respective pass:
    // index 0 - buffer barriers before render pass 0
    // index 1 - buffer barriers before render pass 1
    // ...
    std::vector<std::vector<BufferBarrier>> buffer_barriers;

    // Image barriers at the end of the frame, meant to transition
    // images that don't need to transition from UNDEFINED layout
    // and instead need to match their imported state
    std::vector<ImageBarrier> post_frame_image_barriers;

    bool                     enable_timings   = false;
    uint32_t                 next_query_index = 0;
    std::vector<VkQueryPool> timestamp_query_pools;

    bool     start_reading    = false;
    uint32_t query_pool_index = 0;

    std::unordered_map<std::string, PassTiming> pass_timings;

    tracy::VkCtx* tracy_vk_context;

    Framegraph(
        VkDevice        device,
        VkQueue         queue,
        VkCommandBuffer command_buffer,
        uint32_t        frames_in_flight,
        bool            enable_timings,
        tracy::VkCtx*   tracy_vk_context
    );

    void destroy(VkDevice device);

    RenderPass& add_pass(const std::string& name);

    // Import external image into the framegraph, the `expected_layout`
    // is expected image layout on the first frame of the framegraph
    // execution, used to simplify transition tracking
    void import_image(const Image& image, VkImageLayout expected_layout, bool clear_on_first_write = true);

    const PassTiming& get_pass_timing(const std::string& pass_name);

    void build();
    void execute(VkCommandBuffer command_buffer, uint32_t frame_index);
    void gather_timestamp_queries(VkDevice device, float timestamp_period);
};
