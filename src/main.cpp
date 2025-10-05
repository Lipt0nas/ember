#include <volk.h>

#include <tiny_gltf.h>
#include <vk_mem_alloc.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <spdlog/spdlog.h>

#include <meshoptimizer.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <vector>

#define VK_CHECK(call)                                                                                                 \
    do {                                                                                                               \
        VkResult result_ = call;                                                                                       \
        assert(result_ == VK_SUCCESS);                                                                                 \
    } while (0)

struct MeshletBounds {
    glm::vec3 center;
    float     radius;
};

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;

    bool operator==(const Vertex& other) const {
        return position == other.position && normal == other.normal && uv == other.uv;
    }
};

struct PushConstants {
    glm::mat4    combined;
    VkDeviceSize vertex_buffer_address;
    VkDeviceSize index_buffer_address;
    VkDeviceSize meshlet_buffer_address;
    VkDeviceSize meshlet_vertex_buffer_indices_address;
    VkDeviceSize meshlet_primitive_indices_buffer_address;
    VkDeviceSize meshlet_bounds_buffer_address;
};

struct DrawData {
    glm::mat4 model_matrix;

    uint32_t albedo_index;

    // Vertex pulling
    uint32_t first_index;
    int32_t  vertex_offset;

    // Meshlets
    uint32_t meshlet_offset;
    uint32_t meshlet_count;

    float _pad[3];
};

struct Buffer {
    VkBuffer      handle;
    VkDeviceSize  size;
    VmaAllocation allocation;
};

struct Image {
    VkExtent2D extent;
    VkFormat   format;

    VkImage       handle;
    VkImageView   view;
    VmaAllocation allocation;
};

struct Material {
    uint32_t albedo_index;
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

void image_pipeline_barrier(
    const Image&          image,
    VkImageAspectFlagBits aspect,
    VkCommandBuffer       command_buffer,
    VkImageLayout         old_layout,
    VkImageLayout         new_layout,
    VkPipelineStageFlags2 src_stage_mask,
    VkAccessFlags2        src_access_mask,
    VkPipelineStageFlags2 dst_stage_mask,
    VkAccessFlags2        dst_access_mask
) {
    VkImageMemoryBarrier2 barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = src_stage_mask,
        .srcAccessMask       = src_access_mask,
        .dstStageMask        = dst_stage_mask,
        .dstAccessMask       = dst_access_mask,
        .oldLayout           = old_layout,
        .newLayout           = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image.handle,
        .subresourceRange    = {
               .aspectMask     = aspect,
               .baseMipLevel   = 0,
               .levelCount     = 1,
               .baseArrayLayer = 0,
               .layerCount     = 1,
        }
    };

    VkDependencyInfo dependency = {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 0,
        .pMemoryBarriers          = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers    = nullptr,
        .imageMemoryBarrierCount  = 1,
        .pImageMemoryBarriers     = &barrier
    };
    vkCmdPipelineBarrier2(command_buffer, &dependency);
}

Image create_image(
    VkFormat              format,
    VkExtent2D            extent,
    VkImageUsageFlags     usage,
    VkImageAspectFlagBits aspect,
    VmaAllocator          allocator,
    VkDevice              device
) {
    VkImageCreateInfo image_info = {
        .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .imageType             = VK_IMAGE_TYPE_2D,
        .format                = format,
        .extent                = {.width = extent.width, .height = extent.height, .depth = 1},
        .mipLevels             = 1,
        .arrayLayers           = 1,
        .samples               = VK_SAMPLE_COUNT_1_BIT,
        .tiling                = VK_IMAGE_TILING_OPTIMAL,
        .usage                 = usage,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
        .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo allocation_info = {};
    allocation_info.usage                   = VMA_MEMORY_USAGE_AUTO;

    VkImage       handle = VK_NULL_HANDLE;
    VmaAllocation allocation;
    VK_CHECK(vmaCreateImage(allocator, &image_info, &allocation_info, &handle, &allocation, nullptr));

    VkImageViewCreateInfo view_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext    = nullptr,
        .flags    = 0,
        .image    = handle,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = format,
        .components =
            {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
             .g = VK_COMPONENT_SWIZZLE_IDENTITY,
             .b = VK_COMPONENT_SWIZZLE_IDENTITY,
             .a = VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {
            .aspectMask     = aspect,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        }
    };

    VkImageView view = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &view));

    return {
        .extent     = extent,
        .format     = format,
        .handle     = handle,
        .view       = view,
        .allocation = allocation,
    };
}

void destroy_image(const Image& image, VkDevice device, VmaAllocator allocator) {
    if (image.handle != VK_NULL_HANDLE) {
        vkDestroyImageView(device, image.view, nullptr);
        vmaDestroyImage(allocator, image.handle, image.allocation);
    }
}

