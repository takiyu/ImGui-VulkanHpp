#include "imgui_impl_vulkanhpp.h"

#include <bits/stdint-uintn.h>

#include <iostream>
#include <tuple>

#include "vulkan/vulkan.hpp"

namespace {

// -----------------------------------------------------------------------------
// ---------------------------------- Context ----------------------------------
// -----------------------------------------------------------------------------
struct Context {
    vk::PhysicalDevice physical_device;
    vk::Device device;

    vk::UniqueImage font_img;
    vk::UniqueDeviceMemory font_dev_mem;
    vk::UniqueImageView font_img_view;
    vk::UniqueSampler font_sampler;

    vk::UniqueBuffer unif_buf;
    vk::UniqueDeviceMemory unif_dev_mem;

    vk::UniqueDescriptorSet desc_set;
    vk::UniqueDescriptorSetLayout desc_set_layout;
    vk::UniqueDescriptorPool desc_pool;

    size_t vtx_size = 0;
    size_t idx_size = 0;
    vk::UniqueBuffer vtx_buf;
    vk::UniqueBuffer idx_buf;
    vk::UniqueDeviceMemory vtx_dev_mem;
    vk::UniqueDeviceMemory idx_dev_mem;
};

// Global Context
Context g_ctx;

// -----------------------------------------------------------------------------
// ---------------------------------- Shaders ----------------------------------
// -----------------------------------------------------------------------------
const std::vector<uint32_t> VERT_SPRV = {
#include "shaders/simple.vert.spv"  // Load uint32_t string directly
};
const std::vector<uint32_t> FRAG_SPRV = {
#include "shaders/simple.frag.spv"  // Load uint32_t string directly
};

struct UniformBuffer {
    float scale[2];
    float shift[2];
};

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
    uint8_t* dev_p = static_cast<uint8_t*>(
            g_ctx.device.mapMemory(dev_mem.get(), 0, n_bytes));
    return dev_p;
}

void UnmapDeviceMem(const vk::UniqueDeviceMemory& dev_mem) {
    // Unmap
    g_ctx.device.unmapMemory(dev_mem.get());
}

void SendToDevice(const vk::UniqueDeviceMemory& dev_mem, size_t n_bytes,
                  void* src_p) {
    // Map
    uint8_t* dst_p = MapDeviceMem(dev_mem, n_bytes);
    // Copy
    memcpy(dst_p, src_p, n_bytes);
    // Unmap
    UnmapDeviceMem(dev_mem);
}

// void SendToDevice() {
// }

