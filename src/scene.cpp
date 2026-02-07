#include "scene.hpp"

#include "world.hpp"

#include <tracy/Tracy.hpp>

#include <ktx.h>
#include <map>
#include <thread>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

template <> void Scene::remove_component_internal<components::Physics>(Entity node) {
    auto p = get_component<components::Physics>(node);

    if (!p->body_id.IsInvalid()) {
        world->physics.system.GetBodyInterface().RemoveBody(p->body_id);
    }

    entity_registry.remove<components::Physics>(node);
}

void Scene::initialize(class World* world) {
    this->world = world;
}

void Scene::load_scene(
    const std::filesystem::path& path,
    RendererBuffers&             buffers,
    BufferOffsets&               buffer_offsets,
    bool                         build_lods,
    bool                         fast_build,
    bool                         compress_textures,
    std::vector<unsigned char>&  compressed_texture_data,
    VkDevice                     device,
    VkQueue                      queue,
    VmaAllocator                 allocator,
    VkCommandBuffer              command_buffer
) {
    ZoneScopedN("Load Scene");
    spdlog::info("Loading scene: {}", path.string());

    tinygltf::TinyGLTF loader;
    tinygltf::Model    model;
    std::string        error;
    std::string        warning;

    {
        ZoneScopedN("Read Scene File");
        bool ret = false;
        if (path.extension() == ".gltf") {
            ret = loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
        } else if (path.extension() == ".glb") {
            ret = loader.LoadBinaryFromFile(&model, &error, &warning, path.string());
        }
        if (!warning.empty()) {
            spdlog::warn("Warning loading scene {}: {}", path.string(), warning);
        }

        if (!error.empty()) {
            spdlog::error("Error loading scene {}: {}", path.string(), error);
        }

        if (!ret) {
            spdlog::error("Failed loading scene {}", path.string());
        }
    }

    std::map<uint32_t, int> local_texture_cache;
    std::map<uint32_t, int> local_sampler_cache;

    materials.resize(model.materials.size());
    original_material_size = materials.size();

    spdlog::info("Loading {} samplers", model.samplers.size());
    samplers.push_back(create_sampler(
        VK_FILTER_LINEAR,
        VK_FILTER_LINEAR,
        VK_SAMPLER_MIPMAP_MODE_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        16.0f,
        device
    ));
    for (int i = 0; i < model.samplers.size(); i++) {
        auto& sampler = model.samplers[i];

        int mag_filter = sampler.magFilter == -1 ? TINYGLTF_TEXTURE_FILTER_LINEAR : sampler.magFilter;
        int min_filter = sampler.minFilter == -1 ? TINYGLTF_TEXTURE_FILTER_LINEAR : sampler.minFilter;

        int wrap_s = sampler.wrapS;
        int wrap_t = sampler.wrapS;

        auto to_vk_filter = [&](int filter, VkSamplerMipmapMode& mipmap_mode) {
            switch (filter) {
            case TINYGLTF_TEXTURE_FILTER_NEAREST:
                return VK_FILTER_NEAREST;
            case TINYGLTF_TEXTURE_FILTER_LINEAR:
                return VK_FILTER_LINEAR;
            case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
                mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                return VK_FILTER_NEAREST;
            case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
                mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                return VK_FILTER_LINEAR;
            case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
                mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                return VK_FILTER_NEAREST;
            case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
                mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
                return VK_FILTER_LINEAR;
            }

            return VK_FILTER_LINEAR;
        };

        auto to_vk_wrap = [&](int wrap) {
            switch (wrap) {
            case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
                return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
                return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case TINYGLTF_TEXTURE_WRAP_REPEAT:
                return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            }

            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        };

        VkSamplerMipmapMode mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        auto vk_mag = to_vk_filter(mag_filter, mipmap_mode);
        auto vk_min = to_vk_filter(min_filter, mipmap_mode);

        auto vk_wrap_s = to_vk_wrap(wrap_s);
        auto vk_wrap_t = to_vk_wrap(wrap_t);

        local_sampler_cache.insert({i, samplers.size()});
        samplers.push_back(create_sampler(
            vk_mag, vk_min, mipmap_mode, vk_wrap_s, vk_wrap_t, VK_SAMPLER_ADDRESS_MODE_REPEAT, 16.0f, device
        ));
    }

    spdlog::info("Loading {} materials", materials.size());
    void* staging_buffer_ptr = nullptr;
    VK_CHECK(vmaMapMemory(allocator, buffers.staging_buffer.allocation, &staging_buffer_ptr));
    for (int i = 0; i < model.materials.size(); i++) {
        ZoneScopedN("Load Material");
        auto& mat = model.materials[i];

        auto upload_texture = [&](
                                  int texture_index, VkFormat format, VkFormat compressed_format, bool is_normal = false
                              ) {
            if (texture_index < 0) {
                return 0;
            }

            tinygltf::Texture& texture = model.textures[texture_index];

            int image_index = texture.source;
            if (image_index < 0) {
                return 0;
            }

            int sampler_index = texture.sampler;
            if (sampler_index < 0) {
                sampler_index = 0;
            } else {
                sampler_index = local_sampler_cache.at(sampler_index);
            }

            auto local_it = local_texture_cache.find(image_index);
            if (local_it == local_texture_cache.end()) {
                tinygltf::Image& img = model.images[image_index];

                Image image = create_image(
                    compress_textures ? compressed_format : format,
                    static_cast<uint32_t>(img.width),
                    static_cast<uint32_t>(img.height),
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    true,
                    allocator,
                    device
                );

                if (compress_textures) {
                    ktxTexture2*         ktx_texture;
                    ktxTextureCreateInfo ktx_info = {
                        .glInternalformat = 0,
                        .vkFormat         = format,
                        .pDfd             = nullptr,
                        .baseWidth        = static_cast<uint32_t>(img.width),
                        .baseHeight       = static_cast<uint32_t>(img.height),
                        .baseDepth        = 1,
                        .numDimensions    = 2,
                        .numLevels        = image.levels,
                        .numLayers        = 1,
                        .numFaces         = 1,
                        .isArray          = KTX_FALSE,
                        .generateMipmaps  = KTX_FALSE,
                    };
                    KTX_error_code result =
                        ktxTexture2_Create(&ktx_info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &ktx_texture);
                    if (result != KTX_SUCCESS) {
                        spdlog::error("Failed to create KTX texture: {}", (int)result);
                        return 0;
                    }

                    result = ktxTexture_SetImageFromMemory(
                        ktxTexture(ktx_texture), 0, 0, 0, &img.image.at(0), img.image.size()
                    );
                    if (result != KTX_SUCCESS) {
                        spdlog::error("Failed to set image data: {}", (int)result);
                        ktxTexture_Destroy(ktxTexture(ktx_texture));
                        return 0;
                    }

                    int mip_width  = img.width;
                    int mip_height = img.height;
                    int channels   = img.component;

                    std::vector<uint8_t> prev_mip(img.image.begin(), img.image.end());

                    for (uint32_t level = 1; level < image.levels; level++) {
                        int new_width  = std::max(1, mip_width / 2);
                        int new_height = std::max(1, mip_height / 2);

                        std::vector<uint8_t> mip_data(new_width * new_height * channels);

                        stbir_resize_uint8_linear(
                            prev_mip.data(),
                            mip_width,
                            mip_height,
                            0,
                            mip_data.data(),
                            new_width,
                            new_height,
                            0,
                            (stbir_pixel_layout)channels
                        );

                        result = ktxTexture_SetImageFromMemory(
                            ktxTexture(ktx_texture), level, 0, 0, mip_data.data(), mip_data.size()
                        );

                        if (result != KTX_SUCCESS) {
                            spdlog::error("Failed to set mip level {}: {}", level, (int)result);
                            ktxTexture_Destroy(ktxTexture(ktx_texture));
                            return 0;
                        }

                        prev_mip   = std::move(mip_data);
                        mip_width  = new_width;
                        mip_height = new_height;
                    }

                    ktxBasisParams basis_params   = {0};
                    basis_params.structSize       = sizeof(basis_params);
                    basis_params.compressionLevel = KTX_ETC1S_DEFAULT_COMPRESSION_LEVEL;
                    basis_params.uastc            = KTX_FALSE;
                    basis_params.threadCount      = std::max(1u, std::thread::hardware_concurrency() - 1);
                    basis_params.normalMap        = is_normal;

                    result = ktxTexture2_CompressBasisEx(ktx_texture, &basis_params);
                    if (result != KTX_SUCCESS) {
                        spdlog::error("Failed to compress texture: {}", (int)result);
                        ktxTexture_Destroy(ktxTexture(ktx_texture));
                        return 0;
                    }

                    {
                        ktx_uint8_t* ktx_data;
                        ktx_size_t   file_size;
                        ktxTexture_WriteToMemory(ktxTexture(ktx_texture), &ktx_data, &file_size);

                        size_t current_offset = compressed_texture_data.size();

                        compressed_texture_data.resize(compressed_texture_data.size() + sizeof(ktx_size_t) + file_size);
                        memcpy(compressed_texture_data.data() + current_offset, &file_size, sizeof(ktx_size_t));
                        memcpy(
                            compressed_texture_data.data() + current_offset + sizeof(ktx_size_t), ktx_data, file_size
                        );

                        free(ktx_data);
                    }

                    ktx_transcode_fmt_e transcode_fmt;
                    if (compressed_format == VK_FORMAT_BC7_SRGB_BLOCK ||
                        compressed_format == VK_FORMAT_BC7_UNORM_BLOCK) {
                        transcode_fmt = KTX_TTF_BC7_RGBA;
                    } else if (compressed_format == VK_FORMAT_BC5_UNORM_BLOCK ||
                               compressed_format == VK_FORMAT_BC5_SNORM_BLOCK) {
                        transcode_fmt = KTX_TTF_BC5_RG;
                    } else {
                        spdlog::error("Unsupported compressed format: {}", (uint32_t)compressed_format);
                        ktxTexture_Destroy(ktxTexture(ktx_texture));
                        return 0;
                    }

                    result = ktxTexture2_TranscodeBasis(ktx_texture, transcode_fmt, 0);
                    if (result != KTX_SUCCESS) {
                        spdlog::error("Failed to transcode texture: {}", (int)result);
                        ktxTexture_Destroy(ktxTexture(ktx_texture));
                        return 0;
                    }

                    uint32_t num_levels = ktx_texture->numLevels;
                    for (uint32_t level = 0; level < num_levels; level++) {
                        ktx_size_t offset;
                        ktxTexture_GetImageOffset(ktxTexture(ktx_texture), level, 0, 0, &offset);
                        ktx_size_t mip_size = ktxTexture_GetImageSize(ktxTexture(ktx_texture), level);

                        ktx_uint8_t* src_data = ktxTexture_GetData(ktxTexture(ktx_texture)) + offset;

                        if (mip_size > buffers.staging_buffer.size) {
                            spdlog::error(
                                "Attempted out of bounds image buffer write for image, size={}, staging size={}",
                                mip_size,
                                buffers.staging_buffer.size
                            );
                            exit(1);
                        }

                        memcpy(staging_buffer_ptr, src_data, mip_size);
                        copy_image_mip(buffers.staging_buffer, image, level, command_buffer, queue, device);
                    }

                    VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                    vkBeginCommandBuffer(command_buffer, &begin_info);

                    image_pipeline_barrier(
                        image,
                        command_buffer,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT
                    );

                    vkEndCommandBuffer(command_buffer);
                    VkSubmitInfo submit = {
                        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .commandBufferCount = 1,
                        .pCommandBuffers    = &command_buffer
                    };
                    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
                    vkDeviceWaitIdle(device);

                    ktxTexture_Destroy(ktxTexture(ktx_texture));
                } else {
                    if (img.image.size() > buffers.staging_buffer.size) {
                        spdlog::error(
                            "Attempted out of bounds image buffer write for image, size={}, staging size={}",
                            img.image.size(),
                            buffers.staging_buffer.size
                        );
                        exit(1);
                    }

                    memcpy(staging_buffer_ptr, &img.image.at(0), img.image.size());
                    copy_image(buffers.staging_buffer, image, true, command_buffer, queue, device);
                }

                local_texture_cache.insert({image_index, local_texture_cache.size() + 1});
                images.push_back(
                    ImageResource{
                        .image         = image,
                        .sampler_index = sampler_index,
                    }
                );
            }

            return local_texture_cache.at(image_index);
        };

        uint32_t albedo_index = upload_texture(
            mat.pbrMetallicRoughness.baseColorTexture.index, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_BC7_SRGB_BLOCK
        );
        uint32_t material_index = upload_texture(
            mat.pbrMetallicRoughness.metallicRoughnessTexture.index, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_BC7_UNORM_BLOCK
        );
        uint32_t normals_index =
            upload_texture(mat.normalTexture.index, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_BC5_UNORM_BLOCK, true);
        uint32_t emissive_index =
            upload_texture(mat.emissiveTexture.index, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_BC7_SRGB_BLOCK);

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

    VkDeviceSize indirect_vertex_buffer_offset = 0;
    VkDeviceSize indirect_index_buffer_offset  = 0;

    VkDeviceSize meshlet_buffer_offset                   = 0;
    VkDeviceSize meshlet_vertex_indices_offset           = 0;
    VkDeviceSize meshlet_vertex_primitive_indices_offset = 0;
    VkDeviceSize meshlet_bounds_buffer_offset            = 0;

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

            meshopt_remapVertexBuffer(
                vertices.data(), vertices.data(), vertices.size(), sizeof(Vertex), remap_table.data()
            );
            meshopt_remapIndexBuffer(indices.data(), indices.data(), indices.size(), remap_table.data());
            vertices.resize(unique_vertices);

            if (fast_build) {
                meshopt_optimizeVertexCacheFifo(indices.data(), indices.data(), indices.size(), vertices.size(), 16);
            } else {
                meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertices.size());
            }

            meshopt_optimizeVertexFetch(
                vertices.data(), indices.data(), indices.size(), vertices.data(), vertices.size(), sizeof(Vertex)
            );

            std::vector<glm::vec3> unquantized_positions(vertices.size());
            for (int i = 0; i < vertices.size(); i++) {
                Vertex& v = vertices[i];
                unquantized_positions[i] =
                    glm::vec3(meshopt_dequantizeHalf(v.px), meshopt_dequantizeHalf(v.py), meshopt_dequantizeHalf(v.pz));
            }

            std::vector<glm::vec3> unquantized_normals(vertices.size());
            for (int i = 0; i < vertices.size(); i++) {
                Vertex& v              = vertices[i];
                unquantized_normals[i] = glm::vec3(
                    (v.norm & 1023) / 511.f - 1.f,
                    ((v.norm >> 10) & 1023) / 511.f - 1.f,
                    ((v.norm >> 20) & 1023) / 511.f - 1.f
                );
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

            Mesh mesh = {
                .center        = center,
                .radius        = radius,
                .bounds_min    = glm::vec4(bounds_min, 0.0),
                .bounds_max    = glm::vec4(bounds_max, 0.0),
                .vertex_offset = static_cast<uint32_t>(mesh_vertex_offset / sizeof(Vertex)),
                .vertex_count  = static_cast<uint32_t>(vertices.size()),
                .lod_count     = 0,
            };

            spdlog::debug("Copying vertices into global buffer");
            memcpy(staging_buffer_ptr, vertices.data(), sizeof(Vertex) * vertices.size());
            copy_buffer(
                buffers.staging_buffer,
                buffers.vertex_buffer,
                command_buffer,
                queue,
                device,
                sizeof(Vertex) * vertices.size(),
                indirect_vertex_buffer_offset
            );
            indirect_vertex_buffer_offset += sizeof(Vertex) * vertices.size();

            std::vector<uint32_t>        all_indices;
            std::vector<meshopt_Meshlet> all_meshlets;
            std::vector<MeshletBounds>   all_meshlet_bounds;
            std::vector<unsigned int>    all_meshlet_vertices;
            std::vector<unsigned char>   all_meshlet_triangles;
            {
                float lod_scale =
                    build_lods ? meshopt_simplifyScale(&unquantized_positions[0].x, vertices.size(), sizeof(glm::vec3))
                               : 0.0f;

                std::vector<uint32_t> lod_indices = indices;
                float                 lod_error   = 0.0f;

                float normal_weights[3] = {1.0f, 1.0f, 1.0f};

                uint32_t index_offset        = static_cast<uint32_t>(mesh_index_offset / sizeof(uint32_t));
                uint32_t meshlet_base_offset = meshlet_buffer_offset / sizeof(meshopt_Meshlet);

                while (mesh.lod_count < sizeof(mesh.lods) / sizeof(mesh.lods[0])) {
                    MeshLOD& lod = mesh.lods[mesh.lod_count++];

                    lod.index_offset = index_offset;
                    lod.index_count  = uint32_t(lod_indices.size());

                    const size_t max_vertices  = 64;
                    const size_t max_triangles = 124;
                    const float  cone_weight   = 0.25f;

                    size_t max_meshlets = meshopt_buildMeshletsBound(lod_indices.size(), max_vertices, max_triangles);

                    std::vector<meshopt_Meshlet> meshlets(max_meshlets);
                    std::vector<unsigned int>    meshlet_vertices(max_meshlets * max_vertices);
                    std::vector<unsigned char>   meshlet_triangles(max_meshlets * max_triangles * 3);

                    size_t meshlet_count = 0;
                    if (fast_build) {
                        meshlet_count = meshopt_buildMeshletsScan(
                            meshlets.data(),
                            meshlet_vertices.data(),
                            meshlet_triangles.data(),
                            lod_indices.data(),
                            lod_indices.size(),
                            unquantized_positions.size(),
                            max_vertices,
                            max_triangles
                        );
                    } else {
                        meshlet_count = meshopt_buildMeshlets(
                            meshlets.data(),
                            meshlet_vertices.data(),
                            meshlet_triangles.data(),
                            lod_indices.data(),
                            lod_indices.size(),
                            (float*)unquantized_positions.data(),
                            unquantized_positions.size(),
                            sizeof(glm::vec3),
                            max_vertices,
                            max_triangles,
                            cone_weight
                        );
                    }

                    auto& last_meshlet = meshlets[meshlet_count - 1];
                    meshlet_vertices.resize(last_meshlet.vertex_offset + last_meshlet.vertex_count);
                    meshlet_triangles.resize(
                        last_meshlet.triangle_offset + ((last_meshlet.triangle_count * 3 + 3) & ~3)
                    );
                    meshlets.resize(meshlet_count);
                    std::vector<MeshletBounds> meshlet_bounds(meshlet_count);

                    lod.meshlet_count  = meshlets.size();
                    lod.meshlet_offset = meshlet_base_offset + all_meshlets.size();

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

                    size_t global_vertex_indices_offset =
                        (meshlet_vertex_indices_offset / sizeof(unsigned int)) + all_meshlet_vertices.size();
                    size_t global_primitive_indices_offset =
                        meshlet_vertex_primitive_indices_offset + all_meshlet_triangles.size();

                    for (size_t i = 0; i < meshlet_count; i++) {
                        meshlets[i].vertex_offset += global_vertex_indices_offset;
                        meshlets[i].triangle_offset += global_primitive_indices_offset;
                    }

                    for (size_t i = 0; i < meshlet_vertices.size(); i++) {
                        meshlet_vertices[i] += mesh_vertex_offset / sizeof(Vertex);
                    }

                    lod.error = lod_error * lod_scale;

                    all_indices.insert(all_indices.end(), lod_indices.begin(), lod_indices.end());
                    all_meshlets.insert(all_meshlets.end(), meshlets.begin(), meshlets.end());
                    all_meshlet_bounds.insert(all_meshlet_bounds.end(), meshlet_bounds.begin(), meshlet_bounds.end());
                    all_meshlet_vertices.insert(
                        all_meshlet_vertices.end(), meshlet_vertices.begin(), meshlet_vertices.end()
                    );
                    all_meshlet_triangles.insert(
                        all_meshlet_triangles.end(), meshlet_triangles.begin(), meshlet_triangles.end()
                    );

                    index_offset += lod_indices.size();

                    if (!build_lods) {
                        break;
                    }

                    if (mesh.lod_count < sizeof(mesh.lods) / sizeof(mesh.lods[0])) {
                        const float        max_error = 1e-1f;
                        const unsigned int options   = meshopt_SimplifySparse;

                        size_t next_indices_target = (size_t(double(lod_indices.size()) * 0.6) / 3) * 3;
                        float  next_error          = 0.0f;

                        size_t next_indices = meshopt_simplifyWithAttributes(
                            lod_indices.data(),
                            lod_indices.data(),
                            lod_indices.size(),
                            &unquantized_positions[0].x,
                            vertices.size(),
                            sizeof(glm::vec3),
                            &unquantized_normals[0].x,
                            sizeof(glm::vec3),
                            normal_weights,
                            3,
                            nullptr,
                            next_indices_target,
                            max_error,
                            options,
                            &next_error
                        );
                        assert(next_indices <= lod_indices.size());

                        if (next_indices == lod_indices.size() || next_indices == 0) {
                            break;
                        }

                        if (next_indices >= size_t(double(lod_indices.size()) * 0.85)) {
                            break;
                        }

                        lod_indices.resize(next_indices);
                        lod_error = glm::max(lod_error * 1.5f, next_error);

                        if (fast_build) {
                            meshopt_optimizeVertexCacheFifo(
                                lod_indices.data(), lod_indices.data(), lod_indices.size(), vertices.size(), 16
                            );
                        } else {
                            meshopt_optimizeVertexCache(
                                lod_indices.data(), lod_indices.data(), lod_indices.size(), vertices.size()
                            );
                        }
                    }
                }
            }

            spdlog::debug("Copying all meshlets into buffers");
            memcpy(staging_buffer_ptr, all_meshlets.data(), sizeof(meshopt_Meshlet) * all_meshlets.size());
            copy_buffer(
                buffers.staging_buffer,
                buffers.meshlet_buffer,
                command_buffer,
                queue,
                device,
                sizeof(meshopt_Meshlet) * all_meshlets.size(),
                meshlet_buffer_offset
            );
            meshlet_buffer_offset += sizeof(meshopt_Meshlet) * all_meshlets.size();

            spdlog::debug("Copying all meshlet bounds into buffer");
            memcpy(staging_buffer_ptr, all_meshlet_bounds.data(), sizeof(MeshletBounds) * all_meshlet_bounds.size());
            copy_buffer(
                buffers.staging_buffer,
                buffers.meshlet_bounds_buffer,
                command_buffer,
                queue,
                device,
                sizeof(MeshletBounds) * all_meshlet_bounds.size(),
                meshlet_bounds_buffer_offset
            );
            meshlet_bounds_buffer_offset += sizeof(MeshletBounds) * all_meshlet_bounds.size();

            spdlog::debug("Copying all meshlet vertex indices into buffer");
            memcpy(staging_buffer_ptr, all_meshlet_vertices.data(), sizeof(unsigned int) * all_meshlet_vertices.size());
            copy_buffer(
                buffers.staging_buffer,
                buffers.meshlet_vertex_indices,
                command_buffer,
                queue,
                device,
                sizeof(unsigned int) * all_meshlet_vertices.size(),
                meshlet_vertex_indices_offset
            );
            meshlet_vertex_indices_offset += sizeof(unsigned int) * all_meshlet_vertices.size();

            spdlog::debug("Copying all meshlet triangle indices into buffer");
            memcpy(
                staging_buffer_ptr, all_meshlet_triangles.data(), sizeof(unsigned char) * all_meshlet_triangles.size()
            );
            copy_buffer(
                buffers.staging_buffer,
                buffers.meshlet_primitive_buffer,
                command_buffer,
                queue,
                device,
                sizeof(unsigned char) * all_meshlet_triangles.size(),
                meshlet_vertex_primitive_indices_offset
            );
            meshlet_vertex_primitive_indices_offset += sizeof(unsigned char) * all_meshlet_triangles.size();

            memcpy(staging_buffer_ptr, all_indices.data(), sizeof(uint32_t) * all_indices.size());
            copy_buffer(
                buffers.staging_buffer,
                buffers.index_buffer,
                command_buffer,
                queue,
                device,
                sizeof(uint32_t) * all_indices.size(),
                indirect_index_buffer_offset
            );
            indirect_index_buffer_offset += sizeof(uint32_t) * all_indices.size();

            spdlog::debug("Loaded mesh {} with vertices={}, indices={}", m, vertices.size(), indices.size());

            meshes.push_back(mesh);

            current_entry++;
        }

        buffer_offsets.vertex_buffer            = indirect_vertex_buffer_offset;
        buffer_offsets.index_buffer             = indirect_index_buffer_offset;
        buffer_offsets.meshlet_buffer           = meshlet_buffer_offset;
        buffer_offsets.meshlet_vertex_indices   = meshlet_vertex_indices_offset;
        buffer_offsets.meshlet_primitive_buffer = meshlet_vertex_primitive_indices_offset;
        buffer_offsets.meshlet_bounds_buffer    = meshlet_bounds_buffer_offset;
    }

    vmaUnmapMemory(allocator, buffers.staging_buffer.allocation);

    auto root_node = create_node("Root Node");

    spdlog::info("Scene count: {}", model.scenes.size());
    int scene_id = model.defaultScene >= 0 ? model.defaultScene : (model.scenes.size() >= 1 ? 0 : -1);
    if (scene_id != -1) {
        spdlog::info("Constructing scene");
        const tinygltf::Scene& gltf_scene = model.scenes[scene_id];

        get_component<components::Name>(root_node)->name = gltf_scene.name;

        for (int node_id : gltf_scene.nodes) {
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

                auto entity         = create_node(node.name);
                auto transform      = get_component<components::Transform>(entity);
                transform->position = position;
                transform->scale    = scale;
                transform->rotation = rotation;

                set_node_parent(entity, root_node);

                auto& mesh_component = add_component<components::Mesh>(entity);
                mesh_component.mesh  = {
                     .mesh_id     = mesh_id,
                     .material_id = mesh.primitives[i].material,
                     .position    = position,
                     .scale       = scale,
                     .rotation    = rotation,
                };

                JPH::ShapeRefC final_shape;
                if (scale != 1.0f) {
                    JPH::ScaledShapeSettings scaled(mesh_collision_shapes[mesh_id], JPH::Vec3::sReplicate(scale));
                    final_shape = scaled.Create().Get();
                } else {
                    final_shape = mesh_collision_shapes[mesh_id];
                }

                JPH::BodyInterface& body_interface = world->physics.system.GetBodyInterface();

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

                    auto& physics      = add_component<components::Physics>(entity);
                    physics.body_id    = body->GetID();
                    physics.is_static  = true;
                    physics.last_scale = scale;
                }
            }
        }
    } else {
        spdlog::warn("Could not find a scene to load instances from");
    }
}

