#include "resources.hpp"

#include <stb_image.h>

Buffer create_buffer(
    VkDeviceSize             size,
    VkBufferUsageFlags       usage_flags,
    VmaAllocator             allocator,
    VmaAllocationCreateFlags allocation_flags,
    size_t                   alignment
) {
    VkBufferCreateInfo buffer_info = {
        .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .size                  = size,
        .usage                 = usage_flags,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr
    };

    VmaAllocationCreateInfo allocation_info = {};
    allocation_info.flags                   = allocation_flags;
    allocation_info.usage                   = VMA_MEMORY_USAGE_AUTO;

    VkBuffer      handle = VK_NULL_HANDLE;
    VmaAllocation allocation;
    if (alignment == 0) {
        VK_CHECK(vmaCreateBuffer(allocator, &buffer_info, &allocation_info, &handle, &allocation, nullptr));
    } else {
        VK_CHECK(vmaCreateBufferWithAlignment(
            allocator, &buffer_info, &allocation_info, alignment, &handle, &allocation, nullptr
        ));
    }

    return {
        .handle     = handle,
        .size       = size,
        .allocation = allocation,
    };
}

void destroy_buffer(const Buffer& buffer, VkDevice device, VmaAllocator allocator) {
    if (buffer.handle != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
    }
}

void copy_buffer(
    const Buffer&   src_buffer,
    const Buffer&   dst_buffer,
    VkCommandBuffer command_buffer,
    VkQueue         queue,
    VkDevice        device,
    void*           data,
    size_t          data_size,
    VkDeviceSize    dst_buffer_offset
) {
    if (data_size + dst_buffer_offset >= dst_buffer.size) {
        spdlog::error(
            "Attempted out of bounds buffer write, size={}, remaining space={}",
            data_size,
            dst_buffer.size - dst_buffer_offset
        );
        exit(1);
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .pNext = nullptr, .flags = 0, .pInheritanceInfo = nullptr
    };
    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

    VkBufferCopy copy_region = {
        .srcOffset = 0,
        .dstOffset = dst_buffer_offset,
        .size      = data_size,
    };
    vkCmdCopyBuffer(command_buffer, src_buffer.handle, dst_buffer.handle, 1, &copy_region);

    VK_CHECK(vkEndCommandBuffer(command_buffer));

    VkSubmitInfo submit_info = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext                = nullptr,
        .waitSemaphoreCount   = 0,
        .pWaitSemaphores      = nullptr,
        .pWaitDstStageMask    = nullptr,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores    = nullptr
    };
    VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
    VK_CHECK(vkDeviceWaitIdle(device));
}

Image load_image(
    const std::filesystem::path& path,
    VkFormat                     format,
    bool                         generate_mipmaps,
    const Buffer&                staging_buffer,
    VkCommandBuffer              command_buffer,
    VkQueue                      queue,
    VmaAllocator                 allocator,
    VkDevice                     device
) {
    void* staging_buffer_ptr = nullptr;
    VK_CHECK(vmaMapMemory(allocator, staging_buffer.allocation, &staging_buffer_ptr));

    int32_t width;
    int32_t height;
    int32_t channels;

    if (path.extension() != ".hdr") {
        unsigned char* texture_data = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);

        memcpy(staging_buffer_ptr, texture_data, sizeof(unsigned char) * width * height * 4);
        stbi_image_free(texture_data);
    } else {
        spdlog::warn("FLOATING POINT TEXTURE {}", path.string());
        float* texture_data = stbi_loadf(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);

        memcpy(staging_buffer_ptr, texture_data, sizeof(float) * width * height * 4);
        stbi_image_free(texture_data);
    }

    Image image = create_image(
        format,
        width,
        height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        generate_mipmaps,
        allocator,
        device
    );

    copy_image(staging_buffer, image, generate_mipmaps, command_buffer, queue, device);

    vmaUnmapMemory(allocator, staging_buffer.allocation);

    return image;
}