auto CreateDeviceImage(const vk::Extent2D& size, const vk::Format& format) {
    // Create image
    auto ret_img = g_ctx.device.createImageUnique(
            {vk::ImageCreateFlags(), vk::ImageType::e2D, format,
             vk::Extent3D(size, 1), 1, 1, vk::SampleCountFlagBits::e1,
             vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled,
             vk::SharingMode::eExclusive, 0, nullptr,
             vk::ImageLayout::eUndefined});

    // Create device memory
    auto mem_req = g_ctx.device.getImageMemoryRequirements(*ret_img);
    auto type_idx =
            ObtainMemTypeIdx(mem_req, vk::MemoryPropertyFlagBits::eDeviceLocal);
    auto ret_dev_mem =
            g_ctx.device.allocateMemoryUnique({mem_req.size, type_idx});

    // Bind memory
    g_ctx.device.bindImageMemory(ret_img.get(), ret_dev_mem.get(), 0);

    // Create image view
    const vk::ComponentMapping COMP_MAPPING(
            vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG,
            vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA);
    const vk::ImageSubresourceRange SUBRES_RANGE(
            vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    auto ret_view = g_ctx.device.createImageViewUnique(
            {vk::ImageViewCreateFlags(), ret_img.get(), vk::ImageViewType::e2D,
             format, COMP_MAPPING, SUBRES_RANGE});

    // Create sampler
    auto ret_sampler = g_ctx.device.createSamplerUnique(
            {vk::SamplerCreateFlags(), vk::Filter::eLinear, vk::Filter::eLinear,
             vk::SamplerMipmapMode::eLinear, vk::SamplerAddressMode::eRepeat,
             vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat,
             0.f, false, 16.f, false, vk::CompareOp::eNever, 0.f, 1.f,
             vk::BorderColor::eFloatOpaqueBlack});

    return std::make_tuple(std::move(ret_img), std::move(ret_dev_mem),
                           std::move(ret_view), std::move(ret_sampler));
}

auto CreateSimpleDescSet(const vk::UniqueBuffer& unif_buf,
                         const vk::UniqueSampler& tex_sampler,
                         const vk::UniqueImageView& tex_img_view) {
    // Parse into raw array of bindings, pool sizes
    std::vector<vk::DescriptorSetLayoutBinding> bindings_raw = {
            {0, vk::DescriptorType::eUniformBufferDynamic, 1,
             vk::ShaderStageFlagBits::eVertex},
            {1, vk::DescriptorType::eCombinedImageSampler, 1,
             vk::ShaderStageFlagBits::eFragment}};
    std::vector<vk::DescriptorPoolSize> poolsizes_raw = {
            {vk::DescriptorType::eUniformBufferDynamic, 1},
            {vk::DescriptorType::eCombinedImageSampler, 1}};

    // Create DescriptorSetLayout
    const uint32_t N_BINDINGS = 2;
    const uint32_t DESC_CNT_SUM = 2;
    auto desc_set_layout = g_ctx.device.createDescriptorSetLayoutUnique(
            {vk::DescriptorSetLayoutCreateFlags(), N_BINDINGS,
             bindings_raw.data()});
    // Create DescriptorPool
    auto desc_pool = g_ctx.device.createDescriptorPoolUnique(
            {vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, DESC_CNT_SUM,
             N_BINDINGS, poolsizes_raw.data()});
    // Create DescriptorSet
    auto desc_sets = g_ctx.device.allocateDescriptorSetsUnique(
            {*desc_pool, 1, &*desc_set_layout});
    auto& desc_set = desc_sets[0];

    // Parse into write descriptor arrays
    std::vector<vk::DescriptorBufferInfo> desc_buf_info_vecs = {
            {*unif_buf, 0, VK_WHOLE_SIZE}};
    std::vector<vk::DescriptorImageInfo> desc_img_info_vecs = {
            {*tex_sampler, *tex_img_view, vk::ImageLayout::eGeneral}};
    std::vector<vk::WriteDescriptorSet> write_desc_sets = {
            {*desc_set, 0, 0, 1, vk::DescriptorType::eUniformBufferDynamic,
             nullptr, desc_buf_info_vecs.data(), nullptr},
            {*desc_set, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler,
             desc_img_info_vecs.data(), nullptr, nullptr}};
    // Write descriptor set
    g_ctx.device.updateDescriptorSets(write_desc_sets, nullptr);

    return std::make_tuple(std::move(desc_set_layout), std::move(desc_pool),
                           std::move(desc_set));
}

void BeginCommand(const vk::CommandBuffer& cmd_buf,
                  bool one_time_submit = true) {
    // Create begin flags
    vk::CommandBufferUsageFlags flags;
    if (one_time_submit) {
        flags |= vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    }
    // Begin
    cmd_buf.begin({flags});
}

void EndCommand(const vk::CommandBuffer& cmd_buf) {
    // End
    cmd_buf.end();
}

void ResetCommand(const vk::CommandBuffer& cmd_buf) {
    // Reset
    cmd_buf.reset(vk::CommandBufferResetFlags());
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

}  // anonymous namespace

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

    // Create font texture
    uint8_t* pixels = nullptr;
    int32_t width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    // Create Texture
    std::tie(g_ctx.font_img, g_ctx.unif_dev_mem, g_ctx.font_img_view,
             g_ctx.font_sampler) =
            CreateDeviceImage({width, height}, vk::Format::eR8G8B8A8Unorm);

    //     io.Fonts->TexID = (ImTextureID)static_cast<intptr_t>(100);
    //     io.Fonts->TexID = (ImTextureID)(intptr_t)g_FontImage;

    // Create Uniform Buffer
    std::tie(g_ctx.unif_buf, g_ctx.unif_dev_mem) = CreateHostVisibBuffer(
            sizeof(UniformBuffer), vk::BufferUsageFlagBits::eUniformBuffer);

    // Create Descriptor Sets (for vertex shader and fragment shader)
    std::tie(g_ctx.desc_set_layout, g_ctx.desc_pool, g_ctx.desc_set) =
            CreateSimpleDescSet(g_ctx.unif_buf, g_ctx.font_sampler,
                                g_ctx.font_img_view);

    //     vkw::AddAttachientDesc(
    //             render_pass_pack, surface_format,
    //             vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
    //             vk::ImageLayout::ePresentSrcKHR);
    //     vkw::AddAttachientDesc(render_pass_pack, DEPTH_FORMAT,
    //                            vk::AttachmentLoadOp::eClear,
    //                            vk::AttachmentStoreOp::eDontCare,
    //                            vk::ImageLayout::eDepthStencilAttachmentOptimal);
    //     // Subpass
    //     vkw::AddSubpassDesc(render_pass_pack, {},
    //                         {{0, vk::ImageLayout::eColorAttachmentOptimal}},
    //                         {1,
    //                         vk::ImageLayout::eDepthStencilAttachmentOptimal});
    //     vkw::UpdateRenderPass(device, render_pass_pack);
    //     // Pipeline
    //     vkw::PipelineInfo pipeline_info;
    //     pipeline_info.color_blend_infos.resize(1);
    //     auto pipeline_pack = vkw::CreateGraphicsPipeline(
    //             device, {vert_shader_module_pack, frag_shader_module_pack},
    //             {{0, sizeof(Vertex), vk::VertexInputRate::eVertex}},
    //             {{0, 0, vk::Format::eR32G32B32A32Sfloat, 0},
    //              {1, 0, vk::Format::eR32G32B32A32Sfloat, 16}},
    //             pipeline_info, {desc_set_pack}, render_pass_pack);

    return true;
}

