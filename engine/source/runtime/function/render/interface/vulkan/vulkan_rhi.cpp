#include "runtime/function/render/interface/vulkan/vulkan_rhi.h"
#include "runtime/function/render/interface/vulkan/vulkan_util.h"

#include "runtime/function/render/window_system.h"
#include "runtime/core/base/macro.h"

#include <algorithm>
#include <cmath>

// https://gcc.gnu.org/onlinedocs/cpp/Stringizing.html
#define PICCOLO_XSTR(s) PICCOLO_STR(s)
#define PICCOLO_STR(s) #s

#if defined(__GNUC__)
// https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html
#if defined(__linux__)
#include <stdlib.h>
#elif defined(__MACH__)
// https://developer.apple.com/library/archive/documentation/Porting/Conceptual/PortingUnix/compiling/compiling.html
#include <stdlib.h>
#else
#error Unknown Platform
#endif
#elif defined(_MSC_VER)
// https://docs.microsoft.com/en-us/cpp/preprocessor/predefined-macros
#include <sdkddkver.h>
#define WIN32_LEAN_AND_MEAN 1
#define NOGDICAPMASKS 1
#define NOVIRTUALKEYCODES 1
#define NOWINMESSAGES 1
#define NOWINSTYLES 1
#define NOSYSMETRICS 1
#define NOMENUS 1
#define NOICONS 1
#define NOKEYSTATES 1
#define NOSYSCOMMANDS 1
#define NORASTEROPS 1
#define NOSHOWWINDOW 1
#define NOATOM 1
#define NOCLIPBOARD 1
#define NOCOLOR 1
#define NOCTLMGR 1
#define NODRAWTEXT 1
#define NOGDI 1
#define NOKERNEL 1
#define NOUSER 1
#define NONLS 1
#define NOMB 1
#define NOMEMMGR 1
#define NOMETAFILE 1
#define NOMINMAX 1
#define NOMSG 1
#define NOOPENFILE 1
#define NOSCROLL 1
#define NOSERVICE 1
#define NOSOUND 1
#define NOTEXTMETRIC 1
#define NOWH 1
#define NOWINOFFSETS 1
#define NOCOMM 1
#define NOKANJI 1
#define NOHELP 1
#define NOPROFILER 1
#define NODEFERWINDOWPOS 1
#define NOMCX 1
#include <Windows.h>
#else
#error Unknown Compiler
#endif

