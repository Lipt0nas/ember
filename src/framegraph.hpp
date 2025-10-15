#pragma once

#include "ember.hpp"
#include "resources.hpp"

#include <functional>
#include <unordered_map>

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

struct RenderPass {
    std::string name;

    std::function<void(VkCommandBuffer, uint32_t)> on_render = nullptr;

    uint32_t query_start_index;
    uint32_t query_end_index;

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
    ) {
        if (subresource_range.aspectMask == 0) {
            subresource_range = {
                .aspectMask     = image.aspect,
                .baseMipLevel   = 0,
                .levelCount     = image.levels,
                .baseArrayLayer = 0,
                .layerCount     = 1
            };
        }

        image_reads.emplace_back(image.handle, stage, access, layout, subresource_range);

        return *this;
    }

    RenderPass& samples_image(const Image& image, VkPipelineStageFlagBits2 stage) {
        return reads_image(image, stage, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    RenderPass& reads_storage_image(const Image& image, VkPipelineStageFlagBits2 stage) {
        return reads_image(image, stage, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);
    }

    RenderPass& writes_image(
        const Image&             image,
        VkPipelineStageFlagBits2 stage,
        VkAccessFlagBits2        access,
        VkImageLayout            layout,
        VkImageSubresourceRange  subresource_range = {0}
    ) {
        if (subresource_range.aspectMask == 0) {
            subresource_range = {
                .aspectMask     = image.aspect,
                .baseMipLevel   = 0,
                .levelCount     = image.levels,
                .baseArrayLayer = 0,
                .layerCount     = 1
            };
        }

        image_writes.emplace_back(image.handle, stage, access, layout, subresource_range);

        return *this;
    }

    RenderPass& writes_color_attachment(const Image& image) {
        return writes_image(
            image,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        );
    }

    RenderPass& writes_depth_attachment(const Image& image) {
        return writes_image(
            image,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
        );
    }

    RenderPass& writes_storage_image(const Image& image, VkPipelineStageFlagBits2 stage) {
        return writes_image(image, stage, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
    }

    RenderPass& reads_buffer(
        const Buffer&            buffer,
        VkPipelineStageFlagBits2 stage,
        VkAccessFlagBits2        access,
        VkDeviceSize             offset = 0,
        VkDeviceSize             size   = 0
    ) {
        if (size == 0) {
            size = buffer.size;
        }

        buffer_reads.emplace_back(buffer.handle, stage, access, offset, size);

        return *this;
    }

    RenderPass& writes_buffer(
        const Buffer&            buffer,
        VkPipelineStageFlagBits2 stage,
        VkAccessFlagBits2        access,
        VkDeviceSize             offset = 0,
        VkDeviceSize             size   = 0
    ) {
        if (size == 0) {
            size = buffer.size;
        }

        buffer_writes.emplace_back(buffer.handle, stage, access, offset, size);

        return *this;
    }

    // Assumes that the buffer is split into equal `size` chunks
    RenderPass& reads_buffer_dynamic(
        const Buffer& buffer, VkPipelineStageFlagBits2 stage, VkAccessFlagBits2 access, VkDeviceSize size
    ) {
        buffer_reads.emplace_back(buffer.handle, stage, access, 0, size, true);

        return *this;
    }

    // Assumes that the buffer is split into equal `size` chunks
    RenderPass& writes_buffer_dynamic(
        const Buffer& buffer, VkPipelineStageFlagBits2 stage, VkAccessFlagBits2 access, VkDeviceSize size
    ) {
        buffer_writes.emplace_back(buffer.handle, stage, access, 0, size, true);

        return *this;
    }

    RenderPass& render_func(std::function<void(VkCommandBuffer command_buffer, uint32_t frame_index)> func) {
        on_render = func;

        return *this;
    }
};

struct Framegraph {
    std::vector<RenderPass> passes;

    std::unordered_map<VkImage, VkImageLayout> tracked_layouts;

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

    uint32_t                 next_query_index = 0;
    std::vector<VkQueryPool> timestamp_query_pools;

    std::unordered_map<std::string, double> pass_timings_ms;

    // TODO: Probably not a constructor
    Framegraph(VkDevice device, uint32_t frames_in_flight) {
        VkQueryPoolCreateInfo pool_info = {
            .sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext      = nullptr,
            .flags      = 0,
            .queryType  = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = 256,
        };

        timestamp_query_pools.resize(frames_in_flight);
        for (int i = 0; i < frames_in_flight; i++) {
            vkCreateQueryPool(device, &pool_info, nullptr, &timestamp_query_pools[i]);
        }
    }

    void destroy(VkDevice device) {
        for (auto pool : timestamp_query_pools) {
            vkDestroyQueryPool(device, pool, nullptr);
        }
    }

    RenderPass& add_pass(const std::string& name) {
        return passes.emplace_back(RenderPass{.name = name});
    }

    // Import external image into the framegraph, the `expected_layout`
    // is expected image layout on the first frame of the framegraph
    // execution, used to simplify transition tracking
    void import_image(const Image& image, VkImageLayout expected_layout) {
        tracked_layouts.insert({image.handle, expected_layout});
    }

    double get_pass_time_ms(const std::string& pass_name) {
        return pass_timings_ms.count(pass_name) ? pass_timings_ms[pass_name] : 0.0;
    }

    void build() {
        spdlog::info("Building framegraph");

        // TODO: Technically it should be able to contain a single pass, but it makes the logic
        // a little easier to implement for now
        if (passes.size() <= 1) {
            spdlog::error("Framegraph should contain 2 or more passes");
            return;
        }

        next_query_index = 0;

        image_barriers.resize(passes.size());
        buffer_barriers.resize(passes.size());

        struct LastAccess {
            int                      pass_idx     = -1;
            VkPipelineStageFlagBits2 stage        = VK_PIPELINE_STAGE_2_NONE;
            VkAccessFlagBits2        access       = VK_ACCESS_2_NONE;
            bool                     write_access = false;

            VkDeviceSize offset = 0;
            VkDeviceSize size   = 0;

            bool dynamic = false;
        };

        std::unordered_map<VkImage, LastAccess>  last_image_access;
        std::unordered_map<VkBuffer, LastAccess> last_buffer_access;

        for (int pass_idx = 0; pass_idx < passes.size(); pass_idx++) {
            auto& current_pass = passes[pass_idx];

            current_pass.query_start_index = next_query_index++;
            current_pass.query_end_index   = next_query_index++;

            for (const auto& image_write : current_pass.image_writes) {
                VkImageLayout& old_layout  = tracked_layouts[image_write.image];
                LastAccess&    last_access = last_image_access[image_write.image];

                // If the pass writes to an image as an attachment, we assume that it wants it cleared
                // but only if it's the first writer
                bool should_clear = ((image_write.access & (VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT)) != 0) &&
                                    last_access.pass_idx == -1;

                bool needs_barrier = should_clear;
                if (old_layout != image_write.layout) {
                    needs_barrier = true;
                }

                if (last_access.pass_idx != -1) {
                    needs_barrier = true;
                }

                VkPipelineStageFlagBits2 src_stage_mask  = last_access.stage;
                VkAccessFlagBits2        src_access_mask = last_access.access;

                if (needs_barrier) {
                    ImageBarrier barrier = {
                        .image             = image_write.image,
                        .old_layout        = should_clear ? VK_IMAGE_LAYOUT_UNDEFINED : old_layout,
                        .new_layout        = image_write.layout,
                        .src_stage_mask    = src_stage_mask,
                        .src_access_mask   = src_access_mask,
                        .dst_stage_mask    = image_write.stage,
                        .dst_access_mask   = image_write.access,
                        .subresource_range = image_write.subresource_range
                    };

                    image_barriers[pass_idx].push_back(barrier);
                }

                old_layout  = image_write.layout;
                last_access = {
                    .pass_idx = pass_idx, .stage = image_write.stage, .access = image_write.access, .write_access = true
                };
            }

            for (const auto& image_read : current_pass.image_reads) {
                VkImageLayout& old_layout  = tracked_layouts[image_read.image];
                LastAccess&    last_access = last_image_access[image_read.image];

                bool needs_barrier = false;

                if (old_layout != image_read.layout) {
                    needs_barrier = true;
                }

                if (last_access.pass_idx != -1 && last_access.write_access) {
                    needs_barrier = true;
                }

                VkPipelineStageFlagBits2 src_stage_mask  = last_access.stage;
                VkAccessFlagBits2        src_access_mask = last_access.access;

                if (needs_barrier) {
                    ImageBarrier barrier = {
                        .image             = image_read.image,
                        .old_layout        = old_layout,
                        .new_layout        = image_read.layout,
                        .src_stage_mask    = src_stage_mask,
                        .src_access_mask   = src_access_mask,
                        .dst_stage_mask    = image_read.stage,
                        .dst_access_mask   = image_read.access,
                        .subresource_range = image_read.subresource_range
                    };

                    image_barriers[pass_idx].push_back(barrier);
                }

                old_layout  = image_read.layout;
                last_access = {
                    .pass_idx = pass_idx, .stage = image_read.stage, .access = image_read.access, .write_access = false
                };
            }

            for (const auto& buffer_write : current_pass.buffer_writes) {
                LastAccess& last_access = last_buffer_access[buffer_write.buffer];

                bool needs_barrier = false;

                if (last_access.pass_idx != -1) {
                    needs_barrier = true;
                }

                VkPipelineStageFlagBits2 src_stage_mask  = last_access.stage;
                VkAccessFlagBits2        src_access_mask = last_access.access;

                if (needs_barrier) {
                    BufferBarrier barrier = {
                        .buffer          = buffer_write.buffer,
                        .src_stage_mask  = src_stage_mask,
                        .src_access_mask = src_access_mask,
                        .dst_stage_mask  = buffer_write.stage,
                        .dst_access_mask = buffer_write.access,
                        .offset          = buffer_write.offset,
                        .size            = buffer_write.size,
                        .dynamic         = buffer_write.dynamic
                    };

                    buffer_barriers[pass_idx].push_back(barrier);
                }

                last_access = {
                    .pass_idx     = pass_idx,
                    .stage        = buffer_write.stage,
                    .access       = buffer_write.access,
                    .write_access = true,
                    .offset       = buffer_write.offset,
                    .size         = buffer_write.size,
                    .dynamic      = buffer_write.dynamic,
                };
            }

            for (const auto& buffer_read : current_pass.buffer_reads) {
                LastAccess& last_access = last_buffer_access[buffer_read.buffer];

                bool needs_barrier = false;

                if (last_access.pass_idx != -1 && last_access.write_access) {
                    // For dynamic buffer (per frame region) reads, we'll assume that we always need to sync
                    if (buffer_read.dynamic || last_access.dynamic) {
                        needs_barrier = true;
                    } else {
                        // Otherwise, we check if the range overlaps
                        auto write_start = last_access.offset;
                        auto write_end   = last_access.offset + last_access.size;
                        auto read_start  = buffer_read.offset;
                        auto read_end    = buffer_read.offset + buffer_read.size;

                        // Check if ranges overlap
                        if (read_start < write_end && write_start < read_end) {
                            needs_barrier = true;
                        }
                    }
                }

                VkPipelineStageFlagBits2 src_stage_mask  = last_access.stage;
                VkAccessFlagBits2        src_access_mask = last_access.access;

                if (needs_barrier) {
                    BufferBarrier barrier = {
                        .buffer          = buffer_read.buffer,
                        .src_stage_mask  = src_stage_mask,
                        .src_access_mask = src_access_mask,
                        .dst_stage_mask  = buffer_read.stage,
                        .dst_access_mask = buffer_read.access,
                        .offset          = buffer_read.offset,
                        .size            = buffer_read.size,
                        .dynamic         = buffer_read.dynamic
                    };

                    buffer_barriers[pass_idx].push_back(barrier);
                }

                last_access = {
                    .pass_idx     = pass_idx,
                    .stage        = buffer_read.stage,
                    .access       = buffer_read.access,
                    .write_access = false,
                    .offset       = buffer_read.offset,
                    .size         = buffer_read.size,
                    .dynamic      = buffer_read.dynamic
                };
            }
        }
    }

    void execute(VkDevice device, VkCommandBuffer command_buffer, uint32_t frame_index, float timestamp_period) {
        // VkMemoryBarrier2 full_barrier = {
        //     .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        //     .srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        //     .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        //     .dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        //     .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT
        // };
        //
        // VkDependencyInfo dep = {
        //     .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &full_barrier
        // };
        //
        // vkCmdPipelineBarrier2(command_buffer, &dep);

        VkQueryPool query_pool = timestamp_query_pools[frame_index];
        vkCmdResetQueryPool(command_buffer, query_pool, 0, next_query_index);

        for (int i = 0; i < passes.size(); i++) {
            const auto& current_pass = passes[i];

            vkCmdWriteTimestamp2(
                command_buffer, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, query_pool, current_pass.query_start_index
            );

            std::vector<VkImageMemoryBarrier2>  img_barriers;
            std::vector<VkBufferMemoryBarrier2> buf_barriers;

            for (const auto& image : image_barriers[i]) {
                img_barriers.push_back(
                    VkImageMemoryBarrier2{
                        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .pNext               = nullptr,
                        .srcStageMask        = image.src_stage_mask,
                        .srcAccessMask       = image.src_access_mask,
                        .dstStageMask        = image.dst_stage_mask,
                        .dstAccessMask       = image.dst_access_mask,
                        .oldLayout           = image.old_layout,
                        .newLayout           = image.new_layout,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image               = image.image,
                        .subresourceRange    = image.subresource_range
                    }
                );
            }

            for (const auto& buffer : buffer_barriers[i]) {
                VkDeviceSize size   = buffer.size;
                VkDeviceSize offset = buffer.offset;

                if (buffer.dynamic) {
                    offset = size * frame_index;
                }

                buf_barriers.push_back(
                    VkBufferMemoryBarrier2{
                        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                        .pNext               = nullptr,
                        .srcStageMask        = buffer.src_stage_mask,
                        .srcAccessMask       = buffer.src_access_mask,
                        .dstStageMask        = buffer.dst_stage_mask,
                        .dstAccessMask       = buffer.dst_access_mask,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .buffer              = buffer.buffer,
                        .offset              = offset,
                        .size                = size
                    }
                );
            }

            VkDependencyInfo dependency = {
                .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext                    = nullptr,
                .dependencyFlags          = 0,
                .memoryBarrierCount       = 0,
                .pMemoryBarriers          = nullptr,
                .bufferMemoryBarrierCount = static_cast<uint32_t>(buf_barriers.size()),
                .pBufferMemoryBarriers    = buf_barriers.data(),
                .imageMemoryBarrierCount  = static_cast<uint32_t>(img_barriers.size()),
                .pImageMemoryBarriers     = img_barriers.data(),
            };

            vkCmdPipelineBarrier2(command_buffer, &dependency);

            if (current_pass.on_render) {
                current_pass.on_render(command_buffer, frame_index);
            }

            vkCmdWriteTimestamp2(
                command_buffer, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, query_pool, current_pass.query_end_index
            );
        }

        VkQueryPool           prev_query_pool = timestamp_query_pools[(frame_index - 1) % timestamp_query_pools.size()];
        std::vector<uint64_t> timestamps(next_query_index);
        vkGetQueryPoolResults(
            device,
            prev_query_pool,
            0,
            next_query_index,
            timestamps.size() * sizeof(uint64_t),
            timestamps.data(),
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
        );

        for (auto& pass : passes) {
            uint64_t start             = timestamps[pass.query_start_index];
            uint64_t end               = timestamps[pass.query_end_index];
            double   duration_ns       = (end - start) * timestamp_period;
            pass_timings_ms[pass.name] = duration_ns / 1'000'000.0;
        }
    }
};
