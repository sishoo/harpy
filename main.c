#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define VOXEL_TYPE_HARD 0b00
#define VOXEL_TYPE_SOFT 0b11
#define VOXEL_TYPE_FLUCUATE1 0b10
#define VOXEL_TYPE_FLUCUATE2 0b01

#define RENDERER_SZPUSH_CONSTANTS sizeof(float[33])

#define                                                                              \
        do                                                                               \
        {                                                                                \
                fprintf(stderr, "%d\n", __LINE__);                         \
        } while (0);

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

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdio.h>

#define NFRAMES_IN_FLIGHT 2
#define RENDERER_VK_TIMEOUT 10

#define RENDERER_TIMELINE_INITIAL_VALUE 0
#define RENDERER_TIMELINE_IMAGE_ACQUIRED_VALUE 1
#define RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE 2
#define RENDERER_TIMELINE_FRAME_PRESENT_VALUE 3
#define RENDERER_SWAPCHAIN_IMAGE_FORMAT VK_FORMAT_R8G8B8_SNORM

#define RENDERER_SZWORKGROUP_X 16
#define RENDERER_SZWORKGROUP_Y 16
#define RENDERER_SZWORKGROUP_Z 1

typedef struct
{
        VkFence fence;
        VkSemaphore timeline;
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

        uint32_t idx_qfam;

        uint32_t idx_frame;
        frame_info_t pframe_infos[NFRAMES_IN_FLIGHT];

        float dt;
        float proj_mat[16], view_mat[16];

        /* backend */
        VkInstance instance;
        VkPhysicalDevice pdevice;
        VkDevice ldevice;
        VkQueue queue;

        VkSwapchainKHR swapchain;
        uint32_t nswapchain_images;
        VkImage *swapchain_images;
        VkSurfaceKHR surface;

        int width, height;
        GLFWwindow *pwindow;
} renderer_t;

void renderer_init_backend(renderer_t *prender, char *pname, int width, int height)
{
        prender->width  = width;
        prender->height = height;

        VkApplicationInfo app_info = {
                .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                .pApplicationName   = pname,
                .applicationVersion = VK_VERSION_1_2,
                .pEngineName        = "engine",
                .engineVersion      = VK_VERSION_1_2,
                .apiVersion         = VK_API_VERSION_1_2};

        VkInstanceCreateInfo instance_info = {
                .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,

                .enabledLayerCount       = 1,
                .ppEnabledLayerNames     = (char *[]) {"VK_LAYER_KHRONOS_validation"},
                .enabledExtensionCount   = 0,
                .ppEnabledExtensionNames = (char *[]) {"VK_KHR_portability_enumeration"}};
        VK_TRY(vkCreateInstance(&instance_info, NULL, &prender->instance));

        uint32_t npdevices = 0;
        VK_TRY(vkEnumeratePhysicalDevices(prender->instance, &npdevices, NULL));
        VkPhysicalDevice *ppdevices = malloc(sizeof(VkPhysicalDevice) * npdevices);
        VK_TRY(vkEnumeratePhysicalDevices(prender->instance, &npdevices, ppdevices));
        prender->pdevice = ppdevices[0];

        VkDeviceCreateInfo device_info = {
                .sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos    = &(VkDeviceQueueCreateInfo) {
                           .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                           .queueFamilyIndex = 0,
                           .queueCount       = 1,
                           .pQueuePriorities = (float[]) {1.0f}},
                .enabledExtensionCount = 2,
                .ppEnabledExtensionNames = (char *[]) {"VK_KHR_portability_subset", "VK_KHR_get_physical_device_properties2"}
                        };
        





        VK_TRY(vkCreateDevice(prender->pdevice, &device_info, NULL, &prender->ldevice));


        

        vkGetDeviceQueue(prender->ldevice, 0, 0, &prender->queue);
        

        VK_TRY(glfwCreateWindowSurface(
                prender->instance, prender->pwindow, NULL, &prender->surface));
        

        VK_TRY(vkCreateSwapchainKHR(
                prender->ldevice,
                &(VkSwapchainCreateInfoKHR) {
                        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                        .surface          = prender->surface,
                        .minImageCount    = 1,
                        .imageFormat      = VK_FORMAT_R8G8B8A8_SNORM,
                        .imageColorSpace  = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
                        .imageExtent      = (VkExtent2D) {width, height},
                        .imageArrayLayers = 1,
                        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                        .queueFamilyIndexCount = 1,
                        .pQueueFamilyIndices   = (uint32_t[]) {0},
                        .preTransform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
                        .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                        .presentMode           = VK_PRESENT_MODE_MAILBOX_KHR,
                        .clipped               = VK_TRUE},
                NULL,
                &prender->swapchain));
        

}

