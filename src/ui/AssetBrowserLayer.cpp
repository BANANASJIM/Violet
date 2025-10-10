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
        violet::Log::warn("UI", "Asset directory not found: {}", assetDirectory.c_str());
        statusMessage = "Asset directory not found";
        return;
    }

    rootNode = FileTreeNode();
    rootNode.name = "Assets";
    rootNode.fullPath = assetDirectory;
    rootNode.isDirectory = true;

    buildFileTree(assetDirectory, rootNode);

    int totalAssets = 0;
    eastl::function<void(const FileTreeNode&)> countAssets = [&](const FileTreeNode& node) {
        if (!node.isDirectory) totalAssets++;
        for (const auto& child : node.children) {
            countAssets(child);
        }
    };
    countAssets(rootNode);

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "Found %d assets", totalAssets);
    statusMessage = buffer;
    violet::Log::info("UI", "Found {} assets in {}", totalAssets, assetDirectory.c_str());
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
            // Add glTF files and HDR files
            auto ext = FileSystem::getExtension(entry);
            if (ext == ".gltf" || ext == ".glb" || ext == ".hdr") {
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
        // Render file as selectable item with file type-specific colors

        // Check file extension for color coding
        eastl::string ext = "";
        size_t dotPos = node.name.find_last_of('.');
        if (dotPos != eastl::string::npos) {
            ext = node.name.substr(dotPos);
        }

        ImVec4 fileColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // Default white
        eastl::string iconPrefix = "";

        if (ext == ".gltf" || ext == ".glb") {
            fileColor = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);  // Blue for 3D models
            iconPrefix = "🎯 ";
        } else if (ext == ".hdr") {
            fileColor = ImVec4(1.0f, 0.7f, 0.2f, 1.0f);  // Orange for HDR files
            iconPrefix = "🌅 ";
        } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
            fileColor = ImVec4(0.7f, 1.0f, 0.7f, 1.0f);  // Green for textures
            iconPrefix = "🖼️ ";
        }

        ImGui::PushStyleColor(ImGuiCol_Text, fileColor);

        // Display with icon prefix and color
        eastl::string displayName = iconPrefix + node.name;

        // Selectable item that can be dragged
        if (ImGui::Selectable(displayName.c_str())) {
            violet::Log::info("UI", "File selected: {}", node.fullPath.c_str());
        }

        // Drag source with improved visual feedback
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("ASSET_PATH", node.fullPath.c_str(), node.fullPath.size() + 1);

            // Different drop text based on file type
            if (ext == ".hdr") {
                ImGui::Text("🌅 Drop to load HDR environment: %s", node.name.c_str());
            } else if (ext == ".gltf" || ext == ".glb") {
                ImGui::Text("🎯 Drop to place model: %s", node.name.c_str());
            } else {
                ImGui::Text("Drop to load: %s", node.name.c_str());
            }
            ImGui::EndDragDropSource();
        }

        ImGui::PopStyleColor();
    }
}

}
