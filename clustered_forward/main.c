#include "include/backend.h"
#include "include/graphics/light.h"
#include "include/math/vec.h"
#include "include/vertex.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdio.h>

#define NFRAMES_IN_FLIGHT 2
#define RENDERER_VK_TIMEOUT 10

#define RENDERER_TIMELINE_INITIAL_VALUE 0
#define RENDERER_TIMELINE_IMAGE_ACQUIRED_VALUE 1
#define RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE 2
#define RENDERER_SZWORKGROUP_X 16
#define RENDERER_SZWORKGROUP_Y 16
#define RENDERER_SZWORKGROUP_Z 1


typedef struct
{
        vk_backend_t *pvk_backend;

        VkPipelineLayout pipe_layout;
        VkPipeline object_pipe, light_pipe; /* dont change order */
        VkPipeline meshlet_pipe, zprepass_pipe,
                graphics_pipe; /* dont change order */

        VkDescriptorSetLayout set_layout;
        VkDescriptorSet scene_descriptor;

        VkDeviceMemory scene_mem;
        VkBuffer scene_buf;
        uint32_t idx_geometry, idx_draw, idx_draw_count, idx_object, idx_light;

        VkCommandPool cmd_pool;
        uint32_t idx_frame_info;
        struct
        {
                VkFence fence;
                VkSemaphore timeline;
                VkCommandBuffer cmd_buf;
        } pframe_infos[NFRAMES_IN_FLIGHT];

        uint32_t sz_meshes, nmeshes;
        char *ppaths;

        uint32_t cap_lights, nlights;
        light_t *plights;

        float proj_mat[16], view_mat[16];
} renderer_t;

void renderer_init_common(renderer_t *prenderer)
{
        VkDescriptorSetLayoutBinding binding = {
                .binding         = 0,
                .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags      = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};

        VkDescriptorSetLayoutCreateInfo set_layout_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = 1,
                .pBindings    = &binding};

        VK_TRY(vkCreateDescriptorSetLayout(
                prenderer->pvk_backend->ldevice,
                &set_layout_info,
                NULL,
                &prenderer->set_layout));

        VkPipelineLayoutCreateInfo pipe_layout_info = {
                .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1,
                .pSetLayouts    = &prenderer->set_layout,
                .pushConstantRangesCount = 0,
                .pPushConstantRanges = &(VkPushConstantRange) {
                        .stageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        .offset     = 0,
                        .size       = }};

        VK_TRY(vkCreatePipelineLayout(
                prenderer->pvk_backend->ldevice,
                &pipe_layout_info,
                NULL,
                &prenderer->pipe_layout));
}

static VkShaderModule renderer_init_shader_module(
        renderer_t *prenderer, uint32_t *pcode, uint32_t sz)
{
        VkShaderModule module = VK_NULL_HANDLE;
        VK_TRY(vkCreateShaderModule(
                prenderer->pvk_backend->ldevice,
                &(VkShaderModuleCreateInfo) {
                        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                        .codeSize = sz,
                        .pCode    = pcode},
                NULL,
                &module));
}

