#pragma once

#ifndef GLSL
#include <stdint.h>
#include "./math/vec.h"
#include "./math/quaternion.h"

typedef struct 
{
        float radius;
        vec3_t pos;
        quaternion3_t dir;
        uint32_t idx_meshlets, nmeshlets;
} object_t;

#else

struct object_t
{
        float radius;
        vec3 pos;
        quaternion3_t dir;
        uint idx_meshlets, nmeshlets;
};      

#endif