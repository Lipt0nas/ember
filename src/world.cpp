#include "world.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/cereal.hpp>
#include <ktx.h>

World::World() {
}

void World::initialize(
    SDL_Window* window, bool meshlets_enabled, bool hardware_rt_enabled, bool vsync, bool hdr_requested
) {
    this->input.initialize(this);
    this->scene.initialize(this);
    this->physics.initialize(this);
    this->script.initialize(this);
    this->asset_registry.initialize(this);
    this->renderer.initialize(this, window, meshlets_enabled, hardware_rt_enabled, vsync, hdr_requested);
    this->sound.initialize();
}

int World::load_texture(AssetID id) {
    auto it = texture_map.find(id);
    if (it != texture_map.end()) {
        return it->second;
    }

    VK_CHECK(vkDeviceWaitIdle(renderer.device));

    auto metadata = asset_registry.get_metadata<TextureMetadata>(id);
    if (!metadata) {
        spdlog::error("Failed to load texture {}", id);
        return -1;
    }

    std::ifstream asset_file(metadata->asset_path, std::ios::binary);
    if (!asset_file.is_open()) {
        spdlog::error("Failed to open texture {} for reading", metadata->asset_path);
        return -1;
    }

    cereal::BinaryInputArchive archive(asset_file);

    TextureAssetHeader header;
    archive(header);

    Image image;

    if (header.compressed) {
        std::vector<unsigned char> texture_data(header.size);
        archive.loadBinary(texture_data.data(), header.size);

        ktxTexture2*   ktx_texture;
        KTX_error_code result = ktxTexture2_CreateFromMemory(
            texture_data.data(), header.size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx_texture
        );

        if (result != KTX_SUCCESS) {
            spdlog::error("Failed to load KTX2 texture: {}", (int)result);
            return -1;
        }

        ktx_transcode_fmt_e transcode_fmt;
        if (header.format == VK_FORMAT_BC7_SRGB_BLOCK || header.format == VK_FORMAT_BC7_UNORM_BLOCK) {
            transcode_fmt = KTX_TTF_BC7_RGBA;
        } else if (header.format == VK_FORMAT_BC5_UNORM_BLOCK || header.format == VK_FORMAT_BC5_SNORM_BLOCK) {
            transcode_fmt = KTX_TTF_BC5_RG;
        } else {
            spdlog::error("Unsupported compressed format: {}", header.format);
            ktxTexture_Destroy(ktxTexture(ktx_texture));
            return -1;
        }

        result = ktxTexture2_TranscodeBasis(ktx_texture, transcode_fmt, 0);
        if (result != KTX_SUCCESS) {
            spdlog::error("Failed to transcode texture: {}", (int)result);
            ktxTexture_Destroy(ktxTexture(ktx_texture));
            return -1;
        }

        image = create_image(
            static_cast<VkFormat>(header.format),
            header.width,
            header.height,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            header.mip_levels > 1,
            renderer.vma_allocator,
            renderer.device
        );

        for (uint32_t mip = 0; mip < ktx_texture->numLevels; mip++) {
            ktx_size_t mip_offset;
            ktxTexture_GetImageOffset(ktxTexture(ktx_texture), mip, 0, 0, &mip_offset);
            ktx_size_t mip_size = ktxTexture_GetImageSize(ktxTexture(ktx_texture), mip);
            renderer.buffer_offsets.texture_data_size += mip_size;

            ktx_uint8_t* src_data = ktxTexture_GetData(ktxTexture(ktx_texture)) + mip_offset;

            void* staging_buffer_ptr = nullptr;
            VK_CHECK(
                vmaMapMemory(renderer.vma_allocator, renderer.buffers.staging_buffer.allocation, &staging_buffer_ptr)
            );
            std::memcpy(staging_buffer_ptr, src_data, mip_size);
            vmaUnmapMemory(renderer.vma_allocator, renderer.buffers.staging_buffer.allocation);

            auto command_buffer = renderer.allocate_temporary_command_buffer();
            copy_image_mip(
                renderer.buffers.staging_buffer, image, mip, command_buffer, renderer.graphics_queue, renderer.device
            );
            renderer.free_temporary_command_buffer(command_buffer);
        }
        ktxTexture2_Destroy(ktx_texture);

    } else {
        image = create_image(
            static_cast<VkFormat>(header.format),
            header.width,
            header.height,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            header.mip_levels > 1,
            renderer.vma_allocator,
            renderer.device
        );

        std::vector<unsigned char> texture_data(header.size);
        archive.loadBinary(texture_data.data(), header.size);

        int mip_width  = header.width;
        int mip_height = header.height;

        for (uint32_t mip = 0; mip < header.mip_levels; mip++) {
            size_t mip_offset = 0;
            size_t mip_size   = mip_width * mip_height * 4;

            renderer.buffer_offsets.texture_data_size += mip_size;

            void* staging_buffer_ptr = nullptr;
            VK_CHECK(
                vmaMapMemory(renderer.vma_allocator, renderer.buffers.staging_buffer.allocation, &staging_buffer_ptr)
            );
            std::memcpy(staging_buffer_ptr, texture_data.data() + mip_offset, mip_size);
            vmaUnmapMemory(renderer.vma_allocator, renderer.buffers.staging_buffer.allocation);

            auto command_buffer = renderer.allocate_temporary_command_buffer();
            copy_image_mip(
                renderer.buffers.staging_buffer, image, mip, command_buffer, renderer.graphics_queue, renderer.device
            );
            renderer.free_temporary_command_buffer(command_buffer);

            mip_width  = std::max(1, mip_width / 2);
            mip_height = std::max(1, mip_height / 2);
        }
    }

    auto sampler = get_sampler(header.sampler_description);

    int index = resources.images.size();
    resources.images.push_back({
        .image         = image,
        .sampler_index = 0,
    });

    texture_map.insert({id, index});
    register_bindless_texture(index, sampler);

    return index;
}

