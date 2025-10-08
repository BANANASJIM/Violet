#include "AutoExposure.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "renderer/descriptor/DescriptorManager.hpp"
#include "core/FileSystem.hpp"
#include "core/Log.hpp"
#include <cmath>

namespace violet {

void AutoExposure::init(VulkanContext* ctx, DescriptorManager* descMgr, vk::Extent2D extent) {
    context = ctx;
    descriptorManager = descMgr;
    sceneExtent = extent;

    violet::Log::info("AutoExposure", "Initializing auto-exposure system ({}x{})", extent.width, extent.height);

    // Create luminance buffer (GPU → CPU readable)
    BufferInfo bufferInfo{};
    bufferInfo.size = sizeof(LuminanceData);
    bufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
    bufferInfo.memoryUsage = MemoryUsage::GPU_TO_CPU;
    bufferInfo.debugName = "LuminanceBuffer";

    luminanceBuffer = ResourceFactory::createBuffer(context, bufferInfo);

    // Get persistent mapping for CPU access
    mappedLuminanceData = static_cast<LuminanceData*>(luminanceBuffer.mappedData);
    if (!mappedLuminanceData) {
        violet::Log::error("AutoExposure", "Failed to map luminance buffer");
        return;
    }

    // Initialize buffer data
    mappedLuminanceData->avgLogLuminance = 0.0f;
    mappedLuminanceData->minLuminance = 0.0f;
    mappedLuminanceData->maxLuminance = 0.0f;
    mappedLuminanceData->sampleCount = 0;

    // Allocate descriptor set (layout already registered in ForwardRenderer)
    descriptorSet = descriptorManager->allocateSet("LuminanceCompute", 0);

    // Update descriptor set with luminance buffer (binding 1)
    vk::DescriptorBufferInfo bufferDescInfo{};
    bufferDescInfo.buffer = luminanceBuffer.buffer;
    bufferDescInfo.offset = 0;
    bufferDescInfo.range = sizeof(LuminanceData);

    vk::WriteDescriptorSet bufferWrite{};
    bufferWrite.dstSet = descriptorSet;
    bufferWrite.dstBinding = 1;
    bufferWrite.dstArrayElement = 0;
    bufferWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
    bufferWrite.descriptorCount = 1;
    bufferWrite.pBufferInfo = &bufferDescInfo;

    context->getDevice().updateDescriptorSets(1, &bufferWrite, 0, nullptr);

    // Create compute pipeline
    luminancePipeline = eastl::make_unique<ComputePipeline>();

    eastl::string shaderPath = violet::FileSystem::resolveRelativePath("build/shaders/luminance_average.comp.spv");
    ComputePipelineConfig pipelineConfig{};
    pipelineConfig.descriptorSetLayouts.push_back(descriptorManager->getLayout("LuminanceCompute"));

    luminancePipeline->init(context, shaderPath, pipelineConfig);

    violet::Log::info("AutoExposure", "Auto-exposure initialized");
}

void AutoExposure::cleanup() {
    if (!context) return;

    luminancePipeline.reset();

    if (luminanceBuffer.buffer) {
        ResourceFactory::destroyBuffer(context, luminanceBuffer);
    }

    mappedLuminanceData = nullptr;
    descriptorManager = nullptr;
    context = nullptr;
}

void AutoExposure::computeLuminance(vk::CommandBuffer cmd, vk::ImageView hdrSceneView, vk::Sampler sampler) {
    if (!params.enabled || !luminancePipeline) {
        return;
    }

    // Clear luminance buffer before compute
    cmd.fillBuffer(luminanceBuffer.buffer, 0, sizeof(LuminanceData), 0);

    // Memory barrier: ensure clear completes before compute shader reads
    vk::BufferMemoryBarrier clearBarrier{};
    clearBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    clearBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    clearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    clearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    clearBarrier.buffer = luminanceBuffer.buffer;
    clearBarrier.offset = 0;
    clearBarrier.size = sizeof(LuminanceData);

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eComputeShader,
        {},
        0, nullptr,
        1, &clearBarrier,
        0, nullptr
    );

    // Update descriptor set with HDR scene (binding 0)
    vk::DescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler;
    imageInfo.imageView = hdrSceneView;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::WriteDescriptorSet imageWrite{};
    imageWrite.dstSet = descriptorSet;
    imageWrite.dstBinding = 0;
    imageWrite.dstArrayElement = 0;
    imageWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    imageWrite.descriptorCount = 1;
    imageWrite.pImageInfo = &imageInfo;

    context->getDevice().updateDescriptorSets(1, &imageWrite, 0, nullptr);

    // Bind compute pipeline
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, luminancePipeline->getPipeline());
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        luminancePipeline->getPipelineLayout(),
        0,  // first set
        1,  // count
        &descriptorSet,
        0, nullptr
    );

    // Dispatch compute shader (single 16x16 workgroup for downsampled luminance)
    cmd.dispatch(1, 1, 1);

    // Memory barrier: ensure compute completes before CPU read
    vk::BufferMemoryBarrier computeBarrier{};
    computeBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    computeBarrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
    computeBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    computeBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    computeBarrier.buffer = luminanceBuffer.buffer;
    computeBarrier.offset = 0;
    computeBarrier.size = sizeof(LuminanceData);

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eHost,
        {},
        0, nullptr,
        1, &computeBarrier,
        0, nullptr
    );
}

void AutoExposure::update(float deltaTime) {
    if (!params.enabled) {
        // Use manual EV100 when auto-exposure disabled
        currentEV100 = manualEV100;
        return;
    }

    frameCounter++;

    // Read luminance data with frame delay (avoid pipeline stall)
    if (frameCounter >= READBACK_DELAY) {
        readLuminanceData();
    }

    // Smooth interpolation to target EV100
    float lerpFactor = 1.0f - std::exp(-params.adaptationSpeed * deltaTime);
    currentEV100 = currentEV100 + (targetEV100 - currentEV100) * lerpFactor;

    // Clamp to min/max range
    currentEV100 = std::clamp(currentEV100, params.minEV100, params.maxEV100);
}

void AutoExposure::readLuminanceData() {
    if (!mappedLuminanceData) return;

    // Read from persistent mapping
    uint32_t sampleCount = mappedLuminanceData->sampleCount;
    if (sampleCount == 0) {
        // No data yet, use default
        targetEV100 = 9.0f;
        return;
    }

    // Average log luminance is already computed in shader (single workgroup)
    float avgLogLuminance = mappedLuminanceData->avgLogLuminance;

    // Convert from log2 space to linear
    float avgLuminance = std::pow(2.0f, avgLogLuminance);

    // Compute target EV100 from average luminance
    float ev100 = computeEV100FromLuminance(avgLuminance);

    // Apply exposure compensation
    targetEV100 = ev100 + params.exposureCompensation;

    // Clamp target to valid range
    targetEV100 = std::clamp(targetEV100, params.minEV100, params.maxEV100);
}

float AutoExposure::computeEV100FromLuminance(float avgLuminance) {
    // Frostbite formula: EV100 = log2(avgLuminance * S / K)
    // S = ISO speed (100 for EV100)
    // K = reflected-light meter calibration constant (12.5)
    constexpr float S = 100.0f;
    constexpr float K = 12.5f;

    // Clamp luminance to avoid log(0)
    avgLuminance = std::max(avgLuminance, 0.001f);

    return std::log2(avgLuminance * S / K);
}

} // namespace violet
