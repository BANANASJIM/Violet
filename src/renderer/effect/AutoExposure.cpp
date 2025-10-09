#include "AutoExposure.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "resource/shader/ShaderLibrary.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "core/FileSystem.hpp"
#include "core/Log.hpp"
#include <cmath>

namespace violet {

void AutoExposure::init(VulkanContext* ctx, DescriptorManager* descMgr, vk::Extent2D extent, ShaderLibrary* shaderLib) {
    context = ctx;
    descriptorManager = descMgr;
    sceneExtent = extent;
    shaderLibrary = shaderLib;

    violet::Log::info("AutoExposure", "Initializing auto-exposure system ({}x{})", extent.width, extent.height);

    // ===== Simple Method Resources =====
    {
        BufferInfo bufferInfo{};
        bufferInfo.size = sizeof(LuminanceData);
        bufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        bufferInfo.memoryUsage = MemoryUsage::GPU_TO_CPU;
        bufferInfo.debugName = "LuminanceBuffer";

        luminanceBuffer = ResourceFactory::createBuffer(context, bufferInfo);
        mappedLuminanceData = static_cast<LuminanceData*>(luminanceBuffer.mappedData);

        if (!mappedLuminanceData) {
            violet::Log::error("AutoExposure", "Failed to map luminance buffer");
            return;
        }

        // Initialize buffer
        mappedLuminanceData->avgLogLuminance = 0.0f;
        mappedLuminanceData->minLuminance = 0.0f;
        mappedLuminanceData->maxLuminance = 0.0f;
        mappedLuminanceData->sampleCount = 0;

        // Allocate descriptor set
        luminanceDescriptorSet = descriptorManager->allocateSet("LuminanceCompute", 0);

        // Update descriptor set with buffer (binding 1)
        vk::DescriptorBufferInfo bufferDescInfo{};
        bufferDescInfo.buffer = luminanceBuffer.buffer;
        bufferDescInfo.offset = 0;
        bufferDescInfo.range = sizeof(LuminanceData);

        vk::WriteDescriptorSet bufferWrite{};
        bufferWrite.dstSet = luminanceDescriptorSet;
        bufferWrite.dstBinding = 1;
        bufferWrite.dstArrayElement = 0;
        bufferWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        bufferWrite.descriptorCount = 1;
        bufferWrite.pBufferInfo = &bufferDescInfo;

        context->getDevice().updateDescriptorSets(1, &bufferWrite, 0, nullptr);

        // Create pipeline
        luminancePipeline = eastl::make_unique<ComputePipeline>();
        auto shader = shaderLibrary->get("luminance_average");
        ComputePipelineConfig pipelineConfig{};
        pipelineConfig.descriptorSetLayouts.push_back(descriptorManager->getLayout("LuminanceCompute"));
        luminancePipeline->init(context, shader, pipelineConfig);
    }

    // ===== Histogram Method Resources =====
    {
        BufferInfo bufferInfo{};
        bufferInfo.size = sizeof(HistogramData);
        bufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        bufferInfo.memoryUsage = MemoryUsage::GPU_TO_CPU;
        bufferInfo.debugName = "HistogramBuffer";

        histogramBuffer = ResourceFactory::createBuffer(context, bufferInfo);
        mappedHistogramData = static_cast<HistogramData*>(histogramBuffer.mappedData);

        if (!mappedHistogramData) {
            violet::Log::error("AutoExposure", "Failed to map histogram buffer");
            return;
        }

        // Initialize histogram
        for (int i = 0; i < 64; ++i) {
            mappedHistogramData->bins[i] = 0;
        }
        mappedHistogramData->minLogLuminance = params.minLogLuminance;
        mappedHistogramData->maxLogLuminance = params.maxLogLuminance;
        mappedHistogramData->pixelCount = 0;

        // Allocate descriptor set (reuse same layout)
        histogramDescriptorSet = descriptorManager->allocateSet("LuminanceCompute", 0);

        // Update descriptor set with histogram buffer (binding 1)
        vk::DescriptorBufferInfo bufferDescInfo{};
        bufferDescInfo.buffer = histogramBuffer.buffer;
        bufferDescInfo.offset = 0;
        bufferDescInfo.range = sizeof(HistogramData);

        vk::WriteDescriptorSet bufferWrite{};
        bufferWrite.dstSet = histogramDescriptorSet;
        bufferWrite.dstBinding = 1;
        bufferWrite.dstArrayElement = 0;
        bufferWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        bufferWrite.descriptorCount = 1;
        bufferWrite.pBufferInfo = &bufferDescInfo;

        context->getDevice().updateDescriptorSets(1, &bufferWrite, 0, nullptr);

        // Create pipeline with push constants
        histogramPipeline = eastl::make_unique<ComputePipeline>();
        auto shader = shaderLibrary->get("luminance_histogram");
        ComputePipelineConfig pipelineConfig{};
        pipelineConfig.descriptorSetLayouts.push_back(descriptorManager->getLayout("LuminanceCompute"));

        // Push constants for histogram parameters
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushConstantRange.offset = 0;
        pushConstantRange.size = 4 * sizeof(float); // 4 floats: minLogLum, maxLogLum, centerWeightPower, enabled
        pipelineConfig.pushConstantRanges.push_back(pushConstantRange);

        histogramPipeline->init(context, shader, pipelineConfig);
    }

    violet::Log::info("AutoExposure", "Auto-exposure initialized (Simple + Histogram)");
}

void AutoExposure::cleanup() {
    if (!context) return;

    // Clean up Simple method resources
    luminancePipeline.reset();
    if (luminanceBuffer.buffer) {
        ResourceFactory::destroyBuffer(context, luminanceBuffer);
    }
    mappedLuminanceData = nullptr;

    // Clean up Histogram method resources
    histogramPipeline.reset();
    if (histogramBuffer.buffer) {
        ResourceFactory::destroyBuffer(context, histogramBuffer);
    }
    mappedHistogramData = nullptr;

    descriptorManager = nullptr;
    context = nullptr;
}

void AutoExposure::computeLuminance(vk::CommandBuffer cmd, vk::ImageView hdrSceneView, vk::Sampler sampler) {
    if (!params.enabled) {
        return;
    }

    // Update descriptor set with HDR scene (binding 0) - shared for both methods
    vk::DescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler;
    imageInfo.imageView = hdrSceneView;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    if (params.method == AutoExposureMethod::Simple) {
        if (!luminancePipeline) return;

        // Clear luminance buffer
        cmd.fillBuffer(luminanceBuffer.buffer, 0, sizeof(LuminanceData), 0);

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

        // Update descriptor
        vk::WriteDescriptorSet imageWrite{};
        imageWrite.dstSet = luminanceDescriptorSet;
        imageWrite.dstBinding = 0;
        imageWrite.dstArrayElement = 0;
        imageWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        imageWrite.descriptorCount = 1;
        imageWrite.pImageInfo = &imageInfo;
        context->getDevice().updateDescriptorSets(1, &imageWrite, 0, nullptr);

        // Bind and dispatch
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, luminancePipeline->getPipeline());
        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            luminancePipeline->getPipelineLayout(),
            0, 1, &luminanceDescriptorSet, 0, nullptr
        );
        cmd.dispatch(1, 1, 1);

