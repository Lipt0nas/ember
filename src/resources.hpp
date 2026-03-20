#pragma once

#include "asset.hpp"
#include "ember.hpp"

struct Sound {
    std::string path;
    bool        stream = false;
};

struct Font {
    AssetID atlas_texture_id;

    struct GlyphInfo {
        uint32_t codepoint;

        glm::vec4 uvs;

        float advance_x;
        float bearing_x;
        float bearing_y;

        float width;
        float height;

        template <class Archive> void serialize(Archive& ar) {
            ar(CEREAL_NVP(codepoint),
               CEREAL_NVP(uvs),
               CEREAL_NVP(advance_x),
               CEREAL_NVP(bearing_x),
               CEREAL_NVP(bearing_y),
               CEREAL_NVP(width),
               CEREAL_NVP(height));
        }
    };

    std::unordered_map<uint32_t, GlyphInfo> glyphs;

    float font_size;
    float line_height;
    float ascender;
    float descender;

    template <class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(atlas_texture_id),
           CEREAL_NVP(glyphs),
           CEREAL_NVP(font_size),
           CEREAL_NVP(line_height),
           CEREAL_NVP(ascender),
           CEREAL_NVP(descender));
    }
};

struct MaterialDescription {
    AssetID albedo;
    AssetID normals;
    AssetID material;
    AssetID emissive;

    glm::vec4 albedo_factor;
    glm::vec3 emissive_factor;

    float roughness_factor;
    float metallic_factor;
    float normal_scale;

    template <class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(albedo),
           CEREAL_NVP(normals),
           CEREAL_NVP(material),
           CEREAL_NVP(emissive),
           CEREAL_NVP(albedo_factor),
           CEREAL_NVP(emissive_factor),
           CEREAL_NVP(roughness_factor),
           CEREAL_NVP(metallic_factor),
           CEREAL_NVP(normal_scale));
    }
};

struct TextureAssetHeader {
    uint32_t           format;
    uint32_t           width;
    uint32_t           height;
    uint32_t           mip_levels;
    uint64_t           size;
    bool               compressed;
    SamplerDescription sampler_description;

    template <class Archive> void serialize(Archive& ar) {
        ar(format, width, height, mip_levels, size, compressed, sampler_description);
    }
};

struct MeshAssetHeader {
    uint64_t version;

    uint64_t vertex_buffer_size;
    uint64_t index_buffer_size;
    uint64_t meshlet_buffer_size;
    uint64_t meshlet_vertex_indicies_buffer_size;
    uint64_t meshlet_primitive_buffer_size;
    uint64_t meshlet_bounds_buffer_size;
    uint64_t skin_buffer_size;

    struct MeshDescription {
        glm::vec3 center;
        float     radius;

        glm::vec4 bounds_min;
        glm::vec4 bounds_max;

        uint32_t vertex_count;

        uint32_t lod_count;
        struct LOD {
            uint32_t index_count;
            uint32_t index_offset;

            uint32_t meshlet_count;
            uint32_t meshlet_offset;

            float error;

            template <class Archive> void serialize(Archive& ar) {
                ar(index_count, index_offset, meshlet_count, meshlet_offset, error);
            }
        } lods[8];

        template <class Archive> void serialize(Archive& ar) {
            ar(center, radius, bounds_min, bounds_max, vertex_count, lod_count, lods);
        }
    } mesh;

    template <class Archive> void serialize(Archive& ar) {
        ar(version,
           vertex_buffer_size,
           index_buffer_size,
           meshlet_buffer_size,
           meshlet_vertex_indicies_buffer_size,
           meshlet_primitive_buffer_size,
           meshlet_bounds_buffer_size,
           skin_buffer_size,
           mesh);
    }
};

struct SkeletonAssetHeader {
    uint64_t version = 1;
    uint32_t joint_count;

    struct JointDescription {
        int32_t   parent_index;
        glm::vec3 bind_translation;
        glm::quat bind_rotation;
        glm::vec3 bind_scale;
        glm::mat4 inverse_bind_matrix;

        template <class Archive> void serialize(Archive& ar) {
            ar(parent_index, bind_translation, bind_rotation, bind_scale, inverse_bind_matrix);
        }
    };

    template <class Archive> void serialize(Archive& ar) {
        ar(version, joint_count);
    }
};

struct AnimationAssetHeader {
    uint64_t version = 1;
    float    duration;
    AssetID  skeleton_id;
    uint32_t channel_count;
    uint64_t time_buffer_size;
    uint64_t vec3_buffer_size;
    uint64_t quat_buffer_size;

    struct ChannelDescriptor {
        uint32_t joint_index;
        uint32_t path;
        uint32_t keyframe_count;
        uint32_t time_offset;
        uint32_t value_offset;

        template <class Archive> void serialize(Archive& ar) {
            ar(joint_index, path, keyframe_count, time_offset, value_offset);
        }
    };

    template <class Archive> void serialize(Archive& ar) {
        ar(version, duration, skeleton_id, channel_count, time_buffer_size, vec3_buffer_size, quat_buffer_size);
    }
};

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
    size_t          data_size,
    VkDeviceSize    dst_buffer_offset = 0,
    VkDeviceSize    src_buffer_offset = 0
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

void copy_image_mip(
    const Buffer&   src_buffer,
    const Image&    dst_image,
    uint32_t        mip_level,
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

struct RendererBuffers {
    Buffer staging_buffer;
    Buffer vertex_buffer;
    Buffer index_buffer;
    Buffer meshlet_buffer;
    Buffer meshlet_vertex_indices;
    Buffer meshlet_primitive_buffer;
    Buffer meshlet_bounds_buffer;
    Buffer skin_buffer;
};

// TODO: This should probably be merged into the buffer struct somehow, or wrapped in another struct
struct BufferOffsets {
    uint64_t vertex_buffer            = 0;
    uint64_t index_buffer             = 0;
    uint64_t meshlet_buffer           = 0;
    uint64_t meshlet_vertex_indices   = 0;
    uint64_t meshlet_primitive_buffer = 0;
    uint64_t meshlet_bounds_buffer    = 0;

    uint64_t texture_data_size = 0;

    uint64_t skin_buffer_offset = 0;
};

struct ImageResource {
    Image image;
    int   sampler_index;
};

enum class LightType : int {
    POINT,
    SPOT,
    TUBE
};

struct Light {
    glm::vec3 position = {};
    float     radius   = 3.0f;

    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};

    glm::vec3 direction        = {0.0f, 0.0f, -1.0f};
    float     inner_cone_angle = 20.0f;

    float     outer_cone_angle  = 30.0f;
    float     area_width        = 1.0f;
    LightType type              = LightType::POINT;
    int       ies_profile_index = -1;

    int casts_shadow = false;
    int _pad0        = 0;
    int _pad1        = 0;
    int _pad2        = 0;

    template <class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(position),
           CEREAL_NVP(radius),
           CEREAL_NVP(color),
           CEREAL_NVP(direction),
           CEREAL_NVP(outer_cone_angle),
           CEREAL_NVP(inner_cone_angle),
           CEREAL_NVP(area_width),
           CEREAL_NVP(type),
           CEREAL_NVP(ies_profile_index),
           CEREAL_NVP(casts_shadow));
    }
};
