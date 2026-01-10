#include "ember.hpp"

#include "camera.hpp"
#include "device.hpp"
#include "framegraph.hpp"
#include "geometry.hpp"
#include "physics.hpp"
#include "pipeline.hpp"
#include "resources.hpp"
#include "rt_scene.hpp"
#include "swapchain.hpp"
#include "ui.hpp"

#include <format>

#include <glm/gtc/random.hpp>

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#include <ImGuizmo.h>

enum class DynamicOffset : uint32_t {
    SCENE_UBO = 0,
    INDIRECT_COMMAND_BUFFER,
    DDGI_RAY_BUFFER,
    LIGHTING_UBO,
    MESH_BUFFER,
    MATERIAL_BUFFER,
    DRAWCALL_BUFFER,
    DDGI_PROBE_BUFFER,

    COUNT = DDGI_PROBE_BUFFER + 1
};

void draw_pass_timings_lines(const std::vector<std::pair<std::string, PassTiming>>& passes) {
    ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
    if (ImPlot::BeginPlot("Pass Timings", ImVec2(-1, 300))) {
        ImPlot::SetupAxes("Sample", "Time (ms)");
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, PASS_TIMING_COUNT, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 10, ImGuiCond_Once);

        for (const auto& [name, timing] : passes) {
            ImPlot::PlotShaded(name.c_str(), timing.timings.data(), PASS_TIMING_COUNT);
        }

        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
}

void draw_pass_stats(const std::vector<std::pair<std::string, PassTiming>>& passes) {
    if (ImGui::BeginTable("PassStats", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupColumn("Pass");
        ImGui::TableSetupColumn("Avg (ms)");
        ImGui::TableSetupColumn("Min (ms)");
        ImGui::TableSetupColumn("Max (ms)");
        ImGui::TableSetupColumn("% of Frame");
        ImGui::TableHeadersRow();

        float              total_avg = 0.0f;
        std::vector<float> avg_timings;

        for (const auto& [name, timing] : passes) {
            float avg = timing.get_avg_timing_ms();

            avg_timings.push_back(avg);
            total_avg += avg;
        }

        for (const auto& [name, timing] : passes) {
            float min_time = FLT_MAX;
            float max_time = 0.0f;

            for (float t : timing.timings) {
                min_time = std::min(min_time, t);
                max_time = std::max(max_time, t);
            }
            float avg        = timing.get_avg_timing_ms();
            float percentage = (avg / total_avg) * 100.0f;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImVec4 color = ImVec4(1, 1, 1, 1);
            if (percentage > 40.0f)
                color = ImVec4(1, 0.3f, 0.3f, 1);
            else if (percentage > 20.0f)
                color = ImVec4(1, 1, 0, 1);
            else
                color = ImVec4(0.3f, 1, 0.3f, 1);

            ImGui::TextColored(color, "%s", name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", avg);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", min_time);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", max_time);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f%%", percentage);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "TOTAL");
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.3f", total_avg);
        ImGui::TableNextColumn();
        ImGui::Text("---");
        ImGui::TableNextColumn();
        ImGui::Text("---");
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.2f Theoretical FPS", 1000.0f / total_avg);
        ImGui::EndTable();

        ImGui::SeparatorText("Percentage:");
        for (size_t i = 0; i < passes.size(); i++) {
            float percentage = (avg_timings[i] / total_avg) * 100.0f;
            ImGui::Text("%s", passes[i].first.c_str());
            ImGui::SameLine(200);
            ImGui::ProgressBar(
                avg_timings[i] / total_avg,
                ImVec2(-1, 0),
                (std::format("{}: {:.1f}%", passes[i].first, percentage)).c_str()
            );
        }
    }
}

void draw_pass_profiler(const std::vector<std::pair<std::string, PassTiming>>& passes) {
    if (ImGui::BeginTabBar("ProfilerTabs")) {
        if (ImGui::BeginTabItem("Stats")) {
            draw_pass_stats(passes);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lines")) {
            draw_pass_timings_lines(passes);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Demo")) {
            ImPlot::ShowDemoWindow();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
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

            data[x + 64 * x] = static_cast<uint16_t>(r2_index);
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

uint16_t pack_tangent(glm::vec3 tangent) {
    float sum = glm::abs(tangent.x) + glm::abs(tangent.y) + glm::abs(tangent.z);
    float tu  = tangent.z >= 0 ? tangent.x / sum : (1 - glm::abs(tangent.y / sum)) * (tangent.x >= 0 ? 1 : -1);
    float tv  = tangent.z >= 0 ? tangent.y / sum : (1 - glm::abs(tangent.x / sum)) * (tangent.y >= 0 ? 1 : -1);

    return (meshopt_quantizeSnorm(tu, 8) + 127) | (meshopt_quantizeSnorm(tv, 8) + 127) << 8;
}

struct MeshIndirectDrawCommand {
    uint32_t group_count_x;
    uint32_t group_count_y;
    uint32_t group_count_z;
    uint32_t object_id;
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
    float pad[2];

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

    int use_probe_state;
    int use_bent_normals;
    int compensate_specular;
    int disney_diffuse;

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
    uint      frame_index;
};

struct CullPassPushConstants {
    glm::vec2 screen_size;

    uint32_t draw_count;

    uint32_t disable_frustum_cull;
    uint32_t disable_depth_cull;
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

    float key_value;
    float min_exposure;
    float max_exposure;
    uint  enable_auto_exposure;
};

struct BloomPushConstants {
    glm::vec2 texel_size;

    uint32_t first_pass;
    float    padding;
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

    auto max_instances = 2048 * 5;

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
                            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                            .write_info =
                                DescriptorInfo(ddgi_probe_buffer.handle, 0, ddgi_probe_buffer.size / frames_in_flight),
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

void generate_tangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0));
    std::vector<glm::vec3> bitangents(vertices.size(), glm::vec3(0));

    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i + 0];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        const Vertex& v0 = vertices[i0];
        const Vertex& v1 = vertices[i1];
        const Vertex& v2 = vertices[i2];

        const glm::vec3 v0_pos =
            glm::vec3(meshopt_dequantizeHalf(v0.px), meshopt_dequantizeHalf(v0.py), meshopt_dequantizeHalf(v0.pz));
        const glm::vec3 v1_pos =
            glm::vec3(meshopt_dequantizeHalf(v1.px), meshopt_dequantizeHalf(v1.py), meshopt_dequantizeHalf(v1.pz));
        const glm::vec3 v2_pos =
            glm::vec3(meshopt_dequantizeHalf(v2.px), meshopt_dequantizeHalf(v2.py), meshopt_dequantizeHalf(v2.pz));

        const glm::vec2 uv0 = glm::vec2(meshopt_dequantizeHalf(v0.ux), meshopt_dequantizeHalf(v0.uy));
        const glm::vec2 uv1 = glm::vec2(meshopt_dequantizeHalf(v1.ux), meshopt_dequantizeHalf(v1.uy));
        const glm::vec2 uv2 = glm::vec2(meshopt_dequantizeHalf(v2.ux), meshopt_dequantizeHalf(v2.uy));

        glm::vec3 edge1     = v1_pos - v0_pos;
        glm::vec3 edge2     = v2_pos - v0_pos;
        glm::vec2 delta_uv1 = uv1 - uv0;
        glm::vec2 delta_uv2 = uv2 - uv0;

        float f = 1.0f / (delta_uv1.x * delta_uv2.y - delta_uv2.x * delta_uv1.y);

        glm::vec3 tangent;
        tangent.x = f * (delta_uv2.y * edge1.x - delta_uv1.y * edge2.x);
        tangent.y = f * (delta_uv2.y * edge1.y - delta_uv1.y * edge2.y);
        tangent.z = f * (delta_uv2.y * edge1.z - delta_uv1.y * edge2.z);

        glm::vec3 bitangent;
        bitangent.x = f * (-delta_uv2.x * edge1.x + delta_uv1.x * edge2.x);
        bitangent.y = f * (-delta_uv2.x * edge1.y + delta_uv1.x * edge2.y);
        bitangent.z = f * (-delta_uv2.x * edge1.z + delta_uv1.x * edge2.z);

        tangents[i0] += tangent;
        tangents[i1] += tangent;
        tangents[i2] += tangent;

        bitangents[i0] += bitangent;
        bitangents[i1] += bitangent;
        bitangents[i2] += bitangent;
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        const glm::vec3& n = glm::vec3(
            (vertices[i].norm & 1023) / 511.0f - 1.0f,
            ((vertices[i].norm >> 10) & 1023) / 511.0f - 1.0f,
            ((vertices[i].norm >> 20) & 1023) / 511.0f - 1.0f
        );
        const glm::vec3& t = tangents[i];
        const glm::vec3& b = bitangents[i];

        glm::vec3 tangent = glm::normalize(t - n * glm::dot(n, t));

        float handedness = (glm::dot(glm::cross(n, tangent), b) < 0.0f) ? -1.0f : 1.0f;

        vertices[i].norm |= (handedness >= 0 ? 0 : 1) << 30;
        vertices[i].tn = pack_tangent(tangent);
    }
}

