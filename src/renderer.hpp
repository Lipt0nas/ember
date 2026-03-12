#pragma once

#include "ember.hpp"

#include "camera.hpp"
#include "framegraph.hpp"
#include "pipeline.hpp"
#include "resources.hpp"
#include "rt_scene.hpp"
#include "swapchain.hpp"

namespace tracy {
    class VkCtx;
}

class SpriteBatcher {
public:
    struct Drawcall {
        glm::vec3 position   = {0.0f, 0.0f, 0.0f};
        glm::quat rotation   = {0.0f, 0.0f, 0.0f, 1.0f};
        glm::vec2 size       = {1.0f, 1.0f};
        glm::vec2 pivot      = {0.5f, 0.5f};
        glm::vec4 uvs        = {0.0f, 0.0f, 1.0f, 1.0f};
        glm::vec4 color      = {1.0f, 1.0f, 1.0f, 1.0f};
        int       data_index = 0;
    };

    struct SpriteVertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
        glm::vec4 color;
        int       data_index;
    };

    Buffer drawcall_buffer;
    Buffer geometry_buffer;

    Pipeline                     geometry_build_pipline;
    std::vector<VkDescriptorSet> geometry_build_pipline_descriptor_sets;

    SpriteBatcher(class Renderer* renderer, uint32_t max_drawcalls);

    void reset();
    void draw(const Drawcall& drawcall);
    void build_geometry_buffer(VmaAllocator vma_allocator, VkCommandBuffer command_buffer, uint32_t frame_index);
    void end_batch();

    void destroy();

    uint32_t              drawcall_count = 0;
    std::vector<uint32_t> drawcall_batches;

private:
    uint32_t frames_in_flight = 0;
    uint32_t max_drawcalls    = 0;

    std::vector<Drawcall> drawcalls;
};

class Renderer {
public:
    Renderer();
    void cleanup();

    void initialize(
        class World* world, struct SDL_Window* window, bool meshlets_enabled, bool hardware_rt_enabled, bool vsync
    );

    void begin_frame(Camera* camera);
    void render_frame(float delta_time);
    void end_frame();

    VkCommandBuffer get_current_command_buffer();

    VkCommandBuffer allocate_temporary_command_buffer();
    void            free_temporary_command_buffer(VkCommandBuffer command_buffer);

    void wait_idle();

    Swapchain swapchain;

    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkInstance               instance        = VK_NULL_HANDLE;
    VmaAllocator             vma_allocator   = VK_NULL_HANDLE;
    VkDevice                 device          = VK_NULL_HANDLE;
    VkPhysicalDevice         physical_device = VK_NULL_HANDLE;

    VkPhysicalDeviceProperties physical_device_properties;

    uint32_t graphics_family_index;
    VkQueue  graphics_queue = VK_NULL_HANDLE;

    bool enable_particles           = false;
    bool meshlets_enabled           = false;
    bool hardware_rt_enabled        = false;
    bool supports_timestamp_queries = true;

    constexpr static int FRAMES_IN_FLIGHT = 2;

    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

    VkDescriptorSet       global_texture_descriptor_set    = VK_NULL_HANDLE;
    VkDescriptorSetLayout global_texture_descriptor_layout = VK_NULL_HANDLE;

    RendererBuffers buffers;
    BufferOffsets   buffer_offsets;

    VkSampler linear_sampler         = VK_NULL_HANDLE;
    VkSampler linear_sampler_clamped = VK_NULL_HANDLE;
    VkSampler depth_sampler          = VK_NULL_HANDLE;
    VkSampler nearest_sampler        = VK_NULL_HANDLE;

    Image depth_buffer;
    Image depth_hiz;

    Image gbuffer_albedo;
    Image gbuffer_normals;
    Image gbuffer_emissive;
    Image gbuffer_id;

    Image ui_buffer;

    Image ddgi_depth_atlas;
    Image ddgi_depth_atlas_history;
    Image ddgi_irradiance;
    Image ddgi_irradiance_history;

    Image bloom_buffer;

    Image average_luminance_image;

    Image smaa_edges;
    Image smaa_weights;
    Image smaa_output;

    Image directional_shadow_buffer;
    Image directional_shadow_buffer_pong;

    Image hilbert_lut;

    Image ao_output;
    Image ao_output_edges;
    Image ao_output_denoised;
    Image ao_prefiltered_depth;

    Image lightpass_output;

    Image composite_output;

