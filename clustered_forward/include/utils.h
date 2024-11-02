#pragma once

#include <stdio.h>

#define VK_TRY(expr)                                                           \
        do                                                                     \
        {                                                                      \
                if (expr != VK_SUCCESS)                                        \
                {                                                              \
                        fprintf(stderr, #expr " was not a VK_SUCCESS.\n");     \
                        abort();                                               \
                }                                                              \
        } while (0);
