#include "MeshManager.hpp"
#include "core/Log.hpp"
#include "renderer/vulkan/VulkanContext.hpp"
#include "resource/Mesh.hpp"

namespace violet {

MeshManager::~MeshManager() {
    cleanup();
}

void MeshManager::init(VulkanContext* ctx) {
    context = ctx;

    meshSlots.reserve(128);
    freeSlots.reserve(32);

    violet::Log::info("MeshManager", "Initialized");
}

void MeshManager::cleanup() {
    for (auto& slot : meshSlots) {
        if (slot.inUse && slot.mesh) {
            slot.mesh->cleanup();
            slot.mesh.reset();
        }
    }
    meshSlots.clear();
    freeSlots.clear();

    violet::Log::info("MeshManager", "Cleaned up all meshes");
}

MeshHandle MeshManager::addMesh(eastl::unique_ptr<Mesh> mesh) {
    if (!mesh) {
        return MeshHandle{};
    }

    uint32_t index;
    if (!freeSlots.empty()) {
        index = freeSlots.back();
        freeSlots.pop_back();
    } else {
        index = nextSlot++;
        meshSlots.resize(nextSlot);
    }

    MeshSlot& slot = meshSlots[index];
    slot.mesh = eastl::move(mesh);
    slot.generation++;
    slot.inUse = true;

    return MeshHandle{index, slot.generation};
}

void MeshManager::removeMesh(MeshHandle handle) {
    if (!isValidHandle(handle)) {
        return;
    }

    MeshSlot& slot = meshSlots[handle.index];
    if (slot.mesh) {
        slot.mesh->cleanup();
        slot.mesh.reset();
    }
    slot.inUse = false;
    freeSlots.push_back(handle.index);
}

Mesh* MeshManager::getMesh(MeshHandle handle) {
    if (!isValidHandle(handle)) {
        return nullptr;
    }
    return meshSlots[handle.index].mesh.get();
}

const Mesh* MeshManager::getMesh(MeshHandle handle) const {
    if (!isValidHandle(handle)) {
        return nullptr;
    }
    return meshSlots[handle.index].mesh.get();
}

bool MeshManager::isValidHandle(MeshHandle handle) const {
    if (!handle.isValid() || handle.index >= meshSlots.size()) {
        return false;
    }
    const MeshSlot& slot = meshSlots[handle.index];
    return slot.inUse && slot.generation == handle.generation;
}

size_t MeshManager::getMeshCount() const {
    size_t count = 0;
    for (const auto& slot : meshSlots) {
        if (slot.inUse) {
            count++;
        }
    }
    return count;
}

} // namespace violet
