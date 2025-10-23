// RenderGraph DAG Test
// Tests dependency graph building with backward traversal from Present passes

#include "TestRenderGraph.hpp"
#include <fmt/core.h>
#include <cstdlib>

using namespace violet;

// EASTL allocators
void* operator new[](size_t size, const char*, int, unsigned, const char*, int) {
    return malloc(size);
}

void* operator new[](size_t size, size_t alignment, size_t, const char*, int, unsigned, const char*, int) {
    return aligned_alloc(alignment, size);
}

// Test 1: Linear chain A -> B -> C -> Present
void testLinearChain() {
    fmt::print("\n=== Test 1: Linear Chain ===\n");

    TestRenderGraph graph;
    MockVulkanContext ctx;
    graph.init(&ctx);

    // Create resources
    graph.createImage("imageA", {1920, 1080}, false);
    graph.createImage("imageB", {1920, 1080}, false);
    graph.createImage("imageC", {1920, 1080}, false);

    // Import swapchain with PRESENT_SRC → PRESENT_SRC constraint (acquired from vkAcquireNextImageKHR)
    graph.importImage("swapchain", nullptr,
        ImageLayout::PresentSrc,      // Initial layout (from vkAcquireNextImageKHR)
        ImageLayout::PresentSrc,      // Final layout (REQUIRED for vkQueuePresentKHR)
        PipelineStage::TopOfPipe,
        PipelineStage::BottomOfPipe
    );

    // Pass A: Writes imageA
    graph.addPass("PassA", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("imageA", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassA\n"); });
    });

    // Pass B: Reads imageA, Writes imageB
    graph.addPass("PassB", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageA", ResourceUsage::ShaderRead);
        b.write("imageB", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassB\n"); });
    });

    // Pass C: Reads imageB, Writes imageC
    graph.addPass("PassC", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageB", ResourceUsage::ShaderRead);
        b.write("imageC", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassC\n"); });
    });

    // Present Pass: Reads imageC, Writes swapchain
    graph.addPass("Present", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageC", ResourceUsage::ShaderRead);
        b.write("swapchain", ResourceUsage::Present);
        b.execute([]() { fmt::print("  Executing Present\n"); });
    });

    graph.build();
    graph.debugPrint();
    graph.exportDot("test1_linear_chain.dot");

    graph.compile();
    graph.execute("test1_barriers");

    fmt::print("Expected: All 4 passes reachable\n");
    fmt::print("Generated: test1_linear_chain.dot, barrier_sequence.dot\n");
}

// Test 2: Diamond pattern A -> (B, C) -> D -> Present
void testDiamond() {
    fmt::print("\n=== Test 2: Diamond Pattern ===\n");

    TestRenderGraph graph;
    MockVulkanContext ctx;
    graph.init(&ctx);

    // Create resources
    graph.createImage("imageA", {1920, 1080}, false);
    graph.createImage("imageB", {1920, 1080}, false);
    graph.createImage("imageC", {1920, 1080}, false);
    graph.createImage("imageD", {1920, 1080}, false);

    // Import swapchain with PRESENT_SRC → PRESENT_SRC constraint (acquired from vkAcquireNextImageKHR)
    graph.importImage("swapchain", nullptr,
        ImageLayout::PresentSrc,      // Initial layout (from vkAcquireNextImageKHR)
        ImageLayout::PresentSrc,      // Final layout (REQUIRED for vkQueuePresentKHR)
        PipelineStage::TopOfPipe,
        PipelineStage::BottomOfPipe
    );

    // Pass A: Writes imageA
    graph.addPass("PassA", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("imageA", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassA\n"); });
    });

    // Pass B: Reads imageA, Writes imageB
    graph.addPass("PassB", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageA", ResourceUsage::ShaderRead);
        b.write("imageB", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassB\n"); });
    });

    // Pass C: Reads imageA, Writes imageC
    graph.addPass("PassC", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageA", ResourceUsage::ShaderRead);
        b.write("imageC", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassC\n"); });
    });

    // Pass D: Reads imageB and imageC, Writes imageD
    graph.addPass("PassD", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageB", ResourceUsage::ShaderRead);
        b.read("imageC", ResourceUsage::ShaderRead);
        b.write("imageD", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassD\n"); });
    });

    // Present Pass: Reads imageD, Writes swapchain
    graph.addPass("Present", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageD", ResourceUsage::ShaderRead);
        b.write("swapchain", ResourceUsage::Present);
        b.execute([]() { fmt::print("  Executing Present\n"); });
    });

    graph.build();
    graph.debugPrint();
    graph.exportDot("test2_diamond.dot");
    graph.compile();
    graph.execute("test2_barriers");

    fmt::print("Expected: All 5 passes reachable (diamond pattern)\n");
    fmt::print("Generated: test2_diamond.dot\n");
}

