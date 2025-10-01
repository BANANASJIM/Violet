#pragma once

#include "TransferPass.hpp"
#include "ResourceFactory.hpp"
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>

namespace violet {

class VulkanContext;
class Texture;

// Resource loader service for batch GPU resource uploads
// Uses TransferPass internally for efficient batch transfers
class ResourceLoader {
public:
    // Texture loading request
    struct TextureLoadRequest {
        eastl::string filePath;
        Texture* targetTexture = nullptr;
        bool generateMipmaps = false;
        bool isKTX = false;
        bool isHDR = false;
    };

    // Buffer upload request
    struct BufferUploadRequest {
        BufferResource stagingBuffer;
        BufferResource targetBuffer;
        vk::DeviceSize size = 0;
        vk::DeviceSize srcOffset = 0;
        vk::DeviceSize dstOffset = 0;
    };

    // Image upload request (from staging buffer)
    struct ImageUploadRequest {
        BufferResource stagingBuffer;
        ImageResource targetImage;
        uint32_t width = 0;
        uint32_t height = 0;
        vk::Format format = vk::Format::eR8G8B8A8Srgb;
        vk::ImageLayout initialLayout = vk::ImageLayout::eUndefined;
        vk::ImageLayout finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        uint32_t arrayLayers = 1;  // For cubemaps: 6
        bool needsPreBarrier = true;
        bool needsPostBarrier = true;
    };

    ResourceLoader() = default;
    ~ResourceLoader();

    // Queue texture loading (will be batched)
    void queueTextureLoad(const TextureLoadRequest& request);

    // Queue buffer upload
    void queueBufferUpload(const BufferUploadRequest& request);

    // Queue image upload from staging buffer
    void queueImageUpload(const ImageUploadRequest& request);

    // Execute all queued transfers using a single TransferPass
    void flush(VulkanContext* context);

    // Execute transfers and wait for completion (synchronous)
    void flushAndWait(VulkanContext* context);

    // Clear all pending requests without executing
    void clear();

    // Check if there are pending requests
    bool hasPendingTransfers() const;

private:
    eastl::vector<TextureLoadRequest> pendingTextures;
    eastl::vector<BufferUploadRequest> pendingBuffers;
    eastl::vector<ImageUploadRequest> pendingImages;
};

} // namespace violet