Buffer create_buffer(
    VkDeviceSize             size,
    VkBufferUsageFlags       usage_flags,
    VmaAllocator             allocator,
    VmaAllocationCreateFlags allocation_flags = 0
) {
    VkBufferCreateInfo buffer_info = {
        .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .size                  = size,
        .usage                 = usage_flags,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr
    };

    VmaAllocationCreateInfo allocation_info = {};
    allocation_info.flags                   = allocation_flags;
    allocation_info.usage                   = VMA_MEMORY_USAGE_AUTO;

    VkBuffer      handle = VK_NULL_HANDLE;
    VmaAllocation allocation;
    VK_CHECK(vmaCreateBuffer(allocator, &buffer_info, &allocation_info, &handle, &allocation, nullptr));

    return {
        .handle     = handle,
        .size       = size,
        .allocation = allocation,
    };
}

void copy_buffer(
    const Buffer&   src_buffer,
    const Buffer&   dst_buffer,
    VkCommandBuffer command_buffer,
    VkQueue         queue,
    VkDevice        device,
    void*           data,
    size_t          data_size,
    VkDeviceSize    dst_buffer_offset = 0
) {
    if (data_size + dst_buffer_offset >= dst_buffer.size) {
        spdlog::error(
            "Attempted out of bounds buffer write, size={}, remaining space={}",
            data_size,
            dst_buffer.size - dst_buffer_offset
        );
        exit(1);
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .pNext = nullptr, .flags = 0, .pInheritanceInfo = nullptr
    };
    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

    VkBufferCopy copy_region = {
        .srcOffset = 0,
        .dstOffset = dst_buffer_offset,
        .size      = data_size,
    };
    vkCmdCopyBuffer(command_buffer, src_buffer.handle, dst_buffer.handle, 1, &copy_region);

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
}

void copy_image(
    const Buffer& src_buffer, const Image& dst_image, VkCommandBuffer command_buffer, VkQueue queue, VkDevice device
) {
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .pNext = nullptr, .flags = 0, .pInheritanceInfo = nullptr
    };
    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

    VkBufferImageCopy copy_region = {};
    copy_region.imageExtent       = {.width = dst_image.extent.width, .height = dst_image.extent.height, .depth = 1};
    copy_region.imageOffset       = {.x = 0, .y = 0, .z = 0};
    copy_region.imageSubresource  = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1
    };

    VkImageMemoryBarrier2 to_transfer_dst_barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .srcAccessMask       = 0,
        .dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = dst_image.handle,
        .subresourceRange    = {
               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
               .baseMipLevel   = 0,
               .levelCount     = 1,
               .baseArrayLayer = 0,
               .layerCount     = 1
        }
    };

    VkDependencyInfo to_dst_dependency = {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 0,
        .pMemoryBarriers          = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers    = nullptr,
        .imageMemoryBarrierCount  = 1,
        .pImageMemoryBarriers     = &to_transfer_dst_barrier
    };
    vkCmdPipelineBarrier2(command_buffer, &to_dst_dependency);

    vkCmdCopyBufferToImage(
        command_buffer, src_buffer.handle, dst_image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region
    );

    VkImageMemoryBarrier2 to_shader_read_barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext               = nullptr,
        .srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask        = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        .dstAccessMask       = 0,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = dst_image.handle,
        .subresourceRange    = {
               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
               .baseMipLevel   = 0,
               .levelCount     = 1,
               .baseArrayLayer = 0,
               .layerCount     = 1
        }
    };

    VkDependencyInfo shader_read_dependency = {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 0,
        .pMemoryBarriers          = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers    = nullptr,
        .imageMemoryBarrierCount  = 1,
        .pImageMemoryBarriers     = &to_shader_read_barrier
    };
    vkCmdPipelineBarrier2(command_buffer, &shader_read_dependency);

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
}

const std::vector<const char*> validation_layer_names = {"VK_LAYER_KHRONOS_validation"};

VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*                                       pUserData
) {
    spdlog::debug("VK validation: {}", pCallbackData->pMessage);

    return VK_FALSE;
}

PFN_vkVoidFunction imgui_load_function(const char* function_name, void* user_data) {
    return vkGetInstanceProcAddr((VkInstance)user_data, function_name);
}

std::tuple<VkPhysicalDeviceProperties, VkPhysicalDeviceFeatures>
get_physical_device_info(VkPhysicalDevice physical_device) {
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures   features;

    vkGetPhysicalDeviceProperties(physical_device, &properties);
    vkGetPhysicalDeviceFeatures(physical_device, &features);

    return {properties, features};
}

uint32_t find_queue_index(VkPhysicalDevice physical_device, VkQueueFlagBits queue_flags) {
    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_properties(queue_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_count, queue_properties.data());

    assert(queue_properties.size() > 0);

    uint32_t queue_index = UINT32_MAX;
    for (uint32_t i = 0; i < queue_properties.size(); i++) {
        const auto& queue = queue_properties[i];
        if (queue.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_index = i;
        }
    }

    assert(queue_index != UINT32_MAX);

    return queue_index;
}