Entity Scene::create_node(const std::string& name) {
    Entity e = entity_registry.create();

    entity_registry.emplace<components::Name>(e, name);
    entity_registry.emplace<components::Transform>(e);

    return e;
}

void Scene::delete_node(Entity node, bool delete_children) {
    auto c = get_component<components::Children>(node);
    if (c) {
        auto children_copy = c->children;

        for (auto child : children_copy) {
            if (delete_children) {
                delete_node(child, delete_children);
            } else {
                auto p = get_component<components::Parent>(node);
                if (p) {
                    set_node_parent(child, p->parent);
                } else {
                    remove_node_parent(child);
                }
            }
        }
    }
    remove_node_parent(node);
    remove_all_components(node);

    entity_registry.destroy(node);
}

void Scene::destroy_scene(VkDevice device, VmaAllocator allocator) {
    for (auto image : images) {
        destroy_image(image.image, device, allocator);
    }

    for (auto sampler : samplers) {
        destroy_sampler(sampler, device);
    }
}

void Scene::set_node_parent(Entity child, Entity parent) {
    if (entity_registry.all_of<components::Parent>(child)) {
        auto old_parent = entity_registry.get<components::Parent>(child).parent;
        if (entity_registry.valid(old_parent) && entity_registry.all_of<components::Children>(old_parent)) {
            auto& children = entity_registry.get<components::Children>(old_parent).children;
            children.erase(std::remove(children.begin(), children.end(), child), children.end());
        }
    }

    entity_registry.emplace_or_replace<components::Parent>(child, parent);

    if (!entity_registry.all_of<components::Children>(parent)) {
        entity_registry.emplace<components::Children>(parent);
    }

    entity_registry.get<components::Children>(parent).children.push_back(child);
}

