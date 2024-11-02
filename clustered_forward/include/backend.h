#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdlib.h>

#include "utils.h"

typedef struct
{
        VkInstance instance;
        VkPhysicalDevice pdevice;
        VkDevice ldevice;
        VkQueue queue;

        uint32_t nswapchain_images;
        VkSwapchainKHR swapchain;
        VkSurfaceKHR surface;

        int width, height;
        GLFWwindow *pwindow;
} vk_backend_t;

void vk_backend_init(
        vk_backend_t *pvk_backend, char *pname, int width, int height)
{
        pvk_backend->width  = width;
        pvk_backend->height = height;

        VkInstanceCreateInfo instance_info = {
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .pApplicationInfo =
                        &(VkApplicationInfo) {
                                .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                .pApplicationName   = pname,
                                .applicationVersion = VK_VERSION_1_0,
                                .pEngineName        = "engine",
                                .engineVersion      = VK_VERSION_1_0,
                                .apiVersion         = VK_API_VERSION_1_3},
                .enabledLayoutCount      = 1,
                .ppEnabledLayerNames     = {"VK_LAYER_KHRONOS_validation"},
                .enabledExtensionCount   = 1,
                .ppEnabledExtensionNames = {"VK_KHR_dynamic_rendering"}};

        VK_TRY(vkCreateInstance(&instance_info, NULL, &pvk_backend->instance));

        uint32_t npdevices = 0;
        VK_TRY(vkEnumeratePhysicalDevices(
                pvk_backend->instance, &npdevices, NULL));

        VkPhysicalDevice *ppdevices =
                malloc(sizeof(VkPhysicalDevice) * npdevices);

        VK_TRY(vkEnumeratePhysicalDevices(
                pvk_backend->instance, &npdevices, ppdevices));

        // TODO: finish this
        pvk_backend->pdevice = ppdevices[0];
#ifndef NDEBUG
        printf("pdevice: %s\n", &ppdevices[0]);
#endif

        VkDeviceCreateInfo device_info = {
                .sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos    = &(VkDeviceQueueCreateInfo) {
                           .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                           .queueFamilyIndex = 0,
                           .queueCount       = 1,
                           .pQueuePriorities = (float[]) {1.0f}}};

        VK_TRY(vkCreateDevice(
                pvk_backend->pdevice,
                &device_info,
                NULL,
                &pvk_backend->ldevice));

        vkGetDeviceQueue(pvk_backend->ldevice, 0, 0, &pvk_backend->queue);

        VK_TRY(glfwCreateWindowSurface(
                pvk_backend->instance,
                pvk_backend->pwindow,
                NULL,
                &pvk_backend->surface));

        VK_TRY(vkCreateSwapchainKHR(
                pvk_backend->ldevice,
                &(VkSwapchainCreateInfoKHR) {
                        .sType   = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                        .surface = pvk_backend->surface,
                        .minImageCount    = 1,
                        .imageFormat      = VK_FORMAT_R8G8B8A8_UNORM,
                        .imageColorSpace  = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
                        .imageExtent      = (VkExtent2D) {width, height},
                        .imageArrayLayers = 1,
                        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                        .queueFamilyIndexCount = 1,
                        .pQueueFamilyIndices    = {0},
                        .preTransform   = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                        .presentMode    = VK_PRESENT_MODE_MAILBOX_KHR,
                        .clipped        = VK_TRUE},
                NULL,
                &pvk_backend->swapchain));
}
