#pragma once

#include "renderer/Vertex.hpp"
#include <EASTL/vector.h>

namespace violet {

class TestData {
public:
    static eastl::vector<Vertex> getCubeVertices();
    static eastl::vector<uint32_t> getCubeIndices();
};

}