#include "ember.hpp"

#include "args.hpp"
#include "camera.hpp"
#include "device.hpp"
#include "editor.hpp"
#include "embedded.hpp"
#include "framegraph.hpp"
#include "geometry.hpp"
#include "imgui_internal.h"
#include "input_system.hpp"
#include "physics.hpp"
#include "pipeline.hpp"
#include "resources.hpp"
#include "rt_scene.hpp"
#include "scene.hpp"
#include "scene_serializer.hpp"
#include "script_system.hpp"
#include "swapchain.hpp"
#include "ui.hpp"

#include <map>

#include <glm/gtc/random.hpp>

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>

#include <ImGuizmo.h>

enum class DynamicOffset : uint32_t {
    SCENE_UBO = 0,
    INDIRECT_COMMAND_BUFFER,
    LIGHTING_UBO,
    MESH_BUFFER,
    MATERIAL_BUFFER,
    DRAWCALL_BUFFER,

    COUNT = DRAWCALL_BUFFER + 1
};

struct EditorViewportSource {
    Image           image;
    VkDescriptorSet descriptor_set;
};

#define XEGTAO_HILBERT_LEVEL 6U
#define XEGTAO_HILBERT_WIDTH ((1U << XEGTAO_HILBERT_LEVEL))
#define XEGTAO_HILBERT_AREA  (XEGTAO_HILBERT_WIDTH * XEGTAO_HILBERT_WIDTH)
uint32_t hilbert_index(uint32_t pos_x, uint32_t pos_y) {
    uint32_t index = 0;
    for (uint32_t i = XEGTAO_HILBERT_WIDTH / 2; i > 0; i /= 2) {
        uint32_t region_x = (pos_x & i) > 0;
        uint32_t region_y = (pos_y & i) > 0;

        index += i * i * ((3 * region_x) ^ region_y);
        if (region_y == 0) {
            if (region_x == 1) {
                pos_x = (XEGTAO_HILBERT_WIDTH - 1) - pos_x;
                pos_y = (XEGTAO_HILBERT_WIDTH - 1) - pos_y;
            }

            uint32_t temp = pos_x;
            pos_x         = pos_y;
            pos_y         = temp;
        }
    }

    return index;
}

Image create_hilbert_lut(
    const Buffer& staging_buffer, VkCommandBuffer command_buffer, VkQueue queue, VmaAllocator allocator, VkDevice device
) {
    std::array<uint16_t, 64 * 64> data;

    for (int x = 0; x < 64; x++) {
        for (int y = 0; y < 64; y++) {
            uint32_t r2_index = hilbert_index(x, y);
            assert(r2_index < UINT16_MAX);

            data[x + 64 * y] = static_cast<uint16_t>(r2_index);
        }
    }

    return create_image_with_data(
        VK_FORMAT_R16_UINT,
        64,
        64,
        VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        staging_buffer,
        data.data(),
        sizeof(uint16_t) * data.size(),
        command_buffer,
        queue,
        allocator,
        device
    );
}

static std::uniform_real_distribution<float> rng_distribution(0.f, 1.f);
static std::mt19937                          rng;

float get_random_float() {
    return rng_distribution(rng);
}

glm::quat ddgi_random_rotation() {
    // This approach is based on James Arvo's implementation from Graphics Gems 3 (pg 117-120).
    // Also available at: http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.53.1357&rep=rep1&type=pdf

    float u1   = glm::two_pi<float>() * get_random_float();
    float cos1 = glm::cos(u1);
    float sin1 = glm::sin(u1);

    float u2   = glm::two_pi<float>() * get_random_float();
    float cos2 = glm::cos(u2);
    float sin2 = glm::sin(u2);

    float u3  = get_random_float();
    float sq3 = 2.f * glm::sqrt(u3 * (1.f - u3));

    float s2 = 2.f * u3 * sin2 * sin2 - 1.f;
    float c2 = 2.f * u3 * cos2 * cos2 - 1.f;
    float sc = 2.f * u3 * sin2 * cos2;

    float _11 = cos1 * c2 - sin1 * sc;
    float _12 = sin1 * c2 + cos1 * sc;
    float _13 = sq3 * cos2;

    float _21 = cos1 * sc - sin1 * s2;
    float _22 = sin1 * sc + cos1 * s2;
    float _23 = sq3 * sin2;

    float _31 = cos1 * (sq3 * cos2) - sin1 * (sq3 * sin2);
    float _32 = sin1 * (sq3 * cos2) + cos1 * (sq3 * sin2);
    float _33 = 1.f - 2.f * u3;

    glm::mat3 transform(glm::vec3(_11, _21, _31), glm::vec3(_12, _22, _32), glm::vec3(_13, _23, _33));

    return glm::quat_cast(transform);
}

void transition_clear_color_image_subresource(
    const Image&            image,
    VkImageLayout           old_layout,
    VkImageLayout           new_layout,
    VkPipelineStageFlags2   src_stage_mask,
    VkAccessFlags2          src_access_mask,
    VkPipelineStageFlags2   dst_stage_mask,
    VkAccessFlags2          dst_access_mask,
    VkImageSubresourceRange subresource,
    VkCommandBuffer         command_buffer
) {
    image_pipeline_barrier(
        image.handle,
        command_buffer,
        old_layout,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        src_stage_mask,
        src_access_mask,
        VK_PIPELINE_STAGE_2_CLEAR_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        subresource
    );

    VkClearColorValue clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
    vkCmdClearColorImage(
        command_buffer, image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &subresource
    );

    image_pipeline_barrier(
        image.handle,
        command_buffer,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        new_layout,
        VK_PIPELINE_STAGE_2_CLEAR_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        dst_stage_mask,
        dst_access_mask,
        subresource
    );
}

void transition_clear_color_image(
    const Image&          image,
    VkImageLayout         old_layout,
    VkImageLayout         new_layout,
    VkPipelineStageFlags2 src_stage_mask,
    VkAccessFlags2        src_access_mask,
    VkPipelineStageFlags2 dst_stage_mask,
    VkAccessFlags2        dst_access_mask,
    VkCommandBuffer       command_buffer
) {
    transition_clear_color_image_subresource(
        image,
        old_layout,
        new_layout,
        src_stage_mask,
        src_access_mask,
        dst_stage_mask,
        dst_access_mask,
        VkImageSubresourceRange{
            .aspectMask     = image.aspect,
            .baseMipLevel   = 0,
            .levelCount     = image.levels,
            .baseArrayLayer = 0,
            .layerCount     = image.layers
        },
        command_buffer
    );
}

void initialize_clear_image(const Image& image, VkImageLayout new_layout, VkCommandBuffer command_buffer) {
    transition_clear_color_image(
        image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        new_layout,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        command_buffer
    );
}

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
const int ray_per_probe_values[] = {32, 64, 96, 128, 160, 192, 224, MAX_RAYS_PER_PROBE};
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

void debug_renderer_start_frame(DebugRenderer& renderer, uint32_t frame_index) {
    renderer.instance_count = 0;
}

void debug_renderer_upload_data(DebugRenderer& renderer, VmaAllocator vma_allocator, uint32_t frame_index) {
    void*  ptr        = nullptr;
    size_t ptr_offset = (renderer.instance_buffer.size / renderer.frames_in_flight) * frame_index;
    VK_CHECK(vmaMapMemory(vma_allocator, renderer.instance_buffer.allocation, &ptr));
    memcpy(
        reinterpret_cast<char*>(ptr) + ptr_offset,
        renderer.instances.data(),
        sizeof(glm::vec4) * renderer.instance_count
    );
    vmaUnmapMemory(vma_allocator, renderer.instance_buffer.allocation);
    VK_CHECK(vmaFlushAllocation(
        vma_allocator, renderer.instance_buffer.allocation, ptr_offset, renderer.instance_buffer.size
    ));
}

void debug_renderer_draw_sphere(
    DebugRenderer& renderer, const glm::vec3& center, float radius, const glm::vec4& color
) {
    renderer.instances[renderer.instance_count++] = glm::vec4(center, radius);
}

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
) {
    IcosphereGenerator generator;
    generator.generate(0.45, 3);

    auto max_instances = 64 * 64 * 64; // NOTE: might be a little overkill

    auto vertices = generator.get_vertices();
    auto indices  = generator.get_indices();

    Buffer vertex_buffer = create_buffer(
        sizeof(DebugVertex) * vertices.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    {
        void* ptr = nullptr;
        VK_CHECK(vmaMapMemory(vma_allocator, vertex_buffer.allocation, &ptr));
        memcpy(reinterpret_cast<char*>(ptr), vertices.data(), sizeof(DebugVertex) * vertices.size());
        vmaUnmapMemory(vma_allocator, vertex_buffer.allocation);
        VK_CHECK(vmaFlushAllocation(vma_allocator, vertex_buffer.allocation, 0, vertex_buffer.size));
    }

    Buffer index_buffer = create_buffer(
        sizeof(uint32_t) * indices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    {
        void* ptr = nullptr;
        VK_CHECK(vmaMapMemory(vma_allocator, index_buffer.allocation, &ptr));
        memcpy(reinterpret_cast<char*>(ptr), indices.data(), sizeof(uint32_t) * indices.size());
        vmaUnmapMemory(vma_allocator, index_buffer.allocation);
        VK_CHECK(vmaFlushAllocation(vma_allocator, index_buffer.allocation, 0, index_buffer.size));
    }

    Buffer instance_buffer = create_buffer(
        sizeof(glm::vec3) * max_instances * frames_in_flight,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    Pipeline pipeline = create_debug_render_pipeline(
        device,
        {
            shader_from_file(device, VK_SHADER_STAGE_VERTEX_BIT, "data/shaders/debug.vert.spv"),
            shader_from_file(device, VK_SHADER_STAGE_FRAGMENT_BIT, "data/shaders/debug.frag.spv"),
        },
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info =
                                DescriptorInfo(sampler, ddgi_irradiance.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                        },
                        DescriptorBinding{
                            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info =
                                DescriptorInfo(sampler, ddgi_depth.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                        },
                        DescriptorBinding{
                            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                            .write_info =
                                DescriptorInfo(instance_buffer.handle, 0, instance_buffer.size / frames_in_flight),
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(ddgi_probe_buffer.handle),
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                            .write_info = DescriptorInfo(lighting_ubo.handle, 0, lighting_ubo.size / frames_in_flight)
                        },
                    },
            },
        },
        {VK_FORMAT_R8G8B8A8_UNORM},
        depth_format,
        sizeof(DebugRendererConstants)
    );

    std::vector<VkDescriptorSet> descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, pipeline);

    std::vector<glm::vec4> instances;
    instances.resize(max_instances);

    return DebugRenderer{
        .vertex_buffer    = vertex_buffer,
        .index_buffer     = index_buffer,
        .instance_buffer  = instance_buffer,
        .frames_in_flight = frames_in_flight,
        .pipeline         = pipeline,
        .index_count      = static_cast<uint32_t>(indices.size()),
        .instance_count   = 0,
        .instances        = instances,
        .descriptor_sets  = descriptor_sets
    };
}

void destroy_debug_renderer(const DebugRenderer& renderer, VkDevice device, VmaAllocator vma_allocator) {
    destroy_buffer(renderer.vertex_buffer, device, vma_allocator);
    destroy_buffer(renderer.index_buffer, device, vma_allocator);
    destroy_buffer(renderer.instance_buffer, device, vma_allocator);

    destroy_pipeline(device, renderer.pipeline);
}

void populate_materials(const Scene& scene, VkDescriptorSet descriptor_set, VkDevice device) {
    spdlog::info("Texture cache contains: {} textures", scene.images.size());

    uint32_t slot = 1;
    for (auto& image : scene.images) {
        VkDescriptorImageInfo image_write_info = {
            .sampler     = scene.samplers.at(image.sampler_index).handle,
            .imageView   = image.image.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };

        VkWriteDescriptorSet write_set = {
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = nullptr,
            .dstSet           = descriptor_set,
            .dstBinding       = 0,
            .dstArrayElement  = slot++,
            .descriptorCount  = 1,
            .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo       = &image_write_info,
            .pBufferInfo      = nullptr,
            .pTexelBufferView = nullptr
        };
        vkUpdateDescriptorSets(device, 1, &write_set, 0, nullptr);
    }
}

