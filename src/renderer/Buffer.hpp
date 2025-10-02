#pragma once

#include <vulkan/vulkan.hpp>
#include "ResourceFactory.hpp"

namespace violet {

class VulkanContext;

// RAII variant for special use cases
vk::raii::CommandBuffer beginSingleTimeCommandsRAII(VulkanContext* context);

}