int World::load_texture(const std::string& path) {
    return load_texture(asset_registry.hash_path(asset_registry.root_path() / path));
}

int World::load_mesh(AssetID id) {
    auto it = mesh_map.find(id);
    if (it != mesh_map.end()) {
        return it->second;
    }

    VK_CHECK(vkDeviceWaitIdle(renderer.device));

    auto metadata = asset_registry.get_metadata<MeshMetadata>(id);
    if (!metadata) {
        spdlog::error("Failed to load mesh {}", id);
        return -1;
    }

    std::ifstream asset_file(metadata->asset_path, std::ios::binary);
    if (!asset_file.is_open()) {
        spdlog::error("Failed to open mesh {} for reading", metadata->asset_path);
        return -1;
    }

    needs_blas_rebuild = true;

    cereal::BinaryInputArchive archive(asset_file);

    MeshAssetHeader header;
    archive(header);

    size_t base_vertex_offset           = renderer.buffer_offsets.vertex_buffer / sizeof(Vertex);
    size_t base_index_offset            = renderer.buffer_offsets.index_buffer / sizeof(uint32_t);
    size_t base_meshlet_offset          = renderer.buffer_offsets.meshlet_buffer / sizeof(meshopt_Meshlet);
    size_t base_meshlet_vertex_offset   = renderer.buffer_offsets.meshlet_vertex_indices / sizeof(uint32_t);
    size_t base_meshlet_triangle_offset = renderer.buffer_offsets.meshlet_primitive_buffer / sizeof(uint8_t);

    std::vector<unsigned char> vertices(header.vertex_buffer_size);
    archive.loadBinary(vertices.data(), header.vertex_buffer_size);

    std::vector<unsigned char> indices(header.index_buffer_size);
    archive.loadBinary(indices.data(), header.index_buffer_size);

    std::vector<unsigned char> meshlets(header.meshlet_buffer_size);
    archive.loadBinary(meshlets.data(), header.meshlet_buffer_size);

    std::vector<unsigned char> meshlet_vertices(header.meshlet_vertex_indicies_buffer_size);
    archive.loadBinary(meshlet_vertices.data(), header.meshlet_vertex_indicies_buffer_size);

    std::vector<unsigned char> meshlet_triangles(header.meshlet_primitive_buffer_size);
    archive.loadBinary(meshlet_triangles.data(), header.meshlet_primitive_buffer_size);

    std::vector<unsigned char> meshlet_bounds(header.meshlet_bounds_buffer_size);
    archive.loadBinary(meshlet_bounds.data(), header.meshlet_bounds_buffer_size);

    size_t meshlet_count = header.meshlet_buffer_size / sizeof(meshopt_Meshlet);
    for (size_t i = 0; i < meshlet_count; i++) {
        meshopt_Meshlet* meshlet = reinterpret_cast<meshopt_Meshlet*>(meshlets.data()) + i;
        meshlet->vertex_offset += base_meshlet_vertex_offset;
        meshlet->triangle_offset += base_meshlet_triangle_offset;
    }

    size_t meshlet_vertex_count = header.meshlet_vertex_indicies_buffer_size / sizeof(uint32_t);
    for (size_t i = 0; i < meshlet_vertex_count; i++) {
        uint32_t* vertex_index = reinterpret_cast<uint32_t*>(meshlet_vertices.data()) + i;
        *vertex_index += base_vertex_offset;
    }

    Mesh mesh = {
        .center        = header.mesh.center,
        .radius        = header.mesh.radius,
        .bounds_min    = header.mesh.bounds_min,
        .bounds_max    = header.mesh.bounds_max,
        .vertex_offset = static_cast<uint32_t>(base_vertex_offset),
        .vertex_count  = header.mesh.vertex_count,
        .skin_offset   = 0,
        .lod_count     = header.mesh.lod_count,
    };

    for (int i = 0; i < header.mesh.lod_count; i++) {
        mesh.lods[i] = {
            .index_offset   = static_cast<uint32_t>(header.mesh.lods[i].index_offset + base_index_offset),
            .index_count    = header.mesh.lods[i].index_count,
            .meshlet_offset = static_cast<uint32_t>(header.mesh.lods[i].meshlet_offset + base_meshlet_offset),
            .meshlet_count  = header.mesh.lods[i].meshlet_count,
            .error          = header.mesh.lods[i].error,
        };
    }

    void* staging_buffer_ptr = nullptr;
    VK_CHECK(vmaMapMemory(renderer.vma_allocator, renderer.buffers.staging_buffer.allocation, &staging_buffer_ptr));

    {
        auto command_buffer = renderer.allocate_temporary_command_buffer();
        memcpy(staging_buffer_ptr, vertices.data(), header.vertex_buffer_size);
        copy_buffer(
            renderer.buffers.staging_buffer,
            renderer.buffers.vertex_buffer,
            command_buffer,
            renderer.graphics_queue,
            renderer.device,
            header.vertex_buffer_size,
            renderer.buffer_offsets.vertex_buffer
        );
        renderer.buffer_offsets.vertex_buffer += header.vertex_buffer_size;
        renderer.free_temporary_command_buffer(command_buffer);
    }

    {
        auto command_buffer = renderer.allocate_temporary_command_buffer();
        memcpy(staging_buffer_ptr, indices.data(), header.index_buffer_size);
        copy_buffer(
            renderer.buffers.staging_buffer,
            renderer.buffers.index_buffer,
            command_buffer,
            renderer.graphics_queue,
            renderer.device,
            header.index_buffer_size,
            renderer.buffer_offsets.index_buffer
        );
        renderer.buffer_offsets.index_buffer += header.index_buffer_size;
        renderer.free_temporary_command_buffer(command_buffer);
    }

    {
        auto command_buffer = renderer.allocate_temporary_command_buffer();
        memcpy(staging_buffer_ptr, meshlets.data(), header.meshlet_buffer_size);
        copy_buffer(
            renderer.buffers.staging_buffer,
            renderer.buffers.meshlet_buffer,
            command_buffer,
            renderer.graphics_queue,
            renderer.device,
            header.meshlet_buffer_size,
            renderer.buffer_offsets.meshlet_buffer
        );
        renderer.buffer_offsets.meshlet_buffer += header.meshlet_buffer_size;
        renderer.free_temporary_command_buffer(command_buffer);
    }

    {
        auto command_buffer = renderer.allocate_temporary_command_buffer();
        memcpy(staging_buffer_ptr, meshlet_vertices.data(), header.meshlet_vertex_indicies_buffer_size);
        copy_buffer(
            renderer.buffers.staging_buffer,
            renderer.buffers.meshlet_vertex_indices,
            command_buffer,
            renderer.graphics_queue,
            renderer.device,
            header.meshlet_vertex_indicies_buffer_size,
            renderer.buffer_offsets.meshlet_vertex_indices
        );
        renderer.buffer_offsets.meshlet_vertex_indices += header.meshlet_vertex_indicies_buffer_size;
        renderer.free_temporary_command_buffer(command_buffer);
    }

    {
        auto command_buffer = renderer.allocate_temporary_command_buffer();
        memcpy(staging_buffer_ptr, meshlet_triangles.data(), header.meshlet_primitive_buffer_size);
        copy_buffer(
            renderer.buffers.staging_buffer,
            renderer.buffers.meshlet_primitive_buffer,
            command_buffer,
            renderer.graphics_queue,
            renderer.device,
            header.meshlet_primitive_buffer_size,
            renderer.buffer_offsets.meshlet_primitive_buffer
        );
        renderer.buffer_offsets.meshlet_primitive_buffer += header.meshlet_primitive_buffer_size;
        renderer.free_temporary_command_buffer(command_buffer);
    }

    {
        auto command_buffer = renderer.allocate_temporary_command_buffer();
        memcpy(staging_buffer_ptr, meshlet_bounds.data(), header.meshlet_bounds_buffer_size);
        copy_buffer(
            renderer.buffers.staging_buffer,
            renderer.buffers.meshlet_bounds_buffer,
            command_buffer,
            renderer.graphics_queue,
            renderer.device,
            header.meshlet_bounds_buffer_size,
            renderer.buffer_offsets.meshlet_bounds_buffer
        );
        renderer.buffer_offsets.meshlet_bounds_buffer += header.meshlet_bounds_buffer_size;
        renderer.free_temporary_command_buffer(command_buffer);
    }

    if (header.skin_buffer_size > 0) {
        std::vector<unsigned char> skin_data(header.skin_buffer_size);
        archive.loadBinary(skin_data.data(), header.skin_buffer_size);

        auto command_buffer = renderer.allocate_temporary_command_buffer();
        memcpy(staging_buffer_ptr, skin_data.data(), header.skin_buffer_size);
        copy_buffer(
            renderer.buffers.staging_buffer,
            renderer.buffers.skin_buffer,
            command_buffer,
            renderer.graphics_queue,
            renderer.device,
            header.skin_buffer_size,
            renderer.buffer_offsets.skin_buffer_offset
        );
        renderer.free_temporary_command_buffer(command_buffer);

        mesh.skin_offset = renderer.buffer_offsets.skin_buffer_offset / sizeof(VertexSkinData);
        renderer.buffer_offsets.skin_buffer_offset += header.skin_buffer_size;
    }

    vmaUnmapMemory(renderer.vma_allocator, renderer.buffers.staging_buffer.allocation);

    int index = resources.meshes.size();
    resources.meshes.push_back(mesh);

    mesh_map.insert({id, index});

    return index;
}