// Test 3: Unreachable passes - A -> B -> Present, and isolated C -> D (should be culled)
void testUnreachable() {
    fmt::print("\n=== Test 3: Unreachable Passes ===\n");

    TestRenderGraph graph;
    MockVulkanContext ctx;
    graph.init(&ctx);

    // Create resources
    graph.createImage("imageA", {1920, 1080}, false);
    graph.createImage("imageB", {1920, 1080}, false);
    graph.createImage("imageC", {1920, 1080}, false);
    graph.createImage("imageD", {1920, 1080}, false);

    // Import swapchain with PRESENT_SRC → PRESENT_SRC constraint (acquired from vkAcquireNextImageKHR)
    graph.importImage("swapchain", nullptr,
        ImageLayout::PresentSrc,      // Initial layout (from vkAcquireNextImageKHR)
        ImageLayout::PresentSrc,      // Final layout (REQUIRED for vkQueuePresentKHR)
        PipelineStage::TopOfPipe,
        PipelineStage::BottomOfPipe
    );

    // Reachable chain
    graph.addPass("PassA", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("imageA", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassA\n"); });
    });

    graph.addPass("PassB", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageA", ResourceUsage::ShaderRead);
        b.write("imageB", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassB\n"); });
    });

    graph.addPass("Present", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageB", ResourceUsage::ShaderRead);
        b.write("swapchain", ResourceUsage::Present);
        b.execute([]() { fmt::print("  Executing Present\n"); });
    });

    // Unreachable isolated chain
    graph.addPass("PassC", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("imageC", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassC (SHOULD BE CULLED)\n"); });
    });

    graph.addPass("PassD", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageC", ResourceUsage::ShaderRead);
        b.write("imageD", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassD (SHOULD BE CULLED)\n"); });
    });

    graph.build();
    graph.debugPrint();
    graph.exportDot("test3_unreachable.dot");
    graph.compile();
    graph.execute("test3_barriers");

    fmt::print("Expected: PassA, PassB, Present reachable; PassC, PassD culled\n");
    fmt::print("Generated: test3_unreachable.dot\n");
}

