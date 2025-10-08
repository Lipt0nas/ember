#include "ember.hpp"

#include "camera.hpp"
#include "device.hpp"
#include "pipeline.hpp"
#include "resources.hpp"
#include "swapchain.hpp"

PFN_vkVoidFunction imgui_load_function(const char* function_name, void* user_data) {
    return vkGetInstanceProcAddr((VkInstance)user_data, function_name);
}

struct SceneUBO {
    glm::mat4 view_proj;
    glm::vec4 planes[6];
    glm::vec4 camera_position;

    glm::mat4 frozen_view_proj;
    glm::vec4 frozen_planes[6];
    glm::vec4 frozen_camera_position;

    uint32_t debug_frustum;
    uint32_t disable_culling;
};

struct PushConstants {
    VkDeviceSize vertex_buffer_address;
    VkDeviceSize index_buffer_address;
    VkDeviceSize meshlet_buffer_address;
    VkDeviceSize meshlet_vertex_buffer_indices_address;
    VkDeviceSize meshlet_primitive_indices_buffer_address;
    VkDeviceSize meshlet_bounds_buffer_address;
};

struct PostProcesPushConstants {
    glm::mat4 combined;
    glm::mat4 inv_combined;
    glm::vec4 camera_position;

    uint32_t depth_index;
    uint32_t albedo_index;
    uint32_t normals_index;
    uint32_t material_index;

    uint32_t lightpass_index;
};

struct DrawData {
    glm::mat4 model_matrix;

    // --- INDIRECT VERTEX PIPELINE ---
    uint32_t first_index;
    int32_t  vertex_offset;

    // --- MESHLET PIPELINE ---
    uint32_t meshlet_offset;
    uint32_t meshlet_count;

    uint32_t albedo_index;
    uint32_t normals_index;
    uint32_t material_index;
    float    _pad[1];
};

struct MeshletBounds {
    glm::vec3 center;
    float     radius;

    glm::vec3 cone_axis;
    float     cone_cutoff;

    glm::vec3 cone_apex;
    float     _pad;
};

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;

    bool operator==(const Vertex& other) const {
        return position == other.position && normal == other.normal && uv == other.uv;
    }
};

struct Material {
    uint32_t albedo_index;
    uint32_t normals_index;
    uint32_t material_index;
};

struct Mesh {
    VkDeviceSize vertex_buffer_offset;
    VkDeviceSize index_buffer_offset;

    uint32_t meshlet_offset;
    uint32_t meshlet_count;

    uint32_t vertex_count;
    uint32_t index_count;

    Material material;

    glm::vec3 position = glm::vec3(0.0f);
    float     scale    = 1.0f;
};

std::vector<Mesh> load_model(
    const std::filesystem::path&         path,
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

    std::vector<Mesh> meshes;

    uint32_t                               local_cache_offset = global_texture_cache.size();
    std::unordered_map<uint32_t, uint32_t> local_texture_cache;

    for (int m = 0; m < model.meshes.size(); m++) {
        spdlog::trace("mesh {}", m);
        const tinygltf::Mesh& mesh = model.meshes[m];

        for (int p = 0; p < mesh.primitives.size(); p++) {
            spdlog::trace("primitive {}", p);
            std::vector<Vertex>   vertices;
            std::vector<uint32_t> indices;

            const tinygltf::Primitive& primitive = mesh.primitives[p];

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

            for (size_t i = 0; i < vertex_count; i++) {
                vertices.emplace_back(
                    Vertex{
                        .position =
                            {
                                positions[i * 3 + 2],
                                positions[i * 3 + 1],
                                positions[i * 3 + 0],
                            },
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
            void* staging_buffer_ptr = nullptr;
            VK_CHECK(vmaMapMemory(allocator, staging_buffer.allocation, &staging_buffer_ptr));
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

            uint32_t albedo_index   = 0;
            uint32_t normals_index  = 0;
            uint32_t material_index = 0;
            if (primitive.material >= 0) {
                const tinygltf::Material& material = model.materials[primitive.material];

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
                                allocator,
                                device
                            );

                            if (img.image.size() > staging_buffer.size) {
                                spdlog::error(
                                    "Attempted out of bounds buffer write for image, size={}, staging size={}",
                                    img.image.size(),
                                    staging_buffer.size
                                );
                                exit(1);
                            }

                            memcpy(staging_buffer_ptr, &img.image.at(0), img.image.size());
                            copy_image(staging_buffer, image, command_buffer, queue, device);

                            uint32_t index = global_texture_cache.size();
                            global_texture_cache.insert({index, image});
                            local_texture_cache.insert({image_index + local_cache_offset, index});

                            loaded_images.push_back(image);

                            return index;
                        };
                    }

                    return 0u;
                };

                if (material.pbrMetallicRoughness.baseColorTexture.index >= 0) {
                    albedo_index =
                        upload_texture(material.pbrMetallicRoughness.baseColorTexture.index, VK_FORMAT_R8G8B8A8_SRGB);
                } else {
                    spdlog::warn("Model {} primitive {} does not have a base color texture!", path.string(), p);
                }

                if (material.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
                    material_index = upload_texture(
                        material.pbrMetallicRoughness.metallicRoughnessTexture.index, VK_FORMAT_R8G8B8A8_UNORM
                    );
                } else {
                    spdlog::warn("Model {} primitive {} does not have a metallic/roughness texture!", path.string(), p);
                }

                if (material.normalTexture.index >= 0) {
                    normals_index = upload_texture(material.normalTexture.index, VK_FORMAT_R8G8B8A8_UNORM);
                } else {
                    spdlog::warn("Model {} primitive {} does not have a normals texture!", path.string(), p);
                }
            }

            spdlog::debug("Building meshlets");
            const size_t max_vertices  = 64;
            const size_t max_triangles = 124;
            const float  cone_weight   = 0.5f;

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
                    .center =
                        {
                            bounds.center[0],
                            bounds.center[1],
                            bounds.center[2],
                        },
                    .radius = bounds.radius,
                    .cone_axis =
                        {
                            bounds.cone_axis[0],
                            bounds.cone_axis[1],
                            bounds.cone_axis[2],
                        },
                    .cone_cutoff = bounds.cone_cutoff,
                    .cone_apex   = {
                        bounds.cone_apex[0],
                        bounds.cone_apex[1],
                        bounds.cone_apex[2],
                    },
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

            meshes.emplace_back(
                mesh_vertex_offset,
                mesh_index_offset,
                current_meshlet_offset,
                meshlet_count,
                vertices.size(),
                indices.size(),
                Material{
                    .albedo_index   = albedo_index,
                    .normals_index  = normals_index,
                    .material_index = material_index,
                }
            );
        }
    }

    return meshes;
}