int World::load_mesh(const std::string& path) {
    return load_mesh(asset_registry.hash_path(asset_registry.root_path() / path));
}

int World::load_font(AssetID id) {
    auto it = font_map.find(id);
    if (it != font_map.end()) {
        return it->second;
    }

    auto metadata = asset_registry.get_metadata<FontMetadata>(id);
    if (!metadata) {
        spdlog::error("Failed to load font {}", id);
        return -1;
    }

    std::ifstream asset_file(metadata->asset_path, std::ios::binary);
    if (!asset_file.is_open()) {
        spdlog::error("Failed to open font {} for reading", metadata->asset_path);
        return -1;
    }

    cereal::BinaryInputArchive archive(asset_file);

    Font font;
    archive(font);

    load_texture(font.atlas_texture_id);

    int index = resources.fonts.size();
    resources.fonts.push_back(font);
    font_map[id] = index;

    return index;
}

int World::load_font(const std::string& path) {
    return load_font(asset_registry.hash_path(asset_registry.root_path() / path));
}

int World::load_sound(AssetID id) {
    auto it = sound_map.find(id);
    if (it != sound_map.end()) {
        return it->second;
    }

    auto metadata = asset_registry.get_metadata<SoundMetadata>(id);
    if (!metadata) {
        spdlog::error("Failed to load sound {}", id);
        return -1;
    }

    Sound sound = {
        .path   = metadata->asset_path,
        .stream = metadata->import_options.stream,
    };

    this->sound.preload(sound);

    int index = resources.sounds.size();
    resources.sounds.push_back(sound);
    sound_map[id] = index;

    return index;
}