// Test 4: Multi-Present - A -> B -> Present1, A -> C -> Present2
void testMultiPresent() {
    fmt::print("\n=== Test 4: Multi-Present ===\n");

    TestRenderGraph graph;
    MockVulkanContext ctx;
    graph.init(&ctx);

    // Create resources
    graph.createImage("imageA", {1920, 1080}, false);
    graph.createImage("imageB", {1920, 1080}, false);
    graph.createImage("imageC", {1920, 1080}, false);

    // Import both swapchains with PRESENT_SRC → PRESENT_SRC constraint
    graph.importImage("swapchain1", nullptr,
        ImageLayout::PresentSrc,      // Initial layout (from vkAcquireNextImageKHR)
        ImageLayout::PresentSrc,      // Final layout (REQUIRED for vkQueuePresentKHR)
        PipelineStage::TopOfPipe,
        PipelineStage::BottomOfPipe
    );
    graph.importImage("swapchain2", nullptr,
        ImageLayout::PresentSrc,      // Initial layout (from vkAcquireNextImageKHR)
        ImageLayout::PresentSrc,      // Final layout (REQUIRED for vkQueuePresentKHR)
        PipelineStage::TopOfPipe,
        PipelineStage::BottomOfPipe
    );

    // Pass A: Writes imageA
    graph.addPass("PassA", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("imageA", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassA\n"); });
    });

    // Branch 1: A -> B -> Present1
    graph.addPass("PassB", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageA", ResourceUsage::ShaderRead);
        b.write("imageB", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassB\n"); });
    });

    graph.addPass("Present1", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageB", ResourceUsage::ShaderRead);
        b.write("swapchain1", ResourceUsage::Present);
        b.execute([]() { fmt::print("  Executing Present1\n"); });
    });

    // Branch 2: A -> C -> Present2
    graph.addPass("PassC", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageA", ResourceUsage::ShaderRead);
        b.write("imageC", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassC\n"); });
    });

    graph.addPass("Present2", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageC", ResourceUsage::ShaderRead);
        b.write("swapchain2", ResourceUsage::Present);
        b.execute([]() { fmt::print("  Executing Present2\n"); });
    });

    graph.build();
    graph.debugPrint();
    graph.exportDot("test4_multi_present.dot");
    graph.compile();
    graph.execute("test4_barriers");

    fmt::print("Expected: All 5 passes reachable (2 Present endpoints)\n");
    fmt::print("Generated: test4_multi_present.dot\n");
}

// Test 5: Complex graph with multiple dependencies
void testComplex() {
    fmt::print("\n=== Test 5: Complex Graph ===\n");

    TestRenderGraph graph;
    MockVulkanContext ctx;
    graph.init(&ctx);

    // Create resources
    graph.createImage("depth", {1920, 1080}, false);
    graph.createImage("gbuffer", {1920, 1080}, false);
    graph.createImage("lighting", {1920, 1080}, false);
    graph.createImage("postfx", {1920, 1080}, false);

    // Import swapchain with PRESENT_SRC → PRESENT_SRC constraint (acquired from vkAcquireNextImageKHR)
    graph.importImage("swapchain", nullptr,
        ImageLayout::PresentSrc,      // Initial layout (from vkAcquireNextImageKHR)
        ImageLayout::PresentSrc,      // Final layout (REQUIRED for vkQueuePresentKHR)
        PipelineStage::TopOfPipe,
        PipelineStage::BottomOfPipe
    );

    // GBuffer Pass: Writes depth + gbuffer
    graph.addPass("GBuffer", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("depth", ResourceUsage::DepthAttachment);
        b.write("gbuffer", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing GBuffer\n"); });
    });

    // Lighting Pass: Reads depth + gbuffer, Writes lighting
    graph.addPass("Lighting", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("depth", ResourceUsage::ShaderRead);
        b.read("gbuffer", ResourceUsage::ShaderRead);
        b.write("lighting", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing Lighting\n"); });
    });

    // PostFX Pass: Reads lighting, Writes postfx
    graph.addComputePass("PostFX", [](TestRenderGraph::PassBuilder& b, MockComputePass& p) {
        b.read("lighting", ResourceUsage::ShaderRead);
        b.write("postfx", ResourceUsage::ShaderWrite);
        b.execute([]() { fmt::print("  Executing PostFX\n"); });
    });

    // Present Pass: Reads postfx, Writes swapchain
    graph.addPass("Present", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("postfx", ResourceUsage::ShaderRead);
        b.write("swapchain", ResourceUsage::Present);
        b.execute([]() { fmt::print("  Executing Present\n"); });
    });

    graph.build();
    graph.debugPrint();
    graph.exportDot("test5_complex.dot");

    // NEW: Test barrier generation
    graph.compile();
    graph.execute("test5_barriers");

    fmt::print("Expected: All 4 passes reachable (GBuffer, Lighting, PostFX, Present)\n");
    fmt::print("Generated: test5_complex.dot and barrier_sequence.txt\n");
}