#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace Piccolo
{
    VulkanRHI::~VulkanRHI()
    {
        // TODO
    }

    void VulkanRHI::initialize(RHIInitInfo init_info)
    {
        m_window = init_info.window_system->getWindow();

        std::array<int, 2> window_size = init_info.window_system->getWindowSize();

        m_viewport = {0.0f, 0.0f, (float)window_size[0], (float)window_size[1], 0.0f, 1.0f};
        m_scissor  = {{0, 0}, {(uint32_t)window_size[0], (uint32_t)window_size[1]}};

#ifndef NDEBUG
        m_enable_validation_Layers = true;
        m_enable_debug_utils_label = true;
#else
        m_enable_validation_Layers  = false;
        m_enable_debug_utils_label  = false;
#endif

#if defined(__GNUC__) && defined(__MACH__)
        m_enable_point_light_shadow = false;
#else
        m_enable_point_light_shadow = true;
#endif

#if defined(__GNUC__)
        // https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html
#if defined(__linux__)
        char const* vk_layer_path = PICCOLO_XSTR(PICCOLO_VK_LAYER_PATH);
        setenv("VK_LAYER_PATH", vk_layer_path, 1);
#elif defined(__MACH__)
        // https://developer.apple.com/library/archive/documentation/Porting/Conceptual/PortingUnix/compiling/compiling.html
        char const* vk_layer_path    = PICCOLO_XSTR(PICCOLO_VK_LAYER_PATH);
        char const* vk_icd_filenames = PICCOLO_XSTR(PICCOLO_VK_ICD_FILENAMES);
        setenv("VK_LAYER_PATH", vk_layer_path, 1);
        setenv("VK_ICD_FILENAMES", vk_icd_filenames, 1);
#else
#error Unknown Platform
#endif
#elif defined(_MSC_VER)
        // https://docs.microsoft.com/en-us/cpp/preprocessor/predefined-macros
        char const* vk_layer_path = PICCOLO_XSTR(PICCOLO_VK_LAYER_PATH);
        SetEnvironmentVariableA("VK_LAYER_PATH", vk_layer_path);
        SetEnvironmentVariableA("DISABLE_LAYER_AMD_SWITCHABLE_GRAPHICS_1", "1");
#else
#error Unknown Compiler
#endif
        // 在Vulkan当中的画一个三角形流程可以分为如下:

        // 创建一个 VkInstance 
        // 选择支持的硬件设备（VkPhysicalDevice）
        // 创建用于Draw和Presentation的VkDevice 和 VkQueue
        // 创建窗口(window)、窗口表面(window surface) 和交换链(Swap Chain)
        // 将Swap Chain Image 包装到 VkImageView
        // 创建一个指定Render Target和用途的RenderPass
        // 为RenderPass创建FrameBuffer
        // 设置PipeLine
        // 为每个可能的Swap Chain Image分配并记录带有绘制命令的Command Buffer
        // 通过从Swap Chain获取图像在上面绘制，提交正确的Commander Buffer，并将绘制完的图像返回到Swap Chain去显示。
        createInstance();

        initializeDebugMessenger();

        // 由于窗口表面对物理设备的选择有一定影响，它的创建只能在Vulkan实例创建之后、初始化物理设备之前进行
        createWindowSurface();

        initializePhysicalDevice();

        createLogicalDevice();

        createCommandPool();

        createCommandBuffers();

        createDescriptorPool();

        createSyncPrimitives();

        createSwapchain();
        // 在initVulkan函数调用createSwapChain函数创建交换链之后调用创建交换链image views
        createSwapchainImageViews();

        createFramebufferImageAndView();

        createAssetAllocator();
    }

    void VulkanRHI::prepareContext()
    {
        m_vk_current_command_buffer = m_vk_command_buffers[m_current_frame_index];
        ((VulkanCommandBuffer*)m_current_command_buffer)->setResource(m_vk_current_command_buffer);
    }

    void VulkanRHI::clear()
    {
        if (m_enable_validation_Layers)
        {
            destroyDebugUtilsMessengerEXT(m_instance, m_debug_messenger, nullptr);
        }
    }

    void VulkanRHI::waitForFences()
    {
        VkResult res_wait_for_fences =
            _vkWaitForFences(m_device, 1, &m_is_frame_in_flight_fences[m_current_frame_index], VK_TRUE, UINT64_MAX);
        if (VK_SUCCESS != res_wait_for_fences)
        {
            LOG_ERROR("failed to synchronize!");
        }
    }

    bool VulkanRHI::waitForFences(uint32_t fenceCount, const RHIFence* const* pFences, RHIBool32 waitAll, uint64_t timeout)
    {
        //fence
        int fence_size = fenceCount;
        std::vector<VkFence> vk_fence_list(fence_size);
        for (int i = 0; i < fence_size; ++i)
        {
            const auto& rhi_fence_element = pFences[i];
            auto& vk_fence_element = vk_fence_list[i];

            vk_fence_element = ((VulkanFence*)rhi_fence_element)->getResource();
        };

        VkResult result = vkWaitForFences(m_device, fenceCount, vk_fence_list.data(), waitAll, timeout);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("waitForFences failed");
            return false;
        }
    }

    void VulkanRHI::getPhysicalDeviceProperties(RHIPhysicalDeviceProperties* pProperties)
    {
        VkPhysicalDeviceProperties vk_physical_device_properties;
        vkGetPhysicalDeviceProperties(m_physical_device, &vk_physical_device_properties);

        pProperties->apiVersion = vk_physical_device_properties.apiVersion;
        pProperties->driverVersion = vk_physical_device_properties.driverVersion;
        pProperties->vendorID = vk_physical_device_properties.vendorID;
        pProperties->deviceID = vk_physical_device_properties.deviceID;
        pProperties->deviceType = (RHIPhysicalDeviceType)vk_physical_device_properties.deviceType;
        for (uint32_t i = 0; i < RHI_MAX_PHYSICAL_DEVICE_NAME_SIZE; i++)
        {
            pProperties->deviceName[i] = vk_physical_device_properties.deviceName[i];
        }
        for (uint32_t i = 0; i < RHI_UUID_SIZE; i++)
        {
            pProperties->pipelineCacheUUID[i] = vk_physical_device_properties.pipelineCacheUUID[i];
        }
        pProperties->sparseProperties.residencyStandard2DBlockShape = (VkBool32)vk_physical_device_properties.sparseProperties.residencyStandard2DBlockShape;
        pProperties->sparseProperties.residencyStandard2DMultisampleBlockShape = (VkBool32)vk_physical_device_properties.sparseProperties.residencyStandard2DMultisampleBlockShape;
        pProperties->sparseProperties.residencyStandard3DBlockShape = (VkBool32)vk_physical_device_properties.sparseProperties.residencyStandard3DBlockShape;
        pProperties->sparseProperties.residencyAlignedMipSize = (VkBool32)vk_physical_device_properties.sparseProperties.residencyAlignedMipSize;
        pProperties->sparseProperties.residencyNonResidentStrict = (VkBool32)vk_physical_device_properties.sparseProperties.residencyNonResidentStrict;

        pProperties->limits.maxImageDimension1D = vk_physical_device_properties.limits.maxImageDimension1D;
        pProperties->limits.maxImageDimension2D = vk_physical_device_properties.limits.maxImageDimension2D;
        pProperties->limits.maxImageDimension3D = vk_physical_device_properties.limits.maxImageDimension3D;
        pProperties->limits.maxImageDimensionCube = vk_physical_device_properties.limits.maxImageDimensionCube;
        pProperties->limits.maxImageArrayLayers = vk_physical_device_properties.limits.maxImageArrayLayers;
        pProperties->limits.maxTexelBufferElements = vk_physical_device_properties.limits.maxTexelBufferElements;
        pProperties->limits.maxUniformBufferRange = vk_physical_device_properties.limits.maxUniformBufferRange;
        pProperties->limits.maxStorageBufferRange = vk_physical_device_properties.limits.maxStorageBufferRange;
        pProperties->limits.maxPushConstantsSize = vk_physical_device_properties.limits.maxPushConstantsSize;
        pProperties->limits.maxMemoryAllocationCount = vk_physical_device_properties.limits.maxMemoryAllocationCount;
        pProperties->limits.maxSamplerAllocationCount = vk_physical_device_properties.limits.maxSamplerAllocationCount;
        pProperties->limits.bufferImageGranularity = (VkDeviceSize)vk_physical_device_properties.limits.bufferImageGranularity;
        pProperties->limits.sparseAddressSpaceSize = (VkDeviceSize)vk_physical_device_properties.limits.sparseAddressSpaceSize;
        pProperties->limits.maxBoundDescriptorSets = vk_physical_device_properties.limits.maxBoundDescriptorSets;
        pProperties->limits.maxPerStageDescriptorSamplers = vk_physical_device_properties.limits.maxPerStageDescriptorSamplers;
        pProperties->limits.maxPerStageDescriptorUniformBuffers = vk_physical_device_properties.limits.maxPerStageDescriptorUniformBuffers;
        pProperties->limits.maxPerStageDescriptorStorageBuffers = vk_physical_device_properties.limits.maxPerStageDescriptorStorageBuffers;
        pProperties->limits.maxPerStageDescriptorSampledImages = vk_physical_device_properties.limits.maxPerStageDescriptorSampledImages;
        pProperties->limits.maxPerStageDescriptorStorageImages = vk_physical_device_properties.limits.maxPerStageDescriptorStorageImages;
        pProperties->limits.maxPerStageDescriptorInputAttachments = vk_physical_device_properties.limits.maxPerStageDescriptorInputAttachments;
        pProperties->limits.maxPerStageResources = vk_physical_device_properties.limits.maxPerStageResources;
        pProperties->limits.maxDescriptorSetSamplers = vk_physical_device_properties.limits.maxDescriptorSetSamplers;
        pProperties->limits.maxDescriptorSetUniformBuffers = vk_physical_device_properties.limits.maxDescriptorSetUniformBuffers;
        pProperties->limits.maxDescriptorSetUniformBuffersDynamic = vk_physical_device_properties.limits.maxDescriptorSetUniformBuffersDynamic;
        pProperties->limits.maxDescriptorSetStorageBuffers = vk_physical_device_properties.limits.maxDescriptorSetStorageBuffers;
        pProperties->limits.maxDescriptorSetStorageBuffersDynamic = vk_physical_device_properties.limits.maxDescriptorSetStorageBuffersDynamic;
        pProperties->limits.maxDescriptorSetSampledImages = vk_physical_device_properties.limits.maxDescriptorSetSampledImages;
        pProperties->limits.maxDescriptorSetStorageImages = vk_physical_device_properties.limits.maxDescriptorSetStorageImages;
        pProperties->limits.maxDescriptorSetInputAttachments = vk_physical_device_properties.limits.maxDescriptorSetInputAttachments;
        pProperties->limits.maxVertexInputAttributes = vk_physical_device_properties.limits.maxVertexInputAttributes;
        pProperties->limits.maxVertexInputBindings = vk_physical_device_properties.limits.maxVertexInputBindings;
        pProperties->limits.maxVertexInputAttributeOffset = vk_physical_device_properties.limits.maxVertexInputAttributeOffset;
        pProperties->limits.maxVertexInputBindingStride = vk_physical_device_properties.limits.maxVertexInputBindingStride;
        pProperties->limits.maxVertexOutputComponents = vk_physical_device_properties.limits.maxVertexOutputComponents;
        pProperties->limits.maxTessellationGenerationLevel = vk_physical_device_properties.limits.maxTessellationGenerationLevel;
        pProperties->limits.maxTessellationPatchSize = vk_physical_device_properties.limits.maxTessellationPatchSize;
        pProperties->limits.maxTessellationControlPerVertexInputComponents = vk_physical_device_properties.limits.maxTessellationControlPerVertexInputComponents;
        pProperties->limits.maxTessellationControlPerVertexOutputComponents = vk_physical_device_properties.limits.maxTessellationControlPerVertexOutputComponents;
        pProperties->limits.maxTessellationControlPerPatchOutputComponents = vk_physical_device_properties.limits.maxTessellationControlPerPatchOutputComponents;
        pProperties->limits.maxTessellationControlTotalOutputComponents = vk_physical_device_properties.limits.maxTessellationControlTotalOutputComponents;
        pProperties->limits.maxTessellationEvaluationInputComponents = vk_physical_device_properties.limits.maxTessellationEvaluationInputComponents;
        pProperties->limits.maxTessellationEvaluationOutputComponents = vk_physical_device_properties.limits.maxTessellationEvaluationOutputComponents;
        pProperties->limits.maxGeometryShaderInvocations = vk_physical_device_properties.limits.maxGeometryShaderInvocations;
        pProperties->limits.maxGeometryInputComponents = vk_physical_device_properties.limits.maxGeometryInputComponents;
        pProperties->limits.maxGeometryOutputComponents = vk_physical_device_properties.limits.maxGeometryOutputComponents;
        pProperties->limits.maxGeometryOutputVertices = vk_physical_device_properties.limits.maxGeometryOutputVertices;
        pProperties->limits.maxGeometryTotalOutputComponents = vk_physical_device_properties.limits.maxGeometryTotalOutputComponents;
        pProperties->limits.maxFragmentInputComponents = vk_physical_device_properties.limits.maxFragmentInputComponents;
        pProperties->limits.maxFragmentOutputAttachments = vk_physical_device_properties.limits.maxFragmentOutputAttachments;
        pProperties->limits.maxFragmentDualSrcAttachments = vk_physical_device_properties.limits.maxFragmentDualSrcAttachments;
        pProperties->limits.maxFragmentCombinedOutputResources = vk_physical_device_properties.limits.maxFragmentCombinedOutputResources;
        pProperties->limits.maxComputeSharedMemorySize = vk_physical_device_properties.limits.maxComputeSharedMemorySize;
        for (uint32_t i = 0; i < 3; i++)
        {
            pProperties->limits.maxComputeWorkGroupCount[i] = vk_physical_device_properties.limits.maxComputeWorkGroupCount[i];
        }
        pProperties->limits.maxComputeWorkGroupInvocations = vk_physical_device_properties.limits.maxComputeWorkGroupInvocations;
        for (uint32_t i = 0; i < 3; i++)
        {
            pProperties->limits.maxComputeWorkGroupSize[i] = vk_physical_device_properties.limits.maxComputeWorkGroupSize[i];
        }
        pProperties->limits.subPixelPrecisionBits = vk_physical_device_properties.limits.subPixelPrecisionBits;
        pProperties->limits.subTexelPrecisionBits = vk_physical_device_properties.limits.subTexelPrecisionBits;
        pProperties->limits.mipmapPrecisionBits = vk_physical_device_properties.limits.mipmapPrecisionBits;
        pProperties->limits.maxDrawIndexedIndexValue = vk_physical_device_properties.limits.maxDrawIndexedIndexValue;
        pProperties->limits.maxDrawIndirectCount = vk_physical_device_properties.limits.maxDrawIndirectCount;
        pProperties->limits.maxSamplerLodBias = vk_physical_device_properties.limits.maxSamplerLodBias;
        pProperties->limits.maxSamplerAnisotropy = vk_physical_device_properties.limits.maxSamplerAnisotropy;
        pProperties->limits.maxViewports = vk_physical_device_properties.limits.maxViewports;
        for (uint32_t i = 0; i < 2; i++)
        {
            pProperties->limits.maxViewportDimensions[i] = vk_physical_device_properties.limits.maxViewportDimensions[i];
        }
        for (uint32_t i = 0; i < 2; i++)
        {
            pProperties->limits.viewportBoundsRange[i] = vk_physical_device_properties.limits.viewportBoundsRange[i];
        }
        pProperties->limits.viewportSubPixelBits = vk_physical_device_properties.limits.viewportSubPixelBits;
        pProperties->limits.minMemoryMapAlignment = vk_physical_device_properties.limits.minMemoryMapAlignment;
        pProperties->limits.minTexelBufferOffsetAlignment = (VkDeviceSize)vk_physical_device_properties.limits.minTexelBufferOffsetAlignment;
        pProperties->limits.minUniformBufferOffsetAlignment = (VkDeviceSize)vk_physical_device_properties.limits.minUniformBufferOffsetAlignment;
        pProperties->limits.minStorageBufferOffsetAlignment = (VkDeviceSize)vk_physical_device_properties.limits.minStorageBufferOffsetAlignment;
        pProperties->limits.minTexelOffset = vk_physical_device_properties.limits.minTexelOffset;
        pProperties->limits.maxTexelOffset = vk_physical_device_properties.limits.maxTexelOffset;
        pProperties->limits.minTexelGatherOffset = vk_physical_device_properties.limits.minTexelGatherOffset;
        pProperties->limits.maxTexelGatherOffset = vk_physical_device_properties.limits.maxTexelGatherOffset;
        pProperties->limits.minInterpolationOffset = vk_physical_device_properties.limits.minInterpolationOffset;
        pProperties->limits.maxInterpolationOffset = vk_physical_device_properties.limits.maxInterpolationOffset;
        pProperties->limits.subPixelInterpolationOffsetBits = vk_physical_device_properties.limits.subPixelInterpolationOffsetBits;
        pProperties->limits.maxFramebufferWidth = vk_physical_device_properties.limits.maxFramebufferWidth;
        pProperties->limits.maxFramebufferHeight = vk_physical_device_properties.limits.maxFramebufferHeight;
        pProperties->limits.maxFramebufferLayers = vk_physical_device_properties.limits.maxFramebufferLayers;
        pProperties->limits.framebufferColorSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.framebufferColorSampleCounts;
        pProperties->limits.framebufferDepthSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.framebufferDepthSampleCounts;
        pProperties->limits.framebufferStencilSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.framebufferStencilSampleCounts;
        pProperties->limits.framebufferNoAttachmentsSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.framebufferNoAttachmentsSampleCounts;
        pProperties->limits.maxColorAttachments = vk_physical_device_properties.limits.maxColorAttachments;
        pProperties->limits.sampledImageColorSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.sampledImageColorSampleCounts;
        pProperties->limits.sampledImageIntegerSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.sampledImageIntegerSampleCounts;
        pProperties->limits.sampledImageDepthSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.sampledImageDepthSampleCounts;
        pProperties->limits.sampledImageStencilSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.sampledImageStencilSampleCounts;
        pProperties->limits.storageImageSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.storageImageSampleCounts;
        pProperties->limits.maxSampleMaskWords = vk_physical_device_properties.limits.maxSampleMaskWords;
        pProperties->limits.timestampComputeAndGraphics = (VkBool32)vk_physical_device_properties.limits.timestampComputeAndGraphics;
        pProperties->limits.timestampPeriod = vk_physical_device_properties.limits.timestampPeriod;
        pProperties->limits.maxClipDistances = vk_physical_device_properties.limits.maxClipDistances;
        pProperties->limits.maxCullDistances = vk_physical_device_properties.limits.maxCullDistances;
        pProperties->limits.maxCombinedClipAndCullDistances = vk_physical_device_properties.limits.maxCombinedClipAndCullDistances;
        pProperties->limits.discreteQueuePriorities = vk_physical_device_properties.limits.discreteQueuePriorities;
        for (uint32_t i = 0; i < 2; i++)
        {
            pProperties->limits.pointSizeRange[i] = vk_physical_device_properties.limits.pointSizeRange[i];
        }
        for (uint32_t i = 0; i < 2; i++)
        {
            pProperties->limits.lineWidthRange[i] = vk_physical_device_properties.limits.lineWidthRange[i];
        }
        pProperties->limits.pointSizeGranularity = vk_physical_device_properties.limits.pointSizeGranularity;
        pProperties->limits.lineWidthGranularity = vk_physical_device_properties.limits.lineWidthGranularity;
        pProperties->limits.strictLines = (VkBool32)vk_physical_device_properties.limits.strictLines;
        pProperties->limits.standardSampleLocations = (VkBool32)vk_physical_device_properties.limits.standardSampleLocations;
        pProperties->limits.optimalBufferCopyOffsetAlignment = (VkDeviceSize)vk_physical_device_properties.limits.optimalBufferCopyOffsetAlignment;
        pProperties->limits.optimalBufferCopyRowPitchAlignment = (VkDeviceSize)vk_physical_device_properties.limits.optimalBufferCopyRowPitchAlignment;
        pProperties->limits.nonCoherentAtomSize = (VkDeviceSize)vk_physical_device_properties.limits.nonCoherentAtomSize;

    }

    void VulkanRHI::resetCommandPool()
    {
        VkResult res_reset_command_pool = _vkResetCommandPool(m_device, m_command_pools[m_current_frame_index], 0);
        if (VK_SUCCESS != res_reset_command_pool)
        {
            LOG_ERROR("failed to synchronize");
        }
    }

    bool VulkanRHI::prepareBeforePass(std::function<void()> passUpdateAfterRecreateSwapchain)
    {
        VkResult acquire_image_result =
            vkAcquireNextImageKHR(m_device,
                                  m_swapchain,
                                  UINT64_MAX,
                                  m_image_available_for_render_semaphores[m_current_frame_index],
                                  VK_NULL_HANDLE,
                                  &m_current_swapchain_image_index);

        if (VK_ERROR_OUT_OF_DATE_KHR == acquire_image_result)
        {
            recreateSwapchain();
            passUpdateAfterRecreateSwapchain();
            return RHI_SUCCESS;
        }
        else if (VK_SUBOPTIMAL_KHR == acquire_image_result)
        {
            recreateSwapchain();
            passUpdateAfterRecreateSwapchain();

            // NULL submit to wait semaphore
            VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT};
            VkSubmitInfo         submit_info   = {};
            submit_info.sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.waitSemaphoreCount     = 1;
            submit_info.pWaitSemaphores        = &m_image_available_for_render_semaphores[m_current_frame_index];
            submit_info.pWaitDstStageMask      = wait_stages;
            submit_info.commandBufferCount     = 0;
            submit_info.pCommandBuffers        = NULL;
            submit_info.signalSemaphoreCount   = 0;
            submit_info.pSignalSemaphores      = NULL;

            VkResult res_reset_fences = _vkResetFences(m_device, 1, &m_is_frame_in_flight_fences[m_current_frame_index]);
            if (VK_SUCCESS != res_reset_fences)
            {
                LOG_ERROR("_vkResetFences failed!");
                return false;
            }

            VkResult res_queue_submit =
                vkQueueSubmit(((VulkanQueue*)m_graphics_queue)->getResource(), 1, &submit_info, m_is_frame_in_flight_fences[m_current_frame_index]);
            if (VK_SUCCESS != res_queue_submit)
            {
                LOG_ERROR("vkQueueSubmit failed!");
                return false;
            }
            m_current_frame_index = (m_current_frame_index + 1) % k_max_frames_in_flight;
            return RHI_SUCCESS;
        }
        else
        {
            if (VK_SUCCESS != acquire_image_result)
            {
                LOG_ERROR("vkAcquireNextImageKHR failed!");
                return false;
            }
        }

        // begin command buffer
        VkCommandBufferBeginInfo command_buffer_begin_info {};
        command_buffer_begin_info.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags            = 0;
        command_buffer_begin_info.pInheritanceInfo = nullptr;

        VkResult res_begin_command_buffer =
            _vkBeginCommandBuffer(m_vk_command_buffers[m_current_frame_index], &command_buffer_begin_info);

        if (VK_SUCCESS != res_begin_command_buffer)
        {
            LOG_ERROR("_vkBeginCommandBuffer failed!");
            return false;
        }
        return false;
    }

    // 从交换链获取一张图像 vkAcquireNextImageKHR
    // 对帧缓冲附着(framebuffer attachment)执行指令缓冲中的渲染指令 vkQueueSubmit
    // 返回渲染后的图像到交换链进行呈现操作
    void VulkanRHI::submitRendering(std::function<void()> passUpdateAfterRecreateSwapchain)
    {
        // end command buffer
        VkResult res_end_command_buffer = _vkEndCommandBuffer(m_vk_command_buffers[m_current_frame_index]);
        if (VK_SUCCESS != res_end_command_buffer)
        {
            LOG_ERROR("_vkEndCommandBuffer failed!");
            return;
        }

        VkSemaphore semaphores[2] = { ((VulkanSemaphore*)m_image_available_for_texturescopy_semaphores[m_current_frame_index])->getResource(),
                                     m_image_finished_for_presentation_semaphores[m_current_frame_index] };

        // submit command buffer
        // 我们通过VkSubmitInfo结构体来提交指令缓冲给指令队列
        // VkSubmitInfo结构体的waitSemaphoreCount、pWaitSemaphores和pWaitDstStageMask成员变量
        // 用于指定队列开始执行前需要等待的信号量，以及需要等待的管线阶段
        // 
        // 重要的是提交的数量是指在一帧中所有vkQueueSubmit调用中提交的VkSubmitInfo结构的数量，而不是vkQueueSubmit调用本身的数量。
        // 例如当提交 10 个Command Buffer时，使用一个提交10个Command Buffer的 VkSubmitInfo比
        // 使用10个每个有一个Command Buffer的 VkSubmitInfo 结构要有效率的多，即使在这两种情况下都是只执行一次vkQueueSubmit，
        // 但是提交的Command数量完全不同。从本质上讲VkSubmitInfo是GPU上的一个同步/调度单元，因为它有自己的一套Fence/Semaphores
        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo         submit_info   = {};
        submit_info.sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount     = 1;
        submit_info.pWaitSemaphores        = &m_image_available_for_render_semaphores[m_current_frame_index];
        submit_info.pWaitDstStageMask      = wait_stages;
        submit_info.commandBufferCount     = 1;
        submit_info.pCommandBuffers        = &m_vk_command_buffers[m_current_frame_index];
        submit_info.signalSemaphoreCount = 2;
        submit_info.pSignalSemaphores = semaphores;

        VkResult res_reset_fences = _vkResetFences(m_device, 1, &m_is_frame_in_flight_fences[m_current_frame_index]);

        if (VK_SUCCESS != res_reset_fences)
        {
            LOG_ERROR("_vkResetFences failed!");
            return;
        }
        VkResult res_queue_submit =
            vkQueueSubmit(((VulkanQueue*)m_graphics_queue)->getResource(), 1, &submit_info, m_is_frame_in_flight_fences[m_current_frame_index]);
        
        if (VK_SUCCESS != res_queue_submit)
        {
            LOG_ERROR("vkQueueSubmit failed!");
            return;
        }

        // present swapchain
        // 渲染操作执行后，我们需要将渲染的图像返回给交换链进行呈现操作。
        // 我们在drawFrame函数的尾部通过VkPresentInfoKHR结构体来配置呈现信息
        VkPresentInfoKHR present_info   = {};
        present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores    = &m_image_finished_for_presentation_semaphores[m_current_frame_index];
        present_info.swapchainCount     = 1;
        present_info.pSwapchains        = &m_swapchain;
        present_info.pImageIndices      = &m_current_swapchain_image_index;

        // 根据vkAcquireNextImageKHR或vkQueuePresentKHR函数返回的信息来判定交换链是否需要重建
        VkResult present_result = vkQueuePresentKHR(m_present_queue, &present_info);
        // 显式处理窗口大小改变（在窗口大小改变后触发VK_ERROR_OUT_OF_DATE_KHR信息）
        // VK_ERROR_OUT_OF_DATE_KHR：交换链不能继续使用。通常发生在窗口大小改变后。
        // VK_SUBOPTIMAL_KHR：交换链仍然可以使用，但表面属性已经不能准确匹配
        if (VK_ERROR_OUT_OF_DATE_KHR == present_result || VK_SUBOPTIMAL_KHR == present_result)
        {
            recreateSwapchain();
            passUpdateAfterRecreateSwapchain();
        }
        else
        {
            if (VK_SUCCESS != present_result)
            {
                LOG_ERROR("vkQueuePresentKHR failed!");
                return;
            }
        }

        m_current_frame_index = (m_current_frame_index + 1) % k_max_frames_in_flight;
    }

    RHICommandBuffer* VulkanRHI::beginSingleTimeCommands()
    {
        VkCommandBufferAllocateInfo allocInfo {};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = ((VulkanCommandPool*)m_rhi_command_pool)->getResource();
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer command_buffer;
        vkAllocateCommandBuffers(m_device, &allocInfo, &command_buffer);

        VkCommandBufferBeginInfo beginInfo {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        _vkBeginCommandBuffer(command_buffer, &beginInfo);

        RHICommandBuffer* rhi_command_buffer = new VulkanCommandBuffer();
        ((VulkanCommandBuffer*)rhi_command_buffer)->setResource(command_buffer);
        return rhi_command_buffer;
    }

    void VulkanRHI::endSingleTimeCommands(RHICommandBuffer* command_buffer)
    {
        VkCommandBuffer vk_command_buffer = ((VulkanCommandBuffer*)command_buffer)->getResource();
        _vkEndCommandBuffer(vk_command_buffer);

        VkSubmitInfo submitInfo {};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &vk_command_buffer;

        vkQueueSubmit(((VulkanQueue*)m_graphics_queue)->getResource(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(((VulkanQueue*)m_graphics_queue)->getResource());

        vkFreeCommandBuffers(m_device, ((VulkanCommandPool*)m_rhi_command_pool)->getResource(), 1, &vk_command_buffer);
        delete(command_buffer);
    }

    // validation layers
    // 请求所有可用的校验层
    bool VulkanRHI::checkValidationLayerSupport()
    {
        // 调用vkEnumerateInstanceLayerProperties函数获取了所有可用的校验层列表
        // 这一函数的用法和创建Vulkan实例时使用的vkEnumerateInstanceExtensionProperties函数相同
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        // 检查是否所有m_validation_layers列表中的校验层都可以在availableLayers列表中找到
        for (const char* layerName : m_validation_layers)
        {
            bool layerFound = false;

            for (const auto& layerProperties : availableLayers)
            {
                if (strcmp(layerName, layerProperties.layerName) == 0)
                {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound)
            {
                return false;
            }
        }

        return RHI_SUCCESS;
    }

    // getRequiredExtensions函数根据是否启用校验层，返回所需的扩展列表
    // 这里主要是检查直接调用glfw的函数获取和窗口系统交互的扩展
    std::vector<const char*> VulkanRHI::getRequiredExtensions()
    {
        uint32_t     glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        // 代码中使用了一个VK_EXT_DEBUG_UTILS_EXTENSION_NAME，它等价于VK_EXT_debug_utils
        // 使用该扩展设置回调函数来接受调试信息
        if (m_enable_validation_Layers || m_enable_debug_utils_label)
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

#if defined(__MACH__)
        extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif

        return extensions;
    }

    // debug callback 校验层消息回调
    // 以vkDebugUtilsMessengerCallbackEXT::pfnUserCallback为原型添加一个叫做debugCallback的静态函数。这一函数使用VKAPI_ATTR和VKAPI_CALL定义，确保它可以被Vulkan库调用
    // 参数列表可见https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/PFN_vkDebugUtilsMessengerCallbackEXT.html
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT,
                                                        VkDebugUtilsMessageTypeFlagsEXT,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                        void*)
    {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
        return VK_FALSE;
    }

    void VulkanRHI::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
    {
        createInfo       = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
    }

    // 创建Vulkan实例(VkInstance)
    void VulkanRHI::createInstance()
    {
        // validation layer will be enabled in debug mode
        if (m_enable_validation_Layers && !checkValidationLayerSupport())
        {
            LOG_ERROR("validation layers requested, but not available!");
        }

        m_vulkan_api_version = VK_API_VERSION_1_0;

        // app info 
        // 填写应用程序信息，这些信息的填写不是必须的，但填写的信息可能会作为驱动程序的优化依据，让驱动程序进行一些特殊的优化
        // Vulkan的很多结构体需要我们显式地在sType成员变量中指定结构体的类型
        // 此外，许多Vulkan的结构体还有一个pNext成员变量，用来指向未来可能扩展的参数信息，现在，我们并没有使用它，默认设置为nullptr
        VkApplicationInfo appInfo {};
        // sType: https://stackoverflow.com/questions/36347236/vulkan-what-is-the-point-of-stype-in-vkcreateinfo-structs
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "piccolo_renderer";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName        = "Piccolo";
        appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion         = m_vulkan_api_version;

        // create info
        VkInstanceCreateInfo instance_create_info {};
        instance_create_info.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_create_info.pApplicationInfo = &appInfo; // the appInfo is stored here

        // 获取需要的全局扩展，比如一个和窗口系统交互的扩展(glfw)
        auto extensions                              = getRequiredExtensions();
        instance_create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
        instance_create_info.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo {};
        // 在校验层启用时使用校验层
        if (m_enable_validation_Layers)
        {
            instance_create_info.enabledLayerCount   = static_cast<uint32_t>(m_validation_layers.size());
            instance_create_info.ppEnabledLayerNames = m_validation_layers.data();

            // 向VkDebugUtilsMessengerCreateInfoEXT结构体中填入必要信息
            // 不过这里好像没必要吧，initializeDebugMessenger()函数里面已经完全实现了这一步了，pNext一般用于扩展
            // populateDebugMessengerCreateInfo(debugCreateInfo);
            // instance_create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
        }
        else
        {
            instance_create_info.enabledLayerCount = 0;
            instance_create_info.pNext             = nullptr;
        }

        // create m_vulkan_context._instance
        // 创建Vulkan对象的函数参数的一般形式就是：
        // 1、一个包含了创建信息的结构体指针
        // 2、一个自定义的分配器回调函数，这里没有使用自定义的分配器，总是将它设置为nullptr
        // 3、一个指向新对象句柄存储位置的指针
        if (vkCreateInstance(&instance_create_info, nullptr, &m_instance) != VK_SUCCESS)
        {
            LOG_ERROR("vk create instance");
        }
    }

    void VulkanRHI::initializeDebugMessenger()
    {
        if (m_enable_validation_Layers)
        {
            VkDebugUtilsMessengerCreateInfoEXT createInfo;
            populateDebugMessengerCreateInfo(createInfo);
            // 将createInfo作为一个参数调用vkCreateDebugUtilsMessengerEXT函数来创建VkDebugUtilsMessengerEXT对象
            if (VK_SUCCESS != createDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debug_messenger))
            {
                LOG_ERROR("failed to set up debug messenger!");
            }
        }

        if (m_enable_debug_utils_label)
        {
            // vkCmdBeginDebugUtilsLabelEXT - Open a command buffer debug label region
            _vkCmdBeginDebugUtilsLabelEXT =
                (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(m_instance, "vkCmdBeginDebugUtilsLabelEXT");
            _vkCmdEndDebugUtilsLabelEXT =
                (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(m_instance, "vkCmdEndDebugUtilsLabelEXT");
        }
    }

    void VulkanRHI::createWindowSurface()
    {
        // 使用GLFW库的glfwCreateWindowSurface函数来完成窗口表面创建（window surface用于在屏幕上展示绘制内容）
        // https://zhuanlan.zhihu.com/p/56795405
        if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS)
        {
            LOG_ERROR("glfwCreateWindowSurface failed!");
        }

    }

    // 初始化物理设备（一般指GPU）
    void VulkanRHI::initializePhysicalDevice()
    {
        uint32_t physical_device_count;
        vkEnumeratePhysicalDevices(m_instance, &physical_device_count, nullptr); // 查询Vulkan支持的硬件
        if (physical_device_count == 0)
        {
            LOG_ERROR("enumerate physical devices failed!");
        }
        else
        {
            // find one device that matches our requirement
            // or find which is the best
            std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
            vkEnumeratePhysicalDevices(m_instance, &physical_device_count, physical_devices.data());

            std::vector<std::pair<int, VkPhysicalDevice>> ranked_physical_devices;
            for (const auto& device : physical_devices)
            {
                VkPhysicalDeviceProperties physical_device_properties; // 用于存放GPU设备信息的数据结构
                vkGetPhysicalDeviceProperties(device, &physical_device_properties); // 通过GPU设备的句柄，查询GPU设备的名称，属性，功能等等
                int score = 0;
                // 根据deviceType对每个设备进行评分
                if (physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                {
                    score += 1000;
                }
                else if (physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                {
                    score += 100;
                }

                ranked_physical_devices.push_back({score, device});
            }
            // 排序找出最优，lambda表达式传入两个参数并比较大小（std::pair直接比较大小，先比较first，再比较second）
            std::sort(ranked_physical_devices.begin(),
                      ranked_physical_devices.end(),
                      [](const std::pair<int, VkPhysicalDevice>& p1, const std::pair<int, VkPhysicalDevice>& p2) {
                          return p1 > p2;
                      });

            for (const auto& device : ranked_physical_devices)
            {
                if (isDeviceSuitable(device.second))
                {
                    m_physical_device = device.second;
                    break;
                }
            }

            if (m_physical_device == VK_NULL_HANDLE)
            {
                LOG_ERROR("failed to find suitable physical device");
            }
        }
    }

    // logical device (m_vulkan_context._device : graphic queue, present queue,
    // feature:samplerAnisotropy)
    // 选择物理设备后，我们还需要一个逻辑设备来作为和物理设备交互的接口
    // 逻辑设备的创建过程类似于我们之前描述的Vulkan实例的创建过程
    // 我们还需要指定使用的队列所属的队列族，对于同一个物理设备，我们可以根据需求的不同，创建多个逻辑设备
    void VulkanRHI::createLogicalDevice()
    {
        m_queue_indices = findQueueFamilies(m_physical_device);
        
        // 需要多个VkDeviceQueueCreateInfo结构体来创建所有使用的队列族，一个优雅的处理方式是使用STL的集合创建每一个不同的队列族
        std::vector<VkDeviceQueueCreateInfo> queue_create_infos; // all queues that need to be created
        std::set<uint32_t>                   queue_families = {m_queue_indices.graphics_family.value(),
                                             m_queue_indices.present_family.value(),
                                             m_queue_indices.m_compute_family.value()};

        float queue_priority = 1.0f;
        for (uint32_t queue_family : queue_families) // for every queue family
        {
            // queue create info
            // 对于每个队列族，驱动程序只允许创建很少数量的队列，但实际上，对于每一个队列族
            // 我们很少需要一个以上的队列。我们可以在多个线程创建指令缓冲，然后在主线程一次将它们全部提交，降低调用开销。
            VkDeviceQueueCreateInfo queue_create_info {};
            queue_create_info.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.queueFamilyIndex = queue_family;
            queue_create_info.queueCount       = 1;
            queue_create_info.pQueuePriorities = &queue_priority;
            queue_create_infos.push_back(queue_create_info);
        }

        // physical device features 指定应用程序使用的设备特性
        VkPhysicalDeviceFeatures physical_device_features = {};

        physical_device_features.samplerAnisotropy = VK_TRUE;

        // support inefficient readback storage buffer
        physical_device_features.fragmentStoresAndAtomics = VK_TRUE;

        // support independent blending
        physical_device_features.independentBlend = VK_TRUE;

        // support geometry shader
        if (m_enable_point_light_shadow)
        {
            physical_device_features.geometryShader = VK_TRUE;
        }

        // device create info
        // 将VkDeviceCreateInfo结构体的pQueueCreateInfos指针指向queue_create_infos数据的地址
        // pEnabledFeatures指针指向physical_device_features的地址
        VkDeviceCreateInfo device_create_info {};
        device_create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.pQueueCreateInfos       = queue_create_infos.data();
        device_create_info.queueCreateInfoCount    = static_cast<uint32_t>(queue_create_infos.size());
        device_create_info.pEnabledFeatures        = &physical_device_features;
        device_create_info.enabledExtensionCount   = static_cast<uint32_t>(m_device_extensions.size()); //
        device_create_info.ppEnabledExtensionNames = m_device_extensions.data(); // 启用extensions（这里是交换链扩展）
        device_create_info.enabledLayerCount       = 0;

        // 调用vkCreateDevice函数创建逻辑设备
        // vkCreateDevice函数的参数包括我们创建的逻辑设备进行交互的物理设备对象，我们刚刚在结构体中指定的需要使用的队列信息，
        // 可选的分配器回调，以及用来存储返回的逻辑设备对象的内存地址
        if (vkCreateDevice(m_physical_device, &device_create_info, nullptr, &m_device) != VK_SUCCESS)
        {
            LOG_ERROR("vk create device");
        }

        // initialize queues of this device
        VkQueue vk_graphics_queue;
        vkGetDeviceQueue(m_device, m_queue_indices.graphics_family.value(), 0, &vk_graphics_queue);
        m_graphics_queue = new VulkanQueue();
        ((VulkanQueue*)m_graphics_queue)->setResource(vk_graphics_queue);

        vkGetDeviceQueue(m_device, m_queue_indices.present_family.value(), 0, &m_present_queue);

        VkQueue vk_compute_queue;
        vkGetDeviceQueue(m_device, m_queue_indices.m_compute_family.value(), 0, &vk_compute_queue);
        m_compute_queue = new VulkanQueue();
        ((VulkanQueue*)m_compute_queue)->setResource(vk_compute_queue);

        // more efficient pointer
        _vkResetCommandPool      = (PFN_vkResetCommandPool)vkGetDeviceProcAddr(m_device, "vkResetCommandPool");
        _vkBeginCommandBuffer    = (PFN_vkBeginCommandBuffer)vkGetDeviceProcAddr(m_device, "vkBeginCommandBuffer");
        _vkEndCommandBuffer      = (PFN_vkEndCommandBuffer)vkGetDeviceProcAddr(m_device, "vkEndCommandBuffer");
        _vkCmdBeginRenderPass    = (PFN_vkCmdBeginRenderPass)vkGetDeviceProcAddr(m_device, "vkCmdBeginRenderPass");
        _vkCmdNextSubpass        = (PFN_vkCmdNextSubpass)vkGetDeviceProcAddr(m_device, "vkCmdNextSubpass");
        _vkCmdEndRenderPass      = (PFN_vkCmdEndRenderPass)vkGetDeviceProcAddr(m_device, "vkCmdEndRenderPass");
        _vkCmdBindPipeline       = (PFN_vkCmdBindPipeline)vkGetDeviceProcAddr(m_device, "vkCmdBindPipeline");
        _vkCmdSetViewport        = (PFN_vkCmdSetViewport)vkGetDeviceProcAddr(m_device, "vkCmdSetViewport");
        _vkCmdSetScissor         = (PFN_vkCmdSetScissor)vkGetDeviceProcAddr(m_device, "vkCmdSetScissor");
        _vkWaitForFences         = (PFN_vkWaitForFences)vkGetDeviceProcAddr(m_device, "vkWaitForFences");
        _vkResetFences           = (PFN_vkResetFences)vkGetDeviceProcAddr(m_device, "vkResetFences");
        _vkCmdDrawIndexed        = (PFN_vkCmdDrawIndexed)vkGetDeviceProcAddr(m_device, "vkCmdDrawIndexed");
        _vkCmdBindVertexBuffers  = (PFN_vkCmdBindVertexBuffers)vkGetDeviceProcAddr(m_device, "vkCmdBindVertexBuffers");
        _vkCmdBindIndexBuffer    = (PFN_vkCmdBindIndexBuffer)vkGetDeviceProcAddr(m_device, "vkCmdBindIndexBuffer");
        _vkCmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets)vkGetDeviceProcAddr(m_device, "vkCmdBindDescriptorSets");
        _vkCmdClearAttachments   = (PFN_vkCmdClearAttachments)vkGetDeviceProcAddr(m_device, "vkCmdClearAttachments");

        m_depth_image_format = (RHIFormat)findDepthFormat();
    }

    void VulkanRHI::createCommandPool()
    {
        // default graphics command pool
        {
            m_rhi_command_pool = new VulkanCommandPool();
            VkCommandPool vk_command_pool;
            VkCommandPoolCreateInfo command_pool_create_info {};
            command_pool_create_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            command_pool_create_info.pNext            = NULL;
            // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT：command buffer对象之间相互独立，不会被一起重置。不使用这一标记，command buffer对象会被放在一起重置。
            command_pool_create_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            command_pool_create_info.queueFamilyIndex = m_queue_indices.graphics_family.value();

            if (vkCreateCommandPool(m_device, &command_pool_create_info, nullptr, &vk_command_pool) != VK_SUCCESS)
            {
                LOG_ERROR("vk create command pool");
            }

            ((VulkanCommandPool*)m_rhi_command_pool)->setResource(vk_command_pool);
        }

        // other command pools
        {
            VkCommandPoolCreateInfo command_pool_create_info;
            command_pool_create_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            command_pool_create_info.pNext            = NULL;
            // VK_COMMAND_POOL_CREATE_TRANSIENT_BIT：使用它分配的command buffer对象被频繁用来记录新的指令。
            command_pool_create_info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            command_pool_create_info.queueFamilyIndex = m_queue_indices.graphics_family.value();

            for (uint32_t i = 0; i < k_max_frames_in_flight; ++i)
            {
                if (vkCreateCommandPool(m_device, &command_pool_create_info, NULL, &m_command_pools[i]) != VK_SUCCESS)
                {
                    LOG_ERROR("vk create command pool");
                }
            }
        }
    }

    bool VulkanRHI::createCommandPool(const RHICommandPoolCreateInfo* pCreateInfo, RHICommandPool* &pCommandPool)
    {
        VkCommandPoolCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkCommandPoolCreateFlags)pCreateInfo->flags;
        create_info.queueFamilyIndex = pCreateInfo->queueFamilyIndex;

        pCommandPool = new VulkanCommandPool();
        VkCommandPool vk_commandPool;
        VkResult result = vkCreateCommandPool(m_device, &create_info, nullptr, &vk_commandPool);
        ((VulkanCommandPool*)pCommandPool)->setResource(vk_commandPool);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateCommandPool is failed!");
            return false;
        }
    }

    bool VulkanRHI::createDescriptorPool(const RHIDescriptorPoolCreateInfo* pCreateInfo, RHIDescriptorPool* & pDescriptorPool)
    {
        int size = pCreateInfo->poolSizeCount;
        std::vector<VkDescriptorPoolSize> descriptor_pool_size(size);
        for (int i = 0; i < size; ++i)
        {
            const auto& rhi_desc = pCreateInfo->pPoolSizes[i];
            auto& vk_desc = descriptor_pool_size[i];

            vk_desc.type = (VkDescriptorType)rhi_desc.type;
            vk_desc.descriptorCount = rhi_desc.descriptorCount;
        };

        VkDescriptorPoolCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkDescriptorPoolCreateFlags)pCreateInfo->flags;
        create_info.maxSets = pCreateInfo->maxSets;
        create_info.poolSizeCount = pCreateInfo->poolSizeCount;
        create_info.pPoolSizes = descriptor_pool_size.data();

        pDescriptorPool = new VulkanDescriptorPool();
        VkDescriptorPool vk_descriptorPool;
        VkResult result = vkCreateDescriptorPool(m_device, &create_info, nullptr, &vk_descriptorPool);
        ((VulkanDescriptorPool*)pDescriptorPool)->setResource(vk_descriptorPool);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateDescriptorPool is failed!");
            return false;
        }
    }

    // Vulkan的着色器数据绑定模型：
    // 每个着色器阶段有自己独立的命名空间，片段着色器的0号纹理绑定和顶点着色器的0号纹理绑定没有任何关系。
    // 不同类型的资源位于不同的命名空间，0号uniform缓冲绑定和0号纹理绑定没有任何关系。
    // 资源被独立地进行绑定和解绑定。
    //
    // Vulkan的基本绑定单位是描述符。描述符是一个不透明的绑定表示。它可以表示一个图像、一个采样器或一个uniform缓冲等等。它甚至可以表示数组，比如一个图像数组
    bool VulkanRHI::createDescriptorSetLayout(const RHIDescriptorSetLayoutCreateInfo* pCreateInfo, RHIDescriptorSetLayout* &pSetLayout)
    {
        //descriptor_set_layout_binding
        int descriptor_set_layout_binding_size = pCreateInfo->bindingCount;
        std::vector<VkDescriptorSetLayoutBinding> vk_descriptor_set_layout_binding_list(descriptor_set_layout_binding_size);

        int sampler_count = 0;
        for (int i = 0; i < descriptor_set_layout_binding_size; ++i)
        {
            const auto& rhi_descriptor_set_layout_binding_element = pCreateInfo->pBindings[i];
            if (rhi_descriptor_set_layout_binding_element.pImmutableSamplers != nullptr)
            {
                sampler_count += rhi_descriptor_set_layout_binding_element.descriptorCount;
            }
        }
        std::vector<VkSampler> sampler_list(sampler_count);
        int sampler_current = 0;

        for (int i = 0; i < descriptor_set_layout_binding_size; ++i)
        {
            const auto& rhi_descriptor_set_layout_binding_element = pCreateInfo->pBindings[i];
            auto& vk_descriptor_set_layout_binding_element = vk_descriptor_set_layout_binding_list[i];

            //sampler
            vk_descriptor_set_layout_binding_element.pImmutableSamplers = nullptr;
            if (rhi_descriptor_set_layout_binding_element.pImmutableSamplers)
            {
                vk_descriptor_set_layout_binding_element.pImmutableSamplers = &sampler_list[sampler_current];
                for (int i = 0; i < rhi_descriptor_set_layout_binding_element.descriptorCount; ++i)
                {
                    const auto& rhi_sampler_element = rhi_descriptor_set_layout_binding_element.pImmutableSamplers[i];
                    auto& vk_sampler_element = sampler_list[sampler_current];

                    vk_sampler_element = ((VulkanSampler*)rhi_sampler_element)->getResource();

                    sampler_current++;
                };
            }
            vk_descriptor_set_layout_binding_element.binding = rhi_descriptor_set_layout_binding_element.binding;
            vk_descriptor_set_layout_binding_element.descriptorType = (VkDescriptorType)rhi_descriptor_set_layout_binding_element.descriptorType;
            vk_descriptor_set_layout_binding_element.descriptorCount = rhi_descriptor_set_layout_binding_element.descriptorCount;
            vk_descriptor_set_layout_binding_element.stageFlags = rhi_descriptor_set_layout_binding_element.stageFlags;
        };
        
        if (sampler_count != sampler_current)
        {
            LOG_ERROR("sampler_count != sampller_current");
            return false;
        }

        VkDescriptorSetLayoutCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkDescriptorSetLayoutCreateFlags)pCreateInfo->flags;
        create_info.bindingCount = pCreateInfo->bindingCount;
        create_info.pBindings = vk_descriptor_set_layout_binding_list.data();

        pSetLayout = new VulkanDescriptorSetLayout();
        VkDescriptorSetLayout vk_descriptorSetLayout;
        VkResult result = vkCreateDescriptorSetLayout(m_device, &create_info, nullptr, &vk_descriptorSetLayout);
        ((VulkanDescriptorSetLayout*)pSetLayout)->setResource(vk_descriptorSetLayout);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateDescriptorSetLayout failed!");
            return false;
        }
    }

    bool VulkanRHI::createFence(const RHIFenceCreateInfo* pCreateInfo, RHIFence* &pFence)
    {
        VkFenceCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkFenceCreateFlags)pCreateInfo->flags;

        pFence = new VulkanFence();
        VkFence vk_fence;
        VkResult result = vkCreateFence(m_device, &create_info, nullptr, &vk_fence);
        ((VulkanFence*)pFence)->setResource(vk_fence);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateFence failed!");
            return false;
        }
    }

    bool VulkanRHI::createFramebuffer(const RHIFramebufferCreateInfo* pCreateInfo, RHIFramebuffer* &pFramebuffer)
    {
        //image_view
        int image_view_size = pCreateInfo->attachmentCount;
        std::vector<VkImageView> vk_image_view_list(image_view_size);
        // 这里将单个pass中createInfo包含的所有attachment全部存入vk_image_view_list
        for (int i = 0; i < image_view_size; ++i)
        {
            const auto& rhi_image_view_element = pCreateInfo->pAttachments[i];
            auto& vk_image_view_element = vk_image_view_list[i];

            vk_image_view_element = ((VulkanImageView*)rhi_image_view_element)->getResource();
        };

        VkFramebufferCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkFramebufferCreateFlags)pCreateInfo->flags;
        create_info.renderPass = ((VulkanRenderPass*)pCreateInfo->renderPass)->getResource();
        create_info.attachmentCount = pCreateInfo->attachmentCount; // 图像视图(attachment)的数量
        create_info.pAttachments = vk_image_view_list.data(); // 实际上存储了该交换链中所有的图像视图（数组）
        create_info.width = pCreateInfo->width;
        create_info.height = pCreateInfo->height; // width和height成员变量用于指定帧缓冲的大小
        create_info.layers = pCreateInfo->layers; // layers成员变量用于指定图像层数

        pFramebuffer = new VulkanFramebuffer();
        VkFramebuffer vk_framebuffer;
        VkResult result = vkCreateFramebuffer(m_device, &create_info, nullptr, &vk_framebuffer);
        ((VulkanFramebuffer*)pFramebuffer)->setResource(vk_framebuffer);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateFramebuffer failed!");
            return false;
        }
    }

    bool VulkanRHI::createGraphicsPipelines(RHIPipelineCache* pipelineCache, uint32_t createInfoCount, const RHIGraphicsPipelineCreateInfo* pCreateInfo, RHIPipeline* &pPipelines)
    {
        // pipeline_shader_stage_create_info
        // 需要指定着色器在管线处理哪一阶段被使用。指定着色器阶段需要使用VkPipelineShaderStageCreateInfo结构体
        int pipeline_shader_stage_create_info_size = pCreateInfo->stageCount; // 管线的阶段数
        std::vector<VkPipelineShaderStageCreateInfo> vk_pipeline_shader_stage_create_info_list(pipeline_shader_stage_create_info_size);

        int specialization_map_entry_size_total = 0;
        int specialization_info_total = 0;
        for (int i = 0; i < pipeline_shader_stage_create_info_size; ++i)
        {
            const auto& rhi_pipeline_shader_stage_create_info_element = pCreateInfo->pStages[i];
            if (rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo != nullptr)
            {
                specialization_info_total++;
                specialization_map_entry_size_total+= rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo->mapEntryCount;
            }
        }
        std::vector<VkSpecializationInfo> vk_specialization_info_list(specialization_info_total);
        std::vector<VkSpecializationMapEntry> vk_specialization_map_entry_list(specialization_map_entry_size_total);
        int specialization_map_entry_current = 0;
        int specialization_info_current = 0;

        // 循环每一个VkPipelineShaderStageCreateInfo的vector中的值
        for (int i = 0; i < pipeline_shader_stage_create_info_size; ++i)
        {
            const auto& rhi_pipeline_shader_stage_create_info_element = pCreateInfo->pStages[i]; // pipeline create info的stage
            // vk_pipeline_shader_stage_create_info_element即为每一个VkPipelineShaderStageCreateInfo的具体值
            auto& vk_pipeline_shader_stage_create_info_element = vk_pipeline_shader_stage_create_info_list[i];

            if (rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo != nullptr)
            {
                vk_pipeline_shader_stage_create_info_element.pSpecializationInfo = &vk_specialization_info_list[specialization_info_current];

                VkSpecializationInfo vk_specialization_info{};
                vk_specialization_info.mapEntryCount = rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo->mapEntryCount;
                vk_specialization_info.pMapEntries = &vk_specialization_map_entry_list[specialization_map_entry_current];
                vk_specialization_info.dataSize = rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo->dataSize;
                vk_specialization_info.pData = (const void*)rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo->pData;

                //specialization_map_entry
                for (int i = 0; i < rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo->mapEntryCount; ++i)
                {
                    const auto& rhi_specialization_map_entry_element = rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo->pMapEntries[i];
                    auto& vk_specialization_map_entry_element = vk_specialization_map_entry_list[specialization_map_entry_current];

                    vk_specialization_map_entry_element.constantID = rhi_specialization_map_entry_element->constantID;
                    vk_specialization_map_entry_element.offset = rhi_specialization_map_entry_element->offset;
                    vk_specialization_map_entry_element.size = rhi_specialization_map_entry_element->size;

                    specialization_map_entry_current++;
                };

                specialization_info_current++;
            }
            else
            {
                vk_pipeline_shader_stage_create_info_element.pSpecializationInfo = nullptr;
            }
            // 填写shader stage的基本信息
            vk_pipeline_shader_stage_create_info_element.sType = (VkStructureType)rhi_pipeline_shader_stage_create_info_element.sType;
            vk_pipeline_shader_stage_create_info_element.pNext = (const void*)rhi_pipeline_shader_stage_create_info_element.pNext;
            vk_pipeline_shader_stage_create_info_element.flags = (VkPipelineShaderStageCreateFlags)rhi_pipeline_shader_stage_create_info_element.flags;
            vk_pipeline_shader_stage_create_info_element.stage = (VkShaderStageFlagBits)rhi_pipeline_shader_stage_create_info_element.stage;
            vk_pipeline_shader_stage_create_info_element.module = ((VulkanShader*)rhi_pipeline_shader_stage_create_info_element.module)->getResource();
            vk_pipeline_shader_stage_create_info_element.pName = rhi_pipeline_shader_stage_create_info_element.pName;
        };

        if (!((specialization_map_entry_size_total == specialization_map_entry_current)
            && (specialization_info_total == specialization_info_current)))
        {
            LOG_ERROR("(specialization_map_entry_size_total == specialization_map_entry_current)&& (specialization_info_total == specialization_info_current)");
            return false;
        }

        //vertex_input_binding_description 使用VkVertexInputBindingDescription结构体来描述CPU缓冲中的顶点数据存放方式
        int vertex_input_binding_description_size = pCreateInfo->pVertexInputState->vertexBindingDescriptionCount;
        std::vector<VkVertexInputBindingDescription> vk_vertex_input_binding_description_list(vertex_input_binding_description_size);
        for (int i = 0; i < vertex_input_binding_description_size; ++i)
        {
            const auto& rhi_vertex_input_binding_description_element = pCreateInfo->pVertexInputState->pVertexBindingDescriptions[i];
            auto& vk_vertex_input_binding_description_element = vk_vertex_input_binding_description_list[i];

            vk_vertex_input_binding_description_element.binding = rhi_vertex_input_binding_description_element.binding;
            vk_vertex_input_binding_description_element.stride = rhi_vertex_input_binding_description_element.stride;
            vk_vertex_input_binding_description_element.inputRate = (VkVertexInputRate)rhi_vertex_input_binding_description_element.inputRate;
        };

        //vertex_input_attribute_description 使用VkVertexInputAttributeDescription结构体来描述顶点属性
        int vertex_input_attribute_description_size = pCreateInfo->pVertexInputState->vertexAttributeDescriptionCount;
        std::vector<VkVertexInputAttributeDescription> vk_vertex_input_attribute_description_list(vertex_input_attribute_description_size);
        for (int i = 0; i < vertex_input_attribute_description_size; ++i)
        {
            const auto& rhi_vertex_input_attribute_description_element = pCreateInfo->pVertexInputState->pVertexAttributeDescriptions[i];
            auto& vk_vertex_input_attribute_description_element = vk_vertex_input_attribute_description_list[i];

            vk_vertex_input_attribute_description_element.location = rhi_vertex_input_attribute_description_element.location;
            vk_vertex_input_attribute_description_element.binding = rhi_vertex_input_attribute_description_element.binding;
            vk_vertex_input_attribute_description_element.format = (VkFormat)rhi_vertex_input_attribute_description_element.format;
            vk_vertex_input_attribute_description_element.offset = rhi_vertex_input_attribute_description_element.offset;
        };

        // 顶点输入
        // 使用VkPipelineVertexInputStateCreateInfo结构体来描述传递给顶点着色器的顶点数据格式，主要包括
        // 绑定：数据之间的间距和数据是按逐顶点的方式还是按逐实例的方式进行组织
        // 属性描述：传递给顶点着色器的属性类型，用于将属性绑定到顶点着色器中的变量
        VkPipelineVertexInputStateCreateInfo vk_pipeline_vertex_input_state_create_info{};
        vk_pipeline_vertex_input_state_create_info.sType = (VkStructureType)pCreateInfo->pVertexInputState->sType;
        vk_pipeline_vertex_input_state_create_info.pNext = (const void*)pCreateInfo->pVertexInputState->pNext;
        vk_pipeline_vertex_input_state_create_info.flags = (VkPipelineVertexInputStateCreateFlags)pCreateInfo->pVertexInputState->flags;
        // pVertexBindingDescriptions和pVertexAttributeDescriptions成员变量用于指向描述顶点数据组织信息的结构体数组
        vk_pipeline_vertex_input_state_create_info.vertexBindingDescriptionCount = pCreateInfo->pVertexInputState->vertexBindingDescriptionCount;
        vk_pipeline_vertex_input_state_create_info.pVertexBindingDescriptions = vk_vertex_input_binding_description_list.data();
        vk_pipeline_vertex_input_state_create_info.vertexAttributeDescriptionCount = pCreateInfo->pVertexInputState->vertexAttributeDescriptionCount;
        vk_pipeline_vertex_input_state_create_info.pVertexAttributeDescriptions = vk_vertex_input_attribute_description_list.data();
        
        // 输入装配
        // VkPipelineInputAssemblyStateCreateInfo结构体用于描述两个信息：顶点数据定义了哪种类型的几何图元，以及是否启用几何图元重启
        VkPipelineInputAssemblyStateCreateInfo vk_pipeline_input_assembly_state_create_info{};
        vk_pipeline_input_assembly_state_create_info.sType = (VkStructureType)pCreateInfo->pInputAssemblyState->sType;
        vk_pipeline_input_assembly_state_create_info.pNext = (const void*)pCreateInfo->pInputAssemblyState->pNext;
        vk_pipeline_input_assembly_state_create_info.flags = (VkPipelineInputAssemblyStateCreateFlags)pCreateInfo->pInputAssemblyState->flags;
        vk_pipeline_input_assembly_state_create_info.topology = (VkPrimitiveTopology)pCreateInfo->pInputAssemblyState->topology;
        vk_pipeline_input_assembly_state_create_info.primitiveRestartEnable = (VkBool32)pCreateInfo->pInputAssemblyState->primitiveRestartEnable;

        const VkPipelineTessellationStateCreateInfo* vk_pipeline_tessellation_state_create_info_ptr = nullptr;
        VkPipelineTessellationStateCreateInfo vk_pipeline_tessellation_state_create_info{};
        if (pCreateInfo->pTessellationState != nullptr)
        {
            vk_pipeline_tessellation_state_create_info.sType = (VkStructureType)pCreateInfo->pTessellationState->sType;
            vk_pipeline_tessellation_state_create_info.pNext = (const void*)pCreateInfo->pTessellationState->pNext;
            vk_pipeline_tessellation_state_create_info.flags = (VkPipelineTessellationStateCreateFlags)pCreateInfo->pTessellationState->flags;
            vk_pipeline_tessellation_state_create_info.patchControlPoints = pCreateInfo->pTessellationState->patchControlPoints;

            vk_pipeline_tessellation_state_create_info_ptr = &vk_pipeline_tessellation_state_create_info;
        }

        //viewport 视口
        int viewport_size = pCreateInfo->pViewportState->viewportCount;
        std::vector<VkViewport> vk_viewport_list(viewport_size);
        for (int i = 0; i < viewport_size; ++i)
        {
            const auto& rhi_viewport_element = pCreateInfo->pViewportState->pViewports[i];
            auto& vk_viewport_element = vk_viewport_list[i];

            vk_viewport_element.x = rhi_viewport_element.x;
            vk_viewport_element.y = rhi_viewport_element.y;
            // main_camera_pass中的所有视口大小都为交换链图像的大小
            vk_viewport_element.width = rhi_viewport_element.width;
            vk_viewport_element.height = rhi_viewport_element.height;
            // minDepth和maxDepth成员变量用于指定帧缓冲使用的深度值的范围，一般将它们的值分别设置为0.0f和1.0f
            vk_viewport_element.minDepth = rhi_viewport_element.minDepth;
            vk_viewport_element.maxDepth = rhi_viewport_element.maxDepth;
        };
        
        //rect_2d 用于裁剪的范围
        int rect_2d_size = pCreateInfo->pViewportState->scissorCount;
        std::vector<VkRect2D> vk_rect_2d_list(rect_2d_size);
        for (int i = 0; i < rect_2d_size; ++i)
        {
            const auto& rhi_rect_2d_element = pCreateInfo->pViewportState->pScissors[i];
            auto& vk_rect_2d_element = vk_rect_2d_list[i];

            VkOffset2D offset2d{};
            offset2d.x = rhi_rect_2d_element.offset.x;
            offset2d.y = rhi_rect_2d_element.offset.y;

            VkExtent2D extend2d{};
            extend2d.width = rhi_rect_2d_element.extent.width;
            extend2d.height = rhi_rect_2d_element.extent.height;

            vk_rect_2d_element.offset = offset2d;
            vk_rect_2d_element.extent = extend2d;
        };

        // 视口和裁剪矩形需要组合在一起，通过VkPipelineViewportStateCreateInfo结构体指定。许多显卡可以使用多个视口和裁剪矩形，
        // 所以指定视口和裁剪矩形的成员变量是一个指向视口和裁剪矩形的结构体数组指针
        VkPipelineViewportStateCreateInfo vk_pipeline_viewport_state_create_info{};
        vk_pipeline_viewport_state_create_info.sType = (VkStructureType)pCreateInfo->pViewportState->sType;
        vk_pipeline_viewport_state_create_info.pNext = (const void*)pCreateInfo->pViewportState->pNext;
        vk_pipeline_viewport_state_create_info.flags = (VkPipelineViewportStateCreateFlags)pCreateInfo->pViewportState->flags;
        vk_pipeline_viewport_state_create_info.viewportCount = pCreateInfo->pViewportState->viewportCount;
        vk_pipeline_viewport_state_create_info.pViewports = vk_viewport_list.data();
        vk_pipeline_viewport_state_create_info.scissorCount = pCreateInfo->pViewportState->scissorCount;
        vk_pipeline_viewport_state_create_info.pScissors = vk_rect_2d_list.data();

        // 光栅化：光栅化程序将来自顶点着色器的顶点构成的几何图元转换为片段交由片段着色器着色
        // 深度测试，背面剔除和裁剪测试也由光栅化程序执行。我们可以配置光栅化程序输出整个几何图元作为片段，还是只输出几何图元的边作为片段(也就是线框模式)
        VkPipelineRasterizationStateCreateInfo vk_pipeline_rasterization_state_create_info{};
        vk_pipeline_rasterization_state_create_info.sType = (VkStructureType)pCreateInfo->pRasterizationState->sType;
        vk_pipeline_rasterization_state_create_info.pNext = (const void*)pCreateInfo->pRasterizationState->pNext;
        vk_pipeline_rasterization_state_create_info.flags = (VkPipelineRasterizationStateCreateFlags)pCreateInfo->pRasterizationState->flags;
        // depthClampEnable成员变量设置为VK_TRUE表示在近平面和远平面外的片段会被截断为在近平面和远平面上，而不是直接丢弃这些片段。这对于阴影贴图的生成很有用
        vk_pipeline_rasterization_state_create_info.depthClampEnable = (VkBool32)pCreateInfo->pRasterizationState->depthClampEnable;
        // rasterizerDiscardEnable成员变量设置为VK_TRUE表示所有几何图元都不能通过光栅化阶段。这一设置会禁止一切片段输出到帧缓冲
        vk_pipeline_rasterization_state_create_info.rasterizerDiscardEnable = (VkBool32)pCreateInfo->pRasterizationState->rasterizerDiscardEnable;
        // polygonMode成员变量用于指定几何图元生成片段的方式：VK_POLYGON_MODE_FILL/VK_POLYGON_MODE_LINE/VK_POLYGON_MODE_POINT
        vk_pipeline_rasterization_state_create_info.polygonMode = (VkPolygonMode)pCreateInfo->pRasterizationState->polygonMode;
        // cullMode成员变量用于指定使用的表面剔除类型。我们可以通过它禁用表面剔除，剔除背面，剔除正面，以及剔除双面
        vk_pipeline_rasterization_state_create_info.cullMode = (VkCullModeFlags)pCreateInfo->pRasterizationState->cullMode;
        // frontFace成员变量用于指定顺时针的顶点序是正面，还是逆时针的顶点序是正面
        vk_pipeline_rasterization_state_create_info.frontFace = (VkFrontFace)pCreateInfo->pRasterizationState->frontFace;
        // 光栅化程序可以通过添加常量值或根据片段的斜率来改变深度值。有时用于阴影贴图
        vk_pipeline_rasterization_state_create_info.depthBiasEnable = (VkBool32)pCreateInfo->pRasterizationState->depthBiasEnable;
        vk_pipeline_rasterization_state_create_info.depthBiasConstantFactor = pCreateInfo->pRasterizationState->depthBiasConstantFactor;
        vk_pipeline_rasterization_state_create_info.depthBiasClamp = pCreateInfo->pRasterizationState->depthBiasClamp;
        vk_pipeline_rasterization_state_create_info.depthBiasSlopeFactor = pCreateInfo->pRasterizationState->depthBiasSlopeFactor;
        // lineWidth成员变量用于指定光栅化后的线段宽度，它以线宽所占的片段数目为单位
        vk_pipeline_rasterization_state_create_info.lineWidth = pCreateInfo->pRasterizationState->lineWidth;

        // 多重采样
        VkPipelineMultisampleStateCreateInfo vk_pipeline_multisample_state_create_info{};
        vk_pipeline_multisample_state_create_info.sType = (VkStructureType)pCreateInfo->pMultisampleState->sType;
        vk_pipeline_multisample_state_create_info.pNext = (const void*)pCreateInfo->pMultisampleState->pNext;
        vk_pipeline_multisample_state_create_info.flags = (VkPipelineMultisampleStateCreateFlags)pCreateInfo->pMultisampleState->flags;
        vk_pipeline_multisample_state_create_info.rasterizationSamples = (VkSampleCountFlagBits)pCreateInfo->pMultisampleState->rasterizationSamples;
        vk_pipeline_multisample_state_create_info.sampleShadingEnable = (VkBool32)pCreateInfo->pMultisampleState->sampleShadingEnable;
        vk_pipeline_multisample_state_create_info.minSampleShading = pCreateInfo->pMultisampleState->minSampleShading;
        vk_pipeline_multisample_state_create_info.pSampleMask = (const RHISampleMask*)pCreateInfo->pMultisampleState->pSampleMask;
        vk_pipeline_multisample_state_create_info.alphaToCoverageEnable = (VkBool32)pCreateInfo->pMultisampleState->alphaToCoverageEnable;
        vk_pipeline_multisample_state_create_info.alphaToOneEnable = (VkBool32)pCreateInfo->pMultisampleState->alphaToOneEnable;

        VkStencilOpState stencil_op_state_front{};
        stencil_op_state_front.failOp = (VkStencilOp)pCreateInfo->pDepthStencilState->front.failOp;
        stencil_op_state_front.passOp = (VkStencilOp)pCreateInfo->pDepthStencilState->front.passOp;
        stencil_op_state_front.depthFailOp = (VkStencilOp)pCreateInfo->pDepthStencilState->front.depthFailOp;
        stencil_op_state_front.compareOp = (VkCompareOp)pCreateInfo->pDepthStencilState->front.compareOp;
        stencil_op_state_front.compareMask = pCreateInfo->pDepthStencilState->front.compareMask;
        stencil_op_state_front.writeMask = pCreateInfo->pDepthStencilState->front.writeMask;
        stencil_op_state_front.reference = pCreateInfo->pDepthStencilState->front.reference;

        VkStencilOpState stencil_op_state_back{};
        stencil_op_state_back.failOp = (VkStencilOp)pCreateInfo->pDepthStencilState->back.failOp;
        stencil_op_state_back.passOp = (VkStencilOp)pCreateInfo->pDepthStencilState->back.passOp;
        stencil_op_state_back.depthFailOp = (VkStencilOp)pCreateInfo->pDepthStencilState->back.depthFailOp;
        stencil_op_state_back.compareOp = (VkCompareOp)pCreateInfo->pDepthStencilState->back.compareOp;
        stencil_op_state_back.compareMask = pCreateInfo->pDepthStencilState->back.compareMask;
        stencil_op_state_back.writeMask = pCreateInfo->pDepthStencilState->back.writeMask;
        stencil_op_state_back.reference = pCreateInfo->pDepthStencilState->back.reference;

        // 深度和模板测试
        VkPipelineDepthStencilStateCreateInfo vk_pipeline_depth_stencil_state_create_info{};
        vk_pipeline_depth_stencil_state_create_info.sType = (VkStructureType)pCreateInfo->pDepthStencilState->sType;
        vk_pipeline_depth_stencil_state_create_info.pNext = (const void*)pCreateInfo->pDepthStencilState->pNext;
        vk_pipeline_depth_stencil_state_create_info.flags = (VkPipelineDepthStencilStateCreateFlags)pCreateInfo->pDepthStencilState->flags;
        vk_pipeline_depth_stencil_state_create_info.depthTestEnable = (VkBool32)pCreateInfo->pDepthStencilState->depthTestEnable;
        vk_pipeline_depth_stencil_state_create_info.depthWriteEnable = (VkBool32)pCreateInfo->pDepthStencilState->depthWriteEnable;
        vk_pipeline_depth_stencil_state_create_info.depthCompareOp = (VkCompareOp)pCreateInfo->pDepthStencilState->depthCompareOp;
        vk_pipeline_depth_stencil_state_create_info.depthBoundsTestEnable = (VkBool32)pCreateInfo->pDepthStencilState->depthBoundsTestEnable;
        vk_pipeline_depth_stencil_state_create_info.stencilTestEnable = (VkBool32)pCreateInfo->pDepthStencilState->stencilTestEnable;
        vk_pipeline_depth_stencil_state_create_info.front = stencil_op_state_front;
        vk_pipeline_depth_stencil_state_create_info.back = stencil_op_state_back;
        vk_pipeline_depth_stencil_state_create_info.minDepthBounds = pCreateInfo->pDepthStencilState->minDepthBounds;
        vk_pipeline_depth_stencil_state_create_info.maxDepthBounds = pCreateInfo->pDepthStencilState->maxDepthBounds;

        //pipeline_color_blend_attachment_state
        // 片段着色器返回的片段颜色需要和原来帧缓冲中对应像素的颜色进行混合。混合的方式有下面两种：
        // 混合旧值和新值产生最终的颜色
        // 使用位运算组合旧值和新值
        /*
            // 第一类混合方式的运算过程类似下面的代码
            if (blendEnable)
            {
                finalColor.rgb = (srcColorBlendFactor * newColor.rgb)<colorBlendOp>(dstColorBlendFactor * oldColor.rgb);
                finalColor.a   = (srcAlphaBlendFactor * newColor.a)<alphaBlendOp>(dstAlphaBlendFactor * oldColor.a);
            }
            else
            {
                finalColor = newColor;
            }

            finalColor = finalColor & colorWriteMask;
        */
        int pipeline_color_blend_attachment_state_size = pCreateInfo->pColorBlendState->attachmentCount;
        // VkPipelineColorBlendAttachmentState结构体，可以用它来对每个绑定的帧缓冲进行单独的颜色混合配置
        std::vector<VkPipelineColorBlendAttachmentState> vk_pipeline_color_blend_attachment_state_list(pipeline_color_blend_attachment_state_size);
        for (int i = 0; i < pipeline_color_blend_attachment_state_size; ++i)
        {
            const auto& rhi_pipeline_color_blend_attachment_state_element = pCreateInfo->pColorBlendState->pAttachments[i];
            auto& vk_pipeline_color_blend_attachment_state_element = vk_pipeline_color_blend_attachment_state_list[i];

            // 如果blendEnable成员变量被设置为VK_FALSE，就不会进行混合操作。否则，就会执行指定的混合操作计算新的颜色值。
            // 计算出的新的颜色值会按照colorWriteMask的设置决定写入到帧缓冲的颜色通道
            vk_pipeline_color_blend_attachment_state_element.blendEnable = (VkBool32)rhi_pipeline_color_blend_attachment_state_element.blendEnable;
            vk_pipeline_color_blend_attachment_state_element.srcColorBlendFactor = (VkBlendFactor)rhi_pipeline_color_blend_attachment_state_element.srcColorBlendFactor;
            vk_pipeline_color_blend_attachment_state_element.dstColorBlendFactor = (VkBlendFactor)rhi_pipeline_color_blend_attachment_state_element.dstColorBlendFactor;
            vk_pipeline_color_blend_attachment_state_element.colorBlendOp = (VkBlendOp)rhi_pipeline_color_blend_attachment_state_element.colorBlendOp;
            vk_pipeline_color_blend_attachment_state_element.srcAlphaBlendFactor = (VkBlendFactor)rhi_pipeline_color_blend_attachment_state_element.srcAlphaBlendFactor;
            vk_pipeline_color_blend_attachment_state_element.dstAlphaBlendFactor = (VkBlendFactor)rhi_pipeline_color_blend_attachment_state_element.dstAlphaBlendFactor;
            vk_pipeline_color_blend_attachment_state_element.alphaBlendOp = (VkBlendOp)rhi_pipeline_color_blend_attachment_state_element.alphaBlendOp;
            vk_pipeline_color_blend_attachment_state_element.colorWriteMask = (VkColorComponentFlags)rhi_pipeline_color_blend_attachment_state_element.colorWriteMask;
            /*
                // 例如实现半透明效果
                finalColor.rgb = newAlpha * newColor + (1 - newAlpha) * oldColor;
                finalColor.a = newAlpha.a;

                colorBlendAttachment.blendEnable = VK_TRUE;
                colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
                colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            */
        };

        // VkPipelineColorBlendStateCreateInfo结构体，可以用它来进行全局的颜色混合配置
        VkPipelineColorBlendStateCreateInfo vk_pipeline_color_blend_state_create_info{};
        vk_pipeline_color_blend_state_create_info.sType = (VkStructureType)pCreateInfo->pColorBlendState->sType;
        vk_pipeline_color_blend_state_create_info.pNext = pCreateInfo->pColorBlendState->pNext;
        vk_pipeline_color_blend_state_create_info.flags = pCreateInfo->pColorBlendState->flags;
        vk_pipeline_color_blend_state_create_info.logicOpEnable = pCreateInfo->pColorBlendState->logicOpEnable;
        vk_pipeline_color_blend_state_create_info.logicOp = (VkLogicOp)pCreateInfo->pColorBlendState->logicOp;
        vk_pipeline_color_blend_state_create_info.attachmentCount = pCreateInfo->pColorBlendState->attachmentCount;
        vk_pipeline_color_blend_state_create_info.pAttachments = vk_pipeline_color_blend_attachment_state_list.data();
        for (int i = 0; i < 4; ++i)
        {
            vk_pipeline_color_blend_state_create_info.blendConstants[i] = pCreateInfo->pColorBlendState->blendConstants[i];
        };

        // dynamic_state
        // 只有非常有限的管线状态可以在不重建管线的情况下进行动态修改。这包括视口大小，线宽和混合常量。
        // 我们可以通过填写VkPipelineDynamicStateCreateInfo结构体指定需要动态修改的状态
        int dynamic_state_size = pCreateInfo->pDynamicState->dynamicStateCount;
        std::vector<VkDynamicState> vk_dynamic_state_list(dynamic_state_size);
        for (int i = 0; i < dynamic_state_size; ++i)
        {
            const auto& rhi_dynamic_state_element = pCreateInfo->pDynamicState->pDynamicStates[i];
            auto& vk_dynamic_state_element = vk_dynamic_state_list[i];

            vk_dynamic_state_element = (VkDynamicState)rhi_dynamic_state_element;
        };

        VkPipelineDynamicStateCreateInfo vk_pipeline_dynamic_state_create_info{};
        vk_pipeline_dynamic_state_create_info.sType = (VkStructureType)pCreateInfo->pDynamicState->sType;
        vk_pipeline_dynamic_state_create_info.pNext = pCreateInfo->pDynamicState->pNext;
        vk_pipeline_dynamic_state_create_info.flags = (VkPipelineDynamicStateCreateFlags)pCreateInfo->pDynamicState->flags;
        vk_pipeline_dynamic_state_create_info.dynamicStateCount = pCreateInfo->pDynamicState->dynamicStateCount;
        vk_pipeline_dynamic_state_create_info.pDynamicStates = vk_dynamic_state_list.data();

        VkGraphicsPipelineCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkPipelineCreateFlags)pCreateInfo->flags;
        create_info.stageCount = pCreateInfo->stageCount;
        create_info.pStages = vk_pipeline_shader_stage_create_info_list.data();
        create_info.pVertexInputState = &vk_pipeline_vertex_input_state_create_info;
        create_info.pInputAssemblyState = &vk_pipeline_input_assembly_state_create_info;
        create_info.pTessellationState = vk_pipeline_tessellation_state_create_info_ptr;
        create_info.pViewportState = &vk_pipeline_viewport_state_create_info;
        create_info.pRasterizationState = &vk_pipeline_rasterization_state_create_info;
        create_info.pMultisampleState = &vk_pipeline_multisample_state_create_info;
        create_info.pDepthStencilState = &vk_pipeline_depth_stencil_state_create_info;
        create_info.pColorBlendState = &vk_pipeline_color_blend_state_create_info;
        create_info.pDynamicState = &vk_pipeline_dynamic_state_create_info;
        // 在着色器中使用uniform变量，它可以在管线建立后动态地被应用程序修改，实现对着色器进行一定程度的动态配置
        // 我们在着色器中使用的uniform变量需要在管线创建时使用VkPipelineLayout对象定义
        create_info.layout = ((VulkanPipelineLayout*)pCreateInfo->layout)->getResource(); // 管线布局，在各个pass中create
        create_info.renderPass = ((VulkanRenderPass*)pCreateInfo->renderPass)->getResource();
        create_info.subpass = pCreateInfo->subpass;
        // basePipelineHandle和basePipelineIndex成员变量用于以一个创建好的图形管线为基础创建一个新的图形管线
        // 当要创建一个和已有管线大量设置相同的管线时，使用它的代价要比直接创建小
        // 并且，对于从同一个管线衍生出的两个管线，在它们之间进行管线切换操作的效率也要高很多
        if (pCreateInfo->basePipelineHandle != nullptr)
        {
            create_info.basePipelineHandle = ((VulkanPipeline*)pCreateInfo->basePipelineHandle)->getResource();
        }
        else
        {
            create_info.basePipelineHandle = VK_NULL_HANDLE;
        }
        create_info.basePipelineIndex = pCreateInfo->basePipelineIndex;

        pPipelines = new VulkanPipeline();
        VkPipeline vk_pipelines; // 创建VkPipeline成员变量用来存储创建的管线对象
        // 通过VkPipelineCache可以将管线创建相关的数据进行缓存，在多个vkCreateGraphicsPipelines函数调用中使用，
        // 甚至可以将缓存存入文件，在多个程序间使用。使用它可以加速之后的管线创建
        VkPipelineCache vk_pipeline_cache = VK_NULL_HANDLE;
        if (pipelineCache != nullptr)
        {
            vk_pipeline_cache = ((VulkanPipelineCache*)pipelineCache)->getResource();
        }

        if (vkCreateGraphicsPipelines(
                m_device, vk_pipeline_cache, createInfoCount, &create_info, nullptr, &vk_pipelines) == VK_SUCCESS)
        {
            ((VulkanPipeline*)pPipelines)->setResource(vk_pipelines);
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateGraphicsPipelines failed!");
            return false;
        }
    }

    bool VulkanRHI::createComputePipelines(RHIPipelineCache* pipelineCache, uint32_t createInfoCount, const RHIComputePipelineCreateInfo* pCreateInfos, RHIPipeline*& pPipelines)
    {
        VkPipelineShaderStageCreateInfo shader_stage_create_info{};
        if (pCreateInfos->pStages->pSpecializationInfo != nullptr)
        {
            //will be complete soon if needed.
            shader_stage_create_info.pSpecializationInfo = nullptr;
        }
        else
        {
            shader_stage_create_info.pSpecializationInfo = nullptr;
        }
        shader_stage_create_info.sType = (VkStructureType)pCreateInfos->pStages->sType;
        shader_stage_create_info.pNext = (const void*)pCreateInfos->pStages->pNext;
        shader_stage_create_info.flags = (VkPipelineShaderStageCreateFlags)pCreateInfos->pStages->flags;
        shader_stage_create_info.stage = (VkShaderStageFlagBits)pCreateInfos->pStages->stage;
        shader_stage_create_info.module = ((VulkanShader*)pCreateInfos->pStages->module)->getResource();
        shader_stage_create_info.pName = pCreateInfos->pStages->pName;

        VkComputePipelineCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfos->sType;
        create_info.pNext = (const void*)pCreateInfos->pNext;
        create_info.flags = (VkPipelineCreateFlags)pCreateInfos->flags;
        create_info.stage = shader_stage_create_info;
        create_info.layout = ((VulkanPipelineLayout*)pCreateInfos->layout)->getResource();;
        if (pCreateInfos->basePipelineHandle != nullptr)
        {
            create_info.basePipelineHandle = ((VulkanPipeline*)pCreateInfos->basePipelineHandle)->getResource();
        }
        else
        {
            create_info.basePipelineHandle = VK_NULL_HANDLE;
        }
        create_info.basePipelineIndex = pCreateInfos->basePipelineIndex;

        pPipelines = new VulkanPipeline();
        VkPipeline vk_pipelines;
        VkPipelineCache vk_pipeline_cache = VK_NULL_HANDLE;
        if (pipelineCache != nullptr)
        {
            vk_pipeline_cache = ((VulkanPipelineCache*)pipelineCache)->getResource();
        }
        VkResult result = vkCreateComputePipelines(m_device, vk_pipeline_cache, createInfoCount, &create_info, nullptr, &vk_pipelines);
        ((VulkanPipeline*)pPipelines)->setResource(vk_pipelines);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateComputePipelines failed!");
            return false;
        }
    }

    bool VulkanRHI::createPipelineLayout(const RHIPipelineLayoutCreateInfo* pCreateInfo, RHIPipelineLayout* &pPipelineLayout)
    {
        //descriptor_set_layout
        int descriptor_set_layout_size = pCreateInfo->setLayoutCount;
        std::vector<VkDescriptorSetLayout> vk_descriptor_set_layout_list(descriptor_set_layout_size);
        for (int i = 0; i < descriptor_set_layout_size; ++i)
        {
            const auto& rhi_descriptor_set_layout_element = pCreateInfo->pSetLayouts[i];
            auto& vk_descriptor_set_layout_element = vk_descriptor_set_layout_list[i];

            vk_descriptor_set_layout_element = ((VulkanDescriptorSetLayout*)rhi_descriptor_set_layout_element)->getResource();
        };

        VkPipelineLayoutCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkPipelineLayoutCreateFlags)pCreateInfo->flags;
        create_info.setLayoutCount = pCreateInfo->setLayoutCount;
        create_info.pSetLayouts = vk_descriptor_set_layout_list.data();

        pPipelineLayout = new VulkanPipelineLayout();
        VkPipelineLayout vk_pipeline_layout;
        VkResult result = vkCreatePipelineLayout(m_device, &create_info, nullptr, &vk_pipeline_layout);
        ((VulkanPipelineLayout*)pPipelineLayout)->setResource(vk_pipeline_layout);
        
        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreatePipelineLayout failed!");
            return false;
        }
    }

    bool VulkanRHI::createRenderPass(const RHIRenderPassCreateInfo* pCreateInfo, RHIRenderPass* &pRenderPass)
    {
        // attachment convert
        // attachmentCount: 附着数量
        std::vector<VkAttachmentDescription> vk_attachments(pCreateInfo->attachmentCount);
        for (int i = 0; i < pCreateInfo->attachmentCount; ++i)
        {
            const auto& rhi_desc = pCreateInfo->pAttachments[i];
            auto& vk_desc = vk_attachments[i];

            vk_desc.flags = (VkAttachmentDescriptionFlags)(rhi_desc).flags;
            vk_desc.format = (VkFormat)(rhi_desc).format; // format成员变量用于指定颜色缓冲附着的格式
            vk_desc.samples = (VkSampleCountFlagBits)(rhi_desc).samples; // samples成员变量用于指定采样数（不使用多重采样则为1）
            // loadOp和storeOp成员变量用于指定在渲染之前和渲染之后对附着中的数据进行的操作
            vk_desc.loadOp = (VkAttachmentLoadOp)(rhi_desc).loadOp;
            vk_desc.storeOp = (VkAttachmentStoreOp)(rhi_desc).storeOp; // loadOp和storeOp成员变量的设置会对颜色和深度缓冲起效
            vk_desc.stencilLoadOp = (VkAttachmentLoadOp)(rhi_desc).stencilLoadOp;
            vk_desc.stencilStoreOp = (VkAttachmentStoreOp)(rhi_desc).stencilStoreOp; // stencilLoadOp成员变量和stencilStoreOp成员变量会对模板缓冲起效
            // 一些常用的图形内存布局设置：
            // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL：图像被用作颜色附着
            // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR：图像被用在交换链中进行呈现操作
            // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL：图像被用作复制操作的目的图像
            vk_desc.initialLayout = (VkImageLayout)(rhi_desc).initialLayout; // initialLayout成员变量用于指定渲染流程开始前的图像布局方式
            vk_desc.finalLayout = (VkImageLayout)(rhi_desc).finalLayout; // finalLayout成员变量用于指定渲染流程结束后的图像布局方式
        };

        // subpass convert
        // 一个render pass可以包含多个subpass。子流程依赖于上一pass处理后的帧缓冲内容。将多个子流程组成一个渲染流程后，Vulkan可以对其进行一定程度的优化。
        int totalAttachmentRefenrence = 0;
        for (int i = 0; i < pCreateInfo->subpassCount; i++)
        {
            const auto& rhi_desc = pCreateInfo->pSubpasses[i];
            totalAttachmentRefenrence += rhi_desc.inputAttachmentCount; // pInputAttachments
            totalAttachmentRefenrence += rhi_desc.colorAttachmentCount; // pColorAttachments
            if (rhi_desc.pDepthStencilAttachment != nullptr)
            {
                totalAttachmentRefenrence += rhi_desc.colorAttachmentCount; // pDepthStencilAttachment
            }
            if (rhi_desc.pResolveAttachments != nullptr)
            {
                totalAttachmentRefenrence += rhi_desc.colorAttachmentCount; // pResolveAttachments
            }
        }
        // 使用VkSubpassDescription结构体来描述子流程
        std::vector<VkSubpassDescription> vk_subpass_description(pCreateInfo->subpassCount);
        // Every subpass references one or more of the attachments that we've described using the structure in the previous sections
        // These references are themselves VkAttachmentReference structs
        std::vector<VkAttachmentReference> vk_attachment_reference(totalAttachmentRefenrence);
        int currentAttachmentRefence = 0;
        for (int i = 0; i < pCreateInfo->subpassCount; ++i)
        {
            const auto& rhi_desc = pCreateInfo->pSubpasses[i];
            auto& vk_desc = vk_subpass_description[i];

            vk_desc.flags = (VkSubpassDescriptionFlags)(rhi_desc).flags;
            vk_desc.pipelineBindPoint = (VkPipelineBindPoint)(rhi_desc).pipelineBindPoint;
            vk_desc.preserveAttachmentCount = (rhi_desc).preserveAttachmentCount;
            vk_desc.pPreserveAttachments = (const uint32_t*)(rhi_desc).pPreserveAttachments;

            vk_desc.inputAttachmentCount = (rhi_desc).inputAttachmentCount;
            vk_desc.pInputAttachments = &vk_attachment_reference[currentAttachmentRefence];
            for (int i = 0; i < (rhi_desc).inputAttachmentCount; i++)
            {
                // 这里设置的颜色附着在数组中的索引会被片段着色器使用，对应我们在片段着色器中使用的 layout(location = 0) out vec4 outColor语句
                const auto& rhi_attachment_refence_input = (rhi_desc).pInputAttachments[i];
                auto& vk_attachment_refence_input = vk_attachment_reference[currentAttachmentRefence];

                // The attachment parameter specifies which attachment to reference by its index in the attachment descriptions array
                vk_attachment_refence_input.attachment = rhi_attachment_refence_input.attachment;
                // layout成员变量用于指定进行子流程时引用的附着使用的布局方式
                // 推荐将layout成员变量设置为VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL，一般而言，它的性能表现最佳
                vk_attachment_refence_input.layout = (VkImageLayout)(rhi_attachment_refence_input.layout);

                currentAttachmentRefence += 1;
            };

            vk_desc.colorAttachmentCount = (rhi_desc).colorAttachmentCount;
            vk_desc.pColorAttachments = &vk_attachment_reference[currentAttachmentRefence];
            for (int i = 0; i < (rhi_desc).colorAttachmentCount; ++i)
            {
                const auto& rhi_attachment_refence_color = (rhi_desc).pColorAttachments[i];
                auto& vk_attachment_refence_color = vk_attachment_reference[currentAttachmentRefence];

                vk_attachment_refence_color.attachment = rhi_attachment_refence_color.attachment;
                vk_attachment_refence_color.layout = (VkImageLayout)(rhi_attachment_refence_color.layout);

                currentAttachmentRefence += 1;
            };

            if (rhi_desc.pResolveAttachments != nullptr)
            {
                vk_desc.pResolveAttachments = &vk_attachment_reference[currentAttachmentRefence];
                for (int i = 0; i < (rhi_desc).colorAttachmentCount; ++i)
                {
                    const auto& rhi_attachment_refence_resolve = (rhi_desc).pResolveAttachments[i];
                    auto& vk_attachment_refence_resolve = vk_attachment_reference[currentAttachmentRefence];

                    vk_attachment_refence_resolve.attachment = rhi_attachment_refence_resolve.attachment;
                    vk_attachment_refence_resolve.layout = (VkImageLayout)(rhi_attachment_refence_resolve.layout);

                    currentAttachmentRefence += 1;
                };
            }

            if (rhi_desc.pDepthStencilAttachment != nullptr)
            {
                vk_desc.pDepthStencilAttachment = &vk_attachment_reference[currentAttachmentRefence];
                for (int i = 0; i < (rhi_desc).colorAttachmentCount; ++i)
                {
                    const auto& rhi_attachment_refence_depth = (rhi_desc).pDepthStencilAttachment[i];
                    auto& vk_attachment_refence_depth = vk_attachment_reference[currentAttachmentRefence];

                    vk_attachment_refence_depth.attachment = rhi_attachment_refence_depth.attachment;
                    vk_attachment_refence_depth.layout = (VkImageLayout)(rhi_attachment_refence_depth.layout);

                    currentAttachmentRefence += 1;
                };
            };
        };
        if (currentAttachmentRefence != totalAttachmentRefenrence)
        {
            LOG_ERROR("currentAttachmentRefence != totalAttachmentRefenrence");
            return false;
        }

        // subpass依赖
        // 渲染流程的子流程会自动进行图像布局变换。这一变换过程由子流程的依赖所决定。子流程的依赖包括子流程之间的内存和执行的依赖关系
        // 子流程执行之前和子流程执行之后的操作也被算作隐含的子流程
        std::vector<VkSubpassDependency> vk_subpass_depandecy(pCreateInfo->dependencyCount);
        for (int i = 0; i < pCreateInfo->dependencyCount; ++i)
        {
            const auto& rhi_desc = pCreateInfo->pDependencies[i];
            auto& vk_desc = vk_subpass_depandecy[i];

            vk_desc.srcSubpass = rhi_desc.srcSubpass;
            vk_desc.dstSubpass = rhi_desc.dstSubpass;
            vk_desc.srcStageMask = (VkPipelineStageFlags)(rhi_desc).srcStageMask;
            vk_desc.dstStageMask = (VkPipelineStageFlags)(rhi_desc).dstStageMask;
            vk_desc.srcAccessMask = (VkAccessFlags)(rhi_desc).srcAccessMask;
            vk_desc.dstAccessMask = (VkAccessFlags)(rhi_desc).dstAccessMask;
            vk_desc.dependencyFlags = (VkDependencyFlags)(rhi_desc).dependencyFlags;
        };

        // 创建render pass对象
        VkRenderPassCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkRenderPassCreateFlags)pCreateInfo->flags;
        create_info.attachmentCount = pCreateInfo->attachmentCount;
        create_info.pAttachments = vk_attachments.data();
        create_info.subpassCount = pCreateInfo->subpassCount;
        create_info.pSubpasses = vk_subpass_description.data();
        create_info.dependencyCount = pCreateInfo->dependencyCount;
        create_info.pDependencies = vk_subpass_depandecy.data();

        pRenderPass = new VulkanRenderPass(); // VulkanRenderPass包装了VkRenderPass资产
        VkRenderPass vk_render_pass;
        VkResult result = vkCreateRenderPass(m_device, &create_info, nullptr, &vk_render_pass);
        ((VulkanRenderPass*)pRenderPass)->setResource(vk_render_pass);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateRenderPass failed!");
            return false;
        }
    }

    bool VulkanRHI::createSampler(const RHISamplerCreateInfo* pCreateInfo, RHISampler* &pSampler)
    {
        VkSamplerCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkSamplerCreateFlags)pCreateInfo->flags;
        create_info.magFilter = (VkFilter)pCreateInfo->magFilter;
        create_info.minFilter = (VkFilter)pCreateInfo->minFilter;
        create_info.mipmapMode = (VkSamplerMipmapMode)pCreateInfo->mipmapMode;
        create_info.addressModeU = (VkSamplerAddressMode)pCreateInfo->addressModeU;
        create_info.addressModeV = (VkSamplerAddressMode)pCreateInfo->addressModeV;
        create_info.addressModeW = (VkSamplerAddressMode)pCreateInfo->addressModeW;
        create_info.mipLodBias = pCreateInfo->mipLodBias;
        create_info.anisotropyEnable = (VkBool32)pCreateInfo->anisotropyEnable;
        create_info.maxAnisotropy = pCreateInfo->maxAnisotropy;
        create_info.compareEnable = (VkBool32)pCreateInfo->compareEnable;
        create_info.compareOp = (VkCompareOp)pCreateInfo->compareOp;
        create_info.minLod = pCreateInfo->minLod;
        create_info.maxLod = pCreateInfo->maxLod;
        create_info.borderColor = (VkBorderColor)pCreateInfo->borderColor;
        create_info.unnormalizedCoordinates = (VkBool32)pCreateInfo->unnormalizedCoordinates;

        pSampler = new VulkanSampler();
        VkSampler vk_sampler;
        VkResult result = vkCreateSampler(m_device, &create_info, nullptr, &vk_sampler);
        ((VulkanSampler*)pSampler)->setResource(vk_sampler);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateSampler failed!");
            return false;
        }
    }

    bool VulkanRHI::createSemaphore(const RHISemaphoreCreateInfo* pCreateInfo, RHISemaphore* &pSemaphore)
    {
        VkSemaphoreCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = pCreateInfo->pNext;
        create_info.flags = (VkSemaphoreCreateFlags)pCreateInfo->flags;

        pSemaphore = new VulkanSemaphore();
        VkSemaphore vk_semaphore;
        VkResult result = vkCreateSemaphore(m_device, &create_info, nullptr, &vk_semaphore);
        ((VulkanSemaphore*)pSemaphore)->setResource(vk_semaphore);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateSemaphore failed!");
            return false;
        }
    }

    bool VulkanRHI::waitForFencesPFN(uint32_t fenceCount, RHIFence* const* pFences, RHIBool32 waitAll, uint64_t timeout)
    {
        //fence
        int fence_size = fenceCount;
        std::vector<VkFence> vk_fence_list(fence_size);
        for (int i = 0; i < fence_size; ++i)
        {
            const auto& rhi_fence_element = pFences[i];
            auto& vk_fence_element = vk_fence_list[i];

            vk_fence_element = ((VulkanFence*)rhi_fence_element)->getResource();
        };

        VkResult result = _vkWaitForFences(m_device, fenceCount, vk_fence_list.data(), waitAll, timeout);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("_vkWaitForFences failed!");
            return false;
        }
    }

    bool VulkanRHI::resetFencesPFN(uint32_t fenceCount, RHIFence* const* pFences)
    {
        //fence
        int fence_size = fenceCount;
        std::vector<VkFence> vk_fence_list(fence_size);
        for (int i = 0; i < fence_size; ++i)
        {
            const auto& rhi_fence_element = pFences[i];
            auto& vk_fence_element = vk_fence_list[i];

            vk_fence_element = ((VulkanFence*)rhi_fence_element)->getResource();
        };

        VkResult result = _vkResetFences(m_device, fenceCount, vk_fence_list.data());

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("_vkResetFences failed!");
            return false;
        }
    }

    bool VulkanRHI::resetCommandPoolPFN(RHICommandPool* commandPool, RHICommandPoolResetFlags flags)
    {
        VkResult result = _vkResetCommandPool(m_device, ((VulkanCommandPool*)commandPool)->getResource(), (VkCommandPoolResetFlags)flags);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("_vkResetCommandPool failed!");
            return false;
        }
    }

    bool VulkanRHI::beginCommandBufferPFN(RHICommandBuffer* commandBuffer, const RHICommandBufferBeginInfo* pBeginInfo)
    {
        VkCommandBufferInheritanceInfo* command_buffer_inheritance_info_ptr = nullptr;
        VkCommandBufferInheritanceInfo command_buffer_inheritance_info{};
        if (pBeginInfo->pInheritanceInfo != nullptr)
        {
            command_buffer_inheritance_info.sType = (VkStructureType)pBeginInfo->pInheritanceInfo->sType;
            command_buffer_inheritance_info.pNext = (const void*)pBeginInfo->pInheritanceInfo->pNext;
            command_buffer_inheritance_info.renderPass = ((VulkanRenderPass*)pBeginInfo->pInheritanceInfo->renderPass)->getResource();
            command_buffer_inheritance_info.subpass = pBeginInfo->pInheritanceInfo->subpass;
            command_buffer_inheritance_info.framebuffer = ((VulkanFramebuffer*)pBeginInfo->pInheritanceInfo->framebuffer)->getResource();
            command_buffer_inheritance_info.occlusionQueryEnable = (VkBool32)pBeginInfo->pInheritanceInfo->occlusionQueryEnable;
            command_buffer_inheritance_info.queryFlags = (VkQueryControlFlags)pBeginInfo->pInheritanceInfo->queryFlags;
            command_buffer_inheritance_info.pipelineStatistics = (VkQueryPipelineStatisticFlags)pBeginInfo->pInheritanceInfo->pipelineStatistics;

            command_buffer_inheritance_info_ptr = &command_buffer_inheritance_info;
        }

        VkCommandBufferBeginInfo command_buffer_begin_info{};
        command_buffer_begin_info.sType = (VkStructureType)pBeginInfo->sType;
        command_buffer_begin_info.pNext = (const void*)pBeginInfo->pNext;
        command_buffer_begin_info.flags = (VkCommandBufferUsageFlags)pBeginInfo->flags;
        command_buffer_begin_info.pInheritanceInfo = command_buffer_inheritance_info_ptr;
        VkResult result = _vkBeginCommandBuffer(((VulkanCommandBuffer*)commandBuffer)->getResource(), &command_buffer_begin_info);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("_vkBeginCommandBuffer failed!");
            return false;
        }
    }

    bool VulkanRHI::endCommandBufferPFN(RHICommandBuffer* commandBuffer)
    {
        VkResult result = _vkEndCommandBuffer(((VulkanCommandBuffer*)commandBuffer)->getResource());

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("_vkEndCommandBuffer failed!");
            return false;
        }
    }

    void VulkanRHI::cmdBeginRenderPassPFN(RHICommandBuffer* commandBuffer, const RHIRenderPassBeginInfo* pRenderPassBegin, RHISubpassContents contents)
    {
        VkOffset2D offset_2d{};
        offset_2d.x = pRenderPassBegin->renderArea.offset.x;
        offset_2d.y = pRenderPassBegin->renderArea.offset.y;

        VkExtent2D extent_2d{};
        extent_2d.width = pRenderPassBegin->renderArea.extent.width;
        extent_2d.height = pRenderPassBegin->renderArea.extent.height;

        VkRect2D rect_2d{};
        rect_2d.offset = offset_2d;
        rect_2d.extent = extent_2d;

        //clear_values
        int clear_value_size = pRenderPassBegin->clearValueCount;
        std::vector<VkClearValue> vk_clear_value_list(clear_value_size);
        for (int i = 0; i < clear_value_size; ++i)
        {
            const auto& rhi_clear_value_element = pRenderPassBegin->pClearValues[i];
            auto& vk_clear_value_element = vk_clear_value_list[i];

            VkClearColorValue vk_clear_color_value;
            vk_clear_color_value.float32[0] = rhi_clear_value_element.color.float32[0];
            vk_clear_color_value.float32[1] = rhi_clear_value_element.color.float32[1];
            vk_clear_color_value.float32[2] = rhi_clear_value_element.color.float32[2];
            vk_clear_color_value.float32[3] = rhi_clear_value_element.color.float32[3];
            vk_clear_color_value.int32[0] = rhi_clear_value_element.color.int32[0];
            vk_clear_color_value.int32[1] = rhi_clear_value_element.color.int32[1];
            vk_clear_color_value.int32[2] = rhi_clear_value_element.color.int32[2];
            vk_clear_color_value.int32[3] = rhi_clear_value_element.color.int32[3];
            vk_clear_color_value.uint32[0] = rhi_clear_value_element.color.uint32[0];
            vk_clear_color_value.uint32[1] = rhi_clear_value_element.color.uint32[1];
            vk_clear_color_value.uint32[2] = rhi_clear_value_element.color.uint32[2];
            vk_clear_color_value.uint32[3] = rhi_clear_value_element.color.uint32[3];

            VkClearDepthStencilValue vk_clear_depth_stencil_value;
            vk_clear_depth_stencil_value.depth = rhi_clear_value_element.depthStencil.depth;
            vk_clear_depth_stencil_value.stencil = rhi_clear_value_element.depthStencil.stencil;

            vk_clear_value_element.color = vk_clear_color_value;
            vk_clear_value_element.depthStencil = vk_clear_depth_stencil_value;

        };
        // 调用vkCmdBeginRenderPass函数可以开始一个render pass。这一函数需要我们使用VkRenderPassBeginInfo结构体来指定使用的渲染流程对象
        VkRenderPassBeginInfo vk_render_pass_begin_info{};
        vk_render_pass_begin_info.sType = (VkStructureType)pRenderPassBegin->sType;
        vk_render_pass_begin_info.pNext = pRenderPassBegin->pNext;
        vk_render_pass_begin_info.renderPass = ((VulkanRenderPass*)pRenderPassBegin->renderPass)->getResource();
        vk_render_pass_begin_info.framebuffer = ((VulkanFramebuffer*)pRenderPassBegin->framebuffer)->getResource();
        vk_render_pass_begin_info.renderArea = rect_2d; // renderArea成员变量用于指定用于渲染的区域
        // clearValueCount和pClearValues成员变量用于指定使用VK_ATTACHMENT_LOAD_OP_CLEAR标记后，使用的清除值
        vk_render_pass_begin_info.clearValueCount = pRenderPassBegin->clearValueCount;
        vk_render_pass_begin_info.pClearValues = vk_clear_value_list.data();

        return _vkCmdBeginRenderPass(((VulkanCommandBuffer*)commandBuffer)->getResource(), &vk_render_pass_begin_info, (VkSubpassContents)contents);
    }

    void VulkanRHI::cmdNextSubpassPFN(RHICommandBuffer* commandBuffer, RHISubpassContents contents)
    {
        return _vkCmdNextSubpass(((VulkanCommandBuffer*)commandBuffer)->getResource(), ((VkSubpassContents)contents));
    }

    void VulkanRHI::cmdEndRenderPassPFN(RHICommandBuffer* commandBuffer)
    {
        return _vkCmdEndRenderPass(((VulkanCommandBuffer*)commandBuffer)->getResource());
    }

    void VulkanRHI::cmdBindPipelinePFN(RHICommandBuffer* commandBuffer, RHIPipelineBindPoint pipelineBindPoint, RHIPipeline* pipeline)
    {
        // vkCmdBindPipeline函数的第二个参数用于指定管线对象是图形管线还是计算管线
        return _vkCmdBindPipeline(((VulkanCommandBuffer*)commandBuffer)->getResource(), (VkPipelineBindPoint)pipelineBindPoint, ((VulkanPipeline*)pipeline)->getResource());
    }

    void VulkanRHI::cmdSetViewportPFN(RHICommandBuffer* commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const RHIViewport* pViewports)
    {
        //viewport
        int viewport_size = viewportCount;
        std::vector<VkViewport> vk_viewport_list(viewport_size);
        for (int i = 0; i < viewport_size; ++i)
        {
            const auto& rhi_viewport_element = pViewports[i];
            auto& vk_viewport_element = vk_viewport_list[i];

            vk_viewport_element.x = rhi_viewport_element.x;
            vk_viewport_element.y = rhi_viewport_element.y;
            vk_viewport_element.width = rhi_viewport_element.width;
            vk_viewport_element.height = rhi_viewport_element.height;
            vk_viewport_element.minDepth = rhi_viewport_element.minDepth;
            vk_viewport_element.maxDepth = rhi_viewport_element.maxDepth;
        };

        return _vkCmdSetViewport(((VulkanCommandBuffer*)commandBuffer)->getResource(), firstViewport, viewportCount, vk_viewport_list.data());
    }

    void VulkanRHI::cmdSetScissorPFN(RHICommandBuffer* commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const RHIRect2D* pScissors)
    {
        //rect_2d
        int rect_2d_size = scissorCount;
        std::vector<VkRect2D> vk_rect_2d_list(rect_2d_size);
        for (int i = 0; i < rect_2d_size; ++i)
        {
            const auto& rhi_rect_2d_element = pScissors[i];
            auto& vk_rect_2d_element = vk_rect_2d_list[i];

            VkOffset2D offset_2d{};
            offset_2d.x = rhi_rect_2d_element.offset.x;
            offset_2d.y = rhi_rect_2d_element.offset.y;

            VkExtent2D extent_2d{};
            extent_2d.width = rhi_rect_2d_element.extent.width;
            extent_2d.height = rhi_rect_2d_element.extent.height;

            vk_rect_2d_element.offset = (VkOffset2D)offset_2d;
            vk_rect_2d_element.extent = (VkExtent2D)extent_2d;

        };

        return _vkCmdSetScissor(((VulkanCommandBuffer*)commandBuffer)->getResource(), firstScissor, scissorCount, vk_rect_2d_list.data());
    }

    void VulkanRHI::cmdBindVertexBuffersPFN(
        RHICommandBuffer* commandBuffer,
        uint32_t firstBinding,
        uint32_t bindingCount,
        RHIBuffer* const* pBuffers,
        const RHIDeviceSize* pOffsets)
    {
        //buffer
        int buffer_size = bindingCount;
        std::vector<VkBuffer> vk_buffer_list(buffer_size);
        for (int i = 0; i < buffer_size; ++i)
        {
            const auto& rhi_buffer_element = pBuffers[i];
            auto& vk_buffer_element = vk_buffer_list[i];

            vk_buffer_element = ((VulkanBuffer*)rhi_buffer_element)->getResource();
        };

        //offset
        int offset_size = bindingCount;
        std::vector<VkDeviceSize> vk_device_size_list(offset_size);
        for (int i = 0; i < offset_size; ++i)
        {
            const auto& rhi_offset_element = pOffsets[i];
            auto& vk_offset_element = vk_device_size_list[i];

            vk_offset_element = rhi_offset_element;
        };

        return _vkCmdBindVertexBuffers(((VulkanCommandBuffer*)commandBuffer)->getResource(), firstBinding, bindingCount, vk_buffer_list.data(), vk_device_size_list.data());
    }

    void VulkanRHI::cmdBindIndexBufferPFN(RHICommandBuffer* commandBuffer, RHIBuffer* buffer, RHIDeviceSize offset, RHIIndexType indexType)
    {
        return _vkCmdBindIndexBuffer(((VulkanCommandBuffer*)commandBuffer)->getResource(), ((VulkanBuffer*)buffer)->getResource(), (VkDeviceSize)offset, (VkIndexType)indexType);
    }

    void VulkanRHI::cmdBindDescriptorSetsPFN(
        RHICommandBuffer* commandBuffer,
        RHIPipelineBindPoint pipelineBindPoint,
        RHIPipelineLayout* layout,
        uint32_t firstSet,
        uint32_t descriptorSetCount,
        const RHIDescriptorSet* const* pDescriptorSets,
        uint32_t dynamicOffsetCount,
        const uint32_t* pDynamicOffsets)
    {
        //descriptor_set
        int descriptor_set_size = descriptorSetCount;
        std::vector<VkDescriptorSet> vk_descriptor_set_list(descriptor_set_size);
        for (int i = 0; i < descriptor_set_size; ++i)
        {
            const auto& rhi_descriptor_set_element = pDescriptorSets[i];
            auto& vk_descriptor_set_element = vk_descriptor_set_list[i];

            vk_descriptor_set_element = ((VulkanDescriptorSet*)rhi_descriptor_set_element)->getResource();
        };

        //offset
        int offset_size = dynamicOffsetCount;
        std::vector<uint32_t> vk_offset_list(offset_size);
        for (int i = 0; i < offset_size; ++i)
        {
            const auto& rhi_offset_element = pDynamicOffsets[i];
            auto& vk_offset_element = vk_offset_list[i];

            vk_offset_element = rhi_offset_element;
        };

        return _vkCmdBindDescriptorSets(
            ((VulkanCommandBuffer*)commandBuffer)->getResource(),
            (VkPipelineBindPoint)pipelineBindPoint,
            ((VulkanPipelineLayout*)layout)->getResource(),
            firstSet, descriptorSetCount,
            vk_descriptor_set_list.data(),
            dynamicOffsetCount,
            vk_offset_list.data());
    }

    void VulkanRHI::cmdDrawIndexedPFN(RHICommandBuffer* commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        return _vkCmdDrawIndexed(((VulkanCommandBuffer*)commandBuffer)->getResource(), indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void VulkanRHI::cmdClearAttachmentsPFN(
        RHICommandBuffer* commandBuffer,
        uint32_t attachmentCount,
        const RHIClearAttachment* pAttachments,
        uint32_t rectCount,
        const RHIClearRect* pRects)
    {
        //clear_attachment
        int clear_attachment_size = attachmentCount;
        std::vector<VkClearAttachment> vk_clear_attachment_list(clear_attachment_size);
        for (int i = 0; i < clear_attachment_size; ++i)
        {
            const auto& rhi_clear_attachment_element = pAttachments[i];
            auto& vk_clear_attachment_element = vk_clear_attachment_list[i];

            VkClearColorValue vk_clear_color_value;
            vk_clear_color_value.float32[0] = rhi_clear_attachment_element.clearValue.color.float32[0];
            vk_clear_color_value.float32[1] = rhi_clear_attachment_element.clearValue.color.float32[1];
            vk_clear_color_value.float32[2] = rhi_clear_attachment_element.clearValue.color.float32[2];
            vk_clear_color_value.float32[3] = rhi_clear_attachment_element.clearValue.color.float32[3];
            vk_clear_color_value.int32[0] = rhi_clear_attachment_element.clearValue.color.int32[0];
            vk_clear_color_value.int32[1] = rhi_clear_attachment_element.clearValue.color.int32[1];
            vk_clear_color_value.int32[2] = rhi_clear_attachment_element.clearValue.color.int32[2];
            vk_clear_color_value.int32[3] = rhi_clear_attachment_element.clearValue.color.int32[3];
            vk_clear_color_value.uint32[0] = rhi_clear_attachment_element.clearValue.color.uint32[0];
            vk_clear_color_value.uint32[1] = rhi_clear_attachment_element.clearValue.color.uint32[1];
            vk_clear_color_value.uint32[2] = rhi_clear_attachment_element.clearValue.color.uint32[2];
            vk_clear_color_value.uint32[3] = rhi_clear_attachment_element.clearValue.color.uint32[3];

            VkClearDepthStencilValue vk_clear_depth_stencil_value;
            vk_clear_depth_stencil_value.depth = rhi_clear_attachment_element.clearValue.depthStencil.depth;
            vk_clear_depth_stencil_value.stencil = rhi_clear_attachment_element.clearValue.depthStencil.stencil;

            vk_clear_attachment_element.clearValue.color = vk_clear_color_value;
            vk_clear_attachment_element.clearValue.depthStencil = vk_clear_depth_stencil_value;
            vk_clear_attachment_element.aspectMask = rhi_clear_attachment_element.aspectMask;
            vk_clear_attachment_element.colorAttachment = rhi_clear_attachment_element.colorAttachment;
        };

        //clear_rect
        int clear_rect_size = rectCount;
        std::vector<VkClearRect> vk_clear_rect_list(clear_rect_size);
        for (int i = 0; i < clear_rect_size; ++i)
        {
            const auto& rhi_clear_rect_element = pRects[i];
            auto& vk_clear_rect_element = vk_clear_rect_list[i];

            VkOffset2D offset_2d{};
            offset_2d.x = rhi_clear_rect_element.rect.offset.x;
            offset_2d.y = rhi_clear_rect_element.rect.offset.y;

            VkExtent2D extent_2d{};
            extent_2d.width = rhi_clear_rect_element.rect.extent.width;
            extent_2d.height = rhi_clear_rect_element.rect.extent.height;

            vk_clear_rect_element.rect.offset = (VkOffset2D)offset_2d;
            vk_clear_rect_element.rect.extent = (VkExtent2D)extent_2d;
            vk_clear_rect_element.baseArrayLayer = rhi_clear_rect_element.baseArrayLayer;
            vk_clear_rect_element.layerCount = rhi_clear_rect_element.layerCount;
        };

        return _vkCmdClearAttachments(
            ((VulkanCommandBuffer*)commandBuffer)->getResource(),
            attachmentCount,
            vk_clear_attachment_list.data(),
            rectCount,
            vk_clear_rect_list.data());
    }

    // 记录指令到指令缓冲
    bool VulkanRHI::beginCommandBuffer(RHICommandBuffer* commandBuffer, const RHICommandBufferBeginInfo* pBeginInfo)
    {
        VkCommandBufferInheritanceInfo command_buffer_inheritance_info{};
        const VkCommandBufferInheritanceInfo* command_buffer_inheritance_info_ptr = nullptr;
        if (pBeginInfo->pInheritanceInfo != nullptr)
        {
            command_buffer_inheritance_info.sType = (VkStructureType)(pBeginInfo->pInheritanceInfo->sType);
            command_buffer_inheritance_info.pNext = (const void*)pBeginInfo->pInheritanceInfo->pNext;
            command_buffer_inheritance_info.renderPass = ((VulkanRenderPass*)pBeginInfo->pInheritanceInfo->renderPass)->getResource();
            command_buffer_inheritance_info.subpass = pBeginInfo->pInheritanceInfo->subpass;
            command_buffer_inheritance_info.framebuffer = ((VulkanFramebuffer*)(pBeginInfo->pInheritanceInfo->framebuffer))->getResource();
            command_buffer_inheritance_info.occlusionQueryEnable = (VkBool32)pBeginInfo->pInheritanceInfo->occlusionQueryEnable;
            command_buffer_inheritance_info.queryFlags = (VkQueryControlFlags)pBeginInfo->pInheritanceInfo->queryFlags;
            command_buffer_inheritance_info.pipelineStatistics = (VkQueryPipelineStatisticFlags)pBeginInfo->pInheritanceInfo->pipelineStatistics;

            command_buffer_inheritance_info_ptr = &command_buffer_inheritance_info;
        }

        // 通过调用vkBeginCommandBuffer函数开始指令缓冲的记录操作，
        // 这一函数以VkCommandBufferBeginInfo结构体作为参数来指定一些有关指令缓冲的使用细节
        VkCommandBufferBeginInfo command_buffer_begin_info{};
        command_buffer_begin_info.sType = (VkStructureType)pBeginInfo->sType;
        command_buffer_begin_info.pNext = (const void*)pBeginInfo->pNext;
        command_buffer_begin_info.flags = (VkCommandBufferUsageFlags)pBeginInfo->flags; // flags成员变量用于指定我们将要怎样使用指令缓冲
        // pInheritanceInfo成员变量只用于辅助指令缓冲，可以用它来指定从调用它的主要指令缓冲继承的状态
        command_buffer_begin_info.pInheritanceInfo = command_buffer_inheritance_info_ptr;

        VkResult result = vkBeginCommandBuffer(((VulkanCommandBuffer*)commandBuffer)->getResource(), &command_buffer_begin_info);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("vkBeginCommandBuffer failed!");
            return false;
        }
    }

    bool VulkanRHI::endCommandBuffer(RHICommandBuffer* commandBuffer)
    {
        VkResult result = vkEndCommandBuffer(((VulkanCommandBuffer*)commandBuffer)->getResource());

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("vkEndCommandBuffer failed!");
            return false;
        }
    }

    void VulkanRHI::updateDescriptorSets(
        uint32_t descriptorWriteCount,
        const RHIWriteDescriptorSet* pDescriptorWrites,
        uint32_t descriptorCopyCount,
        const RHICopyDescriptorSet* pDescriptorCopies)
    {
        //write_descriptor_set
        int write_descriptor_set_size = descriptorWriteCount;
        std::vector<VkWriteDescriptorSet> vk_write_descriptor_set_list(write_descriptor_set_size);
        int image_info_count = 0;
        int buffer_info_count = 0;
        for (int i = 0; i < write_descriptor_set_size; ++i)
        {
            const auto& rhi_write_descriptor_set_element = pDescriptorWrites[i];
            if (rhi_write_descriptor_set_element.pImageInfo != nullptr)
            {
                image_info_count++;
            }
            if (rhi_write_descriptor_set_element.pBufferInfo != nullptr)
            {
                buffer_info_count++;
            }
        }
        std::vector<VkDescriptorImageInfo> vk_descriptor_image_info_list(image_info_count);
        std::vector<VkDescriptorBufferInfo> vk_descriptor_buffer_info_list(buffer_info_count);
        int image_info_current = 0;
        int buffer_info_current = 0;

        for (int i = 0; i < write_descriptor_set_size; ++i)
        {
            const auto& rhi_write_descriptor_set_element = pDescriptorWrites[i];
            auto& vk_write_descriptor_set_element = vk_write_descriptor_set_list[i];

            const VkDescriptorImageInfo* vk_descriptor_image_info_ptr = nullptr;
            if (rhi_write_descriptor_set_element.pImageInfo != nullptr)
            {
                auto& vk_descriptor_image_info = vk_descriptor_image_info_list[image_info_current];
                if (rhi_write_descriptor_set_element.pImageInfo->sampler == nullptr)
                {
                    vk_descriptor_image_info.sampler = nullptr;
                }
                else
                {
                    vk_descriptor_image_info.sampler = ((VulkanSampler*)rhi_write_descriptor_set_element.pImageInfo->sampler)->getResource();
                }
                vk_descriptor_image_info.imageView = ((VulkanImageView*)rhi_write_descriptor_set_element.pImageInfo->imageView)->getResource();
                vk_descriptor_image_info.imageLayout = (VkImageLayout)rhi_write_descriptor_set_element.pImageInfo->imageLayout;

                vk_descriptor_image_info_ptr = &vk_descriptor_image_info;
                image_info_current++;
            }

            const VkDescriptorBufferInfo* vk_descriptor_buffer_info_ptr = nullptr;
            if (rhi_write_descriptor_set_element.pBufferInfo != nullptr)
            {
                auto& vk_descriptor_buffer_info = vk_descriptor_buffer_info_list[buffer_info_current];
                vk_descriptor_buffer_info.buffer = ((VulkanBuffer*)rhi_write_descriptor_set_element.pBufferInfo->buffer)->getResource();
                vk_descriptor_buffer_info.offset = (VkDeviceSize)rhi_write_descriptor_set_element.pBufferInfo->offset;
                vk_descriptor_buffer_info.range = (VkDeviceSize)rhi_write_descriptor_set_element.pBufferInfo->range;

                vk_descriptor_buffer_info_ptr = &vk_descriptor_buffer_info;
                buffer_info_current++;
            }

            vk_write_descriptor_set_element.sType = (VkStructureType)rhi_write_descriptor_set_element.sType;
            vk_write_descriptor_set_element.pNext = (const void*)rhi_write_descriptor_set_element.pNext;
            vk_write_descriptor_set_element.dstSet = ((VulkanDescriptorSet*)rhi_write_descriptor_set_element.dstSet)->getResource();
            vk_write_descriptor_set_element.dstBinding = rhi_write_descriptor_set_element.dstBinding;
            vk_write_descriptor_set_element.dstArrayElement = rhi_write_descriptor_set_element.dstArrayElement;
            vk_write_descriptor_set_element.descriptorCount = rhi_write_descriptor_set_element.descriptorCount;
            vk_write_descriptor_set_element.descriptorType = (VkDescriptorType)rhi_write_descriptor_set_element.descriptorType;
            vk_write_descriptor_set_element.pImageInfo = vk_descriptor_image_info_ptr; //
            vk_write_descriptor_set_element.pBufferInfo = vk_descriptor_buffer_info_ptr; // 对Image和Buffer资源的实际绑定
            //vk_write_descriptor_set_element.pTexelBufferView = &((VulkanBufferView*)rhi_write_descriptor_set_element.pTexelBufferView)->getResource();
        };

        if (image_info_current != image_info_count
            || buffer_info_current != buffer_info_count)
        {
            LOG_ERROR("image_info_current != image_info_count || buffer_info_current != buffer_info_count");
            return;
        }

        //copy_descriptor_set
        int copy_descriptor_set_size = descriptorCopyCount;
        std::vector<VkCopyDescriptorSet> vk_copy_descriptor_set_list(copy_descriptor_set_size);
        for (int i = 0; i < copy_descriptor_set_size; ++i)
        {
            const auto& rhi_copy_descriptor_set_element = pDescriptorCopies[i];
            auto& vk_copy_descriptor_set_element = vk_copy_descriptor_set_list[i];

            vk_copy_descriptor_set_element.sType = (VkStructureType)rhi_copy_descriptor_set_element.sType;
            vk_copy_descriptor_set_element.pNext = (const void*)rhi_copy_descriptor_set_element.pNext;
            vk_copy_descriptor_set_element.srcSet = ((VulkanDescriptorSet*)rhi_copy_descriptor_set_element.srcSet)->getResource();
            vk_copy_descriptor_set_element.srcBinding = rhi_copy_descriptor_set_element.srcBinding;
            vk_copy_descriptor_set_element.srcArrayElement = rhi_copy_descriptor_set_element.srcArrayElement;
            vk_copy_descriptor_set_element.dstSet = ((VulkanDescriptorSet*)rhi_copy_descriptor_set_element.dstSet)->getResource();
            vk_copy_descriptor_set_element.dstBinding = rhi_copy_descriptor_set_element.dstBinding;
            vk_copy_descriptor_set_element.dstArrayElement = rhi_copy_descriptor_set_element.dstArrayElement;
            vk_copy_descriptor_set_element.descriptorCount = rhi_copy_descriptor_set_element.descriptorCount;
        };

        vkUpdateDescriptorSets(m_device, descriptorWriteCount, vk_write_descriptor_set_list.data(), descriptorCopyCount, vk_copy_descriptor_set_list.data());
    }

    // 提交信息给指令队列
    bool VulkanRHI::queueSubmit(RHIQueue* queue, uint32_t submitCount, const RHISubmitInfo* pSubmits, RHIFence* fence)
    {
        //submit_info
        int command_buffer_size_total = 0;
        int semaphore_size_total = 0;
        int signal_semaphore_size_total = 0;
        int pipeline_stage_flags_size_total = 0;

        int submit_info_size = submitCount;
        for (int i = 0; i < submit_info_size; ++i)
        {
            const auto& rhi_submit_info_element = pSubmits[i];
            command_buffer_size_total += rhi_submit_info_element.commandBufferCount;
            semaphore_size_total += rhi_submit_info_element.waitSemaphoreCount;
            signal_semaphore_size_total += rhi_submit_info_element.signalSemaphoreCount;
            pipeline_stage_flags_size_total += rhi_submit_info_element.waitSemaphoreCount;
        }
        std::vector<VkCommandBuffer> vk_command_buffer_list_external(command_buffer_size_total);
        std::vector<VkSemaphore> vk_semaphore_list_external(semaphore_size_total);
        std::vector<VkSemaphore> vk_signal_semaphore_list_external(signal_semaphore_size_total);
        std::vector<VkPipelineStageFlags> vk_pipeline_stage_flags_list_external(pipeline_stage_flags_size_total);

        int command_buffer_size_current = 0;
        int semaphore_size_current = 0;
        int signal_semaphore_size_current = 0;
        int pipeline_stage_flags_size_current = 0;


        std::vector<VkSubmitInfo> vk_submit_info_list(submit_info_size);
        for (int i = 0; i < submit_info_size; ++i)
        {
            const auto& rhi_submit_info_element = pSubmits[i];
            auto& vk_submit_info_element = vk_submit_info_list[i];

            vk_submit_info_element.sType = (VkStructureType)rhi_submit_info_element.sType;
            vk_submit_info_element.pNext = (const void*)rhi_submit_info_element.pNext;

            //command_buffer
            if (rhi_submit_info_element.commandBufferCount > 0)
            {
                vk_submit_info_element.commandBufferCount = rhi_submit_info_element.commandBufferCount;
                vk_submit_info_element.pCommandBuffers = &vk_command_buffer_list_external[command_buffer_size_current];
                int command_buffer_size = rhi_submit_info_element.commandBufferCount;
                for (int i = 0; i < command_buffer_size; ++i)
                {
                    const auto& rhi_command_buffer_element = rhi_submit_info_element.pCommandBuffers[i];
                    auto& vk_command_buffer_element = vk_command_buffer_list_external[command_buffer_size_current];

                    vk_command_buffer_element = ((VulkanCommandBuffer*)rhi_command_buffer_element)->getResource();

                    command_buffer_size_current++;
                };
            }

            //semaphore
            if (rhi_submit_info_element.waitSemaphoreCount > 0)
            {
                vk_submit_info_element.waitSemaphoreCount = rhi_submit_info_element.waitSemaphoreCount;
                vk_submit_info_element.pWaitSemaphores = &vk_semaphore_list_external[semaphore_size_current];
                int semaphore_size = rhi_submit_info_element.waitSemaphoreCount;
                for (int i = 0; i < semaphore_size; ++i)
                {
                    const auto& rhi_semaphore_element = rhi_submit_info_element.pWaitSemaphores[i];
                    auto& vk_semaphore_element = vk_semaphore_list_external[semaphore_size_current];

                    vk_semaphore_element = ((VulkanSemaphore*)rhi_semaphore_element)->getResource();

                    semaphore_size_current++;
                };
            }

            //signal_semaphore
            if (rhi_submit_info_element.signalSemaphoreCount > 0)
            {
                vk_submit_info_element.signalSemaphoreCount = rhi_submit_info_element.signalSemaphoreCount;
                vk_submit_info_element.pSignalSemaphores = &vk_signal_semaphore_list_external[signal_semaphore_size_current];
                int signal_semaphore_size = rhi_submit_info_element.signalSemaphoreCount;
                for (int i = 0; i < signal_semaphore_size; ++i)
                {
                    const auto& rhi_signal_semaphore_element = rhi_submit_info_element.pSignalSemaphores[i];
                    auto& vk_signal_semaphore_element = vk_signal_semaphore_list_external[signal_semaphore_size_current];

                    vk_signal_semaphore_element = ((VulkanSemaphore*)rhi_signal_semaphore_element)->getResource();

                    signal_semaphore_size_current++;
                };
            }

            //pipeline_stage_flags
            if (rhi_submit_info_element.waitSemaphoreCount > 0)
            {
                vk_submit_info_element.pWaitDstStageMask = &vk_pipeline_stage_flags_list_external[pipeline_stage_flags_size_current];
                int pipeline_stage_flags_size = rhi_submit_info_element.waitSemaphoreCount;
                for (int i = 0; i < pipeline_stage_flags_size; ++i)
                {
                    const auto& rhi_pipeline_stage_flags_element = rhi_submit_info_element.pWaitDstStageMask[i];
                    auto& vk_pipeline_stage_flags_element = vk_pipeline_stage_flags_list_external[pipeline_stage_flags_size_current];

                    vk_pipeline_stage_flags_element = (VkPipelineStageFlags)rhi_pipeline_stage_flags_element;

                    pipeline_stage_flags_size_current++;
                };
            }
        };
        

        if ((command_buffer_size_total != command_buffer_size_current)
            || (semaphore_size_total != semaphore_size_current)
            || (signal_semaphore_size_total != signal_semaphore_size_current)
            || (pipeline_stage_flags_size_total != pipeline_stage_flags_size_current))
        {
            LOG_ERROR("submit info is not right!");
            return false;
        }

        VkFence vk_fence = VK_NULL_HANDLE;
        if (fence != nullptr)
        {
            vk_fence = ((VulkanFence*)fence)->getResource();
        }

        VkResult result = vkQueueSubmit(((VulkanQueue*)queue)->getResource(), submitCount, vk_submit_info_list.data(), vk_fence);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("vkQueueSubmit failed!");
            return false;
        }
    }

    bool VulkanRHI::queueWaitIdle(RHIQueue* queue)
    {
        VkResult result = vkQueueWaitIdle(((VulkanQueue*)queue)->getResource());

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("vkQueueWaitIdle failed!");
            return false;
        }
    }

    void VulkanRHI::cmdPipelineBarrier(RHICommandBuffer* commandBuffer,
        RHIPipelineStageFlags srcStageMask,
        RHIPipelineStageFlags dstStageMask,
        RHIDependencyFlags dependencyFlags,
        uint32_t memoryBarrierCount,
        const RHIMemoryBarrier* pMemoryBarriers,
        uint32_t bufferMemoryBarrierCount,
        const RHIBufferMemoryBarrier* pBufferMemoryBarriers,
        uint32_t imageMemoryBarrierCount,
        const RHIImageMemoryBarrier* pImageMemoryBarriers)
    {

        //memory_barrier
        // 有三种内存屏障类型：VkMemoryBarrier、VkBufferMemoryBarrier和VkImageMemoryBarrier。
        // VkMemoryBarrier可以进行所有内存资源的同步操作，其它两种类型的内存屏障用于同步特定的内存资源
        int memory_barrier_size = memoryBarrierCount;
        std::vector<VkMemoryBarrier> vk_memory_barrier_list(memory_barrier_size);
        for (int i = 0; i < memory_barrier_size; ++i)
        {
            const auto& rhi_memory_barrier_element = pMemoryBarriers[i];
            auto& vk_memory_barrier_element = vk_memory_barrier_list[i];


            vk_memory_barrier_element.sType = (VkStructureType)rhi_memory_barrier_element.sType;
            vk_memory_barrier_element.pNext = (const void*)rhi_memory_barrier_element.pNext;
            vk_memory_barrier_element.srcAccessMask = (VkAccessFlags)rhi_memory_barrier_element.srcAccessMask;
            vk_memory_barrier_element.dstAccessMask = (VkAccessFlags)rhi_memory_barrier_element.dstAccessMask;
        };

        //buffer_memory_barrier
        int buffer_memory_barrier_size = bufferMemoryBarrierCount;
        std::vector<VkBufferMemoryBarrier> vk_buffer_memory_barrier_list(buffer_memory_barrier_size);
        for (int i = 0; i < buffer_memory_barrier_size; ++i)
        {
            const auto& rhi_buffer_memory_barrier_element = pBufferMemoryBarriers[i];
            auto& vk_buffer_memory_barrier_element = vk_buffer_memory_barrier_list[i];

            vk_buffer_memory_barrier_element.sType = (VkStructureType)rhi_buffer_memory_barrier_element.sType;
            vk_buffer_memory_barrier_element.pNext = (const void*)rhi_buffer_memory_barrier_element.pNext;
            vk_buffer_memory_barrier_element.srcAccessMask = (VkAccessFlags)rhi_buffer_memory_barrier_element.srcAccessMask;
            vk_buffer_memory_barrier_element.dstAccessMask = (VkAccessFlags)rhi_buffer_memory_barrier_element.dstAccessMask;
            vk_buffer_memory_barrier_element.srcQueueFamilyIndex = rhi_buffer_memory_barrier_element.srcQueueFamilyIndex;
            vk_buffer_memory_barrier_element.dstQueueFamilyIndex = rhi_buffer_memory_barrier_element.dstQueueFamilyIndex;
            vk_buffer_memory_barrier_element.buffer = ((VulkanBuffer*)rhi_buffer_memory_barrier_element.buffer)->getResource();
            vk_buffer_memory_barrier_element.offset = (VkDeviceSize)rhi_buffer_memory_barrier_element.offset;
            vk_buffer_memory_barrier_element.size = (VkDeviceSize)rhi_buffer_memory_barrier_element.size;
        };

        //image_memory_barrier
        int image_memory_barrier_size = imageMemoryBarrierCount;
        std::vector<VkImageMemoryBarrier> vk_image_memory_barrier_list(image_memory_barrier_size);
        for (int i = 0; i < image_memory_barrier_size; ++i)
        {
            const auto& rhi_image_memory_barrier_element = pImageMemoryBarriers[i];
            auto& vk_image_memory_barrier_element = vk_image_memory_barrier_list[i];

            VkImageSubresourceRange image_subresource_range{};
            image_subresource_range.aspectMask = (VkImageAspectFlags)rhi_image_memory_barrier_element.subresourceRange.aspectMask;
            image_subresource_range.baseMipLevel = rhi_image_memory_barrier_element.subresourceRange.baseMipLevel;
            image_subresource_range.levelCount = rhi_image_memory_barrier_element.subresourceRange.levelCount;
            image_subresource_range.baseArrayLayer = rhi_image_memory_barrier_element.subresourceRange.baseArrayLayer;
            image_subresource_range.layerCount = rhi_image_memory_barrier_element.subresourceRange.layerCount;

            vk_image_memory_barrier_element.sType = (VkStructureType)rhi_image_memory_barrier_element.sType;
            vk_image_memory_barrier_element.pNext = (const void*)rhi_image_memory_barrier_element.pNext;
            vk_image_memory_barrier_element.srcAccessMask = (VkAccessFlags)rhi_image_memory_barrier_element.srcAccessMask;
            vk_image_memory_barrier_element.dstAccessMask = (VkAccessFlags)rhi_image_memory_barrier_element.dstAccessMask;
            vk_image_memory_barrier_element.oldLayout = (VkImageLayout)rhi_image_memory_barrier_element.oldLayout;
            vk_image_memory_barrier_element.newLayout = (VkImageLayout)rhi_image_memory_barrier_element.newLayout;
            vk_image_memory_barrier_element.srcQueueFamilyIndex = rhi_image_memory_barrier_element.srcQueueFamilyIndex;
            vk_image_memory_barrier_element.dstQueueFamilyIndex = rhi_image_memory_barrier_element.dstQueueFamilyIndex;
            vk_image_memory_barrier_element.image = ((VulkanImage*)rhi_image_memory_barrier_element.image)->getResource();
            vk_image_memory_barrier_element.subresourceRange = image_subresource_range;
        };

        vkCmdPipelineBarrier(
            ((VulkanCommandBuffer*)commandBuffer)->getResource(),
            (RHIPipelineStageFlags)srcStageMask,
            (RHIPipelineStageFlags)dstStageMask,
            (RHIDependencyFlags)dependencyFlags,
            memoryBarrierCount,
            vk_memory_barrier_list.data(),
            bufferMemoryBarrierCount,
            vk_buffer_memory_barrier_list.data(),
            imageMemoryBarrierCount,
            vk_image_memory_barrier_list.data());
    }

    void VulkanRHI::cmdDraw(RHICommandBuffer* commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        vkCmdDraw(((VulkanCommandBuffer*)commandBuffer)->getResource(), vertexCount, instanceCount, firstVertex, firstInstance);
    }
    
    void VulkanRHI::cmdDispatch(RHICommandBuffer* commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        vkCmdDispatch(((VulkanCommandBuffer*)commandBuffer)->getResource(), groupCountX, groupCountY, groupCountZ);
    }

    void VulkanRHI::cmdDispatchIndirect(RHICommandBuffer* commandBuffer, RHIBuffer* buffer, RHIDeviceSize offset)
    {
        vkCmdDispatchIndirect(((VulkanCommandBuffer*)commandBuffer)->getResource(), ((VulkanBuffer*)buffer)->getResource(), offset);
    }

    void VulkanRHI::cmdCopyImageToBuffer(
        RHICommandBuffer* commandBuffer,
        RHIImage* srcImage,
        RHIImageLayout srcImageLayout,
        RHIBuffer* dstBuffer,
        uint32_t regionCount,
        const RHIBufferImageCopy* pRegions)
    {
        //buffer_image_copy
        int buffer_image_copy_size = regionCount;
        std::vector<VkBufferImageCopy> vk_buffer_image_copy_list(buffer_image_copy_size);
        for (int i = 0; i < buffer_image_copy_size; ++i)
        {
            const auto& rhi_buffer_image_copy_element = pRegions[i];
            auto& vk_buffer_image_copy_element = vk_buffer_image_copy_list[i];

            VkImageSubresourceLayers image_subresource_layers{};
            image_subresource_layers.aspectMask = (VkImageAspectFlags)rhi_buffer_image_copy_element.imageSubresource.aspectMask;
            image_subresource_layers.mipLevel = rhi_buffer_image_copy_element.imageSubresource.mipLevel;
            image_subresource_layers.baseArrayLayer = rhi_buffer_image_copy_element.imageSubresource.baseArrayLayer;
            image_subresource_layers.layerCount = rhi_buffer_image_copy_element.imageSubresource.layerCount;

            VkOffset3D offset_3d{};
            offset_3d.x = rhi_buffer_image_copy_element.imageOffset.x;
            offset_3d.y = rhi_buffer_image_copy_element.imageOffset.y;
            offset_3d.z = rhi_buffer_image_copy_element.imageOffset.z;

            VkExtent3D extent_3d{};
            extent_3d.width = rhi_buffer_image_copy_element.imageExtent.width;
            extent_3d.height = rhi_buffer_image_copy_element.imageExtent.height;
            extent_3d.depth = rhi_buffer_image_copy_element.imageExtent.depth;

            VkBufferImageCopy buffer_image_copy{};
            buffer_image_copy.bufferOffset = (VkDeviceSize)rhi_buffer_image_copy_element.bufferOffset;
            buffer_image_copy.bufferRowLength = rhi_buffer_image_copy_element.bufferRowLength;
            buffer_image_copy.bufferImageHeight = rhi_buffer_image_copy_element.bufferImageHeight;
            buffer_image_copy.imageSubresource = image_subresource_layers;
            buffer_image_copy.imageOffset = offset_3d;
            buffer_image_copy.imageExtent = extent_3d;

            vk_buffer_image_copy_element.bufferOffset = (VkDeviceSize)rhi_buffer_image_copy_element.bufferOffset;
            vk_buffer_image_copy_element.bufferRowLength = rhi_buffer_image_copy_element.bufferRowLength;
            vk_buffer_image_copy_element.bufferImageHeight = rhi_buffer_image_copy_element.bufferImageHeight;
            vk_buffer_image_copy_element.imageSubresource = image_subresource_layers;
            vk_buffer_image_copy_element.imageOffset = offset_3d;
            vk_buffer_image_copy_element.imageExtent = extent_3d;
        };

        vkCmdCopyImageToBuffer(
            ((VulkanCommandBuffer*)commandBuffer)->getResource(),
            ((VulkanImage*)srcImage)->getResource(),
            (VkImageLayout)srcImageLayout,
            ((VulkanBuffer*)dstBuffer)->getResource(),
            regionCount,
            vk_buffer_image_copy_list.data());
    }

    void VulkanRHI::cmdCopyImageToImage(RHICommandBuffer* commandBuffer, RHIImage* srcImage, RHIImageAspectFlagBits srcFlag, RHIImage* dstImage, RHIImageAspectFlagBits dstFlag, uint32_t width, uint32_t height)
    {
        VkImageCopy imagecopyRegion = {};
        imagecopyRegion.srcSubresource = { (VkImageAspectFlags)srcFlag, 0, 0, 1 };
        imagecopyRegion.srcOffset = { 0, 0, 0 };
        imagecopyRegion.dstSubresource = { (VkImageAspectFlags)dstFlag, 0, 0, 1 };
        imagecopyRegion.dstOffset = { 0, 0, 0 };
        imagecopyRegion.extent = { width, height, 1 };

        vkCmdCopyImage(((VulkanCommandBuffer*)commandBuffer)->getResource(),
            ((VulkanImage*)srcImage)->getResource(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            ((VulkanImage*)dstImage)->getResource(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &imagecopyRegion);
    }

    void VulkanRHI::cmdCopyBuffer(RHICommandBuffer* commandBuffer, RHIBuffer* srcBuffer, RHIBuffer* dstBuffer, uint32_t regionCount, RHIBufferCopy* pRegions)
    {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = pRegions->srcOffset;
        copyRegion.dstOffset = pRegions->dstOffset;
        copyRegion.size = pRegions->size;

        vkCmdCopyBuffer(((VulkanCommandBuffer*)commandBuffer)->getResource(),
            ((VulkanBuffer*)srcBuffer)->getResource(),
            ((VulkanBuffer*)dstBuffer)->getResource(),
            regionCount,
            &copyRegion);
    }

    // 分配指令缓冲 allocate a single command buffer from the command pool
    void VulkanRHI::createCommandBuffers()
    {
        VkCommandBufferAllocateInfo command_buffer_allocate_info {};
        command_buffer_allocate_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        // level成员变量用于指定分配的指令缓冲对象是primary还是secondary
        // VK_COMMAND_BUFFER_LEVEL_PRIMARY：可以被提交到队列进行执行，但不能被其它指令缓冲对象调用。
        // VK_COMMAND_BUFFER_LEVEL_SECONDARY：不能直接被提交到队列进行执行，但可以从primary command buffer调用执行。
        command_buffer_allocate_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_buffer_allocate_info.commandBufferCount = 1U;

        for (uint32_t i = 0; i < k_max_frames_in_flight; ++i)
        {
            command_buffer_allocate_info.commandPool = m_command_pools[i];
            VkCommandBuffer vk_command_buffer;
            if (vkAllocateCommandBuffers(m_device, &command_buffer_allocate_info, &vk_command_buffer) != VK_SUCCESS)
            {
                LOG_ERROR("vk allocate command buffers");
            }
            m_vk_command_buffers[i] = vk_command_buffer;
            m_command_buffers[i] = new VulkanCommandBuffer();
            ((VulkanCommandBuffer*)m_command_buffers[i])->setResource(vk_command_buffer);
        }
    }

    void VulkanRHI::createDescriptorPool()
    {
        // Since DescriptorSet should be treated as asset in Vulkan, DescriptorPool
        // should be big enough, and thus we can sub-allocate DescriptorSet from
        // DescriptorPool merely as we sub-allocate Buffer/Image from DeviceMemory.

        VkDescriptorPoolSize pool_sizes[7];
        pool_sizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        pool_sizes[0].descriptorCount = 3 + 2 + 2 + 2 + 1 + 1 + 3 + 3;
        pool_sizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[1].descriptorCount = 1 + 1 + 1 * m_max_vertex_blending_mesh_count;
        pool_sizes[2].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[2].descriptorCount = 1 * m_max_material_count;
        pool_sizes[3].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[3].descriptorCount = 3 + 5 * m_max_material_count + 1 + 1; // ImGui_ImplVulkan_CreateDeviceObjects
        pool_sizes[4].type            = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        pool_sizes[4].descriptorCount = 4 + 1 + 1 + 2;
        pool_sizes[5].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        pool_sizes[5].descriptorCount = 3;
        pool_sizes[6].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[6].descriptorCount = 1;

        VkDescriptorPoolCreateInfo pool_info {};
        pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = sizeof(pool_sizes) / sizeof(pool_sizes[0]);
        pool_info.pPoolSizes    = pool_sizes;
        pool_info.maxSets =
            1 + 1 + 1 + m_max_material_count + m_max_vertex_blending_mesh_count + 1 + 1; // +skybox + axis descriptor set
        pool_info.flags = 0U;

        if (vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_vk_descriptor_pool) != VK_SUCCESS)
        {
            LOG_ERROR("create descriptor pool");
        }

        m_descriptor_pool = new VulkanDescriptorPool();
        ((VulkanDescriptorPool*)m_descriptor_pool)->setResource(m_vk_descriptor_pool);
    }

    // semaphore : signal an image is ready for rendering // ready for presentation
    // (m_vulkan_context._swapchain_images --> semaphores, fences)
    void VulkanRHI::createSyncPrimitives()
    {
        VkSemaphoreCreateInfo semaphore_create_info {};
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_create_info {};
        fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        // 栅栏(fence)对象在创建后是未发出信号的状态。这就意味着如果我们没有在vkWaitForFences函数调用之前发出栅栏(fence)信号，
        // vkWaitForFences函数调用将会一直处于等待状态。我们可以在创建栅栏(fence)对象时，设置它的初始状态为已发出信号来解决这一问题
        fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT; // the fence is initialized as signaled

        for (uint32_t i = 0; i < k_max_frames_in_flight; i++)
        {
            m_image_available_for_texturescopy_semaphores[i] = new VulkanSemaphore();
            if (vkCreateSemaphore(
                    m_device, &semaphore_create_info, nullptr, &m_image_available_for_render_semaphores[i]) !=
                    VK_SUCCESS ||
                vkCreateSemaphore(
                    m_device, &semaphore_create_info, nullptr, &m_image_finished_for_presentation_semaphores[i]) !=
                    VK_SUCCESS ||
                vkCreateSemaphore(
                    m_device, &semaphore_create_info, nullptr, &(((VulkanSemaphore*)m_image_available_for_texturescopy_semaphores[i])->getResource())) !=
                    VK_SUCCESS ||
                vkCreateFence(m_device, &fence_create_info, nullptr, &m_is_frame_in_flight_fences[i]) != VK_SUCCESS)
            {
                LOG_ERROR("vk create semaphore & fence");
            }

            m_rhi_is_frame_in_flight_fences[i] = new VulkanFence();
            ((VulkanFence*)m_rhi_is_frame_in_flight_fences[i])->setResource(m_is_frame_in_flight_fences[i]);
        }
    }

    void VulkanRHI::createFramebufferImageAndView()
    {
        VulkanUtil::createImage(m_physical_device,
                                m_device,
                                m_swapchain_extent.width,
                                m_swapchain_extent.height,
                                (VkFormat)m_depth_image_format,
                                VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                ((VulkanImage*)m_depth_image)->getResource(),
                                m_depth_image_memory,
                                0,
                                1,
                                1);

        ((VulkanImageView*)m_depth_image_view)->setResource(
            VulkanUtil::createImageView(m_device, ((VulkanImage*)m_depth_image)->getResource(), (VkFormat)m_depth_image_format, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D, 1, 1));
    }

    RHISampler* VulkanRHI::getOrCreateDefaultSampler(RHIDefaultSamplerType type)
    {
        switch (type)
        {
        case Piccolo::Default_Sampler_Linear:
            if (m_linear_sampler == nullptr)
            {
                m_linear_sampler = new VulkanSampler();
                ((VulkanSampler*)m_linear_sampler)->setResource(VulkanUtil::getOrCreateLinearSampler(m_physical_device, m_device));
            }
            return m_linear_sampler;
            break;

        case Piccolo::Default_Sampler_Nearest:
            if (m_nearest_sampler == nullptr)
            {
                m_nearest_sampler = new VulkanSampler();
                ((VulkanSampler*)m_nearest_sampler)->setResource(VulkanUtil::getOrCreateNearestSampler(m_physical_device, m_device));
            }
            return m_nearest_sampler;
            break;

        default:
            return nullptr;
            break;
        }
    }

    RHISampler* VulkanRHI::getOrCreateMipmapSampler(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            LOG_ERROR("width == 0 || height == 0");
            return nullptr;
        }
        RHISampler* sampler;
        uint32_t  mip_levels = floor(log2(std::max(width, height))) + 1;
        auto      find_sampler = m_mipmap_sampler_map.find(mip_levels);
        if (find_sampler != m_mipmap_sampler_map.end())
        {
            return find_sampler->second;
        }
        else
        {
            sampler = new VulkanSampler();

            VkSampler vk_sampler = VulkanUtil::getOrCreateMipmapSampler(m_physical_device, m_device, width, height);

            ((VulkanSampler*)sampler)->setResource(vk_sampler);

            m_mipmap_sampler_map.insert(std::make_pair(mip_levels, sampler));

            return sampler;
        }
    }

    // 创建着色器模块 要将着色器字节码在管线上使用，还需要使用VkShaderModule对象
    // 着色器模块对象只在管线创建时需要，所以，不需要将它定义为一个成员变量，而将它作为一个局部变量定义在各个pass的setupPipelines函数中
    // 这里利用shader生成的.h文件创建shader module，shader_code变量即为.h文件内容
    RHIShader* VulkanRHI::createShaderModule(const std::vector<unsigned char>& shader_code)
    {
        RHIShader* shader = new VulkanShader();

        VkShaderModule vk_shader =  VulkanUtil::createShaderModule(m_device, shader_code);

        // VulkanShader主要的资产就是VkShaderModule
        ((VulkanShader*)shader)->setResource(vk_shader);

        return shader;
    }

    void VulkanRHI::createBuffer(RHIDeviceSize size, RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties, RHIBuffer* & buffer, RHIDeviceMemory* & buffer_memory)
    {
        VkBuffer vk_buffer;
        VkDeviceMemory vk_device_memory;
        
        VulkanUtil::createBuffer(m_physical_device, m_device, size, usage, properties, vk_buffer, vk_device_memory);

        buffer = new VulkanBuffer();
        buffer_memory = new VulkanDeviceMemory();
        ((VulkanBuffer*)buffer)->setResource(vk_buffer);
        ((VulkanDeviceMemory*)buffer_memory)->setResource(vk_device_memory);
    }

    void VulkanRHI::createBufferAndInitialize(RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties, RHIBuffer*& buffer, RHIDeviceMemory*& buffer_memory, RHIDeviceSize size, void* data, int datasize)
    {
        VkBuffer vk_buffer;
        VkDeviceMemory vk_device_memory;

        VulkanUtil::createBufferAndInitialize(m_device, m_physical_device, usage, properties, &vk_buffer, &vk_device_memory, size, data, datasize);

        buffer = new VulkanBuffer();
        buffer_memory = new VulkanDeviceMemory();
        ((VulkanBuffer*)buffer)->setResource(vk_buffer);
        ((VulkanDeviceMemory*)buffer_memory)->setResource(vk_device_memory);
    }

    bool VulkanRHI::createBufferVMA(VmaAllocator allocator, const RHIBufferCreateInfo* pBufferCreateInfo, const VmaAllocationCreateInfo* pAllocationCreateInfo, RHIBuffer* & pBuffer, VmaAllocation* pAllocation, VmaAllocationInfo* pAllocationInfo)
    {
        VkBuffer vk_buffer;
        VkBufferCreateInfo buffer_create_info{};
        buffer_create_info.sType = (VkStructureType)pBufferCreateInfo->sType;
        buffer_create_info.pNext = (const void*)pBufferCreateInfo->pNext;
        buffer_create_info.flags = (VkBufferCreateFlags)pBufferCreateInfo->flags;
        buffer_create_info.size = (VkDeviceSize)pBufferCreateInfo->size;
        buffer_create_info.usage = (VkBufferUsageFlags)pBufferCreateInfo->usage;
        buffer_create_info.sharingMode = (VkSharingMode)pBufferCreateInfo->sharingMode;
        buffer_create_info.queueFamilyIndexCount = pBufferCreateInfo->queueFamilyIndexCount;
        buffer_create_info.pQueueFamilyIndices = (const uint32_t*)pBufferCreateInfo->pQueueFamilyIndices;

        pBuffer = new VulkanBuffer();
        VkResult result = vmaCreateBuffer(allocator,
            &buffer_create_info,
            pAllocationCreateInfo,
            &vk_buffer,
            pAllocation,
            pAllocationInfo);

        ((VulkanBuffer*)pBuffer)->setResource(vk_buffer);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    bool VulkanRHI::createBufferWithAlignmentVMA(VmaAllocator allocator, const RHIBufferCreateInfo* pBufferCreateInfo, const VmaAllocationCreateInfo* pAllocationCreateInfo, RHIDeviceSize minAlignment, RHIBuffer* &pBuffer, VmaAllocation* pAllocation, VmaAllocationInfo* pAllocationInfo)
    {
        VkBuffer vk_buffer;
        VkBufferCreateInfo buffer_create_info{};
        buffer_create_info.sType = (VkStructureType)pBufferCreateInfo->sType;
        buffer_create_info.pNext = (const void*)pBufferCreateInfo->pNext;
        buffer_create_info.flags = (VkBufferCreateFlags)pBufferCreateInfo->flags;
        buffer_create_info.size = (VkDeviceSize)pBufferCreateInfo->size;
        buffer_create_info.usage = (VkBufferUsageFlags)pBufferCreateInfo->usage;
        buffer_create_info.sharingMode = (VkSharingMode)pBufferCreateInfo->sharingMode;
        buffer_create_info.queueFamilyIndexCount = pBufferCreateInfo->queueFamilyIndexCount;
        buffer_create_info.pQueueFamilyIndices = (const uint32_t*)pBufferCreateInfo->pQueueFamilyIndices;

        pBuffer = new VulkanBuffer();
        VkResult result = vmaCreateBufferWithAlignment(allocator,
            &buffer_create_info,
            pAllocationCreateInfo,
            minAlignment,
            &vk_buffer,
            pAllocation,
            pAllocationInfo);

        ((VulkanBuffer*)pBuffer)->setResource(vk_buffer);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("vmaCreateBufferWithAlignment failed!");
            return false;
        }
    }


    void VulkanRHI::copyBuffer(RHIBuffer* srcBuffer, RHIBuffer* dstBuffer, RHIDeviceSize srcOffset, RHIDeviceSize dstOffset, RHIDeviceSize size)
    {
        VkBuffer vk_src_buffer = ((VulkanBuffer*)srcBuffer)->getResource();
        VkBuffer vk_dst_buffer = ((VulkanBuffer*)dstBuffer)->getResource();
        VulkanUtil::copyBuffer(this, vk_src_buffer, vk_dst_buffer, srcOffset, dstOffset, size);
    }

    void VulkanRHI::createImage(uint32_t image_width, uint32_t image_height, RHIFormat format, RHIImageTiling image_tiling, RHIImageUsageFlags image_usage_flags, RHIMemoryPropertyFlags memory_property_flags,
        RHIImage* &image, RHIDeviceMemory* &memory, RHIImageCreateFlags image_create_flags, uint32_t array_layers, uint32_t miplevels)
    {
        VkImage vk_image;
        VkDeviceMemory vk_device_memory;
        VulkanUtil::createImage(
            m_physical_device,
            m_device,
            image_width,
            image_height,
            (VkFormat)format,
            (VkImageTiling)image_tiling,
            (VkImageUsageFlags)image_usage_flags,
            (VkMemoryPropertyFlags)memory_property_flags,
            vk_image,
            vk_device_memory,
            (VkImageCreateFlags)image_create_flags,
            array_layers,
            miplevels);

        image = new VulkanImage();
        memory = new VulkanDeviceMemory();
        ((VulkanImage*)image)->setResource(vk_image);
        ((VulkanDeviceMemory*)memory)->setResource(vk_device_memory);
    }

    void VulkanRHI::createImageView(RHIImage* image, RHIFormat format, RHIImageAspectFlags image_aspect_flags, RHIImageViewType view_type, uint32_t layout_count, uint32_t miplevels,
        RHIImageView* &image_view)
    {
        image_view = new VulkanImageView();
        VkImage vk_image = ((VulkanImage*)image)->getResource();
        VkImageView vk_image_view;
        vk_image_view = VulkanUtil::createImageView(m_device, vk_image, (VkFormat)format, image_aspect_flags, (VkImageViewType)view_type, layout_count, miplevels);
        ((VulkanImageView*)image_view)->setResource(vk_image_view);
    }

    void VulkanRHI::createGlobalImage(RHIImage* &image, RHIImageView* &image_view, VmaAllocation& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, void* texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels)
    {
        VkImage vk_image;
        VkImageView vk_image_view;
        
        VulkanUtil::createGlobalImage(this, vk_image, vk_image_view,image_allocation,texture_image_width,texture_image_height,texture_image_pixels,texture_image_format,miplevels);
        
        image = new VulkanImage();
        image_view = new VulkanImageView();
        ((VulkanImage*)image)->setResource(vk_image);
        ((VulkanImageView*)image_view)->setResource(vk_image_view);
    }

    void VulkanRHI::createCubeMap(RHIImage* &image, RHIImageView* &image_view, VmaAllocation& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, std::array<void*, 6> texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels)
    {
        VkImage vk_image;
        VkImageView vk_image_view;

        VulkanUtil::createCubeMap(this, vk_image, vk_image_view, image_allocation, texture_image_width, texture_image_height, texture_image_pixels, texture_image_format, miplevels);

        image = new VulkanImage();
        image_view = new VulkanImageView();
        ((VulkanImage*)image)->setResource(vk_image);
        ((VulkanImageView*)image_view)->setResource(vk_image_view);
    }

    // 使用任何VkImage对象，包括处于交换链中的，处于渲染管线中的，都需要我们创建一个VkImageView对象来绑定访问它。
    // 图像视图描述了访问图像的方式，以及图像的哪一部分可以被访问。
    // 比如，图像可以被图像视图描述为一个没有细化级别的二维深度纹理，进而可以在其上进行与二维深度纹理相关的操作。
    // 
    // 用于为交换链中的每一个图像建立图像视图
    void VulkanRHI::createSwapchainImageViews()
    {
        // 在createSwapChain()中m_swapchain_images的size得以确定，imageviews和images拥有相同的大小
        m_swapchain_imageviews.resize(m_swapchain_images.size());

        // create imageview (one for each this time) for all swapchain images
        for (size_t i = 0; i < m_swapchain_images.size(); i++)
        {
            VkImageView vk_image_view;
            vk_image_view = VulkanUtil::createImageView(m_device,
                                                                   m_swapchain_images[i],
                                                                   (VkFormat)m_swapchain_image_format,
                                                                   VK_IMAGE_ASPECT_COLOR_BIT,
                                                                   VK_IMAGE_VIEW_TYPE_2D,
                                                                   1,
                                                                   1);
            m_swapchain_imageviews[i] = new VulkanImageView();
            // ((VulkanImageView*)m_swapchain_imageviews[i])->setResource(vk_image_view);
            static_cast<VulkanImageView*>(m_swapchain_imageviews[i])->setResource(vk_image_view);
            // 没有虚函数你写个几把的动态cast
            // dynamic_cast<VulkanImageView*>(m_swapchain_imageviews[i])->setResource(vk_image_view); // 完全错误
        }
    }

    void VulkanRHI::createAssetAllocator()
    {
        VmaVulkanFunctions vulkanFunctions    = {};
        vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr   = &vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo allocatorCreateInfo = {};
        allocatorCreateInfo.vulkanApiVersion       = m_vulkan_api_version;
        allocatorCreateInfo.physicalDevice         = m_physical_device;
        allocatorCreateInfo.device                 = m_device;
        allocatorCreateInfo.instance               = m_instance;
        allocatorCreateInfo.pVulkanFunctions       = &vulkanFunctions;

        vmaCreateAllocator(&allocatorCreateInfo, &m_assets_allocator);
    }

    // todo : more descriptorSet
    bool VulkanRHI::allocateDescriptorSets(const RHIDescriptorSetAllocateInfo* pAllocateInfo, RHIDescriptorSet* &pDescriptorSets)
    {
        //descriptor_set_layout
        int descriptor_set_layout_size = pAllocateInfo->descriptorSetCount;
        std::vector<VkDescriptorSetLayout> vk_descriptor_set_layout_list(descriptor_set_layout_size);
        for (int i = 0; i < descriptor_set_layout_size; ++i)
        {
            const auto& rhi_descriptor_set_layout_element = pAllocateInfo->pSetLayouts[i];
            auto& vk_descriptor_set_layout_element = vk_descriptor_set_layout_list[i];

            vk_descriptor_set_layout_element = ((VulkanDescriptorSetLayout*)rhi_descriptor_set_layout_element)->getResource();

            VulkanDescriptorSetLayout* test = ((VulkanDescriptorSetLayout*)rhi_descriptor_set_layout_element);

            test = nullptr;
        };

        VkDescriptorSetAllocateInfo descriptorset_allocate_info{};
        descriptorset_allocate_info.sType = (VkStructureType)pAllocateInfo->sType;
        descriptorset_allocate_info.pNext = (const void*)pAllocateInfo->pNext;
        descriptorset_allocate_info.descriptorPool = ((VulkanDescriptorPool*)(pAllocateInfo->descriptorPool))->getResource();
        descriptorset_allocate_info.descriptorSetCount = pAllocateInfo->descriptorSetCount;
        descriptorset_allocate_info.pSetLayouts = vk_descriptor_set_layout_list.data();

        VkDescriptorSet vk_descriptor_set;
        pDescriptorSets = new VulkanDescriptorSet;
        VkResult result = vkAllocateDescriptorSets(m_device, &descriptorset_allocate_info, &vk_descriptor_set);
        ((VulkanDescriptorSet*)pDescriptorSets)->setResource(vk_descriptor_set);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("vkAllocateDescriptorSets failed!");
            return false;
        }
    }

    bool VulkanRHI::allocateCommandBuffers(const RHICommandBufferAllocateInfo* pAllocateInfo, RHICommandBuffer* &pCommandBuffers)
    {
        VkCommandBufferAllocateInfo command_buffer_allocate_info{};
        command_buffer_allocate_info.sType = (VkStructureType)pAllocateInfo->sType;
        command_buffer_allocate_info.pNext = (const void*)pAllocateInfo->pNext;
        command_buffer_allocate_info.commandPool = ((VulkanCommandPool*)(pAllocateInfo->commandPool))->getResource();
        command_buffer_allocate_info.level = (VkCommandBufferLevel)pAllocateInfo->level;
        command_buffer_allocate_info.commandBufferCount = pAllocateInfo->commandBufferCount;

        VkCommandBuffer vk_command_buffer;
        pCommandBuffers = new RHICommandBuffer();
        VkResult result = vkAllocateCommandBuffers(m_device, &command_buffer_allocate_info, &vk_command_buffer);
        ((VulkanCommandBuffer*)pCommandBuffers)->setResource(vk_command_buffer);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("vkAllocateCommandBuffers failed!");
            return false;
        }
    }

    void VulkanRHI::createSwapchain()
    {
        // query all supports of this physical device
        SwapChainSupportDetails swapchain_support_details = querySwapChainSupport(m_physical_device);

        // choose the best or fitting format
        VkSurfaceFormatKHR chosen_surface_format =
            chooseSwapchainSurfaceFormatFromDetails(swapchain_support_details.formats);
        // choose the best or fitting present mode
        VkPresentModeKHR chosen_presentMode =
            chooseSwapchainPresentModeFromDetails(swapchain_support_details.presentModes);
        // choose the best or fitting extent
        VkExtent2D chosen_extent = chooseSwapchainExtentFromDetails(swapchain_support_details.capabilities);

        // 使用交换链支持的最小图像个数+1数量的图像来实现三重缓冲
        // 三重缓冲：https://zhuanlan.zhihu.com/p/599432460
        uint32_t image_count = swapchain_support_details.capabilities.minImageCount + 1;
        // maxImageCount的值为0表明，只要内存可以满足，我们可以使用任意数量的图像
        if (swapchain_support_details.capabilities.maxImageCount > 0 &&
            image_count > swapchain_support_details.capabilities.maxImageCount)
        {
            image_count = swapchain_support_details.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo {};
        createInfo.sType   = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_surface;

        createInfo.minImageCount    = image_count;
        createInfo.imageFormat      = chosen_surface_format.format;
        createInfo.imageColorSpace  = chosen_surface_format.colorSpace;
        createInfo.imageExtent      = chosen_extent;
        // imageArrayLayers成员变量用于指定每个图像所包含的层次，通常值为1。但对于VR相关的应用程序来说，会使用更多的层次
        createInfo.imageArrayLayers = 1;
        // imageUsage成员变量用于指定我们将在图像上进行怎样的操作。我们在图像上进行绘制操作，也就是将图像作为一个color attatchment来使用
        // 如果需要对图像进行后期处理之类的操作，可以使用VK_IMAGE_USAGE_TRANSFER_DST_BIT作为imageUsage成员变量的值，让交换链图像可以作为传输的目的图像
        createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t queueFamilyIndices[] = {m_queue_indices.graphics_family.value(), m_queue_indices.present_family.value()};

        // 通过图形队列在交换链图像上进行绘制操作，然后将图像提交给呈现队列来显示，有两种控制在多个队列访问图像的方式
        if (m_queue_indices.graphics_family != m_queue_indices.present_family)
        {
            // VK_SHARING_MODE_CONCURRENT：图像可以在多个队列族间使用，不需要显式地改变图像所有权
            // 如果图形和呈现不是同一个队列族，我们使用concurrent模式来避免处理图像所有权问题。
            // concurrent模式需要我们使用queueFamilyIndexCount和pQueueFamilyIndices来指定共享所有权的队列族。
            createInfo.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices   = queueFamilyIndices;
        }
        else
        {
            // VK_SHARING_MODE_EXCLUSIVE：一张图像同一时间只能被一个队列族所拥有，在另一队列族使用它之前，必须显式地改变图像所有权。这一模式下性能表现最佳
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        // preTransform指定是否为交换链中的图像指定一个固定的变换操作，比如顺时针旋转90度或是水平翻转。如果不需要进行任何变换操作，指定使用currentTransform变换即可
        createInfo.preTransform   = swapchain_support_details.capabilities.currentTransform;
        // compositeAlpha成员变量用于指定alpha通道是否被用来和窗口系统中的其它窗口进行混合操作
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // 设置忽略alpha通道
        createInfo.presentMode    = chosen_presentMode;
        // clipped成员变量被设置为VK_TRUE表示我们不关心被窗口系统中的其它窗口遮挡的像素的颜色，
        // 这允许Vulkan采取一定的优化措施，但如果我们回读窗口的像素值就可能出现问题
        createInfo.clipped        = VK_TRUE;

        createInfo.oldSwapchain = VK_NULL_HANDLE;

        // 调用vkCreateSwapchainKHR函数创建交换链
        if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS)
        {
            LOG_ERROR("vk create swapchain khr");
        }

        // 获取交换链图像的图像句柄，之后利用这些图像句柄进行渲染操作
        vkGetSwapchainImagesKHR(m_device, m_swapchain, &image_count, nullptr);
        m_swapchain_images.resize(image_count);
        vkGetSwapchainImagesKHR(m_device, m_swapchain, &image_count, m_swapchain_images.data());

        m_swapchain_image_format = (RHIFormat)chosen_surface_format.format;
        m_swapchain_extent.height = chosen_extent.height;
        m_swapchain_extent.width = chosen_extent.width;

        m_scissor = {{0, 0}, {m_swapchain_extent.width, m_swapchain_extent.height}};
    }

    void VulkanRHI::clearSwapchain()
    {
        for (auto imageview : m_swapchain_imageviews)
        {
            vkDestroyImageView(m_device, ((VulkanImageView*)imageview)->getResource(), NULL);
        }
        vkDestroySwapchainKHR(m_device, m_swapchain, NULL); // also swapchain images
    }

    void VulkanRHI::destroyDefaultSampler(RHIDefaultSamplerType type)
    {
        switch (type)
        {
        case Piccolo::Default_Sampler_Linear:
            VulkanUtil::destroyLinearSampler(m_device);
            delete(m_linear_sampler);
            break;
        case Piccolo::Default_Sampler_Nearest:
            VulkanUtil::destroyNearestSampler(m_device);
            delete(m_nearest_sampler);
            break;
        default:
            break;
        }
    }

    void VulkanRHI::destroyMipmappedSampler()
    {
        VulkanUtil::destroyMipmappedSampler(m_device);

        for (auto sampler : m_mipmap_sampler_map)
        {
            delete sampler.second;
        }
        m_mipmap_sampler_map.clear();
    }

    void VulkanRHI::destroyShaderModule(RHIShader* shaderModule)
    {
        vkDestroyShaderModule(m_device, ((VulkanShader*)shaderModule)->getResource(), nullptr);

        delete(shaderModule);
    }

    void VulkanRHI::destroySemaphore(RHISemaphore* semaphore)
    {
        vkDestroySemaphore(m_device, ((VulkanSemaphore*)semaphore)->getResource(), nullptr);
    }

    void VulkanRHI::destroySampler(RHISampler* sampler)
    {
        vkDestroySampler(m_device, ((VulkanSampler*)sampler)->getResource(), nullptr);
    }

    void VulkanRHI::destroyInstance(RHIInstance* instance)
    {
        vkDestroyInstance(((VulkanInstance*)instance)->getResource(), nullptr);
    }

    void VulkanRHI::destroyImageView(RHIImageView* imageView)
    {
        vkDestroyImageView(m_device, ((VulkanImageView*)imageView)->getResource(), nullptr);
    }

    void VulkanRHI::destroyImage(RHIImage* image)
    {
        vkDestroyImage(m_device, ((VulkanImage*)image)->getResource(), nullptr);
    }

    void VulkanRHI::destroyFramebuffer(RHIFramebuffer* framebuffer)
    {
        vkDestroyFramebuffer(m_device, ((VulkanFramebuffer*)framebuffer)->getResource(), nullptr);
    }

    void VulkanRHI::destroyFence(RHIFence* fence)
    {
        vkDestroyFence(m_device, ((VulkanFence*)fence)->getResource(), nullptr);
    }

    void VulkanRHI::destroyDevice()
    {
        vkDestroyDevice(m_device, nullptr);
    }

    void VulkanRHI::destroyCommandPool(RHICommandPool* commandPool)
    {
        vkDestroyCommandPool(m_device, ((VulkanCommandPool*)commandPool)->getResource(), nullptr);
    }

    void VulkanRHI::destroyBuffer(RHIBuffer* &buffer)
    {
        vkDestroyBuffer(m_device, ((VulkanBuffer*)buffer)->getResource(), nullptr);
        RHI_DELETE_PTR(buffer);
    }

    void VulkanRHI::freeCommandBuffers(RHICommandPool* commandPool, uint32_t commandBufferCount, RHICommandBuffer* pCommandBuffers)
    {
        VkCommandBuffer vk_command_buffer = ((VulkanCommandBuffer*)pCommandBuffers)->getResource();
        vkFreeCommandBuffers(m_device, ((VulkanCommandPool*)commandPool)->getResource(), commandBufferCount, &vk_command_buffer);
    }

    void VulkanRHI::freeMemory(RHIDeviceMemory* &memory)
    {
        vkFreeMemory(m_device, ((VulkanDeviceMemory*)memory)->getResource(), nullptr);
        RHI_DELETE_PTR(memory);
    }

    bool VulkanRHI::mapMemory(RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size, RHIMemoryMapFlags flags, void** ppData)
    {
        VkResult result = vkMapMemory(m_device, ((VulkanDeviceMemory*)memory)->getResource(), offset, size, (VkMemoryMapFlags)flags, ppData);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("vkMapMemory failed!");
            return false;
        }
    }

    void VulkanRHI::unmapMemory(RHIDeviceMemory* memory)
    {
        vkUnmapMemory(m_device, ((VulkanDeviceMemory*)memory)->getResource());
    }

    void VulkanRHI::invalidateMappedMemoryRanges(void* pNext, RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size)
    {
        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = ((VulkanDeviceMemory*)memory)->getResource();
        mappedRange.offset = offset;
        mappedRange.size = size;
        vkInvalidateMappedMemoryRanges(m_device, 1, &mappedRange);
    }

    void VulkanRHI::flushMappedMemoryRanges(void* pNext, RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size)
    {
        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = ((VulkanDeviceMemory*)memory)->getResource();
        mappedRange.offset = offset;
        mappedRange.size = size;
        vkFlushMappedMemoryRanges(m_device, 1, &mappedRange);
    }

    RHISemaphore* &VulkanRHI::getTextureCopySemaphore(uint32_t index)
    {
        return m_image_available_for_texturescopy_semaphores[index];
    }

    // 窗口大小改变会导致交换链和窗口不再适配，需要重新对交换链进行处理
    void VulkanRHI::recreateSwapchain()
    {
        int width  = 0;
        int height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        // 窗口最小化
        while (width == 0 || height == 0) // minimized 0,0, pause for now
        {
            glfwGetFramebufferSize(m_window, &width, &height);
            glfwWaitEvents();
        }

        VkResult res_wait_for_fences =
            _vkWaitForFences(m_device, k_max_frames_in_flight, m_is_frame_in_flight_fences, VK_TRUE, UINT64_MAX);
        if (VK_SUCCESS != res_wait_for_fences)
        {
            LOG_ERROR("_vkWaitForFences failed");
            return;
        }

        destroyImageView(m_depth_image_view);
        vkDestroyImage(m_device, ((VulkanImage*)m_depth_image)->getResource(), NULL);
        vkFreeMemory(m_device, m_depth_image_memory, NULL);

        for (auto imageview : m_swapchain_imageviews)
        {
            vkDestroyImageView(m_device, ((VulkanImageView*)imageview)->getResource(), NULL);
        }
        vkDestroySwapchainKHR(m_device, m_swapchain, NULL);

        createSwapchain();
        createSwapchainImageViews();
        createFramebufferImageAndView();
    }

    VkResult VulkanRHI::createDebugUtilsMessengerEXT(VkInstance                                instance,
                                                     const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                                     const VkAllocationCallbacks*              pAllocator,
                                                     VkDebugUtilsMessengerEXT*                 pDebugMessenger)
    {
        // 由于vkCreateDebugUtilsMessengerEXT函数是一个扩展函数，不会被Vulkan库自动加载，所以需要我们自己使用vkGetInstanceProcAddr函数来加载它
        auto func =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr)
        {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        }
        else
        {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    void VulkanRHI::destroyDebugUtilsMessengerEXT(VkInstance                   instance,
                                                  VkDebugUtilsMessengerEXT     debugMessenger,
                                                  const VkAllocationCallbacks* pAllocator)
    {
        auto func =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr)
        {
            func(instance, debugMessenger, pAllocator);
        }
    }

    // Q: What is actually a Queue family in Vulkan?
    // https://stackoverflow.com/questions/55272626/what-is-actually-a-queue-family-in-vulkan
    // Vulkan的几乎所有操作，从绘制到加载纹理都需要将操作指令提交给一个队列，然后才能执行
    // Vulkan有多种不同类型的队列，它们属于不同的队列族，每个队列族的队列只允许执行特定的一部分指令
    // 比如，可能存在只允许执行计算相关指令的队列族和只允许执行内存传输相关指令的队列族
    // 我们需要检测设备支持的队列族，以及它们中哪些支持我们需要使用的指令
    // 为了完成这一目的，我们添加了一个叫做findQueueFamilies的函数，这一函数会查找出满足我们需求的队列族
    Piccolo::QueueFamilyIndices VulkanRHI::findQueueFamilies(VkPhysicalDevice physicalm_device) // for device and surface
    {
        QueueFamilyIndices indices;
        uint32_t           queue_family_count = 0;
        // 使用vkGetPhysicalDeviceQueueFamilyProperties()来获取一个设备所支持的queue families和queues的细节
        // vkGetPhysicalDeviceQueueFamilyProperties()所返回的pQueueFamilyProperties数组中的每个索引，都描述了该物理设备上的一个唯一的queue family。
        // 这些索引将在创建queues时使用，并且它们与通过VkDeviceQueueCreateInfo传递给vkCreateDevice()的queueFamilyIndex直接对应
        vkGetPhysicalDeviceQueueFamilyProperties(physicalm_device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalm_device, &queue_family_count, queue_families.data());

        int i = 0;
        for (const auto& queue_family : queue_families)
        {
            if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) // if support graphics command queue
            {
                indices.graphics_family = i;
            }

            if (queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT) // if support compute command queue
            {
                indices.m_compute_family = i;
            }


            VkBool32 is_present_support = false;
            // 查询是否支持窗口表面
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalm_device,
                                                 i,
                                                 m_surface,
                                                 &is_present_support); // if support surface presentation
            // 根据队列族中的队列数量和是否支持表现确定使用的表现队列族的索引
            if (is_present_support)
            {
                indices.present_family = i;
            }

            if (indices.isComplete())
            {
                break;
            }
            i++;
        }
        return indices;
        // 按照上面的方法最后选择使用的绘制指令队列族和表现队列族很有可能是同一个队列族
        // 但为了统一操作，即使两者是同一个队列族，我们也按照它们是不同的队列族来对待
    }

    // 在这里，我们将所需的扩展保存在一个集合中，然后枚举所有可用的扩展，将集合中的扩展剔除
    // 最后，如果这个集合中的元素为0，说明我们所需的扩展全部都被满足
    bool VulkanRHI::checkDeviceExtensionSupport(VkPhysicalDevice physicalm_device)
    {
        uint32_t extension_count;
        vkEnumerateDeviceExtensionProperties(physicalm_device, nullptr, &extension_count, nullptr);

        std::vector<VkExtensionProperties> available_extensions(extension_count);
        vkEnumerateDeviceExtensionProperties(physicalm_device, nullptr, &extension_count, available_extensions.data());

        // 注意m_device_extensions只包含了VK_KHR_SWAPCHAIN_EXTENSION_NAME，即用于检测swapchain是否支持
        std::set<std::string> required_extensions(m_device_extensions.begin(), m_device_extensions.end());
        for (const auto& extension : available_extensions)
        {
            required_extensions.erase(extension.extensionName);
        }

        return required_extensions.empty();
    }

    // 检测设备是否suitable
    bool VulkanRHI::isDeviceSuitable(VkPhysicalDevice physicalm_device)
    {
        auto queue_indices           = findQueueFamilies(physicalm_device);
        bool is_extensions_supported = checkDeviceExtensionSupport(physicalm_device);
        bool is_swapchain_adequate   = false;
        if (is_extensions_supported)
        {
            SwapChainSupportDetails swapchain_support_details = querySwapChainSupport(physicalm_device);
            is_swapchain_adequate =
                !swapchain_support_details.formats.empty() && !swapchain_support_details.presentModes.empty();
        }

        VkPhysicalDeviceFeatures physicalm_device_features;
        vkGetPhysicalDeviceFeatures(physicalm_device, &physicalm_device_features);

        if (!queue_indices.isComplete() || !is_swapchain_adequate || !physicalm_device_features.samplerAnisotropy)
        {
            return false;
        }

        return true;
    }
    
    // 查询交换链支持细节
    // 
    // 有三种最基本的属性，需要我们检查：
    // 基础表面特性(交换链的最小 / 最大图像数量，最小 / 最大图像宽度、高度)
    // 表面格式(像素格式，颜色空间)
    // 可用的呈现模式
    Piccolo::SwapChainSupportDetails VulkanRHI::querySwapChainSupport(VkPhysicalDevice physicalm_device)
    {
        SwapChainSupportDetails details_result;

        // capabilities 基础表面特性
        // 这一函数以VkPhysicalDevice对象和VkSurfaceKHR作为参数来查询表面特性。与交换链信息查询有关的函数都需要这两个参数，它们是交换链的核心组件。
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalm_device, m_surface, &details_result.capabilities);

        // formats
        // 查询表面支持的格式。这一查询结果是一个结构体列表，所以它的查询首先查询格式数量，然后分配数组空间查询具体信息
        uint32_t format_count;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalm_device, m_surface, &format_count, nullptr);
        if (format_count != 0)
        {
            details_result.formats.resize(format_count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(
                physicalm_device, m_surface, &format_count, details_result.formats.data());
        }

        // present modes 呈现方式
        uint32_t presentmode_count;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalm_device, m_surface, &presentmode_count, nullptr);
        if (presentmode_count != 0)
        {
            details_result.presentModes.resize(presentmode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(
                physicalm_device, m_surface, &presentmode_count, details_result.presentModes.data());
        }

        return details_result;
    }

    VkFormat VulkanRHI::findDepthFormat()
    {
        return findSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                                   VK_IMAGE_TILING_OPTIMAL,
                                   VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

    VkFormat VulkanRHI::findSupportedFormat(const std::vector<VkFormat>& candidates,
                                            VkImageTiling                tiling,
                                            VkFormatFeatureFlags         features)
    {
        for (VkFormat format : candidates)
        {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(m_physical_device, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
            {
                return format;
            }
            else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
            {
                return format;
            }
        }

        LOG_ERROR("findSupportedFormat failed");
        return VkFormat();
    }

    // 用于选择合适的表面格式
    VkSurfaceFormatKHR
    VulkanRHI::chooseSwapchainSurfaceFormatFromDetails(const std::vector<VkSurfaceFormatKHR>& available_surface_formats)
    {
        for (const auto& surface_format : available_surface_formats)
        {
            // TODO: select the VK_FORMAT_B8G8R8A8_SRGB surface format,
            // there is no need to do gamma correction in the fragment shader
            if (surface_format.format == VK_FORMAT_B8G8R8A8_UNORM &&
                surface_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return surface_format;
            }
        }
        return available_surface_formats[0];
    }

    // 呈现模式可以说是交换链中最重要的设置。它决定了什么条件下图像才会显示到屏幕。Vulkan提供了四种可用的呈现模式
    VkPresentModeKHR
    VulkanRHI::chooseSwapchainPresentModeFromDetails(const std::vector<VkPresentModeKHR>& available_present_modes)
    {
        for (VkPresentModeKHR present_mode : available_present_modes)
        {
            if (VK_PRESENT_MODE_MAILBOX_KHR == present_mode)
            {
                // 这一模式是VK_PRESENT_MODE_FIFO_KHR的变种。它不会在交换链的队列满时阻塞应用程序，
                // 队列中的图像会被直接替换为应用程序新提交的图像。这一模式可以用来实现三倍缓冲，避免撕裂现象的同时减小了延迟问题。
                return VK_PRESENT_MODE_MAILBOX_KHR;
            }
        }

        // 交换链变成一个先进先出的队列，每次从队列头部取出一张图像进行显示，应用程序渲染的图像提交给交换链后，
        // 会被放在队列尾部。当队列为满时，应用程序需要进行等待。这一模式非常类似现在常用的垂直同步。刷新显示的时刻也被叫做垂直回扫。
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    // 交换范围是交换链中图像的分辨率，它几乎总是和我们要显示图像的窗口的分辨率相同。VkSurfaceCapabilitiesKHR结构体定义了可用的分辨率范围
    VkExtent2D VulkanRHI::chooseSwapchainExtentFromDetails(const VkSurfaceCapabilitiesKHR& capabilities)
    {
        if (capabilities.currentExtent.width != UINT32_MAX)
        {
            return capabilities.currentExtent;
        }
        else
        {
            int width, height;
            glfwGetFramebufferSize(m_window, &width, &height);

            VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

            actualExtent.width =
                std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height =
                std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    void VulkanRHI::pushEvent(RHICommandBuffer* commond_buffer, const char* name, const float* color)
    {
        if (m_enable_debug_utils_label)
        {
            VkDebugUtilsLabelEXT label_info;
            label_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            label_info.pNext = nullptr;
            label_info.pLabelName = name;
            for (int i = 0; i < 4; ++i)
                label_info.color[i] = color[i];
            _vkCmdBeginDebugUtilsLabelEXT(((VulkanCommandBuffer*)commond_buffer)->getResource(), &label_info);
        }
    }

    void VulkanRHI::popEvent(RHICommandBuffer* commond_buffer)
    {
        if (m_enable_debug_utils_label)
        {
            _vkCmdEndDebugUtilsLabelEXT(((VulkanCommandBuffer*)commond_buffer)->getResource());
        }
    }
    bool VulkanRHI::isPointLightShadowEnabled(){ return m_enable_point_light_shadow; }

    RHICommandBuffer* VulkanRHI::getCurrentCommandBuffer() const
    {
        return m_current_command_buffer;
    }
    RHICommandBuffer* const* VulkanRHI::getCommandBufferList() const
    {
        return m_command_buffers;
    }
    RHICommandPool* VulkanRHI::getCommandPoor() const
    {
        return m_rhi_command_pool;
    }
    RHIDescriptorPool* VulkanRHI::getDescriptorPoor() const
    {
        return m_descriptor_pool;
    }
    RHIFence* const* VulkanRHI::getFenceList() const
    {
        return m_rhi_is_frame_in_flight_fences;
    }
    QueueFamilyIndices VulkanRHI::getQueueFamilyIndices() const
    {
        return m_queue_indices;
    }
    RHIQueue* VulkanRHI::getGraphicsQueue() const
    {
        return m_graphics_queue;
    }
    RHIQueue* VulkanRHI::getComputeQueue() const
    {
        return m_compute_queue;
    }
    RHISwapChainDesc VulkanRHI::getSwapchainInfo()
    {
        RHISwapChainDesc desc;
        desc.image_format = m_swapchain_image_format;
        desc.extent = m_swapchain_extent;
        desc.viewport = &m_viewport;
        desc.scissor = &m_scissor;
        desc.imageViews = m_swapchain_imageviews;
        return desc;
    }
    RHIDepthImageDesc VulkanRHI::getDepthImageInfo() const
    {
        RHIDepthImageDesc desc;
        desc.depth_image_format = m_depth_image_format;
        desc.depth_image_view = m_depth_image_view;
        desc.depth_image = m_depth_image;
        return desc;
    }
    uint8_t VulkanRHI::getMaxFramesInFlight() const
    {
        return k_max_frames_in_flight;
    }
    uint8_t VulkanRHI::getCurrentFrameIndex() const
    {
        return m_current_frame_index;
    }
    void VulkanRHI::setCurrentFrameIndex(uint8_t index)
    {
        m_current_frame_index = index;
    }

} // namespace Piccolo