int World::load_sound(const std::string& path) {
    return load_sound(asset_registry.hash_path(asset_registry.root_path() / path));
}

int World::load_skeleton(AssetID id) {
    auto it = skeleton_map.find(id);
    if (it != skeleton_map.end()) {
        return it->second;
    }

    auto metadata = asset_registry.get_metadata<SkeletonMetadata>(id);
    if (!metadata) {
        spdlog::error("Failed to load skeleton {}", id);
        return -1;
    }

    std::ifstream              file(metadata->asset_path, std::ios::binary);
    cereal::BinaryInputArchive archive(file);

    SkeletonAssetHeader header;
    archive(header);

    std::vector<SkeletonAssetHeader::JointDescription> joint_descs(header.joint_count);
    archive.loadBinary(joint_descs.data(), header.joint_count * sizeof(SkeletonAssetHeader::JointDescription));

    Skeleton skeleton;
    skeleton.joints.resize(header.joint_count);

    for (int i = 0; i < (int)header.joint_count; i++) {
        skeleton.joints[i].parent_index        = joint_descs[i].parent_index;
        skeleton.joints[i].bind_translation    = joint_descs[i].bind_translation;
        skeleton.joints[i].bind_rotation       = joint_descs[i].bind_rotation;
        skeleton.joints[i].bind_scale          = joint_descs[i].bind_scale;
        skeleton.joints[i].inverse_bind_matrix = joint_descs[i].inverse_bind_matrix;
    }

    int index = resources.skeletons.size();
    resources.skeletons.push_back(skeleton);
    skeleton_map[id] = index;

    return index;
}

int World::load_skeleton(const std::string& path) {
    return load_skeleton(asset_registry.hash_path(asset_registry.root_path() / path));
}

int World::load_animation(AssetID id) {
    auto it = animation_map.find(id);
    if (it != animation_map.end())
        return it->second;

    auto metadata = asset_registry.get_metadata<AnimationMetadata>(id);
    if (!metadata) {
        spdlog::error("Failed to load animation {}", id);
        return -1;
    }

    std::ifstream              file(metadata->asset_path, std::ios::binary);
    cereal::BinaryInputArchive archive(file);

    AnimationAssetHeader header;
    archive(header);

    std::vector<AnimationAssetHeader::ChannelDescriptor> channel_descs(header.channel_count);
    archive.loadBinary(channel_descs.data(), header.channel_count * sizeof(AnimationAssetHeader::ChannelDescriptor));

    std::vector<float>     all_times(header.time_buffer_size / sizeof(float));
    std::vector<glm::vec3> all_vec3(header.vec3_buffer_size / sizeof(glm::vec3));
    std::vector<glm::quat> all_quat(header.quat_buffer_size / sizeof(glm::quat));

    archive.loadBinary(all_times.data(), header.time_buffer_size);
    archive.loadBinary(all_vec3.data(), header.vec3_buffer_size);
    archive.loadBinary(all_quat.data(), header.quat_buffer_size);

    Animation anim;
    anim.name           = metadata->source_path;
    anim.duration       = header.duration;
    anim.skeleton_index = load_skeleton(metadata->skeleton_id);

    if (anim.skeleton_index == -1) {
        spdlog::error("Failed to load skeleton for animation {}", id);
        return -1;
    }

    anim.channels.resize(header.channel_count);
    for (int i = 0; i < (int)header.channel_count; i++) {
        const auto& desc = channel_descs[i];

        AnimationChannel& channel = anim.channels[i];
        channel.joint_index       = desc.joint_index;

        switch (desc.path) {
        case 0:
            channel.path = "translation";
            break;
        case 1:
            channel.path = "rotation";
            break;
        case 2:
            channel.path = "scale";
            break;
        }

        channel.sampler.times = std::vector<float>(
            all_times.begin() + desc.time_offset, all_times.begin() + desc.time_offset + desc.keyframe_count
        );

        if (desc.path == 1) {
            channel.sampler.quat_values = std::vector<glm::quat>(
                all_quat.begin() + desc.value_offset, all_quat.begin() + desc.value_offset + desc.keyframe_count
            );
        } else {
            channel.sampler.vec_values = std::vector<glm::vec3>(
                all_vec3.begin() + desc.value_offset, all_vec3.begin() + desc.value_offset + desc.keyframe_count
            );
        }
    }

    int index = resources.animations.size();
    resources.animations.push_back(std::move(anim));
    animation_map.insert({id, index});

    return index;
}

int World::load_animation(const std::string& path) {
    return load_skeleton(asset_registry.hash_path(asset_registry.root_path() / path));
}