// Test 6: History Resources (Temporal Anti-Aliasing scenario)
void testHistoryResources() {
    fmt::print("\n=== Test 6: History Resources (TAA) ===\n");

    TestRenderGraph graph;
    MockVulkanContext ctx;
    graph.init(&ctx);

    // Import history buffer (from previous frame) - simulates persistent resource across frames
    // In real engine: this would be the output of TAA from frame N-1
    graph.importImage("historyColor", nullptr,
        ImageLayout::ShaderReadOnly,    // Last frame left it in ShaderReadOnly
        ImageLayout::ShaderReadOnly,    // This frame should preserve it for next frame
        PipelineStage::FragmentShader,
        PipelineStage::FragmentShader
    );

    // Import motion vectors (generated during previous frame's GBuffer pass)
    graph.importImage("prevMotionVectors", nullptr,
        ImageLayout::ShaderReadOnly,
        ImageLayout::ShaderReadOnly,
        PipelineStage::FragmentShader,
        PipelineStage::FragmentShader
    );

    // Create current frame resources
    graph.createImage("sceneColor", {1920, 1080}, false);      // Current frame render
    graph.createImage("taaOutput", {1920, 1080}, false);       // TAA accumulated result (becomes next frame's history)

    // Import swapchain
    graph.importImage("swapchain", nullptr,
        ImageLayout::PresentSrc,
        ImageLayout::PresentSrc,
        PipelineStage::TopOfPipe,
        PipelineStage::BottomOfPipe
    );

    // Scene Render Pass: Draw current frame geometry
    graph.addPass("SceneRender", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("sceneColor", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing SceneRender\n"); });
    });

    // TAA Compute Pass: Blend current frame with history using previous frame's motion vectors
    // Reads: sceneColor (current N), historyColor (accumulated N-1), prevMotionVectors (from N-1)
    // Writes: taaOutput (new accumulated result - becomes next frame's historyColor)
    graph.addComputePass("TAA", [](TestRenderGraph::PassBuilder& b, MockComputePass& p) {
        b.read("sceneColor", ResourceUsage::ShaderRead);
        b.read("historyColor", ResourceUsage::ShaderRead);        // Read frame N-1 history
        b.read("prevMotionVectors", ResourceUsage::ShaderRead);   // Use frame N-1 motion for reprojection
        b.write("taaOutput", ResourceUsage::ShaderWrite);         // Output for frame N
        b.execute([]() { fmt::print("  Executing TAA (temporal accumulation)\n"); });
    });

    // Present: Display TAA result
    graph.addPass("Present", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("taaOutput", ResourceUsage::ShaderRead);
        b.write("swapchain", ResourceUsage::Present);
        b.execute([]() { fmt::print("  Executing Present\n"); });
    });

    graph.build();
    graph.debugPrint();
    graph.exportDot("test6_history_taa.dot");
    graph.compile();
    graph.execute("test6_barriers");

    fmt::print("Expected: SceneRender → TAA (reads history) → Present\n");
    fmt::print("Note: historyColor and prevMotionVectors are external persistent resources from frame N-1\n");
    fmt::print("Note: taaOutput (frame N) becomes next frame's historyColor (frame N+1)\n");
    fmt::print("Generated: test6_history_taa.dot\n");
}

