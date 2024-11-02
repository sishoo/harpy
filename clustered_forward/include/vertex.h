#pragma once

#include "math/vec.h"

#ifdef GLSL

struct vertex_t
{
        vec3 pos, norm;
        vec2 uv;
};

#else

typedef struct
{
        vec3_t pos, norm;
} vertex_t;

#endif