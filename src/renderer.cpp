#include "renderer.hpp"

#include "device.hpp"
#include "framegraph.hpp"
#include "geometry.hpp"
#include "resources.hpp"
#include "ui.hpp"
#include "world.hpp"

#include <random>

#include <ImGuizmo.h>
#include <SDL3/SDL.h>
#include <glm/gtc/random.hpp>
#include <tracy/TracyVulkan.hpp>

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
void Renderer::destroy_debug_renderer(const DebugRenderer& renderer, VkDevice device, VmaAllocator vma_allocator) {
    destroy_buffer(renderer.vertex_buffer, device, vma_allocator);
    destroy_buffer(renderer.index_buffer, device, vma_allocator);
    destroy_buffer(renderer.instance_buffer, device, vma_allocator);

    destroy_pipeline(device, renderer.pipeline);
}

Renderer::DebugRenderer Renderer::create_debug_renderer(
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

    VkVertexInputBindingDescription vertex_binding_description = {
        .binding   = 0,
        .stride    = sizeof(float) * 8,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    std::array<VkVertexInputAttributeDescription, 3> attribute_desriptions = {
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = 0,

        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = sizeof(float) * 3,
        },
        VkVertexInputAttributeDescription{
            .location = 2,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = sizeof(float) * 6,
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext                           = nullptr,
        .flags                           = 0,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &vertex_binding_description,
        .vertexAttributeDescriptionCount = attribute_desriptions.size(),
        .pVertexAttributeDescriptions    = attribute_desriptions.data()
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    Pipeline pipeline = create_graphics_pipeline(
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
        vertex_input_state,
        input_assembly_state,
        false,
        depth_format,
        true,
        true,
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

void Renderer::debug_renderer_draw_sphere(
    DebugRenderer& renderer, const glm::vec3& center, float radius, const glm::vec4& color
) {
    renderer.instances[renderer.instance_count++] = glm::vec4(center, radius);
}

void Renderer::debug_renderer_upload_data(DebugRenderer& renderer, VmaAllocator vma_allocator, uint32_t frame_index) {
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

void Renderer::debug_renderer_start_frame(DebugRenderer& renderer, uint32_t frame_index) {
    renderer.instance_count = 0;
}

Renderer::Renderer() {
}

void Renderer::cleanup() {
    TracyVkDestroy(tracy_vk_context);

    framegraph->destroy(device);
    delete framegraph;

    destroy_buffer(world_sprite_batcher->drawcall_buffer, device, vma_allocator);
    destroy_buffer(world_sprite_batcher->geometry_buffer, device, vma_allocator);
    destroy_pipeline(device, world_sprite_batcher->geometry_build_pipline);
    destroy_pipeline(device, world_sprite_pipeline);
    delete world_sprite_batcher;

    destroy_buffer(ui_sprite_batcher->drawcall_buffer, device, vma_allocator);
    destroy_buffer(ui_sprite_batcher->geometry_buffer, device, vma_allocator);
    destroy_pipeline(device, ui_sprite_batcher->geometry_build_pipline);
    destroy_pipeline(device, ui_sprite_pipeline);
    delete ui_sprite_batcher;

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
    destroy_buffer(luminance_buffer, device, vma_allocator);
    destroy_buffer(indirect_dispatch_tile_copy_buffer, device, vma_allocator);
    destroy_buffer(indirect_dispatch_tile_process_buffer, device, vma_allocator);
    destroy_buffer(particle_position_buffer, device, vma_allocator);
    destroy_buffer(particle_velocity_buffer, device, vma_allocator);

    destroy_image(ui_buffer, device, vma_allocator);
    destroy_image(depth_buffer, device, vma_allocator);
    destroy_image(lightpass_output, device, vma_allocator);
    destroy_image(composite_output, device, vma_allocator);
    destroy_image(gbuffer_albedo, device, vma_allocator);
    destroy_image(gbuffer_normals, device, vma_allocator);
    destroy_image(gbuffer_emissive, device, vma_allocator);
    destroy_image(gbuffer_id, device, vma_allocator);
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
    destroy_pipeline(device, smaa_edge_pipeline);
    destroy_pipeline(device, smaa_blend_pipeline);
    destroy_pipeline(device, smaa_weights_pipeline);
    destroy_pipeline(device, luminance_histogram_pipeline);
    destroy_pipeline(device, luminance_average_pipeline);
    destroy_pipeline(device, particle_render_pipeline);
    destroy_pipeline(device, particle_update_pipeline);

    if (this->hardware_rt_enabled) {
        destroy_rt_scene(rt_scene, device, vma_allocator);

        destroy_pipeline(device, ddgi_ray_pipeline);
        destroy_pipeline(device, ddgi_probe_blend_irradiance_pipeline);
        destroy_pipeline(device, ddgi_probe_blend_depth_pipeline);
        destroy_pipeline(device, ddgi_probe_classify_pipeline);
        destroy_pipeline(device, ddgi_probe_relocate_pipeline);
        destroy_pipeline(device, shadow_pipeline);
        destroy_pipeline(device, shadow_fill_pipeline);
        destroy_pipeline(device, shadow_blur_pipeline);
        destroy_pipeline(device, rt_reflection_pipeline);
        destroy_pipeline(device, rt_reflection_upsample);
        destroy_pipeline(device, tile_clear_pipeline);
        destroy_pipeline(device, reflection_tile_copy_pipeline);
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
    vkDestroyCommandPool(device, temporary_command_pool, nullptr);
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
}

void Renderer::setup_framegraph() {
    spdlog::info("Creating framegraph");
    framegraph = new Framegraph(
        device, graphics_queue, command_buffers[0], FRAMES_IN_FLIGHT, supports_timestamp_queries, tracy_vk_context
    );
    framegraph->import_image(depth_hiz, VK_IMAGE_LAYOUT_GENERAL);
    framegraph->import_image(depth_buffer, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    framegraph->import_image(gbuffer_albedo, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    framegraph->import_image(gbuffer_normals, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    framegraph->import_image(gbuffer_emissive, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    framegraph->import_image(gbuffer_id, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    framegraph->import_image(lightpass_output, VK_IMAGE_LAYOUT_GENERAL);
    framegraph->import_image(composite_output, VK_IMAGE_LAYOUT_GENERAL);
    framegraph->import_image(bloom_buffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    framegraph->import_image(ao_output, VK_IMAGE_LAYOUT_GENERAL);
    framegraph->import_image(ao_output_edges, VK_IMAGE_LAYOUT_GENERAL);
    framegraph->import_image(ao_output_denoised, VK_IMAGE_LAYOUT_GENERAL);
    framegraph->import_image(ao_prefiltered_depth, VK_IMAGE_LAYOUT_GENERAL);
    framegraph->import_image(directional_shadow_buffer, VK_IMAGE_LAYOUT_GENERAL, false);
    framegraph->import_image(directional_shadow_buffer_pong, VK_IMAGE_LAYOUT_GENERAL);
    framegraph->import_image(smaa_edges, VK_IMAGE_LAYOUT_GENERAL);
    framegraph->import_image(smaa_weights, VK_IMAGE_LAYOUT_GENERAL);
    framegraph->import_image(smaa_output, VK_IMAGE_LAYOUT_GENERAL);
    framegraph->import_image(ddgi_irradiance, VK_IMAGE_LAYOUT_GENERAL, false);
    framegraph->import_image(ddgi_irradiance_history, VK_IMAGE_LAYOUT_GENERAL);
    framegraph->import_image(ddgi_depth_atlas, VK_IMAGE_LAYOUT_GENERAL, false);
    framegraph->import_image(ddgi_depth_atlas_history, VK_IMAGE_LAYOUT_GENERAL);
    framegraph->import_image(average_luminance_image, VK_IMAGE_LAYOUT_GENERAL, false);
    framegraph->import_image(rt_reflection_buffer, VK_IMAGE_LAYOUT_GENERAL, false);

    if (this->hardware_rt_enabled) {
        auto tlas_rebuild_pass = framegraph->add_pass("RT structure rebuild")
                                     .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                                         if (world->needs_blas_rebuild) {
                                             rebuild_blas(
                                                 rt_scene,
                                                 world,
                                                 frame_index,
                                                 command_buffer,
                                                 get_buffer_device_address(global_vertex_buffer, device),
                                                 get_buffer_device_address(global_index_buffer, device)
                                             );
                                             world->needs_blas_rebuild = false;
                                         }

                                         rebuild_tlas(
                                             rt_scene,
                                             device,
                                             vma_allocator,
                                             command_buffer,
                                             frame_index,
                                             world->resources.meshes,
                                             mesh_instances
                                         );
                                     });
    }

    auto cull_early_pass =
        framegraph->add_pass("cull early")
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

    auto sprite_build_pass =
        framegraph->add_pass("sprite batch build")
            .reads_buffer_dynamic(
                world_sprite_batcher->drawcall_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                world_sprite_batcher->drawcall_buffer.size / FRAMES_IN_FLIGHT
            )
            .writes_buffer(
                world_sprite_batcher->geometry_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_WRITE_BIT
            )
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                world_sprite_batcher->build_geometry_buffer(vma_allocator, command_buffer, frame_index);
                ui_sprite_batcher->build_geometry_buffer(vma_allocator, command_buffer, frame_index);
            });

    auto gbuffer_pass =
        framegraph->add_pass("gbuffer")
            .writes_depth_attachment(depth_buffer)
            .writes_color_attachment(gbuffer_albedo)
            .writes_color_attachment(gbuffer_normals)
            .writes_color_attachment(gbuffer_emissive)
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
            .reads_buffer(
                world_sprite_batcher->geometry_buffer,
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT
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
                {
                    auto&     pipeline = world_sprite_pipeline;
                    glm::mat4 push     = camera->combined_matrix;

                    std::array<VkDescriptorSet, 2> descriptors = {
                        world_sprite_pipeline_descriptor_sets[0], global_texture_descriptor_set
                    };

                    std::array<uint32_t, 4> offsets = {
                        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::INDIRECT_COMMAND_BUFFER)],
                        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DRAWCALL_BUFFER)],
                        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MESH_BUFFER)],
                        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MATERIAL_BUFFER)],
                    };

                    vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);
                    vkCmdBindDescriptorSets(
                        command_buffer,
                        pipeline.bind_point,
                        pipeline.pipeline_layout,
                        0,
                        descriptors.size(),
                        descriptors.data(),
                        offsets.size(),
                        offsets.data()
                    );
                    vkCmdPushConstants(
                        command_buffer, pipeline.pipeline_layout, pipeline.stage_flags, 0, sizeof(glm::mat4), &push
                    );
                    VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(
                        command_buffer, 0, 1, &world_sprite_batcher->geometry_buffer.handle, &offset
                    );
                    vkCmdDraw(command_buffer, world_sprite_batcher->drawcall_count * 6, 1, 0, 0);
                }

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

                if (this->meshlets_enabled) {
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

    auto depth_pyramid_pass = framegraph->add_pass("depth pyramid")
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

                                          depth_reduce_constants = {
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
                                              &depth_reduce_constants
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

    auto ao_prefilter_pass =
        framegraph->add_pass("ao prefilter depth")
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

    auto ao_pass = framegraph->add_pass("ao")
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
        framegraph->add_pass("ao denoise")
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

    if (this->hardware_rt_enabled) {
        ddgi_ray_pipeline = create_compute_pipeline(
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
        ddgi_ray_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, ddgi_ray_pipeline);

        auto& ddgi_ray_pass =
            framegraph->add_pass("ddgi ray")
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

                    vkCmdDispatch(
                        command_buffer, glm::ceil((probe_count * lighting_data.rays_per_probe) / 32.0f), 1, 1
                    );
                });

        ddgi_probe_blend_depth_pipeline = create_compute_pipeline(
            device,
            shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/ddgi_blend_depth.comp.spv"),
            {
                DescriptorLayout{
                    .bindings = {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(ddgi_ray_buffer.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(ddgi_probe_buffer.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                            .write_info = DescriptorInfo(
                                lighting_ubo_buffer.handle, 0, lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
                            )
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
        ddgi_probe_blend_depth_descriptor_sets =
            allocate_descriptor_sets(device, descriptor_pool, ddgi_probe_blend_depth_pipeline);

        auto& ddgi_probe_blend_depth_pass =
            framegraph->add_pass("ddgi blend depth")
                .reads_buffer(
                    ddgi_ray_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT
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
                .writes_storage_image(ddgi_depth_atlas, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                .reads_storage_image(ddgi_depth_atlas_history, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                    const Pipeline& pipeline = ddgi_probe_blend_depth_pipeline;

                    std::array<uint32_t, 1> offsets = {
                        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)]
                    };

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
                            {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                             .mipLevel       = 0,
                             .baseArrayLayer = 0,
                             .layerCount     = 1},
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

        ddgi_probe_blend_irradiance_pipeline = create_compute_pipeline(
            device,
            shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/ddgi_blend_irradiance.comp.spv"),
            {
                DescriptorLayout{
                    .bindings = {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(ddgi_ray_buffer.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(ddgi_probe_buffer.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                            .write_info = DescriptorInfo(
                                lighting_ubo_buffer.handle, 0, lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
                            )
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
        ddgi_probe_blend_irradiance_descriptor_sets =
            allocate_descriptor_sets(device, descriptor_pool, ddgi_probe_blend_irradiance_pipeline);

        auto& ddgi_probe_blend_irradiance_pass =
            framegraph->add_pass("ddgi blend irradiance")
                .reads_buffer(
                    ddgi_ray_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT
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
                .writes_storage_image(ddgi_irradiance, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                .reads_storage_image(ddgi_irradiance_history, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                    const Pipeline& pipeline = ddgi_probe_blend_irradiance_pipeline;

                    std::array<uint32_t, 1> offsets = {
                        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)]
                    };

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
                            {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                             .mipLevel       = 0,
                             .baseArrayLayer = 0,
                             .layerCount     = 1},
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

        ddgi_probe_relocate_pipeline = create_compute_pipeline(
            device,
            shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/ddgi_relocate.comp.spv"),
            {
                DescriptorLayout{
                    .bindings = {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(ddgi_ray_buffer.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(ddgi_probe_buffer.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                            .write_info = DescriptorInfo(
                                lighting_ubo_buffer.handle, 0, lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
                            )
                        },
                    },
                },
            }
        );
        ddgi_probe_relocate_descriptor_sets =
            allocate_descriptor_sets(device, descriptor_pool, ddgi_probe_relocate_pipeline);

        auto& ddgi_probe_relocate_pass =
            framegraph->add_pass("ddgi probe relocate")
                .reads_buffer(
                    ddgi_ray_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                )
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

                    std::array<uint32_t, 1> offsets = {
                        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)]
                    };

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

        ddgi_probe_classify_pipeline = create_compute_pipeline(
            device,
            shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/ddgi_classify.comp.spv"),
            {
                DescriptorLayout{
                    .bindings = {
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(ddgi_ray_buffer.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                            .write_info = DescriptorInfo(ddgi_probe_buffer.handle)
                        },
                        DescriptorBinding{
                            .type       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                            .write_info = DescriptorInfo(
                                lighting_ubo_buffer.handle, 0, lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
                            )
                        },
                    },
                },
            }
        );
        ddgi_probe_classify_descriptor_sets =
            allocate_descriptor_sets(device, descriptor_pool, ddgi_probe_classify_pipeline);

        auto& ddgi_probe_classify_pass =
            framegraph->add_pass("ddgi probe classify")
                .reads_buffer(
                    ddgi_ray_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT
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
                    const Pipeline& pipeline = ddgi_probe_classify_pipeline;

                    std::array<uint32_t, 1> offsets = {
                        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)]
                    };

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

        tile_clear_pipeline = create_compute_pipeline(
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
        tile_clear_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, tile_clear_pipeline);

        auto& tile_clear_pass =
            framegraph->add_pass("Tile Clear")
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

        rt_reflection_pipeline = create_compute_pipeline(
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
                                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .write_info = DescriptorInfo(
                                    linear_sampler, brdf_lut.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
        rt_reflection_pass_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, rt_reflection_pipeline);

        auto& rt_reflection_pass =
            framegraph->add_pass("RT Reflection")
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
                    indirect_dispatch_tile_copy_buffer,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_WRITE_BIT
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

        reflection_tile_copy_pipeline = create_compute_pipeline(
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
        reflection_tile_copy_descriptor_sets =
            allocate_descriptor_sets(device, descriptor_pool, reflection_tile_copy_pipeline);

        auto& reflection_tile_copy_pass =
            framegraph->add_pass("Reflection Tile Copy")
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

        rt_reflection_upsample = create_compute_pipeline(
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
                                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .write_info = DescriptorInfo(
                                    linear_sampler_clamped, rt_reflection_views[1], VK_IMAGE_LAYOUT_GENERAL
                                )
                            },
                            DescriptorBinding{
                                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .write_info = DescriptorInfo(
                                    linear_sampler_clamped,
                                    gbuffer_normals.view,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
        rt_upsample_pass_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, rt_reflection_upsample);

        auto& rt_upsample_pass =
            framegraph->add_pass("RT Upsample")
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

                    glossy_rt_constants = {
                        .last_frame_view_proj = last_frame_view_proj,
                        .frame_index          = frame_count,
                    };

                    vkCmdPushConstants(
                        command_buffer,
                        pipeline.pipeline_layout,
                        pipeline.stage_flags,
                        0,
                        sizeof(GlossyRTConstants),
                        &glossy_rt_constants
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
                            {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                             .mipLevel       = 0,
                             .baseArrayLayer = 0,
                             .layerCount     = 1},
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

        shadow_pipeline = create_compute_pipeline(
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
        shadow_pass_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, shadow_pipeline);

        auto& shadow_pass = framegraph->add_pass("RT Shadows")
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

        shadow_fill_pipeline = create_compute_pipeline(
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
                            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            .write_info = DescriptorInfo(
                                linear_sampler, depth_buffer.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            )
                        },
                    }
                },
            }
        );
        shadow_fill_pass_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, shadow_fill_pipeline);

        auto& shadow_fill_pass =
            framegraph->add_pass("RT Shadow fill")
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

        shadow_blur_pipeline = create_compute_pipeline(
            device,
            shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/shadow_blur.comp.spv"),
            {
                DescriptorLayout{
                    .bindings =
                        {
                            DescriptorBinding{
                                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                .write_info =
                                    DescriptorInfo(directional_shadow_buffer_pong.view, VK_IMAGE_LAYOUT_GENERAL)
                            },
                            DescriptorBinding{
                                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .write_info = DescriptorInfo(
                                    linear_sampler, directional_shadow_buffer.view, VK_IMAGE_LAYOUT_GENERAL
                                )
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
            framegraph->add_pass("RT Shadow blur")
                .samples_image(depth_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                .reads_storage_image(directional_shadow_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                .writes_storage_image(directional_shadow_buffer_pong, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
                .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                    const Pipeline& pipeline = shadow_blur_pipeline;

                    vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);

                    for (int i = 0; i < 2; i++) {
                        VkDescriptorImageInfo write_info = {
                            .sampler = VK_NULL_HANDLE,
                            .imageView =
                                i % 2 == 0 ? directional_shadow_buffer_pong.view : directional_shadow_buffer.view,
                            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                        };

                        VkDescriptorImageInfo read_info = {
                            .sampler = linear_sampler_clamped,
                            .imageView =
                                i % 2 == 0 ? directional_shadow_buffer.view : directional_shadow_buffer_pong.view,
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

                        shadow_blur_constants = {
                            .image_size = glm::vec2(directional_shadow_buffer.width, directional_shadow_buffer.height),
                            .direction  = static_cast<float>(i % 2 == 0 ? 0 : 1),
                            .znear      = camera->near_plane
                        };

                        vkCmdPushConstants(
                            command_buffer,
                            pipeline.pipeline_layout,
                            VK_SHADER_STAGE_COMPUTE_BIT,
                            0,
                            sizeof(ShadowBlurConstants),
                            &shadow_blur_constants
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
    }

    auto& light_pass =
        framegraph->add_pass("lighting")
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

    particle_update_pipeline = create_compute_pipeline(
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
    particle_update_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, particle_update_pipeline);

    auto& particle_update_pass =
        framegraph->add_pass("particle update")
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

                vkCmdDispatch(command_buffer, (particle_count + 255) / 256, 1, 1);
            });

    particle_render_pipeline = create_graphics_pipeline(
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
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        },
        {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext                  = nullptr,
            .flags                  = 0,
            .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        },
        true,
        depth_buffer.format,
        true,
        true
    );
    particle_render_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, particle_render_pipeline);

    auto& particle_render_pass =
        framegraph->add_pass("particle render")
            .writes_color_attachment(lightpass_output)
            .reads_image(
                depth_buffer,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
            )
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

    luminance_histogram_pipeline = create_compute_pipeline(
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
    luminance_histogram_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, luminance_histogram_pipeline);

    auto& luminance_histogram_pass =
        framegraph->add_pass("luminance histogram")
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

    luminance_average_pipeline = create_compute_pipeline(
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
    luminance_average_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, luminance_average_pipeline);

    auto& luminance_average_pass =
        framegraph->add_pass("luminance average")
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

    smaa_edge_pipeline = create_compute_pipeline(
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
    smaa_edge_pass_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, smaa_edge_pipeline);

    auto& smaa_edge_pass =
        framegraph->add_pass("SMAA edge")
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

    smaa_weights_pipeline = create_compute_pipeline(
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
    smaa_weights_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, smaa_weights_pipeline);

    auto& smaa_weights_pass =
        framegraph->add_pass("SMAA weights")
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
        framegraph->add_pass("bloom downsample")
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

                    bloom_constants = {
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
                        command_buffer, pipeline_layout, stage_flags, 0, sizeof(BloomPushConstants), &bloom_constants
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
        framegraph->add_pass("bloom upsample")
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

                    bloom_constants = {
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
                        command_buffer, pipeline_layout, stage_flags, 0, sizeof(BloomPushConstants), &bloom_constants
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
        framegraph->add_pass("composite")
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
        framegraph->add_pass("debug geometry")
            .writes_image(
                composite_output,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            )
            .reads_image(
                depth_buffer,
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
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

                debug_renderer_constants.camera_pos = camera->position;
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

    smaa_blend_pipeline = create_compute_pipeline(
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
    smaa_blend_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, smaa_blend_pipeline);

    auto& smaa_blend_pass =
        framegraph->add_pass("SMAA blend")
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

    auto game_ui_pass =
        framegraph->add_pass("Game UI + Composite")
            .writes_color_attachment(ui_buffer)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                image_pipeline_barrier(
                    ui_buffer,
                    command_buffer,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    0,
                    0,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT
                );

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
                            {static_cast<int32_t>(ui_buffer.width), static_cast<int32_t>(ui_buffer.height), 1},
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
                        {static_cast<int32_t>(ui_buffer.width), static_cast<int32_t>(ui_buffer.height), 1},
                    },
                };

                vkCmdBlitImage(
                    command_buffer,
                    blit_source.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    ui_buffer.handle,
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

                image_pipeline_barrier(
                    ui_buffer,
                    command_buffer,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                );

                std::vector<VkRenderingAttachmentInfo> color_attachments = {
                    {
                        .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .pNext              = nullptr,
                        .imageView          = ui_buffer.view,
                        .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .resolveMode        = VK_RESOLVE_MODE_NONE,
                        .resolveImageView   = VK_NULL_HANDLE,
                        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .loadOp             = VK_ATTACHMENT_LOAD_OP_LOAD,
                        .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue         = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}},
                    },
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
                    .pDepthAttachment     = nullptr,
                    .pStencilAttachment   = nullptr
                };

                update_camera(ui_camera);

                vkCmdBeginRendering(command_buffer, &rendering_info);

                auto&     pipeline = ui_sprite_pipeline;
                glm::mat4 push     = ui_camera.combined_matrix;

                {
                    vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);
                    vkCmdBindDescriptorSets(
                        command_buffer,
                        pipeline.bind_point,
                        pipeline.pipeline_layout,
                        0,
                        1,
                        &global_texture_descriptor_set,
                        0,
                        nullptr
                    );
                    vkCmdPushConstants(
                        command_buffer, pipeline.pipeline_layout, pipeline.stage_flags, 0, sizeof(glm::mat4), &push
                    );
                    VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(command_buffer, 0, 1, &ui_sprite_batcher->geometry_buffer.handle, &offset);
                    vkCmdDraw(command_buffer, ui_sprite_batcher->drawcall_batches[0] * 6, 1, 0, 0);
                }

                if (ui_sprite_batcher->drawcall_batches.size() > 1) {
                    auto& pipeline = ui_sprite_text_pipeline;

                    vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);
                    vkCmdBindDescriptorSets(
                        command_buffer,
                        pipeline.bind_point,
                        pipeline.pipeline_layout,
                        0,
                        1,
                        &global_texture_descriptor_set,
                        0,
                        nullptr
                    );
                    vkCmdPushConstants(
                        command_buffer, pipeline.pipeline_layout, pipeline.stage_flags, 0, sizeof(glm::mat4), &push
                    );
                    VkDeviceSize offset = ui_sprite_batcher->drawcall_batches[0] * sizeof(float) * 13 * 6;
                    vkCmdBindVertexBuffers(command_buffer, 0, 1, &ui_sprite_batcher->geometry_buffer.handle, &offset);
                    vkCmdDraw(command_buffer, ui_sprite_batcher->drawcall_batches[1] * 6, 1, 0, 0);
                }

                vkCmdEndRendering(command_buffer);
            });

    auto& blit_ui_pass =
        framegraph->add_pass("Final blit + UI")
            .samples_image(ui_buffer, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
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
                    auto& blit_source = ui_buffer;
                    image_pipeline_barrier(
                        blit_source,
                        command_buffer,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT
                    );

                    VkImageBlit blit_region = {
                        .srcSubresource =
                            {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                             .mipLevel       = 0,
                             .baseArrayLayer = 0,
                             .layerCount     = 1},
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
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
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

    framegraph->build();
}

void Renderer::initialize(
    class World* world, SDL_Window* window, bool meshlets_enabled, bool hardware_rt_enabled, bool vsync
) {
    this->world = world;

    std::random_device rd;
    rng.seed((int)rd());

    bool enable_validation = false;

    VK_CHECK(volkInitialize());

    spdlog::info("Creating Vulkan instance");
    instance = create_instance(enable_validation, debug_messenger);

    spdlog::info("Picking physical device");
    physical_device       = pick_physical_device(instance);
    graphics_family_index = get_graphics_family_index(physical_device);
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
    device = create_device(
        instance,
        physical_device,
        graphics_family_index,
        enable_validation,
        this->meshlets_enabled,
        this->hardware_rt_enabled
    );
    volkLoadDevice(device);

    if (meshlets_enabled && !this->meshlets_enabled) {
        spdlog::warn("Meshlets requested, but not supported");
    } else {
        this->meshlets_enabled = meshlets_enabled;
    }

    if (hardware_rt_enabled && !this->hardware_rt_enabled) {
        spdlog::warn("Hardware raytracing requested, but not supported");
    } else {
        this->hardware_rt_enabled = hardware_rt_enabled;
    }

    spdlog::info(
        "Extension support:\n\tMesh shading: {}\n\tRay tracing: {}", this->meshlets_enabled, this->hardware_rt_enabled
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

    VK_CHECK(vmaCreateAllocator(&allocator_info, &vma_allocator));

    swapchain = create_swapchain(window, instance, device, physical_device, vsync);

    vkGetDeviceQueue(device, graphics_family_index, 0, &graphics_queue);

    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };

    image_available_semaphores.resize(swapchain.images.size());
    render_finished_semaphores.resize(swapchain.images.size());
    for (int i = 0; i < swapchain.images.size(); i++) {
        VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available_semaphores[i]));
        VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished_semaphores[i]));
    }

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

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
    VK_CHECK(vkCreateCommandPool(device, &command_pool_info, nullptr, &command_pool));

    command_pool_info = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = graphics_family_index
    };
    VK_CHECK(vkCreateCommandPool(device, &command_pool_info, nullptr, &temporary_command_pool));

    VkCommandBufferAllocateInfo command_buffer_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = FRAMES_IN_FLIGHT
    };
    VK_CHECK(vkAllocateCommandBuffers(device, &command_buffer_info, &command_buffers[0]));

    tracy_vk_context = TracyVkContextCalibrated(
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
    if (this->hardware_rt_enabled) {
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
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &linear_sampler));

    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &linear_sampler_clamped));

    sampler_info.mipmapMode                            = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VkSamplerReductionModeCreateInfoEXT reduction_info = {
        .sType         = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT,
        .pNext         = nullptr,
        .reductionMode = VK_SAMPLER_REDUCTION_MODE_MIN,
    };
    sampler_info.pNext = &reduction_info;
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &depth_sampler));

    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.magFilter        = VK_FILTER_NEAREST;
    sampler_info.minFilter        = VK_FILTER_NEAREST;
    sampler_info.pNext            = nullptr;
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &nearest_sampler));

    lighting_data = {
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

    staging_buffer = create_buffer(
        1024 * 1024 * 128,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    global_vertex_buffer = create_buffer(
        1024 * 1024 * 128,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            (this->hardware_rt_enabled ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                                       : 0),
        vma_allocator
    );

    global_index_buffer = create_buffer(
        1024 * 1024 * 184,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            (this->hardware_rt_enabled ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                                       : 0),
        vma_allocator
    );

    meshlet_buffer = create_buffer(
        1024 * 1024 * 12,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        vma_allocator
    );

    meshlet_vertex_indices_buffer = create_buffer(
        1024 * 1024 * 128,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        vma_allocator
    );

    meshlet_primitive_indices_buffer = create_buffer(
        1024 * 1024 * 128,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        vma_allocator
    );

    meshlet_bounds_buffer = create_buffer(
        1024 * 1024 * 32,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        vma_allocator
    );

    uint32_t probe_count = lighting_data.probe_counts.x * lighting_data.probe_counts.y * lighting_data.probe_counts.z;
    ddgi_ray_buffer      = create_buffer(
        sizeof(DDGIRay) * probe_count * MAX_RAYS_PER_PROBE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vma_allocator
    );
    spdlog::info("DDGI Ray buffer size: {}MB", ddgi_ray_buffer.size / 1024 / 1024);

    ddgi_probe_buffer =
        create_buffer(sizeof(DDGIProbe) * probe_count, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vma_allocator);
    spdlog::info("DDGI Probe buffer size: {}MB", ddgi_probe_buffer.size / 1024 / 1024);

    // NOTE: could maybe reuse the buffer above, but this isn't a big crime (i think)
    // layout - indirect command structure + tile * screen_size / 8
    indirect_dispatch_tile_copy_buffer = create_buffer(
        sizeof(VkDispatchIndirectCommand) +
            sizeof(uint32_t) * 2 * ((swapchain.width + 7) / 8) * ((swapchain.height + 7) / 8),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        vma_allocator
    );

    // layout - indirect command structure + tile * screen_size / 8
    indirect_dispatch_tile_process_buffer = create_buffer(
        sizeof(VkDispatchIndirectCommand) +
            sizeof(uint32_t) * 2 * ((swapchain.width + 7) / 8) * ((swapchain.height + 7) / 8),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        vma_allocator
    );

    indirect_command_buffer = create_buffer(
        1024 * 1024 * 12 * FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator
    );

    drawcall_buffer = create_buffer(
        1024 * 1024 * 6 * FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    material_buffer = create_buffer(
        1024 * 1024 * 6 * FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    mesh_buffer = create_buffer(
        1024 * 1024 * 12 * FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    scene_ubo_buffer = create_buffer(
        aligned_size(sizeof(SceneUBO), physical_device_properties.limits.minUniformBufferOffsetAlignment) *
            FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    lighting_ubo_buffer = create_buffer(
        aligned_size(sizeof(LightingUBO), physical_device_properties.limits.minUniformBufferOffsetAlignment) *
            FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    particle_position_buffer = create_buffer(
        sizeof(glm::vec3) * particle_count,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator
    );
    particle_velocity_buffer = create_buffer(
        sizeof(glm::vec3) * particle_count,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator
    );

    depth_buffer = create_image(
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
    depth_hiz                     = create_image(
        VK_FORMAT_R32_SFLOAT,
        depth_pyramid_width,
        depth_pyramid_height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        true,
        vma_allocator,
        device
    );

    depth_mip_views.resize(depth_hiz.levels);
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

    bloom_buffer = create_image(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        swapchain.width / 2,
        swapchain.height / 2,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        true,
        vma_allocator,
        device
    );

    bloom_mip_views.resize(bloom_buffer.levels);
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

    ui_camera.position        = {0.0f, 0.0f, 0.0f};
    ui_camera.orientation     = {0.0f, 0.0f, 0.0f, 1.0f};
    ui_camera.near_plane      = 0.01f;
    ui_camera.far_plane       = 10.0f;
    ui_camera.type            = CameraType::ORTHOGRAPHIC;
    ui_camera.viewport_width  = swapchain.width;
    ui_camera.viewport_height = swapchain.height;

    ui_buffer = create_image(
        VK_FORMAT_R8G8B8A8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    gbuffer_albedo = create_image(
        VK_FORMAT_R8G8B8A8_SRGB,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    gbuffer_normals = create_image(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    gbuffer_emissive = create_image(
        VK_FORMAT_B10G11R11_UFLOAT_PACK32,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    gbuffer_id = create_image(
        VK_FORMAT_R32_UINT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    gbuffer_images = {
        gbuffer_albedo,
        gbuffer_normals,
        gbuffer_emissive,
        gbuffer_id,
    };

    ddgi_depth_atlas = create_image(
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

    ddgi_depth_atlas_history = create_image(
        VK_FORMAT_R16G16_SFLOAT,
        depth_atlas_width,
        depth_atlas_height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    ddgi_irradiance = create_image(
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

    ddgi_irradiance_history = create_image(
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

    lightpass_output = create_image(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    luminance_buffer = create_buffer(
        sizeof(uint32_t) * 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_allocator
    );
    average_luminance_image = create_image(
        VK_FORMAT_R16_SFLOAT,
        1,
        1,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    smaa_edges = create_image(
        VK_FORMAT_R8G8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    smaa_weights = create_image(
        VK_FORMAT_R8G8B8A8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    smaa_output = create_image(
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

    directional_shadow_buffer = create_image(
        VK_FORMAT_R8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    directional_shadow_buffer_pong = create_image(
        VK_FORMAT_R8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    hilbert_lut = create_hilbert_lut(staging_buffer, command_buffers[0], graphics_queue, vma_allocator, device);

    ao_output = create_image(
        VK_FORMAT_R32_UINT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    ao_output_edges = create_image(
        VK_FORMAT_R8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    ao_output_denoised = create_image(
        VK_FORMAT_R32_UINT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    ao_prefiltered_depth = create_image(
        VK_FORMAT_R32_SFLOAT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        true,
        vma_allocator,
        device
    );

    ao_depth_mip_views.resize(ao_prefiltered_depth.levels);
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

    composite_output = create_image(
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

    rt_reflection_buffer = create_image(
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

    rt_reflection_history = create_image(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    rt_reflection_views.resize(rt_reflection_buffer.levels);
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

    smaa_area_tex = load_image(
        "data/textures/smaa_area_tex.png",
        VK_FORMAT_R8G8B8A8_UNORM,
        false,
        staging_buffer,
        command_buffers[0],
        graphics_queue,
        vma_allocator,
        device
    );

    smaa_search_tex = load_image(
        "data/textures/smaa_search_tex.png",
        VK_FORMAT_R8G8B8A8_UNORM,
        false,
        staging_buffer,
        command_buffers[0],
        graphics_queue,
        vma_allocator,
        device
    );

    brdf_lut = create_image(
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

    debug_renderer_constants = {};
    debug_renderer           = create_debug_renderer(
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

    global_texture_descriptor_layout = create_descriptor_set_layout(
        device,
        VK_SHADER_STAGE_ALL,
        {
            {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .is_array = true},
        }
    );

    global_texture_descriptor_set = VK_NULL_HANDLE;
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

    buffers = {
        .staging_buffer           = staging_buffer,
        .vertex_buffer            = global_vertex_buffer,
        .index_buffer             = global_index_buffer,
        .meshlet_buffer           = meshlet_buffer,
        .meshlet_vertex_indices   = meshlet_vertex_indices_buffer,
        .meshlet_primitive_buffer = meshlet_primitive_indices_buffer,
        .meshlet_bounds_buffer    = meshlet_bounds_buffer
    };
    buffer_offsets = {};

    if (this->hardware_rt_enabled) {
        spdlog::info("Setting up hardware raytracing scene");
        rt_scene = create_rt_scene(
            device,
            physical_device,
            vma_allocator,
            command_buffers[0],
            graphics_queue,
            RT_MAX_TLAS_INSTANCES,
            FRAMES_IN_FLIGHT
        );
    }

    imgui_descriptor_pool = imgui_init(
        window,
        instance,
        physical_device,
        device,
        swapchain.format,
        graphics_family_index,
        graphics_queue,
        FRAMES_IN_FLIGHT
    );

    bloom_levels               = glm::max(5u, static_cast<uint32_t>(bloom_mip_views.size() - 5));
    bloom_upscale_sample_scale = 2.5f;

    luminance_constants = {
        .min_log2_luminance     = min_log_lum,
        .inverse_log2_luminance = 1.0f / (max_log_lum - min_log_lum),
        .time_coef              = 0,
        .pixel_count            = static_cast<float>(lightpass_output.width * lightpass_output.height),
    };

    scene_data_layout = {
        .bindings = {
            DescriptorBinding{
                .type       = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .write_info = DescriptorInfo(scene_ubo_buffer.handle, 0, scene_ubo_buffer.size / FRAMES_IN_FLIGHT)
            },
        }
    };

    draw_data_layout = {
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

    geometry_data_layout = {
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

    cull_pipeline = create_compute_pipeline(
        device,
        this->meshlets_enabled
            ? shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/cull_meshlets.comp.spv")
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
    cull_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, cull_pipeline);
    cull_push_constants  = {
         .screen_size = {depth_pyramid_width, depth_pyramid_height},
         .draw_count  = 0,
    };
    cull_push_constants.enable_lods = true;

    std::vector<Shader> gpass_shaders = {
        shader_from_file(device, VK_SHADER_STAGE_FRAGMENT_BIT, "data/shaders/bindless.frag.spv"),
    };

    if (this->meshlets_enabled) {
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

    gpass_pipeline = create_graphics_pipeline(
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
            gbuffer_id.format,
        },

        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        },
        {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext                  = nullptr,
            .flags                  = 0,
            .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        },
        false,
        depth_buffer.format,
        true,
        true,
        sizeof(GeometryPushConstants),
        global_texture_descriptor_layout
    );
    gpass_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, gpass_pipeline);
    gpass_push_constants  = {
         .screen_size                 = {swapchain.width, swapchain.height},
         .disable_cone_cull           = false,
         .disable_small_triangle_cull = false,
    };

    VkVertexInputBindingDescription vertex_binding_description = {
        .binding   = 0,
        .stride    = sizeof(float) * 13,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    std::array<VkVertexInputAttributeDescription, 5> attribute_desriptions = {
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = 0,

        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = sizeof(float) * 3,

        },
        VkVertexInputAttributeDescription{
            .location = 2,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = sizeof(float) * 6,
        },
        VkVertexInputAttributeDescription{
            .location = 3,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset   = sizeof(float) * 8,
        },
        VkVertexInputAttributeDescription{
            .location = 4,
            .binding  = 0,
            .format   = VK_FORMAT_R32_SINT,
            .offset   = sizeof(float) * 12,
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext                           = nullptr,
        .flags                           = 0,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &vertex_binding_description,
        .vertexAttributeDescriptionCount = attribute_desriptions.size(),
        .pVertexAttributeDescriptions    = attribute_desriptions.data()
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    world_sprite_pipeline = create_graphics_pipeline(
        device,
        {
            shader_from_file(device, VK_SHADER_STAGE_VERTEX_BIT, "data/shaders/sprite_draw.vert.spv"),
            shader_from_file(device, VK_SHADER_STAGE_FRAGMENT_BIT, "data/shaders/sprite_draw_world.frag.spv"),
        },
        {draw_data_layout},
        {
            gbuffer_albedo.format,
            gbuffer_normals.format,
            gbuffer_emissive.format,
            gbuffer_id.format,
        },
        vertex_input_state,
        input_assembly_state,
        false,
        depth_buffer.format,
        true,
        true,
        sizeof(glm::mat4),
        global_texture_descriptor_layout
    );
    world_sprite_pipeline_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, world_sprite_pipeline);

    ui_sprite_pipeline = create_graphics_pipeline(
        device,
        {
            shader_from_file(device, VK_SHADER_STAGE_VERTEX_BIT, "data/shaders/sprite_draw.vert.spv"),
            shader_from_file(device, VK_SHADER_STAGE_FRAGMENT_BIT, "data/shaders/sprite_draw_ui.frag.spv"),
        },
        {},
        {ui_buffer.format},
        vertex_input_state,
        input_assembly_state,
        true,
        VK_FORMAT_UNDEFINED,
        false,
        false,
        sizeof(glm::mat4),
        global_texture_descriptor_layout

    );

    ui_sprite_text_pipeline = create_graphics_pipeline(
        device,
        {
            shader_from_file(device, VK_SHADER_STAGE_VERTEX_BIT, "data/shaders/sprite_draw.vert.spv"),
            shader_from_file(device, VK_SHADER_STAGE_FRAGMENT_BIT, "data/shaders/sprite_draw_ui_text.frag.spv"),
        },
        {},
        {ui_buffer.format},
        vertex_input_state,
        input_assembly_state,
        true,
        VK_FORMAT_UNDEFINED,
        false,
        false,
        sizeof(glm::mat4),
        global_texture_descriptor_layout

    );

    hiz_pipeline = create_compute_pipeline(
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

    ao_prefilter_pipeline = create_compute_pipeline(
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
    ao_prefilter_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, ao_prefilter_pipeline);

    ao_pipeline = create_compute_pipeline(
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
    ao_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, ao_pipeline);

    ao_denoise_pipeline = create_compute_pipeline(
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
    ao_denoise_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, ao_denoise_pipeline);

    light_pipeline = create_compute_pipeline(
        device,
        shader_from_file(
            device,
            VK_SHADER_STAGE_COMPUTE_BIT,
            this->hardware_rt_enabled ? "data/shaders/light_rt.comp.spv" : "data/shaders/light_no_rt.comp.spv"
        ),
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
    light_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, light_pipeline);

    bloom_downsample_pipeline = create_compute_pipeline(
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

    bloom_upsample_pipeline = create_compute_pipeline(
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

    composite_pipeline = create_compute_pipeline(
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
    composite_descriptor_sets = allocate_descriptor_sets(device, descriptor_pool, composite_pipeline);
    composite_push_constants  = {
         .bloom_strength        = 0.04f,
         .tonemapping_type      = 1,
         .min_ev100             = -4.0f,
         .max_ev100             = 16.0f,
         .enable_auto_exposure  = true,
         .manual_ev100          = 0.0f,
         .exposure_compensation = 0.0f,
    };

    dynamic_offsets.resize(static_cast<uint32_t>(DynamicOffset::COUNT));

    world_sprite_batcher = new SpriteBatcher(this, 10000);
    ui_sprite_batcher    = new SpriteBatcher(this, 10000);

    setup_framegraph();
}

void Renderer::wait_idle() {
    VK_CHECK(vkDeviceWaitIdle(device));
}

VkCommandBuffer Renderer::allocate_temporary_command_buffer() {
    VkCommandBufferAllocateInfo command_buffer_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext              = nullptr,
        .commandPool        = temporary_command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device, &command_buffer_info, &command_buffer));

    return command_buffer;
}

void Renderer::free_temporary_command_buffer(VkCommandBuffer command_buffer) {
    vkFreeCommandBuffers(device, temporary_command_pool, 1, &command_buffer);
}

void Renderer::begin_frame(Camera* camera) {
    this->camera = camera;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void Renderer::render_frame(float delta_time) {
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

    auto transposed_projection = glm::transpose(camera->projection_matrix);

    glm::vec4 frustum_x = normalize_plane(transposed_projection[3] + transposed_projection[0]);
    glm::vec4 frustum_y = normalize_plane(transposed_projection[3] + transposed_projection[1]);

    luminance_constants.time_coef              = glm::clamp(1.0f - glm::exp(-delta_time * adaption_speed), 0.0f, 1.0f);
    luminance_constants.min_log2_luminance     = min_log_lum;
    luminance_constants.inverse_log2_luminance = 1.0f / (max_log_lum - min_log_lum);

    float aspect         = camera->viewport_width / camera->viewport_height;
    float tan_half_fov_y = tanf(glm::radians(camera->fov) / 2.0f);
    float tan_half_fov_x = tan_half_fov_y * aspect;

    xegtao_constants.camera_tan_half_fov          = {tan_half_fov_x, tan_half_fov_y};
    xegtao_constants.ndc_to_view_mul              = {tan_half_fov_x * 2.0f, tan_half_fov_y * -2.0f};
    xegtao_constants.ndc_to_view_add              = {-tan_half_fov_x, tan_half_fov_y};
    xegtao_constants.ndc_to_view_mul_x_pixel_size = {
        xegtao_constants.ndc_to_view_mul.x / (float)swapchain.width,
        xegtao_constants.ndc_to_view_mul.y / (float)swapchain.height
    };
    xegtao_constants.camera_near_far = {camera->near_plane, 10000.0f};

    lighting_data.frame_index += 1;
    lighting_data.camera_pos              = camera->position;
    lighting_data.ddgi_probe_ray_rotation = ddgi_random_rotation();

    SceneUBO scene_ubo        = {};
    scene_ubo.proj            = camera->projection_matrix;
    scene_ubo.camera_position = glm::vec4(camera->position, 1.0);

    scene_ubo.view_proj         = camera->combined_matrix;
    scene_ubo.inverse_view_proj = glm::inverse(camera->combined_matrix);

    scene_ubo.view       = camera->view_matrix;
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

    scene_ubo.P00 = camera->projection_matrix[0][0];
    scene_ubo.P11 = camera->projection_matrix[1][1];

    scene_ubo.near_plane = camera->near_plane;
    scene_ubo.far_plane  = camera->far_plane;
    scene_ubo.lod_target = (2 / scene_ubo.P11) * (1.0f / float(swapchain.height)) * (1 << min_lod);

    scene_ubo.last_frame_view_proj = last_frame_view_proj;

    world_sprite_batcher->reset();
    auto sprite_view =
        world->scene.entity_registry.view<components::Transform, components::Material, components::Sprite>(
            entt::exclude<components::ParticleEffect>
        );
    for (auto [e, t, m, s] : sprite_view.each()) {
        if (m.id == AssetMetadata::INVALID_METADATA) {
            continue;
        }

        int mat_index = world->load_material(m.id);

        if (mat_index == -1) {
            continue;
        }

        world_sprite_batcher->draw(
            SpriteBatcher::Drawcall{
                .position   = t.world_position,
                .rotation   = t.world_rotation,
                .size       = s.size * t.world_scale,
                .pivot      = s.pivot,
                .uvs        = s.uvs,
                .color      = {1.0f, 1.0f, 1.0f, 1.0},
                .data_index = static_cast<int>(
                    m.dedicated_material_index == -1 ? mat_index
                                                     : world->resources.materials.size() + m.dedicated_material_index
                ),
            }
        );
    }

    auto sprite_particle_view =
        world->scene.entity_registry
            .view<components::Transform, components::Material, components::Sprite, components::ParticleEffect>();
    for (auto [e, t, m, s, p] : sprite_particle_view.each()) {
        if (!p.effect.has_value() || p.dirty) {
            if (p.effect_id == AssetMetadata::INVALID_METADATA) {
                continue;
            }

            int effect_index = world->load_particle_effect(p.effect_id);
            if (effect_index == -1) {
                continue;
            }

            p.effect = world->resources.particle_effects[effect_index];
            p.emitter_configs.resize(p.effect->emitters.size());

            for (size_t i = 0; i < p.effect->emitters.size(); i++) {
                auto& emitter = p.effect->emitters[i];
                auto& cfg     = p.emitter_configs[i];

                emitter.resize(cfg.max_particles);
                emitter.emission_rate    = cfg.emission_rate;
                emitter.emitter_lifetime = cfg.emitter_lifetime;
                emitter.loop             = cfg.loop;
            }

            p.dirty = false;
        }

        if (!p.active) {
            continue;
        }

        if (m.id == AssetMetadata::INVALID_METADATA) {
            continue;
        }

        int mat_index = world->load_material(m.id);

        if (mat_index == -1) {
            continue;
        }

        SimParams params = {
            .delta_time  = delta_time,
            .time        = world->time,
            .particle_id = 0,
        };

        for (auto& emitter : p.effect->emitters) {
            emitter.simulate(params);

            for (uint32_t i = 0; i < emitter.live_count; ++i) {
                const Particle& pt = emitter.particles[i];

                world_sprite_batcher->draw(
                    SpriteBatcher::Drawcall{
                        .position   = t.world_position + glm::vec3(pt.position),
                        .rotation   = t.world_rotation,
                        .size       = t.scale * pt.size,
                        .pivot      = s.pivot,
                        .uvs        = s.uvs,
                        .color      = pt.color,
                        .data_index = static_cast<int>(
                            m.dedicated_material_index == -1
                                ? mat_index
                                : world->resources.materials.size() + m.dedicated_material_index
                        ),
                    }
                );
            }
        }
    }

    ui_sprite_batcher->reset();
    auto ui_sprite_view = world->scene.entity_registry.view<components::Transform, components::UISprite>(
        entt::exclude<components::ParticleEffect>
    );
    for (auto [e, t, s] : ui_sprite_view.each()) {
        if (s.texture_id == AssetMetadata::INVALID_METADATA) {
            continue;
        }

        int tex_index = world->load_texture(s.texture_id);

        if (tex_index == -1) {
            continue;
        }

        float     rotation    = glm::eulerAngles(t.world_rotation).z;
        glm::quat ui_rotation = glm::angleAxis(rotation, glm::vec3(0, 0, 1));

        ui_sprite_batcher->draw(
            SpriteBatcher::Drawcall{
                .position   = glm::vec3(glm::vec2(t.world_position), 0.0f),
                .rotation   = ui_rotation,
                .size       = t.scale * s.size,
                .pivot      = s.pivot,
                .uvs        = s.uvs,
                .color      = s.color,
                .data_index = tex_index,
            }
        );
    }

    auto ui_particle_view =
        world->scene.entity_registry.view<components::Transform, components::UISprite, components::ParticleEffect>();
    for (auto [e, t, s, p] : ui_particle_view.each()) {
        if (!p.effect.has_value() || p.dirty) {
            if (p.effect_id == AssetMetadata::INVALID_METADATA) {
                continue;
            }

            int effect_index = world->load_particle_effect(p.effect_id);
            if (effect_index == -1) {
                continue;
            }

            p.effect = world->resources.particle_effects[effect_index];
            p.emitter_configs.resize(p.effect->emitters.size());

            for (size_t i = 0; i < p.effect->emitters.size(); i++) {
                auto& emitter = p.effect->emitters[i];
                auto& cfg     = p.emitter_configs[i];

                emitter.resize(cfg.max_particles);
                emitter.emission_rate    = cfg.emission_rate;
                emitter.emitter_lifetime = cfg.emitter_lifetime;
                emitter.loop             = cfg.loop;
            }

            p.dirty = false;
        }

        if (!p.active) {
            continue;
        }

        if (s.texture_id == AssetMetadata::INVALID_METADATA) {
            continue;
        }

        int tex_index = world->load_texture(s.texture_id);

        if (tex_index == -1) {
            continue;
        }

        SimParams params = {
            .delta_time  = delta_time,
            .time        = world->time,
            .particle_id = 0,
        };

        float     rotation    = glm::eulerAngles(t.world_rotation).z;
        glm::quat ui_rotation = glm::angleAxis(rotation, glm::vec3(0, 0, 1));

        for (auto& emitter : p.effect->emitters) {
            emitter.simulate(params);

            glm::vec2 emitter_pos = glm::vec2(t.world_position);

            for (uint32_t i = 0; i < emitter.live_count; ++i) {
                const Particle& pt = emitter.particles[i];

                ui_sprite_batcher->draw(
                    SpriteBatcher::Drawcall{
                        .position   = glm::vec3(emitter_pos + glm::vec2(pt.position), 0.0f),
                        .rotation   = ui_rotation,
                        .size       = t.scale * pt.size,
                        .pivot      = s.pivot,
                        .uvs        = s.uvs,
                        .color      = pt.color,
                        .data_index = tex_index,
                    }
                );
            }
        }
    }

    ui_sprite_batcher->end_batch();
    auto text_sprite_view = world->scene.entity_registry.view<components::Transform, components::Text>();
    for (auto [e, t, tx] : text_sprite_view.each()) {
        if (tx.font_id == AssetMetadata::INVALID_METADATA) {
            continue;
        }

        int font_index = world->load_font(tx.font_id);
        if (font_index == -1) {
            continue;
        }

        Font& font = world->resources.fonts[font_index];

        int tex_index = world->load_texture(font.atlas_texture_id);
        if (tex_index == -1) {
            continue;
        }

        float total_width = 0.0f;
        float min_y       = 0.0f;
        float max_y       = 0.0f;

        float cursor_x = 0.0f;
        for (char c : tx.text) {
            auto it = font.glyphs.find((uint32_t)c);
            if (it == font.glyphs.end()) {
                continue;
            }

            const auto& glyph = it->second;

            float top_y    = -glyph.bearing_y * t.world_scale;
            float bottom_y = top_y - glyph.height * t.world_scale;

            min_y = std::min(min_y, bottom_y);
            max_y = std::max(max_y, top_y);

            cursor_x += glyph.advance_x * t.world_scale;
        }
        total_width        = cursor_x;
        float total_height = max_y - min_y;

        glm::vec3 pivot_offset = glm::vec3(-total_width * tx.pivot.x, -(min_y + total_height * tx.pivot.y), 0.0f);

        float local_cursor_x = 0.0f;
        float local_cursor_y = 0.0f;

        float     rotation    = glm::eulerAngles(t.world_rotation).z;
        glm::quat ui_rotation = glm::angleAxis(rotation, glm::vec3(0, 0, 1));

        for (char c : tx.text) {
            auto it = font.glyphs.find((uint32_t)c);
            if (it == font.glyphs.end()) {
                continue;
            }

            const auto& glyph = it->second;

            if (glyph.width == 0 || glyph.height == 0) {
                local_cursor_x += glyph.advance_x * t.world_scale;
                continue;
            }

            float w = glyph.width * t.world_scale;
            float h = glyph.height * t.world_scale;

            float top_left_x = local_cursor_x + glyph.bearing_x * t.world_scale;
            float top_left_y = local_cursor_y - glyph.bearing_y * t.world_scale;

            float local_x = top_left_x + w * 0.5f;
            float local_y = top_left_y - h;

            glm::vec3 local_pos = glm::vec3(local_x, local_y, 0.0f) + pivot_offset;
            glm::vec3 world_pos = t.world_position + (ui_rotation * local_pos);

            ui_sprite_batcher->draw(
                SpriteBatcher::Drawcall{
                    .position   = glm::vec3(glm::vec2(world_pos), 0.0f),
                    .rotation   = ui_rotation,
                    .size       = glm::vec2(w, h),
                    .pivot      = {0.5f, 0.0f},
                    .uvs        = glyph.uvs,
                    .color      = tx.color,
                    .data_index = tex_index,
                }
            );

            local_cursor_x += glyph.advance_x * t.world_scale;
        }
    }
    ui_sprite_batcher->end_batch();

    {
        void*  scene_ubo_ptr  = nullptr;
        size_t ubo_ptr_offset = (scene_ubo_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        VK_CHECK(vmaMapMemory(vma_allocator, scene_ubo_buffer.allocation, &scene_ubo_ptr));
        memcpy(reinterpret_cast<char*>(scene_ubo_ptr) + ubo_ptr_offset, &scene_ubo, sizeof(SceneUBO));
        vmaUnmapMemory(vma_allocator, scene_ubo_buffer.allocation);
        VK_CHECK(vmaFlushAllocation(vma_allocator, scene_ubo_buffer.allocation, ubo_ptr_offset, scene_ubo_buffer.size));
    }

    {
        void*  lighting_ubo_ptr    = nullptr;
        size_t lighting_ptr_offset = (lighting_ubo_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        VK_CHECK(vmaMapMemory(vma_allocator, lighting_ubo_buffer.allocation, &lighting_ubo_ptr));
        memcpy(reinterpret_cast<char*>(lighting_ubo_ptr) + lighting_ptr_offset, &lighting_data, sizeof(LightingUBO));
        vmaUnmapMemory(vma_allocator, lighting_ubo_buffer.allocation);
        VK_CHECK(vmaFlushAllocation(
            vma_allocator, lighting_ubo_buffer.allocation, lighting_ptr_offset, lighting_ubo_buffer.size
        ));
    }

    debug_renderer_constants.combined_matrix = camera->combined_matrix;
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
            reinterpret_cast<char*>(ptr) + frame_offset,
            world->resources.meshes.data(),
            sizeof(Mesh) * world->resources.meshes.size()
        );
        vmaUnmapMemory(vma_allocator, mesh_buffer.allocation);
        VK_CHECK(vmaFlushAllocation(vma_allocator, mesh_buffer.allocation, frame_offset, mesh_buffer.size));
    }

    {
        void*  ptr            = nullptr;
        size_t frame_offset   = (material_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        size_t runtime_offset = frame_offset + sizeof(Material) * world->resources.materials.size();

        VK_CHECK(vmaMapMemory(vma_allocator, material_buffer.allocation, &ptr));
        memcpy(
            reinterpret_cast<char*>(ptr) + frame_offset,
            world->resources.materials.data(),
            sizeof(Material) * world->resources.materials.size()
        );
        memcpy(
            reinterpret_cast<char*>(ptr) + runtime_offset,
            world->resources.runtime_materials.data(),
            sizeof(Material) * world->resources.runtime_materials.size()
        );
        vmaUnmapMemory(vma_allocator, material_buffer.allocation);
        VK_CHECK(vmaFlushAllocation(vma_allocator, material_buffer.allocation, frame_offset, material_buffer.size));
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .pNext = nullptr, .flags = 0, .pInheritanceInfo = nullptr
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

    framegraph->execute(command_buffer, frame_index);
}

void Renderer::end_frame() {
    VkCommandBuffer command_buffer = get_current_command_buffer();

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

    framegraph->gather_timestamp_queries(device, physical_device_properties.limits.timestampPeriod);

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

    last_frame_view_proj = camera->combined_matrix;
}

VkCommandBuffer Renderer::get_current_command_buffer() {
    return command_buffers[frame_index];
}

SpriteBatcher::SpriteBatcher(Renderer* renderer, uint32_t max_drawcalls) {
    this->frames_in_flight = Renderer::FRAMES_IN_FLIGHT;
    this->max_drawcalls    = max_drawcalls;
    this->drawcall_count   = 0;

    this->drawcalls.resize(this->max_drawcalls);

    this->drawcall_buffer = create_buffer(
        sizeof(Drawcall) * this->max_drawcalls * frames_in_flight,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        renderer->vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    spdlog::debug("Sprite Batch drawcall buffer size: {}MB", (float)drawcall_buffer.size / 1024.0f / 1024.0f);

    this->geometry_buffer = create_buffer(
        sizeof(SpriteVertex) * 6 * this->max_drawcalls,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        renderer->vma_allocator
    );
    spdlog::debug("Sprite Batch geometry buffer size: {}MB", (float)geometry_buffer.size / 1024.0f / 1024.0f);

    this->geometry_build_pipline = create_compute_pipeline(
        renderer->device,
        shader_from_file(renderer->device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/sprite_batch_build.comp.spv"),
        {DescriptorLayout{
            .bindings = {
                DescriptorBinding{
                    .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                    .write_info = DescriptorInfo(drawcall_buffer.handle, 0, drawcall_buffer.size / frames_in_flight)
                },
                DescriptorBinding{
                    .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .write_info = DescriptorInfo(geometry_buffer.handle),
                },
            }
        }}
    );
    this->geometry_build_pipline_descriptor_sets =
        allocate_descriptor_sets(renderer->device, renderer->descriptor_pool, this->geometry_build_pipline);
}

void SpriteBatcher::reset() {
    drawcall_count = 0;
    drawcall_batches.clear();
}

void SpriteBatcher::draw(const Drawcall& drawcall) {
    if (drawcall_count >= max_drawcalls - 1) {
        return;
    }

    drawcalls[drawcall_count++] = drawcall;
}

void SpriteBatcher::build_geometry_buffer(
    VmaAllocator vma_allocator, VkCommandBuffer command_buffer, uint32_t frame_index
) {
    auto& pipeline = geometry_build_pipline;

    uint32_t dynamic_offset = (drawcall_buffer.size / frames_in_flight) * frame_index;
    {
        void* ptr = nullptr;
        VK_CHECK(vmaMapMemory(vma_allocator, drawcall_buffer.allocation, &ptr));
        memcpy(reinterpret_cast<char*>(ptr) + dynamic_offset, drawcalls.data(), sizeof(Drawcall) * drawcall_count);
        vmaUnmapMemory(vma_allocator, drawcall_buffer.allocation);
        VK_CHECK(vmaFlushAllocation(vma_allocator, drawcall_buffer.allocation, 0, drawcall_buffer.size));
    }

    vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);
    vkCmdBindDescriptorSets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline.pipeline_layout,
        0,
        geometry_build_pipline_descriptor_sets.size(),
        geometry_build_pipline_descriptor_sets.data(),
        1,
        &dynamic_offset
    );

    vkCmdDispatch(command_buffer, drawcall_count, 1, 1);
}

void SpriteBatcher::end_batch() {
    drawcall_batches.push_back(
        drawcall_count - (drawcall_batches.size() == 0 ? 0 : drawcall_batches[drawcall_batches.size() - 1])
    );
}
