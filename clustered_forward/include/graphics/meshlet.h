#pragma once

#include <stdint.h>

#include "math/vec.h"

typedef struct
{
        vec3_t cone_orientation;
        float cone_radius;
} meshlet_t;
