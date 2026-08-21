#include "vk/blit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

static int vk_fail(const char* what, VkResult r) {
    fprintf(stderr, "VK_BLIT failed step=%s vk=%d\n", what, (int)r);
    return 1;
}

static uint32_t vk_find_memory(VkPhysicalDevice phys, uint32_t type_bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(phys, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }
    return UINT32_MAX;
}

int vk_buffer_copy_roundtrip(const void* src, void* dst, size_t bytes) {
    if (!src || !dst || bytes == 0)
        return 1;

    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "armsx-vk-blit",
        .apiVersion = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };
    VkInstance inst = VK_NULL_HANDLE;
    VkResult r = vkCreateInstance(&ici, NULL, &inst);
    if (r != VK_SUCCESS)
        return vk_fail("vkCreateInstance", r);

    uint32_t ndev = 0;
    r = vkEnumeratePhysicalDevices(inst, &ndev, NULL);
    if (r != VK_SUCCESS || ndev == 0) {
        vkDestroyInstance(inst, NULL);
        return vk_fail("vkEnumeratePhysicalDevices", r);
    }
    VkPhysicalDevice* phys_list = (VkPhysicalDevice*)malloc(sizeof(*phys_list) * ndev);
    if (!phys_list) {
        vkDestroyInstance(inst, NULL);
        return 1;
    }
    vkEnumeratePhysicalDevices(inst, &ndev, phys_list);

    VkPhysicalDevice phys = VK_NULL_HANDLE;
    uint32_t qfam = UINT32_MAX;
    for (uint32_t d = 0; d < ndev && phys == VK_NULL_HANDLE; d++) {
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(phys_list[d], &nq, NULL);
        VkQueueFamilyProperties* qp = (VkQueueFamilyProperties*)malloc(sizeof(*qp) * nq);
        if (!qp) continue;
        vkGetPhysicalDeviceQueueFamilyProperties(phys_list[d], &nq, qp);
        for (uint32_t q = 0; q < nq; q++) {
            if (qp[q].queueCount &&
                (qp[q].queueFlags & (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_GRAPHICS_BIT))) {
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
        return vk_fail("no-transfer-queue", VK_ERROR_FEATURE_NOT_PRESENT);
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
        return vk_fail("vkCreateDevice", r);
    }
    VkQueue queue;
    vkGetDeviceQueue(dev, qfam, 0, &queue);

    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer src_buf = VK_NULL_HANDLE, dst_buf = VK_NULL_HANDLE;
    if (vkCreateBuffer(dev, &bci, NULL, &src_buf) != VK_SUCCESS ||
        vkCreateBuffer(dev, &bci, NULL, &dst_buf) != VK_SUCCESS) {
        vkDestroyDevice(dev, NULL);
        vkDestroyInstance(inst, NULL);
        return vk_fail("vkCreateBuffer", VK_ERROR_OUT_OF_DEVICE_MEMORY);
    }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, src_buf, &req);
    uint32_t mem_i = vk_find_memory(
        phys, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mem_i == UINT32_MAX) {
        vkDestroyBuffer(dev, src_buf, NULL);
        vkDestroyBuffer(dev, dst_buf, NULL);
        vkDestroyDevice(dev, NULL);
        vkDestroyInstance(inst, NULL);
        return vk_fail("no-host-visible-memory", VK_ERROR_FEATURE_NOT_PRESENT);
    }
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size * 2,
        .memoryTypeIndex = mem_i,
    };
    /* Separate allocations so the copy is a real device transfer. */
    VkDeviceMemory src_mem = VK_NULL_HANDLE, dst_mem = VK_NULL_HANDLE;
    mai.allocationSize = req.size;
    if (vkAllocateMemory(dev, &mai, NULL, &src_mem) != VK_SUCCESS ||
        vkAllocateMemory(dev, &mai, NULL, &dst_mem) != VK_SUCCESS ||
        vkBindBufferMemory(dev, src_buf, src_mem, 0) != VK_SUCCESS ||
        vkBindBufferMemory(dev, dst_buf, dst_mem, 0) != VK_SUCCESS) {
        vkFreeMemory(dev, src_mem, NULL);
        vkFreeMemory(dev, dst_mem, NULL);
        vkDestroyBuffer(dev, src_buf, NULL);
        vkDestroyBuffer(dev, dst_buf, NULL);
        vkDestroyDevice(dev, NULL);
        vkDestroyInstance(inst, NULL);
        return vk_fail("allocate-bind", VK_ERROR_OUT_OF_DEVICE_MEMORY);
    }

    void* mapped = NULL;
    vkMapMemory(dev, src_mem, 0, bytes, 0, &mapped);
    memcpy(mapped, src, bytes);
    vkUnmapMemory(dev, src_mem);

    VkCommandPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = qfam,
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(dev, &pci, NULL, &pool) != VK_SUCCESS) {
        vkFreeMemory(dev, src_mem, NULL);
        vkFreeMemory(dev, dst_mem, NULL);
        vkDestroyBuffer(dev, src_buf, NULL);
        vkDestroyBuffer(dev, dst_buf, NULL);
        vkDestroyDevice(dev, NULL);
        vkDestroyInstance(inst, NULL);
        return vk_fail("vkCreateCommandPool", VK_ERROR_UNKNOWN);
    }
    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(dev, &cai, &cmd);
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(cmd, &bi);
    VkBufferCopy region = { .srcOffset = 0, .dstOffset = 0, .size = bytes };
    vkCmdCopyBuffer(cmd, src_buf, dst_buf, 1, &region);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    r = vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    if (r == VK_SUCCESS)
        r = vkQueueWaitIdle(queue);
    int bad = (r != VK_SUCCESS);
    if (!bad) {
        vkMapMemory(dev, dst_mem, 0, bytes, 0, &mapped);
        memcpy(dst, mapped, bytes);
        vkUnmapMemory(dev, dst_mem);
        bad = memcmp(src, dst, bytes) != 0;
    }

    vkDestroyCommandPool(dev, pool, NULL);
    vkFreeMemory(dev, src_mem, NULL);
    vkFreeMemory(dev, dst_mem, NULL);
    vkDestroyBuffer(dev, src_buf, NULL);
    vkDestroyBuffer(dev, dst_buf, NULL);
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(inst, NULL);
    if (bad)
        return vk_fail("copy-or-mismatch", r);
    return 0;
}

int vk_copy_software_vram(const uint16_t* vram, uint16_t* out, size_t nbytes) {
    if (!vram || !out || nbytes == 0)
        return 1;
    return vk_buffer_copy_roundtrip(vram, out, nbytes);
}
