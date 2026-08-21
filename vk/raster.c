#include "vk/raster.h"
#include "vk/shaders/tri_shaders.inc"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define FB_W 1024
#define FB_H 512
#define FB_N (FB_W * FB_H)
#define PC_BYTES 80

static const char* g_step = "ok";
static int g_vk = 0;

const char* vk_raster_last_step(void) { return g_step; }
int vk_raster_last_vk(void) { return g_vk; }

static int fail_at(const char* step, VkResult r, int code) {
    g_step = step;
    g_vk = (int)r;
    fprintf(stderr, "VK_RASTER failed step=%s vk=%d\n", step, (int)r);
    return code;
}

static uint32_t find_mem(VkPhysicalDevice phys, uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(phys, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }
    return UINT32_MAX;
}

static int min3(int a, int b, int c) {
    int m = a <= b ? a : b;
    return m <= c ? m : c;
}

static int max3(int a, int b, int c) {
    int m = a > b ? a : b;
    return m > c ? m : c;
}

static int edge_i(vertex_t a, vertex_t b, vertex_t c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

typedef struct {
    int32_t ax, ay, bx, by, cx, cy;
    uint32_t c0, c1, c2, mod0;
    uint32_t attrib;
    int32_t xmin, ymin, xmax, ymax;
    int32_t draw_x1, draw_y1, draw_x2, draw_y2;
    int32_t transp_mode;
} vk_tri_pc;

static void fill_pc(
    vk_tri_pc* pc,
    vertex_t v0,
    vertex_t v1,
    vertex_t v2,
    poly_data_t data,
    const vk_raster_state_t* st
) {
    vertex_t a = v0, b, c;
    if (edge_i(v0, v1, v2) < 0) {
        b = v2;
        c = v1;
    } else {
        b = v1;
        c = v2;
    }
    a.x = (int16_t)(a.x + st->off_x);
    b.x = (int16_t)(b.x + st->off_x);
    c.x = (int16_t)(c.x + st->off_x);
    a.y = (int16_t)(a.y + st->off_y);
    b.y = (int16_t)(b.y + st->off_y);
    c.y = (int16_t)(c.y + st->off_y);
    pc->ax = a.x; pc->ay = a.y;
    pc->bx = b.x; pc->by = b.y;
    pc->cx = c.x; pc->cy = c.y;
    pc->c0 = a.c; pc->c1 = b.c; pc->c2 = c.c;
    pc->mod0 = data.v[0].c;
    pc->attrib = data.attrib;
    pc->xmin = min3(a.x, b.x, c.x);
    pc->ymin = min3(a.y, b.y, c.y);
    pc->xmax = max3(a.x, b.x, c.x);
    pc->ymax = max3(a.y, b.y, c.y);
    pc->draw_x1 = st->draw_x1;
    pc->draw_y1 = st->draw_y1;
    pc->draw_x2 = st->draw_x2;
    pc->draw_y2 = st->draw_y2;
    if (data.attrib & PA_TEXTURED)
        pc->transp_mode = (data.texp >> 5) & 3;
    else
        pc->transp_mode = (st->gpustat >> 5) & 3;
}

static int make_image(
    VkDevice dev,
    VkPhysicalDevice phys,
    VkImageUsageFlags usage,
    VkImage* image,
    VkDeviceMemory* mem,
    VkImageView* view
) {
    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R32_UINT,
        .extent = { FB_W, FB_H, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(dev, &ici, NULL, image) != VK_SUCCESS)
        return 1;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, *image, &req);
    uint32_t mi = find_mem(phys, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mi == UINT32_MAX)
        mi = find_mem(phys, req.memoryTypeBits, 0);
    if (mi == UINT32_MAX)
        return 1;
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = mi,
    };
    if (vkAllocateMemory(dev, &mai, NULL, mem) != VK_SUCCESS)
        return 1;
    if (vkBindImageMemory(dev, *image, *mem, 0) != VK_SUCCESS)
        return 1;
    VkImageViewCreateInfo vci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = *image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R32_UINT,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    if (vkCreateImageView(dev, &vci, NULL, view) != VK_SUCCESS)
        return 1;
    return 0;
}

static void barrier(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout from,
    VkImageLayout to,
    VkAccessFlags src_a,
    VkAccessFlags dst_a,
    VkPipelineStageFlags src_s,
    VkPipelineStageFlags dst_s
) {
    VkImageMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_a,
        .dstAccessMask = dst_a,
        .oldLayout = from,
        .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, src_s, dst_s, 0, 0, NULL, 0, NULL, 1, &b);
}

