#include "imgui_impl_vulkanhpp.h"

#include <bits/stdint-uintn.h>

#include <iostream>

#include "vulkan/vulkan.hpp"

namespace {

// -----------------------------------------------------------------------------
// ---------------------------------- Context ----------------------------------
// -----------------------------------------------------------------------------
struct Context {
    vk::PhysicalDevice physical_device;
    vk::Device device;

    size_t vtx_size = 0;
    size_t idx_size = 0;
    vk::UniqueBuffer vtx_buf;
    vk::UniqueBuffer idx_buf;
    vk::UniqueDeviceMemory vtx_dev_mem;
    vk::UniqueDeviceMemory idx_dev_mem;
};

// Global Context
Context g_ctx;

}  // anonymous namespace

// -----------------------------------------------------------------------------
// ------------------------------ Vulkan Utility -------------------------------
// -----------------------------------------------------------------------------
template <typename BitType>
bool IsFlagSufficient(const vk::Flags<BitType>& actual_flags,
                      const vk::Flags<BitType>& require_flags) {
    return (actual_flags & require_flags) == require_flags;
}

uint32_t ObtainMemTypeIdx(const vk::MemoryRequirements& mem_req,
                          const vk::MemoryPropertyFlags& mem_prop) {
    // Check memory requirements
    auto actual_mem_prop = g_ctx.physical_device.getMemoryProperties();
    uint32_t type_bits = mem_req.memoryTypeBits;
    uint32_t type_idx = uint32_t(~0);
    for (uint32_t i = 0; i < actual_mem_prop.memoryTypeCount; i++) {
        if ((type_bits & 1) &&
            IsFlagSufficient(actual_mem_prop.memoryTypes[i].propertyFlags,
                             mem_prop)) {
            type_idx = i;
            break;
        }
        type_bits >>= 1;
    }
    if (type_idx == uint32_t(~0)) {
        throw std::runtime_error("Failed to allocate requested memory");
    }
    return type_idx;
}

auto CreateHostVisibBuffer(size_t buf_size,
                           const vk::BufferUsageFlags& usage_flags) {
    // Create buffer
    auto ret_buf = g_ctx.device.createBufferUnique({{}, buf_size, usage_flags});

    // Create device memory
    auto mem_req = g_ctx.device.getBufferMemoryRequirements(ret_buf.get());
    auto type_idx = ObtainMemTypeIdx(
            mem_req, vk::MemoryPropertyFlagBits::eHostCoherent |
                             vk::MemoryPropertyFlagBits::eHostVisible);
    auto ret_dev_mem =
            g_ctx.device.allocateMemoryUnique({mem_req.size, type_idx});

    // Bind
    g_ctx.device.bindBufferMemory(ret_buf.get(), ret_dev_mem.get(), 0);

    return std::make_tuple(std::move(ret_buf), std::move(ret_dev_mem));
}

uint8_t* MapDeviceMem(const vk::UniqueDeviceMemory& dev_mem, size_t n_bytes) {
    // Map
    uint8_t* dev_p =
            static_cast<uint8_t*>(g_ctx.device.mapMemory(dev_mem.get(), 0, n_bytes));
    return dev_p;
}

void UnmapDeviceMem(const vk::UniqueDeviceMemory& dev_mem) {
    // Unmap
    g_ctx.device.unmapMemory(dev_mem.get());
}

void BeginCommand(const vk::CommandBuffer &cmd_buf,
                  bool one_time_submit = true) {
    // Create begin flags
    vk::CommandBufferUsageFlags flags;
    if (one_time_submit) {
        flags |= vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    }
    // Begin
    cmd_buf.begin({flags});
}

void EndCommand(const vk::CommandBuffer &cmd_buf) {
    // End
    cmd_buf.end();
}

void ResetCommand(const vk::CommandBuffer &cmd_buf) {
    // Reset
    cmd_buf.reset(vk::CommandBufferResetFlags());
}

// -----------------------------------------------------------------------------
// -------------------------------- Interfaces ---------------------------------
// -----------------------------------------------------------------------------
IMGUI_IMPL_API bool ImGui_ImplVulkanHpp_Init(
        const vk::PhysicalDevice& physical_device, const vk::Device& device) {
    // Set to global context
    g_ctx.physical_device = physical_device;
    g_ctx.device = device;

    // Set backend name
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_vulkanhpp";

    // Create font texture (TODO)
    uint8_t* pixels = nullptr;
    int32_t width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    size_t upload_size = width * height * 4;
    //     io.Fonts->TexID = (ImTextureID)static_cast<intptr_t>(100);
    //     io.Fonts->TexID = (ImTextureID)(intptr_t)g_FontImage;

    return true;
}

IMGUI_IMPL_API void ImGui_ImplVulkanHpp_Shutdown() {
    // Clear global context
    g_ctx = {};
}

IMGUI_IMPL_API void ImGui_ImplVulkanHpp_NewFrame() {}