    Image rt_reflection_buffer;
    Image rt_reflection_history;

    Image smaa_area_tex;
    Image smaa_search_tex;

    Image brdf_lut;

    uint32_t frame_count = 0;
    uint32_t frame_index = 0;

    glm::mat4 frozen_view = glm::mat4(1.0f);
    float     frozen_frustum[4];

    int  min_lod         = 0;
    bool debug_frustum   = false;
    bool disable_culling = false;

    int   bloom_levels;
    float bloom_upscale_sample_scale;

    float min_log_lum    = -4.0f;
    float max_log_lum    = 4.0f;
    float adaption_speed = 1.1f;

    float camera_aperture     = 8.0f;
    float camera_shutter_time = 1.0f / 60.0f;
    float camera_iso          = 100.0f;

    bool editor_overlay   = true;
    bool visualize_probes = false;

    std::array<uint64_t, 2> pipeline_stats;

    uint32_t swapchain_image_index = 0;

    Camera*                   camera;
    Camera                    ui_camera;
    std::vector<MeshInstance> mesh_instances;

    Framegraph* framegraph = nullptr;

private:
    class World* world = nullptr;

    glm::mat4 last_frame_view_proj = glm::mat4(1.0f);

    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;

    VkFence     frame_fences[FRAMES_IN_FLIGHT];
    VkQueryPool statistics_pools[FRAMES_IN_FLIGHT];

    VkCommandPool command_pool           = VK_NULL_HANDLE;
    VkCommandPool temporary_command_pool = VK_NULL_HANDLE;

    VkCommandBuffer command_buffers[FRAMES_IN_FLIGHT];

    SpriteBatcher* world_sprite_batcher = nullptr;
    SpriteBatcher* ui_sprite_batcher    = nullptr;

    std::vector<VkDescriptorPoolSize> descriptor_pool_sizes;

    Buffer staging_buffer;

    Buffer global_vertex_buffer;
    Buffer global_index_buffer;

    Buffer meshlet_buffer;
    Buffer meshlet_vertex_indices_buffer;
    Buffer meshlet_primitive_indices_buffer;
    Buffer meshlet_bounds_buffer;

    Buffer ddgi_ray_buffer;
    Buffer ddgi_probe_buffer;

    Buffer indirect_dispatch_tile_copy_buffer;
    Buffer indirect_dispatch_tile_process_buffer;

    Buffer indirect_command_buffer;
    Buffer drawcall_buffer;
    Buffer material_buffer;
    Buffer mesh_buffer;

    Buffer scene_ubo_buffer;
    Buffer lighting_ubo_buffer;

    Buffer particle_buffer;
    Buffer particle_position_buffer;
    Buffer particle_velocity_buffer;

    Buffer luminance_buffer;

    std::vector<VkImageView>                   depth_mip_views;
    std::vector<VkImageView>                   bloom_mip_views;
    std::vector<std::reference_wrapper<Image>> gbuffer_images;
    std::vector<VkImageView>                   ao_depth_mip_views;
    std::vector<VkImageView>                   rt_reflection_views;

    constexpr static int RT_MAX_TLAS_INSTANCES = 10000;
    RTScene              rt_scene;

    constexpr static uint32_t bindless_texture_count = 10000;

    tracy::VkCtx* tracy_vk_context = nullptr;

    VkDescriptorPool imgui_descriptor_pool = VK_NULL_HANDLE;

    enum class DynamicOffset : uint32_t {
        SCENE_UBO = 0,
        INDIRECT_COMMAND_BUFFER,
        LIGHTING_UBO,
        MESH_BUFFER,
        MATERIAL_BUFFER,
        DRAWCALL_BUFFER,

        COUNT = DRAWCALL_BUFFER + 1
    };

    struct MeshIndirectDrawCommand {
        uint32_t group_count_x;
        uint32_t group_count_y;
        uint32_t group_count_z;

        uint32_t object_id;
        uint32_t meshlet_count;
        uint32_t meshlet_offset;
    };

    struct alignas(16) SceneUBO {
        glm::mat4 proj;
        glm::vec4 camera_position;

        glm::mat4 view_proj;
        glm::mat4 inverse_view_proj;

        glm::mat4 view;
        float     frustum[4];

        glm::mat4 frozen_view = glm::mat4(1.0f);
        float     frozen_frustum[4];

        uint32_t debug_frustum;
        uint32_t disable_culling;

        float P00;
        float P11;

