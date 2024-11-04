#pragma once

#include <stdio.h>

#include <vulkan/vulkan.h>

#define VK_TRY(expr)                                                                     \
        do                                                                               \
        {                                                                                \
                VkResult VK_TRY_res = expr;                                              \
                if (VK_TRY_res != VK_SUCCESS)                                            \
                {                                                                        \
                        fprintf(stderr,                                                  \
                                "VK_TRY FAILED WITH '%d' ON %d:\n\t%s\n",                \
                                VK_TRY_res,                                              \
                                __LINE__,                                                \
                                #expr);                                                  \
                        abort();                                                         \
                }                                                                        \
        } while (0);