// Test 7: Optimization - Resource locality
void testResourceLocality() {
    fmt::print("\n=== Test 7: Resource Locality Optimization ===\n");

    TestRenderGraph graph;
    MockVulkanContext ctx;
    graph.init(&ctx);

    // Create resources
    graph.createImage("resA", {1920, 1080}, false);
    graph.createImage("resB", {1920, 1080}, false);
    graph.createImage("resC", {1920, 1080}, false);
    graph.createImage("resD", {1920, 1080}, false);

    // Import swapchain with PRESENT_SRC → PRESENT_SRC constraint (acquired from vkAcquireNextImageKHR)
    graph.importImage("swapchain", nullptr,
        ImageLayout::PresentSrc,      // Initial layout (from vkAcquireNextImageKHR)
        ImageLayout::PresentSrc,      // Final layout (REQUIRED for vkQueuePresentKHR)
        PipelineStage::TopOfPipe,
        PipelineStage::BottomOfPipe
    );

    // Pass1: Writes resA
    graph.addPass("Pass1", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("resA", ResourceUsage::ColorAttachment);
    });

    // Pass2: Reads resA, Writes resB (should be adjacent to Pass1)
    graph.addPass("Pass2", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("resA", ResourceUsage::ShaderRead);
        b.write("resB", ResourceUsage::ColorAttachment);
    });

    // Pass3: Writes resC (independent, can float)
    graph.addPass("Pass3", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("resC", ResourceUsage::ColorAttachment);
    });

    // Pass4: Reads resB, Writes resD (should follow Pass2)
    graph.addPass("Pass4", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("resB", ResourceUsage::ShaderRead);
        b.write("resD", ResourceUsage::ColorAttachment);
    });

    // Pass5: Reads resC + resD, Writes swapchain
    graph.addPass("Present", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("resC", ResourceUsage::ShaderRead);
        b.read("resD", ResourceUsage::ShaderRead);
        b.write("swapchain", ResourceUsage::Present);
    });

    graph.build();
    graph.debugPrint();
    graph.exportDot("test7_resource_locality.dot");
    graph.compile();
    graph.execute("test7_barriers");

    fmt::print("Expected: Pass1 → Pass2 → Pass4 → Pass3 → Present (locality optimized)\n");
    fmt::print("Or: Pass1 → Pass2 → Pass3 → Pass4 → Present\n");
    fmt::print("Generated: test7_resource_locality.dot\n");
}

// Test 8: Multi-branch convergence - (A, B, C) → D → E → Present
void testMultiBranchConvergence() {
    fmt::print("\n=== Test 8: Multi-Branch Convergence ===\n");

    TestRenderGraph graph;
    MockVulkanContext ctx;
    graph.init(&ctx);

    // Create resources
    graph.createImage("imageA", {1920, 1080}, false);
    graph.createImage("imageB", {1920, 1080}, false);
    graph.createImage("imageC", {1920, 1080}, false);
    graph.createImage("imageD", {1920, 1080}, false);
    graph.createImage("imageE", {1920, 1080}, false);

    // Import swapchain with PRESENT_SRC → PRESENT_SRC constraint (acquired from vkAcquireNextImageKHR)
    graph.importImage("swapchain", nullptr,
        ImageLayout::PresentSrc,      // Initial layout (from vkAcquireNextImageKHR)
        ImageLayout::PresentSrc,      // Final layout (REQUIRED for vkQueuePresentKHR)
        PipelineStage::TopOfPipe,
        PipelineStage::BottomOfPipe
    );

    // Three independent passes
    graph.addPass("PassA", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("imageA", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassA\n"); });
    });

    graph.addPass("PassB", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("imageB", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassB\n"); });
    });

    graph.addPass("PassC", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("imageC", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassC\n"); });
    });

    // Convergence pass: reads all three
    graph.addPass("PassD", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageA", ResourceUsage::ShaderRead);
        b.read("imageB", ResourceUsage::ShaderRead);
        b.read("imageC", ResourceUsage::ShaderRead);
        b.write("imageD", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassD\n"); });
    });

    // Pass E
    graph.addPass("PassE", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageD", ResourceUsage::ShaderRead);
        b.write("imageE", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing PassE\n"); });
    });

    // Present
    graph.addPass("Present", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("imageE", ResourceUsage::ShaderRead);
        b.write("swapchain", ResourceUsage::Present);
        b.execute([]() { fmt::print("  Executing Present\n"); });
    });

    graph.build();
    graph.debugPrint();
    graph.exportDot("test8_multi_convergence.dot");
    graph.compile();
    graph.execute("test8_barriers");

    fmt::print("Expected: All 6 passes reachable, PassD depends on A+B+C\n");
    fmt::print("Generated: test8_multi_convergence.dot\n");
}