        float near_plane;
        float far_plane;
        float lod_target;
        float pad;

        glm::mat4 last_frame_view_proj;
    };

#define DDGI_NUM_FIXED_RAYS 32
#define MAX_RAYS_PER_PROBE  256
    struct alignas(16) LightingUBO {
        glm::vec4 light_direction;
        glm::vec4 light_color;

        glm::vec3 grid_origin;
        float     probe_spacing;

        glm::ivec3 probe_counts;
        int        texels_per_probe;

        int multibounce;
        int remove_visibility_checks;

        int depth_texels_per_probe;
        int rays_per_probe;

        glm::vec3 camera_pos;
        int       frame_index;

        float gi_intensity;
        int   use_bent_normals;
        int   compensate_specular;
        int   disney_diffuse;

        glm::vec4 sky_hemisphere_top;
        glm::vec4 sky_hemisphere_bottom;

        glm::quat ddgi_probe_ray_rotation;
    };

    struct LuminanceConstants {
        float min_log2_luminance;
        float inverse_log2_luminance;

        float time_coef;
        float pixel_count;
    };

    struct ShadowBlurConstants {
        glm::vec2 image_size;
        float     direction;
        float     znear;
    };

    struct GlossyRTConstants {
        glm::mat4 last_frame_view_proj;
        uint32_t  frame_index;
    };

    struct CullPassPushConstants {
        glm::vec2 screen_size;

        uint32_t draw_count;

        uint32_t disable_frustum_cull;
        uint32_t disable_depth_cull;

        uint32_t enable_lods;
    };

    struct DepthReduceConstants {
        glm::vec2 size;
    };

    struct DDGIRay {
        glm::vec4 direction;
    };

    struct DDGIProbe {
        glm::vec3 offset;
        int       active;
    };

    struct XeGTAOConstants {
        glm::vec2 camera_tan_half_fov;
        glm::vec2 ndc_to_view_mul;
        glm::vec2 ndc_to_view_add;
        glm::vec2 ndc_to_view_mul_x_pixel_size;
        glm::vec2 camera_near_far;

        uint32_t final_pass;
    };

    struct CompositePushConstants {
        float    bloom_strength;
        uint32_t tonemapping_type;

        float    min_ev100;
        float    max_ev100;
        uint32_t enable_auto_exposure;

        float manual_ev100;
        float exposure_compensation;
    };

    struct BloomPushConstants {
        glm::vec2 texel_size;

        uint32_t first_pass;
        float    upsample_radius;
    };

    struct GeometryPushConstants {
        glm::vec2 screen_size;

        uint32_t disable_frustum_cull;
        uint32_t disable_depth_cull;
        uint32_t disable_cone_cull;
        uint32_t disable_small_triangle_cull;
    };

    struct DebugRendererConstants {
        glm::mat4 combined_matrix;
        glm::vec3 camera_pos;
        int       cull_innactive_probes;
    };

    struct DebugRenderer {
        Buffer vertex_buffer;
        Buffer index_buffer;
        Buffer instance_buffer;

        uint32_t frames_in_flight;

        Pipeline pipeline;

        uint32_t                     index_count;
        uint32_t                     instance_count;
        std::vector<glm::vec4>       instances;
        std::vector<VkDescriptorSet> descriptor_sets;
    };

    DebugRenderer debug_renderer;

    DescriptorLayout scene_data_layout;
    DescriptorLayout draw_data_layout;
    DescriptorLayout geometry_data_layout;

    Pipeline                     cull_pipeline;
    std::vector<VkDescriptorSet> cull_descriptor_sets;

    Pipeline                     gpass_pipeline;
    std::vector<VkDescriptorSet> gpass_descriptor_sets;

    Pipeline hiz_pipeline;

    Pipeline                     ao_prefilter_pipeline;
    std::vector<VkDescriptorSet> ao_prefilter_descriptor_sets;

    Pipeline                     ao_pipeline;
    std::vector<VkDescriptorSet> ao_descriptor_sets;

    Pipeline                     ao_denoise_pipeline;
    std::vector<VkDescriptorSet> ao_denoise_descriptor_sets;

    Pipeline                     light_pipeline;
    std::vector<VkDescriptorSet> light_descriptor_sets;

    Pipeline bloom_downsample_pipeline;
    Pipeline bloom_upsample_pipeline;

    Pipeline                     composite_pipeline;
    std::vector<VkDescriptorSet> composite_descriptor_sets;

