#pragma once
//
// GPU block compression for the texture compiler: AMD's Compressonator CMP_Core HLSL kernels (SPIR-V, gpu_kernels/) on a minimal compute
// host. Vulkan is loaded at run time (vulkan-1.dll / libvulkan.so.1), never linked: a machine without a Vulkan driver, or with
// XTEXTURE_NO_GPU set, gets Init() == false and the compiler encodes on the CPU as before.
// BC1, BC3, BC4, BC5 (one pass) and BC7 (AMD's seven passes) from 8-bit RGBA; any size (padded to whole blocks by repeating the edge).
// Measured (texture_pipeline_plan, 2026-10-31): BC7 4096^2 13 s on 28 CPU threads -> 0.25 s on an RTX 4070 Ti SUPER, -0.2 to -1 dB PSNR.
//
#if __has_include(<vulkan/vulkan.h>)
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef NOGDI
    #define NOGDI           // (wingdi.h's ERROR macro breaks msg_type::ERROR)
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "gpu_kernels/xtexture_gpu_bc1.h"
#include "gpu_kernels/xtexture_gpu_bc3.h"
#include "gpu_kernels/xtexture_gpu_bc4.h"
#include "gpu_kernels/xtexture_gpu_bc5.h"
#include "gpu_kernels/xtexture_gpu_bc7.h"

namespace xtexture_gpu
{
    enum class format { BC1, BC3, BC4, BC5, BC7 };

