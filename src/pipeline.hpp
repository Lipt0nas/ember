#pragma once

#include "ember.hpp"

VkShaderModule shader_module_from_file(VkDevice device, const std::filesystem::path& path);