        // Barrier for CPU read
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
    else if (params.method == AutoExposureMethod::Histogram) {
        if (!histogramPipeline) return;

        // Clear histogram buffer
        cmd.fillBuffer(histogramBuffer.buffer, 0, sizeof(HistogramData), 0);

        vk::BufferMemoryBarrier clearBarrier{};
        clearBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        clearBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
        clearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        clearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        clearBarrier.buffer = histogramBuffer.buffer;
        clearBarrier.offset = 0;
        clearBarrier.size = sizeof(HistogramData);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            {},
            0, nullptr,
            1, &clearBarrier,
            0, nullptr
        );

        // Update descriptor
        vk::WriteDescriptorSet imageWrite{};
        imageWrite.dstSet = histogramDescriptorSet;
        imageWrite.dstBinding = 0;
        imageWrite.dstArrayElement = 0;
        imageWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        imageWrite.descriptorCount = 1;
        imageWrite.pImageInfo = &imageInfo;
        context->getDevice().updateDescriptorSets(1, &imageWrite, 0, nullptr);

        // Push constants
        struct HistogramPushConstants {
            float minLogLum;
            float maxLogLum;
            float centerWeightPower;
            uint32_t enabled;
        } pushConstants;

        pushConstants.minLogLum = params.minLogLuminance;
        pushConstants.maxLogLum = params.maxLogLuminance;
        pushConstants.centerWeightPower = params.centerWeightPower;
        pushConstants.enabled = 1;