void load_scene(
    const std::filesystem::path&         path,
    std::vector<Mesh>&                   meshes,
    std::vector<Material>&               materials,
    std::vector<MeshInstance>&           instances,
    const Buffer&                        staging_buffer,
    const Buffer&                        indirect_vertex_buffer,
    VkDeviceSize&                        indirect_vertex_buffer_offset,
    const Buffer&                        indirect_index_buffer,
    VkDeviceSize&                        indirect_index_buffer_offset,
    const Buffer&                        meshlet_bufffer,
    VkDeviceSize&                        meshlet_buffer_offset,
    const Buffer&                        meshlet_vertex_indices,
    VkDeviceSize&                        meshlet_vertex_indices_offset,
    const Buffer&                        meshlet_primitive_indices,
    VkDeviceSize&                        meshlet_primitive_indices_offset,
    const Buffer&                        meshlet_bounds_buffer,
    VkDeviceSize&                        meshlet_bounds_buffer_offset,
    std::unordered_map<uint32_t, Image>& global_texture_cache,
    std::vector<Image>&                  loaded_images,
    JPH::PhysicsSystem*                  physics_system,
    std::vector<JPH::BodyID>&            static_bodies,
    VmaAllocator                         allocator,
    VkCommandBuffer                      command_buffer,
    VkQueue                              queue,
    VkDevice                             device
) {
    ZoneScopedN("Load Scene");
    spdlog::info("Loading scene: {}", path.string());

    tinygltf::TinyGLTF loader;
    tinygltf::Model    model;
    std::string        error;
    std::string        warning;

    bool ret = false;
    {
        ZoneScopedN("Read Scene File");
        if (path.extension() == ".gltf") {
            ret = loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
        } else if (path.extension() == ".glb") {
            ret = loader.LoadBinaryFromFile(&model, &error, &warning, path.string());
        }
        if (!warning.empty()) {
            spdlog::warn("Warning loading model {}: {}", path.string(), warning);
        }

        if (!error.empty()) {
            spdlog::error("Error loading model {}: {}", path.string(), error);
        }

        if (!ret) {
            spdlog::error("Failed loading model {}", path.string());
        }
    }

    uint32_t                               local_cache_offset = global_texture_cache.size();
    std::unordered_map<uint32_t, uint32_t> local_texture_cache;

    materials.resize(model.materials.size());
    spdlog::info("Loading {} materials", materials.size());

    void* staging_buffer_ptr = nullptr;
    VK_CHECK(vmaMapMemory(allocator, staging_buffer.allocation, &staging_buffer_ptr));
    for (int i = 0; i < model.materials.size(); i++) {
        ZoneScopedN("Load Material");
        auto& mat = model.materials[i];

        auto upload_texture = [&](int texture_index, VkFormat format) {
            if (texture_index < 0) {
                return 0u;
            }

            tinygltf::Texture& texture = model.textures[texture_index];

            int image_index = texture.source;
            if (image_index < 0) {
                return 0u;
            }

            auto local_it = local_texture_cache.find(image_index + local_cache_offset);
            if (local_it != local_texture_cache.end()) {
                return local_it->second;
            } else {
                tinygltf::Image& img = model.images[image_index];

                Image image = create_image(
                    format,
                    static_cast<uint32_t>(img.width),
                    static_cast<uint32_t>(img.height),
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    true,
                    allocator,
                    device
                );

                if (img.image.size() > staging_buffer.size) {
                    spdlog::error(
                        "Attempted out of bounds image buffer write for image, size={}, staging size={}",
                        img.image.size(),
                        staging_buffer.size
                    );
                    exit(1);
                }

                memcpy(staging_buffer_ptr, &img.image.at(0), img.image.size());
                copy_image(staging_buffer, image, true, command_buffer, queue, device);

                uint32_t index = global_texture_cache.size();
                global_texture_cache.insert({index, image});
                local_texture_cache.insert({image_index + local_cache_offset, index});

                loaded_images.push_back(image);

                return index;
            };
        };

        uint32_t albedo_index =
            upload_texture(mat.pbrMetallicRoughness.baseColorTexture.index, VK_FORMAT_R8G8B8A8_SRGB);
        uint32_t material_index =
            upload_texture(mat.pbrMetallicRoughness.metallicRoughnessTexture.index, VK_FORMAT_R8G8B8A8_UNORM);
        uint32_t normals_index  = upload_texture(mat.normalTexture.index, VK_FORMAT_R8G8B8A8_UNORM);
        uint32_t emissive_index = upload_texture(mat.emissiveTexture.index, VK_FORMAT_R8G8B8A8_SRGB);

        materials[i].albedo_index     = albedo_index;
        materials[i].normals_index    = normals_index;
        materials[i].material_index   = material_index;
        materials[i].emissive_index   = emissive_index;
        materials[i].roughness_factor = mat.pbrMetallicRoughness.roughnessFactor;
        materials[i].metallic_factor  = mat.pbrMetallicRoughness.metallicFactor;
        materials[i].emissive_factor  = glm::vec3(mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2]);
        materials[i].albedo_factor    = glm::vec4(
            mat.pbrMetallicRoughness.baseColorFactor[0],
            mat.pbrMetallicRoughness.baseColorFactor[1],
            mat.pbrMetallicRoughness.baseColorFactor[2],
            mat.pbrMetallicRoughness.baseColorFactor[3]
        );
        materials[i].normal_scale = mat.normalTexture.scale;
    }

    int                         current_entry = 0;
    std::vector<int>            mesh_primitive_offsets(model.meshes.size());
    std::vector<JPH::ShapeRefC> mesh_collision_shapes;

    spdlog::info("Loading {} meshes", model.meshes.size());
    for (int m = 0; m < model.meshes.size(); m++) {
        ZoneScopedN("Load Mesh");
        spdlog::trace("mesh {}", m);
        const tinygltf::Mesh& mesh = model.meshes[m];

        mesh_primitive_offsets[m] = current_entry;

        for (int p = 0; p < mesh.primitives.size(); p++) {
            ZoneScopedN("Load Primitive");
            spdlog::trace("primitive {}", p);
            std::vector<Vertex>   vertices;
            std::vector<uint32_t> indices;

            const tinygltf::Primitive& primitive = mesh.primitives[p];

            auto has_position  = primitive.attributes.count("POSITION");
            auto has_normals   = primitive.attributes.count("NORMAL");
            auto has_texcoords = primitive.attributes.count("TEXCOORD_0");
            auto has_tangents  = primitive.attributes.count("TANGENT");

            if (!(has_position & has_normals & has_texcoords)) {
                spdlog::warn("Mesh {} primitive {} is missing required attributes", m, p);
                continue;
            }

            const tinygltf::Accessor& pos_accessor      = model.accessors[primitive.attributes.at("POSITION")];
            const tinygltf::Accessor& normal_accessor   = model.accessors[primitive.attributes.at("NORMAL")];
            const tinygltf::Accessor& texcoord_accessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];

            const tinygltf::BufferView& pos_view      = model.bufferViews[pos_accessor.bufferView];
            const tinygltf::BufferView& normal_view   = model.bufferViews[normal_accessor.bufferView];
            const tinygltf::BufferView& texcoord_view = model.bufferViews[texcoord_accessor.bufferView];

            const tinygltf::Buffer& pos_buffer      = model.buffers[pos_view.buffer];
            const tinygltf::Buffer& normal_buffer   = model.buffers[normal_view.buffer];
            const tinygltf::Buffer& texcoord_buffer = model.buffers[texcoord_view.buffer];

            size_t vertex_count = pos_accessor.count;

            auto* positions =
                reinterpret_cast<const float*>(&pos_buffer.data[pos_view.byteOffset + pos_accessor.byteOffset]);

            auto* normals = reinterpret_cast<const float*>(
                &normal_buffer.data[normal_view.byteOffset + normal_accessor.byteOffset]
            );

            auto* texcoords = reinterpret_cast<const float*>(
                &texcoord_buffer.data[texcoord_view.byteOffset + texcoord_accessor.byteOffset]
            );

            const float* tangents = nullptr;

            if (has_tangents) {
                const tinygltf::Accessor&   tangent_accessor = model.accessors[primitive.attributes.at("TANGENT")];
                const tinygltf::BufferView& tangent_view     = model.bufferViews[tangent_accessor.bufferView];
                const tinygltf::Buffer&     tangent_buffer   = model.buffers[tangent_view.buffer];

                tangents = reinterpret_cast<const float*>(
                    &tangent_buffer.data[tangent_view.byteOffset + tangent_accessor.byteOffset]
                );
            }

            glm::vec3 center     = glm::vec3(0);
            glm::vec3 bounds_min = glm::vec3(std::numeric_limits<float>::max());
            glm::vec3 bounds_max = glm::vec3(std::numeric_limits<float>::lowest());

            for (size_t i = 0; i < vertex_count; i++) {
                glm::vec3 position = {
                    positions[i * 3 + 0],
                    positions[i * 3 + 1],
                    positions[i * 3 + 2],
                };

                glm::vec4 tangent_sign = glm::vec4(0.0);
                if (has_tangents) {
                    tangent_sign = {
                        tangents[i * 4 + 0],
                        tangents[i * 4 + 1],
                        tangents[i * 4 + 2],
                        tangents[i * 4 + 3],
                    };
                }

                center += position;
                bounds_min = glm::min(bounds_min, position);
                bounds_max = glm::max(bounds_max, position);

                vertices.emplace_back(
                    Vertex{
                        .px   = meshopt_quantizeHalf(position.x),
                        .py   = meshopt_quantizeHalf(position.y),
                        .pz   = meshopt_quantizeHalf(position.z),
                        .ux   = meshopt_quantizeHalf(texcoords[i * 2 + 0]),
                        .uy   = meshopt_quantizeHalf(texcoords[i * 2 + 1]),
                        .tn   = pack_tangent(glm::vec3(tangent_sign)),
                        .norm = static_cast<uint32_t>(
                            (meshopt_quantizeSnorm(normals[i * 3 + 0], 10) + 511) |
                            (meshopt_quantizeSnorm(normals[i * 3 + 1], 10) + 511) << 10 |
                            (meshopt_quantizeSnorm(normals[i * 3 + 2], 10) + 511) << 20 |
                            (tangent_sign.w >= 0 ? 0 : 1) << 30
                        ),
                    }
                );
            }

            center /= float(vertex_count);

            if (primitive.indices >= 0) {
                const tinygltf::Accessor&   index_accessor = model.accessors[primitive.indices];
                const tinygltf::BufferView& index_view     = model.bufferViews[index_accessor.bufferView];
                const tinygltf::Buffer&     index_buffer   = model.buffers[index_view.buffer];

                size_t index_count = index_accessor.count;

                switch (index_accessor.componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                    const uint16_t* short_indices = reinterpret_cast<const uint16_t*>(
                        &index_buffer.data[index_view.byteOffset + index_accessor.byteOffset]
                    );
                    for (size_t i = 0; i < index_count; i++) {
                        indices.push_back(static_cast<uint32_t>(short_indices[i]));
                    }
                    break;
                }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                    const uint32_t* long_indices = reinterpret_cast<const uint32_t*>(
                        &index_buffer.data[index_view.byteOffset + index_accessor.byteOffset]
                    );

                    for (int ind = 0; ind < index_count; ind++) {
                        indices.push_back(long_indices[ind]);
                    }
                    break;
                }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                    const uint8_t* byte_indices = reinterpret_cast<const uint8_t*>(
                        &index_buffer.data[index_view.byteOffset + index_accessor.byteOffset]
                    );
                    for (size_t i = 0; i < index_count; i++) {
                        indices.push_back(static_cast<uint32_t>(byte_indices[i]));
                    }
                    break;
                }
                }
            }

            if (!has_tangents) {
                generate_tangents(vertices, indices);
            }

            std::vector<uint32_t> remap_table(vertices.size());
            auto                  unique_vertices = meshopt_generateVertexRemap(
                remap_table.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex)
            );

            float remap_reduction = (((float)unique_vertices / (float)vertices.size()) * 100.0f);
            remap_reduction       = 100.0f - remap_reduction;
            if (remap_reduction > 0.0f) {
                spdlog::info("Remap table reduction: {:03.2f}%", remap_reduction);
            }

            meshopt_remapVertexBuffer(
                vertices.data(), vertices.data(), vertices.size(), sizeof(Vertex), remap_table.data()
            );
            meshopt_remapIndexBuffer(indices.data(), indices.data(), indices.size(), remap_table.data());
            vertices.resize(unique_vertices);

            meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertices.size());
            meshopt_optimizeVertexFetch(
                vertices.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex)
            );

            std::vector<glm::vec3> unquantized_positions(vertices.size());
            for (int i = 0; i < vertices.size(); i++) {
                Vertex& v = vertices[i];
                unquantized_positions[i] =
                    glm::vec3(meshopt_dequantizeHalf(v.px), meshopt_dequantizeHalf(v.py), meshopt_dequantizeHalf(v.pz));
            }

            float radius = 0.0f;
            for (const auto& pos : unquantized_positions) {
                radius = std::max(radius, glm::distance(center, pos));
            }

            JPH::TriangleList triangles;
            for (size_t i = 0; i < indices.size(); i += 3) {
                const glm::vec3& v0 = unquantized_positions[indices[i + 0]];
                const glm::vec3& v1 = unquantized_positions[indices[i + 1]];
                const glm::vec3& v2 = unquantized_positions[indices[i + 2]];

                triangles.push_back(
                    JPH::Triangle(
                        JPH::Float3(v0.x, v0.y, v0.z), JPH::Float3(v1.x, v1.y, v1.z), JPH::Float3(v2.x, v2.y, v2.z)
                    )
                );
            }

            JPH::MeshShapeSettings mesh_settings(triangles);
            JPH::ShapeRefC         collision_shape = mesh_settings.Create().Get();
            mesh_collision_shapes.push_back(collision_shape);

            VkDeviceSize mesh_vertex_offset = indirect_vertex_buffer_offset;
            VkDeviceSize mesh_index_offset  = indirect_index_buffer_offset;

            memcpy(staging_buffer_ptr, vertices.data(), sizeof(Vertex) * vertices.size());
            copy_buffer(
                staging_buffer,
                indirect_vertex_buffer,
                command_buffer,
                queue,
                device,
                staging_buffer_ptr,
                sizeof(Vertex) * vertices.size(),
                indirect_vertex_buffer_offset
            );
            indirect_vertex_buffer_offset += sizeof(Vertex) * vertices.size();

            spdlog::debug("Copying indices into global buffer");
            memcpy(staging_buffer_ptr, indices.data(), sizeof(uint32_t) * indices.size());
            copy_buffer(
                staging_buffer,
                indirect_index_buffer,
                command_buffer,
                queue,
                device,
                staging_buffer_ptr,
                sizeof(uint32_t) * indices.size(),
                indirect_index_buffer_offset
            );
            indirect_index_buffer_offset += sizeof(uint32_t) * indices.size();

            if (primitive.material >= 0) {
                const tinygltf::Material& material = model.materials[primitive.material];
            }

            spdlog::debug("Building meshlets");
            const size_t max_vertices  = 64;
            const size_t max_triangles = 124;
            const float  cone_weight   = 0.25f;

            size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_vertices, max_triangles);
            spdlog::trace("Max meshlets {}", max_meshlets);

            std::vector<meshopt_Meshlet> meshlets(max_meshlets);
            std::vector<unsigned int>    meshlet_vertices(max_meshlets * max_vertices);
            std::vector<unsigned char>   meshlet_triangles(max_meshlets * max_triangles * 3);

            size_t meshlet_count = meshopt_buildMeshlets(
                meshlets.data(),
                meshlet_vertices.data(),
                meshlet_triangles.data(),
                indices.data(),
                indices.size(),
                (float*)unquantized_positions.data(),
                unquantized_positions.size(),
                sizeof(glm::vec3),
                max_vertices,
                max_triangles,
                cone_weight
            );

            auto& last_meshlet = meshlets[meshlet_count - 1];
            meshlet_vertices.resize(last_meshlet.vertex_offset + last_meshlet.vertex_count);
            meshlet_triangles.resize(last_meshlet.triangle_offset + ((last_meshlet.triangle_count * 3 + 3) & ~3));
            meshlets.resize(meshlet_count);
            std::vector<MeshletBounds> meshlet_bounds(meshlet_count);

            uint32_t idx = 0;
            for (auto& m : meshlets) {
                meshopt_optimizeMeshlet(
                    &meshlet_vertices[m.vertex_offset],
                    &meshlet_triangles[m.triangle_offset],
                    m.triangle_count,
                    m.vertex_count
                );

                auto bounds = meshopt_computeMeshletBounds(
                    &meshlet_vertices[m.vertex_offset],
                    &meshlet_triangles[m.triangle_offset],
                    m.triangle_count,
                    (float*)unquantized_positions.data(),
                    unquantized_positions.size(),
                    sizeof(glm::vec3)
                );

                meshlet_bounds[idx++] = MeshletBounds{
                    .center      = {bounds.center[0], bounds.center[1], bounds.center[2]},
                    .radius      = bounds.radius,
                    .cone_axis   = {bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]},
                    .cone_cutoff = bounds.cone_cutoff,
                    .cone_apex   = {bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]},
                };
            }

            size_t global_vertex_indices_offset    = meshlet_vertex_indices_offset / sizeof(unsigned int);
            size_t global_primitive_indices_offset = meshlet_primitive_indices_offset;

            for (size_t i = 0; i < meshlet_count; i++) {
                meshlets[i].vertex_offset += global_vertex_indices_offset;
                meshlets[i].triangle_offset += global_primitive_indices_offset;
            }

            for (size_t i = 0; i < meshlet_vertices.size(); i++) {
                meshlet_vertices[i] += mesh_vertex_offset / sizeof(Vertex);
            }

            uint32_t current_meshlet_offset = meshlet_buffer_offset / sizeof(meshopt_Meshlet);

            spdlog::trace(
                "Mesh {}: meshlet_count={}, meshlet_offset={}, vertex_buf_size={}MB, index_buf_size={}MB",
                meshes.size(),
                meshlet_count,
                current_meshlet_offset,
                (vertices.size() * sizeof(Vertex)) / 1024.0f / 1024.f,
                (sizeof(uint32_t) * indices.size()) / 1024.0f / 1024.0f
            );
            spdlog::trace(
                "  global_vertex_indices_offset={}, global_primitive_indices_offset={}",
                global_vertex_indices_offset,
                global_primitive_indices_offset
            );
            spdlog::trace(
                "  First meshlet: v_off={}, t_off={}, v_cnt={}, t_cnt={}",
                meshlets[0].vertex_offset,
                meshlets[0].triangle_offset,
                meshlets[0].vertex_count,
                meshlets[0].triangle_count
            );
            if (meshlet_count > 1) {
                spdlog::trace(
                    "  Last meshlet: v_off={}, t_off={}, v_cnt={}, t_cnt={}",
                    meshlets[meshlet_count - 1].vertex_offset,
                    meshlets[meshlet_count - 1].triangle_offset,
                    meshlets[meshlet_count - 1].vertex_count,
                    meshlets[meshlet_count - 1].triangle_count
                );
            }

            spdlog::debug("Copying meshlets into meshlet buffer");
            memcpy(staging_buffer_ptr, meshlets.data(), sizeof(meshopt_Meshlet) * meshlets.size());
            copy_buffer(
                staging_buffer,
                meshlet_bufffer,
                command_buffer,
                queue,
                device,
                staging_buffer_ptr,
                sizeof(meshopt_Meshlet) * meshlets.size(),
                meshlet_buffer_offset
            );
            meshlet_buffer_offset += sizeof(meshopt_Meshlet) * meshlets.size();

            spdlog::debug("Copying meshlet bounds into meshlet bounds buffer");
            memcpy(staging_buffer_ptr, meshlet_bounds.data(), sizeof(MeshletBounds) * meshlet_bounds.size());
            copy_buffer(
                staging_buffer,
                meshlet_bounds_buffer,
                command_buffer,
                queue,
                device,
                staging_buffer_ptr,
                sizeof(MeshletBounds) * meshlet_bounds.size(),
                meshlet_bounds_buffer_offset
            );
            meshlet_bounds_buffer_offset += sizeof(MeshletBounds) * meshlet_bounds.size();

            spdlog::debug("Copying meshlet vertices into meshlet buffer");
            memcpy(staging_buffer_ptr, meshlet_vertices.data(), sizeof(unsigned int) * meshlet_vertices.size());
            copy_buffer(
                staging_buffer,
                meshlet_vertex_indices,
                command_buffer,
                queue,
                device,
                staging_buffer_ptr,
                sizeof(unsigned int) * meshlet_vertices.size(),
                meshlet_vertex_indices_offset
            );
            meshlet_vertex_indices_offset += sizeof(unsigned int) * meshlet_vertices.size();

            spdlog::debug("Copying meshlet triangle indices into meshlet buffer");
            memcpy(staging_buffer_ptr, meshlet_triangles.data(), sizeof(unsigned char) * meshlet_triangles.size());
            copy_buffer(
                staging_buffer,
                meshlet_primitive_indices,
                command_buffer,
                queue,
                device,
                staging_buffer_ptr,
                sizeof(unsigned char) * meshlet_triangles.size(),
                meshlet_primitive_indices_offset
            );
            meshlet_primitive_indices_offset += sizeof(unsigned char) * meshlet_triangles.size();

            spdlog::debug("Loaded mesh {} with vertices={}, indices={}", m, vertices.size(), indices.size());

            meshes.emplace_back(
                current_meshlet_offset,
                meshlet_count,
                vertices.size(),
                static_cast<int32_t>(mesh_vertex_offset / sizeof(Vertex)),
                indices.size(),
                static_cast<uint32_t>(mesh_index_offset / sizeof(uint32_t)),
                center,
                radius,
                bounds_min,
                bounds_max
            );

            current_entry++;
        }
    }

    vmaUnmapMemory(allocator, staging_buffer.allocation);

    spdlog::info("Scene count: {}", model.scenes.size());
    int scene_id = model.defaultScene >= 0 ? model.defaultScene : (model.scenes.size() >= 1 ? 0 : -1);
    if (scene_id <= -1) {
        spdlog::warn("No eligibles scenes found");
        return;
    }

    spdlog::info("Constructing scene");
    const tinygltf::Scene& scene = model.scenes[scene_id];

    for (int node_id : scene.nodes) {
        ZoneScopedN("Parse Scene Node");
        const auto& node = model.nodes[node_id];
        if (node.mesh <= -1)
            continue;

        const auto& mesh = model.meshes[node.mesh];

        glm::vec3 position = glm::vec3(0.0);
        if (node.translation.size() == 3) {
            position = {node.translation[0], node.translation[1], node.translation[2]};
        } else {
        }

        float scale = 1.0f;
        if (node.scale.size() == 3) {
            // TODO: handle this more gracefully
            scale = node.scale[0];
        }

        glm::quat rotation = glm::quat(0.0f, 0.0f, 0.0f, 1.0f);
        if (node.rotation.size() == 4) {
            rotation = glm::quat(node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]);
        }

        if (node.children.size() > 0) {
            spdlog::warn("{} children", node.children.size());
        }

        for (int i = 0; i < mesh.primitives.size(); i++) {
            int mesh_id = mesh_primitive_offsets[node.mesh] + i;

            MeshInstance instance = {
                .mesh_id     = mesh_id,
                .material_id = mesh.primitives[i].material,
                .position    = position,
                .scale       = scale,
                .rotation    = rotation,
            };
            instances.push_back(instance);

            JPH::ShapeRefC final_shape;
            if (scale != 1.0f) {
                JPH::ScaledShapeSettings scaled(mesh_collision_shapes[mesh_id], JPH::Vec3::sReplicate(scale));
                final_shape = scaled.Create().Get();
            } else {
                final_shape = mesh_collision_shapes[mesh_id];
            }

            JPH::BodyInterface& body_interface = physics_system->GetBodyInterface();

            JPH::BodyCreationSettings body_settings(
                final_shape,
                JPH::RVec3(position.x, position.y, position.z),
                JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
                JPH::EMotionType::Static,
                Layers::NON_MOVING
            );

            JPH::Body* body = body_interface.CreateBody(body_settings);
            if (body) {
                body_interface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
                static_bodies.push_back(body->GetID());
            }
        }
    }

    if (instances.size() == 0) {
        for (int m = 0; m < model.meshes.size(); m++) {
            auto& mesh = model.meshes[m];
            for (int i = 0; i < mesh.primitives.size(); i++) {
                int mesh_id = mesh_primitive_offsets[m] + i;

                auto rot = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
                rot *= glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0));

                MeshInstance instance = {
                    .mesh_id     = mesh_id,
                    .material_id = mesh.primitives[i].material,
                    .position    = {0, 0, 0},
                    .scale       = 0.01,
                    .rotation    = rot,
                };
                instances.push_back(instance);
            }
        }

        return;
    }
}