int World::load_material(AssetID id) {
    auto it = material_map.find(id);
    if (it != material_map.end()) {
        return it->second;
    }

    auto metadata = asset_registry.get_metadata<MaterialMetadata>(id);
    if (!metadata) {
        spdlog::error("Failed to load material {}", id);
        return -1;
    }

    std::ifstream asset_file(metadata->asset_path);
    if (!asset_file.is_open()) {
        spdlog::error("Failed to open material {} for reading", metadata->asset_path);
        return -1;
    }

    cereal::JSONInputArchive archive(asset_file);

    MaterialDescription description;
    archive(description);

    Material mat = {
        .albedo_index = description.albedo == AssetMetadata::INVALID_METADATA ? -1 : load_texture(description.albedo),
        .normals_index =
            description.normals == AssetMetadata::INVALID_METADATA ? -1 : load_texture(description.normals),
        .material_index =
            description.material == AssetMetadata::INVALID_METADATA ? -1 : load_texture(description.material),
        .emissive_index =
            description.emissive == AssetMetadata::INVALID_METADATA ? -1 : load_texture(description.emissive),
        .albedo_factor    = description.albedo_factor,
        .emissive_factor  = description.emissive_factor,
        .roughness_factor = description.roughness_factor,
        .metallic_factor  = description.metallic_factor,
        .normal_scale     = description.normal_scale,
    };

    int index = resources.materials.size();
    resources.materials.push_back(mat);

    material_map.insert({id, index});

    return index;
}

int World::load_material(const std::string& path) {
    return load_material(asset_registry.hash_path(asset_registry.root_path() / path));
}

std::string World::load_script(AssetID id) {
    auto metadata = asset_registry.get_metadata<ScriptMetadata>(id);
    if (!metadata) {
        spdlog::error("Failed to load script {}", id);
        return "";
    }

    std::ifstream asset_file(metadata->asset_path);
    if (!asset_file.is_open()) {
        spdlog::error("Failed to open script {} for reading", metadata->asset_path);
        return "";
    }

    return std::string(std::istreambuf_iterator<char>(asset_file), std::istreambuf_iterator<char>());
}

std::string World::load_script(const std::string& path) {
    return load_script(asset_registry.hash_path(asset_registry.root_path() / path));
}

bool World::load_collision_mesh(AssetID id, JPH::TriangleList& triangles) {
    auto metadata = asset_registry.get_metadata<MeshMetadata>(id);
    if (!metadata) {
        spdlog::error("Failed to load mesh {}", id);
        return false;
    }

    std::ifstream asset_file(metadata->asset_path, std::ios::binary);
    if (!asset_file.is_open()) {
        spdlog::error("Failed to open mesh {} for reading", metadata->asset_path);
        return false;
    }

    cereal::BinaryInputArchive archive(asset_file);

    MeshAssetHeader header;
    archive(header);

    std::vector<Vertex> vertices(header.vertex_buffer_size / sizeof(Vertex));
    archive.loadBinary(vertices.data(), header.vertex_buffer_size);

    std::vector<uint32_t> indices(header.index_buffer_size / sizeof(uint32_t));
    archive.loadBinary(indices.data(), header.index_buffer_size);

    auto lowest_lod = header.mesh.lods[header.mesh.lod_count - 1];
    auto count      = lowest_lod.index_count;

    for (size_t i = 0; i < count; i += 3) {
        auto i0 = indices[i + 0 + lowest_lod.index_offset];
        auto i1 = indices[i + 1 + lowest_lod.index_offset];
        auto i2 = indices[i + 2 + lowest_lod.index_offset];

        glm::vec3 v0(
            meshopt_dequantizeHalf(vertices[i0].px),
            meshopt_dequantizeHalf(vertices[i0].py),
            meshopt_dequantizeHalf(vertices[i0].pz)
        );
        glm::vec3 v1(
            meshopt_dequantizeHalf(vertices[i1].px),
            meshopt_dequantizeHalf(vertices[i1].py),
            meshopt_dequantizeHalf(vertices[i1].pz)
        );
        glm::vec3 v2(
            meshopt_dequantizeHalf(vertices[i2].px),
            meshopt_dequantizeHalf(vertices[i2].py),
            meshopt_dequantizeHalf(vertices[i2].pz)
        );

        triangles.push_back(
            JPH::Triangle(JPH::Float3(v0.x, v0.y, v0.z), JPH::Float3(v1.x, v1.y, v1.z), JPH::Float3(v2.x, v2.y, v2.z))
        );
    }

    return true;
}

bool World::load_bottom_lod_mesh(AssetID id, std::vector<glm::vec3>& out_vertices, std::vector<uint32_t>& out_indices) {
    auto metadata = asset_registry.get_metadata<MeshMetadata>(id);
    if (!metadata) {
        spdlog::error("Failed to load mesh {}", id);
        return false;
    }

    std::ifstream asset_file(metadata->asset_path, std::ios::binary);
    if (!asset_file.is_open()) {
        spdlog::error("Failed to open mesh {} for reading", metadata->asset_path);
        return false;
    }

    cereal::BinaryInputArchive archive(asset_file);

    MeshAssetHeader header;
    archive(header);

    std::vector<Vertex> vertices(header.vertex_buffer_size / sizeof(Vertex));
    archive.loadBinary(vertices.data(), header.vertex_buffer_size);

    std::vector<uint32_t> indices(header.index_buffer_size / sizeof(uint32_t));
    archive.loadBinary(indices.data(), header.index_buffer_size);

    auto lowest_lod = header.mesh.lods[header.mesh.lod_count - 1];
    auto count      = lowest_lod.index_count;

    for (size_t i = 0; i < vertices.size(); i++) {
        out_vertices.push_back(
            glm::vec3(
                meshopt_dequantizeHalf(vertices[i].px),
                meshopt_dequantizeHalf(vertices[i].py),
                meshopt_dequantizeHalf(vertices[i].pz)
            )
        );
    }

    for (size_t i = 0; i < count; i += 3) {
        auto i0 = indices[i + 0 + lowest_lod.index_offset];
        auto i1 = indices[i + 1 + lowest_lod.index_offset];
        auto i2 = indices[i + 2 + lowest_lod.index_offset];

        out_indices.push_back(i0);
        out_indices.push_back(i1);
        out_indices.push_back(i2);
    }

    return true;
}

