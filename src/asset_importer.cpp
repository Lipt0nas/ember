#include "asset_importer.hpp"
#include "world.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/cereal.hpp>

#include <fstream>
#include <ktx.h>

#include <stb_image.h>
#include <stb_image_resize2.h>

void AssetImporter::initialize(class World* world) {
    this->world = world;
}

AssetID AssetImporter::import_texture(
    const std::filesystem::path& path, const TextureMetadata::TextureImportOptions& import_options
) {
    spdlog::info("Importing texture: {}", path.string());

    auto source_destination = world->asset_registry.source_asset_path() / "textures" / path.filename();
    auto hash               = world->asset_registry.hash_path(source_destination);

    auto processed_destination =
        world->asset_registry.stored_asset_path() / "textures" / (std::to_string(hash) + ".tex");
    auto metadata_destination =
        world->asset_registry.metadata_path() / "textures" / (std::to_string(hash) + ".metadata");

    std::filesystem::create_directories(source_destination.parent_path());
    std::filesystem::create_directories(processed_destination.parent_path());
    std::filesystem::create_directories(metadata_destination.parent_path());

    try {
        std::filesystem::copy(path, source_destination, std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::error("Failed to copy source file: {}", e.what());
        return AssetMetadata::INVALID_METADATA;
    }

    int            width    = 0;
    int            height   = 0;
    int            channels = 4;
    unsigned char* data     = stbi_load(source_destination.string().c_str(), &width, &height, &channels, 4);

    if (data == nullptr) {
        spdlog::error("Failed to load {}", source_destination.string());
        return AssetMetadata::INVALID_METADATA;
    }

    bool result = process_texture(processed_destination, width, height, 4, data, import_options);
    stbi_image_free(data);
    if (!result) {
        spdlog::error("Failed to process texture");
        return AssetMetadata::INVALID_METADATA;
    }

    std::ofstream             metadata_file(metadata_destination);
    cereal::JSONOutputArchive archive(metadata_file);

    std::unique_ptr<TextureMetadata> metadata = std::make_unique<TextureMetadata>();
    metadata->id                              = hash;
    metadata->source_path                     = source_destination.string();
    metadata->asset_path                      = processed_destination.string();
    metadata->type                            = AssetType::TEXTURE;
    metadata->source_timestamp   = std::filesystem::last_write_time(source_destination).time_since_epoch().count();
    metadata->imported_timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    metadata->standalone         = true;
    metadata->import_options     = import_options;
    archive(cereal::make_nvp("metadata", *metadata));

    world->asset_registry.register_asset(hash, std::move(metadata));

    return hash;
}

AssetID AssetImporter::import_model(
    const std::filesystem::path& path, const ModelMetadata::ModelImportOptions& import_options
) {
    spdlog::info("Importing model: {}", path.string());

    auto source_destination = world->asset_registry.source_asset_path() / "models" / path.filename();
    auto hash               = world->asset_registry.hash_path(source_destination);

    auto processed_destination =
        world->asset_registry.stored_asset_path() / "models" / (std::to_string(hash) + ".model");
    auto metadata_destination = world->asset_registry.metadata_path() / "models" / (std::to_string(hash) + ".metadata");

    std::filesystem::create_directories(source_destination.parent_path());
    std::filesystem::create_directories(processed_destination.parent_path());
    std::filesystem::create_directories(metadata_destination.parent_path());

    try {
        std::filesystem::copy(path, source_destination, std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error& e) {
        spdlog::error("Failed to copy source file: {}", e.what());
        return AssetMetadata::INVALID_METADATA;
    }

    tinygltf::TinyGLTF loader;
    tinygltf::Model    model;
    std::string        error;
    std::string        warning;

    {
        bool ret = false;
        if (path.extension() == ".gltf") {
            ret = loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
        } else if (path.extension() == ".glb") {
            ret = loader.LoadBinaryFromFile(&model, &error, &warning, path.string());
        }
        if (!warning.empty()) {
            spdlog::warn("Warning loading model {}", warning);
        }

        if (!error.empty()) {
            spdlog::error("Error loading model {}", error);
            return AssetMetadata::INVALID_METADATA;
        }

        if (!ret) {
            spdlog::error("Failed loading model {}");
            return AssetMetadata::INVALID_METADATA;
        }
    }

    std::unique_ptr<ModelMetadata> metadata = std::make_unique<ModelMetadata>();
    metadata->id                            = hash;
    metadata->source_path                   = source_destination.string();
    metadata->asset_path                    = processed_destination.string();
    metadata->type                          = AssetType::MODEL;
    metadata->source_timestamp   = std::filesystem::last_write_time(source_destination).time_since_epoch().count();
    metadata->imported_timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    metadata->standalone         = true;
    metadata->import_options     = import_options;

    std::map<uint32_t, AssetID>            local_texture_cache;
    std::map<uint32_t, SamplerDescription> local_sampler_cache;

    std::map<uint32_t, std::map<uint32_t, AssetID>> local_mesh_map;
    std::map<uint32_t, AssetID>                     local_material_map;

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

        if (vk_mag != vk_min) {
            spdlog::warn("min/mag filter missmatch");
        }

        auto vk_wrap_s = to_vk_wrap(wrap_s);
        auto vk_wrap_t = to_vk_wrap(wrap_t);

        if (vk_wrap_s != vk_wrap_t) {
            spdlog::warn("min/mag filter missmatch");
        }

        SamplerDescription desc = {
            .filter       = vk_mag,
            .address_mode = vk_wrap_s,
            .mipmap_mode  = mipmap_mode,
            .anisotropy   = import_options.texture_import_options.sampler_description.anisotropy,
        };

        local_sampler_cache.insert({i, desc});
    }

    for (int i = 0; i < model.materials.size(); i++) {
        auto& mat = model.materials[i];

        auto write_texture = [&](const std::string&                    name,
                                 int                                   texture_index,
                                 TextureMetadata::TextureImportOptions import_options,
                                 bool                                  is_srgb   = true,
                                 bool                                  is_normal = false) {
            if (texture_index < 0) {
                return AssetMetadata::INVALID_METADATA;
            }

            tinygltf::Texture& texture = model.textures[texture_index];

            int image_index = texture.source;
            if (image_index < 0) {
                return AssetMetadata::INVALID_METADATA;
            }

            SamplerDescription sampler_description;

            int sampler_index = texture.sampler;
            if (sampler_index >= 0) {
                sampler_description = local_sampler_cache.at(sampler_index);
            }

            auto local_it = local_texture_cache.find(image_index);
            if (local_it == local_texture_cache.end()) {
                tinygltf::Image& img = model.images[image_index];

                import_options.is_srgb       = is_srgb;
                import_options.is_normal_map = is_normal;

                if (import_options.compression != TextureMetadata::TextureImportOptions::Compression::None) {
                    if (is_normal) {
                        import_options.compression = TextureMetadata::TextureImportOptions::Compression::BC5;
                    } else {
                        import_options.compression = TextureMetadata::TextureImportOptions::Compression::BC7;
                    }
                }
                import_options.sampler_description = sampler_description;

                auto virtual_source = world->asset_registry.source_asset_path() / "textures" /
                                      source_destination.stem() / (mat.name + "_" + name);
                auto hash = world->asset_registry.hash_path(virtual_source);
                spdlog::info("Texture virtual source: {}", virtual_source.string());

                auto tex_destination =
                    world->asset_registry.stored_asset_path() / "textures" / (std::to_string(hash) + ".tex");
                auto tex_metadata_destination =
                    world->asset_registry.metadata_path() / "textures" / (std::to_string(hash) + ".metadata");
                spdlog::info("Texture destination: {}", tex_destination.string());

                std::filesystem::create_directories(tex_destination.parent_path());
                std::filesystem::create_directories(tex_metadata_destination.parent_path());

                if (!process_texture(tex_destination, img.width, img.height, 4, img.image.data(), import_options)) {
                    spdlog::error("Failed to process {} texture {}", name, tex_destination.string());
                    return AssetMetadata::INVALID_METADATA;
                }

                metadata->texture_ids.push_back(hash);

                {
                    std::ofstream             metadata_file(tex_metadata_destination);
                    cereal::JSONOutputArchive archive(metadata_file);

                    std::unique_ptr<TextureMetadata> metadata = std::make_unique<TextureMetadata>();
                    metadata->id                              = hash;
                    metadata->source_path                     = virtual_source.string();
                    metadata->asset_path                      = tex_destination.string();
                    metadata->type                            = AssetType::TEXTURE;
                    metadata->source_timestamp =
                        std::filesystem::last_write_time(source_destination).time_since_epoch().count();
                    metadata->imported_timestamp = std::chrono::system_clock::now().time_since_epoch().count();
                    metadata->standalone         = false;
                    metadata->import_options     = import_options;
                    archive(cereal::make_nvp("metadata", *metadata));

                    world->asset_registry.register_asset(hash, std::move(metadata));
                }

                local_texture_cache.insert({image_index, hash});

                return hash;
            }

            return local_it->second;
        };

        auto albedo_handle = write_texture(
            "albedo", mat.pbrMetallicRoughness.baseColorTexture.index, import_options.texture_import_options, true
        );
        auto material_handle = write_texture(
            "material",
            mat.pbrMetallicRoughness.metallicRoughnessTexture.index,
            import_options.texture_import_options,
            false
        );
        auto normal_handle =
            write_texture("normal", mat.normalTexture.index, import_options.texture_import_options, false, true);
        auto emissive_handle =
            write_texture("emissive", mat.emissiveTexture.index, import_options.texture_import_options, true);

        {
            auto virtual_source =
                world->asset_registry.source_asset_path() / "materials" / source_destination.stem() / mat.name;
            auto hash = world->asset_registry.hash_path(virtual_source);
            spdlog::info("Material virtual source: {}", virtual_source.string());

            auto mat_destination =
                world->asset_registry.stored_asset_path() / "materials" / (std::to_string(hash) + ".mat");
            auto mat_metadata_destination =
                world->asset_registry.metadata_path() / "materials" / (std::to_string(hash) + ".metadata");
            spdlog::info("Material destination: {}", mat_destination.string());

            std::filesystem::create_directories(mat_destination.parent_path());
            std::filesystem::create_directories(mat_metadata_destination.parent_path());

            MaterialDescription desc = {
                .albedo        = albedo_handle,
                .normals       = normal_handle,
                .material      = material_handle,
                .emissive      = emissive_handle,
                .albedo_factor = glm::vec4(
                    mat.pbrMetallicRoughness.baseColorFactor[0],
                    mat.pbrMetallicRoughness.baseColorFactor[1],
                    mat.pbrMetallicRoughness.baseColorFactor[2],
                    mat.pbrMetallicRoughness.baseColorFactor[3]
                ),
                .emissive_factor  = glm::vec3(mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2]),
                .roughness_factor = static_cast<float>(mat.pbrMetallicRoughness.roughnessFactor),
                .metallic_factor  = static_cast<float>(mat.pbrMetallicRoughness.metallicFactor),
                .normal_scale     = static_cast<float>(mat.normalTexture.scale),
            };

            {
                std::ofstream             asset_file(mat_destination);
                cereal::JSONOutputArchive archive(asset_file);
                archive(desc);

                metadata->material_ids.push_back(hash);
            }

            {
                std::ofstream             metadata_file(mat_metadata_destination);
                cereal::JSONOutputArchive archive(metadata_file);

                std::unique_ptr<MaterialMetadata> metadata = std::make_unique<MaterialMetadata>();
                metadata->id                               = hash;
                metadata->source_path                      = virtual_source.string();
                metadata->asset_path                       = mat_destination.string();
                metadata->type                             = AssetType::MATERIAL;
                metadata->source_timestamp =
                    std::filesystem::last_write_time(source_destination).time_since_epoch().count();
                metadata->imported_timestamp = std::chrono::system_clock::now().time_since_epoch().count();
                metadata->standalone         = false;
                archive(cereal::make_nvp("metadata", *metadata));

                world->asset_registry.register_asset(hash, std::move(metadata));
            }

            local_material_map.insert({i, hash});
        }
    }

    spdlog::info("Loading {} meshes", model.meshes.size());
    for (int m = 0; m < model.meshes.size(); m++) {
        spdlog::trace("mesh {}", m);
        const tinygltf::Mesh& mesh = model.meshes[m];

        std::string base_name = mesh.name;

        std::map<uint32_t, AssetID> primitive_map;
        for (int p = 0; p < mesh.primitives.size(); p++) {
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

            MeshAssetHeader::MeshDescription mesh = {
                .center       = center,
                .radius       = radius,
                .bounds_min   = glm::vec4(bounds_min, 0.0),
                .bounds_max   = glm::vec4(bounds_max, 0.0),
                .vertex_count = static_cast<uint32_t>(vertices.size()),
                .lod_count    = 0,
            };

            std::vector<uint32_t>        all_indices;
            std::vector<meshopt_Meshlet> all_meshlets;
            std::vector<MeshletBounds>   all_meshlet_bounds;
            std::vector<unsigned int>    all_meshlet_vertices;
            std::vector<unsigned char>   all_meshlet_triangles;

            float lod_scale =
                import_options.mesh_import_options.generate_lods
                    ? meshopt_simplifyScale(&unquantized_positions[0].x, vertices.size(), sizeof(glm::vec3))
                    : 0.0f;

            std::vector<uint32_t> lod_indices = indices;
            float                 lod_error   = 0.0f;

            float normal_weights[3] = {1.0f, 1.0f, 1.0f};

            uint32_t index_offset = 0;

            while (mesh.lod_count < sizeof(mesh.lods) / sizeof(mesh.lods[0])) {
                MeshAssetHeader::MeshDescription::LOD& lod = mesh.lods[mesh.lod_count++];

                lod.index_offset = index_offset;
                lod.index_count  = uint32_t(lod_indices.size());

                const size_t max_vertices  = 64;
                const size_t max_triangles = 124;
                const float  cone_weight   = 0.25f;

                size_t max_meshlets = meshopt_buildMeshletsBound(lod_indices.size(), max_vertices, max_triangles);

                std::vector<meshopt_Meshlet> meshlets(max_meshlets);
                std::vector<unsigned int>    meshlet_vertices(max_meshlets * max_vertices);
                std::vector<unsigned char>   meshlet_triangles(max_meshlets * max_triangles * 3);

                size_t meshlet_count = meshopt_buildMeshlets(
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

                auto& last_meshlet = meshlets[meshlet_count - 1];
                meshlet_vertices.resize(last_meshlet.vertex_offset + last_meshlet.vertex_count);
                meshlet_triangles.resize(last_meshlet.triangle_offset + ((last_meshlet.triangle_count * 3 + 3) & ~3));
                meshlets.resize(meshlet_count);
                std::vector<MeshletBounds> meshlet_bounds(meshlet_count);

                lod.meshlet_count  = meshlets.size();
                lod.meshlet_offset = all_meshlets.size();

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

                lod.error = lod_error * lod_scale;

                uint32_t current_meshlet_vertex_offset   = all_meshlet_vertices.size();
                uint32_t current_meshlet_triangle_offset = all_meshlet_triangles.size();

                for (auto& m : meshlets) {
                    m.vertex_offset += current_meshlet_vertex_offset;
                    m.triangle_offset += current_meshlet_triangle_offset;
                }

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

                if (!import_options.mesh_import_options.generate_lods) {
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

                    meshopt_optimizeVertexCache(
                        lod_indices.data(), lod_indices.data(), lod_indices.size(), vertices.size()
                    );
                }
            }

            MeshAssetHeader header = {
                .vertex_buffer_size                  = vertices.size() * sizeof(Vertex),
                .index_buffer_size                   = all_indices.size() * sizeof(uint32_t),
                .meshlet_buffer_size                 = all_meshlets.size() * sizeof(meshopt_Meshlet),
                .meshlet_vertex_indicies_buffer_size = all_meshlet_vertices.size() * sizeof(unsigned int),
                .meshlet_primitive_buffer_size       = all_meshlet_triangles.size() * sizeof(unsigned char),
                .meshlet_bounds_buffer_size          = all_meshlet_bounds.size() * sizeof(MeshletBounds),
                .mesh                                = mesh
            };

            auto virtual_source = world->asset_registry.source_asset_path() / "meshes" / source_destination.stem() /
                                  (base_name + "_" + std::to_string(p));
            auto hash = world->asset_registry.hash_path(virtual_source);
            spdlog::info("Mesh virtual source: {}", virtual_source.string());

            auto mesh_destination =
                world->asset_registry.stored_asset_path() / "meshes" / (std::to_string(hash) + ".mesh");
            auto mesh_metadata_destination =
                world->asset_registry.metadata_path() / "meshes" / (std::to_string(hash) + ".metadata");
            spdlog::info("Mesh destination: {}", mesh_destination.string());

            std::filesystem::create_directories(mesh_destination.parent_path());
            std::filesystem::create_directories(mesh_metadata_destination.parent_path());

            {
                std::ofstream               asset_file(mesh_destination, std::ios::binary);
                cereal::BinaryOutputArchive archive(asset_file);
                archive(header);
                archive.saveBinary(vertices.data(), header.vertex_buffer_size);
                archive.saveBinary(all_indices.data(), header.index_buffer_size);
                archive.saveBinary(all_meshlets.data(), header.meshlet_buffer_size);
                archive.saveBinary(all_meshlet_vertices.data(), header.meshlet_vertex_indicies_buffer_size);
                archive.saveBinary(all_meshlet_triangles.data(), header.meshlet_primitive_buffer_size);
                archive.saveBinary(all_meshlet_bounds.data(), header.meshlet_bounds_buffer_size);
                metadata->mesh_ids.push_back(hash);
            }

            {
                std::ofstream             metadata_file(mesh_metadata_destination);
                cereal::JSONOutputArchive archive(metadata_file);

                std::unique_ptr<MeshMetadata> metadata = std::make_unique<MeshMetadata>();
                metadata->id                           = hash;
                metadata->source_path                  = virtual_source.string();
                metadata->asset_path                   = mesh_destination.string();
                metadata->type                         = AssetType::MESH;
                metadata->source_timestamp =
                    std::filesystem::last_write_time(source_destination).time_since_epoch().count();
                metadata->imported_timestamp = std::chrono::system_clock::now().time_since_epoch().count();
                metadata->standalone         = false;
                metadata->import_options     = import_options.mesh_import_options;
                archive(cereal::make_nvp("metadata", *metadata));

                world->asset_registry.register_asset(hash, std::move(metadata));
            }
            primitive_map.insert({p, hash});
        }
        local_mesh_map.insert({m, primitive_map});
    }

    int scene_id = model.defaultScene >= 0 ? model.defaultScene : (model.scenes.size() >= 1 ? 0 : -1);
    if (scene_id != -1) {
        const tinygltf::Scene& gltf_scene = model.scenes[scene_id];

        ModelMetadata::NodeDescription root_node = {
            .name        = gltf_scene.name,
            .position    = {0.0f, 0.0f, 0.0f},
            .scale       = 1.0f,
            .rotation    = {0.0f, 0.0f, 0.0f, 1.0f},
            .mesh_id     = AssetMetadata::INVALID_METADATA,
            .material_id = AssetMetadata::INVALID_METADATA,
            .children    = {},
        };

        for (int node_id : gltf_scene.nodes) {
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
                scale = node.scale[0];
            }

            glm::quat rotation = glm::quat(0.0f, 0.0f, 0.0f, 1.0f);
            if (node.rotation.size() == 4) {
                rotation = glm::quat(node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]);
            }

            for (int i = 0; i < mesh.primitives.size(); i++) {
                auto primitive = mesh.primitives[i];

                auto mesh_id     = local_mesh_map[node.mesh][i];
                auto material_id = local_material_map[primitive.material];

                root_node.children.push_back(
                    ModelMetadata::NodeDescription{
                        .name        = node.name,
                        .position    = position,
                        .scale       = scale,
                        .rotation    = rotation,
                        .mesh_id     = mesh_id,
                        .material_id = material_id,
                        .children    = {},
                    }
                );
            }
        }

        metadata->scene_description.nodes.push_back(root_node);
    }

    std::ofstream             metadata_file(metadata_destination);
    cereal::JSONOutputArchive archive(metadata_file);
    archive(cereal::make_nvp("metadata", *metadata));

    world->asset_registry.register_asset(hash, std::move(metadata));

    return hash;
}