        // Bind and dispatch
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, histogramPipeline->getPipeline());
        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            histogramPipeline->getPipelineLayout(),
            0, 1, &histogramDescriptorSet, 0, nullptr
        );
        cmd.pushConstants(
            histogramPipeline->getPipelineLayout(),
            vk::ShaderStageFlagBits::eCompute,
            0, sizeof(HistogramPushConstants), &pushConstants
        );

        // Dispatch: cover entire scene with 16x16 workgroups
        uint32_t groupsX = (sceneExtent.width + 15) / 16;
        uint32_t groupsY = (sceneExtent.height + 15) / 16;
        cmd.dispatch(groupsX, groupsY, 1);

        // Barrier for CPU read
        vk::BufferMemoryBarrier computeBarrier{};
        computeBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        computeBarrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
        computeBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        computeBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        computeBarrier.buffer = histogramBuffer.buffer;
        computeBarrier.offset = 0;
        computeBarrier.size = sizeof(HistogramData);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eHost,
            {},
            0, nullptr,
            1, &computeBarrier,
            0, nullptr
        );
    }
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
        if (params.method == AutoExposureMethod::Simple) {
            readLuminanceData();
        } else if (params.method == AutoExposureMethod::Histogram) {
            readHistogramData();
        }
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

void AutoExposure::readHistogramData() {
    if (!mappedHistogramData) return;

    // Read from persistent mapping
    uint32_t pixelCount = mappedHistogramData->pixelCount;
    if (pixelCount == 0) {
        // No data yet, use default
        targetEV100 = 9.0f;
        return;
    }

    // Analyze histogram to compute weighted average log luminance
    float avgLogLuminance = analyzeHistogram(*mappedHistogramData);

    // Convert from log2 space to linear
    float avgLuminance = std::pow(2.0f, avgLogLuminance);

    // Compute target EV100 from average luminance
    float ev100 = computeEV100FromLuminance(avgLuminance);

    // Apply exposure compensation
    targetEV100 = ev100 + params.exposureCompensation;

    // Clamp target to valid range
    targetEV100 = std::clamp(targetEV100, params.minEV100, params.maxEV100);
}

float AutoExposure::analyzeHistogram(const HistogramData& histogram) {
    // Calculate total weighted pixel count
    uint64_t totalCount = 0;
    for (int i = 0; i < 64; ++i) {
        totalCount += histogram.bins[i];
    }

    if (totalCount == 0) {
        return 0.0f; // No data
    }

    // Find percentile thresholds to filter outliers
    uint64_t lowThreshold = static_cast<uint64_t>(totalCount * params.lowPercentile);
    uint64_t highThreshold = static_cast<uint64_t>(totalCount * params.highPercentile);

    // Accumulate counts to find valid range
    uint64_t accumulatedCount = 0;
    int startBin = 0;
    int endBin = 63;

    // Find start bin (skip darkest percentile)
    for (int i = 0; i < 64; ++i) {
        accumulatedCount += histogram.bins[i];
        if (accumulatedCount >= lowThreshold) {
            startBin = i;
            break;
        }
    }

    // Find end bin (skip brightest percentile)
    accumulatedCount = 0;
    for (int i = 63; i >= 0; --i) {
        accumulatedCount += histogram.bins[i];
        if ((totalCount - accumulatedCount) >= highThreshold) {
            endBin = i;
            break;
        }
    }

    // Compute weighted average log luminance in valid range
    float sumWeightedLogLum = 0.0f;
    uint64_t validCount = 0;

    float logLumRange = histogram.maxLogLuminance - histogram.minLogLuminance;

    for (int i = startBin; i <= endBin; ++i) {
        if (histogram.bins[i] == 0) continue;

        // Bin center in log space
        float binCenter = histogram.minLogLuminance + (i + 0.5f) * (logLumRange / 64.0f);

        sumWeightedLogLum += binCenter * histogram.bins[i];
        validCount += histogram.bins[i];
    }

    if (validCount == 0) {
        // Fallback to middle of range
        return (histogram.minLogLuminance + histogram.maxLogLuminance) * 0.5f;
    }

    // Average log luminance
    float avgLogLum = sumWeightedLogLum / static_cast<float>(validCount);

    return avgLogLum;
}

} // namespace violet