void populate_materials(
    const std::unordered_map<uint32_t, Image>& texture_cache,
    VkDescriptorSet                            descriptor_set,
    VkSampler                                  sampler,
    VkDevice                                   device
) {
    spdlog::info("Texture cache contains: {} textures", texture_cache.size());

    for (auto& [slot, image] : texture_cache) {
        VkDescriptorImageInfo image_write_info = {
            .sampler = sampler, .imageView = image.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };

        VkWriteDescriptorSet write_set = {
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = nullptr,
            .dstSet           = descriptor_set,
            .dstBinding       = 0,
            .dstArrayElement  = slot,
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

    auto* window = SDL_CreateWindow("Ember", 2550, 1440, SDL_WINDOW_VULKAN);
    if (!window) {
        spdlog::error("Failed to create SDL window");
        return 1;
    }

    const int FRAMES_IN_FLIGHT = 2;

    bool use_meshlets    = true;
    bool use_hardware_rt = true;

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

    spdlog::info("Extension support:\n\tMesh shading: {}\n\tRay tracing: {}", use_meshlets, use_hardware_rt);

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
            .descriptorCount = 40,
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
        .maxAnisotropy           = 16.0f,
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

    sampler_info.addressModeU           = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV           = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW           = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable       = VK_TRUE;
    VkSampler linear_sampler_anisotropy = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &linear_sampler_anisotropy));

    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

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
           .grid_origin      = {0, 15, 0},
           .probe_spacing    = 5.0f,
           .probe_counts     = {16, 8, 16},
           .texels_per_probe = 6,
           .camera_pos       = {},
           .frame_index      = {},
    };

    lighting_data.use_bent_normals         = 1;
    lighting_data.use_probe_state          = 1;
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
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
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

    Image ao_output_denoised_pong = create_image(
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
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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

    Image blue_noise_texure = load_image(
        "data/textures/LDR_RGBA_0.png",
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

    Image specular_cubemap = create_cubemap_image(
        VK_FORMAT_R32G32B32A32_SFLOAT,
        512,
        512,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        true,
        vma_allocator,
        device
    );

    Image irradiance_cubemap = create_cubemap_image(
        VK_FORMAT_R32G32B32A32_SFLOAT,
        32,
        32,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
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

        image_pipeline_barrier(
            ddgi_depth_atlas,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT
        );

        image_pipeline_barrier(
            ddgi_depth_atlas_history,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT
        );

        image_pipeline_barrier(
            rt_reflection_history,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT
        );

        image_pipeline_barrier(
            ddgi_irradiance,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT
        );

        image_pipeline_barrier(
            ddgi_irradiance_history,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT
        );

        image_pipeline_barrier(
            directional_shadow_buffer,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT
        );

        image_pipeline_barrier(
            smaa_edges,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT
        );

        image_pipeline_barrier(
            smaa_weights,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT
        );

        image_pipeline_barrier(
            smaa_output,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT
        );

        image_pipeline_barrier(
            average_luminance_image,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT
        );

        VkClearColorValue       clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
        VkImageSubresourceRange range       = {
                  .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                  .baseMipLevel   = 0,
                  .levelCount     = 1,
                  .baseArrayLayer = 0,
                  .layerCount     = 1
        };

        vkCmdClearColorImage(
            command_buffer,
            ddgi_irradiance_history.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clear_color,
            1,
            &range
        );
        vkCmdClearColorImage(
            command_buffer, ddgi_irradiance.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range
        );

        vkCmdClearColorImage(
            command_buffer, ddgi_depth_atlas.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range
        );

        vkCmdClearColorImage(
            command_buffer, smaa_output.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range
        );

        vkCmdClearColorImage(
            command_buffer, smaa_edges.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range
        );

        vkCmdClearColorImage(
            command_buffer, smaa_weights.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range
        );
        vkCmdClearColorImage(
            command_buffer, rt_reflection_history.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range
        );

        vkCmdClearColorImage(
            command_buffer,
            ddgi_depth_atlas_history.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clear_color,
            1,
            &range
        );

        clear_color.float32[0] = 0.5f;
        vkCmdClearColorImage(
            command_buffer,
            average_luminance_image.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clear_color,
            1,
            &range
        );

        image_pipeline_barrier(
            smaa_edges,
            command_buffer,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT
        );

        image_pipeline_barrier(
            smaa_weights,
            command_buffer,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT
        );

        image_pipeline_barrier(
            smaa_output,
            command_buffer,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT
        );

        image_pipeline_barrier(
            rt_reflection_buffer,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT
        );

        vkCmdClearColorImage(
            command_buffer,
            directional_shadow_buffer.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clear_color,
            1,
            &range
        );

        image_pipeline_barrier(
            directional_shadow_buffer,
            command_buffer,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT
        );

        image_pipeline_barrier(
            ddgi_irradiance_history,
            command_buffer,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT
        );

        image_pipeline_barrier(
            average_luminance_image,
            command_buffer,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT
        );

        image_pipeline_barrier(
            rt_reflection_history,
            command_buffer,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT
        );

        image_pipeline_barrier(
            ddgi_irradiance,
            command_buffer,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT
        );

        image_pipeline_barrier(
            ddgi_depth_atlas,
            command_buffer,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT
        );

        image_pipeline_barrier(
            ddgi_depth_atlas_history,
            command_buffer,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT
        );

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
            directional_shadow_buffer,
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
            ao_output_denoised_pong,
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

        image_pipeline_barrier(
            irradiance_cubemap,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0
        );

        image_pipeline_barrier(
            specular_cubemap,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0
        );

        image_pipeline_barrier(
            brdf_lut,
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

        // std::vector<VkImageView> specular_image_views(specular_cubemap.levels);
        // for (int i = 0; i < specular_image_views.size(); i++) {
        //     VkImageViewCreateInfo mip_view_info = {
        //         .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        //         .pNext            = nullptr,
        //         .flags            = 0,
        //         .image            = specular_cubemap.handle,
        //         .viewType         = VK_IMAGE_VIEW_TYPE_CUBE,
        //         .format           = specular_cubemap.format,
        //         .subresourceRange = {
        //             .aspectMask     = specular_cubemap.aspect,
        //             .baseMipLevel   = static_cast<uint32_t>(i),
        //             .levelCount     = 1,
        //             .baseArrayLayer = 0,
        //             .layerCount     = 6
        //         },
        //     };
        //
        //     VK_CHECK(vkCreateImageView(device, &mip_view_info, nullptr, &specular_image_views[i]));
        // }

        Framegraph ibl_graph(device, graphics_queue, command_buffer, 1, false);
        // ibl_graph.import_image(irradiance_cubemap, VK_IMAGE_LAYOUT_GENERAL);
        // ibl_graph.import_image(specular_cubemap, VK_IMAGE_LAYOUT_GENERAL);
        ibl_graph.import_image(brdf_lut, VK_IMAGE_LAYOUT_GENERAL);
        //
        // Pipeline irradiance_pipeline = create_compute_pipeline(
        //     device,
        //     shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/ibl_irradiance.comp.spv"),
        //     {
        //         DescriptorLayout{
        //             .bindings = {
        //                 DescriptorBinding{
        //                     .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        //                     .write_info = DescriptorInfo(irradiance_cubemap.view, VK_IMAGE_LAYOUT_GENERAL)
        //                 },
        //                 DescriptorBinding{
        //                     .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        //                     .write_info = DescriptorInfo(
        //                         linear_sampler, skybox_image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        //                     )
        //                 },
        //             }
        //         },
        //     }
        // );
        // std::vector<VkDescriptorSet> sets = allocate_descriptor_sets(device, descriptor_pool, irradiance_pipeline);
        //
        // auto irradiance_pass =
        //     ibl_graph.add_pass("irradiance")
        //         .samples_image(skybox_image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
        //         .writes_storage_image(irradiance_cubemap, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
        //         .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
        //             vkCmdBindPipeline(
        //                 command_buffer, irradiance_pipeline.bind_point, irradiance_pipeline.pipeline_handle
        //             );
        //             vkCmdBindDescriptorSets(
        //                 command_buffer,
        //                 irradiance_pipeline.bind_point,
        //                 irradiance_pipeline.pipeline_layout,
        //                 0,
        //                 sets.size(),
        //                 sets.data(),
        //                 0,
        //                 nullptr
        //             );
        //
        //             uint32_t resolution = irradiance_cubemap.width;
        //             vkCmdDispatch(
        //                 command_buffer,
        //                 (resolution + 15) / 16, // X
        //                 (resolution + 15) / 16, // Y
        //                 6
        //             );
        //
        //             image_pipeline_barrier(
        //                 irradiance_cubemap,
        //                 command_buffer,
        //                 VK_IMAGE_LAYOUT_GENERAL,
        //                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        //                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        //                 VK_ACCESS_2_SHADER_WRITE_BIT,
        //                 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        //                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
        //             );
        //         });
        //
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
        //
        // struct IBLConstants {
        //     uint32_t face;
        //     uint32_t mip_level;
        //     float    roughness;
        // } ibl_constants;
        //
        // Pipeline specular_pipeline = create_compute_pipeline(
        //     device,
        //     shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/ibl_specular.comp.spv"),
        //     {
        //         DescriptorLayout{
        //             .bindings =
        //                 {
        //                     DescriptorBinding{
        //                         .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        //                         .write_info = DescriptorInfo(specular_cubemap.view, VK_IMAGE_LAYOUT_GENERAL)
        //                     },
        //                     DescriptorBinding{
        //                         .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        //                         .write_info = DescriptorInfo(
        //                             linear_sampler, skybox_image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        //                         )
        //                     },
        //                 },
        //             .is_push_set = true,
        //         },
        //     },
        //     sizeof(IBLConstants)
        // );
        //
        // auto specular_pass =
        //     ibl_graph.add_pass("ibl specular")
        //         .samples_image(skybox_image, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
        //         .writes_storage_image(specular_cubemap, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
        //         .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
        //             vkCmdBindPipeline(command_buffer, specular_pipeline.bind_point,
        //             specular_pipeline.pipeline_handle);
        //
        //             for (uint32_t mip = 0; mip < specular_cubemap.levels; ++mip) {
        //                 uint32_t mipSize   = specular_cubemap.width >> mip;
        //                 float    roughness = (float)mip / (float)(specular_cubemap.levels - 1);
        //
        //                 VkDescriptorImageInfo image_write_info = {
        //                     .sampler     = VK_NULL_HANDLE,
        //                     .imageView   = specular_image_views[mip],
        //                     .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        //                 };
        //
        //                 VkDescriptorImageInfo image_read_info = {
        //                     .sampler     = linear_sampler,
        //                     .imageView   = skybox_image.view,
        //                     .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        //                 };
        //
        //                 std::vector<VkWriteDescriptorSet> write_sets = {
        //                     {
        //                         .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        //                         .pNext            = nullptr,
        //                         .dstBinding       = 0,
        //                         .dstArrayElement  = 0,
        //                         .descriptorCount  = 1,
        //                         .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        //                         .pImageInfo       = &image_write_info,
        //                         .pBufferInfo      = nullptr,
        //                         .pTexelBufferView = nullptr,
        //                     },
        //                     {
        //                         .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        //                         .pNext            = nullptr,
        //                         .dstBinding       = 1,
        //                         .dstArrayElement  = 0,
        //                         .descriptorCount  = 1,
        //                         .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        //                         .pImageInfo       = &image_read_info,
        //                         .pBufferInfo      = nullptr,
        //                         .pTexelBufferView = nullptr,
        //                     },
        //                 };
        //
        //                 vkCmdPushDescriptorSet(
        //                     command_buffer,
        //                     VK_PIPELINE_BIND_POINT_COMPUTE,
        //                     specular_pipeline.pipeline_layout,
        //                     0,
        //                     static_cast<uint32_t>(write_sets.size()),
        //                     write_sets.data()
        //                 );
        //
        //                 for (uint32_t face = 0; face < 6; ++face) {
        //                     ibl_constants.face      = face;
        //                     ibl_constants.mip_level = mip;
        //                     ibl_constants.roughness = roughness;
        //
        //                     vkCmdPushConstants(
        //                         command_buffer,
        //                         specular_pipeline.pipeline_layout,
        //                         VK_SHADER_STAGE_COMPUTE_BIT,
        //                         0,
        //                         sizeof(IBLConstants),
        //                         &ibl_constants
        //                     );
        //
        //                     uint32_t groupCount = (mipSize + 15) / 16;
        //                     vkCmdDispatch(command_buffer, groupCount, groupCount, 1);
        //                 }
        //             }
        //         });
        //
        ibl_graph.build();
        ibl_graph.execute(command_buffer, 0);
        //
        // image_pipeline_barrier(
        //     specular_cubemap,
        //     command_buffer,
        //     VK_IMAGE_LAYOUT_GENERAL,
        //     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        //     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        //     0,
        //     VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        //     0
        // );

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

        // for (auto view : specular_image_views) {
        //     vkDestroyImageView(device, view, nullptr);
        // }

        destroy_pipeline(device, brdf_lut_pipeline);
        // destroy_pipeline(device, specular_pipeline);
        // destroy_pipeline(device, irradiance_pipeline);
    }

    Buffer global_vertex_buffer = create_buffer(
        1024 * 1024 * 164,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            (use_hardware_rt ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                             : 0),
        vma_allocator
    );

    Buffer global_index_buffer = create_buffer(
        1024 * 1024 * 164,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            (use_hardware_rt ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                             : 0),
        vma_allocator
    );

    uint32_t probe_count = lighting_data.probe_counts.x * lighting_data.probe_counts.y * lighting_data.probe_counts.z;
    Buffer   ddgi_ray_buffer = create_buffer(
        sizeof(DDGIRay) * probe_count * MAX_RAYS_PER_PROBE * FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        vma_allocator
    );
    spdlog::info("DDGI Ray buffer size: {}MB", ddgi_ray_buffer.size / 1024 / 1024);

    Buffer ddgi_probe_buffer = create_buffer(
        sizeof(DDGIProbe) * probe_count * FRAMES_IN_FLIGHT, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vma_allocator
    );
    spdlog::info("DDGI Probe buffer size: {}MB", ddgi_probe_buffer.size / 1024 / 1024);

    Buffer indirect_command_buffer = create_buffer(
        1024 * 1024 * 12 * FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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
        1024 * 1024 * 6 * FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    Buffer meshlet_buffer = create_buffer(
        1024 * 1024 * 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_allocator
    );

    Buffer meshlet_vertex_indices_buffer = create_buffer(
        1024 * 1024 * 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_allocator
    );

    Buffer meshlet_primitive_indices_buffer = create_buffer(
        1024 * 1024 * 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_allocator
    );

    Buffer meshlet_bounds_buffer = create_buffer(
        1024 * 1024 * 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_allocator
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

    VkDeviceSize meshlet_buffer_offset                   = 0;
    VkDeviceSize meshlet_vertex_indices_offset           = 0;
    VkDeviceSize meshlet_vertex_primitive_indices_offset = 0;
    VkDeviceSize meshlet_bounds_buffer_offset            = 0;

    VkDeviceSize indirect_vertex_buffer_offset = 0;
    VkDeviceSize indirect_index_buffer_offset  = 0;

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

    std::unordered_map<uint32_t, Image> texture_cache;
    std::vector<Image>                  loaded_images;

    Image missing_material = load_image(
        "data/textures/missing_material.png",
        VK_FORMAT_R8G8B8A8_UNORM,
        false,
        staging_buffer,
        command_buffers[0],
        graphics_queue,
        vma_allocator,
        device
    );

    // index 0 is a dummy texture that acts as a missing index
    texture_cache.insert({0, missing_material});
    loaded_images.push_back(missing_material);

    std::vector<Mesh>         meshes;
    std::vector<Material>     materials;
    std::vector<MeshInstance> mesh_instances;

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

    struct PhysicsObject {
        JPH::BodyID body_id;
        int         drawcall_id = -1;

        JPH::Vec3 last_position;
        JPH::Quat last_rotation;
    };
    std::vector<PhysicsObject> dynamic_bodies;
    std::vector<JPH::BodyID>   static_bodies;

    const float                             player_height          = 1.8f;
    const float                             player_radius          = 0.3f;
    const float                             player_speed           = 10.0f;
    const float                             player_sprint_modifier = 2.0f;
    const float                             player_jump_velocity   = 10.0f;
    JPH::Ref<JPH::CharacterVirtualSettings> character_settings     = new JPH::CharacterVirtualSettings();
    character_settings->mShape            = new JPH::CapsuleShape(player_height / 2.0f - player_radius, player_radius);
    character_settings->mMass             = 180.0f;
    character_settings->mMaxSlopeAngle    = JPH::DegreesToRadians(35.0f);
    character_settings->mMaxStrength      = 100.0f;
    character_settings->mCharacterPadding = 0.02f;
    character_settings->mPenetrationRecoverySpeed  = 1.0f;
    character_settings->mPredictiveContactDistance = 0.1f;

    JPH::CharacterVirtual* player_character = new JPH::CharacterVirtual(
        character_settings, JPH::RVec3(0.0, 0.0, 0.0), JPH::Quat::sIdentity(), 0, &physics_system
    );
    bool player_physics = false;

    const float physics_delta_time       = 1.0f / 60.0f;
    float       physics_time_accumulator = 0.0f;

    physics_system.OptimizeBroadPhase();

    std::string load_path = argc > 1 ? argv[1] : "data/models/room2.glb";

    load_scene(
        load_path,
        meshes,
        materials,
        mesh_instances,
        staging_buffer,
        global_vertex_buffer,
        indirect_vertex_buffer_offset,
        global_index_buffer,
        indirect_index_buffer_offset,
        meshlet_buffer,
        meshlet_buffer_offset,
        meshlet_vertex_indices_buffer,
        meshlet_vertex_indices_offset,
        meshlet_primitive_indices_buffer,
        meshlet_vertex_primitive_indices_offset,
        meshlet_bounds_buffer,
        meshlet_bounds_buffer_offset,
        texture_cache,
        loaded_images,
        &physics_system,
        static_bodies,
        vma_allocator,
        command_buffers[0],
        graphics_queue,
        device
    );

    spdlog::info(
        "Buffer usage:\n\tVertex: {}MB\n\tIndex: {}MB\n\tMeshlet: {}MB\n\tMeshlet Vertex: {}MB\n\tMeshlet Index: "
        "{}MB\n\tMeshlet Bounds: {}MB",
        indirect_vertex_buffer_offset / 1024 / 1024,
        indirect_index_buffer_offset / 1024 / 1024,
        meshlet_buffer_offset / 1024 / 1024,
        meshlet_vertex_indices_offset / 1024 / 1024,
        meshlet_vertex_primitive_indices_offset / 1024 / 1024,
        meshlet_bounds_buffer_offset / 1024 / 1024
    );

    populate_materials(texture_cache, global_texture_descriptor_set, linear_sampler_anisotropy, device);

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
            meshes,
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
    for (auto& [slot, image] : texture_cache) {
        imgui_material_image_handles.insert({slot, imgui_image_handle(image, linear_sampler_clamped)});
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

    bool      capturing_mouse = false;
    glm::vec2 mouse_pos       = {};

    std::array<bool, 512> pressed_keys    = {0};
    std::array<bool, 12>  pressed_buttons = {0};

    std::array<bool, 512> released_keys    = {1};
    std::array<bool, 12>  released_buttons = {1};

    ImGuizmo::OPERATION tranform_gizmo_op = ImGuizmo::OPERATION::TRANSLATE;

    bool      enable_transform_snap = true;
    glm::vec3 transform_snap        = glm::vec3(1.0f);
    int       grabbed_mesh          = -1;
    glm::vec2 grab_origin           = {};

    uint32_t frame_count = 0;
    uint32_t frame_index = 0;

    float delta_time      = 0.0f;
    auto  frame_timestamp = std::chrono::high_resolution_clock::now();

    float time_passed = 0.0f;

    float total_time = 0.0;

    uint32_t accumulated_fps = 0;
    uint32_t fps             = 0;

    glm::mat4 frozen_view = glm::mat4(1.0f);
    float     frozen_frustum[4];

    bool debug_frustum   = false;
    bool disable_culling = false;

    bool use_smaa = true;
    bool running  = true;

    int bloom_levels = glm::max(5ul, bloom_mip_views.size() - 5);

    std::array<uint64_t, 2> pipeline_stats;

    float min_log_lum    = -8.0f;
    float max_log_lum    = 4.0f;
    float adaption_speed = 1.1f;

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
                                nearest_sampler, ao_output_denoised.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
                            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                            .write_info =
                                DescriptorInfo(ddgi_probe_buffer.handle, 0, ddgi_probe_buffer.size / FRAMES_IN_FLIGHT)
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
        .bloom_strength       = 0.04f,
        .tonemapping_type     = 1,
        .key_value            = 0.18f,
        .min_exposure         = 0.1f,
        .max_exposure         = 10.0f,
        .enable_auto_exposure = true,
    };

    std::vector<uint32_t> dynamic_offsets;
    dynamic_offsets.resize(static_cast<uint32_t>(DynamicOffset::COUNT));

    spdlog::info("Creating framegraph");
    Framegraph framegraph(device, graphics_queue, command_buffers[0], FRAMES_IN_FLIGHT, supports_timestamp_queries);
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
    framegraph.import_image(ao_output_denoised_pong, VK_IMAGE_LAYOUT_GENERAL);
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

    auto tlas_rebuild_pass =
        framegraph.add_pass("RT structure rebuild")
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                rebuild_tlas(rt_scene, device, vma_allocator, command_buffer, frame_index, meshes, mesh_instances);
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
                TracyVkZone(tracy_vk_context, command_buffer, "Early Cull Pass");
                ZoneScopedN("Early Cull Pass");

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
                TracyVkZone(tracy_vk_context, command_buffer, "Gbuffer Pass");
                ZoneScopedN("Gbuffer Pass");

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
                        sizeof(VkDrawIndexedIndirectCommand)
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
                                      TracyVkZone(tracy_vk_context, command_buffer, "Depth Pyramid");
                                      ZoneScopedN("Depth Pyramid");

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
                TracyVkZone(tracy_vk_context, command_buffer, "AO Prefilter Pass");
                ZoneScopedN("AO Prefilter Pass");

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
                           TracyVkZone(tracy_vk_context, command_buffer, "AO Pass");
                           ZoneScopedN("AO Pass");

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
            .writes_storage_image(ao_output_denoised_pong, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                TracyVkZone(tracy_vk_context, command_buffer, "AO Denoise Pass");
                ZoneScopedN("AO Denoise Pass");

                vkCmdBindPipeline(command_buffer, ao_denoise_pipeline.bind_point, ao_denoise_pipeline.pipeline_handle);

                int denoise_passes = 2;

                VkImageView read_view  = ao_output.view;
                VkImageView write_view = ao_output_denoised_pong.view;

                for (int i = 0; i < denoise_passes; i++) {
                    xegtao_constants.final_pass = i == denoise_passes - 1;

                    VkDescriptorImageInfo image_read_info = {
                        .sampler     = nearest_sampler,
                        .imageView   = read_view,
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    };

                    VkDescriptorImageInfo edges_info = {
                        .sampler     = nearest_sampler,
                        .imageView   = ao_output_edges.view,
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    };

                    VkDescriptorImageInfo image_write_info = {
                        .sampler     = VK_NULL_HANDLE,
                        .imageView   = write_view,
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

                    if (i != denoise_passes - 1) {
                        image_pipeline_barrier(
                            ao_output_denoised_pong,
                            command_buffer,
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
                        );
                    }

                    read_view  = ao_output_denoised_pong.view;
                    write_view = ao_output_denoised.view;
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
                            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                            .write_info =
                                DescriptorInfo(ddgi_ray_buffer.handle, 0, ddgi_ray_buffer.size / FRAMES_IN_FLIGHT)
                        },
                        DescriptorBinding{
                            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                            .write_info =
                                DescriptorInfo(ddgi_probe_buffer.handle, 0, ddgi_probe_buffer.size / FRAMES_IN_FLIGHT)
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
            .writes_buffer_dynamic(
                ddgi_ray_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                ddgi_ray_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                ddgi_probe_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                ddgi_probe_buffer.size / FRAMES_IN_FLIGHT
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
            .samples_image(ddgi_irradiance_history, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(ddgi_depth_atlas_history, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                TracyVkZone(tracy_vk_context, command_buffer, "DDGI Pass");
                ZoneScopedN("DDGI Pass");

                const Pipeline& pipeline = ddgi_ray_pipeline;

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);

                std::array<VkDescriptorSet, 4> sets = {
                    ddgi_ray_descriptor_sets[0],
                    ddgi_ray_descriptor_sets[1],
                    ddgi_ray_descriptor_sets[2],
                    global_texture_descriptor_set,
                };

                std::array<uint32_t, 7> offsets = {
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_RAY_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_PROBE_BUFFER)],
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
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                        .write_info = DescriptorInfo(ddgi_ray_buffer.handle, 0, ddgi_ray_buffer.size / FRAMES_IN_FLIGHT)
                    },
                    DescriptorBinding{
                        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                        .write_info =
                            DescriptorInfo(ddgi_probe_buffer.handle, 0, ddgi_probe_buffer.size / FRAMES_IN_FLIGHT)
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
            .reads_buffer_dynamic(
                ddgi_ray_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                ddgi_ray_buffer.size / FRAMES_IN_FLIGHT
            )
            .writes_buffer_dynamic(
                ddgi_probe_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                ddgi_probe_buffer.size / FRAMES_IN_FLIGHT
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
                TracyVkZone(tracy_vk_context, command_buffer, "DDGI Pass");
                ZoneScopedN("DDGI Pass");

                const Pipeline& pipeline = ddgi_probe_blend_depth_pipeline;

                std::array<uint32_t, 3> offsets = {
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_RAY_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_PROBE_BUFFER)],
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
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                        .write_info = DescriptorInfo(ddgi_ray_buffer.handle, 0, ddgi_ray_buffer.size / FRAMES_IN_FLIGHT)
                    },
                    DescriptorBinding{
                        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                        .write_info =
                            DescriptorInfo(ddgi_probe_buffer.handle, 0, ddgi_probe_buffer.size / FRAMES_IN_FLIGHT)
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
            .reads_buffer_dynamic(
                ddgi_ray_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                ddgi_ray_buffer.size / FRAMES_IN_FLIGHT
            )
            .writes_buffer_dynamic(
                ddgi_probe_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                ddgi_probe_buffer.size / FRAMES_IN_FLIGHT
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
                TracyVkZone(tracy_vk_context, command_buffer, "DDGI Pass");
                ZoneScopedN("DDGI Pass");

                const Pipeline& pipeline = ddgi_probe_blend_irradiance_pipeline;

                std::array<uint32_t, 3> offsets = {
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_RAY_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_PROBE_BUFFER)],
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
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                        .write_info = DescriptorInfo(ddgi_ray_buffer.handle, 0, ddgi_ray_buffer.size / FRAMES_IN_FLIGHT)
                    },
                    DescriptorBinding{
                        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                        .write_info =
                            DescriptorInfo(ddgi_probe_buffer.handle, 0, ddgi_probe_buffer.size / FRAMES_IN_FLIGHT)
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
            .reads_buffer_dynamic(
                ddgi_ray_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                ddgi_ray_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                ddgi_probe_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                ddgi_probe_buffer.size / FRAMES_IN_FLIGHT
            )
            .writes_buffer_dynamic(
                ddgi_probe_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                ddgi_probe_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                lighting_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                TracyVkZone(tracy_vk_context, command_buffer, "DDGI Pass");
                ZoneScopedN("DDGI Pass");

                const Pipeline& pipeline = ddgi_probe_relocate_pipeline;

                std::array<uint32_t, 3> offsets = {
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_RAY_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_PROBE_BUFFER)],
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

    Pipeline ddgi_probe_classify_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/ddgi_classify.comp.spv"),
        {
            DescriptorLayout{
                .bindings = {
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                        .write_info = DescriptorInfo(ddgi_ray_buffer.handle, 0, ddgi_ray_buffer.size / FRAMES_IN_FLIGHT)
                    },
                    DescriptorBinding{
                        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                        .write_info =
                            DescriptorInfo(ddgi_probe_buffer.handle, 0, ddgi_probe_buffer.size / FRAMES_IN_FLIGHT)
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
            .reads_buffer_dynamic(
                ddgi_ray_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                ddgi_ray_buffer.size / FRAMES_IN_FLIGHT
            )
            .writes_buffer_dynamic(
                ddgi_probe_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                ddgi_probe_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                lighting_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                lighting_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                TracyVkZone(tracy_vk_context, command_buffer, "DDGI Pass");
                ZoneScopedN("DDGI Pass");

                const Pipeline& pipeline = ddgi_probe_classify_pipeline;

                std::array<uint32_t, 3> offsets = {
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_RAY_BUFFER)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_PROBE_BUFFER)],
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
                                linear_sampler, blue_noise_texure.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
                            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                            .write_info =
                                DescriptorInfo(ddgi_probe_buffer.handle, 0, ddgi_probe_buffer.size / FRAMES_IN_FLIGHT)
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
            .reads_buffer_dynamic(
                ddgi_probe_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                ddgi_probe_buffer.size / FRAMES_IN_FLIGHT
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
            .samples_image(gbuffer_albedo, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(gbuffer_normals, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(ddgi_irradiance, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(ddgi_depth_atlas, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(rt_reflection_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                TracyVkZone(tracy_vk_context, command_buffer, "RT Reflection");
                ZoneScopedN("RT Shadows");

                const Pipeline& pipeline = rt_reflection_pipeline;

                vkCmdBindPipeline(command_buffer, pipeline.bind_point, pipeline.pipeline_handle);

                std::array<VkDescriptorSet, 5> sets = {
                    rt_reflection_pass_descriptor_sets[0],
                    rt_reflection_pass_descriptor_sets[1],
                    rt_reflection_pass_descriptor_sets[2],
                    rt_reflection_pass_descriptor_sets[3],
                    global_texture_descriptor_set,
                };

                std::array<uint32_t, 7> offsets = {
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_PROBE_BUFFER)],
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
            .samples_image(gbuffer_normals, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(rt_reflection_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                TracyVkZone(tracy_vk_context, command_buffer, "RT Reflection");
                ZoneScopedN("RT Shadows");

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

                vkCmdDispatch(
                    command_buffer, (rt_reflection_buffer.width + 7) / 8, (rt_reflection_buffer.height + 7) / 8, 1
                );

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
                                TracyVkZone(tracy_vk_context, command_buffer, "RT Shadows");
                                ZoneScopedN("RT Shadows");

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
                TracyVkZone(tracy_vk_context, command_buffer, "RT Shadow Fill");
                ZoneScopedN("RT Shadow Fill");

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
                TracyVkZone(tracy_vk_context, command_buffer, "RT Shadow Blur");
                ZoneScopedN("RT Shadow Blur");

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
            .reads_buffer_dynamic(
                ddgi_probe_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                ddgi_probe_buffer.size / FRAMES_IN_FLIGHT
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
                TracyVkZone(tracy_vk_context, command_buffer, "Light Pass");
                ZoneScopedN("Light Pass");

                std::array<uint32_t, 3> offsets = {
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)],
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_PROBE_BUFFER)],
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
                TracyVkZone(tracy_vk_context, command_buffer, "DDGI Pass");
                ZoneScopedN("DDGI Pass");

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
                TracyVkZone(tracy_vk_context, command_buffer, "DDGI Pass");
                ZoneScopedN("DDGI Pass");

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
            .writes_storage_image(smaa_edges, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                TracyVkZone(tracy_vk_context, command_buffer, "RT Shadow Fill");
                ZoneScopedN("RT Shadow Fill");

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
                TracyVkZone(tracy_vk_context, command_buffer, "RT Shadow Fill");
                ZoneScopedN("RT Shadow Fill");

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
                TracyVkZone(tracy_vk_context, command_buffer, "Bloom Downsample Pass");
                ZoneScopedN("Bloom Downsample Pass");

                VkPipelineBindPoint bind_point      = bloom_downsample_pipeline.bind_point;
                VkPipeline          pipeline        = bloom_downsample_pipeline.pipeline_handle;
                VkPipelineLayout    pipeline_layout = bloom_downsample_pipeline.pipeline_layout;
                VkShaderStageFlags  stage_flags     = bloom_downsample_pipeline.stage_flags;

                vkCmdBindPipeline(command_buffer, bind_point, pipeline);
                for (int i = 0; i < bloom_levels; ++i) {
                    uint32_t mip_width  = glm::max(1u, bloom_buffer.width >> i);
                    uint32_t mip_height = glm::max(1u, bloom_buffer.height >> i);

                    BloomPushConstants constants = {
                        .texel_size = {1.0 / mip_width, 1.0 / mip_height},
                        .first_pass = static_cast<uint32_t>(i == 0 ? 1 : 0),
                        .padding    = 0.0f,
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
                TracyVkZone(tracy_vk_context, command_buffer, "Bloom Upsample Pass");
                ZoneScopedN("Bloom Upsample Pass");

                VkPipelineBindPoint bind_point      = bloom_upsample_pipeline.bind_point;
                VkPipeline          pipeline        = bloom_upsample_pipeline.pipeline_handle;
                VkPipelineLayout    pipeline_layout = bloom_upsample_pipeline.pipeline_layout;
                VkShaderStageFlags  stage_flags     = bloom_upsample_pipeline.stage_flags;

                vkCmdBindPipeline(command_buffer, bind_point, pipeline);
                for (int i = bloom_levels - 1; i > 0; --i) {
                    uint32_t mip_width  = glm::max(1u, bloom_buffer.width >> (i - 1));
                    uint32_t mip_height = glm::max(1u, bloom_buffer.height >> (i - 1));

                    BloomPushConstants constants = {
                        .texel_size = {1.0 / mip_width, 1.0 / mip_height},
                        .first_pass = i == 0,
                        .padding    = 0.0f,
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
                TracyVkZone(tracy_vk_context, command_buffer, "Composite Pass");
                ZoneScopedN("Composite Pass");

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
            .reads_buffer_dynamic(
                ddgi_probe_buffer,
                VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                ddgi_probe_buffer.size / FRAMES_IN_FLIGHT
            )
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                TracyVkZone(tracy_vk_context, command_buffer, "Debug Pass");
                ZoneScopedN("Debug Pass");

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
                std::array<uint32_t, 3> offsets = {
                    instance_offset,
                    dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_PROBE_BUFFER)],
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
                TracyVkZone(tracy_vk_context, command_buffer, "RT Shadow Fill");
                ZoneScopedN("RT Shadow Fill");

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
    framegraph.build();

    int pick_frame = UINT32_MAX;

    while (running) {
        FrameMark;

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
                pressed_keys[window_event.key.scancode] = true;

                if (pressed_keys[SDL_SCANCODE_LSHIFT]) {
                    if (window_event.key.scancode == SDL_SCANCODE_C) {
                        tranform_gizmo_op = ImGuizmo::OPERATION::SCALEU;
                    }

                    if (window_event.key.scancode == SDL_SCANCODE_R) {
                        tranform_gizmo_op = ImGuizmo::OPERATION::ROTATE;
                    }

                    if (window_event.key.scancode == SDL_SCANCODE_T) {
                        tranform_gizmo_op = ImGuizmo::OPERATION::TRANSLATE;
                    }
                }
                break;
            case SDL_EVENT_KEY_UP:
                pressed_keys[window_event.key.scancode]  = false;
                released_keys[window_event.key.scancode] = true;

                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                pressed_buttons[window_event.button.button] = true;

                if (window_event.button.button == SDL_BUTTON_RIGHT) {
                    SDL_SetWindowMouseGrab(window, true);
                    SDL_SetWindowRelativeMouseMode(window, true);

                    capturing_mouse = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                pressed_buttons[window_event.button.button]  = false;
                released_buttons[window_event.button.button] = true;

                if (window_event.button.button == SDL_BUTTON_RIGHT) {
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

                mouse_pos = {x, y};

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
        if (pressed_keys[SDL_SCANCODE_LSHIFT] | pressed_keys[SDL_SCANCODE_LCTRL]) {
            if (pressed_keys[SDL_SCANCODE_LCTRL]) {
                camera_speed = base_camera_speed / camera_speed_mod;
                current_player_speed /= player_sprint_modifier;
            }

            if (pressed_keys[SDL_SCANCODE_LSHIFT]) {
                camera_speed = base_camera_speed * camera_speed_mod;
                current_player_speed *= player_sprint_modifier;
            }
        } else {
            camera_speed = base_camera_speed;
        }

        glm::vec3 velocity = glm::vec3(0.0);
        if (pressed_keys[SDL_SCANCODE_W]) {
            velocity.x = 1;
        }

        if (pressed_keys[SDL_SCANCODE_S]) {
            velocity.x = -1;
        }

        if (pressed_keys[SDL_SCANCODE_A]) {
            velocity.y = -1;
        }

        if (pressed_keys[SDL_SCANCODE_D]) {
            velocity.y = 1;
        }

        if (pressed_keys[SDL_SCANCODE_ESCAPE]) {
            running = false;
        }

        if (pressed_keys[SDL_SCANCODE_G] && released_keys[SDL_SCANCODE_G]) {
            player_physics = !player_physics;

            if (player_physics) {
                player_character->SetPosition(JPH::RVec3(camera.position.x, camera.position.y, camera.position.z));
            }
        }

        if (pressed_keys[SDL_SCANCODE_P] && released_keys[SDL_SCANCODE_P]) {
            visualize_probes = !visualize_probes;
        }

        while (physics_time_accumulator >= physics_delta_time) {
            for (auto& body : dynamic_bodies) {
                JPH::Vec3 p;
                JPH::Quat r;
                physics_body_interface.GetPositionAndRotation(body.body_id, p, r);

                body.last_position = p;
                body.last_rotation = r;
            }

            physics_system.Update(physics_delta_time, 1, &physics_temp_allocator, &physics_job_system);
            physics_time_accumulator -= physics_delta_time;
        }

        float physics_alpha = physics_time_accumulator / physics_delta_time;
        for (const auto& body : dynamic_bodies) {
            if (body.drawcall_id == -1) {
                continue;
            }

            JPH::Vec3 p;
            JPH::Quat r;
            physics_body_interface.GetPositionAndRotation(body.body_id, p, r);

            glm::vec3 new_pos = glm::vec3(p.GetX(), p.GetY(), p.GetZ());
            glm::quat new_rot = glm::quat(r.GetX(), r.GetY(), r.GetZ(), r.GetW());

            glm::vec3 old_pos =
                glm::vec3(body.last_position.GetX(), body.last_position.GetY(), body.last_position.GetZ());
            glm::quat old_rot = glm::quat(
                body.last_rotation.GetX(),
                body.last_rotation.GetY(),
                body.last_rotation.GetZ(),
                body.last_rotation.GetW()
            );

            MeshInstance& instance = mesh_instances[body.drawcall_id];
            Mesh          mesh     = meshes[instance.mesh_id];

            glm::vec3 center = (mesh.bounds_max + mesh.bounds_min) * 0.5f;
            glm::vec3 offset = new_rot * (center * instance.scale);

            instance.position = glm::mix(old_pos, new_pos, physics_alpha) - offset;
            instance.rotation = glm::slerp(old_rot, new_rot, physics_alpha);
        }

        if (!player_physics) {
            move_camera(camera, velocity, camera_speed * delta_time);
        } else {
            glm::vec3 oriented_velocity =
                camera.orientation * glm::vec3(velocity.y, 0, -velocity.x) * current_player_speed;
            JPH::Vec3 desired_velocity(oriented_velocity.x, 0, oriented_velocity.z);

            static bool                         jumped       = false;
            JPH::CharacterVirtual::EGroundState ground_state = player_character->GetGroundState();

            if (pressed_keys[SDL_SCANCODE_SPACE] && ground_state == JPH::CharacterVirtual::EGroundState::OnGround) {
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

            JPH::RVec3 char_pos = player_character->GetPosition();
            camera.position =
                glm::vec3(char_pos.GetX(), char_pos.GetY(), char_pos.GetZ()) + glm::vec3(0, player_height * 0.4f, 0);
        }

        if (frame_count == pick_frame) {
            void* ptr;
            VK_CHECK(vmaMapMemory(vma_allocator, pick_buffer.allocation, &ptr));
            uint32_t mesh_id = *reinterpret_cast<uint32_t*>(ptr);
            if (mesh_id == UINT32_MAX || mesh_id >= mesh_instances.size()) {
                grabbed_mesh = -1;
            } else {
                grabbed_mesh = mesh_id;
            }

            vmaUnmapMemory(vma_allocator, pick_buffer.allocation);
            pick_frame = UINT32_MAX;
        }

        update_camera(camera);

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

        bool open = true;
        ImGui::Begin("Scene", &open, ImGuiWindowFlags_NoTitleBar);

        ImGui::Checkbox("Enable Transform Snap", &enable_transform_snap);
        if (ImGui::InputFloat("Transform Snap", &transform_snap.x, 1.0f)) {
            transform_snap = glm::vec3(transform_snap.x);
        }

        if (ImGui::TreeNode("Mesh Material")) {
            if (grabbed_mesh != -1) {
                auto& material = materials[mesh_instances[grabbed_mesh].material_id];

                std::vector<uint32_t*> material_indices = {
                    &material.albedo_index, &material.normals_index, &material.material_index, &material.emissive_index
                };

                for (auto id : material_indices) {
                    ImGui::Image(imgui_material_image_handles[*id], ImVec2(50, 50));
                    if (ImGui::BeginDragDropTarget()) {
                        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("texture_id");
                        if (payload) {
                            *id = *(uint32_t*)payload->Data;
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::SameLine();
                }

                ImGui::NewLine();

                ImGui::SliderFloat("Roughness factor", &material.roughness_factor, 0.0, 1.0f);
                ImGui::SliderFloat("Metallic factor", &material.metallic_factor, 0.0, 1.0f);
                ImGui::SliderFloat("Normal scale", &material.normal_scale, 0.0, 1.0f);

                ImGui::ColorEdit4("Albedo factor", &material.albedo_factor.x);
                ImGui::ColorEdit3("Emissive factor", &material.emissive_factor.x);
            }

            ImGui::TreePop();
        }
        ImGui::End();

        ImGui::Begin("Assets", &open, ImGuiWindowFlags_NoTitleBar);
        if (ImGui::TreeNode("Textures")) {
            int images_per_row = 6;
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

        ImGui::Begin("Debug", &open, ImGuiWindowFlags_NoTitleBar);

        ImGui::SeparatorText("Info");
        ImGui::Text("Rendering path: %s", use_meshlets ? "Meshlets" : "Indirect");
        ImGui::Text("Objects: %lu", mesh_instances.size());
        ImGui::Text("FPS: %u", fps);
        ImGui::Text("Camera Position: %s", glm::to_string(camera.position).c_str());
        ImGui::Text("Camera Orientation: %s", glm::to_string(camera.orientation).c_str());
        ImGui::Text("Triangles Rendered: %.3fM", (double(pipeline_stats[0]) / 1'000'000.0));
        ImGui::Text("Fragment shader invocations: %.3fM", (double(pipeline_stats[1]) / 1'000'000.0));
        ImGui::NewLine();

        if (ImGui::Checkbox("Player Physics", &player_physics)) {
            if (player_physics) {
                player_character->SetPosition(JPH::RVec3(camera.position.x, camera.position.y, camera.position.z));
            }
        }

        if (ImGui::TreeNode("Performance")) {
            std::vector<std::pair<std::string, PassTiming>> pass_timings = {};
            for (const auto& pass : framegraph.passes) {
                pass_timings.push_back(std::make_pair(pass.name, framegraph.get_pass_timing(pass.name)));
            }

            draw_pass_profiler(pass_timings);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Config")) {
            ImGui::SeparatorText("Cull");
            if (ImGui::Checkbox("Freeze frustum", &debug_frustum)) {
                frozen_view       = camera.view_matrix;
                frozen_frustum[0] = frustum_x.x;
                frozen_frustum[1] = frustum_x.z;
                frozen_frustum[2] = frustum_y.y;
                frozen_frustum[3] = frustum_y.z;
            }
            ImGui::SameLine();
            ImGui::Checkbox("Disable culling (global)", &disable_culling);
            ImGui::Checkbox("Disable frustum cull (compute)", (bool*)&cull_push_constants.disable_frustum_cull);
            ImGui::Checkbox("Disable depth cull (compute)", (bool*)&cull_push_constants.disable_depth_cull);
            ImGui::Checkbox("Disable frustum cull (mesh)", (bool*)&gpass_push_constants.disable_frustum_cull);
            ImGui::Checkbox("Disable depth cull (mesh)", (bool*)&gpass_push_constants.disable_depth_cull);
            ImGui::Checkbox("Disable cone cull (mesh)", (bool*)&gpass_push_constants.disable_cone_cull);
            ImGui::Checkbox(
                "Disable small triangle cull (mesh)", (bool*)&gpass_push_constants.disable_small_triangle_cull
            );

            ImGui::SeparatorText("Light");
            ImGui::DragFloat3("Light direction", &lighting_data.light_direction.x, 0.01, -1.0, 1.0);
            ImGui::ColorEdit3(
                "Light color", &lighting_data.light_color.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float
            );
            ImGui::SliderFloat("Light intensity", &lighting_data.light_color.w, 0.0, 100.0);

            ImGui::ColorEdit3(
                "Sky Hemisphere Top",
                &lighting_data.sky_hemisphere_top.x,
                ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float
            );
            ImGui::ColorEdit3(
                "Sky Hemisphere Bottom",
                &lighting_data.sky_hemisphere_bottom.x,
                ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float
            );

            ImGui::SeparatorText("DDGI");
            if (ImGui::BeginCombo("DDGI Rays Per Probe", std::to_string(lighting_data.rays_per_probe).c_str())) {
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

            ImGui::DragFloat("Probe Spacing", &lighting_data.probe_spacing, 0.01, 0.1, 10.0);
            ImGui::DragFloat3("Grid Origin", &lighting_data.grid_origin.x, 0.03, -100.0, 100.0);
            ImGui::Checkbox("Visualize Probes", (bool*)&visualize_probes);
            ImGui::SameLine();
            ImGui::Checkbox("Cull Innactive Probes", (bool*)&debug_renderer_constants.cull_innactive_probes);
            ImGui::Checkbox("Multibounce Diffuse", (bool*)&lighting_data.multibounce);
            ImGui::Checkbox("Use Probe State", (bool*)&lighting_data.use_probe_state);
            ImGui::Checkbox("Use Bent Normals", (bool*)&lighting_data.use_bent_normals);
            ImGui::Checkbox("Remove Visibility Checks", (bool*)&lighting_data.remove_visibility_checks);
            ImGui::Checkbox("Compensate Specular", (bool*)&lighting_data.compensate_specular);
            ImGui::Checkbox("Disney Diffuse", (bool*)&lighting_data.disney_diffuse);

            ImGui::SeparatorText("Bloom");
            ImGui::SliderFloat("Bloom strength", &composite_push_constants.bloom_strength, 0.0, 1.0);
            ImGui::SliderInt("Bloom levels", &bloom_levels, 0, bloom_buffer.levels);

            ImGui::SeparatorText("Post process");
            ImGui::Checkbox("Auto Exposure", (bool*)&composite_push_constants.enable_auto_exposure);
            ImGui::SliderFloat("Min Log Luminance", &min_log_lum, -16.0f, 0.0f, "%.1f");
            ImGui::SliderFloat("Max Log Luminance", &max_log_lum, 0.0f, 8.0f, "%.1f");
            ImGui::SliderFloat("Key Value", &composite_push_constants.key_value, 0.05f, 2.0f, "%.2f");
            ImGui::SliderFloat("Min Exposure", &composite_push_constants.min_exposure, 0.05f, 20.0f, "%.2f");
            ImGui::SliderFloat("Max Exposure", &composite_push_constants.max_exposure, 0.05f, 50.0f, "%.2f");
            ImGui::SliderFloat("Adaptation Speed", &adaption_speed, 0.1f, 5.0f, "%.2f");
            ImGui::Separator();
            ImGui::Checkbox("Use GT5 tonemapping", (bool*)&composite_push_constants.tonemapping_type);
            ImGui::Checkbox("Apply SMAA", (bool*)&use_smaa);
            ImGui::TreePop();
        }
        ImGui::End();

        if (grabbed_mesh != -1) {
            auto& inst = mesh_instances[grabbed_mesh];
            auto  mesh = meshes[inst.mesh_id];

            glm::mat4 transform = glm::translate(glm::mat4(1.0f), inst.position);
            transform           = transform * glm::mat4_cast(inst.rotation);
            transform           = glm::scale(transform, glm::vec3(inst.scale));

            auto      angle      = glm::normalize(glm::eulerAngles(camera.orientation));
            glm::mat4 view       = glm::mat4_cast(camera.orientation);
            glm::mat4 projection = glm::perspective(
                glm::radians(camera.fov), camera.viewport_width / camera.viewport_height, 0.01f, 1000.0f
            );

            view = camera.view_matrix;

            view = glm::scale(glm::identity<glm::mat4>(), glm::vec3(1, 1, -1)) * view;

            glm::mat4 delta_mat;

            auto& io = ImGui::GetIO();
            ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
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
                    inst.position += position;
                }

                if (tranform_gizmo_op == ImGuizmo::OPERATION::ROTATE) {
                    inst.rotation = glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(rotation.x), glm::vec3(1, 0, 0)) *
                                    inst.rotation;
                    inst.rotation = glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(rotation.y), glm::vec3(0, 1, 0)) *
                                    inst.rotation;
                    inst.rotation = glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(rotation.z), glm::vec3(0, 0, 1)) *
                                    inst.rotation;
                }

                if (tranform_gizmo_op == ImGuizmo::OPERATION::SCALEU) {
                    inst.scale *= scale.x;
                }
            }
        }

        ImGui::Render();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();

        VK_CHECK(vkWaitForFences(device, 1, &frame_fences[frame_index], VK_TRUE, UINT64_MAX));
        uint32_t image_index = 0;
        vkAcquireNextImageKHR(
            device, swapchain.handle, UINT64_MAX, image_available_semaphores[frame_index], VK_NULL_HANDLE, &image_index
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
                            debug_renderer, probe_pos, lighting_data.probe_spacing / 20.0f, {1, 1, 1, 1}
                        );
                    }
                }
            }
        }

        if (pressed_keys[SDL_SCANCODE_C] && !capturing_mouse) {
            if (pressed_buttons[SDL_BUTTON_LEFT] && released_buttons[SDL_BUTTON_LEFT]) {
                if (grabbed_mesh != -1) {
                    int w;
                    int h;
                    SDL_GetWindowSizeInPixels(window, &w, &h);

                    glm::vec4 mouse_near = {(mouse_pos / glm::vec2(w, h)) * 2.0f - 1.0f, 1, 1.0};
                    mouse_near.y *= -1;
                    mouse_near = glm::inverse(camera.view_matrix) * glm::inverse(camera.projection_matrix) * mouse_near;
                    glm::vec3 near = glm::vec3(mouse_near) / mouse_near.w;

                    glm::vec4 mouse_far = {(mouse_pos / glm::vec2(w, h)) * 2.0f - 1.0f, 0.01, 1.0};
                    mouse_far.y *= -1;
                    mouse_far = glm::inverse(camera.view_matrix) * glm::inverse(camera.projection_matrix) * mouse_far;
                    glm::vec3 far = glm::vec3(mouse_far) / mouse_far.w;

                    glm::vec3 origin  = camera.position;
                    glm::vec3 ray_dir = glm::normalize(far - near);

                    MeshInstance new_instance = mesh_instances[grabbed_mesh];
                    Mesh         mesh         = meshes[new_instance.mesh_id];

                    glm::vec3 half_extent = (mesh.bounds_max - mesh.bounds_min) * 0.5f;
                    half_extent *= new_instance.scale;

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
                    body_settings.mMassPropertiesOverride.mMass = 0.2f;

                    JPH::BodyID body_id =
                        physics_body_interface.CreateAndAddBody(body_settings, JPH::EActivation::Activate);
                    physics_body_interface.SetLinearVelocity(body_id, JPH::Vec3(ray_dir.x, ray_dir.y, ray_dir.z) * 10);
                    physics_body_interface.SetFriction(body_id, 1.0f);

                    mesh_instances.push_back(new_instance);
                    dynamic_bodies.push_back(PhysicsObject{body_id, static_cast<int>(mesh_instances.size() - 1)});
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
            memcpy(reinterpret_cast<char*>(ptr) + frame_offset, meshes.data(), sizeof(Mesh) * meshes.size());
            vmaUnmapMemory(vma_allocator, mesh_buffer.allocation);
            VK_CHECK(vmaFlushAllocation(vma_allocator, mesh_buffer.allocation, frame_offset, mesh_buffer.size));
        }

        {
            void*  ptr          = nullptr;
            size_t frame_offset = (material_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
            VK_CHECK(vmaMapMemory(vma_allocator, material_buffer.allocation, &ptr));
            memcpy(reinterpret_cast<char*>(ptr) + frame_offset, materials.data(), sizeof(Material) * materials.size());
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

        // NOTE: Not buffering rays and probe data makes the relocation more stable, needs more investigation
        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::SCENE_UBO)] =
            (scene_ubo_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::INDIRECT_COMMAND_BUFFER)] =
            (indirect_command_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_RAY_BUFFER)] = 0;
        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::LIGHTING_UBO)] =
            (lighting_ubo_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MESH_BUFFER)] =
            (mesh_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::MATERIAL_BUFFER)] =
            (material_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DRAWCALL_BUFFER)] =
            (drawcall_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        dynamic_offsets[static_cast<uint32_t>(DynamicOffset::DDGI_PROBE_BUFFER)] = 0;

        framegraph.execute(command_buffer, frame_index);

        if (pressed_keys[SDL_SCANCODE_LSHIFT] && !capturing_mouse) {
            if (pressed_buttons[SDL_BUTTON_LEFT] && released_buttons[SDL_BUTTON_LEFT]) {
                int w;
                int h;
                SDL_GetWindowSizeInPixels(window, &w, &h);

                int mouse_x = glm::clamp((int)mouse_pos.x, 0, w);
                int mouse_y = glm::clamp((int)mouse_pos.y, 0, h);

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

        auto& blit_source = use_smaa ? smaa_output : composite_output;

        image_pipeline_barrier(
            blit_source,
            command_buffer,
            use_smaa ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT
        );

        image_pipeline_barrier(
            swapchain.images[image_index],
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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
            swapchain.images[image_index],
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
            swapchain.images[image_index],
            command_buffer,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
            {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            }
        );

        VkRenderingAttachmentInfo swapchain_attachment_info = {
            .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext              = nullptr,
            .imageView          = swapchain.image_views[image_index],
            .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode        = VK_RESOLVE_MODE_NONE,
            .resolveImageView   = VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp             = VK_ATTACHMENT_LOAD_OP_LOAD,
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

        image_pipeline_barrier(
            swapchain.images[image_index],
            command_buffer,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
                    .pSignalSemaphores    = &render_finished_semaphores[image_index]
        };
        VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit_info, frame_fences[frame_index]));

        VkPresentInfoKHR present_info = {
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext              = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &render_finished_semaphores[image_index],
            .swapchainCount     = 1,
            .pSwapchains        = &swapchain.handle,
            .pImageIndices      = &image_index,
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

        for (auto& draw : mesh_instances) {
            draw.last_position = draw.position;
            draw.last_scale    = draw.scale;
            draw.last_rotation = draw.rotation;
        }

        for (int i = 0; i < pressed_keys.size(); i++) {
            released_keys[i] = !pressed_keys[i];
        }

        for (int i = 0; i < pressed_buttons.size(); i++) {
            released_buttons[i] = !pressed_buttons[i];
        }
    }

    VK_CHECK(vkDeviceWaitIdle(device));

    spdlog::info("Cleaning up");

    TracyVkDestroy(tracy_vk_context);

    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplVulkan_Shutdown();

    framegraph.destroy(device);

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
    destroy_image(ao_output_denoised_pong, device, vma_allocator);
    destroy_image(ao_output_edges, device, vma_allocator);
    destroy_image(ao_prefiltered_depth, device, vma_allocator);
    destroy_image(brdf_lut, device, vma_allocator);
    destroy_image(specular_cubemap, device, vma_allocator);
    destroy_image(irradiance_cubemap, device, vma_allocator);
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
    destroy_image(blue_noise_texure, device, vma_allocator);
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

    for (auto image : loaded_images) {
        destroy_image(image, device, vma_allocator);
    }

    if (use_hardware_rt) {
        destroy_rt_scene(rt_scene, device, vma_allocator);
    }

    destroy_debug_renderer(debug_renderer, device, vma_allocator);

    vmaDestroyAllocator(vma_allocator);

    vkDestroyDescriptorSetLayout(device, global_texture_descriptor_layout, nullptr);
    vkDestroySampler(device, linear_sampler, nullptr);
    vkDestroySampler(device, linear_sampler_clamped, nullptr);
    vkDestroySampler(device, linear_sampler_anisotropy, nullptr);
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
