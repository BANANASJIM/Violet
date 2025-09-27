#pragma once

#include "UILayer.hpp"
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/functional.h>

namespace violet {

class AssetBrowserLayer : public UILayer {
public:
    AssetBrowserLayer() = default;
    ~AssetBrowserLayer() override = default;

    void onAttach(VulkanContext* context, GLFWwindow* window) override;
    void onDetach() override;
    void onImGuiRender() override;


private:
    void scanAssetDirectory();

private:
    struct AssetFile {
        eastl::string path;
        eastl::string name;
        eastl::string extension;
        bool isDirectory = false;
    };

    struct FileTreeNode {
        eastl::string name;
        eastl::string fullPath;
        bool isDirectory = false;
        eastl::string extension;
        eastl::vector<FileTreeNode> children;
    };

    FileTreeNode rootNode;
    eastl::string assetDirectory;
    eastl::string statusMessage = "Ready";

    void buildFileTree(const eastl::string& path, FileTreeNode& node);
    void renderTreeNode(const FileTreeNode& node);
};

}