void populate_materials(
    const std::unordered_map<uint32_t, Image>& texture_cache,
    VkDescriptorSet                            descriptor_set,
    VkSampler                                  sampler,
    VkDevice                                   device
) {
    for (auto& [slot, image] : texture_cache) {
        VkDescriptorImageInfo image_write_info = {
            .sampler = sampler, .imageView = image.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };

        VkWriteDescriptorSet write_set = {
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = nullptr,
            .dstSet           = descriptor_set,
            .dstBinding       = 1,
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

    bool use_meshlets      = true;
    bool enable_validation = true;

    spdlog::info("Creating Vulkan instance");
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VkInstance               instance        = create_instance(enable_validation, debug_messenger);

    spdlog::info("Picking physical device");
    VkPhysicalDevice physical_device       = pick_physical_device(instance);
    uint32_t         graphics_family_index = get_graphics_family_index(physical_device);

    spdlog::info("Queue index that supports graphics operations: {}", graphics_family_index);

    spdlog::info("Creating device");
    VkDevice device = create_device(instance, physical_device, graphics_family_index, enable_validation, use_meshlets);
    volkLoadDevice(device);

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

    Swapchain swapchain = create_swapchain(window, instance, device, physical_device);

    VkQueue graphics_queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, graphics_family_index, 0, &graphics_queue);

    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0
    };

    VkSemaphore image_available_semaphore = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available_semaphore));

    VkSemaphore render_finished_semaphore = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished_semaphore));

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
        .commandBufferCount = 1
    };

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device, &command_buffer_info, &command_buffer));

    std::vector<VkDescriptorPoolSize> descriptor_pool_sizes = {
        {
            .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 10000,
        },
        {
            .type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 100,
        },
        {
            .type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 100,
        },
        {
            .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 100,
        },
    };

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

    VkShaderModule mesh_module =
        use_meshlets ? shader_module_from_file(device, "data/shaders/meshlet.mesh.spv") : VK_NULL_HANDLE;
    VkShaderModule task_module =
        use_meshlets ? shader_module_from_file(device, "data/shaders/meshlet.task.spv") : VK_NULL_HANDLE;

    VkShaderModule vertex_module =
        use_meshlets ? VK_NULL_HANDLE : shader_module_from_file(device, "data/shaders/bindless.vert.spv");

    VkShaderModule fragment_module = shader_module_from_file(device, "data/shaders/bindless.frag.spv");

    VkShaderModule light_compute_module     = shader_module_from_file(device, "data/shaders/light.comp.spv");
    VkShaderModule composite_compute_module = shader_module_from_file(device, "data/shaders/composite.comp.spv");

    VkDescriptorSetLayout draw_data_descriptor_layout = create_descriptor_set_layout(
        device,
        {DescriptorLayoutBinding{
            .binding     = 0,
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .count       = 1,
            .stage_flags = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_VERTEX_BIT,
            .bindless    = false
        }}
    );

    VkDescriptorSetLayout scene_ubo_texture_descriptor_layout = create_descriptor_set_layout(
        device,
        {{
             .binding     = 0,
             .type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
             .count       = 1,
             .stage_flags = VK_SHADER_STAGE_ALL,
             .bindless    = false,
         },
         {
             .binding     = 1,
             .type        = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             .count       = 10000,
             .stage_flags = VK_SHADER_STAGE_ALL,
             .bindless    = true,
         }}
    );

    VkShaderStageFlags geometry_pipeline_stage_flags =
        use_meshlets ? VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT : VK_SHADER_STAGE_VERTEX_BIT;
    VkPipelineLayout geometry_pipeline_layout = create_pipeline_layout(
        device,
        {draw_data_descriptor_layout, scene_ubo_texture_descriptor_layout},
        geometry_pipeline_stage_flags,
        sizeof(PushConstants)
    );

    std::vector<Shader> geometry_pipeline_shaders;
    if (use_meshlets) {
        geometry_pipeline_shaders.push_back({
            .module = task_module,
            .stage  = VK_SHADER_STAGE_TASK_BIT_EXT,
        });

        geometry_pipeline_shaders.push_back({
            .module = mesh_module,
            .stage  = VK_SHADER_STAGE_MESH_BIT_EXT,
        });
    } else {
        geometry_pipeline_shaders.push_back({
            .module = vertex_module,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
        });
    }
    geometry_pipeline_shaders.push_back({
        .module = fragment_module,
        .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
    });

    VkPipeline geometry_pipeline = create_graphics_pipeline(
        device,
        geometry_pipeline_layout,
        geometry_pipeline_shaders,
        {
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_R8G8B8A8_UNORM,
        },
        VK_FORMAT_D32_SFLOAT
    );

    VkDescriptorSetLayout compute_descriptor_layout = create_descriptor_set_layout(
        device,
        {
            {
                .binding     = 0,
                .type        = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .count       = 1,
                .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                .bindless    = false,
            },
        }
    );

    VkPipelineLayout compute_pipeline_layout = create_pipeline_layout(
        device,
        {{compute_descriptor_layout, scene_ubo_texture_descriptor_layout}},
        VK_SHADER_STAGE_COMPUTE_BIT,
        sizeof(PostProcesPushConstants)
    );

    VkPipeline lightpass_pipeline = create_compute_pipeline(
        device,
        compute_pipeline_layout,
        {
            .module = light_compute_module,
            .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
        }
    );

    VkDescriptorSetLayout composite_descriptor_layout = create_descriptor_set_layout(
        device,
        {
            {
                .binding     = 0,
                .type        = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .count       = 1,
                .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
                .bindless    = false,
            },
        }
    );

    VkPipelineLayout composite_pipeline_layout = create_pipeline_layout(
        device,
        {composite_descriptor_layout, scene_ubo_texture_descriptor_layout},
        VK_SHADER_STAGE_COMPUTE_BIT,
        sizeof(PostProcesPushConstants)
    );

    VkPipeline composite_pipeline = create_compute_pipeline(
        device,
        composite_pipeline_layout,
        {
            .module = composite_compute_module,
            .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
        }
    );

    VkDescriptorPoolSize imgui_pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE}
    };

    uint32_t max_imgui_sets = 0;
    for (VkDescriptorPoolSize& size : imgui_pool_sizes) {
        max_imgui_sets += size.descriptorCount;
    }

    VkDescriptorPoolCreateInfo imgui_pool_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets       = max_imgui_sets,
        .poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(imgui_pool_sizes)),
        .pPoolSizes    = imgui_pool_sizes

    };

    VkDescriptorPool imgui_descriptor_pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(device, &imgui_pool_info, nullptr, &imgui_descriptor_pool));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsLight();

    ImGuiStyle& style          = ImGui::GetStyle();
    io.ConfigDpiScaleFonts     = true;
    io.ConfigDpiScaleViewports = true;

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding              = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    VkPipelineRenderingCreateInfo imgui_rendering_info = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext                   = nullptr,
        .viewMask                = 0,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &swapchain.format,
        .depthAttachmentFormat   = VK_FORMAT_UNDEFINED,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };

    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo init_info   = {};
    init_info.Instance                    = instance;
    init_info.PhysicalDevice              = physical_device;
    init_info.Device                      = device;
    init_info.QueueFamily                 = graphics_family_index;
    init_info.Queue                       = graphics_queue;
    init_info.PipelineCache               = nullptr;
    init_info.DescriptorPool              = imgui_descriptor_pool;
    init_info.MinImageCount               = 2;
    init_info.ImageCount                  = static_cast<uint32_t>(swapchain.images.size());
    init_info.Subpass                     = 0;
    init_info.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator                   = nullptr;
    init_info.UseDynamicRendering         = true;
    init_info.RenderPass                  = VK_NULL_HANDLE;
    init_info.PipelineRenderingCreateInfo = imgui_rendering_info;
    init_info.CheckVkResultFn             = nullptr;
    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, imgui_load_function, instance);
    ImGui_ImplVulkan_Init(&init_info);

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
        .maxLod                  = 0.0f,
        .borderColor             = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE
    };

    VkSampler linear_sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(device, &sampler_info, nullptr, &linear_sampler));

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
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        vma_allocator,
        device
    );

    Image gbuffer_albedo = create_image(
        VK_FORMAT_R8G8B8A8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        vma_allocator,
        device
    );

    Image gbuffer_normals = create_image(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        vma_allocator,
        device
    );

    Image gbuffer_material = create_image(
        VK_FORMAT_R8G8B8A8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        vma_allocator,
        device
    );

    Image lightpass_output = create_image(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        vma_allocator,
        device
    );

    Image composite_output = create_image(
        VK_FORMAT_R8G8B8A8_UNORM,
        swapchain.width,
        swapchain.height,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
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
        VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

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
        1024 * 1024 * 128, // 128MB
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        vma_allocator
    );

    VkBufferDeviceAddressInfo address_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = global_vertex_buffer.handle,
    };
    VkDeviceAddress global_vertex_buffer_address = vkGetBufferDeviceAddress(device, &address_info);

    Buffer global_index_buffer = create_buffer(
        1024 * 1024 * 64, // 64MB
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        vma_allocator
    );

    address_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = global_index_buffer.handle,
    };
    VkDeviceAddress global_index_buffer_address = vkGetBufferDeviceAddress(device, &address_info);

    Buffer indirect_command_buffer = create_buffer(
        1024 * 1024 * 16, // 16MB
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    std::vector<VkDrawIndexedIndirectCommand> indirect_draw_commands;

    Buffer draw_data_buffer = create_buffer(
        1024 * 1024 * 64, // 64MB
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    std::vector<DrawData> bindless_draw_data_cpu_buffer;

    Buffer meshlet_buffer = create_buffer(
        1024 * 1024 * 64, // 32MB
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        vma_allocator
    );
    address_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = meshlet_buffer.handle,
    };
    VkDeviceAddress meshlet_buffer_address = vkGetBufferDeviceAddress(device, &address_info);

    std::vector<VkDrawMeshTasksIndirectCommandEXT> meshlet_indirect_draw_commands;

    Buffer meshlet_vertex_indices_buffer = create_buffer(
        1024 * 1024 * 64, // 32MB
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        vma_allocator
    );
    address_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = meshlet_vertex_indices_buffer.handle,
    };
    VkDeviceAddress meshlet_vertex_buffer_indices_address = vkGetBufferDeviceAddress(device, &address_info);

    Buffer meshlet_primitive_indices_buffer = create_buffer(
        1024 * 1024 * 64, // 32MB
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        vma_allocator
    );
    address_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = meshlet_primitive_indices_buffer.handle,
    };
    VkDeviceAddress meshlet_primitive_indices_buffer_address = vkGetBufferDeviceAddress(device, &address_info);

    Buffer meshlet_bounds_buffer = create_buffer(
        1024 * 1024 * 64, // 32MB
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        vma_allocator
    );
    address_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = meshlet_bounds_buffer.handle,
    };
    VkDeviceAddress meshlet_bounds_buffer_address = vkGetBufferDeviceAddress(device, &address_info);

    Buffer scene_ubo_buffer = create_buffer(
        sizeof(SceneUBO),
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

    VkDescriptorSetAllocateInfo uniform_buffer_descriptor_set_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = nullptr,
        .descriptorPool     = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &draw_data_descriptor_layout
    };

    VkDescriptorSet uniform_buffer_descriptor_set = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateDescriptorSets(device, &uniform_buffer_descriptor_set_info, &uniform_buffer_descriptor_set));

    uint32_t bindless_texture_count = 10000;

    VkDescriptorSetVariableDescriptorCountAllocateInfo variable_count_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts  = &bindless_texture_count,
    };

    VkDescriptorSetAllocateInfo scene_ubo_texture_descriptor_set_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = &variable_count_info,
        .descriptorPool     = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &scene_ubo_texture_descriptor_layout
    };

    VkDescriptorSet scene_ubo_textures_descriptor_set = VK_NULL_HANDLE;
    VK_CHECK(
        vkAllocateDescriptorSets(device, &scene_ubo_texture_descriptor_set_info, &scene_ubo_textures_descriptor_set)
    );

    VkDescriptorBufferInfo scene_ubo_set_info = {
        .buffer = scene_ubo_buffer.handle,
        .offset = 0,
        .range  = scene_ubo_buffer.size,
    };

    VkWriteDescriptorSet scene_ubo_write_set = {
        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext            = nullptr,
        .dstSet           = scene_ubo_textures_descriptor_set,
        .dstBinding       = 0,
        .dstArrayElement  = 0,
        .descriptorCount  = 1,
        .descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pImageInfo       = nullptr,
        .pBufferInfo      = &scene_ubo_set_info,
        .pTexelBufferView = nullptr
    };
    vkUpdateDescriptorSets(device, 1, &scene_ubo_write_set, 0, nullptr);

    VkDescriptorBufferInfo draw_data_descriptor_buffer_set_info = {
        .buffer = draw_data_buffer.handle,
        .offset = 0,
        .range  = draw_data_buffer.size,
    };

    VkWriteDescriptorSet draw_data_buffer_write_set = {
        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext            = nullptr,
        .dstSet           = uniform_buffer_descriptor_set,
        .dstBinding       = 0,
        .dstArrayElement  = 0,
        .descriptorCount  = 1,
        .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pImageInfo       = nullptr,
        .pBufferInfo      = &draw_data_descriptor_buffer_set_info,
        .pTexelBufferView = nullptr
    };
    vkUpdateDescriptorSets(device, 1, &draw_data_buffer_write_set, 0, nullptr);

    VkDescriptorSetAllocateInfo compute_descriptor_set_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = nullptr,
        .descriptorPool     = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &compute_descriptor_layout
    };

    VkDescriptorSet compute_descriptor_set = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateDescriptorSets(device, &compute_descriptor_set_info, &compute_descriptor_set));

    VkDescriptorImageInfo compute_image_info = {
        .sampler     = VK_NULL_HANDLE,
        .imageView   = lightpass_output.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    VkWriteDescriptorSet compute_write_set = {
        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext            = nullptr,
        .dstSet           = compute_descriptor_set,
        .dstBinding       = 0,
        .dstArrayElement  = 0,
        .descriptorCount  = 1,
        .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo       = &compute_image_info,
        .pBufferInfo      = nullptr,
        .pTexelBufferView = nullptr
    };
    vkUpdateDescriptorSets(device, 1, &compute_write_set, 0, nullptr);

    VkDescriptorSetAllocateInfo composite_descriptor_set_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = nullptr,
        .descriptorPool     = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &composite_descriptor_layout
    };

    VkDescriptorSet composite_descriptor_set = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateDescriptorSets(device, &composite_descriptor_set_info, &composite_descriptor_set));

    VkDescriptorImageInfo composite_image_info = {
        .sampler     = VK_NULL_HANDLE,
        .imageView   = composite_output.view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    VkWriteDescriptorSet composite_write_set = {
        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext            = nullptr,
        .dstSet           = composite_descriptor_set,
        .dstBinding       = 0,
        .dstArrayElement  = 0,
        .descriptorCount  = 1,
        .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo       = &composite_image_info,
        .pBufferInfo      = nullptr,
        .pTexelBufferView = nullptr
    };
    vkUpdateDescriptorSets(device, 1, &composite_write_set, 0, nullptr);

    std::unordered_map<uint32_t, Image> texture_cache;
    std::vector<Image>                  loaded_images;

    auto lantern = load_model(
        "data/models/lion/lion.gltf",
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
        command_buffer,
        graphics_queue,
        device
    );
    for (auto& m : lantern) {
        m.position = glm::vec3(10, -4, 13);
        m.scale    = 10;
    }

    auto helmet = load_model(
        "data/models/horse/horse.gltf",
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
        command_buffer,
        graphics_queue,
        device
    );
    for (auto& m : helmet) {
        m.position = glm::vec3(10, -4, 13);
        m.scale    = 80;
    }

    std::vector<Mesh> meshes;
    meshes.reserve(lantern.size() + helmet.size());
    meshes.insert(meshes.end(), lantern.begin(), lantern.end());
    meshes.insert(meshes.end(), helmet.begin(), helmet.end());

    // Stress test
    {
        int row = 0;
        int col = 0;
        int t   = 0;

        float spacing = 20.0f;

        int grid_break = 10;

        for (int i = 0; i < 300; i++) {
            for (auto mesh : helmet) {
                auto clone     = mesh;
                clone.position = glm::vec3(row * spacing * 0.6, t * spacing, col * spacing * 0.5);
                meshes.push_back(clone);
            }

            row++;

            if (row >= grid_break) {
                row = 0;
                col++;
            }

            if (col >= grid_break) {
                t++;
                col = 0;
            }
        }
    }

    uint32_t depth_index = texture_cache.size();
    texture_cache.insert({depth_index, depth_buffer});

    uint32_t albedo_index = texture_cache.size();
    texture_cache.insert({albedo_index, gbuffer_albedo});

    uint32_t normals_index = texture_cache.size();
    texture_cache.insert({normals_index, gbuffer_normals});

    uint32_t material_index = texture_cache.size();
    texture_cache.insert({material_index, gbuffer_material});

    uint32_t lightpass_output_index = texture_cache.size();
    texture_cache.insert({lightpass_output_index, lightpass_output});

    populate_materials(texture_cache, scene_ubo_textures_descriptor_set, linear_sampler, device);

    Camera camera = {
        .near_plane      = 0.1f,
        .far_plane       = 100.0f,
        .viewport_width  = static_cast<float>(swapchain.width),
        .viewport_height = static_cast<float>(swapchain.height),
        .fov             = 90.0f
    };
    camera.position  = {10, 20, 20};
    camera.direction = {0, 0, -1};

    float base_camera_speed        = 10.0;
    float camera_mouse_sensitivity = 0.002f;
    float camera_speed_mod         = 2.5f;
    float camera_speed             = base_camera_speed;

    bool      capturing_mouse = false;
    glm::vec2 last_mouse_pos  = {};

    bool pressed_keys[512] = {0};

    float delta_time = 0.0f;

    float    time_passed = 0.0f;
    uint32_t fps         = 0;

    glm::mat4     frozen_view_proj;
    glm::vec4     frozen_camera_position;
    FrustumPlanes frozen_planes;

    bool debug_frustum   = false;
    bool disable_culling = false;

    bool running = true;
    while (running) {
        auto frame_start_time = std::chrono::high_resolution_clock::now();

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
                    glm::vec2 mouse_delta = {-xrel * camera_mouse_sensitivity, -yrel * camera_mouse_sensitivity};

                    last_mouse_pos = {xrel, yrel};

                    glm::mat4 rotation_mat(1.0f);
                    rotation_mat     = glm::rotate(rotation_mat, mouse_delta.x, camera.up);
                    camera.direction = glm::vec3(rotation_mat * glm::vec4(camera.direction, 1.0));

                    glm::vec3 temp(camera.direction);
                    temp             = glm::cross(temp, camera.up);
                    temp             = glm::normalize(temp);
                    rotation_mat     = glm::mat4(1.0f);
                    rotation_mat     = glm::rotate(rotation_mat, mouse_delta.y, temp);
                    camera.direction = glm::vec3(rotation_mat * glm::vec4(camera.direction, 1.0));
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
            move_camera(camera, camera.direction, camera_speed * delta_time);
        }

        if (pressed_keys[SDL_SCANCODE_S]) {
            move_camera(camera, camera.direction, -camera_speed * delta_time);
        }

        if (pressed_keys[SDL_SCANCODE_A]) {
            move_camera(camera, glm::cross(camera.direction, camera.up), -camera_speed * delta_time);
        }

        if (pressed_keys[SDL_SCANCODE_D]) {
            move_camera(camera, glm::cross(camera.direction, camera.up), camera_speed * delta_time);
        }

        if (pressed_keys[SDL_SCANCODE_Q]) {
            move_camera(camera, camera.up, camera_speed * delta_time);
        }

        if (pressed_keys[SDL_SCANCODE_E]) {
            move_camera(camera, camera.up, -camera_speed * delta_time);
        }

        update_camera(camera);

        SceneUBO scene_ubo         = {};
        scene_ubo.view_proj        = camera.combined_matrix;
        scene_ubo.frozen_view_proj = frozen_view_proj;

        scene_ubo.camera_position        = glm::vec4(camera.position, 1.0);
        scene_ubo.frozen_camera_position = frozen_camera_position;

        scene_ubo.planes[0] = camera.planes.left;
        scene_ubo.planes[1] = camera.planes.right;
        scene_ubo.planes[2] = camera.planes.bottom;
        scene_ubo.planes[3] = camera.planes.top;
        scene_ubo.planes[4] = camera.planes.near;
        scene_ubo.planes[5] = camera.planes.far;

        scene_ubo.frozen_planes[0] = frozen_planes.left;
        scene_ubo.frozen_planes[1] = frozen_planes.right;
        scene_ubo.frozen_planes[2] = frozen_planes.bottom;
        scene_ubo.frozen_planes[3] = frozen_planes.top;
        scene_ubo.frozen_planes[4] = frozen_planes.near;
        scene_ubo.frozen_planes[5] = frozen_planes.far;

        scene_ubo.debug_frustum   = debug_frustum;
        scene_ubo.disable_culling = disable_culling;

        void* scene_ubo_ptr = nullptr;
        VK_CHECK(vmaMapMemory(vma_allocator, scene_ubo_buffer.allocation, &scene_ubo_ptr));
        memcpy(scene_ubo_ptr, &scene_ubo, sizeof(SceneUBO));

        indirect_draw_commands.clear();
        meshlet_indirect_draw_commands.clear();

        bindless_draw_data_cpu_buffer.clear();

        for (auto& mesh : meshes) {
            uint32_t first_index   = static_cast<uint32_t>(mesh.index_buffer_offset / sizeof(uint32_t));
            int32_t  vertex_offset = static_cast<int32_t>(mesh.vertex_buffer_offset / sizeof(Vertex));

            uint32_t group_count = (mesh.meshlet_count + 31) / 32;

            if (use_meshlets) {
                meshlet_indirect_draw_commands.push_back({
                    .groupCountX = group_count,
                    .groupCountY = 1,
                    .groupCountZ = 1,
                });
            } else {
                indirect_draw_commands.push_back({
                    .indexCount    = mesh.index_count,
                    .instanceCount = 1,
                    .firstIndex    = first_index,
                    .vertexOffset  = vertex_offset,
                    .firstInstance = 0,
                });
            }

            glm::mat4 model = glm::translate(glm::mat4(1.0f), mesh.position);
            model           = glm::scale(model, glm::vec3(mesh.scale));
            bindless_draw_data_cpu_buffer.push_back(
                DrawData{
                    .model_matrix   = model,
                    .first_index    = first_index,
                    .vertex_offset  = vertex_offset,
                    .meshlet_offset = mesh.meshlet_offset,
                    .meshlet_count  = mesh.meshlet_count,
                    .albedo_index   = mesh.material.albedo_index,
                    .normals_index  = mesh.material.normals_index,
                    .material_index = mesh.material.material_index,
                }
            );
        }

        void* bindless_command_ptr = nullptr;
        if (use_meshlets) {
            VK_CHECK(vmaMapMemory(vma_allocator, indirect_command_buffer.allocation, &bindless_command_ptr));
            memcpy(
                bindless_command_ptr,
                meshlet_indirect_draw_commands.data(),
                sizeof(VkDrawMeshTasksIndirectCommandEXT) * meshlet_indirect_draw_commands.size()
            );
        } else {
            VK_CHECK(vmaMapMemory(vma_allocator, indirect_command_buffer.allocation, &bindless_command_ptr));
            memcpy(
                bindless_command_ptr,
                indirect_draw_commands.data(),
                sizeof(VkDrawIndexedIndirectCommand) * indirect_draw_commands.size()
            );
        }

        void* bindless_uniform_ptr = nullptr;
        VK_CHECK(vmaMapMemory(vma_allocator, draw_data_buffer.allocation, &bindless_uniform_ptr));
        memcpy(
            bindless_uniform_ptr,
            bindless_draw_data_cpu_buffer.data(),
            sizeof(DrawData) * bindless_draw_data_cpu_buffer.size()
        );

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Debug");
        ImGui::Text("Rendering path: %s", use_meshlets ? "Meshlets" : "Indirect");
        ImGui::Separator();

        if (ImGui::Checkbox("Freeze frustum", &debug_frustum)) {
            frozen_camera_position = glm::vec4(camera.position, 1.0);
            frozen_planes          = camera.planes;
            frozen_view_proj       = camera.combined_matrix;
        }
        ImGui::Checkbox("Disable culling", &disable_culling);
        ImGui::End();

        ImGui::Render();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();

        uint32_t image_index = 0;
        vkAcquireNextImageKHR(
            device, swapchain.handle, UINT64_MAX, image_available_semaphore, VK_NULL_HANDLE, &image_index
        );

        vmaSetCurrentFrameIndex(vma_allocator, image_index);

        VkCommandBufferBeginInfo begin_info = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .pInheritanceInfo = nullptr
        };
        vkBeginCommandBuffer(command_buffer, &begin_info);

        image_pipeline_barrier(
            swapchain.images[image_index],
            VK_IMAGE_ASPECT_COLOR_BIT,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT
        );

        VkViewport viewport = {
            .x        = 0,
            .y        = 0,
            .width    = static_cast<float>(swapchain.width),
            .height   = static_cast<float>(swapchain.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };

        VkRect2D scissor = {
            .offset = {.x = 0, .y = 0},
            .extent = {static_cast<uint32_t>(viewport.width), static_cast<uint32_t>(viewport.height)}
        };

        vkCmdSetViewport(command_buffer, 0, 1, &viewport);
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);

        std::vector<VkRenderingAttachmentInfo> gbuffer_color_attachments{
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
            .clearValue         = {.depthStencil = {.depth = 1.0f, .stencil = 0}}
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
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometry_pipeline);

        VkDescriptorSet sets[] = {uniform_buffer_descriptor_set, scene_ubo_textures_descriptor_set};
        vkCmdBindDescriptorSets(
            command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometry_pipeline_layout, 0, 2, sets, 0, nullptr
        );

        PushConstants push;
        push.vertex_buffer_address                    = global_vertex_buffer_address;
        push.index_buffer_address                     = global_index_buffer_address;
        push.meshlet_buffer_address                   = meshlet_buffer_address;
        push.meshlet_vertex_buffer_indices_address    = meshlet_vertex_buffer_indices_address;
        push.meshlet_primitive_indices_buffer_address = meshlet_primitive_indices_buffer_address;
        push.meshlet_bounds_buffer_address            = meshlet_bounds_buffer_address;

        vkCmdPushConstants(
            command_buffer, geometry_pipeline_layout, geometry_pipeline_stage_flags, 0, sizeof(PushConstants), &push
        );

        if (use_meshlets) {
            vkCmdDrawMeshTasksIndirectEXT(
                command_buffer,
                indirect_command_buffer.handle,
                0,
                meshlet_indirect_draw_commands.size(),
                sizeof(VkDrawMeshTasksIndirectCommandEXT)
            );
        } else {
            vkCmdBindIndexBuffer(command_buffer, global_index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexedIndirect(
                command_buffer,
                indirect_command_buffer.handle,
                0,
                indirect_draw_commands.size(),
                sizeof(VkDrawIndexedIndirectCommand)
            );
        }

        vkCmdEndRendering(command_buffer);

        for (auto image : gbuffer_images) {
            image_pipeline_barrier(
                image.get(),
                command_buffer,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            );
        }

        image_pipeline_barrier(
            depth_buffer,
            command_buffer,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
        );

        VkDescriptorSet compute_sets[] = {compute_descriptor_set, scene_ubo_textures_descriptor_set};
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, lightpass_pipeline);
        vkCmdBindDescriptorSets(
            command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_layout, 0, 2, compute_sets, 0, nullptr
        );

        PostProcesPushConstants post_process_push = {
            .combined        = camera.combined_matrix,
            .inv_combined    = glm::inverse(camera.combined_matrix),
            .camera_position = glm::vec4(camera.position, 1.0),
            .depth_index     = depth_index,
            .albedo_index    = albedo_index,
            .normals_index   = normals_index,
            .material_index  = material_index,
            .lightpass_index = lightpass_output_index
        };

        vkCmdPushConstants(
            command_buffer,
            compute_pipeline_layout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(PostProcesPushConstants),
            &post_process_push
        );

        vkCmdDispatch(command_buffer, (swapchain.width + 7) / 8, (swapchain.height + 7) / 8, 1);

        image_pipeline_barrier(
            lightpass_output,
            command_buffer,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
        );

        VkDescriptorSet composite_sets[] = {composite_descriptor_set, scene_ubo_textures_descriptor_set};
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, composite_pipeline);
        vkCmdBindDescriptorSets(
            command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, composite_pipeline_layout, 0, 2, composite_sets, 0, nullptr
        );
        vkCmdPushConstants(
            command_buffer,
            composite_pipeline_layout,
            VK_SHADER_STAGE_COMPUTE_BIT,
            0,
            sizeof(PostProcesPushConstants),
            &post_process_push
        );

        vkCmdDispatch(command_buffer, (swapchain.width + 7) / 8, (swapchain.height + 7) / 8, 1);

        for (auto image : gbuffer_images) {
            image_pipeline_barrier(
                image.get(),
                command_buffer,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
            );
        }

        image_pipeline_barrier(
            depth_buffer,
            command_buffer,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
        );

        image_pipeline_barrier(
            lightpass_output,
            command_buffer,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
        );

        image_pipeline_barrier(
            composite_output,
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
            composite_output.handle,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            swapchain.images[image_index],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit_region,
            VK_FILTER_LINEAR
        );

        image_pipeline_barrier(
            composite_output,
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
            VK_IMAGE_ASPECT_COLOR_BIT,
            command_buffer,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
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
            VK_IMAGE_ASPECT_COLOR_BIT,
            command_buffer,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0
        );
        VK_CHECK(vkEndCommandBuffer(command_buffer));

        VkPipelineStageFlags wait_stage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo         submit_info = {
                    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                    .pNext                = nullptr,
                    .waitSemaphoreCount   = 1,
                    .pWaitSemaphores      = &image_available_semaphore,
                    .pWaitDstStageMask    = &wait_stage,
                    .commandBufferCount   = 1,
                    .pCommandBuffers      = &command_buffer,
                    .signalSemaphoreCount = 1,
                    .pSignalSemaphores    = &render_finished_semaphore
        };
        VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

        VkPresentInfoKHR present_info = {
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext              = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &render_finished_semaphore,
            .swapchainCount     = 1,
            .pSwapchains        = &swapchain.handle,
            .pImageIndices      = &image_index,
            .pResults           = nullptr
        };
        VK_CHECK(vkQueuePresentKHR(graphics_queue, &present_info));

        vkDeviceWaitIdle(device);

        auto time_diff =
            std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - frame_start_time)
                .count();
        delta_time = time_diff * 0.001f;
        fps++;

        time_passed += delta_time;
        if (time_passed >= 1.0f) {
            SDL_SetWindowTitle(window, std::to_string(fps).c_str());

            fps         = 0;
            time_passed = 0.0f;
        }
    }

    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplVulkan_Shutdown();

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
    for (auto image : loaded_images) {
        destroy_image(image, device, vma_allocator);
    }

    vmaDestroyAllocator(vma_allocator);

    vkDestroyDescriptorSetLayout(device, scene_ubo_texture_descriptor_layout, nullptr);
    vkDestroyDescriptorSetLayout(device, draw_data_descriptor_layout, nullptr);
    vkDestroyDescriptorSetLayout(device, compute_descriptor_layout, nullptr);
    vkDestroyDescriptorSetLayout(device, composite_descriptor_layout, nullptr);
    vkDestroyPipelineLayout(device, compute_pipeline_layout, nullptr);
    vkDestroyPipelineLayout(device, composite_pipeline_layout, nullptr);
    vkDestroyPipeline(device, lightpass_pipeline, nullptr);
    vkDestroyPipeline(device, composite_pipeline, nullptr);
    vkDestroySampler(device, linear_sampler, nullptr);
    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    vkDestroyDescriptorPool(device, imgui_descriptor_pool, nullptr);
    if (mesh_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, mesh_module, nullptr);
    }
    if (task_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, task_module, nullptr);
    }
    vkDestroyShaderModule(device, fragment_module, nullptr);
    vkDestroyShaderModule(device, composite_compute_module, nullptr);
    vkDestroyShaderModule(device, light_compute_module, nullptr);
    if (vertex_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, vertex_module, nullptr);
    }
    vkDestroyPipelineLayout(device, geometry_pipeline_layout, nullptr);
    vkDestroyPipeline(device, geometry_pipeline, nullptr);
    vkDestroyCommandPool(device, command_pool, nullptr);
    vkDestroySemaphore(device, image_available_semaphore, nullptr);
    vkDestroySemaphore(device, render_finished_semaphore, nullptr);
    if (debug_messenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
    }
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
