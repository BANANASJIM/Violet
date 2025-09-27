#include "AssetBrowserLayer.hpp"
#include "core/FileSystem.hpp"
#include <imgui.h>
#include "core/Log.hpp"

namespace violet {

void AssetBrowserLayer::onAttach(VulkanContext* context, GLFWwindow* window) {
    UILayer::onAttach(context, window);
    assetDirectory = violet::FileSystem::resolveRelativePath("assets/");
    scanAssetDirectory();
    initialized = true;
}

void AssetBrowserLayer::onDetach() {
    rootNode = FileTreeNode();
    UILayer::onDetach();
}

void AssetBrowserLayer::scanAssetDirectory() {
    if (!FileSystem::exists(assetDirectory)) {
        VT_WARN("Asset directory not found: {}", assetDirectory.c_str());
        statusMessage = "Asset directory not found";
        return;
    }

    rootNode = FileTreeNode();
    rootNode.name = "Assets";
    rootNode.fullPath = assetDirectory;
    rootNode.isDirectory = true;

    buildFileTree(assetDirectory, rootNode);

    int totalAssets = 0;
    std::function<void(const FileTreeNode&)> countAssets = [&](const FileTreeNode& node) {
        if (!node.isDirectory) totalAssets++;
        for (const auto& child : node.children) {
            countAssets(child);
        }
    };
    countAssets(rootNode);

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "Found %d glTF files", totalAssets);
    statusMessage = buffer;
    VT_INFO("Found {} glTF files in {}", totalAssets, assetDirectory.c_str());
}

void AssetBrowserLayer::onImGuiRender() {
    ImGui::Begin("Asset Browser");

    if (ImGui::Button("Refresh")) {
        scanAssetDirectory();
    }

    ImGui::SameLine();
    ImGui::Text("%s", statusMessage.c_str());

    ImGui::Separator();

    renderTreeNode(rootNode);

    ImGui::End();
}

void AssetBrowserLayer::buildFileTree(const eastl::string& path, FileTreeNode& node) {
    auto entries = FileSystem::listDirectory(path, false);

    for (const auto& entry : entries) {
        FileTreeNode childNode;
        childNode.fullPath = entry;
        childNode.name = FileSystem::getFilename(entry);
        childNode.isDirectory = FileSystem::isDirectory(entry);

        if (childNode.isDirectory) {
            // Recursively build subdirectories
            buildFileTree(entry, childNode);
            node.children.push_back(childNode);
        } else {
            // Only add glTF files
            auto ext = FileSystem::getExtension(entry);
            if (ext == ".gltf" || ext == ".glb") {
                childNode.extension = ext;
                node.children.push_back(childNode);
            }
        }
    }
}

void AssetBrowserLayer::renderTreeNode(const FileTreeNode& node) {
    if (node.isDirectory) {
        // Render directory as tree node (collapsed by default)
        bool nodeOpen = ImGui::TreeNodeEx(node.name.c_str(), 0);

        if (nodeOpen) {
            for (const auto& child : node.children) {
                renderTreeNode(child);
            }
            ImGui::TreePop();
        }
    } else {
        // Render file as selectable item
        // Color code for glTF files (blue)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.8f, 1.0f, 1.0f));

        // Selectable item that can be dragged
        if (ImGui::Selectable(node.name.c_str())) {
            VT_INFO("File selected: {}", node.fullPath.c_str());
        }

        // Drag source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            // Add debug logging
            VT_INFO("AssetBrowser: Starting drag for {}", node.fullPath.c_str());
            ImGui::SetDragDropPayload("ASSET_PATH", node.fullPath.c_str(), node.fullPath.size() + 1);
            ImGui::Text("Drop to load: %s", node.name.c_str());
            ImGui::EndDragDropSource();
        }

        ImGui::PopStyleColor();
    }
}

}