bool AssetImporter::process_texture(
    const std::filesystem::path&                 destination,
    int                                          width,
    int                                          height,
    int                                          channels,
    unsigned char*                               data,
    const TextureMetadata::TextureImportOptions& import_options
) {
    VkFormat format = import_options.is_srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    size_t   data_size  = width * height * sizeof(unsigned char) * channels;
    uint32_t mip_levels = import_options.generate_mipmaps
                              ? static_cast<uint32_t>(glm::floor(glm::log2((float)glm::max(width, height))) + 1)
                              : 1;

    if (import_options.compression != TextureMetadata::TextureImportOptions::Compression::None) {
        ktxTexture2*         ktx_texture;
        ktxTextureCreateInfo ktx_info = {
            .glInternalformat = 0,
            .vkFormat         = format,
            .pDfd             = nullptr,
            .baseWidth        = static_cast<uint32_t>(width),
            .baseHeight       = static_cast<uint32_t>(height),
            .baseDepth        = 1,
            .numDimensions    = 2,
            .numLevels        = mip_levels,
            .numLayers        = 1,
            .numFaces         = 1,
            .isArray          = KTX_FALSE,
            .generateMipmaps  = KTX_FALSE,
        };
        KTX_error_code result = ktxTexture2_Create(&ktx_info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &ktx_texture);
        if (result != KTX_SUCCESS) {
            spdlog::error("Failed to create KTX texture: {}", (int)result);
            return false;
        }

        result = ktxTexture_SetImageFromMemory(ktxTexture(ktx_texture), 0, 0, 0, data, data_size);
        if (result != KTX_SUCCESS) {
            spdlog::error("Failed to set image data: {}", (int)result);
            ktxTexture_Destroy(ktxTexture(ktx_texture));
            return false;
        }

        int mip_width  = width;
        int mip_height = height;

        if (import_options.generate_mipmaps) {
            std::vector<uint8_t> prev_mip(data, data + data_size);

            for (uint32_t level = 1; level < mip_levels; level++) {
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
                    return false;
                }

                prev_mip   = std::move(mip_data);
                mip_width  = new_width;
                mip_height = new_height;
            }
        }

        ktxBasisParams basis_params   = {0};
        basis_params.structSize       = sizeof(basis_params);
        basis_params.compressionLevel = KTX_ETC1S_DEFAULT_COMPRESSION_LEVEL;
        basis_params.uastc            = KTX_FALSE;
        basis_params.threadCount      = std::max(1u, std::thread::hardware_concurrency() - 1);
        basis_params.normalMap        = import_options.is_normal_map;

        result = ktxTexture2_CompressBasisEx(ktx_texture, &basis_params);
        if (result != KTX_SUCCESS) {
            spdlog::error("Failed to compress texture: {}", (int)result);
            ktxTexture_Destroy(ktxTexture(ktx_texture));
            return false;
        }

        {
            ktx_uint8_t* ktx_data;
            ktx_size_t   file_size;
            ktxTexture_WriteToMemory(ktxTexture(ktx_texture), &ktx_data, &file_size);

            VkFormat compressed_format = VK_FORMAT_UNDEFINED;
            switch (import_options.compression) {
            case TextureMetadata::TextureImportOptions::Compression::None:
                compressed_format = format;
                break;
            case TextureMetadata::TextureImportOptions::Compression::BC5:
                compressed_format = VK_FORMAT_BC5_UNORM_BLOCK;
                break;
            case TextureMetadata::TextureImportOptions::Compression::BC7:
                if (import_options.is_srgb) {
                    compressed_format = VK_FORMAT_BC7_SRGB_BLOCK;
                } else {
                    compressed_format = VK_FORMAT_BC7_UNORM_BLOCK;
                }
                break;
            }

            if (compressed_format == VK_FORMAT_UNDEFINED) {
                spdlog::error("Unsupported compressed format: {}", (uint32_t)compressed_format);
                ktxTexture_Destroy(ktxTexture(ktx_texture));
                free(ktx_data);
                return false;
            }

            TextureAssetHeader header = {
                .format              = compressed_format,
                .width               = static_cast<uint32_t>(width),
                .height              = static_cast<uint32_t>(height),
                .mip_levels          = mip_levels,
                .size                = file_size,
                .compressed          = true,
                .sampler_description = import_options.sampler_description,
            };

            std::ofstream               asset_file(destination, std::ios::binary);
            cereal::BinaryOutputArchive archive(asset_file);
            archive(header);
            archive.saveBinary(ktx_data, file_size);

            free(ktx_data);
        }

        ktxTexture_Destroy(ktxTexture(ktx_texture));
    } else {
        spdlog::warn("Uncompressed textures not supported currently");
    }

    return true;
}
