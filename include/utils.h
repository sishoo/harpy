#pragma once

#include <stdio.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define VK_TRY(expr)                                                                     \
        do                                                                               \
        {                                                                                \
                VkResult VK_TRY_res = expr;                                              \
                if (VK_TRY_res != VK_SUCCESS)                                            \
                {                                                                        \
                        fprintf(stderr,                                                  \
                                "%s, was %d, not VK_SUCCESS.\n",                         \
                                #expr,                                                   \
                                VK_TRY_res);                                             \
                        abort();                                                         \
                }                                                                        \
        } while (0);
