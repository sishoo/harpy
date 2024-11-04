
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define VOXEL_TYPE_HARD 0b00
#define VOXEL_TYPE_SOFT 0b11
#define VOXEL_TYPE_FLUCUATE1 0b10
#define VOXEL_TYPE_FLUCUATE2 0b01

#define RENDERER_SZPUSH_CONSTANTS sizeof(float[36])

typedef struct
{
        float x, y, z;
} vec3_t;

typedef struct
{
        float mass;
        vec3_t position, velocity, acceleration;
} point_mass_t;

typedef struct
{
        float k, rest_distance;
} spring_t;

typedef struct
{
        uint32_t npoint_masses, nsprings;
        point_mass_t *ppoint_masses;
        spring_t *psprings;
} entity_t;

#include "include/utils.h"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NFRAMES_IN_FLIGHT 2
#define RENDERER_VK_TIMEOUT 9999999

#define RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE 2ULL
#define RENDERER_TIMELINE_FRAME_PRESENT_VALUE 3ULL
#define RENDERER_SWAPCHAIN_IMAGE_FORMAT VK_FORMAT_R8G8B8A8_UNORM

#define RENDERER_SZWORKGROUP_X 16
#define RENDERER_SZWORKGROUP_Y 16
#define RENDERER_SZWORKGROUP_Z 1

typedef struct
{
        VkFence fence;
        VkSemaphore time_sema, img_sema;
        VkCommandBuffer cmd_buf;
} frame_info_t;

typedef struct
{
        uint64_t nframe;

        VkPipelineLayout pipe_layout;
        VkPipeline physics_pipe, graphics_pipe;

        VkDescriptorSetLayout set_layout;
        VkDescriptorSet scene_desc;

        VkDeviceMemory scene_mem;
        VkBuffer scene_buf;
        uint32_t idx_geometry, idx_draw, idx_ndraw, idx_object, idx_light;

        VkCommandPool cmd_pool;
        VkCommandBuffer cmd_buf;

        frame_info_t pframe_infos[NFRAMES_IN_FLIGHT];

        float dt;
        float proj_mat[16], view_mat[16];

        /* backend */
        VkInstance instance;
        VkPhysicalDevice pdevice;
        VkDevice ldevice;

        uint32_t idx_qfam;
        VkQueue queue;

        VkSwapchainKHR swapchain;
        uint32_t nswapchain_images;
        VkImage *pswapchain_images;
        VkSurfaceKHR surface;

        int width, height;
        SDL_Window *pwin;
} renderer_t;