int main(int argc, char* argv[]) {
    ArgParser args;
    args.parse(argc, argv);

    std::random_device rd;
    rng.seed((int)rd());

    spdlog::set_level(spdlog::level::info);
    spdlog::info("Starting ember");

    VK_CHECK(volkInitialize());

    spdlog::info("Initializing SDL");
    if (!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO)) {
        spdlog::error("Failed to initialize SDL");
        return 1;
    }

    auto* window = SDL_CreateWindow("Ember", args.get_arg("w", 1920), args.get_arg("h", 1080), SDL_WINDOW_VULKAN);
    if (!window) {
        spdlog::error("Failed to create SDL window");
        return 1;
    }

    const int FRAMES_IN_FLIGHT = 2;

    bool use_meshlets      = true;
    bool use_hardware_rt   = true;
    bool build_lods        = true;
    bool fast_scene_build  = true;
    bool compress_textures = true;

    bool enable_validation = false;

    bool supports_timestamp_queries = true;

    spdlog::info("Creating Vulkan instance");
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkInstance               instance        = create_instance(enable_validation, debug_messenger);

    spdlog::info("Picking physical device");
    VkPhysicalDevice physical_device       = pick_physical_device(instance);
    uint32_t         graphics_family_index = get_graphics_family_index(physical_device);

    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);

    spdlog::info("Queue index that supports graphics operations: {}", graphics_family_index);

    if (physical_device_properties.limits.timestampPeriod == 0) {
        spdlog::warn("Device does not suport timestamp queries");

        supports_timestamp_queries = false;
    }

    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_properties(queue_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_count, queue_properties.data());

    if (queue_properties[graphics_family_index].timestampValidBits == 0) {
        spdlog::warn("Graphics queue does not suport timestamp queries");

        supports_timestamp_queries = false;
    }

    spdlog::info("Creating device");
    VkDevice device = create_device(
        instance, physical_device, graphics_family_index, enable_validation, use_meshlets, use_hardware_rt
    );
    volkLoadDevice(device);

    use_meshlets      = args.get_arg<bool>("meshlets", true);
    build_lods        = args.get_arg<bool>("lods", true);
    fast_scene_build  = args.get_arg<bool>("fast-build", false);
    compress_textures = args.get_arg<bool>("compress-textures", true);

    spdlog::info(
        "Extension support:\n\tMesh shading: {}\n\tRay tracing: {}\n\tFast Scene Build: {}\n\tLOD's: {}\n\tTexture "
        "Compression: {}",
        use_meshlets,
        use_hardware_rt,
        fast_scene_build,
        build_lods,
        compress_textures
    );

    VmaAllocatorCreateInfo allocator_info = {
        .flags                          = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice                 = physical_device,
        .device                         = device,
        .preferredLargeHeapBlockSize    = 0,
        .pAllocationCallbacks           = nullptr,
        .pDeviceMemoryCallbacks         = nullptr,
        .pHeapSizeLimit                 = nullptr,
        .pVulkanFunctions               = nullptr,
        .instance                       = instance,
        .vulkanApiVersion               = VK_API_VERSION_1_4,
        .pTypeExternalMemoryHandleTypes = nullptr
    };

    VmaVulkanFunctions vma_functions = {};
    VK_CHECK(vmaImportVulkanFunctionsFromVolk(&allocator_info, &vma_functions));
    allocator_info.pVulkanFunctions = &vma_functions;

    VmaAllocator vma_allocator;
    VK_CHECK(vmaCreateAllocator(&allocator_info, &vma_allocator));

    Swapchain swapchain = create_swapchain(window, instance, device, physical_device, false);

    VkQueue graphics_queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, graphics_family_index, 0, &graphics_queue);

    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0
    };

    std::vector<VkSemaphore> image_available_semaphores(swapchain.images.size());
    std::vector<VkSemaphore> render_finished_semaphores(swapchain.images.size());
    for (int i = 0; i < swapchain.images.size(); i++) {
        VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available_semaphores[i]));
        VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished_semaphores[i]));
    }

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    VkFence     frame_fences[FRAMES_IN_FLIGHT] = {VK_NULL_HANDLE};
    VkQueryPool statistics_pools[FRAMES_IN_FLIGHT];
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateFence(device, &fence_info, nullptr, &frame_fences[i]));

        VkQueryPoolCreateInfo pool_info = {
            .sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext              = nullptr,
            .flags              = 0,
            .queryType          = VK_QUERY_TYPE_PIPELINE_STATISTICS,
            .queryCount         = 1,
            .pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
                                  VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT,
        };

        VK_CHECK(vkCreateQueryPool(device, &pool_info, nullptr, &statistics_pools[i]));
    }

    VkCommandPoolCreateInfo command_pool_info = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphics_family_index
    };

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(device, &command_pool_info, nullptr, &command_pool));

    VkCommandBufferAllocateInfo command_buffer_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = FRAMES_IN_FLIGHT
    };

    VkCommandBuffer command_buffers[FRAMES_IN_FLIGHT] = {VK_NULL_HANDLE};
    VK_CHECK(vkAllocateCommandBuffers(device, &command_buffer_info, &command_buffers[0]));

    tracy::VkCtx* tracy_vk_context = TracyVkContextCalibrated(
        instance,
        physical_device,
        device,
        graphics_queue,
        command_buffers[0],
        vkGetInstanceProcAddr,
        vkGetDeviceProcAddr
    );

    std::vector<VkDescriptorPoolSize> descriptor_pool_sizes = {
        {
            .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 10100,
        },
        {
            .type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 60,
        },
        {
            .type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
            .descriptorCount = 40,
        },
        {
            .type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 70,
        },
        {
            .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 30,
        },
        {
            .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 30,
        },

    };
    if (use_hardware_rt) {
        descriptor_pool_sizes.push_back({
            .type            = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            .descriptorCount = 10,
        });
    }

    VkDescriptorPoolCreateInfo descriptor_pool_info = {
        .sType   = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext   = nullptr,
        .flags   = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1000,
        .poolSizeCount = static_cast<uint32_t>(descriptor_pool_sizes.size()),
        .pPoolSizes    = &descriptor_pool_sizes[0]

    };

    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(device, &descriptor_pool_info, nullptr, &descriptor_pool));

    VkSamplerCreateInfo sampler_info = {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .magFilter               = VK_FILTER_LINEAR,
        .minFilter               = VK_FILTER_LINEAR,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias              = 0.0f,
        .anisotropyEnable        = VK_FALSE,
        .maxAnisotropy           = 0.0f,
        .compareEnable           = VK_FALSE,
        .compareOp               = VK_COMPARE_OP_ALWAYS,
        .minLod                  = 0.0f,
        .maxLod                  = VK_LOD_CLAMP_NONE,
        .borderColor             = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE
    };

    VkSampler linear_sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &linear_sampler));

    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    VkSampler linear_sampler_clamped = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &linear_sampler_clamped));

    sampler_info.mipmapMode                            = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VkSamplerReductionModeCreateInfoEXT reduction_info = {
        .sType         = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT,
        .pNext         = nullptr,
        .reductionMode = VK_SAMPLER_REDUCTION_MODE_MIN,
    };
    sampler_info.pNext = &reduction_info;

    VkSampler depth_sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &depth_sampler));

    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.magFilter        = VK_FILTER_NEAREST;
    sampler_info.minFilter        = VK_FILTER_NEAREST;
    sampler_info.pNext            = nullptr;
    VkSampler nearest_sampler     = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &nearest_sampler));

    Buffer staging_buffer = create_buffer(
        1024 * 1024 * 128,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    Image depth_buffer = create_image(
        VK_FORMAT_D32_SFLOAT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        false,
        vma_allocator,
        device
    );

    uint32_t depth_pyramid_width  = previous_pow2(swapchain.width);
    uint32_t depth_pyramid_height = previous_pow2(swapchain.height);
    Image    depth_hiz            = create_image(
        VK_FORMAT_R32_SFLOAT,
        depth_pyramid_width,
        depth_pyramid_height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        true,
        vma_allocator,
        device
    );

    std::vector<VkImageView> depth_mip_views(depth_hiz.levels);
    for (int i = 0; i < depth_mip_views.size(); i++) {
        VkImageViewCreateInfo depth_mip_view_info = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .image            = depth_hiz.handle,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = depth_hiz.format,
            .subresourceRange = {
                .aspectMask     = depth_hiz.aspect,
                .baseMipLevel   = static_cast<uint32_t>(i),
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1
            },
        };

        VK_CHECK(vkCreateImageView(device, &depth_mip_view_info, nullptr, &depth_mip_views[i]));
    }

    Image bloom_buffer = create_image(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        swapchain.width / 2,
        swapchain.height / 2,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        true,
        vma_allocator,
        device
    );
    std::vector<VkImageView> bloom_mip_views(bloom_buffer.levels);
    for (int i = 0; i < bloom_mip_views.size(); i++) {
        VkImageViewCreateInfo bloom_mip_view_info = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .image            = bloom_buffer.handle,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = bloom_buffer.format,
            .subresourceRange = {
                .aspectMask     = bloom_buffer.aspect,
                .baseMipLevel   = static_cast<uint32_t>(i),
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1
            },
        };

        VK_CHECK(vkCreateImageView(device, &bloom_mip_view_info, nullptr, &bloom_mip_views[i]));
    }

    Image gbuffer_albedo = create_image(
        VK_FORMAT_R8G8B8A8_SRGB,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image gbuffer_normals = create_image(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image gbuffer_emissive = create_image(
        VK_FORMAT_B10G11R11_UFLOAT_PACK32,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image gbuffer_velocity = create_image(
        VK_FORMAT_R16G16_SFLOAT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image gbuffer_id = create_image(
        VK_FORMAT_R32_UINT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Buffer pick_buffer = create_buffer(
        sizeof(uint32_t) * 4,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    std::vector<std::reference_wrapper<Image>> gbuffer_images = {
        gbuffer_albedo,
        gbuffer_normals,
        gbuffer_emissive,
        gbuffer_velocity,
        gbuffer_id,
    };

    bool        visualize_probes = false;
    LightingUBO lighting_data    = {
           .light_direction  = glm::vec4(-0.2f, -0.7f, -1.0f, 0.0f),
           .light_color      = glm::vec4(1.0, 0.9, 0.8, 5.0f),
           .grid_origin      = {0, 10.0f, 0},
           .probe_spacing    = 2.5f,
           .probe_counts     = {32, 16, 32},
           .texels_per_probe = 6,
           .camera_pos       = {},
           .frame_index      = {},
    };

    lighting_data.use_bent_normals         = 1;
    lighting_data.gi_intensity             = 1.5f;
    lighting_data.compensate_specular      = 1;
    lighting_data.multibounce              = 1;
    lighting_data.remove_visibility_checks = 0;
    lighting_data.sky_hemisphere_top       = {0.3, 0.5, 0.8, 1.0};
    lighting_data.sky_hemisphere_bottom    = {0.6, 0.7, 0.9, 1.0};

    lighting_data.depth_texels_per_probe  = 14;
    lighting_data.rays_per_probe          = MAX_RAYS_PER_PROBE;
    lighting_data.ddgi_probe_ray_rotation = ddgi_random_rotation();

    int probes_x = lighting_data.probe_counts.x * lighting_data.probe_counts.y;
    int probes_y = lighting_data.probe_counts.z;

    int irradiance_atlas_width  = probes_x * (lighting_data.texels_per_probe + 2);
    int irradiance_atlas_height = probes_y * (lighting_data.texels_per_probe + 2);

    int depth_atlas_width  = probes_x * (lighting_data.depth_texels_per_probe + 2);
    int depth_atlas_height = probes_y * (lighting_data.depth_texels_per_probe + 2);

    Image ddgi_depth_atlas = create_image(
        VK_FORMAT_R16G16_SFLOAT,
        depth_atlas_width,
        depth_atlas_height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image ddgi_depth_atlas_history = create_image(
        VK_FORMAT_R16G16_SFLOAT,
        depth_atlas_width,
        depth_atlas_height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image ddgi_irradiance = create_image(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        irradiance_atlas_width,
        irradiance_atlas_height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image ddgi_irradiance_history = create_image(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        irradiance_atlas_width,
        irradiance_atlas_height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    spdlog::info("DDGI Irradiance atlas size: {}x{}", ddgi_irradiance.width, ddgi_irradiance.height);
    spdlog::info("DDGI Depth atlas size: {}x{}", ddgi_depth_atlas.width, ddgi_depth_atlas.height);

    Image lightpass_output = create_image(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Buffer luminance_buffer = create_buffer(
        sizeof(uint32_t) * 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_allocator
    );
    Image average_luminance_image = create_image(
        VK_FORMAT_R16_SFLOAT,
        1,
        1,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image smaa_edges = create_image(
        VK_FORMAT_R8G8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image smaa_weights = create_image(
        VK_FORMAT_R8G8B8A8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image smaa_output = create_image(
        VK_FORMAT_R8G8B8A8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image directional_shadow_buffer = create_image(
        VK_FORMAT_R8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image directional_shadow_buffer_pong = create_image(
        VK_FORMAT_R8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image hilbert_lut = create_hilbert_lut(staging_buffer, command_buffers[0], graphics_queue, vma_allocator, device);

    Image ao_output = create_image(
        VK_FORMAT_R32_UINT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image ao_output_edges = create_image(
        VK_FORMAT_R8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image ao_output_denoised = create_image(
        VK_FORMAT_R32_UINT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image ao_prefiltered_depth = create_image(
        VK_FORMAT_R32_SFLOAT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        true,
        vma_allocator,
        device
    );

    std::vector<VkImageView> ao_depth_mip_views(ao_prefiltered_depth.levels);
    for (int i = 0; i < ao_depth_mip_views.size(); i++) {
        VkImageViewCreateInfo mip_view_info = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .image            = ao_prefiltered_depth.handle,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = ao_prefiltered_depth.format,
            .subresourceRange = {
                .aspectMask     = ao_prefiltered_depth.aspect,
                .baseMipLevel   = static_cast<uint32_t>(i),
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1
            },
        };

        VK_CHECK(vkCreateImageView(device, &mip_view_info, nullptr, &ao_depth_mip_views[i]));
    }

    Image composite_output = create_image(
        VK_FORMAT_R8G8B8A8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image rt_reflection_buffer = create_image(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        true,
        vma_allocator,
        device
    );

    Image rt_reflection_history = create_image(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    std::vector<VkImageView> rt_reflection_views(rt_reflection_buffer.levels);
    for (int i = 0; i < 2; i++) {
        VkImageViewCreateInfo mip_view_info = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .image            = rt_reflection_buffer.handle,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = rt_reflection_buffer.format,
            .subresourceRange = {
                .aspectMask     = rt_reflection_buffer.aspect,
                .baseMipLevel   = static_cast<uint32_t>(i),
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1
            },
        };

        VK_CHECK(vkCreateImageView(device, &mip_view_info, nullptr, &rt_reflection_views[i]));
    }

    Image smaa_area_tex = load_image(
        "data/textures/smaa_area_tex.png",
        VK_FORMAT_R8G8B8A8_UNORM,
        false,
        staging_buffer,
        command_buffers[0],
        graphics_queue,
        vma_allocator,
        device
    );

    Image smaa_search_tex = load_image(
        "data/textures/smaa_search_tex.png",
        VK_FORMAT_R8G8B8A8_UNORM,
        false,
        staging_buffer,
        command_buffers[0],
        graphics_queue,
        vma_allocator,
        device
    );

    Image brdf_lut = create_image(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        512,
        512,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    {
        VkCommandBufferBeginInfo begin_info = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .pInheritanceInfo = nullptr
        };

        VkCommandBuffer command_buffer = command_buffers[0];

        VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

        for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
            vkCmdResetQueryPool(command_buffer, statistics_pools[i], 0, 1);
        }

        image_pipeline_barrier(
            depth_hiz,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0
        );

        image_pipeline_barrier(
            bloom_buffer,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0
        );

        image_pipeline_barrier(
            depth_buffer,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0
        );

        for (auto image : gbuffer_images) {
            image_pipeline_barrier(
                image.get(),
                command_buffer,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                0,
                VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                0
            );
        }

        initialize_clear_image(ddgi_depth_atlas, VK_IMAGE_LAYOUT_GENERAL, command_buffer);
        initialize_clear_image(ddgi_depth_atlas_history, VK_IMAGE_LAYOUT_GENERAL, command_buffer);

        initialize_clear_image(ddgi_irradiance, VK_IMAGE_LAYOUT_GENERAL, command_buffer);
        initialize_clear_image(ddgi_irradiance_history, VK_IMAGE_LAYOUT_GENERAL, command_buffer);

        initialize_clear_image(smaa_output, VK_IMAGE_LAYOUT_GENERAL, command_buffer);
        initialize_clear_image(smaa_edges, VK_IMAGE_LAYOUT_GENERAL, command_buffer);
        initialize_clear_image(smaa_weights, VK_IMAGE_LAYOUT_GENERAL, command_buffer);

        initialize_clear_image(average_luminance_image, VK_IMAGE_LAYOUT_GENERAL, command_buffer);

        initialize_clear_image(rt_reflection_buffer, VK_IMAGE_LAYOUT_GENERAL, command_buffer);
        initialize_clear_image(rt_reflection_history, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, command_buffer);

        initialize_clear_image(directional_shadow_buffer, VK_IMAGE_LAYOUT_GENERAL, command_buffer);

        image_pipeline_barrier(
            lightpass_output,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0
        );

        image_pipeline_barrier(
            composite_output,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0
        );

        image_pipeline_barrier(
            directional_shadow_buffer_pong,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0
        );

        image_pipeline_barrier(
            ao_output,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0
        );

        image_pipeline_barrier(
            ao_output_edges,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0
        );

        image_pipeline_barrier(
            ao_output_denoised,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0
        );

        image_pipeline_barrier(
            ao_prefiltered_depth,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0
        );

        vkCmdFillBuffer(command_buffer, luminance_buffer.handle, 0, VK_WHOLE_SIZE, 0);
        buffer_pipeline_barrier(
            luminance_buffer,
            command_buffer,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            0,
            luminance_buffer.size
        );

        Framegraph ibl_graph(device, graphics_queue, command_buffer, 1, false, tracy_vk_context);
        ibl_graph.import_image(brdf_lut, VK_IMAGE_LAYOUT_GENERAL);

        Pipeline brdf_lut_pipeline = create_compute_pipeline(
            device,
            shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/brdf_lut.comp.spv"),
            {
                DescriptorLayout{
                    .bindings = {DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        .write_info = DescriptorInfo(brdf_lut.view, VK_IMAGE_LAYOUT_GENERAL)
                    }}
                },
            }
        );
        std::vector<VkDescriptorSet> lut_sets = allocate_descriptor_sets(device, descriptor_pool, brdf_lut_pipeline);

        auto lut_pass =
            ibl_graph.add_pass("lut")
                .writes_storage_image(brdf_lut, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                    vkCmdBindPipeline(command_buffer, brdf_lut_pipeline.bind_point, brdf_lut_pipeline.pipeline_handle);
                    vkCmdBindDescriptorSets(
                        command_buffer,
                        brdf_lut_pipeline.bind_point,
                        brdf_lut_pipeline.pipeline_layout,
                        0,
                        lut_sets.size(),
                        lut_sets.data(),
                        0,
                        nullptr
                    );

                    uint32_t resolution = brdf_lut.width;
                    vkCmdDispatch(
                        command_buffer,
                        (resolution + 15) / 16, // X
                        (resolution + 15) / 16, // Y
                        1
                    );

                    image_pipeline_barrier(
                        brdf_lut,
                        command_buffer,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
                    );
                });
        ibl_graph.build();
        ibl_graph.execute(command_buffer, 0);

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

        VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
        VK_CHECK(vkDeviceWaitIdle(device));

        destroy_pipeline(device, brdf_lut_pipeline);
    }

    Buffer global_vertex_buffer = create_buffer(
        1024 * 1024 * 128,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            (use_hardware_rt ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                             : 0),
        vma_allocator
    );

    Buffer global_index_buffer = create_buffer(
        1024 * 1024 * 184,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            (use_hardware_rt ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                             : 0),
        vma_allocator
    );

    uint32_t probe_count = lighting_data.probe_counts.x * lighting_data.probe_counts.y * lighting_data.probe_counts.z;
    Buffer   ddgi_ray_buffer = create_buffer(
        sizeof(DDGIRay) * probe_count * MAX_RAYS_PER_PROBE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vma_allocator
    );
    spdlog::info("DDGI Ray buffer size: {}MB", ddgi_ray_buffer.size / 1024 / 1024);

    Buffer ddgi_probe_buffer =
        create_buffer(sizeof(DDGIProbe) * probe_count, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vma_allocator);
    spdlog::info("DDGI Probe buffer size: {}MB", ddgi_probe_buffer.size / 1024 / 1024);

    Buffer indirect_command_buffer = create_buffer(
        1024 * 1024 * 12 * FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator
    );

    // NOTE: could maybe reuse the buffer above, but this isn't a big crime (i think)

    // layout - indirect command structure + tile * screen_size / 8
    Buffer indirect_dispatch_tile_copy_buffer = create_buffer(
        sizeof(VkDispatchIndirectCommand) +
            sizeof(uint32_t) * 2 * ((swapchain.width + 7) / 8) * ((swapchain.height + 7) / 8),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        vma_allocator
    );

    // layout - indirect command structure + tile * screen_size / 8
    Buffer indirect_dispatch_tile_process_buffer = create_buffer(
        sizeof(VkDispatchIndirectCommand) +
            sizeof(uint32_t) * 2 * ((swapchain.width + 7) / 8) * ((swapchain.height + 7) / 8),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        vma_allocator
    );

    Buffer drawcall_buffer = create_buffer(
        1024 * 1024 * 6 * FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    Buffer material_buffer = create_buffer(
        1024 * 1024 * 6 * FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    Buffer mesh_buffer = create_buffer(
        1024 * 1024 * 12 * FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    Buffer meshlet_buffer = create_buffer(
        1024 * 1024 * 12,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        vma_allocator
    );

    Buffer meshlet_vertex_indices_buffer = create_buffer(
        1024 * 1024 * 128,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        vma_allocator
    );

    Buffer meshlet_primitive_indices_buffer = create_buffer(
        1024 * 1024 * 128,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        vma_allocator
    );

    Buffer meshlet_bounds_buffer = create_buffer(
        1024 * 1024 * 32,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        vma_allocator
    );

    Buffer scene_ubo_buffer = create_buffer(
        aligned_size(sizeof(SceneUBO), physical_device_properties.limits.minUniformBufferOffsetAlignment) *
            FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    Buffer lighting_ubo_buffer = create_buffer(
        aligned_size(sizeof(LightingUBO), physical_device_properties.limits.minUniformBufferOffsetAlignment) *
            FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    const int particle_count           = 10000;
    Buffer    particle_position_buffer = create_buffer(
        sizeof(glm::vec3) * particle_count,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator
    );
    Buffer particle_velocity_buffer = create_buffer(
        sizeof(glm::vec3) * particle_count,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator
    );

    {
        std::vector<glm::vec3> positions(particle_count);
        std::vector<glm::vec3> velocities(particle_count);
        for (int i = 0; i < positions.size(); i++) {
            positions[i]  = glm::ballRand(20.0f);
            velocities[i] = glm::ballRand(20.0f);
        }

        copy_to_buffer(staging_buffer, vma_allocator, positions.data(), sizeof(glm::vec3) * positions.size());
        copy_buffer(
            staging_buffer,
            particle_position_buffer,
            command_buffers[0],
            graphics_queue,
            device,
            sizeof(glm::vec3) * positions.size(),
            0
        );

        copy_to_buffer(staging_buffer, vma_allocator, velocities.data(), sizeof(glm::vec3) * velocities.size());
        copy_buffer(
            staging_buffer,
            particle_velocity_buffer,
            command_buffers[0],
            graphics_queue,
            device,
            sizeof(glm::vec3) * velocities.size(),
            0
        );
    }

    DebugRendererConstants debug_renderer_constants = {};
    DebugRenderer          debug_renderer           = create_debug_renderer(
        lighting_ubo_buffer,
        linear_sampler_clamped,
        ddgi_irradiance,
        ddgi_depth_atlas,
        ddgi_probe_buffer,
        device,
        vma_allocator,
        FRAMES_IN_FLIGHT,
        depth_buffer.format,
        descriptor_pool
    );

    VkDescriptorSetLayout global_texture_descriptor_layout = create_descriptor_set_layout(
        device,
        VK_SHADER_STAGE_ALL,
        {
            {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .is_array = true},
        }
    );

    uint32_t        bindless_texture_count        = 10000;
    VkDescriptorSet global_texture_descriptor_set = VK_NULL_HANDLE;
    {
        VkDescriptorSetVariableDescriptorCountAllocateInfo variable_count_info = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
            .descriptorSetCount = 1,
            .pDescriptorCounts  = &bindless_texture_count,
        };

        VkDescriptorSetAllocateInfo global_texture_descriptor_set_info = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext              = &variable_count_info,
            .descriptorPool     = descriptor_pool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &global_texture_descriptor_layout
        };
        VK_CHECK(vkAllocateDescriptorSets(device, &global_texture_descriptor_set_info, &global_texture_descriptor_set));
    }

    spdlog::info("Initializing physics engine");
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    JPH::TempAllocatorImpl   physics_temp_allocator(10 * 1024 * 1024);
    JPH::JobSystemThreadPool physics_job_system(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1
    );
    const uint32_t physics_max_bodies             = UINT16_MAX;
    const uint32_t physics_num_body_mutexes       = 0;
    const uint32_t physica_max_body_pairs         = UINT16_MAX;
    const uint32_t physics_max_contact_contraints = 10240;

    BPLayerInterfaceImpl              physics_broad_phase_layer_interface;
    ObjectVsBroadPhaseLayerFilterImpl physics_object_vs_broad_phase_layer_filter;
    ObjectLayerPairFilterImpl         physics_object_vs_object_layer_filter;

    JPH::PhysicsSystem physics_system;
    physics_system.Init(
        physics_max_bodies,
        physics_num_body_mutexes,
        physica_max_body_pairs,
        physics_max_contact_contraints,
        physics_broad_phase_layer_interface,
        physics_object_vs_broad_phase_layer_filter,
        physics_object_vs_object_layer_filter
    );

    MyBodyActivationListener physics_body_activation_listener;
    physics_system.SetBodyActivationListener(&physics_body_activation_listener);

    MyContactListener physics_contact_listener;
    physics_system.SetContactListener(&physics_contact_listener);

    JPH::BodyInterface& physics_body_interface = physics_system.GetBodyInterface();

    const float                             player_height          = 1.8f;
    const float                             player_radius          = 0.5f;
    const float                             player_speed           = 5.0f;
    const float                             player_sprint_modifier = 1.5f;
    const float                             player_jump_velocity   = 4.0f;
    JPH::Ref<JPH::CharacterVirtualSettings> character_settings     = new JPH::CharacterVirtualSettings();
    character_settings->mShape = new JPH::CapsuleShape(player_height / 2.0f - player_radius, player_radius);

    JPH::CharacterVirtual* player_character = new JPH::CharacterVirtual(
        character_settings, JPH::RVec3(0.0, 0.0, 0.0), JPH::Quat::sIdentity(), 0, &physics_system
    );
    bool player_physics = false;

    float physics_spawn_mass        = 0.2f;
    float physics_spawn_restitution = 0.2f;
    float physics_spawn_friction    = 0.3f;
    float physics_fling_modifier    = 100.0f;

    const float physics_delta_time       = 1.0f / 60.0f;
    float       physics_time_accumulator = 0.0f;

    physics_system.OptimizeBroadPhase();

    std::string scene_load_path  = args.get_arg<std::string>("s", "data/models/room2.glb");
    std::string script_load_path = args.get_arg<std::string>("scripts", "");

    Scene       scene;
    InputSystem input_system;

    spdlog::info("Initializing script system");
    ScriptSystem script_system(scene, physics_system, input_system);

    RendererBuffers buffers = {
        .staging_buffer           = staging_buffer,
        .vertex_buffer            = global_vertex_buffer,
        .index_buffer             = global_index_buffer,
        .meshlet_buffer           = meshlet_buffer,
        .meshlet_vertex_indices   = meshlet_vertex_indices_buffer,
        .meshlet_primitive_buffer = meshlet_primitive_indices_buffer,
        .meshlet_bounds_buffer    = meshlet_bounds_buffer
    };
    BufferOffsets buffer_offsets;

    std::vector<unsigned char> compressed_texture_data;

    if (std::filesystem::is_directory(scene_load_path)) {
        VkCommandBufferAllocateInfo alloc_info = {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext              = nullptr,
            .commandPool        = command_pool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer));
        SceneSerializer::load(
            scene_load_path,
            scene,
            physics_system,
            script_system,
            buffers,
            buffer_offsets,
            compressed_texture_data,
            device,
            graphics_queue,
            vma_allocator,
            command_buffer
        );
        vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);

        script_system.load_scripts(std::filesystem::path(scene_load_path) / "scripts");
    } else {
        scene.load_scene(
            scene_load_path,
            buffers,
            buffer_offsets,
            &physics_system,
            build_lods,
            fast_scene_build,
            compress_textures,
            compressed_texture_data,
            device,
            graphics_queue,
            vma_allocator,
            command_buffers[0]
        );

        if (!script_load_path.empty()) {
            script_system.load_scripts(script_load_path);
        }
    }
    VK_CHECK(vkDeviceWaitIdle(device));

    spdlog::info(
        "Geometry buffer sizes:\n\tVertex: {}MB\n\tIndex: {}MB\n\tMeshlet: {}MB\n\tMeshlet Vertex Indices: "
        "{}MB\n\tMeshlet Primitive Indices: {}MB\n\tMeshlet Bounds: {}MB\n\tCompressed Texture Data: {}MB",
        static_cast<float>(buffer_offsets.vertex_buffer) / 1024.0f / 1024.0f,
        static_cast<float>(buffer_offsets.index_buffer) / 1024.0f / 1024.0f,
        static_cast<float>(buffer_offsets.meshlet_buffer) / 1024.0f / 1024.0f,
        static_cast<float>(buffer_offsets.meshlet_vertex_indices) / 1024.0f / 1024.0f,
        static_cast<float>(buffer_offsets.meshlet_primitive_buffer) / 1024.0f / 1024.0f,
        static_cast<float>(buffer_offsets.meshlet_bounds_buffer) / 1024.0f / 1024.0f,
        static_cast<float>(compressed_texture_data.size()) / 1024.0f / 1024.0f
    );

    // This is updated by a system at the beginning of a frame
    std::vector<MeshInstance> mesh_instances;
    std::vector<Entity>       mesh_instance_entities;
    {
        auto view = scene.entity_registry.view<components::Mesh>();
        for (auto& e : view) {
            auto& c = view.get<components::Mesh>(e);
            mesh_instances.push_back(c.mesh);
            mesh_instance_entities.push_back(e);
        }
    }

    populate_materials(scene, global_texture_descriptor_set, device);

    std::stringstream scene_state_snapshot;
    // We want to avoid clearing static bodies which are constructed from
    // the source geometry, as this would force us to either:
    // keep vertex positions and indices on the CPU at all times
    // or readback from the GPU on demand
    // either option feels unnecessary, at least for the time being, so instead
    // we never unload these bodies during save/load and reassign then back
    std::unordered_map<entt::entity, JPH::BodyID> static_body_load_map;

    RTScene rt_scene = {};

    if (use_hardware_rt) {
        spdlog::info("Setting up hardware raytracing scene");
        rt_scene = create_rt_scene(
            device,
            physical_device,
            vma_allocator,
            command_buffers[0],
            graphics_queue,
            10000,
            FRAMES_IN_FLIGHT,
            scene.meshes,
            mesh_instances,
            get_buffer_device_address(global_vertex_buffer, device),
            get_buffer_device_address(global_index_buffer, device)
        );
    }

    VkDescriptorPool imgui_descriptor_pool = imgui_init(
        window,
        instance,
        physical_device,
        device,
        swapchain.format,
        graphics_family_index,
        graphics_queue,
        FRAMES_IN_FLIGHT
    );

    std::unordered_map<uint32_t, VkDescriptorSet> imgui_material_image_handles;
    {
        uint32_t slot = 1;
        for (auto& image : scene.images) {
            imgui_material_image_handles.insert({slot++, imgui_image_handle(image.image, nearest_sampler)});
        }
    }

    Camera camera = {
        .near_plane      = 0.01f,
        .viewport_width  = static_cast<float>(swapchain.width),
        .viewport_height = static_cast<float>(swapchain.height),
        .fov             = 90.0f,
        .orientation     = {0.0f, 0.0f, 0.0f, 1.0f}
    };
    camera.position = glm::vec3(0, 0, 0);

    camera.position = {16.1, 6.3, -0.57};
    camera.orientation *= glm::angleAxis(glm::radians(-90.0f), glm::vec3(0, 1, 0));

    glm::mat4 last_frame_view_proj = glm::mat4(1.0);

    float base_camera_speed        = 10.0f;
    float camera_mouse_sensitivity = 0.003f;
    float camera_speed_mod         = 2.5f;
    float camera_speed             = base_camera_speed;

    float camera_aperture     = 8.0f;
    float camera_shutter_time = 1.0f / 60.0f;
    float camera_iso          = 100.0f;

    bool                capturing_mouse   = false;
    ImGuizmo::OPERATION tranform_gizmo_op = ImGuizmo::OPERATION::TRANSLATE;

    bool      enable_transform_snap = false;
    glm::vec3 transform_snap        = glm::vec3(1.0f);
    glm::vec2 grab_origin           = {};

    bool enable_particles = false;

    bool editor_mode = true;
    // To allow being in gameplay mode with the editor overlay
    bool                                        editor_overlay = true;
    std::map<std::string, EditorViewportSource> editor_viewport_source_handles;

    auto add_viewport_source = [&](const std::string& name, Image& image) {
        editor_viewport_source_handles.insert({name, {image, imgui_image_handle(image, linear_sampler)}});
    };

    bool simulate_lower_fps = false;
    int  simulated_fps      = 60;

    add_viewport_source("Anti-Aliased Composite", smaa_output);
    add_viewport_source("Composite", composite_output);
    add_viewport_source("GBuffer Albedo", gbuffer_albedo);
    add_viewport_source("GBuffer Normals", gbuffer_normals);
    add_viewport_source("GBuffer Velocity", gbuffer_velocity);
    add_viewport_source("GBuffer Emissive", gbuffer_emissive);
    add_viewport_source("GBuffer Depth", depth_buffer);
    add_viewport_source("Lighting", lightpass_output);
    add_viewport_source("DDGI Irradiance", ddgi_irradiance);
    add_viewport_source("DDGI Depth", ddgi_depth_atlas);
    add_viewport_source("SMAA Edges", smaa_edges);
    add_viewport_source("RT Reflection", rt_reflection_buffer);
    add_viewport_source("RT Shadows", directional_shadow_buffer);
    add_viewport_source("Bloom Buffer", bloom_buffer);

    std::string editor_viewport_source = "Anti-Aliased Composite";

    uint32_t frame_count = 0;
    uint32_t frame_index = 0;

    glm::vec4 viewport_pos_size = glm::vec4();

    auto  frame_timestamp = std::chrono::high_resolution_clock::now();
    float delta_time      = 0.0f;
    float time_passed     = 0.0f;
    float total_time      = 0.0;

    uint32_t accumulated_fps = 0;
    uint32_t fps             = 0;

    glm::mat4 frozen_view = glm::mat4(1.0f);
    float     frozen_frustum[4];

    int  min_lod         = 0;
    bool debug_frustum   = false;
    bool disable_culling = false;

    bool running = true;

    int   bloom_levels               = glm::max(5u, static_cast<uint32_t>(bloom_mip_views.size() - 5));
    float bloom_upscale_sample_scale = 2.5f;

    std::array<uint64_t, 2> pipeline_stats;

    float min_log_lum    = -4.0f;
    float max_log_lum    = 4.0f;
    float adaption_speed = 1.1f;

    uint32_t swapchain_image_index = 0;

    LuminanceConstants luminance_constants = {
        .min_log2_luminance     = min_log_lum,
        .inverse_log2_luminance = 1.0f / (max_log_lum - min_log_lum),
        .time_coef              = 0,
        .pixel_count            = static_cast<float>(lightpass_output.width * lightpass_output.height),
    };

    DescriptorLayout scene_data_layout = {
        .bindings = {
            DescriptorBinding{
                .type       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .write_info = DescriptorInfo(scene_ubo_buffer.handle, 0, scene_ubo_buffer.size / FRAMES_IN_FLIGHT)
            },
        }
    };

    DescriptorLayout draw_data_layout = {
        .bindings = {
            DescriptorBinding{
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                .write_info =
                    DescriptorInfo(indirect_command_buffer.handle, 0, indirect_command_buffer.size / FRAMES_IN_FLIGHT)
            },
            DescriptorBinding{
                .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                .write_info = DescriptorInfo(drawcall_buffer.handle, 0, drawcall_buffer.size / FRAMES_IN_FLIGHT)
            },
            DescriptorBinding{
                .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                .write_info = DescriptorInfo(mesh_buffer.handle, 0, mesh_buffer.size / FRAMES_IN_FLIGHT)
            },
            DescriptorBinding{
                .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                .write_info = DescriptorInfo(material_buffer.handle, 0, material_buffer.size / FRAMES_IN_FLIGHT)
            },
        },
    };

    DescriptorLayout geometry_data_layout = {
        .bindings = {
            DescriptorBinding{
                .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .write_info = DescriptorInfo(global_index_buffer.handle),
            },
            DescriptorBinding{
                .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .write_info = DescriptorInfo(global_vertex_buffer.handle),
            },
            DescriptorBinding{
                .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .write_info = DescriptorInfo(meshlet_buffer.handle),
            },
            DescriptorBinding{
                .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .write_info = DescriptorInfo(meshlet_bounds_buffer.handle),
            },
            DescriptorBinding{
                .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .write_info = DescriptorInfo(meshlet_vertex_indices_buffer.handle),
            },
            DescriptorBinding{
                .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .write_info = DescriptorInfo(meshlet_primitive_indices_buffer.handle),
            },
        },
    };

    // COMPUTE CULL
    Pipeline cull_pipeline = create_compute_pipeline(
        device,
        use_meshlets ? shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/cull_meshlets.comp.spv")
                     : shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/cull.comp.spv"),
        {
            scene_data_layout,
            draw_data_layout,
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(depth_sampler, depth_hiz.view, VK_IMAGE_LAYOUT_GENERAL)
                        },
                    }
            },
        },
        sizeof(CullPassPushConstants)
    );

    std::vector<VkDescriptorSet> cull_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, cull_pipeline);

    CullPassPushConstants cull_push_constants = {
        .screen_size = {depth_pyramid_width, depth_pyramid_height},
        .draw_count  = 0,
    };
    cull_push_constants.enable_lods = true;

    // GPASS
    std::vector<Shader> gpass_shaders = {
        shader_from_file(device, VK_SHADER_STAGE_FRAGMENT_BIT, "data/shaders/bindless.frag.spv"),
    };

    if (use_meshlets) {
        gpass_shaders.emplace_back(
            shader_from_file(device, VK_SHADER_STAGE_TASK_BIT_EXT, "data/shaders/meshlet.task.spv")
        );
        gpass_shaders.emplace_back(
            shader_from_file(device, VK_SHADER_STAGE_MESH_BIT_EXT, "data/shaders/meshlet.mesh.spv")
        );
    } else {
        gpass_shaders.emplace_back(
            shader_from_file(device, VK_SHADER_STAGE_VERTEX_BIT, "data/shaders/bindless.vert.spv")
        );
    }

    Pipeline gpass_pipeline = create_graphics_pipeline(
        device,
        gpass_shaders,
        {
            scene_data_layout,
            draw_data_layout,
            geometry_data_layout,
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(depth_sampler, depth_hiz.view, VK_IMAGE_LAYOUT_GENERAL)
                        },
                    }
            },

        },
        {
            gbuffer_albedo.format,
            gbuffer_normals.format,
            gbuffer_emissive.format,
            gbuffer_velocity.format,
            gbuffer_id.format,
        },
        depth_buffer.format,
        sizeof(GeometryPushConstants),
        global_texture_descriptor_layout
    );

    std::vector<VkDescriptorSet> gpass_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, gpass_pipeline);

    GeometryPushConstants gpass_push_constants = {
        .screen_size                 = {swapchain.width, swapchain.height},
        .disable_cone_cull           = false,
        .disable_small_triangle_cull = false,
    };

    // HI-Z
    Pipeline hiz_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/hi_z.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
                        DescriptorBinding{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
                    },
                .is_push_set = true
            },
        },
        sizeof(DepthReduceConstants)
    );

    // AO Prefilter Depth
    Pipeline ao_prefilter_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/ao_prefilter.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(ao_depth_mip_views[0], VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(ao_depth_mip_views[1], VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(ao_depth_mip_views[2], VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(ao_depth_mip_views[3], VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(ao_depth_mip_views[4], VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, depth_buffer.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                    },
            },
            scene_data_layout,
        },
        sizeof(XeGTAOConstants)
    );

    std::vector<VkDescriptorSet> ao_prefilter_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, ao_prefilter_pipeline);

    // AO
    Pipeline ao_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/xegtao.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(ao_output.view, VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(ao_output_edges.view, VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped,
                                ao_prefiltered_depth.view,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped, gbuffer_normals.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                nearest_sampler, hilbert_lut.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                    },
            },
            scene_data_layout,
        },
        sizeof(XeGTAOConstants)
    );

    std::vector<VkDescriptorSet> ao_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, ao_pipeline);

    // AO Denoise
    Pipeline ao_denoise_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/xegtao_denoise.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(ao_output_denoised.view, VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info =
                                DescriptorInfo(linear_sampler, ao_output.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, ao_output_edges.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                    },
                .is_push_set = true,
            },
        },
        sizeof(XeGTAOConstants)
    );

    std::vector<VkDescriptorSet> ao_denoise_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, ao_denoise_pipeline);

    // LIGHT
    Pipeline light_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/light.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(lightpass_output.view, VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, gbuffer_albedo.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, gbuffer_normals.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, depth_buffer.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                nearest_sampler, ao_output.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped, ddgi_irradiance.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped, gbuffer_emissive.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped, ddgi_depth_atlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped,
                                directional_shadow_buffer.view,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped,
                                rt_reflection_buffer.view,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped, brdf_lut.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                            .write_info = DescriptorInfo(
                                lighting_ubo_buffer.handle, 0, lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(ddgi_probe_buffer.handle)
                        },
                    }
            },
            scene_data_layout,
        }
    );
    std::vector<VkDescriptorSet> light_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, light_pipeline);

    // BLOOM DOWNSAMPLE
    Pipeline bloom_downsample_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/bloom_downsample.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
                        DescriptorBinding{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
                    },
                .is_push_set = true
            },
        },
        sizeof(BloomPushConstants)
    );

    // BLOOM UPSAMPLE
    Pipeline bloom_upsample_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/bloom_upsample.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
                        DescriptorBinding{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
                        DescriptorBinding{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
                    },
                .is_push_set = true
            },
        },
        sizeof(BloomPushConstants)
    );

    // COMPOSITE
    Pipeline composite_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/composite.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(composite_output.view, VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped, lightpass_output.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped, bloom_buffer.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            ),
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(average_luminance_image.view, VK_IMAGE_LAYOUT_GENERAL),
                        },
                    }
            },
        },
        sizeof(CompositePushConstants)
    );

    std::vector<VkDescriptorSet> composite_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, composite_pipeline);

    CompositePushConstants composite_push_constants{
        .bloom_strength        = 0.04f,
        .tonemapping_type      = 1,
        .min_ev100             = -4.0f,
        .max_ev100             = 16.0f,
        .enable_auto_exposure  = true,
        .manual_ev100          = 0.0f,
        .exposure_compensation = 0.0f,
    };

    std::vector<uint32_t> dynamic_offsets;
    dynamic_offsets.resize(static_cast<uint32_t>(DynamicOffset::COUNT));

    spdlog::info("Creating framegraph");
    Framegraph framegraph(
        device, graphics_queue, command_buffers[0], FRAMES_IN_FLIGHT, supports_timestamp_queries, tracy_vk_context
    );
    framegraph.import_image(depth_hiz, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(depth_buffer, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    framegraph.import_image(gbuffer_albedo, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    framegraph.import_image(gbuffer_normals, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    framegraph.import_image(gbuffer_emissive, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    framegraph.import_image(gbuffer_velocity, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    framegraph.import_image(gbuffer_id, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    framegraph.import_image(lightpass_output, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(composite_output, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(bloom_buffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    framegraph.import_image(ao_output, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(ao_output_edges, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(ao_output_denoised, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(ao_prefiltered_depth, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(directional_shadow_buffer, VK_IMAGE_LAYOUT_GENERAL, false);
    framegraph.import_image(directional_shadow_buffer_pong, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(smaa_edges, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(smaa_weights, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(smaa_output, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(ddgi_irradiance, VK_IMAGE_LAYOUT_GENERAL, false);
    framegraph.import_image(ddgi_irradiance_history, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(ddgi_depth_atlas, VK_IMAGE_LAYOUT_GENERAL, false);
    framegraph.import_image(ddgi_depth_atlas_history, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(average_luminance_image, VK_IMAGE_LAYOUT_GENERAL, false);
    framegraph.import_image(rt_reflection_buffer, VK_IMAGE_LAYOUT_GENERAL, false);

    auto tlas_rebuild_pass =
        framegraph.add_pass("RT structure rebuild")
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                rebuild_tlas(
                    rt_scene, device, vma_allocator, command_buffer, frame_index, scene.meshes, mesh_instances
                );
            });

    auto cull_early_pass =
        framegraph.add_pass("cull early")
            .reads_storage_image(depth_hiz, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .reads_buffer_dynamic(
                scene_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                scene_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                drawcall_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                drawcall_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                mesh_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                mesh_buffer.size / FRAMES_IN_FLIGHT
            )
            .writes_buffer_dynamic(
                indirect_command_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                indirect_command_buffer.size / FRAMES_IN_FLIGHT
            )
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                std::array<uint32_t, 5> offsets = {
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::SCENE_UBO)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::INDIRECT_COMMAND_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DRAWCALL_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MESH_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MATERIAL_BUFFER)],
                };

                vkCmdBindPipeline(command_buffer, cull_pipeline.bind_point, cull_pipeline.pipeline_handle);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    cull_pipeline.pipeline_layout,
                    0,
                    cull_descriptor_sets.size(),
                    cull_descriptor_sets.data(),
                    offsets.size(),
                    offsets.data()
                );

                cull_push_constants.draw_count = static_cast<uint32_t>(mesh_instances.size());
                vkCmdPushConstants(
                    command_buffer,
                    cull_pipeline.pipeline_layout,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    0,
                    sizeof(CullPassPushConstants),
                    &cull_push_constants
                );

                vkCmdDispatch(command_buffer, (mesh_instances.size() + 255) / 256, 1, 1);
            });

    auto gbuffer_pass =
        framegraph.add_pass("gbuffer")
            .writes_depth_attachment(depth_buffer)
            .writes_color_attachment(gbuffer_albedo)
            .writes_color_attachment(gbuffer_normals)
            .writes_color_attachment(gbuffer_emissive)
            .writes_color_attachment(gbuffer_velocity)
            .writes_color_attachment(gbuffer_id)
            .reads_buffer_dynamic(
                indirect_command_buffer,
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                indirect_command_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                scene_ubo_buffer,
                VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                scene_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                drawcall_buffer,
                VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                drawcall_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                mesh_buffer,
                VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                mesh_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                material_buffer,
                VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                material_buffer.size / FRAMES_IN_FLIGHT
            )
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                vkCmdBeginQuery(command_buffer, statistics_pools[frame_index], 0, 0);

                std::vector<VkRenderingAttachmentInfo> gbuffer_color_attachments = {
                    {
                        .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .pNext              = nullptr,
                        .imageView          = gbuffer_albedo.view,
                        .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .resolveMode        = VK_RESOLVE_MODE_NONE,
                        .resolveImageView   = VK_NULL_HANDLE,
                        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue         = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}},
                    },
                    {
                        .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .pNext              = nullptr,
                        .imageView          = gbuffer_normals.view,
                        .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .resolveMode        = VK_RESOLVE_MODE_NONE,
                        .resolveImageView   = VK_NULL_HANDLE,
                        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue         = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}},
                    },
                    {
                        .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .pNext              = nullptr,
                        .imageView          = gbuffer_emissive.view,
                        .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .resolveMode        = VK_RESOLVE_MODE_NONE,
                        .resolveImageView   = VK_NULL_HANDLE,
                        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue         = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}},
                    },
                    {
                        .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .pNext              = nullptr,
                        .imageView          = gbuffer_velocity.view,
                        .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .resolveMode        = VK_RESOLVE_MODE_NONE,
                        .resolveImageView   = VK_NULL_HANDLE,
                        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue         = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}},
                    },
                    {
                        .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .pNext              = nullptr,
                        .imageView          = gbuffer_id.view,
                        .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .resolveMode        = VK_RESOLVE_MODE_NONE,
                        .resolveImageView   = VK_NULL_HANDLE,
                        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue         = {.color = {.uint32 = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX}}},
                    },
                };

                VkRenderingAttachmentInfo depth_attachment_info = {
                    .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .pNext              = nullptr,
                    .imageView          = depth_buffer.view,
                    .imageLayout        = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    .resolveMode        = VK_RESOLVE_MODE_NONE,
                    .resolveImageView   = VK_NULL_HANDLE,
                    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue         = {.depthStencil = {.depth = 0.0f, .stencil = 0}}
                };

                VkRenderingInfo rendering_info = {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .renderArea =
                        {
                            .offset = {.x = 0, .y = 0},
                            .extent = {.width = swapchain.width, .height = swapchain.height},
                        },
                    .layerCount           = 1,
                    .viewMask             = 0,
                    .colorAttachmentCount = static_cast<uint32_t>(gbuffer_color_attachments.size()),
                    .pColorAttachments    = gbuffer_color_attachments.data(),
                    .pDepthAttachment     = &depth_attachment_info,
                    .pStencilAttachment   = nullptr
                };

                vkCmdBeginRendering(command_buffer, &rendering_info);
                vkCmdBindPipeline(command_buffer, gpass_pipeline.bind_point, gpass_pipeline.pipeline_handle);

                vkCmdPushConstants(
                    command_buffer,
                    gpass_pipeline.pipeline_layout,
                    gpass_pipeline.stage_flags,
                    0,
                    sizeof(GeometryPushConstants),
                    &gpass_push_constants
                );

                std::array<VkDescriptorSet, 5> sets = {
                    gpass_descriptor_sets[0],
                    gpass_descriptor_sets[1],
                    gpass_descriptor_sets[2],
                    gpass_descriptor_sets[3],
                    global_texture_descriptor_set,
                };

                std::array<uint32_t, 5> offsets = {
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::SCENE_UBO)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::INDIRECT_COMMAND_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DRAWCALL_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MESH_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MATERIAL_BUFFER)],
                };

                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    gpass_pipeline.pipeline_layout,
                    0,
                    sets.size(),
                    sets.data(),
                    offsets.size(),
                    offsets.data()
                );

                if (use_meshlets) {
                    vkCmdDrawMeshTasksIndirectCountEXT(
                        command_buffer,
                        indirect_command_buffer.handle,
                        offsets[1] + sizeof(uint32_t),
                        indirect_command_buffer.handle,
                        offsets[1],
                        mesh_instances.size(),
                        sizeof(MeshIndirectDrawCommand)
                    );
                } else {
                    vkCmdBindIndexBuffer(command_buffer, global_index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);

                    vkCmdDrawIndexedIndirectCount(
                        command_buffer,
                        indirect_command_buffer.handle,
                        offsets[1] + sizeof(uint32_t),
                        indirect_command_buffer.handle,
                        offsets[1],
                        mesh_instances.size(),
                        sizeof(VkDrawIndexedIndirectCommand) + sizeof(uint32_t)
                    );
                }

                vkCmdEndRendering(command_buffer);
                vkCmdEndQuery(command_buffer, statistics_pools[frame_index], 0);
            });

    auto depth_pyramid_pass = framegraph.add_pass("depth pyramid")
                                  .writes_image(
                                      depth_hiz,
                                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                      VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                                      VK_IMAGE_LAYOUT_GENERAL
                                  )
                                  .reads_image(
                                      depth_buffer,
                                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                      VK_ACCESS_2_SHADER_READ_BIT,
                                      VK_IMAGE_LAYOUT_GENERAL
                                  )
                                  .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                                      VkPipelineBindPoint bind_point      = hiz_pipeline.bind_point;
                                      VkPipeline          pipeline        = hiz_pipeline.pipeline_handle;
                                      VkPipelineLayout    pipeline_layout = hiz_pipeline.pipeline_layout;

                                      vkCmdBindPipeline(command_buffer, bind_point, pipeline);
                                      for (int i = 0; i < depth_hiz.levels; ++i) {
                                          uint32_t mip_width  = glm::max(1u, depth_hiz.width >> i);
                                          uint32_t mip_height = glm::max(1u, depth_hiz.height >> i);

                                          DepthReduceConstants constants = {
                                              .size = {mip_width, mip_height},
                                          };

                                          VkDescriptorImageInfo image_read_info = {
                                              .sampler     = depth_sampler,
                                              .imageView   = i == 0 ? depth_buffer.view : depth_mip_views[i - 1],
                                              .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                          };

                                          VkDescriptorImageInfo image_write_info = {
                                              .sampler     = VK_NULL_HANDLE,
                                              .imageView   = depth_mip_views[i],
                                              .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                          };

                                          std::vector<VkWriteDescriptorSet> write_sets = {
                                              {
                                                  .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                  .pNext            = nullptr,
                                                  .dstBinding       = 0,
                                                  .dstArrayElement  = 0,
                                                  .descriptorCount  = 1,
                                                  .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                  .pImageInfo       = &image_write_info,
                                                  .pBufferInfo      = nullptr,
                                                  .pTexelBufferView = nullptr,
                                              },
                                              {
                                                  .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                  .pNext            = nullptr,
                                                  .dstBinding       = 1,
                                                  .dstArrayElement  = 0,
                                                  .descriptorCount  = 1,
                                                  .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                  .pImageInfo       = &image_read_info,
                                                  .pBufferInfo      = nullptr,
                                                  .pTexelBufferView = nullptr,
                                              },
                                          };

                                          vkCmdPushDescriptorSet(
                                              command_buffer,
                                              VK_PIPELINE_BIND_POINT_COMPUTE,
                                              pipeline_layout,
                                              0,
                                              static_cast<uint32_t>(write_sets.size()),
                                              write_sets.data()
                                          );

                                          vkCmdPushConstants(
                                              command_buffer,
                                              pipeline_layout,
                                              VK_SHADER_STAGE_COMPUTE_BIT,
                                              0,
                                              sizeof(DepthReduceConstants),
                                              &constants
                                          );

                                          uint32_t groups_x = (mip_width + 7) / 8;
                                          uint32_t groups_y = (mip_height + 7) / 8;
                                          vkCmdDispatch(command_buffer, groups_x, groups_y, 1);

                                          image_pipeline_barrier(
                                              depth_hiz.handle,
                                              command_buffer,
                                              VK_IMAGE_LAYOUT_GENERAL,
                                              VK_IMAGE_LAYOUT_GENERAL,
                                              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                              VK_ACCESS_2_SHADER_WRITE_BIT,
                                              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                              {
                                                  .aspectMask     = depth_hiz.aspect,
                                                  .baseMipLevel   = static_cast<uint32_t>(i),
                                                  .levelCount     = 1,
                                                  .baseArrayLayer = 0,
                                                  .layerCount     = 1,
                                              }
                                          );
                                      }
                                  });

    XeGTAOConstants xegtao_constants = {};

    auto ao_prefilter_pass =
        framegraph.add_pass("ao prefilter depth")
            .reads_buffer_dynamic(
                scene_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                scene_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .samples_image(depth_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(ao_prefiltered_depth, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                vkCmdBindPipeline(
                    command_buffer, ao_prefilter_pipeline.bind_point, ao_prefilter_pipeline.pipeline_handle
                );
                vkCmdBindDescriptorSets(
                    command_buffer,
                    ao_prefilter_pipeline.bind_point,
                    ao_prefilter_pipeline.pipeline_layout,
                    0,
                    ao_prefilter_descriptor_sets.size(),
                    ao_prefilter_descriptor_sets.data(),
                    1,
                    &dynamic_offsets[static_cast<uint32_t>(DynamicOffset::SCENE_UBO)]
                );

                vkCmdPushConstants(
                    command_buffer,
                    ao_prefilter_pipeline.pipeline_layout,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    0,
                    sizeof(XeGTAOConstants),
                    &xegtao_constants
                );

                vkCmdDispatch(command_buffer, (swapchain.width + 7) / 8, (swapchain.height + 7) / 8, 1);
            });

    auto ao_pass = framegraph.add_pass("ao")
                       .reads_buffer_dynamic(
                           scene_ubo_buffer,
                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_ACCESS_2_UNIFORM_READ_BIT,
                           scene_ubo_buffer.size / FRAMES_IN_FLIGHT
                       )
                       .samples_image(ao_prefiltered_depth, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                       .samples_image(gbuffer_normals, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                       .writes_storage_image(ao_output, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                       .writes_storage_image(ao_output_edges, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                       .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                           vkCmdBindPipeline(command_buffer, ao_pipeline.bind_point, ao_pipeline.pipeline_handle);
                           vkCmdBindDescriptorSets(
                               command_buffer,
                               ao_pipeline.bind_point,
                               ao_pipeline.pipeline_layout,
                               0,
                               ao_descriptor_sets.size(),
                               ao_descriptor_sets.data(),
                               1,
                               &dynamic_offsets[static_cast<uint32_t>(DynamicOffset::SCENE_UBO)]
                           );

                           vkCmdPushConstants(
                               command_buffer,
                               ao_pipeline.pipeline_layout,
                               VK_SHADER_STAGE_COMPUTE_BIT,
                               0,
                               sizeof(XeGTAOConstants),
                               &xegtao_constants
                           );

                           vkCmdDispatch(command_buffer, (swapchain.width + 7) / 8, (swapchain.height + 7) / 8, 1);
                       });

    auto ao_denoise_pass =
        framegraph.add_pass("ao denoise")
            .reads_buffer_dynamic(
                scene_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                scene_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .samples_image(ao_output, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(ao_output_edges, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(ao_output_denoised, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                vkCmdBindPipeline(command_buffer, ao_denoise_pipeline.bind_point, ao_denoise_pipeline.pipeline_handle);

                int denoise_passes = 2;

                Image read_image  = ao_output;
                Image write_image = ao_output_denoised;

                for (int i = 0; i < denoise_passes; i++) {
                    xegtao_constants.final_pass = i == denoise_passes - 1;

                    VkDescriptorImageInfo image_read_info = {
                        .sampler     = nearest_sampler,
                        .imageView   = read_image.view,
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    };

                    VkDescriptorImageInfo edges_info = {
                        .sampler     = nearest_sampler,
                        .imageView   = ao_output_edges.view,
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    };

                    VkDescriptorImageInfo image_write_info = {
                        .sampler     = VK_NULL_HANDLE,
                        .imageView   = write_image.view,
                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    };

                    std::vector<VkWriteDescriptorSet> write_sets = {
                        {
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstBinding       = 0,
                            .dstArrayElement  = 0,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .pImageInfo       = &image_write_info,
                            .pBufferInfo      = nullptr,
                            .pTexelBufferView = nullptr,
                        },
                        {
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstBinding       = 1,
                            .dstArrayElement  = 0,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .pImageInfo       = &image_read_info,
                            .pBufferInfo      = nullptr,
                            .pTexelBufferView = nullptr,
                        },
                        {
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstBinding       = 2,
                            .dstArrayElement  = 0,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .pImageInfo       = &edges_info,
                            .pBufferInfo      = nullptr,
                            .pTexelBufferView = nullptr,
                        },
                    };

                    vkCmdPushDescriptorSet(
                        command_buffer,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        ao_denoise_pipeline.pipeline_layout,
                        0,
                        static_cast<uint32_t>(write_sets.size()),
                        write_sets.data()
                    );

                    vkCmdPushConstants(
                        command_buffer,
                        ao_denoise_pipeline.pipeline_layout,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        0,
                        sizeof(XeGTAOConstants),
                        &xegtao_constants
                    );

                    vkCmdDispatch(command_buffer, (swapchain.width + 15) / 16, (swapchain.height + 7) / 8, 1);

                    image_pipeline_barrier(
                        write_image,
                        command_buffer,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
                    );

                    image_pipeline_barrier(
                        read_image,
                        command_buffer,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
                    );

                    auto temp = read_image;

                    read_image  = write_image;
                    write_image = temp;
                }
            });

    Pipeline ddgi_ray_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/ddgi_trace_ray.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                            .write_info = DescriptorInfo(rt_scene.tlas.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                            .write_info = DescriptorInfo(
                                lighting_ubo_buffer.handle, 0, lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(ddgi_ray_buffer.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(ddgi_probe_buffer.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped,
                                ddgi_irradiance_history.view,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped,
                                ddgi_depth_atlas_history.view,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                    }
            },
            draw_data_layout,
            geometry_data_layout,
        },
        0,
        global_texture_descriptor_layout
    );
    std::vector<VkDescriptorSet> ddgi_ray_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, ddgi_ray_pipeline);

    auto& ddgi_ray_pass =
        framegraph.add_pass("ddgi ray")
            .reads_buffer_dynamic(
                lighting_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .writes_buffer(
                ddgi_ray_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            )
            .reads_buffer(ddgi_probe_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT)
            .reads_buffer_dynamic(
                drawcall_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                drawcall_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                mesh_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                mesh_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                material_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                material_buffer.size / FRAMES_IN_FLIGHT
            )
            .samples_image(ddgi_irradiance_history, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(ddgi_depth_atlas_history, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = ddgi_ray_pipeline;

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);

                std::array<VkDescriptorSet, 4> sets = {
                    ddgi_ray_descriptor_sets[0],
                    ddgi_ray_descriptor_sets[1],
                    ddgi_ray_descriptor_sets[2],
                    global_texture_descriptor_set,
                };

                std::array<uint32_t, 5> offsets = {
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::INDIRECT_COMMAND_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DRAWCALL_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MESH_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MATERIAL_BUFFER)],
                };

                vkCmdBindDescriptorSets(
                    command_buffer,
                    pipeline.bind_point,
                    pipeline.pipeline_layout,
                    0,
                    sets.size(),
                    sets.data(),
                    offsets.size(),
                    offsets.data()
                );

                uint32_t probe_count =
                    lighting_data.probe_counts.x * lighting_data.probe_counts.y * lighting_data.probe_counts.z;

                vkCmdDispatch(command_buffer, glm::ceil((probe_count * lighting_data.rays_per_probe) / 32.0f), 1, 1);
            });

    Pipeline ddgi_probe_blend_depth_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/ddgi_blend_depth.comp.spv"),
        {
            DescriptorLayout{
                .bindings = {
                    DescriptorBinding{
                        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .write_info = DescriptorInfo(ddgi_ray_buffer.handle)
                    },
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .write_info = DescriptorInfo(ddgi_probe_buffer.handle)
                    },
                    DescriptorBinding{
                        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                        .write_info =
                            DescriptorInfo(lighting_ubo_buffer.handle, 0, lighting_ubo_buffer.size / FRAMES_IN_FLIGHT)
                    },
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        .write_info = DescriptorInfo(ddgi_depth_atlas.view, VK_IMAGE_LAYOUT_GENERAL)
                    },
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        .write_info = DescriptorInfo(ddgi_depth_atlas_history.view, VK_IMAGE_LAYOUT_GENERAL)
                    },
                },
            },
        }
    );
    std::vector<VkDescriptorSet> ddgi_probe_blend_depth_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, ddgi_probe_blend_depth_pipeline);

    auto& ddgi_probe_blend_depth_pass =
        framegraph.add_pass("ddgi blend depth")
            .reads_buffer(ddgi_ray_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT)
            .writes_buffer(
                ddgi_probe_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            )
            .reads_buffer_dynamic(
                lighting_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .writes_storage_image(ddgi_depth_atlas, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .reads_storage_image(ddgi_depth_atlas_history, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = ddgi_probe_blend_depth_pipeline;

                std::array<uint32_t, 1> offsets = {dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)]};

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.pipeline_layout,
                    0,
                    ddgi_probe_blend_depth_descriptor_sets.size(),
                    ddgi_probe_blend_depth_descriptor_sets.data(),
                    offsets.size(),
                    offsets.data()
                );

                vkCmdDispatch(
                    command_buffer,
                    lighting_data.probe_counts.x,
                    lighting_data.probe_counts.y,
                    lighting_data.probe_counts.z
                );

                image_pipeline_barrier(
                    ddgi_depth_atlas,
                    command_buffer,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT
                );

                image_pipeline_barrier(
                    ddgi_depth_atlas_history,
                    command_buffer,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT
                );

                VkImageBlit blit_region = {
                    .srcSubresource =
                        {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
                    .srcOffsets =
                        {
                            {0, 0, 0},
                            {
                                static_cast<int32_t>(ddgi_depth_atlas.width),
                                static_cast<int32_t>(ddgi_depth_atlas.height),
                                1,
                            },
                        },
                    .dstSubresource =
                        {
                            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                            .mipLevel       = 0,
                            .baseArrayLayer = 0,
                            .layerCount     = 1,
                        },
                    .dstOffsets = {
                        {0, 0, 0},
                        {
                            static_cast<int32_t>(ddgi_depth_atlas_history.width),
                            static_cast<int32_t>(ddgi_depth_atlas_history.height),
                            1,
                        },
                    },
                };

                vkCmdBlitImage(
                    command_buffer,
                    ddgi_depth_atlas.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    ddgi_depth_atlas_history.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &blit_region,
                    VK_FILTER_LINEAR
                );

                image_pipeline_barrier(
                    ddgi_depth_atlas,
                    command_buffer,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                );

                image_pipeline_barrier(
                    ddgi_depth_atlas_history,
                    command_buffer,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                );
            });

    Pipeline ddgi_probe_blend_irradiance_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/ddgi_blend_irradiance.comp.spv"),
        {
            DescriptorLayout{
                .bindings = {
                    DescriptorBinding{
                        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .write_info = DescriptorInfo(ddgi_ray_buffer.handle)
                    },
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .write_info = DescriptorInfo(ddgi_probe_buffer.handle)
                    },
                    DescriptorBinding{
                        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                        .write_info =
                            DescriptorInfo(lighting_ubo_buffer.handle, 0, lighting_ubo_buffer.size / FRAMES_IN_FLIGHT)
                    },
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        .write_info = DescriptorInfo(ddgi_irradiance.view, VK_IMAGE_LAYOUT_GENERAL)
                    },
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        .write_info = DescriptorInfo(ddgi_irradiance_history.view, VK_IMAGE_LAYOUT_GENERAL)
                    },
                },
            },
        }
    );
    std::vector<VkDescriptorSet> ddgi_probe_blend_irradiance_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, ddgi_probe_blend_irradiance_pipeline);

    auto& ddgi_probe_blend_irradiance_pass =
        framegraph.add_pass("ddgi blend irradiance")
            .reads_buffer(ddgi_ray_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT)
            .writes_buffer(
                ddgi_probe_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            )
            .reads_buffer_dynamic(
                lighting_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .writes_storage_image(ddgi_irradiance, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .reads_storage_image(ddgi_irradiance_history, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = ddgi_probe_blend_irradiance_pipeline;

                std::array<uint32_t, 1> offsets = {dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)]};

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.pipeline_layout,
                    0,
                    ddgi_probe_blend_irradiance_descriptor_sets.size(),
                    ddgi_probe_blend_irradiance_descriptor_sets.data(),
                    offsets.size(),
                    offsets.data()
                );

                vkCmdDispatch(
                    command_buffer,
                    lighting_data.probe_counts.x,
                    lighting_data.probe_counts.y,
                    lighting_data.probe_counts.z
                );

                image_pipeline_barrier(
                    ddgi_irradiance,
                    command_buffer,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT
                );

                image_pipeline_barrier(
                    ddgi_irradiance_history,
                    command_buffer,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT
                );

                VkImageBlit blit_region = {
                    .srcSubresource =
                        {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
                    .srcOffsets =
                        {
                            {0, 0, 0},
                            {
                                static_cast<int32_t>(ddgi_irradiance.width),
                                static_cast<int32_t>(ddgi_irradiance.height),
                                1,
                            },
                        },
                    .dstSubresource =
                        {
                            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                            .mipLevel       = 0,
                            .baseArrayLayer = 0,
                            .layerCount     = 1,
                        },
                    .dstOffsets = {
                        {0, 0, 0},
                        {
                            static_cast<int32_t>(ddgi_irradiance_history.width),
                            static_cast<int32_t>(ddgi_irradiance_history.height),
                            1,
                        },
                    },
                };

                vkCmdBlitImage(
                    command_buffer,
                    ddgi_irradiance.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    ddgi_irradiance_history.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &blit_region,
                    VK_FILTER_LINEAR
                );

                image_pipeline_barrier(
                    ddgi_irradiance,
                    command_buffer,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                );

                image_pipeline_barrier(
                    ddgi_irradiance_history,
                    command_buffer,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                );
            });

    Pipeline ddgi_probe_relocate_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/ddgi_relocate.comp.spv"),
        {
            DescriptorLayout{
                .bindings = {
                    DescriptorBinding{
                        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .write_info = DescriptorInfo(ddgi_ray_buffer.handle)
                    },
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .write_info = DescriptorInfo(ddgi_probe_buffer.handle)
                    },
                    DescriptorBinding{
                        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                        .write_info =
                            DescriptorInfo(lighting_ubo_buffer.handle, 0, lighting_ubo_buffer.size / FRAMES_IN_FLIGHT)
                    },
                },
            },
        }
    );
    std::vector<VkDescriptorSet> ddgi_probe_relocate_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, ddgi_probe_relocate_pipeline);

    auto& ddgi_probe_relocate_pass =
        framegraph.add_pass("ddgi probe relocate")
            .reads_buffer(ddgi_ray_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT)
            .reads_buffer(
                ddgi_probe_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            )
            .writes_buffer(
                ddgi_probe_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            )
            .reads_buffer_dynamic(
                lighting_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = ddgi_probe_relocate_pipeline;

                std::array<uint32_t, 1> offsets = {dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)]};

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.pipeline_layout,
                    0,
                    ddgi_probe_relocate_descriptor_sets.size(),
                    ddgi_probe_relocate_descriptor_sets.data(),
                    offsets.size(),
                    offsets.data()
                );

                uint32_t probe_count =
                    lighting_data.probe_counts.x * lighting_data.probe_counts.y * lighting_data.probe_counts.z;

                vkCmdDispatch(command_buffer, (probe_count + 31) / 32, 1, 1);
            });

    Pipeline ddgi_probe_classify_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/ddgi_classify.comp.spv"),
        {
            DescriptorLayout{
                .bindings = {
                    DescriptorBinding{
                        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .write_info = DescriptorInfo(ddgi_ray_buffer.handle)
                    },
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .write_info = DescriptorInfo(ddgi_probe_buffer.handle)
                    },
                    DescriptorBinding{
                        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                        .write_info =
                            DescriptorInfo(lighting_ubo_buffer.handle, 0, lighting_ubo_buffer.size / FRAMES_IN_FLIGHT)
                    },
                },
            },
        }
    );
    std::vector<VkDescriptorSet> ddgi_probe_classify_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, ddgi_probe_classify_pipeline);

    auto& ddgi_probe_classify_pass =
        framegraph.add_pass("ddgi probe classify")
            .reads_buffer(ddgi_ray_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT)
            .writes_buffer(
                ddgi_probe_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            )
            .reads_buffer_dynamic(
                lighting_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = ddgi_probe_classify_pipeline;

                std::array<uint32_t, 1> offsets = {dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)]};

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.pipeline_layout,
                    0,
                    ddgi_probe_classify_descriptor_sets.size(),
                    ddgi_probe_classify_descriptor_sets.data(),
                    offsets.size(),
                    offsets.data()
                );

                uint32_t probe_count =
                    lighting_data.probe_counts.x * lighting_data.probe_counts.y * lighting_data.probe_counts.z;

                vkCmdDispatch(command_buffer, (probe_count + 31) / 32, 1, 1);
            });

    Pipeline tile_clear_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/dispatch_clear.comp.spv"),
        {
            DescriptorLayout{
                .bindings = {
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .write_info = DescriptorInfo(indirect_dispatch_tile_process_buffer.handle)
                    },
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .write_info = DescriptorInfo(indirect_dispatch_tile_copy_buffer.handle)
                    },
                }
            },
        }
    );

    std::vector<VkDescriptorSet> tile_clear_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, tile_clear_pipeline);

    auto& tile_clear_pass = framegraph.add_pass("Tile Clear")
                                .writes_buffer(
                                    indirect_dispatch_tile_process_buffer,
                                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                                )
                                .writes_buffer(
                                    indirect_dispatch_tile_copy_buffer,
                                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                                )
                                .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                                    const Pipeline& pipeline = tile_clear_pipeline;

                                    vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);

                                    vkCmdBindDescriptorSets(
                                        command_buffer,
                                        VK_PIPELINE_BIND_POINT_COMPUTE,
                                        pipeline.pipeline_layout,
                                        0,
                                        tile_clear_descriptor_sets.size(),
                                        tile_clear_descriptor_sets.data(),
                                        0,
                                        nullptr
                                    );

                                    vkCmdDispatch(command_buffer, 1, 1, 1);
                                });

    Pipeline rt_reflection_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/rt_reflection.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(rt_reflection_views[1], VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, gbuffer_albedo.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, gbuffer_normals.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, depth_buffer.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, ddgi_irradiance.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, ddgi_depth_atlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                nearest_sampler, hilbert_lut.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info =
                                DescriptorInfo(linear_sampler, brdf_lut.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                            .write_info = DescriptorInfo(rt_scene.tlas.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                            .write_info = DescriptorInfo(
                                lighting_ubo_buffer.handle, 0, lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(ddgi_probe_buffer.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(indirect_dispatch_tile_process_buffer.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(indirect_dispatch_tile_copy_buffer.handle)
                        },
                    }
            },
            scene_data_layout,
            draw_data_layout,
            geometry_data_layout,
        },
        0,
        global_texture_descriptor_layout
    );
    std::vector<VkDescriptorSet> rt_reflection_pass_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, rt_reflection_pipeline);

    auto& rt_reflection_pass =
        framegraph.add_pass("RT Reflection")
            .reads_buffer_dynamic(
                scene_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                scene_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                lighting_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                drawcall_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                drawcall_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer(ddgi_probe_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT)
            .reads_buffer_dynamic(
                mesh_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                mesh_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                material_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                material_buffer.size / FRAMES_IN_FLIGHT
            )
            .writes_buffer(
                indirect_dispatch_tile_process_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_WRITE_BIT
            )
            .writes_buffer(
                indirect_dispatch_tile_copy_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT
            )
            .samples_image(depth_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(gbuffer_albedo, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(gbuffer_normals, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(ddgi_irradiance, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(ddgi_depth_atlas, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(rt_reflection_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = rt_reflection_pipeline;

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);

                std::array<VkDescriptorSet, 5> sets = {
                    rt_reflection_pass_descriptor_sets[0],
                    rt_reflection_pass_descriptor_sets[1],
                    rt_reflection_pass_descriptor_sets[2],
                    rt_reflection_pass_descriptor_sets[3],
                    global_texture_descriptor_set,
                };

                std::array<uint32_t, 6> offsets = {
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::SCENE_UBO)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::INDIRECT_COMMAND_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DRAWCALL_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MESH_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MATERIAL_BUFFER)],
                };

                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.pipeline_layout,
                    0,
                    sets.size(),
                    sets.data(),
                    offsets.size(),
                    offsets.data()
                );

                vkCmdDispatch(
                    command_buffer,
                    ((rt_reflection_buffer.width / 2) + 7) / 8,
                    ((rt_reflection_buffer.height / 2) + 7) / 8,
                    1
                );
            });

    Pipeline reflection_tile_copy_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/tile_copy.comp.spv"),
        {
            DescriptorLayout{
                .bindings = {
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        .write_info = DescriptorInfo(rt_reflection_views[0], VK_IMAGE_LAYOUT_GENERAL)
                    },
                    DescriptorBinding{
                        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .write_info =
                            DescriptorInfo(linear_sampler_clamped, rt_reflection_views[1], VK_IMAGE_LAYOUT_GENERAL)
                    },
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .write_info = DescriptorInfo(indirect_dispatch_tile_copy_buffer.handle)
                    },
                }
            },
        }
    );

    std::vector<VkDescriptorSet> reflection_tile_copy_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, reflection_tile_copy_pipeline);

    auto& reflection_tile_copy_pass =
        framegraph.add_pass("Reflection Tile Copy")
            .reads_buffer(
                indirect_dispatch_tile_copy_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            )
            .writes_storage_image(rt_reflection_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = reflection_tile_copy_pipeline;

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);

                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.pipeline_layout,
                    0,
                    reflection_tile_copy_descriptor_sets.size(),
                    reflection_tile_copy_descriptor_sets.data(),
                    0,
                    nullptr
                );
                vkCmdDispatchIndirect(command_buffer, indirect_dispatch_tile_copy_buffer.handle, 0);
            });

    Pipeline rt_reflection_upsample = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/rt_upsample.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(rt_reflection_views[0], VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info =
                                DescriptorInfo(linear_sampler_clamped, rt_reflection_views[1], VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped, gbuffer_normals.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped,
                                rt_reflection_history.view,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped, depth_buffer.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(indirect_dispatch_tile_process_buffer.handle)
                        },
                    }
            },
            scene_data_layout,
        },
        sizeof(GlossyRTConstants)
    );

    std::vector<VkDescriptorSet> rt_upsample_pass_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, rt_reflection_upsample);

    auto& rt_upsample_pass =
        framegraph.add_pass("RT Upsample")
            .reads_buffer_dynamic(
                scene_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                scene_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer(
                indirect_dispatch_tile_process_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            )
            .samples_image(gbuffer_normals, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(rt_reflection_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = rt_reflection_upsample;

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);

                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.pipeline_layout,
                    0,
                    rt_upsample_pass_descriptor_sets.size(),
                    rt_upsample_pass_descriptor_sets.data(),
                    1,
                    &dynamic_offsets[static_cast<uint32_t>(DynamicOffset::SCENE_UBO)]
                );

                GlossyRTConstants constants = {
                    .last_frame_view_proj = last_frame_view_proj, .frame_index = frame_count
                };

                vkCmdPushConstants(
                    command_buffer,
                    pipeline.pipeline_layout,
                    pipeline.stage_flags,
                    0,
                    sizeof(GlossyRTConstants),
                    &constants
                );

                vkCmdDispatchIndirect(command_buffer, indirect_dispatch_tile_process_buffer.handle, 0);

                image_pipeline_barrier(
                    rt_reflection_buffer,
                    command_buffer,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT
                );

                image_pipeline_barrier(
                    rt_reflection_history,
                    command_buffer,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT
                );

                VkImageBlit blit_region = {
                    .srcSubresource =
                        {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
                    .srcOffsets =
                        {
                            {0, 0, 0},
                            {
                                static_cast<int32_t>(rt_reflection_buffer.width),
                                static_cast<int32_t>(rt_reflection_buffer.height),
                                1,
                            },
                        },
                    .dstSubresource =
                        {
                            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                            .mipLevel       = 0,
                            .baseArrayLayer = 0,
                            .layerCount     = 1,
                        },
                    .dstOffsets = {
                        {0, 0, 0},
                        {
                            static_cast<int32_t>(rt_reflection_history.width),
                            static_cast<int32_t>(rt_reflection_history.height),
                            1,
                        },
                    },
                };

                vkCmdBlitImage(
                    command_buffer,
                    rt_reflection_buffer.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    rt_reflection_history.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &blit_region,
                    VK_FILTER_LINEAR
                );

                image_pipeline_barrier(
                    rt_reflection_buffer,
                    command_buffer,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                );

                image_pipeline_barrier(
                    rt_reflection_history,
                    command_buffer,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                );
            });

    Pipeline shadow_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/shadow.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(directional_shadow_buffer.view, VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, gbuffer_normals.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, depth_buffer.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                            .write_info = DescriptorInfo(rt_scene.tlas.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                            .write_info = DescriptorInfo(
                                lighting_ubo_buffer.handle, 0, lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
                            )
                        },
                    }
            },
            scene_data_layout,
            draw_data_layout,
            geometry_data_layout,
        },
        0,
        global_texture_descriptor_layout
    );
    std::vector<VkDescriptorSet> shadow_pass_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, shadow_pipeline);

    auto& shadow_pass = framegraph.add_pass("RT Shadows")
                            .reads_buffer_dynamic(
                                scene_ubo_buffer,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                VK_ACCESS_2_UNIFORM_READ_BIT,
                                scene_ubo_buffer.size / FRAMES_IN_FLIGHT
                            )
                            .reads_buffer_dynamic(
                                lighting_ubo_buffer,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                VK_ACCESS_2_UNIFORM_READ_BIT,
                                lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
                            )
                            .reads_buffer_dynamic(
                                drawcall_buffer,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                VK_ACCESS_2_SHADER_READ_BIT,
                                drawcall_buffer.size / FRAMES_IN_FLIGHT
                            )
                            .reads_buffer_dynamic(
                                mesh_buffer,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                VK_ACCESS_2_SHADER_READ_BIT,
                                mesh_buffer.size / FRAMES_IN_FLIGHT
                            )
                            .reads_buffer_dynamic(
                                material_buffer,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                VK_ACCESS_2_SHADER_READ_BIT,
                                material_buffer.size / FRAMES_IN_FLIGHT
                            )
                            .samples_image(depth_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                            .samples_image(gbuffer_normals, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                            .writes_storage_image(directional_shadow_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                                const Pipeline& pipeline = shadow_pipeline;

                                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);

                                std::array<VkDescriptorSet, 5> sets = {
                                    shadow_pass_descriptor_sets[0],
                                    shadow_pass_descriptor_sets[1],
                                    shadow_pass_descriptor_sets[2],
                                    shadow_pass_descriptor_sets[3],
                                    global_texture_descriptor_set,
                                };

                                std::array<uint32_t, 6> offsets = {
                                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)],
                                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::SCENE_UBO)],
                                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::INDIRECT_COMMAND_BUFFER)],
                                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DRAWCALL_BUFFER)],
                                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MESH_BUFFER)],
                                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MATERIAL_BUFFER)],
                                };

                                vkCmdBindDescriptorSets(
                                    command_buffer,
                                    VK_PIPELINE_BIND_POINT_COMPUTE,
                                    pipeline.pipeline_layout,
                                    0,
                                    sets.size(),
                                    sets.data(),
                                    offsets.size(),
                                    offsets.data()
                                );

                                vkCmdDispatch(
                                    command_buffer,
                                    (((directional_shadow_buffer.width + 1) / 2) + 7) / 8,
                                    (directional_shadow_buffer.height + 7) / 8,
                                    1
                                );
                            });

    Pipeline shadow_fill_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/shadow_fill.comp.spv"),
        {
            DescriptorLayout{
                .bindings = {
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        .write_info = DescriptorInfo(directional_shadow_buffer.view, VK_IMAGE_LAYOUT_GENERAL)
                    },
                    DescriptorBinding{
                        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .write_info =
                            DescriptorInfo(linear_sampler, depth_buffer.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                    },
                }
            },
        }
    );
    std::vector<VkDescriptorSet> shadow_fill_pass_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, shadow_fill_pipeline);

    auto& shadow_fill_pass =
        framegraph.add_pass("RT Shadow fill")
            .samples_image(gbuffer_normals, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(directional_shadow_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = shadow_fill_pipeline;

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);

                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.pipeline_layout,
                    0,
                    shadow_fill_pass_descriptor_sets.size(),
                    shadow_fill_pass_descriptor_sets.data(),
                    0,
                    nullptr
                );

                vkCmdDispatch(
                    command_buffer,
                    (((directional_shadow_buffer.width + 1) / 2) + 7) / 8,
                    (directional_shadow_buffer.height + 7) / 8,
                    1
                );
            });

    Pipeline shadow_blur_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/shadow_blur.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(directional_shadow_buffer_pong.view, VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info =
                                DescriptorInfo(linear_sampler, directional_shadow_buffer.view, VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, depth_buffer.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                    },
                .is_push_set = true
            },
        },
        sizeof(ShadowBlurConstants)
    );

    auto& shadow_blur_pass =
        framegraph.add_pass("RT Shadow blur")
            .samples_image(depth_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .reads_storage_image(directional_shadow_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(directional_shadow_buffer_pong, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = shadow_blur_pipeline;

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);

                for (int i = 0; i < 2; i++) {
                    VkDescriptorImageInfo write_info = {
                        .sampler   = VK_NULL_HANDLE,
                        .imageView = i % 2 == 0 ? directional_shadow_buffer_pong.view : directional_shadow_buffer.view,
                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    };

                    VkDescriptorImageInfo read_info = {
                        .sampler   = linear_sampler_clamped,
                        .imageView = i % 2 == 0 ? directional_shadow_buffer.view : directional_shadow_buffer_pong.view,
                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    };

                    VkDescriptorImageInfo depth_info = {
                        .sampler     = linear_sampler_clamped,
                        .imageView   = depth_buffer.view,
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    };

                    std::vector<VkWriteDescriptorSet> write_sets = {
                        {
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstBinding       = 0,
                            .dstArrayElement  = 0,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .pImageInfo       = &write_info,
                            .pBufferInfo      = nullptr,
                            .pTexelBufferView = nullptr,
                        },
                        {
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstBinding       = 1,
                            .dstArrayElement  = 0,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .pImageInfo       = &read_info,
                            .pBufferInfo      = nullptr,
                            .pTexelBufferView = nullptr,
                        },
                        {
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstBinding       = 2,
                            .dstArrayElement  = 0,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .pImageInfo       = &depth_info,
                            .pBufferInfo      = nullptr,
                            .pTexelBufferView = nullptr,
                        },
                    };

                    vkCmdPushDescriptorSet(
                        command_buffer,
                        pipeline.bind_point,
                        pipeline.pipeline_layout,
                        0,
                        static_cast<uint32_t>(write_sets.size()),
                        write_sets.data()
                    );

                    ShadowBlurConstants constants = {
                        .image_size = glm::vec2(directional_shadow_buffer.width, directional_shadow_buffer.height),
                        .direction  = static_cast<float>(i % 2 == 0 ? 0 : 1),
                        .znear      = camera.near_plane
                    };

                    vkCmdPushConstants(
                        command_buffer,
                        pipeline.pipeline_layout,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        0,
                        sizeof(ShadowBlurConstants),
                        &constants
                    );

                    vkCmdDispatch(
                        command_buffer,
                        (directional_shadow_buffer.width + 7) / 8,
                        (directional_shadow_buffer.height + 7) / 8,
                        1
                    );

                    image_pipeline_barrier(
                        i % 2 == 0 ? directional_shadow_buffer_pong : directional_shadow_buffer,
                        command_buffer,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                    );
                }
            });

    auto& light_pass =
        framegraph.add_pass("lighting")
            .reads_buffer_dynamic(
                scene_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                scene_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                lighting_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer(
                ddgi_probe_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            )
            .samples_image(depth_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(gbuffer_albedo, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(gbuffer_normals, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(ao_output_denoised, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(ddgi_irradiance, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(gbuffer_emissive, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(ddgi_depth_atlas, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(directional_shadow_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(rt_reflection_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(lightpass_output, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                std::array<uint32_t, 2> offsets = {
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::SCENE_UBO)]
                };

                vkCmdBindPipeline(command_buffer, light_pipeline.bind_point, light_pipeline.pipeline_handle);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    light_pipeline.bind_point,
                    light_pipeline.pipeline_layout,
                    0,
                    light_descriptor_sets.size(),
                    light_descriptor_sets.data(),
                    offsets.size(),
                    offsets.data()
                );

                vkCmdDispatch(command_buffer, (swapchain.width + 7) / 8, (swapchain.height + 7) / 8, 1);
            });

    Pipeline particle_update_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/particle_update.comp.spv"),
        {
            DescriptorLayout{
                .bindings = {
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .write_info = DescriptorInfo(particle_position_buffer.handle)
                    },
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .write_info = DescriptorInfo(particle_velocity_buffer.handle)
                    },
                },
            },
        }
    );
    std::vector<VkDescriptorSet> particle_update_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, particle_update_pipeline);

    auto& particle_update_pass =
        framegraph.add_pass("particle update")
            .writes_buffer(
                particle_velocity_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            )
            .reads_buffer(
                particle_position_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            )
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                if (!enable_particles) {
                    return;
                }

                const Pipeline& pipeline = particle_update_pipeline;

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.pipeline_layout,
                    0,
                    particle_update_descriptor_sets.size(),
                    particle_update_descriptor_sets.data(),
                    0,
                    nullptr
                );
                vkCmdPushConstants(command_buffer, pipeline.pipeline_layout, pipeline.stage_flags, 0, 0, nullptr);

                vkCmdDispatch(command_buffer, (particle_count + 255) / 256, 1, 1);
            });

    Pipeline particle_render_pipeline = create_graphics_pipeline(
        device,
        {
            shader_from_file(device, VK_SHADER_STAGE_VERTEX_BIT, "data/shaders/particle_render.vert.spv"),
            shader_from_file(device, VK_SHADER_STAGE_FRAGMENT_BIT, "data/shaders/particle_render.frag.spv"),
        },
        {
            scene_data_layout,
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(particle_position_buffer.handle)
                        },
                    }
            },

        },
        {
            lightpass_output.format,
        },
        depth_buffer.format
    );

    std::vector<VkDescriptorSet> particle_render_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, particle_render_pipeline);

    auto& particle_render_pass =
        framegraph.add_pass("particle render")
            .writes_color_attachment(lightpass_output)
            .samples_image(depth_buffer, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT)
            .reads_buffer_dynamic(
                scene_ubo_buffer,
                VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                scene_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer(
                particle_position_buffer,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT
            )
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                if (!enable_particles) {
                    return;
                }

                std::array<uint32_t, 1> offsets = {dynamic_offsets[static_cast<uint32_t>(DynamicOffset::SCENE_UBO)]};

                auto& pipeline = particle_render_pipeline;

                VkRenderingAttachmentInfo color_attachment = {
                    .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .pNext              = nullptr,
                    .imageView          = lightpass_output.view,
                    .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .resolveMode        = VK_RESOLVE_MODE_NONE,
                    .resolveImageView   = VK_NULL_HANDLE,
                    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .loadOp             = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue         = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}},
                };

                VkRenderingAttachmentInfo depth_attachment = {
                    .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .pNext              = nullptr,
                    .imageView          = depth_buffer.view,
                    .imageLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    .resolveMode        = VK_RESOLVE_MODE_NONE,
                    .resolveImageView   = VK_NULL_HANDLE,
                    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .loadOp             = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp            = VK_ATTACHMENT_STORE_OP_NONE,
                    .clearValue         = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}},
                };

                VkRenderingInfo rendering_info = {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .renderArea =
                        {
                            .offset = {.x = 0, .y = 0},
                            .extent = {.width = swapchain.width, .height = swapchain.height},
                        },
                    .layerCount           = 1,
                    .viewMask             = 0,
                    .colorAttachmentCount = 1,
                    .pColorAttachments    = &color_attachment,
                    .pDepthAttachment     = &depth_attachment,
                    .pStencilAttachment   = nullptr
                };

                vkCmdBeginRendering(command_buffer, &rendering_info);

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    pipeline.bind_point,
                    pipeline.pipeline_layout,
                    0,
                    particle_render_descriptor_sets.size(),
                    particle_render_descriptor_sets.data(),
                    offsets.size(),
                    offsets.data()
                );

                vkCmdDraw(command_buffer, particle_count * 6, 1, 0, 0);

                vkCmdEndRendering(command_buffer);
            });

    Pipeline luminance_histogram_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/luminance_histogram.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(lightpass_output.view)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(luminance_buffer.handle, 0, luminance_buffer.size)
                        },
                    },
            },
        },
        sizeof(LuminanceConstants)
    );
    std::vector<VkDescriptorSet> luminance_histogram_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, luminance_histogram_pipeline);

    auto& luminance_histogram_pass =
        framegraph.add_pass("luminance histogram")
            .writes_buffer(
                luminance_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            )
            .reads_storage_image(lightpass_output, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = luminance_histogram_pipeline;

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.pipeline_layout,
                    0,
                    luminance_histogram_descriptor_sets.size(),
                    luminance_histogram_descriptor_sets.data(),
                    0,
                    nullptr
                );
                vkCmdPushConstants(
                    command_buffer,
                    pipeline.pipeline_layout,
                    pipeline.stage_flags,
                    0,
                    sizeof(LuminanceConstants),
                    &luminance_constants
                );

                vkCmdDispatch(
                    command_buffer, (lightpass_output.width + 15) / 16, (lightpass_output.height + 15) / 16, 1
                );
            });

    Pipeline luminance_average_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/luminance_average.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(average_luminance_image.view)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(luminance_buffer.handle, 0, luminance_buffer.size)
                        },
                    },
            },
        },
        sizeof(LuminanceConstants)

    );
    std::vector<VkDescriptorSet> luminance_average_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, luminance_average_pipeline);

    auto& luminance_average_pass =
        framegraph.add_pass("luminance average")
            .reads_buffer(luminance_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT)
            .writes_buffer(
                luminance_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            )
            .writes_storage_image(average_luminance_image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .reads_storage_image(average_luminance_image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = luminance_average_pipeline;

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.pipeline_layout,
                    0,
                    luminance_average_descriptor_sets.size(),
                    luminance_average_descriptor_sets.data(),
                    0,
                    nullptr
                );
                vkCmdPushConstants(
                    command_buffer,
                    pipeline.pipeline_layout,
                    pipeline.stage_flags,
                    0,
                    sizeof(LuminanceConstants),
                    &luminance_constants
                );

                vkCmdDispatch(command_buffer, 1, 1, 1);
            });

    Pipeline smaa_edge_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/smaa_edges.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(smaa_edges.view, VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, gbuffer_normals.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                nearest_sampler, gbuffer_id.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, lightpass_output.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                    }
            },
        },
        sizeof(glm::vec4)
    );
    std::vector<VkDescriptorSet> smaa_edge_pass_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, smaa_edge_pipeline);

    auto& smaa_edge_pass =
        framegraph.add_pass("SMAA edge")
            .samples_image(lightpass_output, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(gbuffer_normals, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(gbuffer_id, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(smaa_edges, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = smaa_edge_pipeline;

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);

                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.pipeline_layout,
                    0,
                    smaa_edge_pass_descriptor_sets.size(),
                    smaa_edge_pass_descriptor_sets.data(),
                    0,
                    nullptr
                );

                glm::vec4 constants = {
                    1.0 / (float)smaa_weights.width,
                    1.0 / (float)smaa_weights.height,
                    smaa_weights.width,
                    smaa_weights.height
                };

                vkCmdPushConstants(
                    command_buffer,
                    pipeline.pipeline_layout,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    0,
                    sizeof(glm::vec4),
                    &constants
                );

                vkCmdDispatch(command_buffer, (smaa_edges.width + 7) / 8, (smaa_edges.height + 7) / 8, 1);
            });

    Pipeline smaa_weights_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/smaa_weights.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(smaa_weights.view, VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, smaa_edges.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped, smaa_area_tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler_clamped, smaa_search_tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                    }
            },
        },
        sizeof(glm::vec4)
    );
    std::vector<VkDescriptorSet> smaa_weights_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, smaa_weights_pipeline);

    auto& smaa_weights_pass =
        framegraph.add_pass("SMAA weights")
            .samples_image(smaa_edges, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(smaa_weights, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = smaa_weights_pipeline;

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);

                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.pipeline_layout,
                    0,
                    smaa_weights_descriptor_sets.size(),
                    smaa_weights_descriptor_sets.data(),
                    0,
                    nullptr
                );

                glm::vec4 constants = {
                    1.0 / (float)smaa_weights.width,
                    1.0 / (float)smaa_weights.height,
                    smaa_weights.width,
                    smaa_weights.height
                };

                vkCmdPushConstants(
                    command_buffer,
                    pipeline.pipeline_layout,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    0,
                    sizeof(glm::vec4),
                    &constants
                );

                vkCmdDispatch(command_buffer, (smaa_weights.width + 7) / 8, (smaa_weights.height + 7) / 8, 1);
            });

    auto& bloom_downsample_pass =
        framegraph.add_pass("bloom downsample")
            .reads_image(
                lightpass_output,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL
            )
            .writes_image(
                bloom_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_IMAGE_LAYOUT_GENERAL
            )
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                VkPipelineBindPoint bind_point      = bloom_downsample_pipeline.bind_point;
                VkPipeline          pipeline        = bloom_downsample_pipeline.pipeline_handle;
                VkPipelineLayout    pipeline_layout = bloom_downsample_pipeline.pipeline_layout;
                VkShaderStageFlags  stage_flags     = bloom_downsample_pipeline.stage_flags;

                vkCmdBindPipeline(command_buffer, bind_point, pipeline);
                for (int i = 0; i < bloom_levels; ++i) {
                    uint32_t mip_width  = glm::max(1u, bloom_buffer.width >> i);
                    uint32_t mip_height = glm::max(1u, bloom_buffer.height >> i);

                    BloomPushConstants constants = {
                        .texel_size      = {1.0 / mip_width, 1.0 / mip_height},
                        .first_pass      = static_cast<uint32_t>(i == 0 ? 1 : 0),
                        .upsample_radius = bloom_upscale_sample_scale,
                    };

                    VkDescriptorImageInfo image_read_info = {
                        .sampler     = linear_sampler_clamped,
                        .imageView   = i == 0 ? lightpass_output.view : bloom_mip_views[i - 1],
                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    };

                    VkDescriptorImageInfo image_write_info = {
                        .sampler     = VK_NULL_HANDLE,
                        .imageView   = bloom_mip_views[i],
                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    };

                    std::vector<VkWriteDescriptorSet> write_sets = {
                        {
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstBinding       = 0,
                            .dstArrayElement  = 0,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .pImageInfo       = &image_write_info,
                            .pBufferInfo      = nullptr,
                            .pTexelBufferView = nullptr,
                        },
                        {
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstBinding       = 1,
                            .dstArrayElement  = 0,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .pImageInfo       = &image_read_info,
                            .pBufferInfo      = nullptr,
                            .pTexelBufferView = nullptr,
                        },
                    };

                    vkCmdPushDescriptorSet(
                        command_buffer,
                        bind_point,
                        pipeline_layout,
                        0,
                        static_cast<uint32_t>(write_sets.size()),
                        write_sets.data()
                    );

                    vkCmdPushConstants(
                        command_buffer, pipeline_layout, stage_flags, 0, sizeof(BloomPushConstants), &constants
                    );

                    uint32_t groups_x = (mip_width + 7) / 8;
                    uint32_t groups_y = (mip_height + 7) / 8;
                    vkCmdDispatch(command_buffer, groups_x, groups_y, 1);

                    image_pipeline_barrier(
                        bloom_buffer.handle,
                        command_buffer,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                        {
                            .aspectMask     = bloom_buffer.aspect,
                            .baseMipLevel   = static_cast<uint32_t>(i),
                            .levelCount     = 1,
                            .baseArrayLayer = 0,
                            .layerCount     = 1,
                        }
                    );
                }
            });

    auto& bloom_upsample_pass =
        framegraph.add_pass("bloom upsample")
            .writes_image(
                bloom_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                VK_IMAGE_LAYOUT_GENERAL
            )
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                VkPipelineBindPoint bind_point      = bloom_upsample_pipeline.bind_point;
                VkPipeline          pipeline        = bloom_upsample_pipeline.pipeline_handle;
                VkPipelineLayout    pipeline_layout = bloom_upsample_pipeline.pipeline_layout;
                VkShaderStageFlags  stage_flags     = bloom_upsample_pipeline.stage_flags;

                vkCmdBindPipeline(command_buffer, bind_point, pipeline);
                for (int i = bloom_levels - 1; i > 0; --i) {
                    uint32_t mip_width  = glm::max(1u, bloom_buffer.width >> (i - 1));
                    uint32_t mip_height = glm::max(1u, bloom_buffer.height >> (i - 1));

                    BloomPushConstants constants = {
                        .texel_size      = {1.0 / mip_width, 1.0 / mip_height},
                        .first_pass      = i == 0,
                        .upsample_radius = bloom_upscale_sample_scale,
                    };

                    VkDescriptorImageInfo lower_mip_info = {
                        .sampler     = linear_sampler_clamped,
                        .imageView   = bloom_mip_views[i],
                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    };

                    VkDescriptorImageInfo current_mip_read_info = {
                        .sampler     = linear_sampler_clamped,
                        .imageView   = bloom_mip_views[i - 1],
                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    };

                    VkDescriptorImageInfo image_write_info = {
                        .sampler     = VK_NULL_HANDLE,
                        .imageView   = bloom_mip_views[i - 1],
                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    };

                    std::vector<VkWriteDescriptorSet> write_sets = {
                        {
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstBinding       = 0,
                            .dstArrayElement  = 0,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .pImageInfo       = &image_write_info,
                            .pBufferInfo      = nullptr,
                            .pTexelBufferView = nullptr,
                        },
                        {
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstBinding       = 1,
                            .dstArrayElement  = 0,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .pImageInfo       = &current_mip_read_info,
                            .pBufferInfo      = nullptr,
                            .pTexelBufferView = nullptr,
                        },
                        {
                            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .pNext            = nullptr,
                            .dstBinding       = 2,
                            .dstArrayElement  = 0,
                            .descriptorCount  = 1,
                            .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .pImageInfo       = &lower_mip_info,
                            .pBufferInfo      = nullptr,
                            .pTexelBufferView = nullptr,
                        },
                    };

                    vkCmdPushDescriptorSet(
                        command_buffer,
                        bind_point,
                        pipeline_layout,
                        0,
                        static_cast<uint32_t>(write_sets.size()),
                        write_sets.data()
                    );

                    vkCmdPushConstants(
                        command_buffer, pipeline_layout, stage_flags, 0, sizeof(BloomPushConstants), &constants
                    );

                    uint32_t groups_x = (mip_width + 7) / 8;
                    uint32_t groups_y = (mip_height + 7) / 8;
                    vkCmdDispatch(command_buffer, groups_x, groups_y, 1);

                    image_pipeline_barrier(
                        bloom_buffer.handle,
                        command_buffer,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                        {
                            .aspectMask     = bloom_buffer.aspect,
                            .baseMipLevel   = static_cast<uint32_t>(i - 1),
                            .levelCount     = 1,
                            .baseArrayLayer = 0,
                            .layerCount     = 1,
                        }
                    );
                }
            });

    auto& composite_pass =
        framegraph.add_pass("composite")
            .samples_image(lightpass_output, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(bloom_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .reads_storage_image(average_luminance_image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(composite_output, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                float manual_ev100 =
                    glm::log2((camera_aperture * camera_aperture) / camera_shutter_time * 100.0 / camera_iso);
                composite_push_constants.manual_ev100 = manual_ev100;

                vkCmdBindPipeline(command_buffer, composite_pipeline.bind_point, composite_pipeline.pipeline_handle);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    composite_pipeline.bind_point,
                    composite_pipeline.pipeline_layout,
                    0,
                    composite_descriptor_sets.size(),
                    composite_descriptor_sets.data(),
                    0,
                    nullptr
                );
                vkCmdPushConstants(
                    command_buffer,
                    composite_pipeline.pipeline_layout,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    0,
                    sizeof(CompositePushConstants),
                    &composite_push_constants
                );

                vkCmdDispatch(command_buffer, (swapchain.width + 7) / 8, (swapchain.height + 7) / 8, 1);
            });

    auto& debug_pass =
        framegraph.add_pass("debug geometry")
            .writes_image(
                composite_output,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            )
            .writes_image(
                depth_buffer,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
            )
            .reads_buffer_dynamic(
                debug_renderer.instance_buffer,
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                debug_renderer.instance_buffer.size / debug_renderer.frames_in_flight
            )
            .reads_buffer_dynamic(
                lighting_ubo_buffer,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer(
                ddgi_probe_buffer,
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            )
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                if (debug_renderer.instance_count == 0) {
                    return;
                }

                debug_renderer_upload_data(debug_renderer, vma_allocator, frame_index);

                std::vector<VkRenderingAttachmentInfo> color_attachments = {
                    {
                        .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .pNext              = nullptr,
                        .imageView          = composite_output.view,
                        .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .resolveMode        = VK_RESOLVE_MODE_NONE,
                        .resolveImageView   = VK_NULL_HANDLE,
                        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .loadOp             = VK_ATTACHMENT_LOAD_OP_LOAD,
                        .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue         = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}},
                    },
                };

                VkRenderingAttachmentInfo depth_attachment_info = {
                    .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .pNext              = nullptr,
                    .imageView          = depth_buffer.view,
                    .imageLayout        = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    .resolveMode        = VK_RESOLVE_MODE_NONE,
                    .resolveImageView   = VK_NULL_HANDLE,
                    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .loadOp             = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue         = {.depthStencil = {.depth = 0.0f, .stencil = 0}}
                };

                VkRenderingInfo rendering_info = {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .renderArea =
                        {
                            .offset = {.x = 0, .y = 0},
                            .extent = {.width = swapchain.width, .height = swapchain.height},
                        },
                    .layerCount           = 1,
                    .viewMask             = 0,
                    .colorAttachmentCount = static_cast<uint32_t>(color_attachments.size()),
                    .pColorAttachments    = color_attachments.data(),
                    .pDepthAttachment     = &depth_attachment_info,
                    .pStencilAttachment   = nullptr
                };

                vkCmdBeginRendering(command_buffer, &rendering_info);

                vkCmdBindPipeline(
                    command_buffer, debug_renderer.pipeline.bind_point, debug_renderer.pipeline.pipeline_handle
                );

                uint32_t instance_offset =
                    (debug_renderer.instance_buffer.size / debug_renderer.frames_in_flight) * frame_index;
                std::array<uint32_t, 2> offsets = {
                    instance_offset,
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)],
                };

                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    debug_renderer.pipeline.pipeline_layout,
                    0,
                    debug_renderer.descriptor_sets.size(),
                    debug_renderer.descriptor_sets.data(),
                    offsets.size(),
                    offsets.data()
                );

                debug_renderer_constants.camera_pos = camera.position;
                vkCmdPushConstants(
                    command_buffer,
                    debug_renderer.pipeline.pipeline_layout,
                    debug_renderer.pipeline.stage_flags,
                    0,
                    sizeof(DebugRendererConstants),
                    &debug_renderer_constants
                );

                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(command_buffer, 0, 1, &debug_renderer.vertex_buffer.handle, &offset);
                vkCmdBindIndexBuffer(command_buffer, debug_renderer.index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);

                vkCmdDrawIndexed(command_buffer, debug_renderer.index_count, debug_renderer.instance_count, 0, 0, 0);

                vkCmdEndRendering(command_buffer);
            });

    Pipeline smaa_blend_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/smaa_blend.comp.spv"),
        {
            DescriptorLayout{
                .bindings =
                    {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                            .write_info = DescriptorInfo(smaa_output.view, VK_IMAGE_LAYOUT_GENERAL)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, composite_output.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, smaa_weights.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                    }
            },
        },
        sizeof(glm::vec4)
    );
    std::vector<VkDescriptorSet> smaa_blend_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, smaa_blend_pipeline);

    auto& smaa_blend_pass =
        framegraph.add_pass("SMAA blend")
            .samples_image(smaa_weights, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(composite_output, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(smaa_output, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                const Pipeline& pipeline = smaa_blend_pipeline;

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);

                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pipeline.pipeline_layout,
                    0,
                    smaa_blend_descriptor_sets.size(),
                    smaa_blend_descriptor_sets.data(),
                    0,
                    nullptr
                );

                glm::vec4 constants = {
                    1.0 / (float)smaa_weights.width,
                    1.0 / (float)smaa_weights.height,
                    smaa_weights.width,
                    smaa_weights.height
                };

                vkCmdPushConstants(
                    command_buffer,
                    pipeline.pipeline_layout,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    0,
                    sizeof(glm::vec4),
                    &constants
                );

                vkCmdDispatch(command_buffer, (smaa_weights.width + 7) / 8, (smaa_weights.height + 7) / 8, 1);
            });

    auto& blit_ui_pass =
        framegraph.add_pass("Final blit + UI").render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
            image_pipeline_barrier(
                swapchain.images[swapchain_image_index],
                command_buffer,
                VK_IMAGE_LAYOUT_UNDEFINED,
                editor_overlay ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                0,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                }
            );

            if (!editor_overlay) {
                auto& blit_source = smaa_output;
                image_pipeline_barrier(
                    blit_source,
                    command_buffer,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT
                );

                VkImageBlit blit_region = {
                    .srcSubresource =
                        {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
                    .srcOffsets =
                        {
                            {0, 0, 0},
                            {static_cast<int32_t>(swapchain.width), static_cast<int32_t>(swapchain.height), 1},
                        },
                    .dstSubresource =
                        {
                            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                            .mipLevel       = 0,
                            .baseArrayLayer = 0,
                            .layerCount     = 1,
                        },
                    .dstOffsets = {
                        {},
                        {static_cast<int32_t>(swapchain.width), static_cast<int32_t>(swapchain.height), 1},
                    },
                };

                vkCmdBlitImage(
                    command_buffer,
                    blit_source.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    swapchain.images[swapchain_image_index],
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &blit_region,
                    VK_FILTER_LINEAR
                );

                image_pipeline_barrier(
                    blit_source,
                    command_buffer,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                );
            } else {
                VkRenderingAttachmentInfo swapchain_attachment_info = {
                    .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .pNext              = nullptr,
                    .imageView          = swapchain.image_views[swapchain_image_index],
                    .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .resolveMode        = VK_RESOLVE_MODE_NONE,
                    .resolveImageView   = VK_NULL_HANDLE,
                    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .loadOp             = editor_overlay ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue         = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}},
                };

                VkRenderingInfo imgui_rendering_info = {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .renderArea =
                        {
                            .offset = {.x = 0, .y = 0},
                            .extent = {.width = swapchain.width, .height = swapchain.height},
                        },
                    .layerCount           = 1,
                    .viewMask             = 0,
                    .colorAttachmentCount = 1,
                    .pColorAttachments    = &swapchain_attachment_info,
                    .pDepthAttachment     = nullptr,
                    .pStencilAttachment   = nullptr
                };
                vkCmdBeginRendering(command_buffer, &imgui_rendering_info);
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
                vkCmdEndRendering(command_buffer);
            }

            image_pipeline_barrier(
                swapchain.images[swapchain_image_index],
                command_buffer,
                editor_overlay ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                0,
                0,
                {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                }
            );
        });

    framegraph.build();

    auto screen_pos_to_scene_vewport = [&](glm::vec2 pos) -> glm::vec2 {
        return {pos.x - viewport_pos_size.x, pos.y - viewport_pos_size.y};
    };

    auto coords_in_scene_viewport = [&](glm::vec2 pos) -> bool {
        auto coords = screen_pos_to_scene_vewport(pos);

        return coords.x >= 0.0 && coords.x <= viewport_pos_size.z && coords.y >= 0.0 && coords.y <= viewport_pos_size.w;
    };

    int pick_frame = UINT32_MAX;

    Editor editor;

    while (running) {
        FrameMark;

        if (simulate_lower_fps) {
            auto time       = std::chrono::high_resolution_clock::now();
            auto delta_time = std::chrono::duration<float>(time - frame_timestamp).count();

            while (delta_time < 1.0f / simulated_fps) {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(1ms);

                time       = std::chrono::high_resolution_clock::now();
                delta_time = std::chrono::duration<float>(time - frame_timestamp).count();
            }
        }

        auto time       = std::chrono::high_resolution_clock::now();
        auto delta_time = std::chrono::duration<float>(time - frame_timestamp).count();
        frame_timestamp = time;

        accumulated_fps++;
        time_passed += delta_time;
        physics_time_accumulator += delta_time;

        if (time_passed >= 1.0f) {
            fps = accumulated_fps;

            accumulated_fps = 0;
            time_passed -= 1.0f;
        }

        SDL_Event window_event;
        while (SDL_PollEvent(&window_event)) {
            ImGui_ImplSDL3_ProcessEvent(&window_event);
            switch (window_event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                input_system.register_key_press(window_event.key.scancode);
                break;
            case SDL_EVENT_KEY_UP:
                input_system.register_key_release(window_event.key.scancode);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                input_system.register_button_press(window_event.button.button);

                if (window_event.button.button == SDL_BUTTON_RIGHT &&
                    coords_in_scene_viewport(input_system.get_mouse_position())) {
                    SDL_SetWindowMouseGrab(window, true);
                    SDL_SetWindowRelativeMouseMode(window, true);

                    capturing_mouse = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                input_system.register_button_release(window_event.button.button);

                if (window_event.button.button == SDL_BUTTON_RIGHT && capturing_mouse) {
                    SDL_SetWindowMouseGrab(window, false);
                    SDL_SetWindowRelativeMouseMode(window, false);

                    capturing_mouse = false;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                auto xrel = static_cast<float>(window_event.motion.xrel);
                auto yrel = static_cast<float>(window_event.motion.yrel);

                auto x = static_cast<float>(window_event.motion.x);
                auto y = static_cast<float>(window_event.motion.y);

                input_system.mouse_pos = {x, y};

                if (capturing_mouse) {
                    camera.orientation =
                        glm::rotate(
                            glm::quat(0, 0, 0, 1), float(-xrel * camera_mouse_sensitivity), glm::vec3(0, 1, 0)
                        ) *
                        camera.orientation;
                    camera.orientation = glm::rotate(
                                             glm::quat(0, 0, 0, 1),
                                             float(-yrel * camera_mouse_sensitivity),
                                             camera.orientation * glm::vec3(1, 0, 0)
                                         ) *
                                         camera.orientation;

                    glm::vec2 mouse_delta = {-xrel * camera_mouse_sensitivity, -yrel * camera_mouse_sensitivity};
                }
                break;
            }
        }

        float current_player_speed = player_speed;
        if (input_system.is_key_pressed(Key::LEFT_SHIFT) || input_system.is_key_pressed(Key::LEFT_CTRL)) {
            if (input_system.is_key_pressed(Key::LEFT_CTRL)) {
                camera_speed = base_camera_speed / camera_speed_mod;
                current_player_speed /= player_sprint_modifier;
            }

            if (input_system.is_key_pressed(Key::LEFT_SHIFT)) {
                camera_speed = base_camera_speed * camera_speed_mod;
                current_player_speed *= player_sprint_modifier;
            }
        } else {
            camera_speed = base_camera_speed;
        }

        glm::vec3 velocity = glm::vec3(0.0);
        if (input_system.is_key_pressed(Key::W)) {
            velocity.x = 1;
        }

        if (input_system.is_key_pressed(Key::S)) {
            velocity.x = -1;
        }

        if (input_system.is_key_pressed(Key::A)) {
            velocity.y = -1;
        }

        if (input_system.is_key_pressed(Key::D)) {
            velocity.y = 1;
        }

        if (input_system.is_key_just_pressed(Key::G)) {
            player_physics = !player_physics;

            if (player_physics) {
                player_character->SetPosition(JPH::RVec3(camera.position.x, camera.position.y, camera.position.z));
            }
        }

        if (input_system.is_key_just_pressed(Key::P)) {
            visualize_probes = !visualize_probes;
        }

        if (input_system.is_key_just_pressed(Key::GRAVE)) {
            editor_overlay = !editor_overlay;
        }

        if (input_system.is_key_just_pressed(Key::F5)) {
            editor_mode = !editor_mode;

            if (editor_mode) {
                // Entering editor state
                editor_overlay = true;

                auto physics_view = scene.entity_registry.view<components::Physics>();
                for (auto [e, p] : physics_view.each()) {
                    if (!p.body_id.IsInvalid()) {
                        if (!p.is_static) {
                            physics_body_interface.RemoveBody(p.body_id);
                            physics_body_interface.DestroyBody(p.body_id);
                        }
                    }
                }

                script_system.clear();
                scene.entity_registry.clear();
                scene.materials.resize(scene.original_material_size);

                cereal::BinaryInputArchive input(scene_state_snapshot);

                entt::snapshot_loader(scene.entity_registry)
                    .get<entt::entity>(input)
                    .get<components::Transform>(input)
                    .get<components::Name>(input)
                    .get<components::Mesh>(input)
                    .get<components::Parent>(input)
                    .get<components::Children>(input)
                    .get<components::Script>(input)
                    .get<components::Physics>(input);

                auto view = scene.entity_registry.view<components::Physics, components::Mesh, components::Transform>();
                for (auto [e, p, m, t] : view.each()) {
                    if (p.is_static) {
                        if (static_body_load_map.contains(e)) {
                            p.body_id = static_body_load_map.at(e);
                        }
                    }
                }

                SDL_SetWindowMouseGrab(window, false);
                SDL_SetWindowRelativeMouseMode(window, false);
                capturing_mouse = false;
            } else {
                // Entering play state
                editor_overlay = false;

                scene_state_snapshot.str("");
                scene_state_snapshot.clear();

                static_body_load_map.clear();

                cereal::BinaryOutputArchive output(scene_state_snapshot);

                entt::snapshot(scene.entity_registry)
                    .get<entt::entity>(output)
                    .get<components::Transform>(output)
                    .get<components::Name>(output)
                    .get<components::Mesh>(output)
                    .get<components::Parent>(output)
                    .get<components::Children>(output)
                    .get<components::Script>(output)
                    .get<components::Physics>(output);

                auto physics_view = scene.entity_registry.view<components::Physics>();
                for (auto [e, p] : physics_view.each()) {
                    if (!p.body_id.IsInvalid()) {
                        if (p.is_static) {
                            static_body_load_map.insert({e, p.body_id});
                        }
                    }
                }

                auto view = scene.entity_registry.view<components::Script>();
                for (auto [e, s] : view.each()) {
                    script_system.initialize(s);
                }
            }
        }

        if (!editor_overlay) {
            SDL_SetWindowMouseGrab(window, true);
            SDL_SetWindowRelativeMouseMode(window, true);
            capturing_mouse = true;
        }

        if (input_system.is_key_just_pressed(Key::ESCAPE)) {
            running = false;
        }

        {
            mesh_instances.clear();
            mesh_instance_entities.clear();
            auto view = scene.entity_registry.view<components::Mesh>();
            for (auto& e : view) {
                auto& c = view.get<components::Mesh>(e);
                mesh_instances.push_back(c.mesh);
                mesh_instance_entities.push_back(e);
            }
        }

        {
            auto view = scene.entity_registry.view<components::Transform, components::Physics>();
            for (auto [entity, transform, physics] : view.each()) {
                if (physics.body_id.IsInvalid()) {
                    continue;
                }

                JPH::EActivation activation =
                    physics.is_static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;

                if (transform.world_scale != physics.last_scale && transform.world_scale != 0.0f) {
                    float scale_delta = transform.world_scale / physics.last_scale;
                    auto  shape       = physics_body_interface.GetShape(physics.body_id);
                    auto  new_shape   = shape->ScaleShape(JPH::Vec3(scale_delta, scale_delta, scale_delta));
                    if (new_shape.IsValid()) {
                        physics_body_interface.SetShape(physics.body_id, new_shape.Get(), false, activation);
                    }

                    physics.last_scale = transform.world_scale;
                }

                if (!physics.is_static) {
                    continue;
                }

                physics_body_interface.SetPosition(
                    physics.body_id,
                    JPH::Vec3(transform.world_position.x, transform.world_position.y, transform.world_position.z),
                    activation
                );

                physics_body_interface.SetRotation(
                    physics.body_id,
                    JPH::Quat(
                        transform.world_rotation.x,
                        transform.world_rotation.y,
                        transform.world_rotation.z,
                        transform.world_rotation.w
                    ),
                    activation
                );
            }
        }

        {
            auto update_view = scene.entity_registry.view<components::Transform, components::Physics>();

            while (physics_time_accumulator >= physics_delta_time) {
                if (!editor_mode) {
                    auto script_view = scene.entity_registry.view<components::Script>();
                    for (auto [e, s] : script_view.each()) {
                        script_system.call_on_fixed_update(s, physics_delta_time);
                    }
                }

                for (auto [entity, transform, physics] : update_view.each()) {
                    if (physics.body_id.IsInvalid()) {
                        continue;
                    }

                    JPH::Vec3 p;
                    JPH::Quat r;
                    physics_body_interface.GetPositionAndRotation(physics.body_id, p, r);

                    physics.last_position = p;
                    physics.last_rotation = r;
                }

                physics_system.Update(physics_delta_time, 1, &physics_temp_allocator, &physics_job_system);
                physics_time_accumulator -= physics_delta_time;
            }

            auto interpolate_view =
                scene.entity_registry.view<components::Transform, components::Physics, components::Mesh>();
            float physics_alpha = physics_time_accumulator / physics_delta_time;
            for (auto [entity, transform, physics, m] : interpolate_view.each()) {
                if (physics.is_static || physics.body_id.IsInvalid()) {
                    continue;
                }

                JPH::Vec3 p;
                JPH::Quat r;
                physics_body_interface.GetPositionAndRotation(physics.body_id, p, r);

                glm::vec3 new_pos = glm::vec3(p.GetX(), p.GetY(), p.GetZ());
                glm::quat new_rot = glm::quat(r.GetX(), r.GetY(), r.GetZ(), r.GetW());

                glm::vec3 old_pos =
                    glm::vec3(physics.last_position.GetX(), physics.last_position.GetY(), physics.last_position.GetZ());
                glm::quat old_rot = glm::quat(
                    physics.last_rotation.GetX(),
                    physics.last_rotation.GetY(),
                    physics.last_rotation.GetZ(),
                    physics.last_rotation.GetW()
                );

                Mesh& geometry = scene.meshes[m.mesh.mesh_id];

                glm::vec3 center = (geometry.bounds_max + geometry.bounds_min) * 0.5f;
                glm::vec3 offset = new_rot * (center * m.mesh.scale);

                transform.position = glm::mix(old_pos, new_pos, physics_alpha) - offset;
                transform.rotation = glm::slerp(old_rot, new_rot, physics_alpha);
            }
        }

        if (!editor_mode) {
            auto script_view = scene.entity_registry.view<components::Script>();
            for (auto [e, s] : script_view.each()) {
                script_system.call_on_update(s, delta_time);
            }
        }

        {
            auto root_view = scene.entity_registry.view<components::Transform>(entt::exclude<components::Parent>);
            for (auto [e, t] : root_view.each()) {
                t.world_position = t.position;
                t.world_scale    = t.scale;
                t.world_rotation = t.rotation;
            }

            bool                       has_updates = true;
            std::unordered_set<Entity> processed;

            for (auto e : root_view) {
                processed.insert(e);
            }

            while (has_updates) {
                has_updates = false;

                auto child_view = scene.entity_registry.view<components::Transform, components::Parent>();
                for (auto [e, ct, p] : child_view.each()) {
                    if (processed.contains(e)) {
                        continue;
                    }

                    if (!processed.contains(p.parent)) {
                        continue;
                    }

                    auto& parent_transform = scene.entity_registry.get<components::Transform>(p.parent);

                    ct.world_rotation = parent_transform.world_rotation * ct.rotation;
                    ct.world_scale    = parent_transform.world_scale * ct.scale;
                    ct.world_position = parent_transform.world_position +
                                        (parent_transform.world_rotation * (ct.position * parent_transform.scale));

                    processed.insert(e);
                    has_updates = true;
                }
            }

            auto view = scene.entity_registry.view<components::Transform, components::Mesh>();
            for (auto [e, t, m] : view.each()) {
                m.mesh.position = t.world_position;
                m.mesh.scale    = t.world_scale;
                m.mesh.rotation = t.world_rotation;
            }
        }

        if (!player_physics) {
            move_camera(camera, velocity, camera_speed * delta_time);
            script_system.set_player_velocity(velocity);
        } else {
            glm::vec3 oriented_velocity =
                camera.orientation * glm::vec3(velocity.y, 0, -velocity.x) * current_player_speed;
            JPH::Vec3 desired_velocity(oriented_velocity.x, 0, oriented_velocity.z);

            static bool                         jumped       = false;
            JPH::CharacterVirtual::EGroundState ground_state = player_character->GetGroundState();

            if (input_system.is_key_pressed(Key::SPACE) &&
                ground_state == JPH::CharacterVirtual::EGroundState::OnGround) {
                if (!jumped) {
                    desired_velocity.SetY(player_jump_velocity);
                    jumped = true;
                }
            } else {
                if (ground_state != JPH::CharacterVirtual::EGroundState::OnGround) {
                    JPH::Vec3 current_vel = player_character->GetLinearVelocity();

                    if (current_vel.GetY() > 1.0f && current_vel.GetY() < desired_velocity.GetY()) {
                        current_vel.SetY(0.0f);
                    }

                    desired_velocity.SetY(current_vel.GetY() + physics_system.GetGravity().GetY() * delta_time);
                }
                jumped = false;
            }

            if (player_character->GetLinearVelocity().GetY() > 0 &&
                player_character->GetGroundNormal().GetY() < -0.75) {
                desired_velocity.SetY(0.0);
            }

            JPH::CharacterVirtual::ExtendedUpdateSettings update_settings;
            update_settings.mStickToFloorStepDown = JPH::Vec3(0, -0.5f, 0);
            update_settings.mWalkStairsStepUp     = JPH::Vec3(0, 0.4f, 0);

            player_character->SetLinearVelocity(desired_velocity);
            player_character->ExtendedUpdate(
                delta_time,
                physics_system.GetGravity(),
                update_settings,
                physics_system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                physics_system.GetDefaultLayerFilter(Layers::MOVING),
                {},
                {},
                physics_temp_allocator
            );

            JPH::RVec3 char_vel = player_character->GetLinearVelocity();
            script_system.set_player_velocity(glm::vec3(char_vel.GetX(), char_vel.GetY(), char_vel.GetZ()));
            JPH::RVec3 char_pos = player_character->GetPosition();
            camera.position =
                glm::vec3(char_pos.GetX(), char_pos.GetY(), char_pos.GetZ()) + glm::vec3(0, player_height * 0.4f, 0);
        }

        if (frame_count == pick_frame) {
            void* ptr;
            VK_CHECK(vmaMapMemory(vma_allocator, pick_buffer.allocation, &ptr));
            uint32_t mesh_id = *reinterpret_cast<uint32_t*>(ptr);
            if (mesh_id == UINT32_MAX || mesh_id >= mesh_instance_entities.size()) {
                editor.set_selected_entity(entt::null);
            } else {
                editor.set_selected_entity(mesh_instance_entities[mesh_id]);
            }

            vmaUnmapMemory(vma_allocator, pick_buffer.allocation);
            pick_frame = UINT32_MAX;
        }

        update_camera(camera);

        script_system.set_player_position(camera.position);
        script_system.set_player_look_dir(camera.orientation * glm::vec3(0, 0, -1));

        luminance_constants.time_coef          = glm::clamp(1.0f - glm::exp(-delta_time * adaption_speed), 0.0f, 1.0f);
        luminance_constants.min_log2_luminance = min_log_lum;
        luminance_constants.inverse_log2_luminance = 1.0f / (max_log_lum - min_log_lum);

        auto transposed_projection = glm::transpose(camera.projection_matrix);

        glm::vec4 frustum_x = normalize_plane(transposed_projection[3] + transposed_projection[0]);
        glm::vec4 frustum_y = normalize_plane(transposed_projection[3] + transposed_projection[1]);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
        if (editor_overlay) {
            editor.render_main_menu(scene, script_system, physics_system, [&](std::string path) {
                VkCommandBufferAllocateInfo alloc_info = {
                    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                    .pNext              = nullptr,
                    .commandPool        = command_pool,
                    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                    .commandBufferCount = 1
                };
                VkCommandBuffer command_buffer = VK_NULL_HANDLE;
                VK_CHECK(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer));
                SceneSerializer::save(
                    path,
                    scene,
                    physics_system,
                    script_system,
                    buffers,
                    buffer_offsets,
                    compressed_texture_data,
                    device,
                    graphics_queue,
                    vma_allocator,
                    command_buffer
                );
                vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
            });

            ImGui::Begin(ICON_FA_VIDEO " Scene Viewport", nullptr, ImGuiWindowFlags_MenuBar);
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu(ICON_FA_ARROW_CIRCLE_RIGHT " Viewport Source")) {
                    for (auto [name, handle] : editor_viewport_source_handles) {
                        if (ImGui::MenuItem(name.c_str(), nullptr, editor_viewport_source.compare(name) == 0)) {
                            editor_viewport_source = name;
                        }
                    }
                    ImGui::EndMenu();
                }

                ImGui::Text(": %s", editor_viewport_source.c_str());
                ImGui::Text("        State: %s", editor_mode ? "Editor" : "Gameplay");
                ImGui::EndMenuBar();
            }

            auto region = ImGui::GetContentRegionAvail();
            auto cursor = ImGui::GetCursorScreenPos();

            if (editor_viewport_source_handles.contains(editor_viewport_source)) {
                auto source = editor_viewport_source_handles[editor_viewport_source];

                float aspect_ratio = (float)source.image.width / (float)source.image.height;

                ImVec2 size = ImVec2(region.y * aspect_ratio, region.y);
                if (size.x > region.x) {
                    size = ImVec2(region.x, region.x / aspect_ratio);
                }

                viewport_pos_size = glm::vec4(cursor.x, cursor.y, size.x, size.y);

                ImGui::Image(source.descriptor_set, size);
                ImGui::End();
            }

        } else {
            viewport_pos_size = glm::vec4(0, 0, swapchain.width, swapchain.height);
        }

        editor.render_scene_node_property_window(scene, script_system, imgui_material_image_handles);

        ImGui::Begin(ICON_FA_FILE " Assets");
        if (ImGui::TreeNode("Textures")) {
            int images_per_row = (ImGui::GetContentRegionAvail().x) / 50 - 1;
            int row_id         = 0;
            for (auto& [slot, handle] : imgui_material_image_handles) {
                ImGui::Image(handle, ImVec2(50, 50));
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    ImGui::SetDragDropPayload("texture_id", &slot, sizeof(uint32_t));
                    ImGui::EndDragDropSource();
                }

                row_id++;
                if (row_id < images_per_row) {
                    ImGui::SameLine();
                } else {
                    row_id = 0;
                }
            }

            ImGui::TreePop();
        }
        ImGui::End();

        std::vector<std::pair<std::string, PassTiming>> pass_timings = {};
        for (const auto& pass : framegraph.passes) {
            pass_timings.push_back(std::make_pair(pass.name, framegraph.get_pass_timing(pass.name)));
        }
        editor.render_performance_window(pass_timings);

        ImGui::Begin(ICON_FA_COGS " Configuration");
        ImGui::Checkbox("Enable Particles", &enable_particles);
        ImGui::InputInt("Simulate Target FPS", &simulated_fps);
        ImGui::Checkbox("Simulate FPS", &simulate_lower_fps);
        if (ImGui::CollapsingHeader("Renderer Info", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Rendering path: %s", use_meshlets ? "Meshlets" : "Indirect");
            ImGui::Text("Objects: %lu", mesh_instances.size());
            ImGui::Text("FPS: %u", fps);
            ImGui::Text("Camera Position: %.3f, %.3f, %.3f", camera.position.x, camera.position.y, camera.position.z);
            ImGui::Text(
                "Camera Orientation: %.3f, %.3f, %.3f, %.3f",
                camera.orientation.x,
                camera.orientation.y,
                camera.orientation.z,
                camera.orientation.w
            );
            ImGui::Text("Triangles Rendered: %.3fM", (double(pipeline_stats[0]) / 1'000'000.0));
            ImGui::Text("Fragment shader invocations: %.3fM", (double(pipeline_stats[1]) / 1'000'000.0));
        }

        if (ImGui::CollapsingHeader("Culling & LOD's")) {
            if (ImGui::Checkbox("Freeze frustum", &debug_frustum)) {
                frozen_view       = camera.view_matrix;
                frozen_frustum[0] = frustum_x.x;
                frozen_frustum[1] = frustum_x.z;
                frozen_frustum[2] = frustum_y.y;
                frozen_frustum[3] = frustum_y.z;
            }
            ImGui::SameLine();
            ImGui::Checkbox("Enable LOD's", (bool*)&cull_push_constants.enable_lods);
            ImGui::SliderInt("Min LOD Level", &min_lod, 0, 8);
            ImGui::NewLine();

            ImGui::Checkbox("Disable culling (global)", &disable_culling);
            ImGui::Checkbox("Disable frustum cull (compute)", (bool*)&cull_push_constants.disable_frustum_cull);
            ImGui::Checkbox("Disable depth cull (compute)", (bool*)&cull_push_constants.disable_depth_cull);
            ImGui::Checkbox("Disable frustum cull (mesh)", (bool*)&gpass_push_constants.disable_frustum_cull);
            ImGui::Checkbox("Disable depth cull (mesh)", (bool*)&gpass_push_constants.disable_depth_cull);
            ImGui::Checkbox("Disable cone cull (mesh)", (bool*)&gpass_push_constants.disable_cone_cull);
            ImGui::Checkbox(
                "Disable small triangle cull (mesh)", (bool*)&gpass_push_constants.disable_small_triangle_cull
            );
        }

        if (ImGui::CollapsingHeader("Rendering")) {
            ImGui::SeparatorText("Directional Light");
            ImGui::DragFloat3("Direction", &lighting_data.light_direction.x, 0.01, -1.0, 1.0);
            ImGui::ColorEdit3(
                "Color", &lighting_data.light_color.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float
            );
            ImGui::SliderFloat("Intensity", &lighting_data.light_color.w, 0.0, 100.0);

            ImGui::SeparatorText("Sky");
            ImGui::ColorEdit3(
                "Top Hemisphere",
                &lighting_data.sky_hemisphere_top.x,
                ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float
            );
            ImGui::ColorEdit3(
                "Bottom Hemisphere",
                &lighting_data.sky_hemisphere_bottom.x,
                ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float
            );

            ImGui::SeparatorText("DDGI");
            if (ImGui::BeginCombo("Rays Per Probe", std::to_string(lighting_data.rays_per_probe).c_str())) {
                for (int i = 0; i < IM_ARRAYSIZE(ray_per_probe_values); i++) {
                    bool is_selected = (lighting_data.rays_per_probe == ray_per_probe_values[i]);
                    if (ImGui::Selectable(std::to_string(ray_per_probe_values[i]).c_str(), is_selected)) {
                        lighting_data.rays_per_probe = ray_per_probe_values[i];
                    }

                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::DragFloat("GI Intensity", &lighting_data.gi_intensity, 0.01);
            ImGui::DragFloat("Probe Spacing", &lighting_data.probe_spacing, 0.01, 0.1, 10.0);
            ImGui::DragFloat3("Grid Origin", &lighting_data.grid_origin.x, 0.03, -100.0, 100.0);
            ImGui::Checkbox("Visualize Probes", (bool*)&visualize_probes);
            ImGui::Checkbox("Cull Innactive Probes", (bool*)&debug_renderer_constants.cull_innactive_probes);
            ImGui::Checkbox("Multibounce Diffuse", (bool*)&lighting_data.multibounce);

            ImGui::SeparatorText("Light Pass");
            ImGui::Checkbox("Use Bent Normals", (bool*)&lighting_data.use_bent_normals);
            ImGui::Checkbox("Remove Visibility Checks", (bool*)&lighting_data.remove_visibility_checks);
            ImGui::Checkbox("Compensate Specular", (bool*)&lighting_data.compensate_specular);
            ImGui::Checkbox("Disney Diffuse", (bool*)&lighting_data.disney_diffuse);

            ImGui::SeparatorText("Bloom");
            ImGui::SliderFloat("Bloom Upscale radius", &bloom_upscale_sample_scale, 0.0, 5.0);
            ImGui::SliderFloat("Bloom Strength", &composite_push_constants.bloom_strength, 0.0, 1.0);
            ImGui::SliderInt("Bloom Levels", &bloom_levels, 0, bloom_buffer.levels);

            ImGui::SeparatorText("Post Process");
            ImGui::Checkbox("Use GT5 tonemapping", (bool*)&composite_push_constants.tonemapping_type);
            ImGui::Checkbox("Enable Auto Exposure", (bool*)&composite_push_constants.enable_auto_exposure);
            ImGui::SliderFloat(
                "EV Compensation", &composite_push_constants.exposure_compensation, -3.0f, 3.0f, "%.2f EV"
            );

            if (!composite_push_constants.enable_auto_exposure) {
                ImGui::Separator();
                ImGui::Text("Manual Exposure Settings");

                if (ImGui::SliderFloat("Aperture", &camera_aperture, 1.0f, 22.0f, "f/%.1f")) {
                    const float f_stops[] = {1.4f, 2.0f, 2.8f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f, 22.0f};
                    float       closest   = f_stops[0];
                    float       min_dist  = fabsf(camera_aperture - closest);
                    for (float stop : f_stops) {
                        float dist = fabsf(camera_aperture - stop);
                        if (dist < min_dist) {
                            min_dist = dist;
                            closest  = stop;
                        }
                    }
                    if (min_dist < 0.2f) {
                        camera_aperture = closest;
                    }
                }

                float shutter_log = -log2f(camera_shutter_time);
                if (ImGui::SliderFloat(
                        "Shutter Speed", &shutter_log, 0.0f, 13.0f, "1/%.0f", ImGuiSliderFlags_Logarithmic
                    )) {
                    camera_shutter_time = powf(2.0f, -shutter_log);
                }

                ImGui::SliderFloat("ISO", &camera_iso, 100.0f, 6400.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
            } else {
                ImGui::SliderFloat("Min Log Luminance", &min_log_lum, -16.0f, 0.0f, "%.1f");
                ImGui::SliderFloat("Max Log Luminance", &max_log_lum, 0.0f, 8.0f, "%.1f");

                ImGui::SliderFloat("Min EV100", &composite_push_constants.min_ev100, -10.0f, 10.0f, "%.1f");
                ImGui::SliderFloat("Max EV100", &composite_push_constants.max_ev100, -5.0f, 20.0f, "%.1f");
                ImGui::SliderFloat("Adaptation Speed", &adaption_speed, 0.1f, 10.0f, "%.1f");
            }
        }

        if (ImGui::CollapsingHeader("Transform Gizmo")) {
            ImGui::Checkbox("Enable Transform Snap", &enable_transform_snap);

            if (ImGui::InputFloat("Transform Snap", &transform_snap.x, 1.0f)) {
                transform_snap = glm::vec3(transform_snap.x);
            }
        }

        if (ImGui::CollapsingHeader("Physics")) {
            ImGui::InputFloat("Fling Modifier", &physics_fling_modifier);
            ImGui::InputFloat("Spawn Mass", &physics_spawn_mass);
            ImGui::SliderFloat("Spawn Restitution", &physics_spawn_restitution, 0.0f, 1.0f);
            ImGui::SliderFloat("Spawn Friction", &physics_spawn_friction, 0.0f, 1.0f);

            if (ImGui::Checkbox("Enable Player Physics", &player_physics)) {
                if (player_physics) {
                    player_character->SetPosition(JPH::RVec3(camera.position.x, camera.position.y, camera.position.z));
                }
            }
        }
        ImGui::End();

        editor.render_scene_hierarchy_window(scene);

        if (editor.get_selected_entity() != entt::null) {
            if (input_system.is_key_pressed(Key::LEFT_SHIFT)) {
                if (input_system.is_key_just_pressed(Key::C)) {
                    tranform_gizmo_op = ImGuizmo::OPERATION::SCALEU;
                }

                if (input_system.is_key_just_pressed(Key::R)) {
                    tranform_gizmo_op = ImGuizmo::OPERATION::ROTATE;
                }

                if (input_system.is_key_just_pressed(Key::T)) {
                    tranform_gizmo_op = ImGuizmo::OPERATION::TRANSLATE;
                }
            }

            auto t = scene.get_component<components::Transform>(editor.get_selected_entity());

            glm::mat4 transform = glm::translate(glm::mat4(1.0f), t->world_position);
            transform           = transform * glm::mat4_cast(t->world_rotation);
            transform           = glm::scale(transform, glm::vec3(t->world_scale));

            auto      angle      = glm::normalize(glm::eulerAngles(camera.orientation));
            glm::mat4 view       = glm::mat4_cast(camera.orientation);
            glm::mat4 projection = glm::perspective(
                glm::radians(camera.fov), camera.viewport_width / camera.viewport_height, 0.01f, 1000.0f
            );

            view = camera.view_matrix;

            view = glm::scale(glm::identity<glm::mat4>(), glm::vec3(1, 1, -1)) * view;

            glm::mat4 delta_mat;

            ImGuizmo::SetRect(viewport_pos_size.x, viewport_pos_size.y, viewport_pos_size.z, viewport_pos_size.w);
            ImGuizmo::SetAlternativeWindow(ImGui::FindWindowByName(ICON_FA_VIDEO " Scene Viewport"));
            if (ImGuizmo::Manipulate(
                    &view[0].x,
                    &projection[0].x,
                    tranform_gizmo_op,
                    ImGuizmo::MODE::WORLD,
                    &transform[0].x,
                    &delta_mat[0].x,
                    enable_transform_snap ? &transform_snap.x : nullptr
                )) {
                glm::vec3 position;
                glm::vec3 rotation;
                glm::vec3 scale;
                ImGuizmo::DecomposeMatrixToComponents(&delta_mat[0].x, &position.x, &rotation.x, &scale.x);

                if (tranform_gizmo_op == ImGuizmo::OPERATION::TRANSLATE) {
                    t->position += position;
                }

                if (tranform_gizmo_op == ImGuizmo::OPERATION::ROTATE) {
                    t->rotation =
                        glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(rotation.x), glm::vec3(1, 0, 0)) * t->rotation;
                    t->rotation =
                        glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(rotation.y), glm::vec3(0, 1, 0)) * t->rotation;
                    t->rotation =
                        glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(rotation.z), glm::vec3(0, 0, 1)) * t->rotation;
                }

                if (tranform_gizmo_op == ImGuizmo::OPERATION::SCALEU) {
                    t->scale *= scale.x;
                }

                auto p = scene.get_component<components::Physics>(editor.get_selected_entity());
                if (p && !p->is_static && !p->body_id.IsInvalid()) {
                    JPH::EActivation activation = JPH::EActivation::Activate;

                    auto last_position = physics_body_interface.GetPosition(p->body_id);
                    auto new_position  = JPH::Vec3(
                        last_position.GetX() + position.x,
                        last_position.GetY() + position.y,
                        last_position.GetZ() + position.z
                    );

                    if (tranform_gizmo_op == ImGuizmo::OPERATION::TRANSLATE) {
                        physics_body_interface.SetPosition(p->body_id, new_position, activation);
                        auto velocity = new_position - last_position;
                        velocity *= physics_fling_modifier;

                        physics_body_interface.SetLinearVelocity(p->body_id, velocity);
                    }

                    if (tranform_gizmo_op == ImGuizmo::OPERATION::ROTATE) {
                        physics_body_interface.SetRotation(
                            p->body_id,
                            JPH::Quat(t->rotation.x, t->rotation.y, t->rotation.z, t->rotation.w),
                            activation
                        );
                    }

                    if (tranform_gizmo_op == ImGuizmo::OPERATION::SCALEU) {
                        auto shape     = physics_body_interface.GetShape(p->body_id);
                        auto new_shape = shape->ScaleShape(JPH::Vec3(scale.x, scale.x, scale.x));
                        if (new_shape.IsValid()) {
                            physics_body_interface.SetShape(p->body_id, new_shape.Get(), false, activation);
                        }
                    }
                }
            }
        }

        ImGui::Render();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();

        VK_CHECK(vkWaitForFences(device, 1, &frame_fences[frame_index], VK_TRUE, UINT64_MAX));
        vkAcquireNextImageKHR(
            device,
            swapchain.handle,
            UINT64_MAX,
            image_available_semaphores[frame_index],
            VK_NULL_HANDLE,
            &swapchain_image_index
        );
        VK_CHECK(vkResetFences(device, 1, &frame_fences[frame_index]));
        vmaSetCurrentFrameIndex(vma_allocator, frame_index);

        float aspect         = camera.viewport_width / camera.viewport_height;
        float tan_half_fov_y = tanf(glm::radians(camera.fov) / 2.0f);
        float tan_half_fov_x = tan_half_fov_y * aspect;

        xegtao_constants.camera_tan_half_fov          = {tan_half_fov_x, tan_half_fov_y};
        xegtao_constants.ndc_to_view_mul              = {tan_half_fov_x * 2.0f, tan_half_fov_y * -2.0f}; // ← Y negative
        xegtao_constants.ndc_to_view_add              = {-tan_half_fov_x, tan_half_fov_y};               // ← Y positive
        xegtao_constants.ndc_to_view_mul_x_pixel_size = {
            xegtao_constants.ndc_to_view_mul.x / (float)swapchain.width,
            xegtao_constants.ndc_to_view_mul.y / (float)swapchain.height
        };
        xegtao_constants.camera_near_far = {camera.near_plane, 10000.0f};

        lighting_data.frame_index += 1;
        lighting_data.camera_pos              = camera.position;
        lighting_data.ddgi_probe_ray_rotation = ddgi_random_rotation();

        SceneUBO scene_ubo        = {};
        scene_ubo.proj            = camera.projection_matrix;
        scene_ubo.camera_position = glm::vec4(camera.position, 1.0);

        scene_ubo.view_proj         = camera.combined_matrix;
        scene_ubo.inverse_view_proj = glm::inverse(camera.combined_matrix);

        scene_ubo.view       = camera.view_matrix;
        scene_ubo.frustum[0] = frustum_x.x;
        scene_ubo.frustum[1] = frustum_x.z;
        scene_ubo.frustum[2] = frustum_y.y;
        scene_ubo.frustum[3] = frustum_y.z;

        scene_ubo.frozen_view       = frozen_view;
        scene_ubo.frozen_frustum[0] = frozen_frustum[0];
        scene_ubo.frozen_frustum[1] = frozen_frustum[1];
        scene_ubo.frozen_frustum[2] = frozen_frustum[2];
        scene_ubo.frozen_frustum[3] = frozen_frustum[3];

        scene_ubo.debug_frustum   = debug_frustum;
        scene_ubo.disable_culling = disable_culling;

        scene_ubo.P00 = camera.projection_matrix[0][0];
        scene_ubo.P11 = camera.projection_matrix[1][1];

        scene_ubo.near_plane = camera.near_plane;
        scene_ubo.far_plane  = 1000.0f;
        scene_ubo.lod_target = (2 / scene_ubo.P11) * (1.0f / float(swapchain.height)) * (1 << min_lod);

        scene_ubo.last_frame_view_proj = last_frame_view_proj;

        {
            void*  scene_ubo_ptr  = nullptr;
            size_t ubo_ptr_offset = (scene_ubo_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
            VK_CHECK(vmaMapMemory(vma_allocator, scene_ubo_buffer.allocation, &scene_ubo_ptr));
            memcpy(reinterpret_cast<char*>(scene_ubo_ptr) + ubo_ptr_offset, &scene_ubo, sizeof(SceneUBO));
            vmaUnmapMemory(vma_allocator, scene_ubo_buffer.allocation);
            VK_CHECK(
                vmaFlushAllocation(vma_allocator, scene_ubo_buffer.allocation, ubo_ptr_offset, scene_ubo_buffer.size)
            );
        }

        {
            void*  lighting_ubo_ptr    = nullptr;
            size_t lighting_ptr_offset = (lighting_ubo_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
            VK_CHECK(vmaMapMemory(vma_allocator, lighting_ubo_buffer.allocation, &lighting_ubo_ptr));
            memcpy(
                reinterpret_cast<char*>(lighting_ubo_ptr) + lighting_ptr_offset, &lighting_data, sizeof(LightingUBO)
            );
            vmaUnmapMemory(vma_allocator, lighting_ubo_buffer.allocation);
            VK_CHECK(vmaFlushAllocation(
                vma_allocator, lighting_ubo_buffer.allocation, lighting_ptr_offset, lighting_ubo_buffer.size
            ));
        }

        debug_renderer_constants.combined_matrix = camera.combined_matrix;
        debug_renderer_start_frame(debug_renderer, frame_index);

        if (visualize_probes) {
            glm::vec3 grid_shift =
                glm::vec3(
                    lighting_data.probe_counts.x - 1, lighting_data.probe_counts.y - 1, lighting_data.probe_counts.z - 1
                ) *
                lighting_data.probe_spacing * 0.5f;

            for (int z = 0; z < lighting_data.probe_counts.z; z++) {
                for (int y = 0; y < lighting_data.probe_counts.y; y++) {
                    for (int x = 0; x < lighting_data.probe_counts.x; x++) {
                        glm::vec3 probe_grid_pos = glm::vec3(
                            x * lighting_data.probe_spacing,
                            y * lighting_data.probe_spacing,
                            z * lighting_data.probe_spacing
                        );

                        glm::vec3 probe_pos = lighting_data.grid_origin + probe_grid_pos - grid_shift;

                        debug_renderer_draw_sphere(
                            debug_renderer, probe_pos, lighting_data.probe_spacing / 10.0f, {1, 1, 1, 1}
                        );
                    }
                }
            }
        }

        glm::vec2 mouse_pos = input_system.get_mouse_position();
        if (input_system.is_key_pressed(Key::C) && !capturing_mouse && coords_in_scene_viewport(mouse_pos)) {
            if (input_system.is_button_just_pressed(Button::LEFT)) {
                if (editor.get_selected_entity() != entt::null) {
                    auto t = scene.get_component<components::Transform>(editor.get_selected_entity());
                    auto m = scene.get_component<components::Mesh>(editor.get_selected_entity());
                    auto n = scene.get_component<components::Name>(editor.get_selected_entity());

                    if (t && m && n) {
                        glm::vec2 pos  = screen_pos_to_scene_vewport(mouse_pos);
                        glm::vec2 frac = pos / glm::vec2(viewport_pos_size.z, viewport_pos_size.w);

                        glm::vec4 mouse_near = {frac * 2.0f - 1.0f, 1, 1.0};
                        mouse_near.y *= -1;
                        mouse_near =
                            glm::inverse(camera.view_matrix) * glm::inverse(camera.projection_matrix) * mouse_near;
                        glm::vec3 near = glm::vec3(mouse_near) / mouse_near.w;

                        glm::vec4 mouse_far = {frac * 2.0f - 1.0f, 0.01, 1.0};
                        mouse_far.y *= -1;
                        mouse_far =
                            glm::inverse(camera.view_matrix) * glm::inverse(camera.projection_matrix) * mouse_far;
                        glm::vec3 far = glm::vec3(mouse_far) / mouse_far.w;

                        glm::vec3 origin  = camera.position;
                        glm::vec3 ray_dir = glm::normalize(far - near);

                        Mesh& mesh = scene.meshes[m->mesh.mesh_id];

                        glm::vec3 half_extent = (mesh.bounds_max - mesh.bounds_min) * 0.5f;
                        half_extent *= t->scale;

                        float radius = glm::max(half_extent.x, glm::max(half_extent.y, half_extent.z));

                        float ratios[3];
                        for (int i = 0; i < 3; i++) {
                            ratios[i] = half_extent[i] / radius;
                        }

                        enum class Shape {
                            SPHERE,
                            BOX,
                            CAPSULE,
                        };

                        Shape shape = Shape::SPHERE;

                        const float capsule_ratio     = 0.6;
                        const float similar_threshold = 1.3;
                        int         capsule_axis      = -1;

                        bool is_capsule = false;
                        for (int dominant = 0; dominant < 3; dominant++) {
                            int other1 = (dominant + 1) % 3;
                            int other2 = (dominant + 2) % 3;

                            if (ratios[dominant] > capsule_ratio && ratios[dominant] * capsule_ratio > ratios[other1] &&
                                ratios[dominant] * capsule_ratio > ratios[other2] &&
                                glm::max(ratios[other1], ratios[other2]) / glm::min(ratios[other1], ratios[other2]) <
                                    similar_threshold) {
                                is_capsule   = true;
                                capsule_axis = dominant;
                                break;
                            }
                        }

                        if (is_capsule) {
                            shape = Shape::CAPSULE;
                        } else {
                            for (int i = 0; i < 3; i++) {
                                if (ratios[i] < 0.85) {
                                    shape = Shape::BOX;
                                    break;
                                }
                            }
                        }

                        glm::vec3 spawn_point = origin + ray_dir * radius * 2.0f;

                        auto physics_spawn_point = JPH::RVec3(spawn_point.x, spawn_point.y, spawn_point.z);
                        auto physics_rotation    = JPH::Quat::sIdentity();

                        JPH::BodyCreationSettings body_settings;
                        switch (shape) {
                        case Shape::SPHERE:
                            body_settings = JPH::BodyCreationSettings(
                                new JPH::SphereShape(radius),
                                physics_spawn_point,
                                physics_rotation,
                                JPH::EMotionType::Dynamic,
                                Layers::MOVING
                            );
                            break;
                        case Shape::BOX:
                            body_settings = JPH::BodyCreationSettings(
                                new JPH::BoxShape(JPH::RVec3(half_extent.x, half_extent.y, half_extent.z)),
                                physics_spawn_point,
                                physics_rotation,
                                JPH::EMotionType::Dynamic,
                                Layers::MOVING
                            );
                            break;
                        case Shape::CAPSULE:
                            float height = half_extent[capsule_axis];

                            int   other1     = (capsule_axis + 1) % 3;
                            int   other2     = (capsule_axis + 2) % 3;
                            float cap_radius = glm::max(half_extent[other1], half_extent[other2]);

                            JPH::Quat rotation = JPH::Quat::sIdentity();
                            if (capsule_axis == 0) {
                                rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), JPH::DegreesToRadians(90));
                            } else if (capsule_axis == 2) {
                                rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), JPH::DegreesToRadians(90));
                            }
                            JPH::CapsuleShape*           capsule = new JPH::CapsuleShape(height, cap_radius);
                            JPH::RotatedTranslatedShape* rotated_capsule =
                                new JPH::RotatedTranslatedShape(JPH::Vec3::sZero(), rotation, capsule);

                            body_settings = JPH::BodyCreationSettings(
                                rotated_capsule,
                                physics_spawn_point,
                                physics_rotation,
                                JPH::EMotionType::Dynamic,
                                Layers::MOVING
                            );
                            break;
                        }

                        body_settings.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateInertia;
                        body_settings.mMassPropertiesOverride.mMass = physics_spawn_mass;

                        JPH::BodyID body_id =
                            physics_body_interface.CreateAndAddBody(body_settings, JPH::EActivation::Activate);
                        physics_body_interface.SetLinearVelocity(
                            body_id, JPH::Vec3(ray_dir.x, ray_dir.y, ray_dir.z) * 10
                        );
                        physics_body_interface.SetFriction(body_id, physics_spawn_mass);
                        physics_body_interface.SetRestitution(body_id, physics_spawn_restitution);

                        Entity new_entity       = scene.create_entity(n->name + "_copy");
                        auto   new_transform    = scene.get_component<components::Transform>(new_entity);
                        new_transform->position = spawn_point;
                        new_transform->scale    = t->scale;
                        new_transform->rotation = t->rotation;

                        auto& new_mesh = scene.add_component<components::Mesh>(new_entity);
                        new_mesh.mesh  = m->mesh;

                        auto& new_physics      = scene.add_component<components::Physics>(new_entity);
                        new_physics.body_id    = body_id;
                        new_physics.is_static  = false;
                        new_physics.last_scale = new_transform->scale;
                    }
                }
            }
        }

        {
            void*  ptr          = nullptr;
            size_t frame_offset = (drawcall_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
            VK_CHECK(vmaMapMemory(vma_allocator, drawcall_buffer.allocation, &ptr));
            memcpy(
                reinterpret_cast<char*>(ptr) + frame_offset,
                mesh_instances.data(),
                sizeof(MeshInstance) * mesh_instances.size()
            );
            vmaUnmapMemory(vma_allocator, drawcall_buffer.allocation);
            VK_CHECK(vmaFlushAllocation(vma_allocator, drawcall_buffer.allocation, frame_offset, drawcall_buffer.size));
        }

        {
            void*  ptr          = nullptr;
            size_t frame_offset = (mesh_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
            VK_CHECK(vmaMapMemory(vma_allocator, mesh_buffer.allocation, &ptr));
            memcpy(
                reinterpret_cast<char*>(ptr) + frame_offset, scene.meshes.data(), sizeof(Mesh) * scene.meshes.size()
            );
            vmaUnmapMemory(vma_allocator, mesh_buffer.allocation);
            VK_CHECK(vmaFlushAllocation(vma_allocator, mesh_buffer.allocation, frame_offset, mesh_buffer.size));
        }

        {
            void*  ptr          = nullptr;
            size_t frame_offset = (material_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
            VK_CHECK(vmaMapMemory(vma_allocator, material_buffer.allocation, &ptr));
            memcpy(
                reinterpret_cast<char*>(ptr) + frame_offset,
                scene.materials.data(),
                sizeof(Material) * scene.materials.size()
            );
            vmaUnmapMemory(vma_allocator, material_buffer.allocation);
            VK_CHECK(vmaFlushAllocation(vma_allocator, material_buffer.allocation, frame_offset, material_buffer.size));
        }

        VkCommandBufferBeginInfo begin_info = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .pInheritanceInfo = nullptr
        };

        VkCommandBuffer command_buffer = command_buffers[frame_index];
        vkBeginCommandBuffer(command_buffer, &begin_info);

        vkCmdResetQueryPool(command_buffer, statistics_pools[frame_index], 0, 1);

        VkViewport viewport = {
            .x        = 0,
            .y        = static_cast<float>(swapchain.height),
            .width    = static_cast<float>(swapchain.width),
            .height   = -static_cast<float>(swapchain.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };

        VkRect2D scissor = {.offset = {.x = 0, .y = 0}, .extent = {swapchain.width, swapchain.height}};

        vkCmdSetViewport(command_buffer, 0, 1, &viewport);
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);

        size_t command_ptr_frame_offset = (indirect_command_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        vkCmdFillBuffer(command_buffer, indirect_command_buffer.handle, command_ptr_frame_offset, sizeof(uint32_t), 0);

        buffer_pipeline_barrier(
            indirect_command_buffer,
            command_buffer,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            command_ptr_frame_offset,
            indirect_command_buffer.size / FRAMES_IN_FLIGHT
        );

        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::SCENE_UBO)] =
            (scene_ubo_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::INDIRECT_COMMAND_BUFFER)] =
            (indirect_command_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)] =
            (lighting_ubo_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MESH_BUFFER)] =
            (mesh_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MATERIAL_BUFFER)] =
            (material_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DRAWCALL_BUFFER)] =
            (drawcall_buffer.size / FRAMES_IN_FLIGHT) * frame_index;

        framegraph.execute(command_buffer, frame_index);

        if (input_system.is_key_pressed(Key::LEFT_SHIFT) && !capturing_mouse && coords_in_scene_viewport(mouse_pos)) {
            if (input_system.is_button_just_pressed(Button::LEFT)) {
                glm::vec2 pos  = screen_pos_to_scene_vewport(mouse_pos);
                glm::vec2 frac = pos / glm::vec2(viewport_pos_size.z, viewport_pos_size.w);

                int mouse_x = glm::floor(frac.x * gbuffer_id.width);
                int mouse_y = glm::floor(frac.y * gbuffer_id.height);

                VkBufferImageCopy region = {
                    .bufferOffset      = 0,
                    .bufferRowLength   = 0,
                    .bufferImageHeight = 0,
                    .imageSubresource =
                        {
                            .aspectMask     = gbuffer_id.aspect,
                            .mipLevel       = 0,
                            .baseArrayLayer = 0,
                            .layerCount     = 1,
                        },
                    .imageOffset =
                        {
                            .x = mouse_x,
                            .y = mouse_y,
                            .z = 0,
                        },
                    .imageExtent = {
                        .width  = 1,
                        .height = 1,
                        .depth  = 1,
                    }
                };

                image_pipeline_barrier(
                    gbuffer_id,
                    command_buffer,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT
                );

                vkCmdCopyImageToBuffer(
                    command_buffer,
                    gbuffer_id.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    pick_buffer.handle,
                    1,
                    &region
                );

                image_pipeline_barrier(
                    gbuffer_id,
                    command_buffer,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                );

                pick_frame = frame_count + FRAMES_IN_FLIGHT + 1;
            }
        }

        TracyVkCollect(tracy_vk_context, command_buffer);
        VK_CHECK(vkEndCommandBuffer(command_buffer));

        VkPipelineStageFlags wait_stage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo         submit_info = {
                    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .pNext                = nullptr,
                    .waitSemaphoreCount   = 1,
                    .pWaitSemaphores      = &image_available_semaphores[frame_index],
                    .pWaitDstStageMask    = &wait_stage,
                    .commandBufferCount   = 1,
                    .pCommandBuffers      = &command_buffer,
                    .signalSemaphoreCount = 1,
                    .pSignalSemaphores    = &render_finished_semaphores[swapchain_image_index]
        };
        VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit_info, frame_fences[frame_index]));

        VkPresentInfoKHR present_info = {
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext              = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &render_finished_semaphores[swapchain_image_index],
            .swapchainCount     = 1,
            .pSwapchains        = &swapchain.handle,
            .pImageIndices      = &swapchain_image_index,
            .pResults           = nullptr
        };
        VK_CHECK(vkQueuePresentKHR(graphics_queue, &present_info));

        framegraph.gather_timestamp_queries(device, physical_device_properties.limits.timestampPeriod);

        frame_index = (frame_index + 1) % FRAMES_IN_FLIGHT;
        frame_count++;

        if (frame_count > FRAMES_IN_FLIGHT) {
            auto result = vkGetQueryPoolResults(
                device,
                statistics_pools[frame_index],
                0,
                1,
                pipeline_stats.size() * sizeof(uint64_t),
                &pipeline_stats[0],
                0,
                VK_QUERY_RESULT_64_BIT
            );
        }

        last_frame_view_proj = camera.combined_matrix;

        {
            auto view = scene.entity_registry.view<components::Transform, components::Mesh>();
            for (auto [e, t, m] : view.each()) {
                m.mesh.last_position = m.mesh.position;
                m.mesh.last_scale    = m.mesh.scale;
                m.mesh.last_rotation = m.mesh.rotation;
            }
        }

        input_system.update_key_states();
    }

    VK_CHECK(vkDeviceWaitIdle(device));

    spdlog::info("Cleaning up");

    TracyVkDestroy(tracy_vk_context);

    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplVulkan_Shutdown();

    framegraph.destroy(device);

    scene.destroy_scene(device, vma_allocator);

    destroy_swapchain(swapchain, window, instance, device);
    destroy_buffer(staging_buffer, device, vma_allocator);
    destroy_buffer(global_index_buffer, device, vma_allocator);
    destroy_buffer(global_vertex_buffer, device, vma_allocator);
    destroy_buffer(indirect_command_buffer, device, vma_allocator);
    destroy_buffer(meshlet_bounds_buffer, device, vma_allocator);
    destroy_buffer(meshlet_primitive_indices_buffer, device, vma_allocator);
    destroy_buffer(meshlet_vertex_indices_buffer, device, vma_allocator);
    destroy_buffer(meshlet_buffer, device, vma_allocator);
    destroy_buffer(scene_ubo_buffer, device, vma_allocator);
    destroy_buffer(lighting_ubo_buffer, device, vma_allocator);
    destroy_buffer(ddgi_ray_buffer, device, vma_allocator);
    destroy_buffer(ddgi_probe_buffer, device, vma_allocator);
    destroy_buffer(material_buffer, device, vma_allocator);
    destroy_buffer(drawcall_buffer, device, vma_allocator);
    destroy_buffer(mesh_buffer, device, vma_allocator);
    destroy_buffer(pick_buffer, device, vma_allocator);
    destroy_buffer(luminance_buffer, device, vma_allocator);
    destroy_buffer(indirect_dispatch_tile_copy_buffer, device, vma_allocator);
    destroy_buffer(indirect_dispatch_tile_process_buffer, device, vma_allocator);
    destroy_image(depth_buffer, device, vma_allocator);
    destroy_image(lightpass_output, device, vma_allocator);
    destroy_image(composite_output, device, vma_allocator);
    destroy_image(gbuffer_albedo, device, vma_allocator);
    destroy_image(gbuffer_normals, device, vma_allocator);
    destroy_image(gbuffer_emissive, device, vma_allocator);
    destroy_image(gbuffer_id, device, vma_allocator);
    destroy_image(gbuffer_velocity, device, vma_allocator);
    destroy_image(depth_hiz, device, vma_allocator);
    destroy_image(bloom_buffer, device, vma_allocator);
    destroy_image(ao_output, device, vma_allocator);
    destroy_image(ao_output_denoised, device, vma_allocator);
    destroy_image(ao_output_edges, device, vma_allocator);
    destroy_image(ao_prefiltered_depth, device, vma_allocator);
    destroy_image(brdf_lut, device, vma_allocator);
    destroy_image(ddgi_irradiance, device, vma_allocator);
    destroy_image(ddgi_irradiance_history, device, vma_allocator);
    destroy_image(ddgi_depth_atlas, device, vma_allocator);
    destroy_image(ddgi_depth_atlas_history, device, vma_allocator);
    destroy_image(directional_shadow_buffer, device, vma_allocator);
    destroy_image(directional_shadow_buffer_pong, device, vma_allocator);
    destroy_image(smaa_area_tex, device, vma_allocator);
    destroy_image(smaa_search_tex, device, vma_allocator);
    destroy_image(smaa_edges, device, vma_allocator);
    destroy_image(smaa_output, device, vma_allocator);
    destroy_image(smaa_weights, device, vma_allocator);
    destroy_image(rt_reflection_buffer, device, vma_allocator);
    destroy_image(rt_reflection_history, device, vma_allocator);
    destroy_image(average_luminance_image, device, vma_allocator);
    destroy_image(hilbert_lut, device, vma_allocator);

    for (auto view : depth_mip_views) {
        vkDestroyImageView(device, view, nullptr);
    }

    for (auto view : ao_depth_mip_views) {
        vkDestroyImageView(device, view, nullptr);
    }

    for (auto view : bloom_mip_views) {
        vkDestroyImageView(device, view, nullptr);
    }

    for (auto view : rt_reflection_views) {
        vkDestroyImageView(device, view, nullptr);
    }

    destroy_pipeline(device, cull_pipeline);
    destroy_pipeline(device, gpass_pipeline);
    destroy_pipeline(device, hiz_pipeline);
    destroy_pipeline(device, light_pipeline);
    destroy_pipeline(device, bloom_downsample_pipeline);
    destroy_pipeline(device, bloom_upsample_pipeline);
    destroy_pipeline(device, composite_pipeline);
    destroy_pipeline(device, ao_prefilter_pipeline);
    destroy_pipeline(device, ao_pipeline);
    destroy_pipeline(device, ao_denoise_pipeline);
    destroy_pipeline(device, ddgi_ray_pipeline);
    destroy_pipeline(device, ddgi_probe_blend_irradiance_pipeline);
    destroy_pipeline(device, ddgi_probe_blend_depth_pipeline);
    destroy_pipeline(device, ddgi_probe_classify_pipeline);
    destroy_pipeline(device, ddgi_probe_relocate_pipeline);
    destroy_pipeline(device, smaa_edge_pipeline);
    destroy_pipeline(device, smaa_blend_pipeline);
    destroy_pipeline(device, smaa_weights_pipeline);
    destroy_pipeline(device, shadow_pipeline);
    destroy_pipeline(device, shadow_fill_pipeline);
    destroy_pipeline(device, shadow_blur_pipeline);
    destroy_pipeline(device, rt_reflection_pipeline);
    destroy_pipeline(device, rt_reflection_upsample);
    destroy_pipeline(device, luminance_histogram_pipeline);
    destroy_pipeline(device, luminance_average_pipeline);
    destroy_pipeline(device, tile_clear_pipeline);
    destroy_pipeline(device, reflection_tile_copy_pipeline);

    if (use_hardware_rt) {
        destroy_rt_scene(rt_scene, device, vma_allocator);
    }

    destroy_debug_renderer(debug_renderer, device, vma_allocator);

    vmaDestroyAllocator(vma_allocator);

    vkDestroyDescriptorSetLayout(device, global_texture_descriptor_layout, nullptr);
    vkDestroySampler(device, linear_sampler, nullptr);
    vkDestroySampler(device, linear_sampler_clamped, nullptr);
    vkDestroySampler(device, nearest_sampler, nullptr);
    vkDestroySampler(device, depth_sampler, nullptr);
    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    vkDestroyDescriptorPool(device, imgui_descriptor_pool, nullptr);
    vkDestroyCommandPool(device, command_pool, nullptr);
    for (int i = 0; i < swapchain.images.size(); i++) {
        vkDestroySemaphore(device, image_available_semaphores[i], nullptr);
        vkDestroySemaphore(device, render_finished_semaphores[i], nullptr);
    }
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        vkDestroyFence(device, frame_fences[i], nullptr);
        vkDestroyQueryPool(device, statistics_pools[i], nullptr);
    }
    if (debug_messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
    }
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
