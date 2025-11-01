#include "AutoExposure.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "resource/shader/ShaderLibrary.hpp"
#include "renderer/vulkan/DescriptorManager.hpp"
#include "renderer/vulkan/ShaderResourceBinding.hpp"
#include "renderer/graph/RenderGraph.hpp"
#include "core/FileSystem.hpp"
#include "core/Log.hpp"
#include <cmath>

namespace violet {

void AutoExposure::init(VulkanContext* ctx, DescriptorManager* descMgr, vk::Extent2D extent, ShaderLibrary* shaderLib, RenderGraph* graph, const eastl::string& hdrName) {
    context = ctx;
    descriptorManager = descMgr;
    sceneExtent = extent;
    shaderLibrary = shaderLib;
    renderGraph = graph;
    hdrImageName = hdrName;
    updateTimer.reset();

    luminanceBuffer = ResourceFactory::createBuffer(context, {
        .size = sizeof(LuminanceData),
        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .memoryUsage = MemoryUsage::GPU_TO_CPU,
        .debugName = "LuminanceBuffer"
    });
    mappedLuminanceData = static_cast<LuminanceData*>(luminanceBuffer.mappedData);
    *mappedLuminanceData = {};

    // Get layout handle from DescriptorManager (shader auto-registers during pipeline creation later)
    // For now, we need to create pipeline first to register layout, then allocate sets
    auto luminanceShader = shaderLibrary->get("luminance_average_comp").lock();
    if (!luminanceShader) {
        violet::Log::error("Renderer", "Failed to get luminance_average_comp shader");
        return;
    }

    // Create and initialize pipeline (registers layouts automatically via shader reflection)
    luminancePipeline = eastl::make_unique<ComputePipeline>();
    ComputePipelineConfig config{};
    luminancePipeline->init(context, descriptorManager, luminanceShader, config);

    // Create SRB and bind storage buffer (binding 2)
    luminanceSRB.bindBuffer(0, 2, &luminanceBuffer, 0, sizeof(LuminanceData));

    histogramBuffer = ResourceFactory::createBuffer(context, {
        .size = sizeof(HistogramData),
        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .memoryUsage = MemoryUsage::GPU_TO_CPU,
        .debugName = "HistogramBuffer"
    });
    mappedHistogramData = static_cast<HistogramData*>(histogramBuffer.mappedData);
    *mappedHistogramData = {};
    mappedHistogramData->minLogLuminance = params.minLogLuminance;
    mappedHistogramData->maxLogLuminance = params.maxLogLuminance;

    // Get shader
    auto histogramShader = shaderLibrary->get("luminance_histogram_comp").lock();
    if (!histogramShader) {
        violet::Log::error("Renderer", "Failed to get luminance_histogram_comp shader");
        return;
    }

    // Create and initialize pipeline first (registers layouts automatically)
    histogramPipeline = eastl::make_unique<ComputePipeline>();
    config = {};
    config.pushConstantRanges.push_back({vk::ShaderStageFlagBits::eCompute, 0, 4 * sizeof(float)});
    histogramPipeline->init(context, descriptorManager, histogramShader, config);

    // Create SRB and bind storage buffer (binding 2)
    histogramSRB.bindBuffer(0, 2, &histogramBuffer, 0, sizeof(HistogramData));
}

void AutoExposure::cleanup() {
    if (!context) return;

    luminancePipeline.reset();
    histogramPipeline.reset();

    if (luminanceBuffer.buffer) {
        ResourceFactory::destroyBuffer(context, luminanceBuffer);
    }
    if (histogramBuffer.buffer) {
        ResourceFactory::destroyBuffer(context, histogramBuffer);
    }

    mappedLuminanceData = nullptr;
    mappedHistogramData = nullptr;
    descriptorManager = nullptr;
    context = nullptr;
}

void AutoExposure::importBufferToRenderGraph(RenderGraph* graph) {
    if (!params.enabled || !graph) return;

    // Import readback buffer with GPU→CPU synchronization
    // GPU writes in compute shader, CPU reads after frame completion
    graph->importBuffer(
        getActiveBufferName(),
        getActiveReadbackBuffer(),
        vk::PipelineStageFlagBits2::eComputeShader,  // initialStage: GPU writes
        vk::PipelineStageFlagBits2::eHost,           // finalStage: CPU reads
        vk::AccessFlagBits2::eShaderWrite,           // initialAccess: compute shader output
        vk::AccessFlagBits2::eHostRead               // finalAccess: CPU readback
    );
}

void AutoExposure::executePass(vk::CommandBuffer cmd, uint32_t frameIndex) {
    const LogicalResource* hdrRes = renderGraph->getResource(hdrImageName);
    if (!hdrRes) return;

    vk::ImageView hdrView = hdrRes->isExternal ? hdrRes->imageResource->view : hdrRes->transientView;
    if (!hdrView) return;

    // Create temporary SRB with per-frame resources
    ShaderResourceBinding tempSRB;

    // Binding 0: HDR image (SampledImage)
    tempSRB.bindSampledImage(0, 0, hdrView, vk::ImageLayout::eShaderReadOnlyOptimal);

    // Binding 1: Sampler
    vk::Sampler sampler = descriptorManager->getSamplerManager().getSampler(SamplerType::ClampToEdge);
    tempSRB.bindSampler(0, 1, sampler);

    // Binding 2: Storage buffer (merge from persistent SRB)
    if (params.method == AutoExposureMethod::Simple) {
        tempSRB.merge(luminanceSRB);
    } else {
        tempSRB.merge(histogramSRB);
    }

    if (params.method == AutoExposureMethod::Simple) {
        cmd.fillBuffer(luminanceBuffer.buffer, 0, VK_WHOLE_SIZE, 0);
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, luminancePipeline->getPipeline());

        // Update and bind descriptors using SRB
        descriptorManager->update(tempSRB, luminancePipeline.get());
        descriptorManager->bind(cmd, tempSRB, luminancePipeline.get());

        cmd.dispatch(1, 1, 1);
    } else {
        cmd.fillBuffer(histogramBuffer.buffer, 0, VK_WHOLE_SIZE, 0);

        struct { float minLogLum, maxLogLum, centerWeightPower; uint32_t enabled; } pushConstants{
            params.minLogLuminance, params.maxLogLuminance, params.centerWeightPower, 1
        };

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, histogramPipeline->getPipeline());