JPH::ShapeRefC World::get_vhacd_shape(AssetID id) {
    auto it = vhacd_shapes.find(id);
    if (it != vhacd_shapes.end()) {
        return it->second;
    }

    std::vector<glm::vec3> vertices;
    std::vector<uint32_t>  indices;
    if (!load_bottom_lod_mesh(id, vertices, indices)) {
        spdlog::warn("Failed to load bottom level LOD for VHACD");
        return nullptr;
    }

    auto                      vhacd = VHACD::CreateVHACD();
    VHACD::IVHACD::Parameters params;
    params.m_maxConvexHulls                   = 32;
    params.m_resolution                       = 100000;
    params.m_minimumVolumePercentErrorAllowed = 1.0;
    params.m_maxNumVerticesPerCH              = 64;

    vhacd->Compute((float*)vertices.data(), vertices.size(), indices.data(), indices.size() / 3, params);

    JPH::StaticCompoundShapeSettings compound;
    for (uint32_t i = 0; i < vhacd->GetNConvexHulls(); i++) {
        VHACD::IVHACD::ConvexHull hull;
        if (!vhacd->GetConvexHull(i, hull)) {
            continue;
        }

        JPH::Array<JPH::Vec3> pts;
        for (auto& pt : hull.m_points) {
            pts.push_back(JPH::Vec3(pt.mX, pt.mY, pt.mZ));
        }

        auto* hull_settings             = new JPH::ConvexHullShapeSettings(pts);
        hull_settings->mMaxConvexRadius = 0.01f;
        compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), hull_settings);
    }

    vhacd->Clean();
    vhacd->Release();

    auto result = compound.Create();
    if (result.HasError()) {
        spdlog::warn("Failed to create VHACD compound shape: {}", result.GetError());
        return nullptr;
    }

    JPH::ShapeRefC shape = result.Get();
    vhacd_shapes[id]     = shape;

    return shape;
}