IMGUI_IMPL_API void ImGui_ImplVulkanHpp_RenderDrawData(
                ImDrawData* draw_data,
                const vk::CommandBuffer& dst_cmd_buf,
                const vk::Framebuffer& dst_frame_buffer) {
    // Compute framebuffer size
    auto fb_width_f = draw_data->DisplaySize.x * draw_data->FramebufferScale.x;
    auto fb_height_f = draw_data->DisplaySize.y * draw_data->FramebufferScale.y;
    if (fb_width_f <= 0.f || fb_height_f <= 0.f) {
        // Empty command
        ResetCommand(dst_cmd_buf);
        BeginCommand(dst_cmd_buf);
        EndCommand(dst_cmd_buf);
        return;
    }

    // Create Vertex Buffer
    const size_t vtx_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
    if (vtx_size != g_ctx.vtx_size) {
        g_ctx.vtx_size = vtx_size;
        std::tie(g_ctx.vtx_buf, g_ctx.vtx_dev_mem) = CreateHostVisibBuffer(
                vtx_size, vk::BufferUsageFlagBits::eVertexBuffer);
    }
    // Create Index Buffer
    const size_t idx_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
    if (idx_size != g_ctx.idx_size) {
        g_ctx.idx_size = idx_size;
        std::tie(g_ctx.idx_buf, g_ctx.idx_dev_mem) = CreateHostVisibBuffer(
                idx_size, vk::BufferUsageFlagBits::eIndexBuffer);
    }

    if (0 < vtx_size) {
        // Send vertex/index data to GPU (TODO: Async)
        uint8_t* vtx_dst = MapDeviceMem(g_ctx.vtx_dev_mem, vtx_size);
        uint8_t* idx_dst = MapDeviceMem(g_ctx.idx_dev_mem, idx_size);
        for (int n = 0; n < draw_data->CmdListsCount; n++) {
            const ImDrawList* cmd_list = draw_data->CmdLists[n];
            size_t vtx_n_bytes = cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
            size_t idx_n_bytes = cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
            memcpy(vtx_dst, cmd_list->VtxBuffer.Data, vtx_n_bytes);
            memcpy(idx_dst, cmd_list->IdxBuffer.Data, idx_n_bytes);
            vtx_dst += vtx_n_bytes;
            idx_dst += idx_n_bytes;
        }
        UnmapDeviceMem(g_ctx.vtx_dev_mem);
        UnmapDeviceMem(g_ctx.idx_dev_mem);
    }

//     // Setup desired Vulkan state
//     ImGui_ImplVulkan_SetupRenderState(draw_data, pipeline, command_buffer, rb, fb_width, fb_height);
// 
//     // Will project scissor/clipping rectangles into framebuffer space
//     ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
//     ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)
// 
//     // Render command lists
//     // (Because we merged all buffers into a single one, we maintain our own offset into them)
//     int global_vtx_offset = 0;
//     int global_idx_offset = 0;
//     for (int n = 0; n < draw_data->CmdListsCount; n++)
//     {
//         const ImDrawList* cmd_list = draw_data->CmdLists[n];
//         for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
//         {
//             const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
//             if (pcmd->UserCallback != NULL)
//             {
//                 // User callback, registered via ImDrawList::AddCallback()
//                 // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
//                 if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
//                     ImGui_ImplVulkan_SetupRenderState(draw_data, pipeline, command_buffer, rb, fb_width, fb_height);
//                 else
//                     pcmd->UserCallback(cmd_list, pcmd);
//             }
//             else
//             {
//                 // Project scissor/clipping rectangles into framebuffer space
//                 ImVec4 clip_rect;
//                 clip_rect.x = (pcmd->ClipRect.x - clip_off.x) * clip_scale.x;
//                 clip_rect.y = (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
//                 clip_rect.z = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
//                 clip_rect.w = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;
// 
//                 if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f)
//                 {
//                     // Negative offsets are illegal for vkCmdSetScissor
//                     if (clip_rect.x < 0.0f)
//                         clip_rect.x = 0.0f;
//                     if (clip_rect.y < 0.0f)
//                         clip_rect.y = 0.0f;
// 
//                     // Apply scissor/clipping rectangle
//                     VkRect2D scissor;
//                     scissor.offset.x = (int32_t)(clip_rect.x);
//                     scissor.offset.y = (int32_t)(clip_rect.y);
//                     scissor.extent.width = (uint32_t)(clip_rect.z - clip_rect.x);
//                     scissor.extent.height = (uint32_t)(clip_rect.w - clip_rect.y);
//                     vkCmdSetScissor(command_buffer, 0, 1, &scissor);
// 
//                     // Draw
//                     vkCmdDrawIndexed(command_buffer, pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset, 0);
//                 }
//             }
//         }
//         global_idx_offset += cmd_list->IdxBuffer.Size;
//         global_vtx_offset += cmd_list->VtxBuffer.Size;
//     }

    ResetCommand(dst_cmd_buf);
    BeginCommand(dst_cmd_buf);
    EndCommand(dst_cmd_buf);

    return;
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
