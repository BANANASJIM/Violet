#pragma once

#include "Event.hpp"
#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>
#include <EASTL/functional.h>
#include <EASTL/sort.h>
#include <EASTL/string.h>

namespace violet {

// Simple type identifier based on type name hash
template<typename T>
struct TypeId {
    static size_t getId() {
        static const size_t id = eastl::hash<eastl::string>{}(eastl::string(typeid(T).name()));
        return id;
    }
};

class EventDispatcher {
public:
    using HandlerId = size_t;

    template<typename EventType>
    static HandlerId subscribe(eastl::function<bool(const EventType&)> handler, int priority = 0) {
        static_assert(eastl::is_base_of_v<Event, EventType>, "EventType must inherit from Event");

        size_t typeId = TypeId<EventType>::getId();
        HandlerId id = s_nextHandlerId++;

        HandlerInfo info;
        info.id = id;
        info.priority = priority;
        info.handler = [handler](const Event& event) -> bool {
            return handler(static_cast<const EventType&>(event));
        };

        s_handlers[typeId].push_back(info);

        // Sort handlers by priority (higher priority first)
        eastl::sort(s_handlers[typeId].begin(), s_handlers[typeId].end(),
                   [](const HandlerInfo& a, const HandlerInfo& b) {
                       return a.priority > b.priority;
                   });

        return id;
    }

    template<typename EventType>
    static void unsubscribe(HandlerId handlerId) {
        size_t typeId = TypeId<EventType>::getId();
        auto it = s_handlers.find(typeId);
        if (it != s_handlers.end()) {
            auto& handlers = it->second;
            handlers.erase(eastl::remove_if(handlers.begin(), handlers.end(),
                          [handlerId](const HandlerInfo& info) {
                              return info.id == handlerId;
                          }), handlers.end());
        }
    }

    template<typename EventType>
    static void publish(const EventType& event) {
        static_assert(eastl::is_base_of_v<Event, EventType>, "EventType must inherit from Event");

        size_t typeId = TypeId<EventType>::getId();
        auto it = s_handlers.find(typeId);
        if (it != s_handlers.end()) {
            Event& mutableEvent = const_cast<EventType&>(event);

            for (const auto& handlerInfo : it->second) {
                if (mutableEvent.consumed) {
                    break; // Stop propagation if event is consumed
                }

                bool consumed = handlerInfo.handler(event);
                if (consumed) {
                    mutableEvent.consumed = true;
                }
            }
        }
    }

    static void clear() {
        s_handlers.clear();
        s_nextHandlerId = 1;
    }

private:
    struct HandlerInfo {
        HandlerId id;
        int priority;
        eastl::function<bool(const Event&)> handler;
    };

    static eastl::unordered_map<size_t, eastl::vector<HandlerInfo>> s_handlers;
    static HandlerId s_nextHandlerId;
};

}