void renderer_init_common(renderer_t *prender)
{
        VkDescriptorSetLayoutBinding binding = {
                .binding         = 0,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags      = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};

        VkDescriptorSetLayoutCreateInfo set_layout_info = {
                .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1,
                .pBindings    = &binding};

        // VK_TRY(vkCreateDescriptorSetLayout(
        //         prender->ldevice, &set_layout_info, NULL, &prender->set_layout));

        VkPipelineLayoutCreateInfo pipe_layout_info = {
                .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount         = 1,
                .pSetLayouts            = &prender->set_layout,
                .pushConstantRangeCount = 0,
                .pPushConstantRanges    = &(VkPushConstantRange) {
                           .stageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           .offset     = 0,
                           .size       = RENDERER_SZPUSH_CONSTANTS}};

        VK_TRY(vkCreatePipelineLayout(
                prender->ldevice, &pipe_layout_info, NULL, &prender->pipe_layout));
}

static VkShaderModule renderer_init_shader_module(
        renderer_t *prender, uint32_t *pcode, uint32_t sz)
{
        VkShaderModule module = VK_NULL_HANDLE;
        VK_TRY(vkCreateShaderModule(
                prender->ldevice,
                &(VkShaderModuleCreateInfo) {
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

        // VkPipelineRenderingCreateInfo render_info = {
        //         .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        //         .colorAttachmentCount = 1,
        //         .pColorAttachmentFormats =
        //                 (VkFormat[]) {RENDERER_SWAPCHAIN_IMAGE_FORMAT}};

        VkGraphicsPipelineCreateInfo pipe_info = {
                .sType      = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                // .pNext      = &render_info,
                .stageCount = 2,
                .pStages    = pstages,
                .layout     = prender->pipe_layout};

        pipe_info.pVertexInputState = &(VkPipelineVertexInputStateCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

        pipe_info.pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};

        pipe_info.pViewportState = &(VkPipelineViewportStateCreateInfo) {
                .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .scissorCount  = 1};

        pipe_info.pRasterizationState = &(VkPipelineRasterizationStateCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .depthClampEnable        = VK_FALSE,
                .rasterizerDiscardEnable = VK_FALSE,
                .polygonMode             = VK_POLYGON_MODE_FILL,
                .cullMode                = VK_CULL_MODE_BACK_BIT,
                .frontFace               = VK_FRONT_FACE_CLOCKWISE,
                .depthBiasEnable         = VK_FALSE,
                .depthBiasClamp          = VK_FALSE,
                .lineWidth               = 1.0f};

        pipe_info.pColorBlendState = &(VkPipelineColorBlendStateCreateInfo) {
                .sType         = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .logicOpEnable = VK_FALSE,
                .logicOp       = VK_LOGIC_OP_COPY,
                .attachmentCount = 1,
                .pAttachments =
                        &(VkPipelineColorBlendAttachmentState) {.blendEnable = VK_FALSE}};

        pipe_info.pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
                .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = 2,
                .pDynamicStates    = (VkDynamicState[]) {VK_DYNAMIC_STATE_VIEWPORT,
                                                         VK_DYNAMIC_STATE_SCISSOR}};

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

void renderer_init_frame_infos(renderer_t *prender)
{
        /* init command pool */
        VK_TRY(vkCreateCommandPool(
                prender->ldevice,
                &(VkCommandPoolCreateInfo) {
                        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                        .queueFamilyIndex = prender->idx_qfam},
                NULL,
                &prender->cmd_pool));

        /* init frame infos */
        for (uint32_t i = 0; i < NFRAMES_IN_FLIGHT; i++)
        {
                VkFenceCreateInfo fence_info = {
                        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};

                VK_TRY(vkCreateFence(
                        prender->ldevice,
                        &fence_info,
                        NULL,
                        &prender->pframe_infos[i].fence));

                VkSemaphoreTypeCreateInfo semaphore_type_info = {
                        .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                        .initialValue  = RENDERER_TIMELINE_INITIAL_VALUE};

                VkSemaphoreCreateInfo semaphore_info = {
                        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                        .pNext = &semaphore_type_info};

                VK_TRY(vkCreateSemaphore(
                        prender->ldevice,
                        &semaphore_info,
                        NULL,
                        &prender->pframe_infos[i].timeline));

                VkCommandBufferAllocateInfo cmd_buf_info = {
                        .sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                        .commandPool = prender->cmd_pool,
                        .level       = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                        .commandBufferCount = 1};

                VK_TRY(vkAllocateCommandBuffers(
                        prender->ldevice,
                        &cmd_buf_info,
                        &prender->pframe_infos[i].cmd_buf));
        }
}

void renderer_init(renderer_t *prender, char *pname, int width, int height)
{
        
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
//         uint32_t idx_frame = prender->nframe % prender->nswapchain_images;

//         uint32_t idx_prev_frame = (prender->nframe - 1) % prender->nswapchain_images;

//         frame_info_t *pframe_info = &prender->pframe_infos[idx_frame];

//         VkSemaphore wait_sema = prender->pframe_infos[idx_prev_frame].timeline;

//         // TODO: i feel like this doesnt need to exist
//         // result = vkWaitForFences(
//         //         prender->ldevice,
//         //         1,
//         //         &frame_info.fence,
//         //         VK_TRUE,
//         //         10
//         // );

//         // result = vkResetFences(
//         //         prender->device,
//         //         1,
//         //         &frame_info.fence
//         // );

//         VkTimelineSemaphoreSubmitInfo sema_info = {
//                 .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
//                 .waitSemaphoreValueCount = 1,
//                 .pWaitSemaphoreValues    = (uint64_t[]) {RENDERER_TIMELINE_INITIAL_VALUE},
//                 .signalSemaphoreValueCount = 1,
//                 .pSignalSemaphoreValues =
//                         (uint64_t[]) {RENDERER_TIMELINE_IMAGE_ACQUIRED_VALUE}};

//         uint32_t idx_image;
//         VkAcquireNextImageInfoKHR image_info = {
//                 .sType      = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
//                 .pNext      = &sema_info,
//                 .swapchain  = prender->swapchain,
//                 .timeout    = RENDERER_VK_TIMEOUT,
//                 .semaphore  = pframe_info->timeline,
//                 .deviceMask = 0};

//         VK_TRY(vkAcquireNextImage2KHR(prender->ldevice, &image_info, &idx_image));

//         VkCommandBuffer cmd_buf = prender->cmd_buf;
//         VK_TRY(vkBeginCommandBuffer(
//                 cmd_buf,
//                 &(VkCommandBufferBeginInfo) {
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
//                 cmd_buf, 0, 1, &(VkRect2D) {.extent = {prender->width, prender->height}});

//         /* transition image */
//         VkImageMemoryBarrier2 image_barrier = {
//                 .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
//                 .srcAccessMask       = VK_ACCESS_2_NONE_KHR,
//                 .dstAccessMask       = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR,
//                 .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
//                 .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
//                 .srcQueueFamilyIndex = prender->idx_qfam,
//                 .dstQueueFamilyIndex = prender->idx_qfam,
//                 .image               = prender->swapchain_images[idx_image],
//                 .subresourceRange    = (VkImageSubresourceRange) {
//                            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
//                            .baseMipLevel   = 0,
//                            .levelCount     = 1,
//                            .baseArrayLayer = 0,
//                            .layerCount     = 1}};

//         VkDependencyInfoKHR dep_info = {
//                 .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
//                 .imageMemoryBarrierCount = 1,
//                 .pImageMemoryBarriers    = &image_barrier};

//         vkCmdPipelineBarrier2(cmd_buf, &dep_info);

//         vkCmdBindDescriptorSets(
//                 cmd_buf,
//                 VK_PIPELINE_BIND_POINT_COMPUTE,
//                 prender->pipe_layout,
//                 0,
//                 1,
//                 &prender->scene_desc,
//                 0,
//                 NULL);

//         // vkCmdBindPipeline(
//         //         cmd_buf,
//         //         VK_PIPELINE_BIND_POINT_COMPUTE,
//         //         prender->physics_pipe);
//         // vkCmdDispatch(ceil(prender->nentities / 256), 1, 1);

//         vkCmdPushConstants(
//                 cmd_buf,
//                 prender->pipe_layout,
//                 VK_SHADER_STAGE_COMPUTE_BIT,
//                 0,
//                 RENDERER_SZPUSH_CONSTANTS,
//                 &prender->dt);

//         vkCmdBindPipeline(
//                 cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, prender->graphics_pipe);
//         vkCmdDraw(cmd_buf, 8, 1, 0, 0);

//         /* transition image */
//         image_barrier.srcAccessMask = VK_KHR_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
//         image_barrier.dstAccessMask = VK_KHR_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
//         image_barrier.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
//         image_barrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
//         vkCmdPipelineBarrier2(cmd_buf, &dep_info);

//         vkEndCommandBuffer(cmd_buf);

//         sema_info.pWaitSemaphoreValues =
//                 (uint64_t[]) {RENDERER_TIMELINE_IMAGE_ACQUIRED_VALUE};
//         sema_info.pSignalSemaphoreValues =
//                 (uint64_t[]) {RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE};
//         VkSubmitInfo submit_info = {
//                 .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
//                 .pNext                = &sema_info,
//                 .waitSemaphoreCount   = 1,
//                 .pWaitSemaphores      = &wait_sema,
//                 .commandBufferCount   = 1,
//                 .pCommandBuffers      = &cmd_buf,
//                 .signalSemaphoreCount = 1,
//                 .pSignalSemaphores    = &pframe_info->timeline};

//         VK_TRY(vkQueueSubmit(prender->queue, 1, &submit_info, VK_NULL_HANDLE));

//         sema_info.pWaitSemaphoreValues =
//                 (uint64_t[]) {RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE};
//         sema_info.pSignalSemaphoreValues =
//                 (uint64_t[]) {RENDERER_TIMELINE_FRAME_PRESENT_VALUE};

//         VkPresentInfoKHR present_info = {
//                 .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
//                 .pNext              = &sema_info,
//                 .waitSemaphoreCount = 1,
//                 .pWaitSemaphores    = &wait_sema,
//                 .swapchainCount     = 1,
//                 .pSwapchains        = &prender->swapchain,
//                 .pImageIndices      = &idx_image};

//         VK_TRY(vkQueuePresentKHR(prender->queue, &present_info));

//         prender->nframe++;
// }

// void renderer_load_mesh(renderer_t* prender, char* ppath)
// {
//         char* pchar = ppath;
//         while (*pchar != '\0') {
//                 while (*pchar++ != ' ')
//                         ;
//         }
// }

int main()
{
        
        renderer_t renderer = {};
        renderer_init(&renderer, "HELLO BRO", 800, 600);

        return 0;
}