void renderer_init_graphics_pipes(renderer_t *prenderer)
{
        static uint32_t pzprepass_spv[] = {
#include "shader/spv/zprepass.vert.spv"
        };
        VkShaderModule zprepass_module =
                init_shader_module(pzprepass_spv, sizeof pzprepass_spv);

        static uint32_t pmain_vert_spv[] = {
#include "shader/spv/zprepass.vert.spv"
        };
        VkShaderModule main_vert_module =
                init_shader_module(pmain_vert_spv, sizeof pmain_vert_spv);

        static uint32_t pmain_frag_spv[] = {
#include "shader/spv/main.frag.spv"
        };
        VkShaderModule main_frag_module =
                init_shader_module(pmain_frag_spv, sizeof pmain_frag_spv);

        static uint32_t pmeshlet_vert_spv[] = {
#include "shader/spv/meshlet.vert.spv"
        };
        VkShaderModule meshlet_vert_module =
                init_shader_module(pmeshlet_vert_spv, sizeof pmeshlet_vert_spv);

        static uint32_t pmeshlet_frag_spv[] = {
#include "shader/spv/meshlet.frag.spv"
        };
        VkShaderModule meshlet_frag_module =
                init_shader_module(pmeshlet_frag_spv, sizeof pmeshlet_frag_spv);

        VkPipelineShaderStageCreateInfo stages[3];
        stages[0] = {
                .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE,
                .stage  = VK_SHADER_STAGE_VERTEX_BIT,
                .module = zprepass_shader_module,
                .pName  = "main"};

        stages[1] = {
                .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE,
                .stage  = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vert_shader_module,
                .pName  = "main"};

        stages[2] = {
                .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE,
                .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = frag_shader_module,
                .pName  = "main"};

        VkGraphicsPipelineCreateInfo graphics_pipe_infos[2] = {};
        graphics_pipe_infos[0] = (VkGraphicsPipelineCreateInfo) {
                .sType      = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = 1,
                .pStages    = stages,
                .pViewportState = sf,
                .pRasterizationState =
                        &(VkPipelineRasterizationStateCreateInfo) {

                        },
                .pMultisampleState  = sf,
                .pDepthStencilState = sf,
                .pColorBlendState   = sf,
                .pDynamicState      = sf,
                .layout             = prenderer->pipe_layout};

        graphics_pipe_infos[1] = (VkGraphicsPipelineCreateInfo) {
                .sType      = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = 3,
                .pStages    = stages,
                .layout     = prenderer->pipe_layout,
        };

        VK_TRY(vkCreateGraphicsPipelines(
                pvk_backend->ldevice,
                VK_NULL_HANDLE,
                2,
                graphics_pipe_infos,
                NULL,
                &prenderer->zprepass_pipe));
}

void renderer_init_compute_pipes(renderer_t *prenderer)
{
        static uint32_t object_spv[] = {
#include "shader/spv/object.comp.spv"
        };

        VkShaderModule object_module =
                init_shader_module(pobject_spv, sizeof pobject_spv);

        VkPipelineShaderStageCreateInfo object_shader_info = {
                .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = object_module,
                .pname  = "main"};

        static uint32_t light_spv[] = {
#include "shader/spv/light.comp.spv"
        };

        VkShaderModule object_module =
                init_shader_module(plight_spv, sizeof plight_spv);

        VkPipelineShaderStageCreateInfo light_shader_info = {
                .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = light_module,
                .pname  = "main"};

        VkComputePipelineCreateInfo pipeline_infos[2];
        pipeline_infos[0] = {
                .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage  = object_stage_info,
                .layout = prenderer->pipe_layout};

        pipeline_infos[1] = {
                .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage  = light_stage_info,
                .layout = prenderer->pipe_layout};

        VK_TRY(vkCreateComputePipelines(
                prenderer->pvk_backend->ldevice,
                VK_NULL_HANDLE,
                2,
                pipeline_infos,
                NULL,
                &prenderer->object_pipe));
}

void renderer_init_frame_infos(renderer_t *prenderer)
{
        /* init command pool */
        VK_TRY(vkCreateCommandPool(
                prenderer->pvk_backend->ldevice,
                &(vkCommandBufferCreateInfo) {
                        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                        .queueFamilyIndex = prenderer->idx_queue_family},
                NULL,
                &prenderer->cmd_pool));

        /* init frame infos */
        for (uint32_t i = 0; i < NFRAMES_IN_FLIGHT; i++)
        {
                VkFenceCreateInfo fence_info = {
                        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};

                VK_TRY(vkCreateFence(
                        prenderer->pvk_backend->ldevice,
                        &fence_info,
                        NULL,
                        &prenderer->pframe_infos[i].fence));

                VkSemaphoreTypeCreateInfo semaphore_type_info = {
                        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                        .initialValue  = RENDERER_TIMELINE_INITIAL_VALUE};

                VkSemaphoreCreateInfo semaphore_info = {
                        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                        .pNext = &semaphore_type_info};

                VK_TRY(vkCreateSemaphore(
                        prenderer->pvk_backend->ldevice,
                        &semaphore_info NULL,
                        &prenderer->pframe_infos[i]));

                VkCommandBufferAllocateInfo cmd_buf_info = {
                        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                        .commandPool        = prenderer->cmd_pool,
                        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                        .commandBufferCount = 1};

                VK_TRY(vkAllocateCommandBuffers(
                        prenderer->pvk_backend->ldevice,
                        &cmd_buf_info,
                        &prenderer->pframe_infos[i].cmd_buf));
        }
}