int vk_raster_triangle(
    const uint16_t* vram_in,
    uint16_t* vram_out,
    vertex_t v0,
    vertex_t v1,
    vertex_t v2,
    poly_data_t data,
    const vk_raster_state_t* st
) {
    if (!vram_in || !vram_out || !st)
        return fail_at("args", VK_ERROR_UNKNOWN, VK_RASTER_FAIL);

    vk_tri_pc pc;
    _Static_assert(sizeof(vk_tri_pc) == PC_BYTES, "push constant packing");
    fill_pc(&pc, v0, v1, v2, data, st);

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "armsx-vk-raster",
        .apiVersion = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo inst_ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };
    VkInstance inst = VK_NULL_HANDLE;
    VkResult r = vkCreateInstance(&inst_ci, NULL, &inst);
    if (r != VK_SUCCESS)
        return fail_at("vkCreateInstance", r, VK_RASTER_FAIL);

    uint32_t ndev = 0;
    vkEnumeratePhysicalDevices(inst, &ndev, NULL);
    if (ndev == 0) {
        vkDestroyInstance(inst, NULL);
        return fail_at("no-physical-device", VK_ERROR_FEATURE_NOT_PRESENT, VK_RASTER_NO_PIPELINE);
    }
    VkPhysicalDevice* phys_list = (VkPhysicalDevice*)malloc(sizeof(*phys_list) * ndev);
    if (!phys_list) {
        vkDestroyInstance(inst, NULL);
        return fail_at("alloc", VK_ERROR_OUT_OF_HOST_MEMORY, VK_RASTER_FAIL);
    }
    vkEnumeratePhysicalDevices(inst, &ndev, phys_list);

    VkPhysicalDevice phys = VK_NULL_HANDLE;
    uint32_t qfam = UINT32_MAX;
    for (uint32_t d = 0; d < ndev && phys == VK_NULL_HANDLE; d++) {
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(phys_list[d], &nq, NULL);
        VkQueueFamilyProperties* qp = (VkQueueFamilyProperties*)malloc(sizeof(*qp) * nq);
        if (!qp)
            continue;
        vkGetPhysicalDeviceQueueFamilyProperties(phys_list[d], &nq, qp);
        for (uint32_t q = 0; q < nq; q++) {
            if (qp[q].queueCount && (qp[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                phys = phys_list[d];
                qfam = q;
                break;
            }
        }
        free(qp);
    }
    free(phys_list);
    if (phys == VK_NULL_HANDLE) {
        vkDestroyInstance(inst, NULL);
        return fail_at("no-graphics-queue", VK_ERROR_FEATURE_NOT_PRESENT, VK_RASTER_NO_PIPELINE);
    }

    VkFormatProperties fprops;
    vkGetPhysicalDeviceFormatProperties(phys, VK_FORMAT_R32_UINT, &fprops);
    VkFormatFeatureFlags need_img =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((fprops.optimalTilingFeatures & need_img) != need_img) {
        vkDestroyInstance(inst, NULL);
        return fail_at("R32_UINT-features", VK_ERROR_FORMAT_NOT_SUPPORTED, VK_RASTER_NO_PIPELINE);
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = qfam,
        .queueCount = 1,
        .pQueuePriorities = &prio,
    };
    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci,
    };
    VkDevice dev = VK_NULL_HANDLE;
    r = vkCreateDevice(phys, &dci, NULL, &dev);
    if (r != VK_SUCCESS) {
        vkDestroyInstance(inst, NULL);
        return fail_at("vkCreateDevice", r, VK_RASTER_FAIL);
    }
    VkQueue queue;
    vkGetDeviceQueue(dev, qfam, 0, &queue);

    int code = VK_RASTER_FAIL;
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VkRenderPass rp = VK_NULL_HANDLE;
    VkPipeline pipe = VK_NULL_HANDLE;
    VkImage src_img = VK_NULL_HANDLE, dst_img = VK_NULL_HANDLE;
    VkDeviceMemory src_mem = VK_NULL_HANDLE, dst_mem = VK_NULL_HANDLE;
    VkImageView src_view = VK_NULL_HANDLE, dst_view = VK_NULL_HANDLE;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkCommandPool cpool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    VkShaderModuleCreateInfo smci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smci.codeSize = vk_tri_vert_spv_words * 4;
    smci.pCode = vk_tri_vert_spv;
    r = vkCreateShaderModule(dev, &smci, NULL, &vs);
    if (r != VK_SUCCESS) {
        code = fail_at("vkCreateShaderModule-vert", r, VK_RASTER_NO_PIPELINE);
        goto cleanup;
    }
    smci.codeSize = vk_tri_frag_spv_words * 4;
    smci.pCode = vk_tri_frag_spv;
    r = vkCreateShaderModule(dev, &smci, NULL, &fs);
    if (r != VK_SUCCESS) {
        code = fail_at("vkCreateShaderModule-frag", r, VK_RASTER_NO_PIPELINE);
        goto cleanup;
    }

    VkSamplerCreateInfo sci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    r = vkCreateSampler(dev, &sci, NULL, &sampler);
    if (r != VK_SUCCESS) {
        code = fail_at("vkCreateSampler", r, VK_RASTER_FAIL);
        goto cleanup;
    }

    VkDescriptorSetLayoutBinding bind = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dslci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &bind,
    };
    r = vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl);
    if (r != VK_SUCCESS) {
        code = fail_at("vkCreateDescriptorSetLayout", r, VK_RASTER_FAIL);
        goto cleanup;
    }

    VkPushConstantRange pcr = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = PC_BYTES,
    };
    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &dsl,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcr,
    };
    r = vkCreatePipelineLayout(dev, &plci, NULL, &pl);
    if (r != VK_SUCCESS) {
        code = fail_at("vkCreatePipelineLayout", r, VK_RASTER_FAIL);
        goto cleanup;
    }

    VkAttachmentDescription att = {
        .format = VK_FORMAT_R32_UINT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference attref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription sub = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attref,
    };
    VkRenderPassCreateInfo rpci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &att,
        .subpassCount = 1,
        .pSubpasses = &sub,
    };
    r = vkCreateRenderPass(dev, &rpci, NULL, &rp);
    if (r != VK_SUCCESS) {
        code = fail_at("vkCreateRenderPass", r, VK_RASTER_NO_PIPELINE);
        goto cleanup;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vs,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fs,
            .pName = "main",
        },
    };
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkViewport vp = { .width = (float)FB_W, .height = (float)FB_H, .minDepth = 0.0f, .maxDepth = 1.0f };
    VkRect2D sc = { .extent = { FB_W, FB_H } };
    VkPipelineViewportStateCreateInfo vps = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &vp,
        .scissorCount = 1,
        .pScissors = &sc,
    };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState cba = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &cba,
    };
    VkGraphicsPipelineCreateInfo gpci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState = &vps,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pColorBlendState = &cb,
        .layout = pl,
        .renderPass = rp,
        .subpass = 0,
    };
    r = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe);
    if (r != VK_SUCCESS) {
        code = fail_at("vkCreateGraphicsPipelines", r, VK_RASTER_NO_PIPELINE);
        goto cleanup;
    }

    if (make_image(
            dev, phys,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            &src_img, &src_mem, &src_view) ||
        make_image(
            dev, phys,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            &dst_img, &dst_mem, &dst_view)) {
        code = fail_at("create-images", VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_RASTER_FAIL);
        goto cleanup;
    }

    VkFramebufferCreateInfo fbci = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = rp,
        .attachmentCount = 1,
        .pAttachments = &dst_view,
        .width = FB_W,
        .height = FB_H,
        .layers = 1,
    };
    r = vkCreateFramebuffer(dev, &fbci, NULL, &fb);
    if (r != VK_SUCCESS) {
        code = fail_at("vkCreateFramebuffer", r, VK_RASTER_FAIL);
        goto cleanup;
    }

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = (VkDeviceSize)FB_N * sizeof(uint32_t),
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    r = vkCreateBuffer(dev, &bci, NULL, &staging);
    if (r != VK_SUCCESS) {
        code = fail_at("vkCreateBuffer", r, VK_RASTER_FAIL);
        goto cleanup;
    }
    VkMemoryRequirements breq;
    vkGetBufferMemoryRequirements(dev, staging, &breq);
    uint32_t bmi = find_mem(
        phys, breq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (bmi == UINT32_MAX) {
        code = fail_at("staging-memory", VK_ERROR_FEATURE_NOT_PRESENT, VK_RASTER_FAIL);
        goto cleanup;
    }
    VkMemoryAllocateInfo bmai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = breq.size,
        .memoryTypeIndex = bmi,
    };
    r = vkAllocateMemory(dev, &bmai, NULL, &staging_mem);
    if (r != VK_SUCCESS || vkBindBufferMemory(dev, staging, staging_mem, 0) != VK_SUCCESS) {
        code = fail_at("bind-staging", r, VK_RASTER_FAIL);
        goto cleanup;
    }

    uint32_t* mapped = NULL;
    vkMapMemory(dev, staging_mem, 0, FB_N * sizeof(uint32_t), 0, (void**)&mapped);
    for (size_t i = 0; i < FB_N; i++)
        mapped[i] = vram_in[i];
    vkUnmapMemory(dev, staging_mem);

    VkDescriptorPoolSize dps = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
    };
    VkDescriptorPoolCreateInfo dpci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &dps,
    };
    r = vkCreateDescriptorPool(dev, &dpci, NULL, &dpool);
    if (r != VK_SUCCESS) {
        code = fail_at("vkCreateDescriptorPool", r, VK_RASTER_FAIL);
        goto cleanup;
    }
    VkDescriptorSetAllocateInfo dsai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = dpool,
        .descriptorSetCount = 1,
        .pSetLayouts = &dsl,
    };
    VkDescriptorSet dset;
    r = vkAllocateDescriptorSets(dev, &dsai, &dset);
    if (r != VK_SUCCESS) {
        code = fail_at("vkAllocateDescriptorSets", r, VK_RASTER_FAIL);
        goto cleanup;
    }
    VkDescriptorImageInfo dii = {
        .sampler = sampler,
        .imageView = src_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet wr = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = dset,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &dii,
    };
    vkUpdateDescriptorSets(dev, 1, &wr, 0, NULL);

    VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = qfam,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };
    r = vkCreateCommandPool(dev, &cpci, NULL, &cpool);
    if (r != VK_SUCCESS) {
        code = fail_at("vkCreateCommandPool", r, VK_RASTER_FAIL);
        goto cleanup;
    }
    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cpool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    r = vkAllocateCommandBuffers(dev, &cai, &cmd);
    if (r != VK_SUCCESS) {
        code = fail_at("vkAllocateCommandBuffers", r, VK_RASTER_FAIL);
        goto cleanup;
    }

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &bi);

    barrier(
        cmd, src_img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT
    );
    barrier(
        cmd, dst_img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    VkBufferImageCopy copy = {
        .imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
        .imageExtent = { FB_W, FB_H, 1 },
    };
    vkCmdCopyBufferToImage(cmd, staging, src_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    vkCmdCopyBufferToImage(cmd, staging, dst_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    barrier(
        cmd, src_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
    barrier(
        cmd, dst_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    );

    VkRenderPassBeginInfo rpbi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = rp,
        .framebuffer = fb,
        .renderArea = { .extent = { FB_W, FB_H } },
    };
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &dset, 0, NULL);
    vkCmdPushConstants(cmd, pl, VK_SHADER_STAGE_FRAGMENT_BIT, 0, PC_BYTES, &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    barrier(
        cmd, dst_img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT
    );
    vkCmdCopyImageToBuffer(cmd, dst_img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &copy);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    r = vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    if (r == VK_SUCCESS)
        r = vkQueueWaitIdle(queue);
    if (r != VK_SUCCESS) {
        code = fail_at("submit", r, VK_RASTER_FAIL);
        goto cleanup;
    }

    vkMapMemory(dev, staging_mem, 0, FB_N * sizeof(uint32_t), 0, (void**)&mapped);
    for (size_t i = 0; i < FB_N; i++)
        vram_out[i] = (uint16_t)mapped[i];
    vkUnmapMemory(dev, staging_mem);

    g_step = "ok";
    g_vk = 0;
    code = VK_RASTER_OK;

cleanup:
    if (cpool) vkDestroyCommandPool(dev, cpool, NULL);
    if (dpool) vkDestroyDescriptorPool(dev, dpool, NULL);
    if (staging) vkDestroyBuffer(dev, staging, NULL);
    if (staging_mem) vkFreeMemory(dev, staging_mem, NULL);
    if (fb) vkDestroyFramebuffer(dev, fb, NULL);
    if (src_view) vkDestroyImageView(dev, src_view, NULL);
    if (dst_view) vkDestroyImageView(dev, dst_view, NULL);
    if (src_img) vkDestroyImage(dev, src_img, NULL);
    if (dst_img) vkDestroyImage(dev, dst_img, NULL);
    if (src_mem) vkFreeMemory(dev, src_mem, NULL);
    if (dst_mem) vkFreeMemory(dev, dst_mem, NULL);
    if (pipe) vkDestroyPipeline(dev, pipe, NULL);
    if (rp) vkDestroyRenderPass(dev, rp, NULL);
    if (pl) vkDestroyPipelineLayout(dev, pl, NULL);
    if (dsl) vkDestroyDescriptorSetLayout(dev, dsl, NULL);
    if (sampler) vkDestroySampler(dev, sampler, NULL);
    if (vs) vkDestroyShaderModule(dev, vs, NULL);
    if (fs) vkDestroyShaderModule(dev, fs, NULL);
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(inst, NULL);
    return code;
}