Image create_image(
    VkFormat           format,
    uint32_t           width,
    uint32_t           height,
    VkImageUsageFlags  usage,
    VkImageAspectFlags aspect,
    bool               generate_mipmaps,
    VmaAllocator       allocator,
    VkDevice           device
) {
    uint32_t mip_levels =
        generate_mipmaps ? static_cast<uint32_t>(glm::floor(glm::log2((float)glm::max(width, height))) + 1) : 1;

    VkImageCreateInfo image_info = {
        .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .imageType             = VK_IMAGE_TYPE_2D,
        .format                = format,
        .extent                = {.width = width, .height = height, .depth = 1},
        .mipLevels             = mip_levels,
        .arrayLayers           = 1,
        .samples               = VK_SAMPLE_COUNT_1_BIT,
        .tiling                = VK_IMAGE_TILING_OPTIMAL,
        .usage                 = usage | (generate_mipmaps ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0),
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
        .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo allocation_info = {};
    allocation_info.usage                   = VMA_MEMORY_USAGE_AUTO;

    VkImage       handle = VK_NULL_HANDLE;
    VmaAllocation allocation;
    VK_CHECK(vmaCreateImage(allocator, &image_info, &allocation_info, &handle, &allocation, nullptr));

    VkImageViewCreateInfo view_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext    = nullptr,
        .flags    = 0,
        .image    = handle,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = format,
        .components =
            {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
             .g = VK_COMPONENT_SWIZZLE_IDENTITY,
             .b = VK_COMPONENT_SWIZZLE_IDENTITY,
             .a = VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {
            .aspectMask     = aspect,
            .baseMipLevel   = 0,
            .levelCount     = mip_levels,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        }
    };

    VkImageView view = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &view));

    return {
        .width      = width,
        .height     = height,
        .levels     = mip_levels,
        .layers     = 1,
        .format     = format,
        .aspect     = aspect,
        .handle     = handle,
        .view       = view,
        .allocation = allocation,
    };
}

Image create_cubemap_image(
    VkFormat           format,
    uint32_t           width,
    uint32_t           height,
    VkImageUsageFlags  usage,
    VkImageAspectFlags aspect,
    bool               generate_mipmaps,
    VmaAllocator       allocator,
    VkDevice           device
) {
    uint32_t mip_levels =
        generate_mipmaps ? static_cast<uint32_t>(glm::floor(glm::log2((float)glm::max(width, height))) + 1) : 1;

    VkImageCreateInfo image_info = {
        .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType             = VK_IMAGE_TYPE_2D,
        .format                = format,
        .extent                = {.width = width, .height = height, .depth = 1},
        .mipLevels             = mip_levels,
        .arrayLayers           = 6,
        .samples               = VK_SAMPLE_COUNT_1_BIT,
        .tiling                = VK_IMAGE_TILING_OPTIMAL,
        .usage                 = usage,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
        .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo allocation_info = {};
    allocation_info.usage                   = VMA_MEMORY_USAGE_AUTO;

    VkImage       handle = VK_NULL_HANDLE;
    VmaAllocation allocation;
    VK_CHECK(vmaCreateImage(allocator, &image_info, &allocation_info, &handle, &allocation, nullptr));

    VkImageViewCreateInfo view_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext    = nullptr,
        .flags    = 0,
        .image    = handle,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
        .format   = format,
        .components =
            {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
             .g = VK_COMPONENT_SWIZZLE_IDENTITY,
             .b = VK_COMPONENT_SWIZZLE_IDENTITY,
             .a = VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {
            .aspectMask     = aspect,
            .baseMipLevel   = 0,
            .levelCount     = mip_levels,
            .baseArrayLayer = 0,
            .layerCount     = 6,
        }
    };

    VkImageView view = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &view));

    return {
        .width      = width,
        .height     = height,
        .levels     = mip_levels,
        .layers     = 6,
        .format     = format,
        .aspect     = aspect,
        .handle     = handle,
        .view       = view,
        .allocation = allocation,
    };
}

void destroy_image(const Image& image, VkDevice device, VmaAllocator allocator) {
    if (image.handle != VK_NULL_HANDLE) {
        vkDestroyImageView(device, image.view, nullptr);
        vmaDestroyImage(allocator, image.handle, image.allocation);
    }
}

