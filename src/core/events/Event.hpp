#pragma once

#include <cstdint>

namespace violet {

class Event {
public:
    virtual ~Event() = default;

    bool consumed = false;
    uint32_t timestamp = 0;

protected:
    Event() = default;
};

}