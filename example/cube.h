#ifndef CUBE_H_20201230
#define CUBE_H_20201230

#include <vector>

struct Vertex {
    float x, y, z, w;  // Position
    float r, g, b, a;  // Color
};

const std::vector<Vertex> CUBE_VERTICES = {
        // red face
        {-1.f, -1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f},
        {-1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f},
        {1.f, -1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f},
        {1.f, -1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f},
        {-1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f},
        {1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f},
        // green face
        {-1.f, -1.f, -1.f, 1.f, 0.f, 1.f, 0.f, 1.f},
        {1.f, -1.f, -1.f, 1.f, 0.f, 1.f, 0.f, 1.f},
        {-1.f, 1.f, -1.f, 1.f, 0.f, 1.f, 0.f, 1.f},
        {-1.f, 1.f, -1.f, 1.f, 0.f, 1.f, 0.f, 1.f},
        {1.f, -1.f, -1.f, 1.f, 0.f, 1.f, 0.f, 1.f},
        {1.f, 1.f, -1.f, 1.f, 0.f, 1.f, 0.f, 1.f},
        // blue face
        {-1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f},
        {-1.f, -1.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f},
        {-1.f, 1.f, -1.f, 1.f, 0.f, 0.f, 1.f, 1.f},
        {-1.f, 1.f, -1.f, 1.f, 0.f, 0.f, 1.f, 1.f},
        {-1.f, -1.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f},
        {-1.f, -1.f, -1.f, 1.f, 0.f, 0.f, 1.f, 1.f},
        // yellow face
        {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 1.f},
        {1.f, 1.f, -1.f, 1.f, 1.f, 1.f, 0.f, 1.f},
        {1.f, -1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 1.f},
        {1.f, -1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 1.f},
        {1.f, 1.f, -1.f, 1.f, 1.f, 1.f, 0.f, 1.f},
        {1.f, -1.f, -1.f, 1.f, 1.f, 1.f, 0.f, 1.f},
        // magenta face
        {1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 1.f, 1.f},
        {-1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 1.f, 1.f},
        {1.f, 1.f, -1.f, 1.f, 1.f, 0.f, 1.f, 1.f},
        {1.f, 1.f, -1.f, 1.f, 1.f, 0.f, 1.f, 1.f},
        {-1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 1.f, 1.f},
        {-1.f, 1.f, -1.f, 1.f, 1.f, 0.f, 1.f, 1.f},
        // cyan face
        {1.f, -1.f, 1.f, 1.f, 0.f, 1.f, 1.f, 1.f},
        {1.f, -1.f, -1.f, 1.f, 0.f, 1.f, 1.f, 1.f},
        {-1.f, -1.f, 1.f, 1.f, 0.f, 1.f, 1.f, 1.f},
        {-1.f, -1.f, 1.f, 1.f, 0.f, 1.f, 1.f, 1.f},
        {1.f, -1.f, -1.f, 1.f, 0.f, 1.f, 1.f, 1.f},
        {-1.f, -1.f, -1.f, 1.f, 0.f, 1.f, 1.f, 1.f},
};

#endif /* end of include guard */
