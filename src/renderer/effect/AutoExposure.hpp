#pragma once

#include <vulkan/vulkan.hpp>
#include "renderer/vulkan/VulkanContext.hpp"
#include "resource/gpu/ResourceFactory.hpp"
#include "renderer/vulkan/ComputePipeline.hpp"
#include <EASTL/unique_ptr.h>

namespace violet {

class ShaderLibrary;

// Auto-exposure method
enum class AutoExposureMethod {
    Simple,      // Simple downsampled average (fast, less accurate)
    Histogram    // Histogram-based (accurate, industry standard)
};

// Auto-exposure parameters
struct AutoExposureParams {
    bool enabled = false;              // Enable/disable auto-exposure
    AutoExposureMethod method = AutoExposureMethod::Histogram; // Method to use
    float adaptationSpeed = 2.0f;      // Speed of adaptation (higher = faster)
    float minEV100 = 1.0f;             // Minimum EV100 (prevent too dark)
    float maxEV100 = 16.0f;            // Maximum EV100 (prevent too bright)
    float exposureCompensation = 0.0f; // Manual compensation offset (EV stops)

    // Histogram-specific parameters
    float lowPercentile = 0.05f;       // Ignore darkest 5% (0.0-1.0)
    float highPercentile = 0.95f;      // Ignore brightest 5% (0.0-1.0)
    float centerWeightPower = 2.0f;    // Center weighting strength (0 = uniform)
    float minLogLuminance = -4.0f;     // Histogram min range (EV)
    float maxLogLuminance = 12.0f;     // Histogram max range (EV)
};

// Luminance statistics buffer (GPU → CPU) - Simple method
struct LuminanceData {
    float avgLogLuminance = 0.0f;  // Average log2 luminance
    float minLuminance = 0.0f;     // Minimum luminance (future use)
    float maxLuminance = 0.0f;     // Maximum luminance (future use)
    uint32_t sampleCount = 0;      // Number of workgroups (for averaging)
};

// Histogram buffer (GPU → CPU) - Histogram method
struct HistogramData {
    uint32_t bins[64];             // Histogram bins (64 bins for 16 EV range)
    float minLogLuminance;         // Minimum log2 luminance in range
    float maxLogLuminance;         // Maximum log2 luminance in range
    uint32_t pixelCount;           // Total pixels processed
};

/**
 * @brief Auto-exposure system using compute shader luminance analysis
 *
 * Implements automatic exposure adjustment based on scene luminance,
 * following Frostbite/UE4 approach with log-average luminance.
 *
 * Pipeline:
 * 1. Compute Pass: Calculate average scene luminance
 * 2. CPU: Read luminance, compute target EV100
 * 3. CPU: Smooth interpolation to target EV100
 * 4. PostProcess: Use auto-computed EV100
 *
 * Reference:
 * - https://bruop.github.io/exposure/
 * - https://knarkowicz.wordpress.com/2016/01/09/automatic-exposure/
 */
class AutoExposure {
public:
    AutoExposure() = default;
    ~AutoExposure() { cleanup(); }

    AutoExposure(const AutoExposure&) = delete;
    AutoExposure& operator=(const AutoExposure&) = delete;

    /**
     * @brief Initialize auto-exposure system
     * @param context Vulkan context
     * @param descriptorManager Descriptor manager for layout/set allocation
     * @param sceneExtent Size of the HDR scene texture
     * @param shaderLibrary Shader library for loading shaders
     */
    void init(VulkanContext* context, class DescriptorManager* descriptorManager, vk::Extent2D sceneExtent, ShaderLibrary* shaderLibrary);

    /**
     * @brief Cleanup resources
     */
    void cleanup();

    /**
     * @brief Compute luminance statistics from HDR scene
     * @param cmd Command buffer for compute commands
     * @param hdrSceneView Image view of HDR scene texture
     * @param sampler Sampler for HDR scene
     */
    void computeLuminance(vk::CommandBuffer cmd, vk::ImageView hdrSceneView, vk::Sampler sampler);

    /**
     * @brief Update auto-exposure state (call once per frame on CPU)
     * @param deltaTime Time since last frame (seconds)
     */
    void update(float deltaTime);

    /**
     * @brief Get current EV100 value (auto or manual)
     * @return Current exposure value
     */
    float getCurrentEV100() const { return currentEV100; }

    /**
     * @brief Get target EV100 computed from scene luminance
     * @return Target exposure value
     */
    float getTargetEV100() const { return targetEV100; }

    /**
     * @brief Get auto-exposure parameters (for UI)
     */
    AutoExposureParams& getParams() { return params; }
    const AutoExposureParams& getParams() const { return params; }

    /**
     * @brief Set manual EV100 (when auto-exposure disabled)
     */
    void setManualEV100(float ev100) { manualEV100 = ev100; }
    float getManualEV100() const { return manualEV100; }

private:
    /**
     * @brief Convert average luminance to EV100
     * Formula from Frostbite: EV100 = log2(avgLuminance * S / K)
     * where S = ISO (100), K = calibration constant (12.5)
     */
    float computeEV100FromLuminance(float avgLuminance);

    /**
     * @brief Read luminance data from GPU buffer (with frame delay)
     */
    void readLuminanceData();

    /**
     * @brief Read histogram data and compute weighted average luminance
     */
    void readHistogramData();

    /**
     * @brief Analyze histogram to compute average log luminance
     * @param histogram Histogram data from GPU
     * @return Average log2 luminance (weighted, percentile-filtered)
     */
    float analyzeHistogram(const HistogramData& histogram);

private:
    VulkanContext* context = nullptr;
    class DescriptorManager* descriptorManager = nullptr;
    ShaderLibrary* shaderLibrary = nullptr;

    // Simple method resources
    eastl::unique_ptr<ComputePipeline> luminancePipeline;
    vk::DescriptorSet luminanceDescriptorSet = VK_NULL_HANDLE;
    BufferResource luminanceBuffer;
    LuminanceData* mappedLuminanceData = nullptr;

    // Histogram method resources
    eastl::unique_ptr<ComputePipeline> histogramPipeline;
    vk::DescriptorSet histogramDescriptorSet = VK_NULL_HANDLE;
    BufferResource histogramBuffer;
    HistogramData* mappedHistogramData = nullptr;

    // Exposure state
    AutoExposureParams params;
    float currentEV100 = 9.0f;   // Current EV100 (smoothly interpolated)
    float targetEV100 = 9.0f;    // Target EV100 (from luminance)
    float manualEV100 = 9.0f;    // Manual EV100 (when auto disabled)

    // Frame delay for GPU→CPU readback (avoid pipeline stall)
    static constexpr uint32_t READBACK_DELAY = 2;
    uint32_t frameCounter = 0;

    vk::Extent2D sceneExtent = {1280, 720};
};

} // namespace violet