void renderer_init_backend(renderer_t *prender, char *pname, int width, int height)
{
        prender->width  = width;
        prender->height = height;

        // SDL
        if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
        {
                fprintf(stderr, "Cant init SDL.\n");
                abort();
        }

        prender->pwin = SDL_CreateWindow(
                pname,
                SDL_WINDOWPOS_UNDEFINED,
                SDL_WINDOWPOS_UNDEFINED,
                width,
                height,
                SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);
        if (!prender->pwin)
        {
                fprintf(stderr, "Cant init SDL window.\n");
                abort();
        }

        // Instance
        VkInstanceCreateInfo instance_info = {
                .sType                 = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .enabledLayerCount     = 0,
                .ppEnabledLayerNames   = (char *[]){"VK_LAYER_KHRONOS_validation"},
                .enabledExtensionCount = 2,
                .ppEnabledExtensionNames =
                        (char *[]){"VK_KHR_surface", "VK_KHR_win32_surface"}};
        VK_TRY(vkCreateInstance(&instance_info, NULL, &prender->instance));

        /* pdevice */
        uint32_t npdevices = 0;
        VK_TRY(vkEnumeratePhysicalDevices(prender->instance, &npdevices, NULL));
        VkPhysicalDevice *ppdevices = malloc(sizeof(VkPhysicalDevice) * npdevices);
        VK_TRY(vkEnumeratePhysicalDevices(prender->instance, &npdevices, ppdevices));
        prender->pdevice = ppdevices[0];

        /* ldevice */
        VkPhysicalDeviceTimelineSemaphoreFeatures timeline_feat = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
                .timelineSemaphore = VK_TRUE};

        VkPhysicalDeviceDynamicRenderingFeaturesKHR dyn_rendering_feat = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
                .pNext = &timeline_feat,
                .dynamicRendering = VK_TRUE};

        VkDeviceCreateInfo device_info = {
                .sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .pNext                = &dyn_rendering_feat,
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos =
                        &(VkDeviceQueueCreateInfo){
                                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                .queueFamilyIndex = 0,
                                .queueCount       = 1,
                                .pQueuePriorities = (float[]){1.0f}},
                .enabledExtensionCount   = 5,
                .ppEnabledExtensionNames = (char *[]){
                        "VK_KHR_swapchain",
                        "VK_KHR_dynamic_rendering",
                        "VK_KHR_depth_stencil_resolve",
                        "VK_KHR_create_renderpass2",
                        "VK_KHR_timeline_semaphore"}};
        VK_TRY(vkCreateDevice(prender->pdevice, &device_info, NULL, &prender->ldevice));

        /* queue */
        vkGetDeviceQueue(prender->ldevice, 0, 0, &prender->queue);

        /* swapchain */
        SDL_Vulkan_CreateSurface(prender->pwin, prender->instance, &prender->surface);

        // uint32_t nsurface_formats;
        // vkGetPhysicalDeviceSurfaceFormatsKHR(
        //         prender->pdevice, prender->surface, &nsurface_formats, NULL);
        // VkSurfaceFormatKHR *psurface_formats =
        //         malloc(sizeof(VkSurfaceFormatKHR) * nsurface_formats);
        // vkGetPhysicalDeviceSurfaceFormatsKHR(
        //         prender->pdevice, prender->surface, &nsurface_formats,
        //         psurface_formats);

        VK_TRY(vkCreateSwapchainKHR(
                prender->ldevice,
                &(VkSwapchainCreateInfoKHR){
                        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                        .surface          = prender->surface,
                        .minImageCount    = 2,
                        .imageFormat      = RENDERER_SWAPCHAIN_IMAGE_FORMAT,
                        .imageColorSpace  = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
                        .imageExtent      = (VkExtent2D){width, height},
                        .imageArrayLayers = 1,
                        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                        .queueFamilyIndexCount = 1,
                        .pQueueFamilyIndices   = (uint32_t[]){0},
                        .preTransform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                        .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                        .presentMode           = VK_PRESENT_MODE_MAILBOX_KHR,
                        .clipped               = VK_TRUE},
                NULL,
                &prender->swapchain));

        VK_TRY(vkGetSwapchainImagesKHR(
                prender->ldevice, prender->swapchain, &prender->nswapchain_images, NULL));
        prender->pswapchain_images = malloc(sizeof(VkImage) * prender->nswapchain_images);
        VK_TRY(vkGetSwapchainImagesKHR(
                prender->ldevice,
                prender->swapchain,
                &prender->nswapchain_images,
                prender->pswapchain_images));
}

void renderer_init_common(renderer_t *prender)
{
        VkDescriptorSetLayoutBinding binding = {
                .binding         = 0,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_ALL};

        VkDescriptorSetLayoutCreateInfo set_layout_info = {
                .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1,
                .pBindings    = &binding};

        VK_TRY(vkCreateDescriptorSetLayout(
                prender->ldevice, &set_layout_info, NULL, &prender->set_layout));

        VkPipelineLayoutCreateInfo pipe_layout_info = {
                .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount         = 1,
                .pSetLayouts            = &prender->set_layout,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges    = &(VkPushConstantRange){
                           .stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                      VK_SHADER_STAGE_FRAGMENT_BIT |
                                      VK_SHADER_STAGE_COMPUTE_BIT,
                           .offset = 0,
                           .size   = RENDERER_SZPUSH_CONSTANTS}};

        VK_TRY(vkCreatePipelineLayout(
                prender->ldevice, &pipe_layout_info, NULL, &prender->pipe_layout));
}

