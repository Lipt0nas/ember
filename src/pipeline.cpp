#include "pipeline.hpp"

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

    spdlog::error("Failed to load shader module {}", path.filename().string());
    return VK_NULL_HANDLE;
}