void Scene::remove_node_parent(Entity child) {
    if (!entity_registry.all_of<components::Parent>(child)) {
        return;
    }

    auto parent = entity_registry.get<components::Parent>(child).parent;
    if (entity_registry.valid(parent) && entity_registry.all_of<components::Children>(parent)) {
        auto& children = entity_registry.get<components::Children>(parent).children;
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }

    entity_registry.remove<components::Parent>(child);
}

Entity Scene::clone_node(Entity base) {
    return clone_node_internal(base, entt::null);
}

Entity Scene::clone_node_internal(Entity base, Entity cloned_parent) {
    auto src_name = get_component<components::Name>(base);

    std::string new_name = src_name->name;
    if (cloned_parent == entt::null) {
        new_name += "_clone";
    }

    Entity new_entity = create_node(new_name);

    auto src_transform                                = get_component<components::Transform>(base);
    *get_component<components::Transform>(new_entity) = *src_transform;

    auto src_physics = get_component<components::Physics>(base);
    if (src_physics && !src_physics->body_id.IsInvalid()) {
        auto& p              = add_component<components::Physics>(new_entity);
        auto& body_interface = world->physics.system.GetBodyInterface();

        JPH::EMotionType motion_type = body_interface.GetMotionType(src_physics->body_id);
        JPH::Vec3        position    = body_interface.GetPosition(src_physics->body_id);
        JPH::Quat        rotation    = body_interface.GetRotation(src_physics->body_id);

        const JPH::Shape* shape = body_interface.GetShape(src_physics->body_id);

        JPH::BodyCreationSettings settings(
            shape,
            JPH::RVec3(position),
            rotation,
            motion_type,
            motion_type == JPH::EMotionType::Static ? Layers::NON_MOVING : Layers::MOVING
        );

        settings.mFriction      = body_interface.GetFriction(src_physics->body_id);
        settings.mGravityFactor = body_interface.GetGravityFactor(src_physics->body_id);

        JPH::Body*  new_body = body_interface.CreateBody(settings);
        JPH::BodyID new_id   = new_body->GetID();

        body_interface.AddBody(
            new_id,
            motion_type == JPH::EMotionType::Static ? JPH::EActivation::Activate : JPH::EActivation::DontActivate
        );

        p.body_id    = new_id;
        p.is_static  = motion_type == JPH::EMotionType::Static;
        p.last_scale = src_transform->scale;
    }

    auto src_mesh = get_component<components::Mesh>(base);
    if (src_mesh) {
        auto& m = add_component<components::Mesh>(new_entity);
        m.mesh  = src_mesh->mesh;
    }

    auto src_children = get_component<components::Children>(base);
    if (src_children) {
        for (Entity child : src_children->children) {
            Entity new_child = clone_node_internal(child, new_entity);
            set_node_parent(new_child, new_entity);
        }
    }

    Entity parent     = cloned_parent == entt::null ? base : cloned_parent;
    auto   src_parent = get_component<components::Parent>(parent);
    if (src_parent) {
        set_node_parent(new_entity, src_parent->parent);
    }

    auto src_script = get_component<components::Script>(base);
    if (src_script) {
        auto& script   = add_component<components::Script>(new_entity);
        script.scripts = src_script->scripts;
    }

    auto src_tag = get_component<components::Tag>(base);
    if (src_tag) {
        auto& tag = add_component<components::Tag>(new_entity);
        tag.tags  = src_tag->tags;
    }

    auto src_camera = get_component<components::Camera>(base);
    if (src_camera) {
        auto& camera           = add_component<components::Camera>(new_entity);
        camera.near_plane      = src_camera->near_plane;
        camera.far_plane       = src_camera->far_plane;
        camera.fov             = src_camera->fov;
        camera.viewport_x      = src_camera->viewport_x;
        camera.viewport_y      = src_camera->viewport_y;
        camera.viewport_width  = src_camera->viewport_width;
        camera.viewport_height = src_camera->viewport_height;
        camera.ortho_size      = src_camera->ortho_size;
        camera.type            = src_camera->type;
        camera.is_active       = src_camera->is_active;
    }

    return new_entity;
}

bool Scene::node_has_tag(Entity node, const std::string tag) {
    auto t = get_component<components::Tag>(node);

    if (t) {
        for (const auto& etag : t->tags) {
            if (etag.compare(tag) == 0) {
                return true;
            }
        }
    }

    return false;
}

std::vector<Entity> Scene::find_nodes_with_tag(const std::string tag) {
    auto view = entity_registry.view<components::Tag>();

    std::vector<Entity> nodes;
    for (auto [e, t] : view.each()) {
        for (const auto& etag : t.tags) {
            if (etag.compare(tag) == 0) {
                nodes.push_back(e);
                break;
            }
        }
    }

    return nodes;
}

void Scene::remove_all_components(Entity node) {
    using AllComponents = std::tuple<
        components::Transform,
        components::Parent,
        components::Children,
        components::Name,
        components::Mesh,
        components::Physics,
        components::Script,
        components::Tag>;

    std::apply(
        [&](auto... args) {
            (remove_component<decltype(args)>(node), ...);
        },
        AllComponents{}
    );
}
