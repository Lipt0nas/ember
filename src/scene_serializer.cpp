#include "scene_serializer.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>

namespace glm {
    template <class Archive> void serialize(Archive& archive, glm::vec3& v) {
        archive(cereal::make_nvp("x", v.x), cereal::make_nvp("y", v.y), cereal::make_nvp("z", v.z));
    }

    template <class Archive> void serialize(Archive& archive, glm::vec4& v) {
        archive(
            cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z),
            cereal::make_nvp("w", v.w)
        );
    }
} // namespace glm

template <typename Archive> void serialize(Archive& archive, GeometryDataHeader& header) {
    archive(
        header.vertex_buffer_size,
        header.index_buffer_size,
        header.meshlet_buffer_size,
        header.meshlet_vertex_indicies_buffer_size,
        header.meshlet_primitive_buffer_size,
        header.meshlet_bounds_buffer_size
    );
}

template <typename Archive> void serialize(Archive& archive, TextureDataHeader& header) {
    archive(header.texture_count);
}

template <typename Archive> void serialize(Archive& archive, TextureHeader& header) {
    archive(header.width, header.height, header.format, header.data_size, header.sampler_index);
}

template <typename Archive> void serialize(Archive& archive, SamplerInfo& info) {
    archive(
        cereal::make_nvp("mag_filter", info.mag_filter),
        cereal::make_nvp("min_filter", info.min_filter),
        cereal::make_nvp("mipmap_mode", info.mipmap_mode),
        cereal::make_nvp("address_mode_u", info.address_mode_u),
        cereal::make_nvp("address_mode_v", info.address_mode_v),
        cereal::make_nvp("address_mode_w", info.address_mode_w),
        cereal::make_nvp("anisotropy", info.anisotropy)
    );
}

template <typename Archive> void serialize(Archive& archive, Material& mat) {
    archive(
        cereal::make_nvp("albedo_index", mat.albedo_index),
        cereal::make_nvp("normals_index", mat.normals_index),
        cereal::make_nvp("material_index", mat.material_index),
        cereal::make_nvp("emissive_index", mat.emissive_index),
        cereal::make_nvp("albedo_factor", mat.albedo_factor),
        cereal::make_nvp("emissive_factor", mat.emissive_factor),
        cereal::make_nvp("roughness_factor", mat.roughness_factor),
        cereal::make_nvp("metallic_factor", mat.metallic_factor),
        cereal::make_nvp("normal_scale", mat.normal_scale)
    );
}

template <class Archive> void serialize(Archive& archive, MeshLOD& lod) {
    archive(
        cereal::make_nvp("index_offset", lod.index_offset),
        cereal::make_nvp("index_count", lod.index_count),
        cereal::make_nvp("meshlet_offset", lod.meshlet_offset),
        cereal::make_nvp("meshlet_count", lod.meshlet_count),
        cereal::make_nvp("error", lod.error)
    );
}

template <class Archive> void serialize(Archive& archive, Mesh& mesh) {
    archive(
        cereal::make_nvp("center", mesh.center),
        cereal::make_nvp("radius", mesh.radius),
        cereal::make_nvp("bounds_min", mesh.bounds_min),
        cereal::make_nvp("bounds_max", mesh.bounds_max),
        cereal::make_nvp("vertex_offset", mesh.vertex_offset),
        cereal::make_nvp("vertex_count", mesh.vertex_count),
        cereal::make_nvp("lod_count", mesh.lod_count)
    );

    std::vector<MeshLOD> active_lods(mesh.lods, mesh.lods + mesh.lod_count);
    archive(cereal::make_nvp("lods", active_lods));

    if (Archive::is_loading::value) {
        std::copy(active_lods.begin(), active_lods.end(), mesh.lods);
    }
}