bool World::rebuild_physics_body(Entity e) {
    auto p = scene.get_component<components::Physics>(e);
    auto t = scene.get_component<components::Transform>(e);

    if (!p) {
        return false;
    }

    if (!p->body_id.IsInvalid()) {
        JPH::BodyInterface& body_interface = physics.system.GetBodyInterface();
        body_interface.RemoveBody(p->body_id);
        body_interface.DestroyBody(p->body_id);

        p->body_id = JPH::BodyID(JPH::BodyID::cInvalidBodyID);
    }

    if (p->collider_type == PhysicsColliderType::NONE) {
        return true;
    }

    JPH::EMotionType motion_type = JPH::EMotionType::Static;
    switch (p->motion_type) {
    case PhysicsMotionType::STATIC:
        motion_type = JPH::EMotionType::Static;
        break;
    case PhysicsMotionType::KINEMATIC:
        motion_type = JPH::EMotionType::Kinematic;
        break;
    case PhysicsMotionType::DYNAMIC:
        motion_type = JPH::EMotionType::Dynamic;
        break;
    }

    JPH::EActivation activation =
        p->motion_type == PhysicsMotionType::DYNAMIC ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;

    float scale = t->world_scale;

    JPH::ShapeRefC shape;

    switch (p->collider_type) {
    case PhysicsColliderType::BOX: {
        auto                  he = p->box_half_extent;
        JPH::BoxShapeSettings settings(JPH::Vec3(he.x, he.y, he.z) * scale);
        auto                  result = settings.Create();
        if (result.HasError()) {
            spdlog::warn("Failed to create box shape: {}", result.GetError());
            return false;
        }
        shape = result.Get();
        break;
    }
    case PhysicsColliderType::CAPSULE: {
        JPH::CapsuleShapeSettings settings(p->capsule_half_height * scale, p->capsule_radius * scale);
        auto                      result = settings.Create();
        if (result.HasError()) {
            spdlog::warn("Failed to create capsule shape: {}", result.GetError());
            return false;
        }
        shape = result.Get();

        break;
    }
    case PhysicsColliderType::SPHERE: {
        JPH::SphereShapeSettings settings(p->sphere_radius * scale);
        auto                     result = settings.Create();
        if (result.HasError()) {
            spdlog::warn("Failed to create sphere shape: {}", result.GetError());
            return false;
        }
        shape = result.Get();
        break;
    }
    case PhysicsColliderType::MESH: {
        auto m = scene.get_component<components::Mesh>(e);
        if (!m) {
            spdlog::warn("No source mesh component for Mesh collider");
            return false;
        }

        if (p->motion_type == PhysicsMotionType::DYNAMIC) {
            spdlog::warn("Mesh collider body can't be dynamic");
            return false;
        }

        JPH::TriangleList triangles;
        if (!load_collision_mesh(m->id, triangles)) {
            spdlog::warn("Failed to load bottom level LOD for Mesh");
            return false;
        }

        JPH::MeshShapeSettings mesh_settings(triangles);

        auto result = mesh_settings.Create();
        if (result.HasError()) {
            spdlog::warn("Failed to create Mesh compound shape: {}", result.GetError());
            return false;
        }

        if (scale != 1.0f) {
            JPH::ScaledShapeSettings scaled(result.Get(), JPH::Vec3::sReplicate(scale));
            auto                     scaled_result = scaled.Create();
            if (scaled_result.HasError()) {
                spdlog::warn("Failed to create scaled Mesh shape: {}", scaled_result.GetError());
                return false;
            }
            shape = scaled_result.Get();
        } else {
            shape = result.Get();
        }

        break;
    }
    case PhysicsColliderType::VHACD: {
        auto m = scene.get_component<components::Mesh>(e);

        if (!m) {
            spdlog::warn("No source mesh component for VHACD");
            return false;
        }

        JPH::ShapeRefC result = get_vhacd_shape(m->id);
        if (!result) {
            return false;
        }

        if (scale != 1.0f) {
            JPH::ScaledShapeSettings scaled(result, JPH::Vec3::sReplicate(scale));
            auto                     scaled_result = scaled.Create();
            if (scaled_result.HasError()) {
                spdlog::warn("Failed to create scaled VHACD shape: {}", scaled_result.GetError());
                return false;
            }
            shape = scaled_result.Get();
        } else {
            shape = result;
        }
        break;
    }
    default:
        return false;
    }

    auto pos = t->world_position;
    auto rot = t->world_rotation;

    glm::vec3 pivot = p->pivot_offset * scale;
    JPH::Vec3 shape_offset(-pivot.x, -pivot.y, -pivot.z);

    if (shape_offset.Length() > 0.001f) {
        JPH::OffsetCenterOfMassShapeSettings offset_settings(shape_offset, shape);
        auto                                 offset_result = offset_settings.Create();
        if (offset_result.HasError()) {
            spdlog::warn("Failed to offset shape: {}", offset_result.GetError());
            return false;
        }
        shape = offset_result.Get();
    }

    JPH::RVec3 body_pos = JPH::RVec3(pos.x, pos.y, pos.z);
    JPH::Quat  body_rot = JPH::Quat(rot.x, rot.y, rot.z, rot.w);

    JPH::BodyCreationSettings body_settings(shape, body_pos, body_rot, motion_type, p->layer);

    body_settings.mAllowDynamicOrKinematic = true;
    body_settings.mFriction                = p->friction;
    body_settings.mRestitution             = p->restitution;
    body_settings.mLinearDamping           = p->linear_damping;
    body_settings.mAngularDamping          = p->angular_damping;

    if (p->mass_override > 0.0f) {
        body_settings.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateInertia;
        body_settings.mMassPropertiesOverride.mMass = p->mass_override;
    } else {
        body_settings.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateMassAndInertia;
        body_settings.mMassPropertiesOverride.mMass = p->density;
    }

    JPH::BodyInterface& bi      = physics.system.GetBodyInterface();
    JPH::BodyID         body_id = bi.CreateAndAddBody(body_settings, activation);

    if (body_id.IsInvalid()) {
        spdlog::warn("Failed to create physics body");
        return false;
    }

    bi.SetUserData(body_id, (uint64_t)e);
    p->body_id    = body_id;
    p->last_scale = scale;

    return true;
}

int World::load_particle_effect(AssetID id) {
    auto it = particle_effect_map.find(id);
    if (it != particle_effect_map.end()) {
        return it->second;
    }

    auto metadata = asset_registry.get_metadata<ParticleEffectMetadata>(id);
    if (!metadata) {
        spdlog::error("Failed to load particle effect {}", id);
        return -1;
    }

    std::ifstream asset_file(metadata->asset_path, std::ios::binary);
    if (!asset_file.is_open()) {
        spdlog::error("Failed to open particle effect file {} for reading", metadata->asset_path);
        return -1;
    }

    cereal::BinaryInputArchive archive(asset_file);

    ParticleEffectHeader effect_header;
    archive(effect_header);

    spdlog::info("Loading {} emmiters", effect_header.emmiter_count);

    ParticleEffectAsset effect;

    for (int i = 0; i < effect_header.emmiter_count; i++) {
        spdlog::info("Loading emmiter {}", i);

        ParticleEmitterHeader header;
        archive(header);

        auto spawn_instruction_count  = header.spawn_instruction_size / sizeof(ParticleInstruction);
        auto update_instruction_count = header.update_instruction_size / sizeof(ParticleInstruction);

        ParticleEmitterAsset emitter;
        emitter.spawn_register_state.resize(header.spawn_register_count);
        emitter.spawn_instructions.resize(spawn_instruction_count);

        emitter.update_register_state.resize(header.update_register_count);
        emitter.update_instructions.resize(update_instruction_count);

        archive.loadBinary(emitter.spawn_register_state.data(), header.spawn_register_count * sizeof(glm::vec4));
        archive.loadBinary(emitter.spawn_instructions.data(), header.spawn_instruction_size);

        archive.loadBinary(emitter.update_register_state.data(), header.update_register_count * sizeof(glm::vec4));
        archive.loadBinary(emitter.update_instructions.data(), header.update_instruction_size);

        effect.emitters.push_back(std::move(emitter));
    }

    int index = resources.particle_effects.size();
    resources.particle_effects.push_back(effect);

    particle_effect_map.insert({id, index});

    return index;
}

