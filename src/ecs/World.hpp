#pragma once

#include <entt/entt.hpp>
#include "Components.hpp"

namespace violet {

class World {
public:
    World() = default;
    ~World() = default;

    entt::entity createEntity() {
        return registry.create();
    }

    void destroyEntity(entt::entity entity) {
        registry.destroy(entity);
    }

    template<typename Component, typename... Args>
    Component& addComponent(entt::entity entity, Args&&... args) {
        return registry.emplace<Component>(entity, eastl::forward<Args>(args)...);
    }

    template<typename Component>
    void removeComponent(entt::entity entity) {
        registry.remove<Component>(entity);
    }

    template<typename Component>
    bool hasComponent(entt::entity entity) const {
        return registry.any_of<Component>(entity);
    }

    template<typename Component>
    Component& getComponent(entt::entity entity) {
        return registry.get<Component>(entity);
    }

    template<typename Component>
    const Component& getComponent(entt::entity entity) const {
        return registry.get<Component>(entity);
    }

    template<typename... Components>
    auto view() {
        return registry.view<Components...>();
    }

    template<typename... Components>
    auto view() const {
        return registry.view<Components...>();
    }

    size_t getEntityCount() const {
        return static_cast<size_t>(registry.storage<entt::entity>()->size());
    }

    bool isEntityValid(entt::entity entity) const {
        return registry.valid(entity);
    }

    entt::registry& getRegistry() { return registry; }
    const entt::registry& getRegistry() const { return registry; }

private:
    entt::registry registry;
};

}