// Test 9: Deferred Rendering with Compute - GBuffer → (Lighting, SSAO) → Combine → PostFX → Present
void testDeferredWithCompute() {
    fmt::print("\n=== Test 9: Deferred Rendering with Compute Passes ===\n");

    TestRenderGraph graph;
    MockVulkanContext ctx;
    graph.init(&ctx);

    // Create resources - images
    graph.createImage("depth", {1920, 1080}, false);
    graph.createImage("albedo", {1920, 1080}, false);
    graph.createImage("normal", {1920, 1080}, false);
    graph.createImage("lighting", {1920, 1080}, false);
    graph.createImage("ssao", {1920, 1080}, false);
    graph.createImage("combined", {1920, 1080}, false);
    graph.createImage("postfx", {1920, 1080}, false);

    // Import swapchain with PRESENT_SRC → PRESENT_SRC constraint (acquired from vkAcquireNextImageKHR)
    graph.importImage("swapchain", nullptr,
        ImageLayout::PresentSrc,      // Initial layout (from vkAcquireNextImageKHR)
        ImageLayout::PresentSrc,      // Final layout (REQUIRED for vkQueuePresentKHR)
        PipelineStage::TopOfPipe,
        PipelineStage::BottomOfPipe
    );

    // Create resources - buffers (for compute passes)
    graph.createBuffer("lightData", {4096}, false);

    // GBuffer Pass: Writes depth, albedo, normal
    graph.addPass("GBuffer", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("depth", ResourceUsage::DepthAttachment);
        b.write("albedo", ResourceUsage::ColorAttachment);
        b.write("normal", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing GBuffer\n"); });
    });

    // Lighting Compute Pass: Reads depth, albedo, normal, lightData buffer → Writes lighting
    graph.addComputePass("Lighting", [](TestRenderGraph::PassBuilder& b, MockComputePass& p) {
        b.read("depth", ResourceUsage::ShaderRead);
        b.read("albedo", ResourceUsage::ShaderRead);
        b.read("normal", ResourceUsage::ShaderRead);
        b.read("lightData", ResourceUsage::ShaderRead);
        b.write("lighting", ResourceUsage::ShaderWrite);
        b.execute([]() { fmt::print("  Executing Lighting (Compute)\n"); });
    });

    // SSAO Compute Pass: Reads depth, normal → Writes ssao
    graph.addComputePass("SSAO", [](TestRenderGraph::PassBuilder& b, MockComputePass& p) {
        b.read("depth", ResourceUsage::ShaderRead);
        b.read("normal", ResourceUsage::ShaderRead);
        b.write("ssao", ResourceUsage::ShaderWrite);
        b.execute([]() { fmt::print("  Executing SSAO (Compute)\n"); });
    });

    // Combine Graphics Pass: Reads lighting, ssao → Writes combined
    graph.addPass("Combine", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("lighting", ResourceUsage::ShaderRead);
        b.read("ssao", ResourceUsage::ShaderRead);
        b.write("combined", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing Combine\n"); });
    });

    // PostFX Compute Pass: Reads combined → Writes postfx
    graph.addComputePass("PostFX", [](TestRenderGraph::PassBuilder& b, MockComputePass& p) {
        b.read("combined", ResourceUsage::ShaderRead);
        b.write("postfx", ResourceUsage::ShaderWrite);
        b.execute([]() { fmt::print("  Executing PostFX (Compute)\n"); });
    });

    // Present Pass
    graph.addPass("Present", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("postfx", ResourceUsage::ShaderRead);
        b.write("swapchain", ResourceUsage::Present);
        b.execute([]() { fmt::print("  Executing Present\n"); });
    });

    graph.build();
    graph.debugPrint();
    graph.exportDot("test9_deferred_compute.dot");
    graph.compile();
    graph.execute("test9_barriers");

    fmt::print("Expected: All 6 passes reachable, GBuffer feeds both Lighting and SSAO compute\n");
    fmt::print("Generated: test9_deferred_compute.dot\n");
}