int World::load_particle_effect(const std::string& path) {
    return load_particle_effect(asset_registry.hash_path(asset_registry.root_path() / path));
}

void World::reload_particle_effect(AssetID id) {
    auto it = particle_effect_map.find(id);
    if (it != particle_effect_map.end()) {
        particle_effect_map.erase(it);
    }
}

void World::register_bindless_texture(int index, const Sampler& sampler) {
    uint32_t slot = index;

    VkDescriptorImageInfo image_write_info = {
        .sampler     = sampler.handle,
        .imageView   = resources.images[index].image.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    VkWriteDescriptorSet write_set = {
        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext            = nullptr,
        .dstSet           = renderer.global_texture_descriptor_set,
        .dstBinding       = 0,
        .dstArrayElement  = slot,
        .descriptorCount  = 1,
        .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo       = &image_write_info,
        .pBufferInfo      = nullptr,
        .pTexelBufferView = nullptr
    };
    vkUpdateDescriptorSets(renderer.device, 1, &write_set, 0, nullptr);
}

Sampler World::get_sampler(const SamplerDescription& description) {
    auto it = resources.samplers.find(description.state_hash());
    if (it != resources.samplers.end()) {
        return it->second;
    }

    auto sampler = create_sampler(
        description.filter,
        description.filter,
        description.mipmap_mode,
        description.address_mode,
        description.address_mode,
        description.address_mode,
        description.anisotropy,
        renderer.device
    );

    resources.samplers.insert({description.state_hash(), std::move(sampler)});

    return resources.samplers[description.state_hash()];
}

int World::dedicate_material(components::Material& mat, int original_id) {
    Material result = resources.materials[original_id];

    int index = resources.runtime_materials.size();
    resources.runtime_materials.push_back(result);

    return index;
}

void World::apply_material_override(components::Material& mat) {
    Material& result = resources.runtime_materials[mat.dedicated_material_index];

    if (mat.overrides.albedo_factor) {
        result.albedo_factor = *mat.overrides.albedo_factor;
    }
    if (mat.overrides.emissive_factor) {
        result.emissive_factor = *mat.overrides.emissive_factor;
    }
    if (mat.overrides.roughness_factor) {
        result.roughness_factor = *mat.overrides.roughness_factor;
    }
    if (mat.overrides.metallic_factor) {
        result.metallic_factor = *mat.overrides.metallic_factor;
    }
    if (mat.overrides.normal_scale) {
        result.normal_scale = *mat.overrides.normal_scale;
    }
}

Material* World::get_material(AssetID id) {
    int index = load_material(id);

    return index == -1 ? nullptr : &resources.materials[load_material(id)];
}

void World::cleanup() {
    for (auto& image : resources.images) {
        destroy_image(image.image, renderer.device, renderer.vma_allocator);
    }

    for (auto& [hash, sampler] : resources.samplers) {
        destroy_sampler(sampler, renderer.device);
    }

    scene.cleanup();
    renderer.cleanup();
}

int World::node_play_sound(Entity e) {
    auto s = scene.get_component<components::Sound>(e);
    if (!s) {
        return SoundSystem::INVALID_SOUND_INSTANCE;
    }

    auto t = scene.get_component<components::Transform>(e);

    int index = load_sound(s->sound_id);
    if (index == -1) {
        spdlog::error("Failed to play sound");
        return SoundSystem::INVALID_SOUND_INSTANCE;
    }

    auto& sound = resources.sounds[index];

    int sound_index = this->sound.play_sound(sound.path.c_str(), s->spatial);
    this->sound.set_sound_properties(
        sound_index, s->volume, s->pitch, s->min_distance, s->max_distance, s->rolloff, s->loop, t->world_position
    );

    return sound_index;
}

int World::play_sound(AssetID id, bool spatial) {
    int index = load_sound(id);
    if (index == -1) {
        spdlog::error("Failed to play sound");
        return SoundSystem::INVALID_SOUND_INSTANCE;
    }

    auto& sound = resources.sounds[index];

    return this->sound.play_sound(sound.path.c_str(), spatial);
}

int World::play_sound(const std::string& path, bool spatial) {
    return play_sound(asset_registry.hash_path(asset_registry.root_path() / path), spatial);
}

int World::load_ies_profile(AssetID id) {
    auto it = ies_profile_map.find(id);
    if (it != ies_profile_map.end()) {
        return it->second;
    }

    auto metadata = asset_registry.get_metadata<IESProfileMetadata>(id);
    if (!metadata) {
        spdlog::error("Failed to load IES profile {}", id);
        return -1;
    }

    std::ifstream asset_file(metadata->asset_path);
    if (!asset_file.is_open()) {
        spdlog::error("Failed to open IES profile {} for reading", metadata->asset_path);
        return -1;
    }

    cereal::BinaryInputArchive archive(asset_file);

    IESProfile profile;
    archive(profile);

    int index = resources.ies_profiles.size();
    resources.ies_profiles.push_back(profile);

    ies_profile_map.insert({id, index});

    return index;
}

int World::load_ies_profile(const std::string& path) {
    return load_ies_profile(asset_registry.hash_path(asset_registry.root_path() / path));
}