std::tuple<VkSurfaceCapabilitiesKHR, VkSurfaceFormatKHR, VkPresentModeKHR, VkExtent2D>
get_swapchain_settings(SDL_Window* window, VkPhysicalDevice physical_device, VkSurfaceKHR surface) {
    VkSurfaceCapabilitiesKHR capabilities = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities);

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);

    std::vector<VkSurfaceFormatKHR> surface_formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, surface_formats.data());

    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, nullptr);

    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, present_modes.data());

    VkSurfaceFormatKHR surface_format = surface_formats[0];
    for (const auto& format : surface_formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
            surface_format = format;
            break;
        }
    }

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto& mode : present_modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode = mode;
            break;
        }
    }

    int width;
    int height;
    SDL_GetWindowSize(window, &width, &height);

    VkExtent2D extent = {.width = static_cast<uint32_t>(width), .height = static_cast<uint32_t>(height)};

    extent.width  = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return {capabilities, surface_format, present_mode, extent};
}

VkShaderModule shader_module_from_file(VkDevice device, const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::in | std::ios::ate);

    if (file.is_open()) {
        size_t                 length = static_cast<size_t>(file.tellg());
        std::vector<std::byte> buffer(length);

        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), length);

        VkShaderModuleCreateInfo module_info = {
            .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext    = nullptr,
            .flags    = 0,
            .codeSize = buffer.size(),
            .pCode    = reinterpret_cast<const uint32_t*>(buffer.data())
        };

        VkShaderModule module = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(device, &module_info, nullptr, &module));

        return module;
    }

    return VK_NULL_HANDLE;
}

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
        const tinygltf::Mesh& mesh = model.meshes[m];

        for (int p = 0; p < mesh.primitives.size(); p++) {
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

            uint32_t albedo_index = 0;
            if (primitive.material >= 0) {
                const tinygltf::Material& material = model.materials[primitive.material];

                if (material.pbrMetallicRoughness.baseColorTexture.index >= 0) {
                    int                texture_index = material.pbrMetallicRoughness.baseColorTexture.index;
                    tinygltf::Texture& texture       = model.textures[texture_index];
                    int                image_index   = texture.source;

                    if (image_index >= 0) {
                        auto local_it = local_texture_cache.find(image_index + local_cache_offset);
                        if (local_it != local_texture_cache.end()) {
                            albedo_index = local_it->second;
                        } else {
                            spdlog::info("Loading albedo texture");
                            tinygltf::Image& img = model.images[image_index];

                            Image image = create_image(
                                VK_FORMAT_R8G8B8A8_SRGB,
                                {
                                    .width  = static_cast<uint32_t>(img.width),
                                    .height = static_cast<uint32_t>(img.height),
                                },
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

                            albedo_index = index;
                        }
                    }
                } else {
                    spdlog::warn("Model {} primitive {} does not have a base color texture!", path.string(), p);
                }
            }

            spdlog::debug("Building meshlets");
            const size_t max_vertices  = 64;
            const size_t max_triangles = 124;
            const float  cone_weight   = 0.0f;

            size_t max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_vertices, max_triangles);
            spdlog::info("Max meshlets {}", max_meshlets);

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

                spdlog::info("bounds x={} y={} z={}", bounds.center[0], bounds.center[1], bounds.center[2]);

                meshlet_bounds[idx++] = MeshletBounds{
                    .center =
                        {
                            bounds.center[0],
                            bounds.center[1],
                            bounds.center[2],
                        },
                    .radius = bounds.radius,
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

            // spdlog::debug(
            //     "Mesh {}: meshlet_count={}, meshlet_offset={}, vertex_buf_size={}MB, index_buf_size={}MB",
            //     meshes.size(),
            //     meshlet_count,
            //     current_meshlet_offset,
            //     (vertices.size() * sizeof(Vertex)) / 1024.0f / 1024.f,
            //     (sizeof(uint32_t) * indices.size()) / 1024.0f / 1024.0f
            // );
            // spdlog::debug(
            //     "  global_vertex_indices_offset={}, global_primitive_indices_offset={}",
            //     global_vertex_indices_offset,
            //     global_primitive_indices_offset
            // );
            // spdlog::debug(
            //     "  First meshlet: v_off={}, t_off={}, v_cnt={}, t_cnt={}",
            //     meshlets[0].vertex_offset,
            //     meshlets[0].triangle_offset,
            //     meshlets[0].vertex_count,
            //     meshlets[0].triangle_count
            // );
            // if (meshlet_count > 1) {
            //     spdlog::debug(
            //         "  Last meshlet: v_off={}, t_off={}, v_cnt={}, t_cnt={}",
            //         meshlets[meshlet_count - 1].vertex_offset,
            //         meshlets[meshlet_count - 1].triangle_offset,
            //         meshlets[meshlet_count - 1].vertex_count,
            //         meshlets[meshlet_count - 1].triangle_count
            //     );
            // }

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
                    .albedo_index = albedo_index,
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
    spdlog::set_level(spdlog::level::trace);
    spdlog::info("Starting ember");

    VK_CHECK(volkInitialize());

    spdlog::info("Initializing SDL");
    if (!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO)) {
        spdlog::error("Failed to initialize SDL");
        return 1;
    }

    auto* window = SDL_CreateWindow("Ember", 1280, 720, SDL_WINDOW_VULKAN);
    if (!window) {
        spdlog::error("Failed to create SDL window");
        return 1;
    }

    uint32_t extension_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
    spdlog::debug("Instance supports {} extensions", extension_count);

    uint32_t sdl_extension_count = 0;
    auto     sdl_extensions      = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);

    spdlog::debug("SDL requires {} extensions", sdl_extension_count);

    std::vector<const char*> instance_extensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    for (uint32_t i = 0; i < sdl_extension_count; i++) {
        instance_extensions.push_back(sdl_extensions[i]);
    }

    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_info = {
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext           = nullptr,
        .flags           = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = vulkan_debug_callback,
        .pUserData       = nullptr
    };

    VkApplicationInfo app_info = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = nullptr,
        .pApplicationName   = "Ember",
        .applicationVersion = 1,
        .pEngineName        = "Ember",
        .engineVersion      = 1,
        .apiVersion         = VK_MAKE_API_VERSION(0, 1, 4, 0)
    };

    VkInstanceCreateInfo instance_info = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = &debug_messenger_info,
        .flags                   = 0,
        .pApplicationInfo        = &app_info,
        .enabledLayerCount       = static_cast<uint32_t>(validation_layer_names.size()),
        .ppEnabledLayerNames     = validation_layer_names.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.data()
    };

    VkInstance instance = VK_NULL_HANDLE;
    VK_CHECK(vkCreateInstance(&instance_info, nullptr, &instance));
    volkLoadInstance(instance);

    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &debug_messenger_info, nullptr, &debug_messenger));

    spdlog::info("Picking first available physical device");
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);

    std::vector<VkPhysicalDevice> physical_devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, physical_devices.data());

    assert(physical_devices.size() > 0);

    VkPhysicalDevice physical_device                           = physical_devices[0];
    auto [physical_device_properies, physical_device_features] = get_physical_device_info(physical_device);
    spdlog::info("Picked device {}", physical_device_properies.deviceName);

    uint32_t graphics_queue_index = find_queue_index(physical_device, VK_QUEUE_GRAPHICS_BIT);

    spdlog::info("Queue index that supports graphics operations: {}", graphics_queue_index);

    std::vector<const char*> device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_EXT_MESH_SHADER_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME
    };

    float                   queue_prorities   = 1.0f;
    VkDeviceQueueCreateInfo device_queue_info = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .queueFamilyIndex = graphics_queue_index,
        .queueCount       = 1,
        .pQueuePriorities = &queue_prorities
    };

    VkPhysicalDeviceFeatures features = {};
    features.samplerAnisotropy        = VK_TRUE;
    features.multiDrawIndirect        = VK_TRUE;

    VkPhysicalDeviceVulkan11Features vulkan_features_11{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = nullptr,
    };
    vulkan_features_11.shaderDrawParameters = VK_TRUE;

    VkPhysicalDeviceVulkan12Features vulkan_features_12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &vulkan_features_11,
    };
    vulkan_features_12.storageBuffer8BitAccess                      = VK_TRUE;
    vulkan_features_12.drawIndirectCount                            = VK_TRUE;
    vulkan_features_12.scalarBlockLayout                            = VK_TRUE;
    vulkan_features_12.bufferDeviceAddress                          = VK_TRUE;
    vulkan_features_12.descriptorIndexing                           = VK_TRUE;
    vulkan_features_12.runtimeDescriptorArray                       = VK_TRUE;
    vulkan_features_12.descriptorBindingPartiallyBound              = VK_TRUE;
    vulkan_features_12.descriptorBindingVariableDescriptorCount     = VK_TRUE;
    vulkan_features_12.shaderSampledImageArrayNonUniformIndexing    = VK_TRUE;
    vulkan_features_12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    vulkan_features_12.hostQueryReset                               = VK_TRUE;
    vulkan_features_12.uniformAndStorageBuffer8BitAccess            = VK_TRUE;

    VkPhysicalDeviceVulkan13Features enabled_features_13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &vulkan_features_12,
    };
    enabled_features_13.dynamicRendering = VK_TRUE;
    enabled_features_13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_features = {
        .sType                                  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
        .pNext                                  = &enabled_features_13,
        .taskShader                             = VK_TRUE,
        .meshShader                             = VK_TRUE,
        .multiviewMeshShader                    = VK_FALSE,
        .primitiveFragmentShadingRateMeshShader = VK_FALSE,
        .meshShaderQueries                      = VK_FALSE
    };

    VkDeviceCreateInfo device_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &mesh_shader_features,
        .flags                   = 0,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &device_queue_info,
        .enabledLayerCount       = static_cast<uint32_t>(validation_layer_names.size()),
        .ppEnabledLayerNames     = validation_layer_names.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
        .pEnabledFeatures        = &features
    };

    VkDevice device = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDevice(physical_device, &device_info, nullptr, &device));
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

    VkQueue graphics_queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, graphics_queue_index, 0, &graphics_queue);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        spdlog::error("Failed to create SDL window surface");
        return 1;
    }

    auto [swapchain_capabilities, swapchain_format, swapchain_mode, swapchain_extent] =
        get_swapchain_settings(window, physical_device, surface);

    uint32_t image_count = swapchain_capabilities.minImageCount + 1;
    if (swapchain_capabilities.maxImageCount > 0 && image_count > swapchain_capabilities.maxImageCount) {
        image_count = swapchain_capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchain_info = {
        .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext                 = nullptr,
        .flags                 = 0,
        .surface               = surface,
        .minImageCount         = image_count,
        .imageFormat           = swapchain_format.format,
        .imageColorSpace       = swapchain_format.colorSpace,
        .imageExtent           = swapchain_extent,
        .imageArrayLayers      = 1,
        .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = nullptr,
        .preTransform          = swapchain_capabilities.currentTransform,
        .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode           = swapchain_mode,
        .clipped               = VK_TRUE,
        .oldSwapchain          = VK_NULL_HANDLE
    };

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &swapchain));

    uint32_t swapchain_image_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, nullptr);

    std::vector<VkImage> swapchain_images(swapchain_image_count);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, swapchain_images.data());

    std::vector<VkImageView> swapchain_image_views(swapchain_images.size());
    for (int i = 0; i < swapchain_image_views.size(); i++) {
        VkImageViewCreateInfo image_view_info = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext    = nullptr,
            .flags    = 0,
            .image    = swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = swapchain_format.format,
            .components =
                {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
                 .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                 .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                 .a = VK_COMPONENT_SWIZZLE_IDENTITY},
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1
            }
        };

        VK_CHECK(vkCreateImageView(device, &image_view_info, nullptr, &swapchain_image_views[i]));
    }

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
        .queueFamilyIndex = graphics_queue_index
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
            .descriptorCount = 1000,
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

    VkPipelineRenderingCreateInfo pipeline_rendering_info = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext                   = nullptr,
        .viewMask                = 0,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &swapchain_format.format,
        .depthAttachmentFormat   = VK_FORMAT_D32_SFLOAT,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };

    VkShaderModule vertex_module   = shader_module_from_file(device, "data/shaders/bindless.vert.spv");
    VkShaderModule fragment_module = shader_module_from_file(device, "data/shaders/bindless.frag.spv");

    VkShaderModule mesh_module = shader_module_from_file(device, "data/shaders/meshlet.mesh.spv");
    VkShaderModule task_module = shader_module_from_file(device, "data/shaders/meshlet.task.spv");

    std::vector<VkPipelineShaderStageCreateInfo> shader_stage_infos = {
        {
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext               = nullptr,
            .flags               = 0,
            .stage               = VK_SHADER_STAGE_MESH_BIT_EXT,
            .module              = mesh_module,
            .pName               = "main",
            .pSpecializationInfo = nullptr,
        },
        // {
        //     .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        //     .pNext               = nullptr,
        //     .flags               = 0,
        //     .stage               = VK_SHADER_STAGE_TASK_BIT_EXT,
        //     .module              = task_module,
        //     .pName               = "main",
        //     .pSpecializationInfo = nullptr,
        // },
        {
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext               = nullptr,
            .flags               = 0,
            .stage               = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module              = fragment_module,
            .pName               = "main",
            .pSpecializationInfo = nullptr,
        }
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    VkViewport viewport = {
        .x        = 0,
        .y        = 0,
        .width    = static_cast<float>(swapchain_extent.width),
        .height   = static_cast<float>(swapchain_extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };

    VkRect2D scissor = {
        .offset = {.x = 0, .y = 0},
        .extent = {.width = swapchain_extent.width, .height = swapchain_extent.height},
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = 0,
        .viewportCount = 1,
        .pViewports    = &viewport,
        .scissorCount  = 1,
        .pScissors     = &scissor
    };

    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable   = VK_FALSE,
        .minSampleShading      = 0.0f,
        .pSampleMask           = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable      = VK_FALSE
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_BACK_BIT,
        .frontFace               = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp          = 0.0f,
        .depthBiasSlopeFactor    = 0.0f,
        .lineWidth               = 1.0f
    };

    VkPipelineColorBlendAttachmentState color_blend_attachment_state = {
        .blendEnable         = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments    = &color_blend_attachment_state,
        .blendConstants  = {0.0f, 0.0f, 0.0f, 0.0f}
    };

    VkPushConstantRange push_constants_range = {
        .stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT,
        .offset     = 0,
        .size       = sizeof(PushConstants)
    };

    std::vector<VkDescriptorSetLayoutBinding> descriptor_layout_bindings = {
        {
            .binding            = 0,
            .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT,
            .pImmutableSamplers = nullptr,
        },
        {
            .binding            = 1,
            .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount    = 10000,
            .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        }
    };

    VkDescriptorBindingFlags binding_flags[2] = {
        0,
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount  = 2,
        .pBindingFlags = binding_flags,
    };

    VkDescriptorSetLayoutCreateInfo descriptor_layout_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext        = &binding_flags_info,
        .flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = static_cast<uint32_t>(descriptor_layout_bindings.size()),
        .pBindings    = &descriptor_layout_bindings[0]

    };

    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &descriptor_layout_info, nullptr, &descriptor_layout));

    VkPipelineLayoutCreateInfo layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .setLayoutCount         = 1,
        .pSetLayouts            = &descriptor_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &push_constants_range
    };

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout));

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .depthTestEnable       = VK_TRUE,
        .depthWriteEnable      = VK_TRUE,
        .depthCompareOp        = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
        .front                 = {},
        .back                  = {},
        .minDepthBounds        = 0.0f,
        .maxDepthBounds        = 1.0f
    };

    VkGraphicsPipelineCreateInfo graphics_pipeline_info = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = &pipeline_rendering_info,
        .flags               = 0,
        .stageCount          = static_cast<uint32_t>(shader_stage_infos.size()),
        .pStages             = &shader_stage_infos[0],
        .pVertexInputState   = nullptr,
        .pInputAssemblyState = nullptr,
        .pTessellationState  = nullptr,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState   = &multisample_state,
        .pDepthStencilState  = &depth_stencil_state,
        .pColorBlendState    = &color_blend_state,
        .pDynamicState       = nullptr,
        .layout              = pipeline_layout,
        .renderPass          = VK_NULL_HANDLE,
        .subpass             = 0,
        .basePipelineHandle  = VK_NULL_HANDLE,
        .basePipelineIndex   = 0
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphics_pipeline_info, nullptr, &pipeline));

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

    VkPipelineRenderingCreateInfoKHR imgui_rendering_info = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext                   = nullptr,
        .viewMask                = 0,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &swapchain_format.format,
        .depthAttachmentFormat   = VK_FORMAT_D32_SFLOAT,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };

    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo init_info   = {};
    init_info.Instance                    = instance;
    init_info.PhysicalDevice              = physical_device;
    init_info.Device                      = device;
    init_info.QueueFamily                 = graphics_queue_index;
    init_info.Queue                       = graphics_queue;
    init_info.PipelineCache               = nullptr;
    init_info.DescriptorPool              = imgui_descriptor_pool;
    init_info.MinImageCount               = 2;
    init_info.ImageCount                  = swapchain_image_count;
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
        swapchain_extent,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
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
        VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

        image_pipeline_barrier(
            depth_buffer,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            command_buffer,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
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

    Buffer bindless_global_vertex_buffer = create_buffer(
        1024 * 1024 * 128, // 128MB
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        vma_allocator
    );

    VkBufferDeviceAddressInfo address_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = bindless_global_vertex_buffer.handle,
    };
    VkDeviceAddress global_vertex_buffer_address = vkGetBufferDeviceAddress(device, &address_info);

    Buffer bindless_global_index_buffer = create_buffer(
        1024 * 1024 * 64, // 32MB
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        vma_allocator
    );

    address_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = bindless_global_index_buffer.handle,
    };
    VkDeviceAddress global_index_buffer_address = vkGetBufferDeviceAddress(device, &address_info);

    Buffer bindless_render_command_buffer = create_buffer(
        1024 * 1024 * 16, // 16MB
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    std::vector<VkDrawIndexedIndirectCommand> bindless_render_cpu_command_buffer;

    Buffer bindless_global_uniform_buffer = create_buffer(
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

    std::vector<VkDrawMeshTasksIndirectCommandEXT> meshlet_draw_commands;

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

    VkDeviceSize meshlet_buffer_offset                   = 0;
    VkDeviceSize meshlet_vertex_indices_offset           = 0;
    VkDeviceSize meshlet_vertex_primitive_indices_offset = 0;
    VkDeviceSize meshlet_bounds_buffer_offset            = 0;

    VkDeviceSize indirect_vertex_buffer_offset = 0;
    VkDeviceSize indirect_index_buffer_offset  = 0;

    uint32_t bindless_texture_count = 10000;

    VkDescriptorSetVariableDescriptorCountAllocateInfo variable_count_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts  = &bindless_texture_count,
    };

    VkDescriptorSetAllocateInfo bindless_uniform_buffer_descriptor_set_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = &variable_count_info,
        .descriptorPool     = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &descriptor_layout
    };

    VkDescriptorSet bindless_uniform_buffer_descriptor_set = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateDescriptorSets(
        device, &bindless_uniform_buffer_descriptor_set_info, &bindless_uniform_buffer_descriptor_set
    ));

    VkDescriptorBufferInfo bindless_uniform_buffer_set_info = {
        .buffer = bindless_global_uniform_buffer.handle,
        .offset = 0,
        .range  = bindless_global_uniform_buffer.size,
    };

    VkWriteDescriptorSet bindless_uniform_buffer_write_set = {
        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext            = nullptr,
        .dstSet           = bindless_uniform_buffer_descriptor_set,
        .dstBinding       = 0,
        .dstArrayElement  = 0,
        .descriptorCount  = 1,
        .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pImageInfo       = nullptr,
        .pBufferInfo      = &bindless_uniform_buffer_set_info,
        .pTexelBufferView = nullptr
    };
    vkUpdateDescriptorSets(device, 1, &bindless_uniform_buffer_write_set, 0, nullptr);

    std::unordered_map<uint32_t, Image> texture_cache;

    auto lantern = load_model(
        "data/models/lantern.glb",
        staging_buffer,
        bindless_global_vertex_buffer,
        indirect_vertex_buffer_offset,
        bindless_global_index_buffer,
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
        vma_allocator,
        command_buffer,
        graphics_queue,
        device
    );

    auto helmet = load_model(
        "data/models/sponza-gltf-pbr/sponza.glb",
        staging_buffer,
        bindless_global_vertex_buffer,
        indirect_vertex_buffer_offset,
        bindless_global_index_buffer,
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
        vma_allocator,
        command_buffer,
        graphics_queue,
        device
    );
    for (auto& m : helmet) {
        m.position = glm::vec3(10, -4, 13);
        m.scale    = 0.01;
    }

    std::vector<Mesh> meshes;
    meshes.reserve(lantern.size() + helmet.size());
    meshes.insert(meshes.end(), lantern.begin(), lantern.end());
    meshes.insert(meshes.end(), helmet.begin(), helmet.end());

    populate_materials(texture_cache, bindless_uniform_buffer_descriptor_set, linear_sampler, device);

    bool running = true;
    while (running) {
        SDL_Event window_event;
        while (SDL_PollEvent(&window_event)) {
            ImGui_ImplSDL3_ProcessEvent(&window_event);
            switch (window_event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            }
        }

        // bindless_render_cpu_command_buffer.clear();

        meshlet_draw_commands.clear();
        bindless_draw_data_cpu_buffer.clear();
        for (auto& mesh : meshes) {
            uint32_t first_index   = static_cast<uint32_t>(mesh.index_buffer_offset / sizeof(uint32_t));
            int32_t  vertex_offset = static_cast<int32_t>(mesh.vertex_buffer_offset / sizeof(Vertex));

            VkDrawMeshTasksIndirectCommandEXT command = {
                .groupCountX = mesh.meshlet_count,
                .groupCountY = 1,
                .groupCountZ = 1,
            };
            meshlet_draw_commands.push_back(command);

            // VkDrawIndexedIndirectCommand command = {
            //     .indexCount    = mesh.index_count,
            //     .instanceCount = 1,
            //     .firstIndex    = first_index,
            //     .vertexOffset  = vertex_offset,
            //     .firstInstance = 0
            // };
            // bindless_render_cpu_command_buffer.push_back(command);

            glm::mat4 model = glm::translate(glm::mat4(1.0f), mesh.position);
            model           = glm::scale(model, glm::vec3(mesh.scale));
            bindless_draw_data_cpu_buffer.push_back(
                {model, mesh.material.albedo_index, first_index, vertex_offset, mesh.meshlet_offset, mesh.meshlet_count}
            );
        }

        void* bindless_command_ptr = nullptr;
        VK_CHECK(vmaMapMemory(vma_allocator, bindless_render_command_buffer.allocation, &bindless_command_ptr));
        memcpy(
            bindless_command_ptr,
            meshlet_draw_commands.data(),
            sizeof(VkDrawMeshTasksIndirectCommandEXT) * meshlet_draw_commands.size()
        );

        void* bindless_uniform_ptr = nullptr;
        VK_CHECK(vmaMapMemory(vma_allocator, bindless_global_uniform_buffer.allocation, &bindless_uniform_ptr));
        memcpy(
            bindless_uniform_ptr,
            bindless_draw_data_cpu_buffer.data(),
            sizeof(DrawData) * bindless_draw_data_cpu_buffer.size()
        );

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::ShowDemoWindow();

        ImGui::Render();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();

        uint32_t image_index = 0;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, image_available_semaphore, VK_NULL_HANDLE, &image_index);

        vmaSetCurrentFrameIndex(vma_allocator, image_index);

        VkCommandBufferBeginInfo begin_info = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .pInheritanceInfo = nullptr
        };
        vkBeginCommandBuffer(command_buffer, &begin_info);

        VkImageMemoryBarrier2 to_render_barrier = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext               = nullptr,
            .srcStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .srcAccessMask       = 0,
            .dstStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = swapchain_images[image_index],
            .subresourceRange    = {
                   .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                   .baseMipLevel   = 0,
                   .levelCount     = 1,
                   .baseArrayLayer = 0,
                   .layerCount     = 1
            }
        };

        VkDependencyInfo to_dependency = {
            .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext                    = nullptr,
            .dependencyFlags          = 0,
            .memoryBarrierCount       = 0,
            .pMemoryBarriers          = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers    = nullptr,
            .imageMemoryBarrierCount  = 1,
            .pImageMemoryBarriers     = &to_render_barrier
        };
        vkCmdPipelineBarrier2(command_buffer, &to_dependency);

        VkRenderingAttachmentInfo color_attachment_info = {
            .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext              = nullptr,
            .imageView          = swapchain_image_views[image_index],
            .imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode        = VK_RESOLVE_MODE_NONE,
            .resolveImageView   = VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue         = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}}
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
            .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .pNext                = nullptr,
            .flags                = 0,
            .renderArea           = {.offset = {.x = 0, .y = 0}, .extent = swapchain_extent},
            .layerCount           = 1,
            .viewMask             = 0,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color_attachment_info,
            .pDepthAttachment     = &depth_attachment_info,
            .pStencilAttachment   = nullptr
        };

        vkCmdBeginRendering(command_buffer, &rendering_info);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout,
            0,
            1,
            &bindless_uniform_buffer_descriptor_set,
            0,
            nullptr
        );

        // vkCmdBindIndexBuffer(command_buffer, bindless_global_index_buffer.handle, 0, VK_INDEX_TYPE_UINT32);

        glm::vec3 position  = {10, 0, 20};
        glm::vec3 direction = {0, 0, -1};
        glm::vec3 up        = {0, 1, 0};

        auto view = glm::lookAt(position, position + direction, up);
        auto proj = glm::perspective(glm::radians(90.0f), 1280.0f / 720.0f, 0.1f, 50.0f);
        proj[1][1] *= -1;

        PushConstants push;
        push.combined                                 = proj * view;
        push.vertex_buffer_address                    = global_vertex_buffer_address;
        push.index_buffer_address                     = global_index_buffer_address;
        push.meshlet_buffer_address                   = meshlet_buffer_address;
        push.meshlet_vertex_buffer_indices_address    = meshlet_vertex_buffer_indices_address;
        push.meshlet_primitive_indices_buffer_address = meshlet_primitive_indices_buffer_address;
        push.meshlet_bounds_buffer_address            = meshlet_bounds_buffer_address;

        vkCmdPushConstants(
            command_buffer,
            pipeline_layout,
            VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT,
            0,
            sizeof(PushConstants),
            &push
        );
        // vkCmdDrawIndexedIndirect(
        //     command_buffer,
        //     bindless_render_command_buffer.handle,
        //     0,
        //     bindless_render_cpu_command_buffer.size(),
        //     sizeof(VkDrawIndexedIndirectCommand)
        // );

        vkCmdDrawMeshTasksIndirectEXT(
            command_buffer,
            bindless_render_command_buffer.handle,
            0,
            meshlet_draw_commands.size(),
            sizeof(VkDrawMeshTasksIndirectCommandEXT)
        );

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);

        vkCmdEndRendering(command_buffer);

        VkImageMemoryBarrier2 to_present_barrier = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext               = nullptr,
            .srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask        = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            .dstAccessMask       = 0,
            .oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = swapchain_images[image_index],
            .subresourceRange    = {
                   .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                   .baseMipLevel   = 0,
                   .levelCount     = 1,
                   .baseArrayLayer = 0,
                   .layerCount     = 1
            }
        };

        VkDependencyInfo present_dependency = {
            .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext                    = nullptr,
            .dependencyFlags          = 0,
            .memoryBarrierCount       = 0,
            .pMemoryBarriers          = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers    = nullptr,
            .imageMemoryBarrierCount  = 1,
            .pImageMemoryBarriers     = &to_present_barrier
        };
        vkCmdPipelineBarrier2(command_buffer, &present_dependency);
        VK_CHECK(vkEndCommandBuffer(command_buffer));

        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo         submit_info   = {
                      .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                      .pNext                = nullptr,
                      .waitSemaphoreCount   = 1,
                      .pWaitSemaphores      = &image_available_semaphore,
                      .pWaitDstStageMask    = &wait_stages[0],
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
            .pSwapchains        = &swapchain,
            .pImageIndices      = &image_index,
            .pResults           = nullptr
        };
        VK_CHECK(vkQueuePresentKHR(graphics_queue, &present_info));

        vkDeviceWaitIdle(device);
    }

    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplVulkan_Shutdown();
    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    destroy_image(depth_buffer, device, vma_allocator);
    vmaDestroyBuffer(vma_allocator, staging_buffer.handle, staging_buffer.allocation);
    vmaDestroyAllocator(vma_allocator);
    vkDestroyDescriptorPool(device, imgui_descriptor_pool, nullptr);
    vkDestroyShaderModule(device, vertex_module, nullptr);
    vkDestroyShaderModule(device, fragment_module, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyCommandPool(device, command_pool, nullptr);
    vkDestroySemaphore(device, image_available_semaphore, nullptr);
    vkDestroySemaphore(device, render_finished_semaphore, nullptr);
    for (auto view : swapchain_image_views) {
        vkDestroyImageView(device, view, nullptr);
    }
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
    vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