void image_pipeline_barrier(
    VkImage                 handle,
    VkCommandBuffer         command_buffer,
    VkImageLayout           old_layout,
    VkImageLayout           new_layout,
    VkPipelineStageFlags2   src_stage_mask,
    VkAccessFlags2          src_access_mask,
    VkPipelineStageFlags2   dst_stage_mask,
    VkAccessFlags2          dst_access_mask,
    VkImageSubresourceRange subresource
) {
    VkImageMemoryBarrier2 barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = src_stage_mask,
        .srcAccessMask       = src_access_mask,
        .dstStageMask        = dst_stage_mask,
        .dstAccessMask       = dst_access_mask,
        .oldLayout           = old_layout,
        .newLayout           = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = handle,
        .subresourceRange    = subresource
    };

    VkDependencyInfo dependency = {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 0,
        .pMemoryBarriers          = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers    = nullptr,
        .imageMemoryBarrierCount  = 1,
        .pImageMemoryBarriers     = &barrier
    };
    vkCmdPipelineBarrier2(command_buffer, &dependency);
}

void image_pipeline_barrier(
    const Image&          image,
    VkCommandBuffer       command_buffer,
    VkImageLayout         old_layout,
    VkImageLayout         new_layout,
    VkPipelineStageFlags2 src_stage_mask,
    VkAccessFlags2        src_access_mask,
    VkPipelineStageFlags2 dst_stage_mask,
    VkAccessFlags2        dst_access_mask
) {
    image_pipeline_barrier(
        image.handle,
        command_buffer,
        old_layout,
        new_layout,
        src_stage_mask,
        src_access_mask,
        dst_stage_mask,
        dst_access_mask,
        {
            .aspectMask     = image.aspect,
            .baseMipLevel   = 0,
            .levelCount     = image.levels,
            .baseArrayLayer = 0,
            .layerCount     = image.layers,
        }
    );
}

void copy_image(
    const Buffer&   src_buffer,
    const Image&    dst_image,
    bool            generate_mipmaps,
    VkCommandBuffer command_buffer,
    VkQueue         queue,
    VkDevice        device
) {
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .pNext = nullptr, .flags = 0, .pInheritanceInfo = nullptr
    };
    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

    VkBufferImageCopy copy_region = {};
    copy_region.imageExtent       = {.width = dst_image.width, .height = dst_image.height, .depth = 1};
    copy_region.imageOffset       = {.x = 0, .y = 0, .z = 0};
    copy_region.imageSubresource  = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1
    };

    image_pipeline_barrier(
        dst_image,
        command_buffer,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT
    );

    vkCmdCopyBufferToImage(
        command_buffer, src_buffer.handle, dst_image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region
    );

    if (dst_image.levels > 1) {
        VkImageSubresourceRange mip_range = {};
        mip_range.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
        mip_range.baseMipLevel            = 0;
        mip_range.baseArrayLayer          = 0;
        mip_range.levelCount              = 1;
        mip_range.layerCount              = 1;

        image_pipeline_barrier(
            dst_image.handle,
            command_buffer,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_BLIT_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT,
            {
                .aspectMask     = dst_image.aspect,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            }
        );

        for (int i = 1; i < dst_image.levels; i++) {
            VkImageBlit blit_info = {
                .srcSubresource =
                    {
                        .aspectMask     = dst_image.aspect,
                        .mipLevel       = static_cast<uint32_t>(i - 1),
                        .baseArrayLayer = 0,
                        .layerCount     = 1,
                    },
                .srcOffsets =
                    {
                        {.x = 0, .y = 0, .z = 0},
                        {
                            .x = glm::max(int32_t(dst_image.width >> (i - 1)), 1),
                            .y = glm::max(int32_t(dst_image.height >> (i - 1)), 1),
                            .z = 1,
                        },
                    },
                .dstSubresource =
                    {
                        .aspectMask     = dst_image.aspect,
                        .mipLevel       = static_cast<uint32_t>(i),
                        .baseArrayLayer = 0,
                        .layerCount     = 1,
                    },
                .dstOffsets = {
                    {.x = 0, .y = 0, .z = 0},
                    {
                        .x = glm::max(int32_t(dst_image.width >> i), 1),
                        .y = glm::max(int32_t(dst_image.height >> i), 1),
                        .z = 1,
                    },
                },
            };

            vkCmdBlitImage(
                command_buffer,
                dst_image.handle,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                dst_image.handle,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &blit_info,
                VK_FILTER_LINEAR
            );

            image_pipeline_barrier(
                dst_image.handle,
                command_buffer,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_BLIT_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT,
                {
                    .aspectMask     = dst_image.aspect,
                    .baseMipLevel   = static_cast<uint32_t>(i),
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                }
            );
        }
    }

    image_pipeline_barrier(
        dst_image,
        command_buffer,
        generate_mipmaps ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        generate_mipmaps ? VK_ACCESS_2_TRANSFER_READ_BIT : VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        0
    );

    VK_CHECK(vkEndCommandBuffer(command_buffer));

    VkSubmitInfo submit_info = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext                = nullptr,
        .waitSemaphoreCount   = 0,
        .pWaitSemaphores      = nullptr,
        .pWaitDstStageMask    = nullptr,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores    = nullptr
    };
    VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
    VK_CHECK(vkDeviceWaitIdle(device));
}
void buffer_pipeline_barrier(
    const Buffer&         buffer,
    VkCommandBuffer       command_buffer,
    VkPipelineStageFlags2 src_stage_mask,
    VkAccessFlags2        src_access_mask,
    VkPipelineStageFlags2 dst_stage_mask,
    VkAccessFlags2        dst_access_mask,
    VkDeviceSize          offset,
    VkDeviceSize          size
) {
    VkBufferMemoryBarrier2 barrier = {
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = src_stage_mask,
        .srcAccessMask       = src_access_mask,
        .dstStageMask        = dst_stage_mask,
        .dstAccessMask       = dst_access_mask,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer              = buffer.handle,
        .offset              = offset,
        .size                = size
    };

    VkDependencyInfo dependency = {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 0,
        .pMemoryBarriers          = nullptr,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers    = &barrier,
        .imageMemoryBarrierCount  = 0,
        .pImageMemoryBarriers     = nullptr,
    };
    vkCmdPipelineBarrier2(command_buffer, &dependency);
}

