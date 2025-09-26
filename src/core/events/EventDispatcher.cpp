#include "EventDispatcher.hpp"

namespace violet {

eastl::unordered_map<size_t, eastl::vector<EventDispatcher::HandlerInfo>> EventDispatcher::s_handlers;
EventDispatcher::HandlerId EventDispatcher::s_nextHandlerId = 1;

}