void renderer_init_cluster_img(renderer_t *prenderer)
{
        VkImageCreateInfo img_info = {
                .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_3D,
                .format    = VK_FORMAT_R8G8B8_UINT,
                .extent    = (VkExtent3D) {.width = 9, .height = 9, .depth = 9},
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples     = VK_SAMPLE_COUNT_1_BIT,
                .tiling      = VK_IMAGE_TILING_OPTIMAL,
                .usage =
                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                .shardingMode          = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 1,
                .pQueueFamilyIndices   = &prenderer->idx_queue_family,
                .initialLayout         = VK_IMAGE_LAYOUT_GENERAL};

        VK_TRY(vkCreateImage(
                prenderer->pvkc_backend->ldevice,
                &img_info,
                NULL,
                &prenderer->cluster_img));
}

void renderer_init(renderer_t *prenderer, vk_backend_t *pvk_backend)
{
        prenderer->pvk_backend = pvk_backend;
        renderer_init_common(prenderer);
        renderer_init_graphics_pipes(prenderer);
        renderer_init_compute_pipes(prenderer);
        renderer_init_frame_infos(prenderer);
        renderer_init_cluster_img(prenderer);
}

void renderer_prerecord_cmd_buf(renderer_t *prenderer)
{
        VkCommandBuffer cmd_buf = prenderer->cmd_buf;
        VK_TRY(vkBeginCommandBuffer(
                cmd_buf,
                &(VkCommandBufferBeginInfo) {
                        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}));

        /* transition image */
        VkImageMemoryBarrier2 image_barrier = {
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask       =,
                .dstAccessMask       =,
                .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = prenderer->idx_queue_family,
                .dstQueueFamilyIndex = prenderer->idx_queue_family,
                .image = prenderer->pvk_backend->swapchain_images[idx_image],
                .subresourceRange = (VkImageSubresourceRange) {
                        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel   = 0,
                        .levelCount     = 1,
                        .baseArrayLayer = 0,
                        .layerCount     = 1}};

        VkDependencyInfo dep_info = {
                .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .dependencyFlags         =,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers    = &image_barrier};
        vkCmdPipelineBarrier2(cmd_buf, &dep_info);

        vkCmdBindDescriptorSets(
                cmd_buf,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                prenderer->pipe_layout,
                0,
                1,
                &prenderer->scene_descriptor,
                0,
                NULL);

        vkCmdPushConstants(
                cmd_buf,
                prenderer->pipe_layout,
                VK_SHADER_STAGE_COMPUTE_BIT,
                0,
                sizeof(float[16]) * 2, // two mat4s
                &prenderer->proj_mat);

        /* light clustering */
        vkCmdBindPipeline(
                cmd_buf,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                prenderer->cluster_pipe);
        vkCmdDispatch(cmd_buf, 9, 9, 9);

        /* object pass */
        vkCmdBindPipeline(
                cmd_buf,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                prenderer->object_pipe);
        vkCmdDispatch(cmd_buf, ceil(prenderer->nobjects / 256), 1, 1);

        /* sync draw buffer access for meshlet pass */
        dep_info = {
                .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarrier     = &(VkBufferMemoryBarrier2) {
                            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                            .srcStageMask        =,
                            .srcAccessMask       = VK_ACCESS_2_MEMORY_READ_BIT,
                            .dstStageMask        = VK_PIPELINE_STAGE_2_,
                            .dstAccessMask       = VK_ACCESS_2_MEMORY_READ_BIT,
                            .srcQueueFamilyIndex = prenderer->idx_queue_family,
                            .dstQueueFamilyIndex = prenderer->idx_queue_family,
                            .buffer              = prenderer->scene_buf,
                            .offset              = prenderer->idx_draws,
                            .size                = prenderer->nobjects *
                                sizeof(VkDrawIndexedIndirectCommand)}};
        vkCmdPipelineBarrier2(cmd_buf, &dep_info);

        /* meshlet pass */
        vkCmdBindPipeline(
                cmd_buf,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                prenderer->meshlet_pipe);
        vkCmdDrawIndexedIndirectCount(
                cmd_buf,
                prenderer->scene_buf,
                prenderer->idx_draw,
                prenderer->scene_buf,
                prenderer->idx_draw_count,
                prenderer->nmeshlets, // TODO: update this with the current draw
                                      // buffer size of something
                0);

        /* sync draw and idx_meshlets buffer access for main pipeline

                TODO: can probably just make this one barrier for the
                combined region of both sub areas
        */
        VkBufferMemoryBarrier2 pbuffer_barriers[2];
        pbuffer_barriers[0] = {
                // draw buffer
                .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR,
                .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
                .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR,
                .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
                .srcQueueFamilyIndex = prenderer->idx_queue_family,
                .dstQueueFamilyIndex = prenderer->idx_queue_family,
                .buffer              = prenderer->scene_buf,
                .offset              = prenderer->idx_draws,
                .size                = prenderer->nobjects *
                        sizeof(VkDrawIndexedIndirectCommand)};
        pbuffer_barriers[1] = {
                // idx_visible_meshlets
                .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR,
                .srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
                .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR,
                .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
                .srcQueueFamilyIndex = prenderer->idx_queue_family,
                .dstQueueFamilyIndex = prenderer->idx_queue_family,
                .buffer              = prenderer->scene_buf,
                .offset              = prenderer->idx_visible_meshlets,
                .size                = prenderer->nmeshlets *
                        sizeof(VkDrawIndexedIndirectCommand)};

        dep_info = {
                .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = 2,
                .pBufferMemoryBarrier     = pbuffer_barriers};
        vkCmdPipelineBarrier2(cmd_buf, &dep_info);

        /* zprepass */
        vkCmdBindPipeline(
                cmd_buf,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                prenderer->zprepass_pipe);
        vkCmdDrawIndexedIndirectCount(
                cmd_buf,
                prenderer->scene_buf,
                prenderer->idx_draw,
                prenderer->scene_buf,
                prenderer->idx_visible_meshlets,
                prenderer->nmeshlets,
                0);

        /* sync idx_light buffer access */
        dep_info = {
                .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarrier     = &(VkImageMemoryBarrier2) {
                            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                            .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                            .dstAccessMask       = VK_ACCESS_2_MEMORY_READ_BIT,
                            .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
                            .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
                            .srcQueueFamilyIndex = prenderer->idx_queue_family,
                            .dstQueueFamilyIndex = prenderer->idx_queue_family,
                            .image               = prenderer->cluster_image,
                            .subresourceRange    = (VkImageSubresourceRange) {
                                       .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                       .baseMipLevel   = 0,
                                       .levelCount     = 1,
                                       .baseArrayLayer = 0,
                                       .layerCount     = 1}}};
        vkCmdPipelineBarrier2(cmd_buf, &dep_info);

        /* real draw */
        vkCmdBindPipeline(
                cmd_buf,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                prenderer->graphics_pipe);
        vkCmdDrawIndexedIndirectCount(
                cmd_buf,
                prenderer->scene_buf,
                prenderer->idx_draw,
                prenderer->scene_buf,
                prenderer->idx_visible_meshlets,
                prenderer->nmeshlets,
                0);

        vkEndCommandBuffer(cmd_buf);
}