static VkShaderModule renderer_init_shader_module(
        renderer_t *prender, uint32_t *pcode, uint32_t sz)
{
        VkShaderModule module = VK_NULL_HANDLE;
        VK_TRY(vkCreateShaderModule(
                prender->ldevice,
                &(VkShaderModuleCreateInfo){
                        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                        .codeSize = sz,
                        .pCode    = pcode},
                NULL,
                &module));

        return module;
}

void renderer_init_graphics_pipes(renderer_t *prender)
{
        static uint32_t pvert_spv[] = {
#include "shader/spv/graphics.vert.spv"
        };

        static uint32_t pfrag_spv[] = {
#include "shader/spv/graphics.frag.spv"
        };

        VkShaderModule vert_module =
                renderer_init_shader_module(prender, pvert_spv, sizeof pvert_spv);

        VkShaderModule frag_module =
                renderer_init_shader_module(prender, pfrag_spv, sizeof pfrag_spv);

        VkPipelineShaderStageCreateInfo pstages[2] = {
                {.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                 .stage  = VK_SHADER_STAGE_VERTEX_BIT,
                 .module = vert_module,
                 .pName  = "main"},
                {.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                 .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
                 .module = frag_module,
                 .pName  = "main"}};

        VkPipelineRenderingCreateInfo render_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
                .colorAttachmentCount    = 1,
                .pColorAttachmentFormats = (VkFormat[]){RENDERER_SWAPCHAIN_IMAGE_FORMAT}};

        VkGraphicsPipelineCreateInfo pipe_info = {
                .sType      = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .pNext      = &render_info,
                .stageCount = 2,
                .pStages    = pstages,
                .layout     = prender->pipe_layout};

        pipe_info.pVertexInputState = &(VkPipelineVertexInputStateCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

        pipe_info.pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo){
                .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};

        pipe_info.pViewportState = &(VkPipelineViewportStateCreateInfo){
                .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .scissorCount  = 1};

        pipe_info.pRasterizationState = &(VkPipelineRasterizationStateCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .depthClampEnable        = VK_FALSE,
                .rasterizerDiscardEnable = VK_FALSE,
                .polygonMode             = VK_POLYGON_MODE_FILL,
                .cullMode                = VK_CULL_MODE_BACK_BIT,
                .frontFace               = VK_FRONT_FACE_CLOCKWISE,
                .depthBiasEnable         = VK_FALSE,
                .depthBiasClamp          = VK_FALSE,
                .lineWidth               = 1.0f};

        pipe_info.pMultisampleState = &(VkPipelineMultisampleStateCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

        pipe_info.pColorBlendState = &(VkPipelineColorBlendStateCreateInfo){
                .sType         = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .logicOpEnable = VK_FALSE,
                .logicOp       = VK_LOGIC_OP_COPY,
                .attachmentCount = 1,
                .pAttachments =
                        &(VkPipelineColorBlendAttachmentState){.blendEnable = VK_FALSE}};

        pipe_info.pDynamicState = &(VkPipelineDynamicStateCreateInfo){
                .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = 2,
                .pDynamicStates    = (VkDynamicState[]){
                        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}};

        VK_TRY(vkCreateGraphicsPipelines(
                prender->ldevice,
                VK_NULL_HANDLE,
                1,
                &pipe_info,
                NULL,
                &prender->graphics_pipe));
}

/*
void renderer_init_compute_pipes(renderer_t *prender)
{
        static uint32_t pphysics_spv[] = {
#include "shader/spv/physics.comp.spv"
        };

        VkShaderModule physics_module =
                init_shader_module(pphysics_spv, sizeof pphysics_spv);

        VkPipelineShaderStageCreateInfo physics_shader_info = {
                .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = physics_module,
                .pname  = "main"};

        VkPipelineInfo pipe_info = {
                .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage  = physics_shader_info,
                .layout = prender->pipe_layout};

        VK_TRY(vkCreateComputePipelines(
                prender->ldevice,
                VK_NULL_HANDLE,
                2,
                pipeline_infos,
                NULL,
                &prender->physics_pipe));
}
*/