void SceneSerializer::load(
    const std::filesystem::path& path,
    Scene&                       scene,
    JPH::PhysicsSystem&          physics_system,
    ScriptSystem&                script_system,
    RendererBuffers&             buffers,
    BufferOffsets&               buffer_offsets,
    VkDevice                     device,
    VkQueue                      queue,
    VmaAllocator                 allocator,
    VkCommandBuffer              command_buffer
) {
    if (!std::filesystem::is_directory(path)) {
        spdlog::error("{} is not a directory", path.string());
        return;
    }

    spdlog::info("Loading scene {}", path.string());

    VK_CHECK(vkDeviceWaitIdle(device));

    {
        spdlog::info("Loading scene data");
        std::ifstream is(path / "scene.json", std::ios::binary);
        if (!is.is_open()) {
            spdlog::error("Could not open scene file for reading");
            return;
        }
        cereal::JSONInputArchive archive(is);

        spdlog::info("Loading samplers");
        std::vector<SamplerInfo> samplers;
        archive(cereal::make_nvp("samplers", samplers));

        for (auto& info : samplers) {
            scene.samplers.push_back(create_sampler(
                static_cast<VkFilter>(info.mag_filter),
                static_cast<VkFilter>(info.min_filter),
                static_cast<VkSamplerMipmapMode>(info.mipmap_mode),
                static_cast<VkSamplerAddressMode>(info.address_mode_u),
                static_cast<VkSamplerAddressMode>(info.address_mode_v),
                static_cast<VkSamplerAddressMode>(info.address_mode_w),
                info.anisotropy,
                device
            ));
        }

        spdlog::info("Loading materials");
        archive(cereal::make_nvp("materials", scene.materials));
        scene.original_material_size = scene.materials.size();

        spdlog::info("Loading meshes");
        archive(cereal::make_nvp("meshes", scene.meshes));

        spdlog::info("Loading entities");
        entt::snapshot_loader(scene.entity_registry)
            .get<entt::entity>(archive)
            .get<components::Transform>(archive)
            .get<components::Name>(archive)
            .get<components::Mesh>(archive)
            .get<components::Parent>(archive)
            .get<components::Children>(archive)
            .get<components::Script>(archive);

        // NOTE: Physics can only be serialized during runtime only right now
        // .get<components::Physics>(archive);
    }

    {
        spdlog::info("Loading geometry data");
        std::ifstream is(path / "geometry.bin", std::ios::binary);
        if (!is.is_open()) {
            spdlog::error("Failed to open geometry file for reading");
            return;
        }
        cereal::BinaryInputArchive archive(is);

        GeometryDataHeader header;
        archive(header);

        auto chunked_read = [&](Buffer&                     dst_buffer,
                                uint64_t                    dst_buffer_size,
                                std::vector<unsigned char>* cpu_copy = nullptr) -> uint64_t {
            int chunks = static_cast<int>(
                glm::ceil(static_cast<float>(dst_buffer_size) / static_cast<float>(buffers.staging_buffer.size))
            );
            uint64_t chunk_size = buffers.staging_buffer.size;

            if (cpu_copy) {
                cpu_copy->resize(dst_buffer_size);
            }

            for (int c = 0; c < chunks; c++) {
                uint64_t dst_offset  = chunk_size * c;
                uint64_t region_size = glm::clamp(chunk_size, 0ul, dst_buffer_size - (chunk_size * c));

                void* staging_buffer_ptr = nullptr;
                VK_CHECK(vmaMapMemory(allocator, buffers.staging_buffer.allocation, &staging_buffer_ptr));
                archive.loadBinary(staging_buffer_ptr, region_size);

                if (cpu_copy) {
                    size_t element_offset = dst_offset;
                    size_t element_count  = region_size;
                    std::memcpy(cpu_copy->data() + element_offset, staging_buffer_ptr, region_size);
                }

                vmaUnmapMemory(allocator, buffers.staging_buffer.allocation);

                copy_buffer(
                    buffers.staging_buffer, dst_buffer, command_buffer, queue, device, region_size, dst_offset, 0
                );
            }

            return dst_buffer_size;
        };

        std::vector<unsigned char> vertex_cpu_data(header.vertex_buffer_size);
        std::vector<unsigned char> index_cpu_data(header.index_buffer_size);

        buffer_offsets.vertex_buffer = chunked_read(buffers.vertex_buffer, header.vertex_buffer_size, &vertex_cpu_data);
        buffer_offsets.index_buffer  = chunked_read(buffers.index_buffer, header.index_buffer_size, &index_cpu_data);
        buffer_offsets.meshlet_buffer = chunked_read(buffers.meshlet_buffer, header.meshlet_buffer_size);
        buffer_offsets.meshlet_vertex_indices =
            chunked_read(buffers.meshlet_vertex_indices, header.meshlet_vertex_indicies_buffer_size);
        buffer_offsets.meshlet_primitive_buffer =
            chunked_read(buffers.meshlet_primitive_buffer, header.meshlet_primitive_buffer_size);
        buffer_offsets.meshlet_bounds_buffer =
            chunked_read(buffers.meshlet_bounds_buffer, header.meshlet_bounds_buffer_size);

        spdlog::info("Baking static physics shapes");
        std::vector<JPH::ShapeRefC> mesh_collision_shapes;
        const uint32_t*             indices = reinterpret_cast<const uint32_t*>(index_cpu_data.data());
        for (int m = 0; m < scene.meshes.size(); m++) {
            auto& mesh = scene.meshes[m];

            std::vector<glm::vec3> unquantized_positions(mesh.vertex_count);
            for (int i = 0; i < unquantized_positions.size(); i++) {
                Vertex* v =
                    reinterpret_cast<Vertex*>(vertex_cpu_data.data() + (mesh.vertex_offset * sizeof(Vertex))) + i;
                unquantized_positions[i] = glm::vec3(
                    meshopt_dequantizeHalf(v->px), meshopt_dequantizeHalf(v->py), meshopt_dequantizeHalf(v->pz)
                );
            }

            JPH::TriangleList triangles;
            uint32_t          index_count  = mesh.lods[0].index_count;
            uint32_t          index_offset = mesh.lods[0].index_offset;

            for (size_t i = 0; i < index_count; i += 3) {
                const glm::vec3& v0 = unquantized_positions[indices[index_offset + i + 0]];
                const glm::vec3& v1 = unquantized_positions[indices[index_offset + i + 1]];
                const glm::vec3& v2 = unquantized_positions[indices[index_offset + i + 2]];

                triangles.push_back(
                    JPH::Triangle(
                        JPH::Float3(v0.x, v0.y, v0.z), JPH::Float3(v1.x, v1.y, v1.z), JPH::Float3(v2.x, v2.y, v2.z)
                    )
                );
            }

            JPH::MeshShapeSettings mesh_settings(triangles);
            JPH::ShapeRefC         collision_shape = mesh_settings.Create().Get();
            mesh_collision_shapes.push_back(collision_shape);
        }

        auto view = scene.entity_registry.view<components::Mesh, components::Transform>();
        for (auto [e, m, t] : view.each()) {
            JPH::ShapeRefC final_shape;
            if (t.scale != 1.0f) {
                JPH::ScaledShapeSettings scaled(mesh_collision_shapes[m.mesh.mesh_id], JPH::Vec3::sReplicate(t.scale));
                final_shape = scaled.Create().Get();
            } else {
                final_shape = mesh_collision_shapes[m.mesh.mesh_id];
            }

            JPH::BodyInterface& body_interface = physics_system.GetBodyInterface();

            JPH::BodyCreationSettings body_settings(
                final_shape,
                JPH::RVec3(t.position.x, t.position.y, t.position.z),
                JPH::Quat(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w),
                JPH::EMotionType::Static,
                Layers::NON_MOVING
            );

            JPH::Body* body = body_interface.CreateBody(body_settings);
            if (body) {
                body_interface.AddBody(body->GetID(), JPH::EActivation::DontActivate);

                auto& physics      = scene.add_component<components::Physics>(e);
                physics.body_id    = body->GetID();
                physics.is_static  = true;
                physics.last_scale = t.scale;
            }
        }
    }

    {
        spdlog::info("Loading Textures");
        std::ifstream is(path / "textures.bin", std::ios::binary);
        if (!is.is_open()) {
            spdlog::error("Failed to open texture file for reading");
            return;
        }
        cereal::BinaryInputArchive archive(is);

        TextureDataHeader header;
        archive(header);

        for (uint32_t i = 0; i < header.texture_count; i++) {
            TextureHeader texture_header;
            archive(texture_header);

            if (texture_header.data_size > buffers.staging_buffer.size) {
                spdlog::error(
                    "Attempted out of bounds image buffer write for image, size={}, staging size={}",
                    texture_header.data_size,
                    buffers.staging_buffer.size
                );
                exit(1);
            }

            Image image = create_image(
                static_cast<VkFormat>(texture_header.format),
                texture_header.width,
                texture_header.height,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                true,
                allocator,
                device
            );

            void* staging_buffer_ptr = nullptr;
            VK_CHECK(vmaMapMemory(allocator, buffers.staging_buffer.allocation, &staging_buffer_ptr));

            archive.loadBinary(staging_buffer_ptr, texture_header.data_size);
            copy_image(buffers.staging_buffer, image, true, command_buffer, queue, device);
            vmaUnmapMemory(allocator, buffers.staging_buffer.allocation);

            scene.images.push_back(
                ImageResource{
                    .image         = image,
                    .sampler_index = texture_header.sampler_index,
                }
            );
        }
    }
}