void renderer_prepare(renderer_t *prenderer)
{
        /* init scene buffer */
        uint32_t sz_meshes =
                prenderer->sz_meshes + sizeof(object_t) * prenderer->nmeshes;
        uint32_t sz_draws =
                prenderer->nobjects * sizeof(VkDrawIndexedIndirectCommand);
        uint32_t sz_objects = prenderer->nbojects * sizeof(object_t);
        VK_TRY(vkCreateBuffer(
                pvk_backend->ldevice,
                &(VkBufferCreateInfo) {
                        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                        .size  = sz_meshes + sz_draws + sz_objects +
                                4 // 4 for the draw count
                },
                NULL,
                &prenderer->scene_buf));

        VK_TRY(vkBindBufferMemory(
                prenderer->pvk_backend->ldevice,
                prenderer->scene_buf,
                prenderer->scene_mem,
                0));

        void *pmapped = NULL;
        VK_TRY(vkMapMemory(
                prenderer->pvk_backend->ldevice,
                prenderer->scene_mem,
                0,
                prenderer->sz_meshes,
                0,
                &pmapped));

        memcpy(pmapped, pmeshes, prenderer->sz_meshes);

        vkUnmapMemory(prenderer->pvk_backend->ldevice, prenderer->scene_mem);
}

void renderer_draw(renderer_t *prenderer)
{
        uint32_t prev_idx_frame_info = prenderer->idx_frame_info++;
        if (prenderer->pvk_backend->nswapchain_images ==
            prenderer->idx_resource)
                prenderer->idx_resource = 0;

        frame_info_t *pframe_info      = pframe_infos[prenderer->idx_resource];
        frame_info_t *plast_frame_info = pframe_infos[prev_idx_resource];

        // TODO: i feel like this doesnt need to exist
        // result = vkWaitForFences(
        //         prenderer->pvk_backend->ldevice,
        //         1,
        //         &frame_info.fence,
        //         VK_TRUE,
        //         10
        // );

        // result = vkResetFences(
        //         prenderer->pvk_backend->device,
        //         1,
        //         &frame_info.fence
        // );

        VkTimelineSemaphoreSubmitInfo timeline_submit_info = {
                .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
                .waitSemaphoreValueCount = 1,
                .pWaitSemaphoreValues =
                        (uint32_t[]) {RENDERER_TIMELINE_INITIAL_VALUE},
                .signalSemaphoreValueCount = 1,
                .pSignalSemaphoreValues =
                        (uint32_t[]) {RENDERER_TIMELINE_IMAGE_ACQUIRED_VALUE}};

        uint32_t idx_image;
        VkAcquireNextImageInfoKHR image_info = {
                .sType      = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
                .pNext      = &timeline_submit_info,
                .swapchain  = prenderer->pvk_backend->swapchain,
                .timeout    = RENDERER_VK_TIMEOUT,
                .semaphore  = pframe_info->timeline,
                .deviceMask = 0};

        VK_TRY(vkAcquireNextImage2KHR(
                prenderer->pvk_backend->ldevice, &image_info, &idx_image));

        timeline_submit_info.pWaitSemaphoreValues = {
                RENDERER_TIMELINE_IMAGE_ACQUIRED_VALUE};
        timeline_submit_info.pSignalSemaphoreValues = {
                RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE};
        VkSubmitInfo submit_info = {
                .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext                = &timeline_submit_info,
                .waitSemaphoreCount   = 1,
                .pWaitSemaphores      = &plast_frame_info->timeline,
                .commandBufferCount   = 1,
                .pCommandBuffers      = &cmd_buf,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores    = &pframe_info->timeline};

        VK_TRY(vkQueueSubmit(
                prenderer->pvk_backend->queue,
                1,
                &submit_info,
                VK_NULL_HANDLE));

        timeline_submit_info.pWaitSemaphoreValues = {
                RENDERER_TIMELINE_COMMANDS_COMPLETE_VALUE};
        timeline_submit_info.pSignalSemaphoreValues = {
                RENDERER_TIMELINE_FRAME_PRESENTED_VALUE};
        VK_TRY(vkQueuePresentKHR(
                prenderer->pvk_backend->queue,
                &(VkPresentInfoKHR) {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO,
                                     .pNext = &timeline_submit_info,
                                     .waitSemaphoreCount = 1,
                                     .pWaitSemaphores = &pframe_infos->timeline,
                                     .swapchainCount  = 1,
                                     .pSwapchains =
                                             prenderer->pvk_backend->swapchain,
                                     .pImageIndices = &idx_image}));
}

// void renderer_load_mesh(renderer_t* prenderer, char* ppath)
// {
//         char* pchar = ppath;
//         while (*pchar != '\0') {
//                 while (*pchar++ != ' ')
//                         ;
//         }
// }

void renderer_add_light(renderer_t *pharpy, light_t light)
{
        if (prenderer->nlights == prenderer->cap_lights)
        {
                prenderer->plights =
                        realloc(prenderer->plights, prenderer->cap_lights * 2);
        }

        prenderer->plights[prenderer->nlights] = light;
}

int main()
{
        vk_backend_t backend = {};
        vk_backend_init(&backend, "Clustered Forward");

        renderer_t renderer = {};
        renderer_init(&renderer, &backend);
        renderer_prerecord_cmd_buf(&renderer);

        renderer_add_light(
                &renderer,
                (light_t) {.color = {0.0, 255.0, 0.0}, .pos = {0.0, 0.0, 0.0}});

        // renderer_load_mesh(&renderer);

        while (renderer.is_running)
        {
                renderer_draw(&renderer);
        }

        return 0;
}