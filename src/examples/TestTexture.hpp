#pragma once

#include "resource/Texture.hpp"

namespace violet {

class TestTexture {
public:
    static void createCheckerboardTexture(VulkanContext* context, Texture& texture, uint32_t width = 256, uint32_t height = 256);
    static void createWhiteTexture(VulkanContext* context, Texture& texture);
};

}