void SceneSerializer::save(
    const std::filesystem::path& path,
    const Scene&                 scene,
    const JPH::PhysicsSystem&    physics_system,
    const ScriptSystem&          script_system,
    const RendererBuffers&       buffers,
    const BufferOffsets&         buffer_offsets,
    VkDevice                     device,
    VkQueue                      queue,
    VmaAllocator                 allocator,
    VkCommandBuffer              command_buffer
) {
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }

    if (!std::filesystem::is_directory(path)) {
        spdlog::error("{} is not a directory", path.string());
        return;
    }

    spdlog::info("Saving scene to {}", path.string());

    VK_CHECK(vkDeviceWaitIdle(device));

    {
        spdlog::info("Saving scene data");
        std::ofstream os(path / "scene.json", std::ios::binary);
        if (!os.is_open()) {
            spdlog::error("Could not open scene file for writing");
            return;
        }
        cereal::JSONOutputArchive archive(os);

        spdlog::info("Saving samplers");
        std::vector<SamplerInfo> samplers;
        for (int i = 0; i < scene.samplers.size(); i++) {
            auto& src = scene.samplers[i];

            samplers.push_back(
                SamplerInfo{
                    .mag_filter     = src.mag_filter,
                    .min_filter     = src.min_filter,
                    .mipmap_mode    = src.mipmap_mode,
                    .address_mode_u = src.address_mode_u,
                    .address_mode_v = src.address_mode_v,
                    .address_mode_w = src.address_mode_w,
                    .anisotropy     = src.anisotropy
                }
            );
        }
        archive(cereal::make_nvp("samplers", samplers));

        spdlog::info("Saving materials");
        archive(cereal::make_nvp("materials", scene.materials));

        spdlog::info("Saving meshes");
        archive(cereal::make_nvp("meshes", scene.meshes));

        spdlog::info("Saving entities");
        entt::snapshot(scene.entity_registry)
            .get<entt::entity>(archive)
            .get<components::Transform>(archive)
            .get<components::Name>(archive)
            .get<components::Mesh>(archive)
            .get<components::Parent>(archive)
            .get<components::Children>(archive)
            .get<components::Script>(archive);

        // NOTE: Physics can only be serialized during runtime only right now
        // .get<components::Physics>(archive);
    }

    {
        spdlog::info("Saving geometry data");
        std::ofstream os(path / "geometry.bin", std::ios::binary);
        if (!os.is_open()) {
            spdlog::error("Could not open geometry file for writing");
            return;
        }
        cereal::BinaryOutputArchive archive(os);

        GeometryDataHeader header = {
            .vertex_buffer_size                  = buffer_offsets.vertex_buffer,
            .index_buffer_size                   = buffer_offsets.index_buffer,
            .meshlet_buffer_size                 = buffer_offsets.meshlet_buffer,
            .meshlet_vertex_indicies_buffer_size = buffer_offsets.meshlet_vertex_indices,
            .meshlet_primitive_buffer_size       = buffer_offsets.meshlet_primitive_buffer,
            .meshlet_bounds_buffer_size          = buffer_offsets.meshlet_bounds_buffer
        };
        archive(header);

        auto chunked_write = [&](const Buffer& src_buffer, uint64_t size) {
            int chunks =
                static_cast<int>(glm::ceil(static_cast<float>(size) / static_cast<float>(buffers.staging_buffer.size)));
            uint64_t chunk_size = buffers.staging_buffer.size;

            for (int c = 0; c < chunks; c++) {
                uint64_t src_offset  = chunk_size * c;
                uint64_t region_size = glm::clamp(chunk_size, 0ul, size - (chunk_size * c));

                copy_buffer(
                    src_buffer, buffers.staging_buffer, command_buffer, queue, device, region_size, 0, src_offset
                );

                void* staging_buffer_ptr = nullptr;
                VK_CHECK(vmaMapMemory(allocator, buffers.staging_buffer.allocation, &staging_buffer_ptr));
                archive.saveBinary(staging_buffer_ptr, region_size);
                vmaUnmapMemory(allocator, buffers.staging_buffer.allocation);
            }
        };

        chunked_write(buffers.vertex_buffer, buffer_offsets.vertex_buffer);
        chunked_write(buffers.index_buffer, buffer_offsets.index_buffer);
        chunked_write(buffers.meshlet_buffer, buffer_offsets.meshlet_buffer);
        chunked_write(buffers.meshlet_vertex_indices, buffer_offsets.meshlet_vertex_indices);
        chunked_write(buffers.meshlet_primitive_buffer, buffer_offsets.meshlet_primitive_buffer);
        chunked_write(buffers.meshlet_bounds_buffer, buffer_offsets.meshlet_bounds_buffer);
    }

    {
        spdlog::info("Saving textures");
        std::ofstream os(path / "textures.bin", std::ios::binary);
        if (!os.is_open()) {
            spdlog::error("Could not open texture file for writing");
            return;
        }
        cereal::BinaryOutputArchive archive(os);

        TextureDataHeader header = {
            .texture_count = static_cast<uint32_t>(scene.images.size()),
        };
        archive(header);

        for (uint32_t i = 0; i < scene.images.size(); i++) {
            auto& resource = scene.images[i];

            VkCommandBufferBeginInfo begin_info = {
                .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .pNext            = nullptr,
                .flags            = 0,
                .pInheritanceInfo = nullptr
            };
            VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

            image_pipeline_barrier(
                resource.image,
                command_buffer,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                0,
                VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                0
            );

            VkBufferImageCopy copy_region = {
                .bufferOffset      = 0,
                .bufferRowLength   = 0,
                .bufferImageHeight = 0,
                .imageSubresource =
                    {
                        .aspectMask     = resource.image.aspect,
                        .mipLevel       = 0,
                        .baseArrayLayer = 0,
                        .layerCount     = 1,
                    },
                .imageOffset =
                    {
                        .x = 0,
                        .y = 0,
                        .z = 0,
                    },
                .imageExtent = {
                    .width  = resource.image.width,
                    .height = resource.image.height,
                    .depth  = 1,
                },
            };

            vkCmdCopyImageToBuffer(
                command_buffer,
                resource.image.handle,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                buffers.staging_buffer.handle,
                1,
                &copy_region
            );

            image_pipeline_barrier(
                resource.image,
                command_buffer,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
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
            VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
            VK_CHECK(vkDeviceWaitIdle(device));

            TextureHeader texture_header = {
                .width         = resource.image.width,
                .height        = resource.image.height,
                .format        = resource.image.format,
                .data_size     = resource.image.width * resource.image.height * 4,
                .sampler_index = resource.sampler_index,
            };
            archive(texture_header);

            void* staging_buffer_ptr = nullptr;
            VK_CHECK(vmaMapMemory(allocator, buffers.staging_buffer.allocation, &staging_buffer_ptr));
            archive.saveBinary(staging_buffer_ptr, texture_header.data_size);
            vmaUnmapMemory(allocator, buffers.staging_buffer.allocation);
        }
    }
}
