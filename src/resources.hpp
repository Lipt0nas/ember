#pragma once

#include "ember.hpp"

struct Sampler {
    VkSampler handle;

    VkFilter mag_filter;
    VkFilter min_filter;

    VkSamplerMipmapMode mipmap_mode;

    VkSamplerAddressMode address_mode_u;
    VkSamplerAddressMode address_mode_v;
    VkSamplerAddressMode address_mode_w;

    float anisotropy;
};

struct Buffer {
    VkBuffer      handle;
    VkDeviceSize  size;
    VmaAllocation allocation;
};

struct Image {
    uint32_t width;
    uint32_t height;

    uint32_t levels;
    uint32_t layers;

    VkFormat           format;
    VkImageAspectFlags aspect;

    VkImage       handle;
    VkImageView   view;
    VmaAllocation allocation;
};

Buffer create_buffer(
    VkDeviceSize             size,
    VkBufferUsageFlags       usage_flags,
    VmaAllocator             allocator,
    VmaAllocationCreateFlags allocation_flags = 0,
    size_t                   alignment        = 0
);
void destroy_buffer(const Buffer& buffer, VkDevice device, VmaAllocator allocator);
void copy_buffer(
    const Buffer&   src_buffer,
    const Buffer&   dst_buffer,
    VkCommandBuffer command_buffer,
    VkQueue         queue,
    VkDevice        device,
    void*           data,
    size_t          data_size,
    VkDeviceSize    dst_buffer_offset = 0
);

void copy_to_buffer(const Buffer& dst_buffer, VmaAllocator allocator, void* src_ptr, size_t size);

uint64_t get_buffer_device_address(const Buffer& buffer, VkDevice device);

void buffer_pipeline_barrier(
    const Buffer&         buffer,
    VkCommandBuffer       command_buffer,
    VkPipelineStageFlags2 src_stage_mask,
    VkAccessFlags2        src_access_mask,
    VkPipelineStageFlags2 dst_stage_mask,
    VkAccessFlags2        dst_access_mask,
    VkDeviceSize          offset,
    VkDeviceSize          size
);

void memory_pipeline_barrier(
    VkCommandBuffer       command_buffer,
    VkPipelineStageFlags2 src_stage_mask,
    VkAccessFlags2        src_access_mask,
    VkPipelineStageFlags2 dst_stage_mask,
    VkAccessFlags2        dst_access_mask
);

Image load_image(
    const std::filesystem::path& path,
    VkFormat                     format,
    bool                         generate_mipmaps,
    const Buffer&                staging_buffer,
    VkCommandBuffer              command_buffer,
    VkQueue                      queue,
    VmaAllocator                 allocator,
    VkDevice                     device
);

Image create_image_with_data(
    VkFormat           format,
    uint32_t           width,
    uint32_t           height,
    VkImageUsageFlags  usage,
    VkImageAspectFlags aspect,
    bool               generate_mipmaps,
    const Buffer&      staging_buffer,
    void*              data_ptr,
    size_t             data_size,
    VkCommandBuffer    command_buffer,
    VkQueue            queue,
    VmaAllocator       allocator,
    VkDevice           device
);

Image create_image(
    VkFormat           format,
    uint32_t           width,
    uint32_t           height,
    VkImageUsageFlags  usage,
    VkImageAspectFlags aspect,
    bool               generate_mipmaps,
    VmaAllocator       allocator,
    VkDevice           device
);

Image create_cubemap_image(
    VkFormat           format,
    uint32_t           width,
    uint32_t           height,
    VkImageUsageFlags  usage,
    VkImageAspectFlags aspect,
    bool               generate_mipmaps,
    VmaAllocator       allocator,
    VkDevice           device
);
void destroy_image(const Image& image, VkDevice device, VmaAllocator allocator);

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
);

void image_pipeline_barrier(
    const Image&          image,
    VkCommandBuffer       command_buffer,
    VkImageLayout         old_layout,
    VkImageLayout         new_layout,
    VkPipelineStageFlags2 src_stage_mask,
    VkAccessFlags2        src_access_mask,
    VkPipelineStageFlags2 dst_stage_mask,
    VkAccessFlags2        dst_access_mask
);

void copy_image(
    const Buffer&   src_buffer,
    const Image&    dst_image,
    bool            generate_mipmaps,
    VkCommandBuffer command_buffer,
    VkQueue         queue,
    VkDevice        device
);

Sampler create_sampler(
    VkFilter             mag_filter,
    VkFilter             min_filter,
    VkSamplerMipmapMode  mipmap_mode,
    VkSamplerAddressMode address_mode_u,
    VkSamplerAddressMode address_mode_v,
    VkSamplerAddressMode address_mode_w,
    float                anisotropy,
    VkDevice             device,
    void*                extensions = nullptr
);

void destroy_sampler(const Sampler& sampler, VkDevice device);

uint32_t aligned_size(uint32_t size, uint32_t alignment);

uint32_t previous_pow2(uint32_t v);