    // The Vulkan functions used, loaded at run time
    #define XTEXTURE_GPU_VK_INSTANCE(X) \
        X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceQueueFamilyProperties) \
        X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr)
    #define XTEXTURE_GPU_VK_DEVICE(X) \
        X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkCreateBuffer) X(vkDestroyBuffer) \
        X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkAllocateCommandBuffers) \
        X(vkFreeCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkQueueSubmit) X(vkQueueWaitIdle) X(vkCreateShaderModule) \
        X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
        X(vkCreateComputePipelines) X(vkDestroyPipeline) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) \
        X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkResetDescriptorPool) \
        X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) X(vkCmdBindPipeline) \
        X(vkCmdBindDescriptorSets) X(vkCmdDispatch) X(vkCmdCopyBuffer)

    class encoder
    {
    public:

        ~encoder() noexcept { Release(); }

        // A device and the kernels' pipelines; false when there is no usable GPU (the compiler then stays on the CPU)
        bool Init() noexcept
        {
            if (std::getenv("XTEXTURE_NO_GPU")) return false;
            Mark();
        #if defined(_WIN32)
            m_hLib = LoadLibraryA("vulkan-1.dll");
            if (!m_hLib) return false;
            auto GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(static_cast<HMODULE>(m_hLib), "vkGetInstanceProcAddr"));
        #else
            m_hLib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
            if (!m_hLib) return false;
            auto GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(m_hLib, "vkGetInstanceProcAddr"));
        #endif
            if (!GetInstanceProcAddr) return false;
            auto CreateInstance = reinterpret_cast<PFN_vkCreateInstance>(GetInstanceProcAddr(nullptr, "vkCreateInstance"));
            if (!CreateInstance) return false;

            VkApplicationInfo App{ VK_STRUCTURE_TYPE_APPLICATION_INFO }; App.apiVersion = VK_API_VERSION_1_1; App.pApplicationName = "xtexture_compiler";
            VkInstanceCreateInfo Ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO }; Ici.pApplicationInfo = &App;
            Lap(m_tLoader);
            if (CreateInstance(&Ici, nullptr, &m_Instance) != VK_SUCCESS) return false;
            Lap(m_tInstance);
            #define XTEXTURE_GPU_LOAD_I(F) F = reinterpret_cast<PFN_##F>(GetInstanceProcAddr(m_Instance, #F)); if (!F) return false;
            XTEXTURE_GPU_VK_INSTANCE(XTEXTURE_GPU_LOAD_I)

            uint32_t n = 0; vkEnumeratePhysicalDevices(m_Instance, &n, nullptr);
            if (n == 0) return false;
            std::vector<VkPhysicalDevice> All(n); vkEnumeratePhysicalDevices(m_Instance, &n, All.data());
            for (auto P : All)          // a discrete GPU first, else whatever there is (integrated GPUs still beat a small CPU)
            {
                VkPhysicalDeviceProperties Pr; vkGetPhysicalDeviceProperties(P, &Pr);
                if (Pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) continue;
                if (!m_Phys || Pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { m_Phys = P; std::memcpy(m_Name, Pr.deviceName, sizeof(m_Name) - 1); }
            }
            if (!m_Phys) return false;

            uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(m_Phys, &nq, nullptr);
            std::vector<VkQueueFamilyProperties> Q(nq); vkGetPhysicalDeviceQueueFamilyProperties(m_Phys, &nq, Q.data());
            for (uint32_t i = 0; i < nq && m_Family == ~0u; ++i) if (Q[i].queueFlags & VK_QUEUE_COMPUTE_BIT) m_Family = i;
            if (m_Family == ~0u) return false;

            float Prio = 1; VkDeviceQueueCreateInfo Qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO }; Qci.queueFamilyIndex = m_Family; Qci.queueCount = 1; Qci.pQueuePriorities = &Prio;
            VkDeviceCreateInfo Dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO }; Dci.queueCreateInfoCount = 1; Dci.pQueueCreateInfos = &Qci;
            if (vkCreateDevice(m_Phys, &Dci, nullptr, &m_Device) != VK_SUCCESS) return false;
            #define XTEXTURE_GPU_LOAD_D(F) F = reinterpret_cast<PFN_##F>(vkGetDeviceProcAddr(m_Device, #F)); if (!F) return false;
            XTEXTURE_GPU_VK_DEVICE(XTEXTURE_GPU_LOAD_D)
            vkGetDeviceQueue(m_Device, m_Family, 0, &m_Queue);
            Lap(m_tDevice);

            VkCommandPoolCreateInfo Pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO }; Pci.queueFamilyIndex = m_Family; Pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            if (vkCreateCommandPool(m_Device, &Pci, nullptr, &m_Pool) != VK_SUCCESS) return false;

            // the kernels' bindings (gpu_kernels/generate_gpu_kernels.py): b0 cbuffer = 0, t0 image = 10, t1 = 11, u0 = 20
            VkDescriptorSetLayoutBinding Bind[4] =
            { { 0,  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
            , { 10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
            , { 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
            , { 20, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr } };
            VkDescriptorSetLayoutCreateInfo Dl{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO }; Dl.bindingCount = 4; Dl.pBindings = Bind;
            if (vkCreateDescriptorSetLayout(m_Device, &Dl, nullptr, &m_Dsl) != VK_SUCCESS) return false;
            VkPipelineLayoutCreateInfo Pl{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO }; Pl.setLayoutCount = 1; Pl.pSetLayouts = &m_Dsl;
            if (vkCreatePipelineLayout(m_Device, &Pl, nullptr, &m_Layout) != VK_SUCCESS) return false;

            VkDescriptorPoolSize Ps[3] = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 8 }, { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16 } };
            VkDescriptorPoolCreateInfo Dp{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO }; Dp.maxSets = 8; Dp.poolSizeCount = 3; Dp.pPoolSizes = Ps;
            if (vkCreateDescriptorPool(m_Device, &Dp, nullptr, &m_DPool) != VK_SUCCESS) return false;

            m_bReady = true;
            return true;
        }

        const char* getDeviceName() const noexcept { return m_Name; }

        // Where the start-up went (ms): loader, instance, device, pipelines (the formats made so far)
        void getStartupTimes(char* p, std::size_t n) const noexcept { std::snprintf(p, n, "loader %.0f ms, instance %.0f ms, device %.0f ms, pipelines %.0f ms", m_tLoader, m_tInstance, m_tDevice, m_tPipelines); }

        // Encodes one RGBA8 image (Width x Height, rows of Width * 4 bytes) to blocks: ceil(W/4) x ceil(H/4) blocks of 8 (BC1, BC4) or 16 bytes.
        // false on any failure (the caller then encodes this one on the CPU)
        bool Encode(format Fmt, const std::uint8_t* pRGBA, int Width, int Height, float Quality, std::vector<std::uint8_t>& Blocks) noexcept
        {
            if (!m_bReady || Width <= 0 || Height <= 0) return false;
            const int      W4 = (Width + 3) & ~3, H4 = (Height + 3) & ~3, bx = W4 / 4, by = H4 / 4;
            const uint32_t nBlocks = uint32_t(bx) * uint32_t(by);
            const uint32_t BlockSize = (Fmt == format::BC1 || Fmt == format::BC4) ? 8 : 16;
            const uint32_t DXGI = Fmt == format::BC1 ? 71 : Fmt == format::BC3 ? 77 : Fmt == format::BC4 ? 80 : Fmt == format::BC5 ? 83 : 98;

            if (!Pipelines(Fmt)) return false;
            if (!Buffers(W4, H4, nBlocks, BlockSize, Fmt == format::BC7)) return false;

            // the image, padded to whole blocks by repeating the last column / row
            {
                auto* p = static_cast<std::uint8_t*>(m_Staging.p);
                for (int y = 0; y < H4; ++y)
                {
                    const std::uint8_t* Row = pRGBA + size_t(std::min(y, Height - 1)) * Width * 4;
                    std::memcpy(p + size_t(y) * W4 * 4, Row, size_t(Width) * 4);
                    for (int x = Width; x < W4; ++x) std::memcpy(p + (size_t(y) * W4 + x) * 4, Row + size_t(Width - 1) * 4, 4);
                }
            }

            // the passes (AMD's DirectX host order, compute_directx.cpp): pipeline, mode_id, blocks per group, in (t1), out (u0)
            struct pass { VkPipeline P; uint32_t Mode, PerGroup; buffer* pIn; buffer* pOut; };
            pass Passes[7]; int nPasses = 0;
            const auto& K = m_Kernels[int(Fmt)];
            if (Fmt != format::BC7) Passes[nPasses++] = { K[0], 0, 4, &m_Dummy, &m_Out };
            else
            {
                Passes[nPasses++] = { K[0], 6, 4, &m_Dummy, &m_Err0 };
                Passes[nPasses++] = { K[1], 1, 1, &m_Err0, &m_Err1 }; Passes[nPasses++] = { K[1], 3, 1, &m_Err1, &m_Err0 }; Passes[nPasses++] = { K[1], 7, 1, &m_Err0, &m_Err1 };
                Passes[nPasses++] = { K[2], 0, 1, &m_Err1, &m_Err0 }; Passes[nPasses++] = { K[2], 2, 1, &m_Err0, &m_Err1 };
                Passes[nPasses++] = { K[3], 0, 4, &m_Err1, &m_Out };
            }

            // dispatches of at most 65535 groups, each its own cbuffer slot (the kernels' cbCS, std140, 32 bytes) at a dynamic offset
            struct options { uint32_t tex_width, num_block_x, format, mode_id, start_block_id, num_total_blocks; float alpha_weight, quality; };
            struct dispatch { int Pass; uint32_t Groups, Slot; };
            std::vector<dispatch> Dispatches; std::vector<options> Slots;
            for (int k = 0; k < nPasses; ++k)
                for (uint32_t First = 0; First < nBlocks; First += 65535u * Passes[k].PerGroup)
                {
                    const uint32_t Blocks = std::min(65535u * Passes[k].PerGroup, nBlocks - First);
                    Dispatches.push_back({ k, (Blocks + Passes[k].PerGroup - 1) / Passes[k].PerGroup, uint32_t(Slots.size()) });
                    Slots.push_back({ uint32_t(W4), uint32_t(bx), DXGI, Passes[k].Mode, First, nBlocks, 1.0f, Quality });
                }
            if (!Ensure(m_Cb, VkDeviceSize(Slots.size()) * 256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true)) return false;
            for (size_t c = 0; c < Slots.size(); ++c) std::memcpy(static_cast<char*>(m_Cb.p) + c * 256, &Slots[c], sizeof(options));

            // one descriptor set per pass
            vkResetDescriptorPool(m_Device, m_DPool, 0);
            VkDescriptorSet Sets[7];
            for (int k = 0; k < nPasses; ++k)
            {
                VkDescriptorSetAllocateInfo Da{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO }; Da.descriptorPool = m_DPool; Da.descriptorSetCount = 1; Da.pSetLayouts = &m_Dsl;
                if (vkAllocateDescriptorSets(m_Device, &Da, &Sets[k]) != VK_SUCCESS) return false;
                VkDescriptorBufferInfo CbI{ m_Cb.B, 0, sizeof(options) }, InI{ Passes[k].pIn->B, 0, VK_WHOLE_SIZE }, OutI{ Passes[k].pOut->B, 0, VK_WHOLE_SIZE };
                VkDescriptorImageInfo  ImI{ VK_NULL_HANDLE, m_View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
                VkWriteDescriptorSet Wr[4] = {};
                for (auto& w : Wr) { w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w.dstSet = Sets[k]; w.descriptorCount = 1; }
                Wr[0].dstBinding = 0;  Wr[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; Wr[0].pBufferInfo = &CbI;
                Wr[1].dstBinding = 10; Wr[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;          Wr[1].pImageInfo  = &ImI;
                Wr[2].dstBinding = 11; Wr[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;         Wr[2].pBufferInfo = &InI;
                Wr[3].dstBinding = 20; Wr[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;         Wr[3].pBufferInfo = &OutI;
                vkUpdateDescriptorSets(m_Device, 4, Wr, 0, nullptr);
            }

            // record: upload, the passes (a barrier between them: each reads what the previous wrote), read back
            VkCommandBufferAllocateInfo A{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO }; A.commandPool = m_Pool; A.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; A.commandBufferCount = 1;
            VkCommandBuffer C; if (vkAllocateCommandBuffers(m_Device, &A, &C) != VK_SUCCESS) return false;
            VkCommandBufferBeginInfo Bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; Bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(C, &Bi);

            VkImageMemoryBarrier B{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER }; B.image = m_Image; B.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            B.srcQueueFamilyIndex = B.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            B.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; B.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; B.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; B.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(C, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &B);
            VkBufferImageCopy Copy{}; Copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; Copy.imageExtent = { uint32_t(W4), uint32_t(H4), 1 };
            vkCmdCopyBufferToImage(C, m_Staging.B, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Copy);
            B.oldLayout = B.newLayout; B.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; B.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; B.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(C, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &B);

            int Last = -1;
            for (const auto& D : Dispatches)
            {
                if (D.Pass != Last)
                {
                    if (Last >= 0)
                    {
                        VkMemoryBarrier Pb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER }; Pb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; Pb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                        vkCmdPipelineBarrier(C, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Pb, 0, nullptr, 0, nullptr);
                    }
                    vkCmdBindPipeline(C, VK_PIPELINE_BIND_POINT_COMPUTE, Passes[D.Pass].P);
                    Last = D.Pass;
                }
                const uint32_t Dyn = D.Slot * 256;
                vkCmdBindDescriptorSets(C, VK_PIPELINE_BIND_POINT_COMPUTE, m_Layout, 0, 1, &Sets[D.Pass], 1, &Dyn);
                vkCmdDispatch(C, D.Groups, 1, 1);
            }

            VkMemoryBarrier Mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER }; Mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; Mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(C, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &Mb, 0, nullptr, 0, nullptr);
            VkBufferCopy Bc{ 0, 0, VkDeviceSize(nBlocks) * BlockSize }; vkCmdCopyBuffer(C, m_Out.B, m_Read.B, 1, &Bc);
            vkEndCommandBuffer(C);

            VkSubmitInfo S{ VK_STRUCTURE_TYPE_SUBMIT_INFO }; S.commandBufferCount = 1; S.pCommandBuffers = &C;
            const bool bOk = vkQueueSubmit(m_Queue, 1, &S, VK_NULL_HANDLE) == VK_SUCCESS && vkQueueWaitIdle(m_Queue) == VK_SUCCESS;
            vkFreeCommandBuffers(m_Device, m_Pool, 1, &C);
            if (!bOk) return false;

            Blocks.resize(size_t(nBlocks) * BlockSize);
            std::memcpy(Blocks.data(), m_Read.p, Blocks.size());
            return true;
        }

    private:

        struct buffer { VkBuffer B{}; VkDeviceMemory M{}; void* p = nullptr; VkDeviceSize Size = 0; };

        uint32_t MemType(uint32_t Bits, VkMemoryPropertyFlags Want) noexcept
        {
            VkPhysicalDeviceMemoryProperties P; vkGetPhysicalDeviceMemoryProperties(m_Phys, &P);
            for (uint32_t i = 0; i < P.memoryTypeCount; ++i) if ((Bits & (1u << i)) && (P.memoryTypes[i].propertyFlags & Want) == Want) return i;
            return ~0u;
        }

        void Free(buffer& b) noexcept
        {
            if (b.B) vkDestroyBuffer(m_Device, b.B, nullptr);
            if (b.M) vkFreeMemory(m_Device, b.M, nullptr);
            b = {};
        }

        // b at least Size bytes (kept between calls: the largest mip first, so it is made once per texture)
        bool Ensure(buffer& b, VkDeviceSize Size, VkBufferUsageFlags Usage, bool bHost) noexcept
        {
            if (b.B && b.Size >= Size) return true;
            Free(b);
            VkBufferCreateInfo Bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO }; Bci.size = Size; Bci.usage = Usage;
            if (vkCreateBuffer(m_Device, &Bci, nullptr, &b.B) != VK_SUCCESS) return false;
            VkMemoryRequirements Req; vkGetBufferMemoryRequirements(m_Device, b.B, &Req);
            VkMemoryAllocateInfo Mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; Mai.allocationSize = Req.size;
            Mai.memoryTypeIndex = MemType(Req.memoryTypeBits, bHost ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (Mai.memoryTypeIndex == ~0u || vkAllocateMemory(m_Device, &Mai, nullptr, &b.M) != VK_SUCCESS || vkBindBufferMemory(m_Device, b.B, b.M, 0) != VK_SUCCESS) return false;
            if (bHost && vkMapMemory(m_Device, b.M, 0, Size, 0, &b.p) != VK_SUCCESS) return false;
            b.Size = Size;
            return true;
        }

        bool Buffers(int W4, int H4, uint32_t nBlocks, uint32_t BlockSize, bool bBC7) noexcept
        {
            const VkDeviceSize Pixels = VkDeviceSize(W4) * H4 * 4, Out = VkDeviceSize(nBlocks) * BlockSize;
            if (!Ensure(m_Staging, Pixels, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true)) return false;
            if (!Ensure(m_Out, Out, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false)) return false;
            if (!Ensure(m_Read, Out, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) return false;
            if (!Ensure(m_Dummy, 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false)) return false;
            // BC7's per-block best mode so far (SharedIOData: 5 uints + a uint4 = 48 bytes in SPIR-V's layout), ping-ponged by the passes
            const VkDeviceSize Err = bBC7 ? VkDeviceSize(nBlocks) * 48 : 256;
            if (!Ensure(m_Err0, Err, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false) || !Ensure(m_Err1, Err, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false)) return false;

            // the source image (RGBA8 UNORM: Load returns 0..1 as the kernels expect), remade only when the size changes
            if (m_Image && m_ImageW == W4 && m_ImageH == H4) return true;
            if (m_View)  vkDestroyImageView(m_Device, m_View, nullptr);
            if (m_Image) vkDestroyImage(m_Device, m_Image, nullptr);
            if (m_ImageMem) vkFreeMemory(m_Device, m_ImageMem, nullptr);
            m_View = VK_NULL_HANDLE; m_Image = VK_NULL_HANDLE; m_ImageMem = VK_NULL_HANDLE;
            VkImageCreateInfo Ic{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO }; Ic.imageType = VK_IMAGE_TYPE_2D; Ic.format = VK_FORMAT_R8G8B8A8_UNORM;
            Ic.extent = { uint32_t(W4), uint32_t(H4), 1 }; Ic.mipLevels = 1; Ic.arrayLayers = 1; Ic.samples = VK_SAMPLE_COUNT_1_BIT; Ic.tiling = VK_IMAGE_TILING_OPTIMAL;
            Ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            if (vkCreateImage(m_Device, &Ic, nullptr, &m_Image) != VK_SUCCESS) return false;
            VkMemoryRequirements Req; vkGetImageMemoryRequirements(m_Device, m_Image, &Req);
            VkMemoryAllocateInfo Mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; Mai.allocationSize = Req.size; Mai.memoryTypeIndex = MemType(Req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (Mai.memoryTypeIndex == ~0u || vkAllocateMemory(m_Device, &Mai, nullptr, &m_ImageMem) != VK_SUCCESS || vkBindImageMemory(m_Device, m_Image, m_ImageMem, 0) != VK_SUCCESS) return false;
            VkImageViewCreateInfo Vc{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO }; Vc.image = m_Image; Vc.viewType = VK_IMAGE_VIEW_TYPE_2D; Vc.format = Ic.format;
            Vc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            if (vkCreateImageView(m_Device, &Vc, nullptr, &m_View) != VK_SUCCESS) return false;
            m_ImageW = W4; m_ImageH = H4;
            return true;
        }

        // the format's pipelines, made the first time it is used
        bool Pipelines(format Fmt) noexcept
        {
            auto& K = m_Kernels[int(Fmt)];
            if (K[0]) return true;
            Mark();
            struct code { const std::uint32_t* p; std::size_t n; };
            code Codes[4]; int nCodes = 0;
            switch (Fmt)
            {
            case format::BC1: Codes[nCodes++] = { xtexture_gpu_bc1_EncodeBlocks_v, sizeof(xtexture_gpu_bc1_EncodeBlocks_v) }; break;
            case format::BC3: Codes[nCodes++] = { xtexture_gpu_bc3_EncodeBlocks_v, sizeof(xtexture_gpu_bc3_EncodeBlocks_v) }; break;
            case format::BC4: Codes[nCodes++] = { xtexture_gpu_bc4_EncodeBlocks_v, sizeof(xtexture_gpu_bc4_EncodeBlocks_v) }; break;
            case format::BC5: Codes[nCodes++] = { xtexture_gpu_bc5_EncodeBlocks_v, sizeof(xtexture_gpu_bc5_EncodeBlocks_v) }; break;
            case format::BC7:
                Codes[nCodes++] = { xtexture_gpu_bc7_TryMode456CS_v, sizeof(xtexture_gpu_bc7_TryMode456CS_v) };
                Codes[nCodes++] = { xtexture_gpu_bc7_TryMode137CS_v, sizeof(xtexture_gpu_bc7_TryMode137CS_v) };
                Codes[nCodes++] = { xtexture_gpu_bc7_TryMode02CS_v,  sizeof(xtexture_gpu_bc7_TryMode02CS_v)  };
                Codes[nCodes++] = { xtexture_gpu_bc7_EncodeBlocks_v, sizeof(xtexture_gpu_bc7_EncodeBlocks_v) };
                break;
            }
            for (int i = 0; i < nCodes; ++i)
            {
                VkShaderModuleCreateInfo Si{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO }; Si.codeSize = Codes[i].n; Si.pCode = Codes[i].p;
                VkShaderModule M; if (vkCreateShaderModule(m_Device, &Si, nullptr, &M) != VK_SUCCESS) return false;
                VkComputePipelineCreateInfo Ci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
                Ci.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, M, "main", nullptr };
                Ci.layout = m_Layout;
                const bool bOk = vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &Ci, nullptr, &K[i]) == VK_SUCCESS;
                vkDestroyShaderModule(m_Device, M, nullptr);
                if (!bOk) return false;
            }
            Lap(m_tPipelines);
            return true;
        }

        void Release() noexcept
        {
            // (an Init that stopped half way can leave a device without its functions: then only the device itself is destroyed)
            // (vkCmdCopyBuffer is the last one loaded)
            if (m_Device && !vkCmdCopyBuffer) { if (vkDestroyDevice) vkDestroyDevice(m_Device, nullptr); }
            else if (m_Device)
            {
                if (m_Queue) vkQueueWaitIdle(m_Queue);
                for (auto& K : m_Kernels) for (auto& P : K) if (P) vkDestroyPipeline(m_Device, P, nullptr);
                for (buffer* b : { &m_Staging, &m_Out, &m_Read, &m_Dummy, &m_Err0, &m_Err1, &m_Cb }) Free(*b);
                if (m_View)     vkDestroyImageView(m_Device, m_View, nullptr);
                if (m_Image)    vkDestroyImage(m_Device, m_Image, nullptr);
                if (m_ImageMem) vkFreeMemory(m_Device, m_ImageMem, nullptr);
                if (m_DPool)    vkDestroyDescriptorPool(m_Device, m_DPool, nullptr);
                if (m_Layout)   vkDestroyPipelineLayout(m_Device, m_Layout, nullptr);
                if (m_Dsl)      vkDestroyDescriptorSetLayout(m_Device, m_Dsl, nullptr);
                if (m_Pool)     vkDestroyCommandPool(m_Device, m_Pool, nullptr);
                vkDestroyDevice(m_Device, nullptr);
            }
            if (m_Instance && vkDestroyInstance) vkDestroyInstance(m_Instance, nullptr);
        #if defined(_WIN32)
            if (m_hLib) FreeLibrary(static_cast<HMODULE>(m_hLib));
        #else
            if (m_hLib) dlclose(m_hLib);
        #endif
            m_Device = VK_NULL_HANDLE; m_Instance = VK_NULL_HANDLE; m_hLib = nullptr; m_bReady = false;
        }

        #define XTEXTURE_GPU_DECLARE(F) PFN_##F F = nullptr;
        XTEXTURE_GPU_VK_INSTANCE(XTEXTURE_GPU_DECLARE)
        XTEXTURE_GPU_VK_DEVICE(XTEXTURE_GPU_DECLARE)

        void*                   m_hLib      = nullptr;
        VkInstance              m_Instance  = VK_NULL_HANDLE;
        VkPhysicalDevice        m_Phys      = VK_NULL_HANDLE;
        VkDevice                m_Device    = VK_NULL_HANDLE;
        VkQueue                 m_Queue     = VK_NULL_HANDLE;
        uint32_t                m_Family    = ~0u;
        VkCommandPool           m_Pool      = VK_NULL_HANDLE;
        VkDescriptorSetLayout   m_Dsl       = VK_NULL_HANDLE;
        VkPipelineLayout        m_Layout    = VK_NULL_HANDLE;
        VkDescriptorPool        m_DPool     = VK_NULL_HANDLE;
        VkPipeline              m_Kernels[5][4] = {};
        buffer                  m_Staging, m_Out, m_Read, m_Dummy, m_Err0, m_Err1, m_Cb;
        VkImage                 m_Image     = VK_NULL_HANDLE;
        VkDeviceMemory          m_ImageMem  = VK_NULL_HANDLE;
        VkImageView             m_View      = VK_NULL_HANDLE;
        int                     m_ImageW = 0, m_ImageH = 0;
        char                    m_Name[256] = {};
        std::chrono::steady_clock::time_point m_t0;
        double                  m_tLoader = 0, m_tInstance = 0, m_tDevice = 0, m_tPipelines = 0;
        void Mark() noexcept { m_t0 = std::chrono::steady_clock::now(); }
        void Lap(double& Ms) noexcept { const auto t = std::chrono::steady_clock::now(); Ms += std::chrono::duration<double, std::milli>(t - m_t0).count(); m_t0 = t; }
        bool                    m_bReady    = false;
    };
}
#else
namespace xtexture_gpu       // built without the Vulkan headers: always the CPU
{
    enum class format { BC1, BC3, BC4, BC5, BC7 };
    class encoder
    {
    public:
        bool        Init() noexcept { return false; }
        const char* getDeviceName() const noexcept { return ""; }
        void        getStartupTimes(char* p, std::size_t n) const noexcept { if (n) p[0] = 0; }
        bool        Encode(format, const std::uint8_t*, int, int, float, std::vector<std::uint8_t>&) noexcept { return false; }
    };
}
#endif