void memory_pipeline_barrier(
    VkCommandBuffer       command_buffer,
    VkPipelineStageFlags2 src_stage_mask,
    VkAccessFlags2        src_access_mask,
    VkPipelineStageFlags2 dst_stage_mask,
    VkAccessFlags2        dst_access_mask
) {
    VkMemoryBarrier2 barrier = {
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .pNext         = nullptr,
        .srcStageMask  = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask  = dst_stage_mask,
        .dstAccessMask = dst_access_mask,
    };

    VkDependencyInfo dependency = {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 1,
        .pMemoryBarriers          = &barrier,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers    = nullptr,
        .imageMemoryBarrierCount  = 0,
        .pImageMemoryBarriers     = nullptr,
    };
    vkCmdPipelineBarrier2(command_buffer, &dependency);
}

uint64_t get_buffer_device_address(const Buffer& buffer, VkDevice device) {
    VkBufferDeviceAddressInfo address_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .pNext = nullptr, .buffer = buffer.handle
    };

    return vkGetBufferDeviceAddress(device, &address_info);
}

void copy_to_buffer(const Buffer& dst_buffer, VmaAllocator allocator, void* src_ptr, size_t size) {
    void* buffer_ptr = nullptr;
    VK_CHECK(vmaMapMemory(allocator, dst_buffer.allocation, &buffer_ptr));

    if (buffer_ptr == nullptr) {
        spdlog::error("Failed to map buffer for copy");
        return;
    }

    memcpy(buffer_ptr, src_ptr, size);
    VK_CHECK(vmaFlushAllocation(allocator, dst_buffer.allocation, 0, size));
    vmaUnmapMemory(allocator, dst_buffer.allocation);
}
uint32_t aligned_size(uint32_t size, uint32_t alignment) {
    uint32_t aligned = size;
    if (alignment > 0) {
        aligned = (aligned + alignment - 1) & ~(alignment - 1);
    }

    return aligned;
}

uint32_t previous_pow2(uint32_t v) {
    uint32_t r = 1;

    while (r * 2 < v)
        r *= 2;

    return r;
}