IMGUI_IMPL_API void ImGui_ImplVulkanHpp_Shutdown() {
    // Clear global context
    g_ctx = {};
}

IMGUI_IMPL_API void ImGui_ImplVulkanHpp_NewFrame() {}

IMGUI_IMPL_API void ImGui_ImplVulkanHpp_RenderDrawData(
        ImDrawData* draw_data, const vk::CommandBuffer& dst_cmd_buf,
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
    //     ImGui_ImplVulkan_SetupRenderState(draw_data, pipeline,
    //     command_buffer, rb, fb_width, fb_height);
    //
    //     // Will project scissor/clipping rectangles into framebuffer space
    //     ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless
    //     using multi-viewports ImVec2 clip_scale =
    //     draw_data->FramebufferScale; // (1,1) unless using retina display
    //     which are often (2,2)
    //
    //     // Render command lists
    //     // (Because we merged all buffers into a single one, we maintain our
    //     own offset into them) int global_vtx_offset = 0; int
    //     global_idx_offset = 0; for (int n = 0; n < draw_data->CmdListsCount;
    //     n++)
    //     {
    //         const ImDrawList* cmd_list = draw_data->CmdLists[n];
    //         for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
    //         {
    //             const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
    //             if (pcmd->UserCallback != NULL)
    //             {
    //                 // User callback, registered via
    //                 ImDrawList::AddCallback()
    //                 // (ImDrawCallback_ResetRenderState is a special callback
    //                 value used by the user to request the renderer to reset
    //                 render state.) if (pcmd->UserCallback ==
    //                 ImDrawCallback_ResetRenderState)
    //                     ImGui_ImplVulkan_SetupRenderState(draw_data,
    //                     pipeline, command_buffer, rb, fb_width, fb_height);
    //                 else
    //                     pcmd->UserCallback(cmd_list, pcmd);
    //             }
    //             else
    //             {
    //                 // Project scissor/clipping rectangles into framebuffer
    //                 space ImVec4 clip_rect; clip_rect.x = (pcmd->ClipRect.x -
    //                 clip_off.x) * clip_scale.x; clip_rect.y =
    //                 (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
    //                 clip_rect.z = (pcmd->ClipRect.z - clip_off.x) *
    //                 clip_scale.x; clip_rect.w = (pcmd->ClipRect.w -
    //                 clip_off.y) * clip_scale.y;
    //
    //                 if (clip_rect.x < fb_width && clip_rect.y < fb_height &&
    //                 clip_rect.z >= 0.0f && clip_rect.w >= 0.0f)
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
    //                     scissor.extent.width = (uint32_t)(clip_rect.z -
    //                     clip_rect.x); scissor.extent.height =
    //                     (uint32_t)(clip_rect.w - clip_rect.y);
    //                     vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    //
    //                     // Draw
    //                     vkCmdDrawIndexed(command_buffer, pcmd->ElemCount, 1,
    //                     pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset
    //                     + global_vtx_offset, 0);
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