void create_semaphore(renderer_t *prender, VkSemaphore *psema, uint64_t val, bool is_bin)
{
        VkSemaphoreCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

        if (is_bin)
        {
                VkSemaphoreTypeCreateInfo type_info = {
                        .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                        .initialValue  = val};

                info.pNext = &type_info;
        }

        VK_TRY(vkCreateSemaphore(prender->ldevice, &info, NULL, psema));
}

void create_command_buffers(
        renderer_t *prender,
        VkCommandBuffer *pcmd,
        uint32_t ncmd,
        VkCommandBufferLevel lvl)
{
        VkCommandBufferAllocateInfo info = {
                .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool        = prender->cmd_pool,
                .level              = lvl,
                .commandBufferCount = ncmd};
        VK_TRY(vkAllocateCommandBuffers(prender->ldevice, &info, pcmd));
}

void renderer_init_frame_infos(renderer_t *prender)
{
        VK_TRY(vkCreateCommandPool(
                prender->ldevice,
                &(VkCommandPoolCreateInfo){
                        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                        .queueFamilyIndex = prender->idx_qfam},
                NULL,
                &prender->cmd_pool));

        for (uint32_t i = 0; i < NFRAMES_IN_FLIGHT; i++)
        {
                frame_info_t *pframe_info = &prender->pframe_infos[i];

                create_semaphore(prender, &pframe_info->img_sema, 0, 0);
                create_semaphore(
                        prender,
                        &pframe_info->time_sema,
                        RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE,
                        1);

                create_command_buffers(
                        prender,
                        &pframe_info->cmd_buf,
                        1,
                        VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        }
}

void renderer_init(renderer_t *prender, char *pname, int width, int height)
{
        prender->nframe = 1;

        renderer_init_backend(prender, pname, width, height);
        renderer_init_common(prender);
        renderer_init_graphics_pipes(prender);
        // renderer_init_compute_pipes(prender);
        renderer_init_frame_infos(prender);
}

/*
void renderer_prepare(renderer_t *prender)
{

        uint32_t sz_meshes =
                prender->sz_meshes + sizeof(object_t) * prender->nmeshes;
        uint32_t sz_draws =
                prender->nobjects * sizeof(VkDrawIndexedIndirectCommand);
        uint32_t sz_objects = prender->nbojects * sizeof(object_t);
        VK_TRY(vkCreateBuffer(
                ldevice,
                &(VkBufferCreateInfo) {
                        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                        .size  = sz_meshes + sz_draws + sz_objects +
                                4 // 4 for the draw count
                },
                NULL,
                &prender->scene_buf));

        VK_TRY(vkBindBufferMemory(
                prender->ldevice,
                prender->scene_buf,
                prender->scene_mem,
                0));

        void *pmapped = NULL;
        VK_TRY(vkMapMemory(
                prender->ldevice,
                prender->scene_mem,
                0,
                prender->sz_meshes,
                0,
                &pmapped));

        memcpy(pmapped, pmeshes, prender->sz_meshes);

        vkUnmapMemory(prender->ldevice, prender->scene_mem);
}
*/

// void renderer_draw(renderer_t *prender)
// {
//         fprintf(stderr, "NFRAME: %llu\n", prender->nframe);

//         uint32_t idx_last  = prender->idx_frame;
//         prender->idx_frame = (prender->idx_frame + 1) % prender->nswapchain_images;
//         uint32_t idx_frame = prender->idx_frame;

//         frame_info_t *pframe_info = &prender->pframe_infos[idx_frame];
//         frame_info_t *plast_info  = &prender->pframe_infos[idx_last];

//         VkFence last_fence         = plast_info->fence;
//         VkSemaphore last_time_sema = plast_info->time_sema;

//         VkSemaphore time_sema   = pframe_info->time_sema;
//         VkSemaphore img_sema    = pframe_info->img_sema;
//         VkCommandBuffer cmd_buf = pframe_info->cmd_buf;

//         VK_TRY(vkWaitForFences(
//                 prender->ldevice, 1, &last_fence, VK_TRUE, RENDERER_VK_TIMEOUT));
//         VK_TRY(vkResetFences(prender->ldevice, 1, &last_fence));

//         VkTimelineSemaphoreSubmitInfo time_sema_info = {
//                 .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
//                 .waitSemaphoreValueCount = 1,
//                 .pWaitSemaphoreValues    =
//                 (uint64_t[]){RENDERER_TIMELINE_INITIAL_VALUE},
//                 .signalSemaphoreValueCount = 1,
//                 .pSignalSemaphoreValues =
//                         (uint64_t[]){RENDERER_TIMELINE_IMAGE_ACQUIRED_VALUE}};

//         uint32_t idx_image;
//         VkAcquireNextImageInfoKHR image_info = {
//                 .sType      = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
//                 .swapchain  = prender->swapchain,
//                 .timeout    = RENDERER_VK_TIMEOUT,
//                 .semaphore  = img_sema,
//                 .deviceMask = 1};
//         VK_TRY(vkAcquireNextImage2KHR(prender->ldevice, &image_info, &idx_image));
//         VkImage image = prender->pswapchain_images[idx_image];

//         VK_TRY(vkBeginCommandBuffer(
//                 cmd_buf,
//                 &(VkCommandBufferBeginInfo){
//                         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}));

//         /* set dynamic state */
//         VkViewport vport = {
//                 .x        = 10,
//                 .y        = 10,
//                 .width    = prender->width,
//                 .height   = prender->height,
//                 .minDepth = 0.0f,
//                 .maxDepth = 1.0f};
//         vkCmdSetViewport(cmd_buf, 0, 1, &vport);

//         vkCmdSetScissor(
//                 cmd_buf, 0, 1, &(VkRect2D){.extent = {prender->width,
//                 prender->height}});

//         /* transition image */
//         VkImageMemoryBarrier2 image_barrier = {
//                 .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
//                 .srcAccessMask       = VK_ACCESS_2_NONE_KHR,
//                 .dstAccessMask       = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR,
//                 .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
//                 .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
//                 .srcQueueFamilyIndex = prender->idx_qfam,
//                 .dstQueueFamilyIndex = prender->idx_qfam,
//                 .image               = prender->pswapchain_images[idx_image],
//                 .subresourceRange    = (VkImageSubresourceRange){
//                         .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
//                         .baseMipLevel   = 0,
//                         .levelCount     = 1,
//                         .baseArrayLayer = 0,
//                         .layerCount     = 1}};

//         VkDependencyInfoKHR dep_info = {
//                 .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
//                 .imageMemoryBarrierCount = 1,
//                 .pImageMemoryBarriers    = &image_barrier};
//         vkCmdPipelineBarrier2(cmd_buf, &dep_info);

//         vkCmdClearColorImage(
//                 cmd_buf,
//                 image,
//                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
//                 &(VkClearColorValue){.uint32 = {255, 0, 0, 0}},
//                 1,
//                 &(VkImageSubresourceRange){
//                         .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
//                         .baseMipLevel   = 0,
//                         .levelCount     = 1,
//                         .baseArrayLayer = 0,
//                         .layerCount     = 1});

//         // vkCmdBindDescriptorSets(
//         //         cmd_buf,
//         //         VK_PIPELINE_BIND_POINT_COMPUTE,
//         //         prender->pipe_layout,
//         //         0,
//         //         1,
//         //         &prender->scene_desc,
//         //         0,
//         //         NULL);

//         // // vkCmdBindPipeline(
//         // //         cmd_buf,
//         // //         VK_PIPELINE_BIND_POINT_COMPUTE,
//         // //         prender->physics_pipe);
//         // // vkCmdDispatch(ceil(prender->nentities / 256), 1, 1);

//         // vkCmdPushConstants(
//         //         cmd_buf,
//         //         prender->pipe_layout,
//         //         VK_SHADER_STAGE_COMPUTE_BIT,
//         //         0,
//         //         RENDERER_SZPUSH_CONSTANTS,
//         //         &prender->dt);

//         // vkCmdBindPipeline(
//         //         cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, prender->graphics_pipe);
//         // vkCmdDraw(cmd_buf, 8, 1, 0, 0);

//         /* transition image */
//         image_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
//         image_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
//         image_barrier.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
//         image_barrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
//         vkCmdPipelineBarrier2(cmd_buf, &dep_info);

//         vkEndCommandBuffer(cmd_buf);

//         time_sema_info.pSignalSemaphoreValues =
//                 (uint64_t[]){RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE};
//         VkSubmitInfo submit_info = {
//                 .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
//                 .pNext                = &time_sema_info,
//                 .waitSemaphoreCount   = 1,
//                 .pWaitSemaphores      = &img_sema,
//                 .commandBufferCount   = 1,
//                 .pCommandBuffers      = &cmd_buf,
//                 .signalSemaphoreCount = 1,
//                 .pSignalSemaphores    = &time_sema};

//         VK_TRY(vkQueueSubmit(prender->queue, 1, &submit_info, VK_NULL_HANDLE));

//         time_sema_info.pWaitSemaphoreValues =
//                 (uint64_t[]){RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE};
//         time_sema_info.pSignalSemaphoreValues =
//                 (uint64_t[]){RENDERER_TIMELINE_FRAME_PRESENT_VALUE};

//         VkPresentInfoKHR present_info = {
//                 .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
//                 .pNext              = &time_sema_info,
//                 .waitSemaphoreCount = 1,
//                 .pWaitSemaphores    = &img_sema,
//                 .swapchainCount     = 1,
//                 .pSwapchains        = &prender->swapchain,
//                 .pImageIndices      = &idx_image};

//         VK_TRY(vkQueuePresentKHR(prender->queue, &present_info));

//         prender->nframe++;
// }

// void renderer_test_draw(renderer_t *prender)
// {
//         fprintf(stderr, "NFRAME: %llu\n", prender->nframe);

//         uint32_t idx_frame = prender->idx_frame % prender->nswapchain_images;
//         uint32_t idx_last  = (prender->nframe - 1) % prender->nswapchain_images;

//         frame_info_t *pframe_info = &prender->pframe_infos[idx_frame];
//         frame_info_t *plast_info  = &prender->pframe_infos[idx_last];

//         VkFence last_fence         = plast_info->fence;
//         VkSemaphore last_time_sema = plast_info->time_sema;

//         VkSemaphore time_sema   = pframe_info->time_sema;
//         VkSemaphore img_sema    = pframe_info->img_sema;
//         VkFence fence           = pframe_info->fence;
//         VkCommandBuffer cmd_buf = pframe_info->cmd_buf;

//         uint32_t idx_image;
//         VkAcquireNextImageInfoKHR image_info = {
//                 .sType      = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
//                 .swapchain  = prender->swapchain,
//                 .timeout    = RENDERER_VK_TIMEOUT,
//                 .semaphore  = img_sema,
//                 .deviceMask = 1};
//         VK_TRY(vkAcquireNextImage2KHR(prender->ldevice, &image_info, &idx_image));
//         VkImage image = prender->pswapchain_images[idx_image];

//         VK_TRY(vkResetCommandBuffer)
//         VK_TRY(vkBeginCommandBuffer(
//                 cmd_buf,
//                 &(VkCommandBufferBeginInfo){
//                         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}));

//         /* set dynamic state */
//         VkViewport vport = {
//                 .x        = 0,
//                 .y        = 0,
//                 .width    = prender->width,
//                 .height   = prender->height,
//                 .minDepth = 0.0f,
//                 .maxDepth = 1.0f};
//         vkCmdSetViewport(cmd_buf, 0, 1, &vport);

//         vkCmdSetScissor(
//                 cmd_buf, 0, 1, &(VkRect2D){.extent = {prender->width,
//                 prender->height}});

//         /* transition image */
//         VkImageMemoryBarrier2 image_barrier = {
//                 .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
//                 .srcAccessMask       = VK_ACCESS_2_NONE_KHR,
//                 .dstAccessMask       = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR,
//                 .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
//                 .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
//                 .srcQueueFamilyIndex = prender->idx_qfam,
//                 .dstQueueFamilyIndex = prender->idx_qfam,
//                 .image               = prender->pswapchain_images[idx_image],
//                 .subresourceRange    = (VkImageSubresourceRange){
//                         .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
//                         .baseMipLevel   = 0,
//                         .levelCount     = 1,
//                         .baseArrayLayer = 0,
//                         .layerCount     = 1}};

//         VkDependencyInfoKHR dep_info = {
//                 .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
//                 .imageMemoryBarrierCount = 1,
//                 .pImageMemoryBarriers    = &image_barrier};
//         vkCmdPipelineBarrier2(cmd_buf, &dep_info);

//         vkCmdClearColorImage(
//                 cmd_buf,
//                 image,
//                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
//                 &(VkClearColorValue){.uint32 = {0, 255, 0, 0}},
//                 1,
//                 &(VkImageSubresourceRange){
//                         .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
//                         .baseMipLevel   = 0,
//                         .levelCount     = 1,
//                         .baseArrayLayer = 0,
//                         .layerCount     = 1});

//         /* transition image */
//         image_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
//         image_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
//         image_barrier.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
//         image_barrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
//         vkCmdPipelineBarrier2(cmd_buf, &dep_info);

//         vkEndCommandBuffer(cmd_buf);

//         /* submit work */
//         VkTimelineSemaphoreSubmitInfo time_sema_info = {
//                 .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
//                 .waitSemaphoreValueCount = 1,
//                 .pWaitSemaphoreValues =
//                         (uint64_t[]){RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE},
//                 .signalSemaphoreValueCount = 1,
//                 .pSignalSemaphoreValues =
//                         (uint64_t[]){RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE}};

//         VkSubmitInfo submit_info = {
//                 .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
//                 .pNext              = &time_sema_info,
//                 .waitSemaphoreCount = 2,
//                 .pWaitSemaphores    = (VkSemaphore[]){img_sema, last_time_sema},
//                 .pWaitDstStageMask =
//                         (VkPipelineStageFlags[]){
//                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
//                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT},
//                 .commandBufferCount   = 1,
//                 .pCommandBuffers      = &cmd_buf,
//                 .signalSemaphoreCount = 1,
//                 .pSignalSemaphores    = &time_sema};
//         VK_TRY(vkQueueSubmit(prender->queue, 1, &submit_info, fence));

//         /* present frame */
//         time_sema_info.waitSemaphoreValueCount = 1;
//         time_sema_info.pWaitSemaphoreValues =
//                 (uint64_t[]){RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE};
//         time_sema_info.signalSemaphoreValueCount = 0;

//         VkPresentInfoKHR present_info = {
//                 .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
//                 .pNext              = &time_sema_info,
//                 .waitSemaphoreCount = 1,
//                 .pWaitSemaphores    = &time_sema,
//                 .swapchainCount     = 1,
//                 .pSwapchains        = &prender->swapchain,
//                 .pImageIndices      = &idx_image};

//         VK_TRY(vkQueuePresentKHR(prender->queue, &present_info));

//         prender->nframe++;
// }

void renderer_test_prerecord(renderer_t *prender)
{
        for (uint32_t i = 0; i < prender->nswapchain_images; i++)
        {
                frame_info_t *pframe_info = &prender->pframe_infos[i];

                VkCommandBuffer cmd_buf = pframe_info->cmd_buf;

                VK_TRY(vkResetCommandBuffer(cmd_buf, 0));
                VK_TRY(vkBeginCommandBuffer(
                        cmd_buf,
                        &(VkCommandBufferBeginInfo){
                                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}));

                /* set dynamic state */
                VkViewport vport = {
                        .x        = 0,
                        .y        = 0,
                        .width    = prender->width,
                        .height   = prender->height,
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f};
                vkCmdSetViewport(cmd_buf, 0, 1, &vport);

                vkCmdSetScissor(
                        cmd_buf,
                        0,
                        1,
                        &(VkRect2D){.extent = {prender->width, prender->height}});

                /* transition image */
                VkImageSubresourceRange all_img = {
                        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel   = 0,
                        .levelCount     = 1,
                        .baseArrayLayer = 0,
                        .layerCount     = 1};

                VkImageMemoryBarrier2 undef_to_color = {
                        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .srcStageMask        = VK_PIPELINE_STAGE_2_NONE,
                        .srcAccessMask       = VK_ACCESS_2_MEMORY_READ_BIT,
                        .dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        .dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
                        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image               = img,
                        .subresourceRange    = all_img};

                VkDependencyInfoKHR dep_info = {
                        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                        .imageMemoryBarrierCount = 1,
                        .pImageMemoryBarriers    = &undef_to_color};
                vkCmdPipelineBarrier2(cmd_buf, &dep_info);

                vkCmdClearColorImage(
                        cmd_buf,
                        img,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        &(VkClearColorValue){.uint32 = {0, 255, 0, 0}},
                        1,
                        &all_img);

                /* transition image */
                VkImageMemoryBarrier2 color_to_present = {
                        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        .srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        .dstStageMask        = VK_PIPELINE_STAGE_2_NONE,
                        .dstAccessMask       = VK_ACCESS_2_MEMORY_READ_BIT,
                        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image               = img,
                        .subresourceRange    = all_img};

                dep_info.pImageMemoryBarriers = &color_to_present;
                vkCmdPipelineBarrier2(cmd_buf, &dep_info);

                vkEndCommandBuffer(cmd_buf);
        }
}

void renderer_test2_draw(renderer_t *prender)
{
        fprintf(stderr, "NFRAME: %llu\n", prender->nframe);

        uint32_t idx_img;
        VK_TRY(vkAcquireNextImageKHR(
                prender->ldevice,
                prender->swapchain,
                RENDERER_VK_TIMEOUT,
                img_sema,
                VK_NULL_HANDLE,
                &idx_img));
        VkImage img = prender->pswapchain_images[idx_img];




        frame_info_t *pframe_info = &prender->pframe_infos[idx_img];


        VkFence last_fence         = plast_info->fence;
        VkSemaphore last_time_sema = plast_info->time_sema;

        VkSemaphore time_sema   = pframe_info->time_sema;
        VkSemaphore img_sema    = pframe_info->img_sema;
        VkFence fence           = pframe_info->fence;
        VkCommandBuffer cmd_buf = pframe_info->cmd_buf;
        VkCommandBuffer cmd_buf = prender->pframe_infos[idx_img].cmd_buf;


        /* submit work */
        VkTimelineSemaphoreSubmitInfo time_sema_info = {
                .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
                .signalSemaphoreValueCount = 1,
                .pSignalSemaphoreValues =
                        (uint64_t[]){RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE}};

        VkSubmitInfo submit_info = {
                .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext              = &time_sema_info,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores    = (VkSemaphore[]){img_sema},
                .pWaitDstStageMask =
                        (VkPipelineStageFlags[]){VK_PIPELINE_STAGE_2_TRANSFER_BIT},
                .commandBufferCount   = 1,
                .pCommandBuffers      = &cmd_buf,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores    = &time_sema};
        VK_TRY(vkQueueSubmit(prender->queue, 1, &submit_info, VK_NULL_HANDLE));

        /* present frame */
        time_sema_info.waitSemaphoreValueCount = 1;
        time_sema_info.pWaitSemaphoreValues =
                (uint64_t[]){RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE};
        time_sema_info.signalSemaphoreValueCount = 0;

        VkPresentInfoKHR present_info = {
                .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .pNext              = &time_sema_info,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores    = &last_time_sema,
                .swapchainCount     = 1,
                .pSwapchains        = &prender->swapchain,
                .pImageIndices      = &idx_img};

        VK_TRY(vkQueuePresentKHR(prender->queue, &present_info));

        prender->nframe++;
}

void renderer_load_mesh(renderer_t *prender, char *ppath)
{
        char *pchar = ppath;
        while (*pchar != '\0')
        {
                while (*pchar++ != ' ')
                        ;
        }
}

int main()
{
        srand(time(NULL));
        renderer_t renderer = {};
        renderer_init(&renderer, "HELLO BRO", 800, 600);

        uint32_t n = 2;
        while (1)
        {
                renderer_test2_draw(&renderer);
        }

        return 0;
}