// Test 10: External resources with compute - Import external buffer/image
void testExternalComputeResources() {
    fmt::print("\n=== Test 10: External Resources with Compute ===\n");

    TestRenderGraph graph;
    MockVulkanContext ctx;
    graph.init(&ctx);

    // Import external resources (simulating GPU-uploaded data)
    graph.importImage("externalTexture", nullptr);
    graph.importBuffer("externalVertexBuffer", nullptr);
    graph.importBuffer("externalUniformBuffer", nullptr);

    // Create internal resources
    graph.createImage("processed", {1920, 1080}, false);
    graph.createImage("final", {1920, 1080}, false);

    // Import swapchain with PRESENT_SRC → PRESENT_SRC constraint (acquired from vkAcquireNextImageKHR)
    graph.importImage("swapchain", nullptr,
        ImageLayout::PresentSrc,      // Initial layout (from vkAcquireNextImageKHR)
        ImageLayout::PresentSrc,      // Final layout (REQUIRED for vkQueuePresentKHR)
        PipelineStage::TopOfPipe,
        PipelineStage::BottomOfPipe
    );

    graph.createBuffer("computeResult", {8192}, false);

    // Compute Pass 1: Reads external texture and uniform buffer → Writes to compute result buffer
    graph.addComputePass("PreProcess", [](TestRenderGraph::PassBuilder& b, MockComputePass& p) {
        b.read("externalTexture", ResourceUsage::ShaderRead);
        b.read("externalUniformBuffer", ResourceUsage::ShaderRead);
        b.write("computeResult", ResourceUsage::ShaderWrite);
        b.execute([]() { fmt::print("  Executing PreProcess (Compute)\n"); });
    });

    // Graphics Pass: Reads external vertex buffer and compute result → Writes processed image
    graph.addPass("Render", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("externalVertexBuffer", ResourceUsage::ShaderRead);
        b.read("computeResult", ResourceUsage::ShaderRead);
        b.write("processed", ResourceUsage::ColorAttachment);
        b.execute([]() { fmt::print("  Executing Render\n"); });
    });

    // Compute Pass 2: Reads processed image → Writes final
    graph.addComputePass("PostProcess", [](TestRenderGraph::PassBuilder& b, MockComputePass& p) {
        b.read("processed", ResourceUsage::ShaderRead);
        b.write("final", ResourceUsage::ShaderWrite);
        b.execute([]() { fmt::print("  Executing PostProcess (Compute)\n"); });
    });

    // Present
    graph.addPass("Present", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("final", ResourceUsage::ShaderRead);
        b.write("swapchain", ResourceUsage::Present);
        b.execute([]() { fmt::print("  Executing Present\n"); });
    });

    graph.build();
    graph.debugPrint();
    graph.exportDot("test10_external_compute.dot");
    graph.compile();
    graph.execute("test10_barriers");

    fmt::print("Expected: All 4 passes reachable, external resources correctly handled\n");
    fmt::print("Generated: test10_external_compute.dot\n");
}

