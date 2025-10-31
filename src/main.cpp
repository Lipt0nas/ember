#include "ember.hpp"

#include "camera.hpp"
#include "device.hpp"
#include "framegraph.hpp"
#include "pipeline.hpp"
#include "resources.hpp"
#include "rt_scene.hpp"
#include "swapchain.hpp"
#include "ui.hpp"

#include <format>

#include <glm/gtc/random.hpp>

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

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

    glm::mat4 frozen_view;
    float     frozen_frustum[4];

    uint32_t debug_frustum;
    uint32_t disable_culling;

    float P00;
    float P11;

    float near_plane;
    float far_plane;
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

struct LightpassPushConstants {
    glm::vec3 light_direction;
    float     ambient_intensity;

    glm::vec3 light_color;
    float     light_intensity;
};

struct CompositePushConstants {
    float    bloom_strength;
    uint32_t tonemapping_type;
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

struct DrawData {
    glm::vec3 center;
    float     radius;

    glm::vec3 position;
    float     scale;
    glm::quat rotation;

    // --- INDIRECT VERTEX PIPELINE ---
    uint32_t index_count;
    uint32_t first_index;
    int32_t  vertex_offset;

    // --- MESHLET PIPELINE ---
    uint32_t meshlet_offset;
    uint32_t meshlet_count;

    uint32_t albedo_index;
    uint32_t normals_index;
    uint32_t material_index;
};

struct MeshletBounds {
    glm::vec3 center;
    float     radius;

    glm::vec3 cone_axis;
    float     cone_cutoff;