        // Update and bind descriptors using SRB
        descriptorManager->update(tempSRB, histogramPipeline.get());
        descriptorManager->bind(cmd, tempSRB, histogramPipeline.get());

        cmd.pushConstants(histogramPipeline->getPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(pushConstants), &pushConstants);
        cmd.dispatch((sceneExtent.width + 15) / 16, (sceneExtent.height + 15) / 16, 1);
    }
}

void AutoExposure::updateExposure() {
    if (!params.enabled) {
        currentEV100 = manualEV100;
        return;
    }

    frameCounter++;
    if (frameCounter >= READBACK_DELAY) {
        params.method == AutoExposureMethod::Simple ? readLuminanceData() : readHistogramData();
    }

    float lerpFactor = 1.0f - std::exp(-params.adaptationSpeed * updateTimer.tick());
    currentEV100 = std::clamp(currentEV100 + (targetEV100 - currentEV100) * lerpFactor, params.minEV100, params.maxEV100);
}

void AutoExposure::readLuminanceData() {
    if (!mappedLuminanceData) return;

    if (mappedLuminanceData->sampleCount == 0) {
        targetEV100 = 9.0f;
        return;
    }

    float avgLuminance = std::pow(2.0f, mappedLuminanceData->avgLogLuminance);
    float ev100 = computeEV100FromLuminance(avgLuminance);
    targetEV100 = std::clamp(ev100 + params.exposureCompensation, params.minEV100, params.maxEV100);
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

    if (mappedHistogramData->pixelCount == 0) {
        targetEV100 = 9.0f;
        return;
    }

    float avgLogLuminance = analyzeHistogram(*mappedHistogramData);
    float avgLuminance = std::pow(2.0f, avgLogLuminance);
    float ev100 = computeEV100FromLuminance(avgLuminance);
    targetEV100 = std::clamp(ev100 + params.exposureCompensation, params.minEV100, params.maxEV100);
}

const BufferResource* AutoExposure::getActiveReadbackBuffer() const {
    return params.method == AutoExposureMethod::Simple ? &luminanceBuffer : &histogramBuffer;
}

eastl::string AutoExposure::getActiveBufferName() const {
    return params.method == AutoExposureMethod::Simple ? "luminanceBuffer" : "histogramBuffer";
}

void AutoExposure::resize(vk::Extent2D newExtent) {
    sceneExtent = newExtent;
}

float AutoExposure::analyzeHistogram(const HistogramData& histogram) {
    uint64_t totalCount = 0;
    for (int i = 0; i < 64; ++i) totalCount += histogram.bins[i];
    if (totalCount == 0) return 0.0f;

    uint64_t lowThreshold = totalCount * params.lowPercentile;
    uint64_t highThreshold = totalCount * params.highPercentile;

    int startBin = 0, endBin = 63;
    uint64_t accumulated = 0;

    for (int i = 0; i < 64; ++i) {
        accumulated += histogram.bins[i];
        if (accumulated >= lowThreshold) { startBin = i; break; }
    }

    accumulated = 0;
    for (int i = 63; i >= 0; --i) {
        accumulated += histogram.bins[i];
        if (totalCount - accumulated >= highThreshold) { endBin = i; break; }
    }

    float sumWeighted = 0.0f;
    uint64_t validCount = 0;
    float binSize = (histogram.maxLogLuminance - histogram.minLogLuminance) / 64.0f;

    for (int i = startBin; i <= endBin; ++i) {
        if (histogram.bins[i] > 0) {
            sumWeighted += (histogram.minLogLuminance + (i + 0.5f) * binSize) * histogram.bins[i];
            validCount += histogram.bins[i];
        }
    }

    return validCount > 0 ? sumWeighted / validCount : (histogram.minLogLuminance + histogram.maxLogLuminance) * 0.5f;
}

} // namespace violet