// Test 11: Very complex graph with 10+ passes and mixed dependencies
void testVeryComplexGraph() {
    fmt::print("\n=== Test 11: Very Complex Graph (10+ passes) ===\n");

    TestRenderGraph graph;
    MockVulkanContext ctx;
    graph.init(&ctx);

    // Resources
    graph.createImage("image0", {1920, 1080}, false);
    graph.createImage("image1", {1920, 1080}, false);
    graph.createImage("image2", {1920, 1080}, false);
    graph.createImage("image3", {1920, 1080}, false);
    graph.createImage("image4", {1920, 1080}, false);
    graph.createImage("image5", {1920, 1080}, false);
    graph.createImage("image6", {1920, 1080}, false);
    graph.createImage("image7", {1920, 1080}, false);
    graph.createImage("image8", {1920, 1080}, false);
    graph.createImage("image9", {1920, 1080}, false);

    // Import swapchain with PRESENT_SRC → PRESENT_SRC constraint (acquired from vkAcquireNextImageKHR)
    graph.importImage("swapchain", nullptr,
        ImageLayout::PresentSrc,      // Initial layout (from vkAcquireNextImageKHR)
        ImageLayout::PresentSrc,      // Final layout (REQUIRED for vkQueuePresentKHR)
        PipelineStage::TopOfPipe,
        PipelineStage::BottomOfPipe
    );

    // Pass structure:
    // P0 → image0
    // P1 → image1
    // P2(image0, image1) → image2
    // P3(image2) → image3
    // P4(image2) → image4
    // P5(image3, image4) → image5
    // P6 → image6 (independent)
    // P7(image5) → image7
    // P8(image5, image6) → image8
    // P9(image7, image8) → image9
    // P10(image9) → swapchain (Present)

    graph.addPass("Pass0", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("image0", ResourceUsage::ColorAttachment);
    });

    graph.addPass("Pass1", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("image1", ResourceUsage::ColorAttachment);
    });

    graph.addComputePass("Pass2", [](TestRenderGraph::PassBuilder& b, MockComputePass& p) {
        b.read("image0", ResourceUsage::ShaderRead);
        b.read("image1", ResourceUsage::ShaderRead);
        b.write("image2", ResourceUsage::ShaderWrite);
    });

    graph.addPass("Pass3", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("image2", ResourceUsage::ShaderRead);
        b.write("image3", ResourceUsage::ColorAttachment);
    });

    graph.addComputePass("Pass4", [](TestRenderGraph::PassBuilder& b, MockComputePass& p) {
        b.read("image2", ResourceUsage::ShaderRead);
        b.write("image4", ResourceUsage::ShaderWrite);
    });

    graph.addPass("Pass5", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("image3", ResourceUsage::ShaderRead);
        b.read("image4", ResourceUsage::ShaderRead);
        b.write("image5", ResourceUsage::ColorAttachment);
    });

    graph.addPass("Pass6", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.write("image6", ResourceUsage::ColorAttachment);
    });

    graph.addComputePass("Pass7", [](TestRenderGraph::PassBuilder& b, MockComputePass& p) {
        b.read("image5", ResourceUsage::ShaderRead);
        b.write("image7", ResourceUsage::ShaderWrite);
    });

    graph.addPass("Pass8", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("image5", ResourceUsage::ShaderRead);
        b.read("image6", ResourceUsage::ShaderRead);
        b.write("image8", ResourceUsage::ColorAttachment);
    });

    graph.addComputePass("Pass9", [](TestRenderGraph::PassBuilder& b, MockComputePass& p) {
        b.read("image7", ResourceUsage::ShaderRead);
        b.read("image8", ResourceUsage::ShaderRead);
        b.write("image9", ResourceUsage::ShaderWrite);
    });

    graph.addPass("Present", [](TestRenderGraph::PassBuilder& b, MockRenderPass& p) {
        b.read("image9", ResourceUsage::ShaderRead);
        b.write("swapchain", ResourceUsage::Present);
    });

    graph.build();
    graph.debugPrint();
    graph.exportDot("test11_very_complex.dot");
    graph.compile();
    graph.execute("test11_barriers");

    fmt::print("Expected: All 11 passes reachable, complex dependency web\n");
    fmt::print("Generated: test11_very_complex.dot\n");
}

int main() {
    fmt::print("========================================\n");
    fmt::print("RenderGraph DAG Building Test Suite\n");
    fmt::print("========================================\n");

    // Basic tests
    testLinearChain();
    testDiamond();
    testUnreachable();
    testMultiPresent();
    testComplex();
    testHistoryResources();

    // Advanced tests
    testResourceLocality();
    testMultiBranchConvergence();
    testDeferredWithCompute();
    testExternalComputeResources();
    testVeryComplexGraph();

    fmt::print("\n========================================\n");
    fmt::print("All tests completed!\n");
    fmt::print("Generate visualizations with:\n");
    fmt::print("  dot -Tpng test*.dot -O\n");
    fmt::print("========================================\n");

    return 0;
}