    glm::vec3 cone_apex;
    float     _pad;
};

// TODO: is not nice
uint32_t missing_albedo_index   = 0;
uint32_t missing_normals_index  = 0;
uint32_t missing_material_index = 0;

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
    VmaAllocator                         allocator,
    VkCommandBuffer                      command_buffer,
    VkQueue                              queue,
    VkDevice                             device
) {
    spdlog::info("Loading scene: {}", path.string());

    tinygltf::TinyGLTF loader;
    tinygltf::Model    model;
    std::string        error;
    std::string        warning;

    bool ret = false;
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

    uint32_t                               local_cache_offset = global_texture_cache.size();
    std::unordered_map<uint32_t, uint32_t> local_texture_cache;

    materials.resize(model.materials.size());
    spdlog::info("Loading {} materials", materials.size());

    void* staging_buffer_ptr = nullptr;
    VK_CHECK(vmaMapMemory(allocator, staging_buffer.allocation, &staging_buffer_ptr));
    for (int i = 0; i < model.materials.size(); i++) {
        auto& mat = model.materials[i];

        uint32_t albedo_index   = missing_albedo_index;
        uint32_t normals_index  = missing_normals_index;
        uint32_t material_index = missing_material_index;

        auto upload_texture = [&](int texture_index, VkFormat format) {
            tinygltf::Texture& texture     = model.textures[texture_index];
            int                image_index = texture.source;

            if (image_index >= 0) {
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
            }

            return 0u;
        };

        if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
            albedo_index = upload_texture(mat.pbrMetallicRoughness.baseColorTexture.index, VK_FORMAT_R8G8B8A8_SRGB);
        } else {
            spdlog::warn("Material id {} \"{}\" does not have a base color texture", i, mat.name);
        }

        if (mat.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
            material_index =
                upload_texture(mat.pbrMetallicRoughness.metallicRoughnessTexture.index, VK_FORMAT_R8G8B8A8_UNORM);
        } else {
            spdlog::warn("Material id {} \"{}\" does not have a metallic/rougness texture", i, mat.name);
        }

        if (mat.normalTexture.index >= 0) {
            normals_index = upload_texture(mat.normalTexture.index, VK_FORMAT_R8G8B8A8_UNORM);
        } else {
            spdlog::warn("Material id {} \"{}\" does not have a normals texture", i, mat.name);
        }

        materials[i].albedo_index   = albedo_index;
        materials[i].normals_index  = normals_index;
        materials[i].material_index = material_index;
    }

    int              current_entry = 0;
    std::vector<int> mesh_primitive_offsets(model.meshes.size());
    spdlog::info("Loading {} meshes", model.meshes.size());
    for (int m = 0; m < model.meshes.size(); m++) {
        spdlog::trace("mesh {}", m);
        const tinygltf::Mesh& mesh = model.meshes[m];

        mesh_primitive_offsets[m] = current_entry;

        for (int p = 0; p < mesh.primitives.size(); p++) {
            spdlog::trace("primitive {}", p);
            std::vector<Vertex>   vertices;
            std::vector<uint32_t> indices;

            const tinygltf::Primitive& primitive = mesh.primitives[p];

            auto has_position  = primitive.attributes.count("POSITION");
            auto has_normals   = primitive.attributes.count("NORMAL");
            auto has_texcoords = primitive.attributes.count("TEXCOORD_0");

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

            glm::vec3 center = glm::vec3(0);

            for (size_t i = 0; i < vertex_count; i++) {
                glm::vec3 position = {
                    positions[i * 3 + 0],
                    positions[i * 3 + 1],
                    positions[i * 3 + 2],
                };

                center += position;

                vertices.emplace_back(
                    Vertex{
                        .position = position,
                        .normal =
                            {
                                normals[i * 3 + 0],
                                normals[i * 3 + 1],
                                normals[i * 3 + 2],
                            },
                        .uv = {
                            texcoords[i * 2 + 0],
                            texcoords[i * 2 + 1],
                        },
                    }
                );
            }

            center /= float(vertex_count);
            float radius = 0.0f;

            for (size_t i = 0; i < vertex_count; i++) {
                glm::vec3 position = {
                    positions[i * 3 + 0],
                    positions[i * 3 + 1],
                    positions[i * 3 + 2],
                };

                radius = std::max(radius, distance(center, position));
            }

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

            VkDeviceSize mesh_vertex_offset = indirect_vertex_buffer_offset;
            VkDeviceSize mesh_index_offset  = indirect_index_buffer_offset;

            spdlog::debug("Copying vertices into global buffer");
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
                &vertices[0].position.x,
                vertices.size(),
                sizeof(Vertex),
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
                    &vertices[0].position.x,
                    vertices.size(),
                    sizeof(Vertex)
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

            spdlog::info("Loaded mesh {} with vertices={}, indices={}", m, vertices.size(), indices.size());

            meshes.emplace_back(
                mesh_vertex_offset,
                mesh_index_offset,
                current_meshlet_offset,
                meshlet_count,
                vertices.size(),
                indices.size(),
                center,
                radius
            );

            current_entry++;
        }
    }

    spdlog::info("Scene count: {}", model.scenes.size());
    int scene_id = model.defaultScene >= 0 ? model.defaultScene : (model.scenes.size() >= 1 ? 0 : -1);
    if (scene_id <= -1) {
        spdlog::warn("No eligibles scenes found");
        return;
    }

    spdlog::info("Constructing scene");
    const tinygltf::Scene& scene = model.scenes[scene_id];

    for (int node_id : scene.nodes) {
        const auto& node = model.nodes[node_id];
        if (node.mesh <= -1)
            continue;

        const auto& mesh = model.meshes[node.mesh];

        glm::vec3 position = glm::vec3(0.0);
        if (node.translation.size() == 3) {
            position = {node.translation[0], node.translation[1], node.translation[2]};
        } else {
            spdlog::warn("node missing position");
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
        }
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

int main() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Starting ember");

    VK_CHECK(volkInitialize());

    spdlog::info("Initializing SDL");
    if (!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO)) {
        spdlog::error("Failed to initialize SDL");
        return 1;
    }

    auto* window = SDL_CreateWindow("Ember", 1920, 1080, SDL_WINDOW_VULKAN);
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
            .descriptorCount = 10000,
        },
        {
            .type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 20,
        },
        {
            .type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
            .descriptorCount = 20,
        },
        {
            .type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 50,
        },
        {
            .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 10,
        },
        {
            .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 10,
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
        .anisotropyEnable        = VK_TRUE,
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

    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VkSamplerReductionModeCreateInfoEXT reduction_info = {
        .sType         = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT,
        .pNext         = nullptr,
        .reductionMode = VK_SAMPLER_REDUCTION_MODE_MIN,
    };
    sampler_info.pNext = &reduction_info;

    VkSampler depth_sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &depth_sampler));

    Buffer staging_buffer = create_buffer(
        1024 * 1024 * 64,
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
        VK_FORMAT_R8G8B8A8_UNORM,
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

    Image gbuffer_material = create_image(
        VK_FORMAT_R8G8B8A8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

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

    Image composite_output = create_image(
        VK_FORMAT_R8G8B8A8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    Image fxaa_output = create_image(
        VK_FORMAT_R8G8B8A8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        false,
        vma_allocator,
        device
    );

    std::vector<std::reference_wrapper<Image>> gbuffer_images = {
        gbuffer_albedo,
        gbuffer_normals,
        gbuffer_material,
    };

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
            fxaa_output,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
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

        VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
        VK_CHECK(vkDeviceWaitIdle(device));
    }

    Buffer global_vertex_buffer = create_buffer(
        1024 * 1024 * 200,
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

    Buffer indirect_command_buffer = create_buffer(
        1024 * 1024 * 12 * FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator
    );

    Buffer draw_data_buffer = create_buffer(
        1024 * 1024 * 12 * FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    std::vector<DrawData> bindless_draw_data_cpu_buffer;

    Buffer meshlet_buffer = create_buffer(
        1024 * 1024 * 164, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_allocator
    );

    Buffer meshlet_vertex_indices_buffer = create_buffer(
        1024 * 1024 * 164, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_allocator
    );

    Buffer meshlet_primitive_indices_buffer = create_buffer(
        1024 * 1024 * 164, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_allocator
    );

    Buffer meshlet_bounds_buffer = create_buffer(
        1024 * 1024 * 164, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vma_allocator
    );

    Buffer scene_ubo_buffer = create_buffer(
        aligned_size(sizeof(SceneUBO), physical_device_properties.limits.minUniformBufferOffsetAlignment) *
            FRAMES_IN_FLIGHT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
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

    Image missing_albedo = load_image(
        "data/textures/missing_albedo.png",
        VK_FORMAT_R8G8B8A8_SRGB,
        false,
        staging_buffer,
        command_buffers[0],
        graphics_queue,
        vma_allocator,
        device
    );

    missing_albedo_index = texture_cache.size();
    texture_cache.insert({missing_albedo_index, missing_albedo});
    loaded_images.push_back(missing_albedo);

    Image missing_normals = load_image(
        "data/textures/missing_normals.png",
        VK_FORMAT_R8G8B8A8_UNORM,
        false,
        staging_buffer,
        command_buffers[0],
        graphics_queue,
        vma_allocator,
        device
    );

    missing_normals_index = texture_cache.size();
    texture_cache.insert({missing_normals_index, missing_normals});
    loaded_images.push_back(missing_normals);

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

    missing_material_index = texture_cache.size();
    texture_cache.insert({missing_material_index, missing_material});
    loaded_images.push_back(missing_material);

    std::vector<Mesh>         meshes;
    std::vector<Material>     materials;
    std::vector<MeshInstance> mesh_instances;

    load_scene(
        "data/models/sponza/sponza.glb",
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
        vma_allocator,
        command_buffers[0],
        graphics_queue,
        device
    );

    populate_materials(texture_cache, global_texture_descriptor_set, linear_sampler, device);

    RTScene rt_scene = {};

    if (use_hardware_rt) {
        spdlog::info("Setting up hardware raytracing scene");
        rt_scene = create_rt_scene(
            device,
            physical_device,
            vma_allocator,
            command_buffers[0],
            graphics_queue,
            meshes,
            mesh_instances,
            get_buffer_device_address(global_vertex_buffer, device),
            get_buffer_device_address(global_index_buffer, device)
        );
    }

    VkDescriptorPool imgui_descriptor_pool = init_imgui(
        window,
        instance,
        physical_device,
        device,
        swapchain.format,
        graphics_family_index,
        graphics_queue,
        FRAMES_IN_FLIGHT
    );

    Camera camera = {
        .near_plane      = 0.01f,
        .viewport_width  = static_cast<float>(swapchain.width),
        .viewport_height = static_cast<float>(swapchain.height),
        .fov             = 90.0f,
        .orientation     = {0.0f, 0.0f, 0.0f, 1.0f}
    };

    float base_camera_speed        = 10.0f;
    float camera_mouse_sensitivity = 0.003f;
    float camera_speed_mod         = 2.5f;
    float camera_speed             = base_camera_speed;

    bool      capturing_mouse = false;
    glm::vec2 last_mouse_pos  = {};

    bool pressed_keys[512] = {0};

    uint32_t frame_count = 0;
    uint32_t frame_index = 0;

    float delta_time      = 0.0f;
    auto  frame_timestamp = std::chrono::high_resolution_clock::now();

    float    time_passed     = 0.0f;
    uint32_t accumulated_fps = 0;
    uint32_t fps             = 0;

    glm::mat4 frozen_view;
    float     frozen_frustum[4];

    bool debug_frustum   = false;
    bool disable_culling = false;

    bool use_fxaa = true;

    bool running = true;

    int bloom_levels = (bloom_buffer.levels - 2);

    std::array<uint64_t, 2> pipeline_stats;

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
                .type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                .write_info = DescriptorInfo(draw_data_buffer.handle, 0, draw_data_buffer.size / FRAMES_IN_FLIGHT),
            },
            DescriptorBinding{
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
                .write_info =
                    DescriptorInfo(indirect_command_buffer.handle, 0, indirect_command_buffer.size / FRAMES_IN_FLIGHT)
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
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_R8G8B8A8_UNORM,
        },
        VK_FORMAT_D32_SFLOAT,
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
                                linear_sampler, gbuffer_material.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
                    }
            },
            scene_data_layout,
        },
        sizeof(LightpassPushConstants)
    );
    std::vector<VkDescriptorSet> light_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, light_pipeline);

    LightpassPushConstants light_constants = {
        .light_direction   = glm::vec3(0.2, -0.9, 0.1),
        .ambient_intensity = 0.3,
        .light_color       = glm::vec3(1.0, 1.0, 1.0),
        .light_intensity   = 10.0f
    };

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
                            )
                        },
                    }
            },
        },
        sizeof(CompositePushConstants)
    );

    std::vector<VkDescriptorSet> composite_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, composite_pipeline);

    CompositePushConstants composite_push_constants{
        .bloom_strength   = 0.04f,
        .tonemapping_type = 1,
    };

    Pipeline fxaa_pipeline = create_compute_pipeline(
        device,
        shader_from_file(device, VK_SHADER_STAGE_COMPUTE_BIT, "data/shaders/fxaa.comp.spv"),
        {
            DescriptorLayout{
                .bindings = {
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        .write_info = DescriptorInfo(fxaa_output.view, VK_IMAGE_LAYOUT_GENERAL)
                    },
                    DescriptorBinding{
                        .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .write_info = DescriptorInfo(
                            linear_sampler_clamped, composite_output.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        )
                    }
                }
            },
        }
    );

    std::vector<VkDescriptorSet> fxaa_descriptor_sets =
        allocate_descriptor_sets(device, descriptor_pool, fxaa_pipeline);

    std::vector<uint32_t> dynamic_offsets = {0, 0, 0};

    Framegraph framegraph(device, graphics_queue, command_buffers[0], FRAMES_IN_FLIGHT, supports_timestamp_queries);

    framegraph.import_image(depth_hiz, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(depth_buffer, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    framegraph.import_image(gbuffer_albedo, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    framegraph.import_image(gbuffer_normals, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    framegraph.import_image(gbuffer_material, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    framegraph.import_image(lightpass_output, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(composite_output, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(fxaa_output, VK_IMAGE_LAYOUT_GENERAL);
    framegraph.import_image(bloom_buffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    auto cull_early_pass =
        framegraph.add_pass("cull early")
            .reads_storage_image(depth_hiz, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .reads_buffer_dynamic(
                draw_data_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                draw_data_buffer.size / FRAMES_IN_FLIGHT
            )
            .reads_buffer_dynamic(
                scene_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_READ_BIT,
                scene_ubo_buffer.size / FRAMES_IN_FLIGHT
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

                vkCmdBindPipeline(command_buffer, cull_pipeline.bind_point, cull_pipeline.pipeline_handle);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    cull_pipeline.pipeline_layout,
                    0,
                    3,
                    cull_descriptor_sets.data(),
                    3,
                    dynamic_offsets.data()
                );

                cull_push_constants.draw_count = static_cast<uint32_t>(bindless_draw_data_cpu_buffer.size());
                vkCmdPushConstants(
                    command_buffer,
                    cull_pipeline.pipeline_layout,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    0,
                    sizeof(CullPassPushConstants),
                    &cull_push_constants
                );

                vkCmdDispatch(command_buffer, (bindless_draw_data_cpu_buffer.size() + 255) / 256, 1, 1);
            });

    auto gbuffer_pass =
        framegraph.add_pass("gbuffer")
            .writes_depth_attachment(depth_buffer)
            .writes_color_attachment(gbuffer_albedo)
            .writes_color_attachment(gbuffer_normals)
            .writes_color_attachment(gbuffer_material)
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
                draw_data_buffer,
                VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                draw_data_buffer.size / FRAMES_IN_FLIGHT
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
                        .loadOp             = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
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
                        .loadOp             = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue         = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}},
                    },
                    {
                        .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .pNext              = nullptr,
                        .imageView          = gbuffer_material.view,
                        .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .resolveMode        = VK_RESOLVE_MODE_NONE,
                        .resolveImageView   = VK_NULL_HANDLE,
                        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .loadOp             = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
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

                VkDescriptorSet sets[] = {
                    gpass_descriptor_sets[0],
                    gpass_descriptor_sets[1],
                    gpass_descriptor_sets[2],
                    gpass_descriptor_sets[3],
                    global_texture_descriptor_set,
                };

                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    gpass_pipeline.pipeline_layout,
                    0,
                    5,
                    sets,
                    3,
                    &dynamic_offsets[0]
                );

                if (use_meshlets) {
                    vkCmdDrawMeshTasksIndirectCountEXT(
                        command_buffer,
                        indirect_command_buffer.handle,
                        dynamic_offsets[2] + sizeof(uint32_t),
                        indirect_command_buffer.handle,
                        dynamic_offsets[2],
                        bindless_draw_data_cpu_buffer.size(),
                        sizeof(MeshIndirectDrawCommand)
                    );
                } else {
                    vkCmdBindIndexBuffer(command_buffer, global_index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);

                    vkCmdDrawIndexedIndirectCount(
                        command_buffer,
                        indirect_command_buffer.handle,
                        dynamic_offsets[2] + sizeof(uint32_t),
                        indirect_command_buffer.handle,
                        dynamic_offsets[2],
                        bindless_draw_data_cpu_buffer.size(),
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

    auto& light_pass =
        framegraph.add_pass("lighting")
            .reads_buffer_dynamic(
                scene_ubo_buffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_UNIFORM_READ_BIT,
                scene_ubo_buffer.size / FRAMES_IN_FLIGHT
            )
            .samples_image(depth_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(gbuffer_albedo, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(gbuffer_normals, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .samples_image(gbuffer_material, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(lightpass_output, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                TracyVkZone(tracy_vk_context, command_buffer, "Light Pass");
                ZoneScopedN("Light Pass");

                vkCmdBindPipeline(command_buffer, light_pipeline.bind_point, light_pipeline.pipeline_handle);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    light_pipeline.bind_point,
                    light_pipeline.pipeline_layout,
                    0,
                    light_descriptor_sets.size(),
                    light_descriptor_sets.data(),
                    1,
                    &dynamic_offsets[0]
                );

                vkCmdPushConstants(
                    command_buffer,
                    light_pipeline.pipeline_layout,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    0,
                    sizeof(LightpassPushConstants),
                    &light_constants
                );

                vkCmdDispatch(command_buffer, (swapchain.width + 7) / 8, (swapchain.height + 7) / 8, 1);
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

    auto& fxaa_pass =
        framegraph.add_pass("fxaa")
            .samples_image(composite_output, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .writes_storage_image(fxaa_output, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
            .render_func([&](VkCommandBuffer command_buffer, uint32_t frame_index) {
                TracyVkZone(tracy_vk_context, command_buffer, "FXAA Pass");
                ZoneScopedN("FXAA Pass");

                vkCmdBindPipeline(command_buffer, fxaa_pipeline.bind_point, fxaa_pipeline.pipeline_handle);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    fxaa_pipeline.bind_point,
                    fxaa_pipeline.pipeline_layout,
                    0,
                    fxaa_descriptor_sets.size(),
                    fxaa_descriptor_sets.data(),
                    0,
                    nullptr
                );

                vkCmdDispatch(command_buffer, (swapchain.width + 7) / 8, (swapchain.height + 7) / 8, 1);
            });

    framegraph.build();

    while (running) {
        FrameMark;

        auto time       = std::chrono::high_resolution_clock::now();
        auto delta_time = std::chrono::duration<float>(time - frame_timestamp).count();
        frame_timestamp = time;

        accumulated_fps++;
        time_passed += delta_time;

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
                break;
            case SDL_EVENT_KEY_UP:
                pressed_keys[window_event.key.scancode] = false;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (window_event.button.button == SDL_BUTTON_RIGHT) {
                    SDL_SetWindowMouseGrab(window, true);
                    SDL_SetWindowRelativeMouseMode(window, true);

                    capturing_mouse = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
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

        if (pressed_keys[SDL_SCANCODE_LSHIFT]) {
            camera_speed = base_camera_speed * camera_speed_mod;
        } else {
            camera_speed = base_camera_speed;
        }

        if (pressed_keys[SDL_SCANCODE_W]) {
            move_camera(camera, glm::vec2(1, 0), camera_speed * delta_time);
        }

        if (pressed_keys[SDL_SCANCODE_S]) {
            move_camera(camera, glm::vec2(-1, 0), camera_speed * delta_time);
        }

        if (pressed_keys[SDL_SCANCODE_A]) {
            move_camera(camera, glm::vec2(0, -1), camera_speed * delta_time);
        }

        if (pressed_keys[SDL_SCANCODE_D]) {
            move_camera(camera, glm::vec2(0, 1), camera_speed * delta_time);
        }

        update_camera(camera);

        auto transposed_projection = glm::transpose(camera.projection_matrix);

        glm::vec4 frustum_x = normalize_plane(transposed_projection[3] + transposed_projection[0]);
        glm::vec4 frustum_y = normalize_plane(transposed_projection[3] + transposed_projection[1]);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        bool open = true;
        ImGui::Begin("Debug", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);

        ImGui::SeparatorText("Info");
        ImGui::Text("Rendering path: %s", use_meshlets ? "Meshlets" : "Indirect");
        ImGui::Text("Draw calls: %lu", bindless_draw_data_cpu_buffer.size());
        ImGui::Text("FPS: %u", fps);
        ImGui::Text("Triangles Rendered: %.3fM", (double(pipeline_stats[0]) / 1'000'000.0));
        ImGui::Text("Fragment shader invocations: %.3fM", (double(pipeline_stats[1]) / 1'000'000.0));
        ImGui::NewLine();

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
            ImGui::SliderFloat3("Light direction", &light_constants.light_direction.x, -1.0, 1.0);
            ImGui::SliderFloat3("Light color", &light_constants.light_color.x, 0.0, 1.0);
            ImGui::SliderFloat("Light intensity", &light_constants.light_intensity, 0.0, 100.0);
            ImGui::SliderFloat("Ambient intensity", &light_constants.ambient_intensity, 0.0, 1.0);

            ImGui::SeparatorText("Bloom");
            ImGui::SliderFloat("Bloom strength", &composite_push_constants.bloom_strength, 0.0, 1.0);
            ImGui::SliderInt("Bloom levels", &bloom_levels, 0, bloom_buffer.levels);

            ImGui::SeparatorText("Post process");
            ImGui::Checkbox("Use GT5 tonemapping", (bool*)&composite_push_constants.tonemapping_type);
            ImGui::Checkbox("Apply FXAA", (bool*)&use_fxaa);
            ImGui::TreePop();
        }
        ImGui::End();

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

        void*  scene_ubo_ptr  = nullptr;
        size_t ubo_ptr_offset = (scene_ubo_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        VK_CHECK(vmaMapMemory(vma_allocator, scene_ubo_buffer.allocation, &scene_ubo_ptr));
        memcpy(reinterpret_cast<char*>(scene_ubo_ptr) + ubo_ptr_offset, &scene_ubo, sizeof(SceneUBO));
        VK_CHECK(vmaFlushAllocation(vma_allocator, scene_ubo_buffer.allocation, ubo_ptr_offset, scene_ubo_buffer.size));

        bindless_draw_data_cpu_buffer.clear();

        for (auto& instance : mesh_instances) {
            Mesh&     mesh     = meshes[instance.mesh_id];
            Material& material = materials[instance.material_id];

            uint32_t first_index   = static_cast<uint32_t>(mesh.index_buffer_offset / sizeof(uint32_t));
            int32_t  vertex_offset = static_cast<int32_t>(mesh.vertex_buffer_offset / sizeof(Vertex));

            bindless_draw_data_cpu_buffer.push_back(
                DrawData{
                    .center         = mesh.center,
                    .radius         = mesh.radius,
                    .position       = instance.position,
                    .scale          = instance.scale,
                    .rotation       = instance.rotation,
                    .index_count    = mesh.index_count,
                    .first_index    = first_index,
                    .vertex_offset  = vertex_offset,
                    .meshlet_offset = mesh.meshlet_offset,
                    .meshlet_count  = mesh.meshlet_count,
                    .albedo_index   = material.albedo_index,
                    .normals_index  = material.normals_index,
                    .material_index = material.material_index,
                }
            );
        }

        void*  bindless_uniform_ptr       = nullptr;
        size_t draw_data_ptr_frame_offset = (draw_data_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        VK_CHECK(vmaMapMemory(vma_allocator, draw_data_buffer.allocation, &bindless_uniform_ptr));
        memcpy(
            reinterpret_cast<char*>(bindless_uniform_ptr) + draw_data_ptr_frame_offset,
            bindless_draw_data_cpu_buffer.data(),
            sizeof(DrawData) * bindless_draw_data_cpu_buffer.size()
        );
        vmaUnmapMemory(vma_allocator, draw_data_buffer.allocation);
        VK_CHECK(vmaFlushAllocation(
            vma_allocator, draw_data_buffer.allocation, draw_data_ptr_frame_offset, draw_data_buffer.size
        ));

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

        dynamic_offsets[0] = (scene_ubo_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        dynamic_offsets[1] = (draw_data_buffer.size / FRAMES_IN_FLIGHT) * frame_index;
        dynamic_offsets[2] = (indirect_command_buffer.size / FRAMES_IN_FLIGHT) * frame_index;

        framegraph.execute(command_buffer, frame_index);

        auto& blit_source = use_fxaa ? fxaa_output : composite_output;

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
    destroy_buffer(draw_data_buffer, device, vma_allocator);
    destroy_buffer(indirect_command_buffer, device, vma_allocator);
    destroy_buffer(meshlet_bounds_buffer, device, vma_allocator);
    destroy_buffer(meshlet_primitive_indices_buffer, device, vma_allocator);
    destroy_buffer(meshlet_vertex_indices_buffer, device, vma_allocator);
    destroy_buffer(meshlet_buffer, device, vma_allocator);
    destroy_buffer(scene_ubo_buffer, device, vma_allocator);
    destroy_image(depth_buffer, device, vma_allocator);
    destroy_image(lightpass_output, device, vma_allocator);
    destroy_image(composite_output, device, vma_allocator);
    destroy_image(gbuffer_albedo, device, vma_allocator);
    destroy_image(gbuffer_material, device, vma_allocator);
    destroy_image(gbuffer_normals, device, vma_allocator);
    destroy_image(depth_hiz, device, vma_allocator);
    destroy_image(bloom_buffer, device, vma_allocator);
    destroy_image(fxaa_output, device, vma_allocator);

    for (auto view : depth_mip_views) {
        vkDestroyImageView(device, view, nullptr);
    }

    for (auto view : bloom_mip_views) {
        vkDestroyImageView(device, view, nullptr);
    }

    destroy_pipeline(device, cull_pipeline);
    destroy_pipeline(device, gpass_pipeline);
    destroy_pipeline(device, hiz_pipeline);
    destroy_pipeline(device, light_pipeline);
    destroy_pipeline(device, bloom_downsample_pipeline);
    destroy_pipeline(device, bloom_upsample_pipeline);
    destroy_pipeline(device, composite_pipeline);
    destroy_pipeline(device, fxaa_pipeline);

    for (auto image : loaded_images) {
        destroy_image(image, device, vma_allocator);
    }

    if (use_hardware_rt) {
        destroy_rt_scene(rt_scene, device, vma_allocator);
    }

    vmaDestroyAllocator(vma_allocator);

    vkDestroyDescriptorSetLayout(device, global_texture_descriptor_layout, nullptr);
    vkDestroySampler(device, linear_sampler, nullptr);
    vkDestroySampler(device, linear_sampler_clamped, nullptr);
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