    Pipeline                     ddgi_ray_pipeline;
    std::vector<VkDescriptorSet> ddgi_ray_descriptor_sets;

    Pipeline                     ddgi_probe_blend_irradiance_pipeline;
    std::vector<VkDescriptorSet> ddgi_probe_blend_irradiance_descriptor_sets;

    Pipeline                     ddgi_probe_blend_depth_pipeline;
    std::vector<VkDescriptorSet> ddgi_probe_blend_depth_descriptor_sets;

    Pipeline                     ddgi_probe_classify_pipeline;
    std::vector<VkDescriptorSet> ddgi_probe_classify_descriptor_sets;

    Pipeline                     ddgi_probe_relocate_pipeline;
    std::vector<VkDescriptorSet> ddgi_probe_relocate_descriptor_sets;

    Pipeline                     shadow_pipeline;
    std::vector<VkDescriptorSet> shadow_pass_descriptor_sets;

    Pipeline                     shadow_fill_pipeline;
    std::vector<VkDescriptorSet> shadow_fill_pass_descriptor_sets;

    Pipeline shadow_blur_pipeline;

    Pipeline                     rt_reflection_pipeline;
    std::vector<VkDescriptorSet> rt_reflection_pass_descriptor_sets;

    Pipeline                     rt_reflection_upsample;
    std::vector<VkDescriptorSet> rt_upsample_pass_descriptor_sets;

    Pipeline                     tile_clear_pipeline;
    std::vector<VkDescriptorSet> tile_clear_descriptor_sets;

    Pipeline                     reflection_tile_copy_pipeline;
    std::vector<VkDescriptorSet> reflection_tile_copy_descriptor_sets;

    static constexpr int particle_count = 10000;

    Pipeline                     particle_update_pipeline;
    std::vector<VkDescriptorSet> particle_update_descriptor_sets;

    Pipeline                     particle_render_pipeline;
    std::vector<VkDescriptorSet> particle_render_descriptor_sets;

    Pipeline                     luminance_histogram_pipeline;
    std::vector<VkDescriptorSet> luminance_histogram_descriptor_sets;

    Pipeline                     luminance_average_pipeline;
    std::vector<VkDescriptorSet> luminance_average_descriptor_sets;

    Pipeline                     smaa_edge_pipeline;
    std::vector<VkDescriptorSet> smaa_edge_pass_descriptor_sets;

    Pipeline                     smaa_weights_pipeline;
    std::vector<VkDescriptorSet> smaa_weights_descriptor_sets;

    Pipeline                     smaa_blend_pipeline;
    std::vector<VkDescriptorSet> smaa_blend_descriptor_sets;

    Pipeline                     world_sprite_pipeline;
    std::vector<VkDescriptorSet> world_sprite_pipeline_descriptor_sets;

    Pipeline ui_sprite_pipeline;
    Pipeline ui_sprite_text_pipeline;

    std::vector<uint32_t> dynamic_offsets;

    void setup_framegraph();

    void debug_renderer_start_frame(DebugRenderer& renderer, uint32_t frame_index);
    void debug_renderer_upload_data(DebugRenderer& renderer, VmaAllocator vma_allocator, uint32_t frame_index);
    void
    debug_renderer_draw_sphere(DebugRenderer& renderer, const glm::vec3& center, float radius, const glm::vec4& color);
    DebugRenderer create_debug_renderer(
        const Buffer&    lighting_ubo,
        const VkSampler& sampler,
        const Image&     ddgi_irradiance,
        const Image&     ddgi_depth,
        const Buffer&    ddgi_probe_buffer,
        VkDevice         device,
        VmaAllocator     vma_allocator,
        uint32_t         frames_in_flight,
        VkFormat         depth_format,
        VkDescriptorPool descriptor_pool
    );
    void destroy_debug_renderer(const DebugRenderer& renderer, VkDevice device, VmaAllocator vma_allocator);

public:
    DebugRendererConstants debug_renderer_constants;
    LightingUBO            lighting_data;
    LuminanceConstants     luminance_constants;
    CullPassPushConstants  cull_push_constants;
    GeometryPushConstants  gpass_push_constants;
    DepthReduceConstants   depth_reduce_constants;
    XeGTAOConstants        xegtao_constants;
    BloomPushConstants     bloom_constants;
    CompositePushConstants composite_push_constants;
    GlossyRTConstants      glossy_rt_constants;
    ShadowBlurConstants    shadow_blur_constants;
};
