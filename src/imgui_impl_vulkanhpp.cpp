#include "imgui_impl_vulkanhpp.h"

#include <bits/stdint-uintn.h>
#include <vkw/vkw.h>

#include <iostream>
#include <tuple>

namespace {

// -----------------------------------------------------------------------------
// --------------------------------- Constants ---------------------------------
// -----------------------------------------------------------------------------
const std::array<float, 4> CLEAR_COLOR = {0.2f, 0.2f, 1.0f, 1.0f};
const std::vector<vk::ClearValue> CLEAR_VALS = {{CLEAR_COLOR}};
constexpr auto IDX_TYPE = (sizeof(ImDrawIdx) == 2) ? vk::IndexType::eUint16 :
                                                     vk::IndexType::eUint32;

// -----------------------------------------------------------------------------
// ---------------------------------- Shaders ----------------------------------
// -----------------------------------------------------------------------------
const std::string VERT_SOURCE = R"(
#version 460
layout (binding = 0) uniform UnifBuf {
    vec2 scale;
    vec2 shift;
} uniform_buf;
layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 uv;
layout (location = 2) in vec4 col;
layout (location = 0) out vec2 vtx_uv;
layout (location = 1) out vec4 vtx_col;
void main() {
    gl_Position = vec4(uniform_buf.scale * pos + uniform_buf.shift, 0.0, 1.0);
    vtx_uv = uv;
    vtx_col = col;
}
)";

const std::string FRAG_SOURCE = R"(
#version 460
layout (set = 0, binding = 1) uniform sampler2D tex;
layout (location = 0) in vec2 vtx_uv;
layout (location = 1) in vec4 vtx_col;
layout (location = 0) out vec4 frag_col;
void main() {
    frag_col = vtx_col * texture(tex, vtx_uv);
}
)";

struct UnifBuf {
    float scale[2];
    float shift[2];
};

// -----------------------------------------------------------------------------
// ---------------------------------- Context ----------------------------------
// -----------------------------------------------------------------------------
struct Context {
    const vk::PhysicalDevice* physical_device_p;
    const vk::UniqueDevice* device_p;

    vkw::ShaderModulePackPtr vert_shader_module_pack;
    vkw::ShaderModulePackPtr frag_shader_module_pack;

    vkw::BufferPackPtr unif_buf_pack;
    UnifBuf unif_buf;

    uint8_t* font_pixel_p = nullptr;
    size_t font_pixel_size = 0;
    vkw::ImagePackPtr font_img_pack;
    vkw::TexturePackPtr font_tex_pack;
    bool is_font_tex_sent = false;
    vkw::BufferPackPtr font_buf_pack;

    vkw::DescSetPackPtr desc_set_pack;
    vkw::WriteDescSetPackPtr write_desc_set_pack;

    vk::Format img_format = vk::Format::eUndefined;
    vk::Extent2D img_size = {0, 0};
    vkw::RenderPassPackPtr render_pass_pack;
    vkw::PipelinePackPtr pipeline_pack;
    vkw::FrameBufferPackPtr frame_buffer_pack;

    size_t vtx_size = 0;
    vkw::BufferPackPtr vtx_buf_pack;
    size_t idx_size = 0;
    vkw::BufferPackPtr idx_buf_pack;
};

// Global Context
Context g_ctx;

// -----------------------------------------------------------------------------
// ------------------------------ Vulkan Utility -------------------------------
// -----------------------------------------------------------------------------
uint8_t* MapDeviceMem(const vk::UniqueDevice& device,
                      const vkw::BufferPackPtr& buf_pack, size_t n_bytes) {
    // Map
    uint8_t* dev_p = static_cast<uint8_t*>(device->mapMemory(
            buf_pack->dev_mem_pack->dev_mem.get(), 0, n_bytes));
    return dev_p;
}

void UnmapDeviceMem(const vk::UniqueDevice& device,
                    const vkw::BufferPackPtr& buf_pack) {
    // Unmap
    device->unmapMemory(buf_pack->dev_mem_pack->dev_mem.get());
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

}  // anonymous namespace

// -----------------------------------------------------------------------------
// -------------------------------- Interfaces ---------------------------------
// -----------------------------------------------------------------------------
IMGUI_IMPL_API bool ImGui_ImplVulkanHpp_Init(
        const vk::PhysicalDevice& physical_device,
        const vk::UniqueDevice& device) {
    // Set to global context
    g_ctx.physical_device_p = &physical_device;
    g_ctx.device_p = &device;

    // Set backend name
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_vulkanhpp";

    // Compile shaders
    vkw::GLSLCompiler glsl_compiler;
    g_ctx.vert_shader_module_pack = glsl_compiler.compileFromString(
            device, VERT_SOURCE, vk::ShaderStageFlagBits::eVertex);
    g_ctx.frag_shader_module_pack = glsl_compiler.compileFromString(
            device, FRAG_SOURCE, vk::ShaderStageFlagBits::eFragment);

    // Create Uniform Buffer
    g_ctx.unif_buf_pack =
            vkw::CreateBufferPack(physical_device, device, sizeof(UnifBuf),
                                  vk::BufferUsageFlagBits::eUniformBuffer,
                                  vkw::HOST_VISIB_COHER_PROPS);

    // Create font texture
    int32_t width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&g_ctx.font_pixel_p, &width, &height);
    g_ctx.font_pixel_size = static_cast<size_t>(width * height) * 4;
    // Create Texture
    g_ctx.font_img_pack = vkw::CreateImagePack(
            physical_device, device, vk::Format::eR8G8B8A8Unorm,
            {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}, 1,
            vk::ImageUsageFlagBits::eSampled |
                    vk::ImageUsageFlagBits::eTransferDst);
    g_ctx.font_tex_pack = vkw::CreateTexturePack(g_ctx.font_img_pack, device);
    io.Fonts->TexID = static_cast<ImTextureID>(g_ctx.font_img_pack.get());

    // Descriptor set for Uniform buffer and Font texture
    g_ctx.desc_set_pack = vkw::CreateDescriptorSetPack(
            device, {{vk::DescriptorType::eUniformBufferDynamic, 1,
                      vk::ShaderStageFlagBits::eVertex},
                     {vk::DescriptorType::eCombinedImageSampler, 1,
                      vk::ShaderStageFlagBits::eFragment}});
    // Bind descriptor set with actual buffer
    g_ctx.write_desc_set_pack = vkw::CreateWriteDescSetPack();
    vkw::AddWriteDescSet(g_ctx.write_desc_set_pack, g_ctx.desc_set_pack, 0,
                         {g_ctx.unif_buf_pack});
    vkw::AddWriteDescSet(g_ctx.write_desc_set_pack, g_ctx.desc_set_pack, 1,
                         {g_ctx.font_tex_pack},  // layout is still undefined.
                         {vk::ImageLayout::eShaderReadOnlyOptimal});
    vkw::UpdateDescriptorSets(device, g_ctx.write_desc_set_pack);

    return true;
}

IMGUI_IMPL_API void ImGui_ImplVulkanHpp_Shutdown() {
    // Clear global context
    g_ctx = {};
}

IMGUI_IMPL_API void ImGui_ImplVulkanHpp_NewFrame() {}

IMGUI_IMPL_API void ImGui_ImplVulkanHpp_RenderDrawData(
        ImDrawData* draw_data, const vk::UniqueCommandBuffer& dst_cmd_buf,
        const vk::ImageView& dst_img_view, const vk::Format& dst_img_format,
        const vk::Extent2D& dst_img_size,
        const vk::ImageLayout& dst_img_layout) {
    // Reset and begin command buffer
    vkw::ResetCommand(dst_cmd_buf);
    vkw::BeginCommand(dst_cmd_buf, true);  // once

    // Compute framebuffer size
    auto fb_width_f = draw_data->DisplaySize.x * draw_data->FramebufferScale.x;
    auto fb_height_f = draw_data->DisplaySize.y * draw_data->FramebufferScale.y;
    if (fb_width_f <= 0.f || fb_height_f <= 0.f) {
        vkw::EndCommand(dst_cmd_buf);  // Empty command
        return;
    }

    auto&& physical_device = *g_ctx.physical_device_p;
    auto&& device = *g_ctx.device_p;

    // Create Vertex Buffer
    const size_t& vtx_size =
            static_cast<size_t>(draw_data->TotalVtxCount) * sizeof(ImDrawVert);
    if (vtx_size == 0) {
        vkw::EndCommand(dst_cmd_buf);  // Empty command
        return;
    }
    if (vtx_size != g_ctx.vtx_size) {
        g_ctx.vtx_size = vtx_size;
        g_ctx.vtx_buf_pack =
                vkw::CreateBufferPack(physical_device, device, vtx_size,
                                      vk::BufferUsageFlagBits::eVertexBuffer,
                                      vkw::HOST_VISIB_COHER_PROPS);
    }
    // Create Index Buffer
    const size_t& idx_size =
            static_cast<size_t>(draw_data->TotalIdxCount) * sizeof(ImDrawIdx);
    if (idx_size != g_ctx.idx_size) {
        g_ctx.idx_size = idx_size;
        g_ctx.idx_buf_pack =
                vkw::CreateBufferPack(physical_device, device, idx_size,
                                      vk::BufferUsageFlagBits::eIndexBuffer,
                                      vkw::HOST_VISIB_COHER_PROPS);
    }

    // Send vertex/index data to GPU (TODO: Async)
    uint8_t* vtx_dst = MapDeviceMem(device, g_ctx.vtx_buf_pack, vtx_size);
    uint8_t* idx_dst = MapDeviceMem(device, g_ctx.idx_buf_pack, idx_size);
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const size_t vtx_n_bytes =
                static_cast<size_t>(cmd_list->VtxBuffer.Size) *
                sizeof(ImDrawVert);
        const size_t idx_n_bytes =
                static_cast<size_t>(cmd_list->IdxBuffer.Size) *
                sizeof(ImDrawIdx);
        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, vtx_n_bytes);
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, idx_n_bytes);
        vtx_dst += vtx_n_bytes;
        idx_dst += idx_n_bytes;
    }
    UnmapDeviceMem(device, g_ctx.vtx_buf_pack);
    UnmapDeviceMem(device, g_ctx.idx_buf_pack);

    // Send font texture
    if (!g_ctx.is_font_tex_sent) {
        g_ctx.is_font_tex_sent = true;
        // Create transferring buffer
        g_ctx.font_buf_pack = vkw::CreateBufferPack(
                physical_device, device, g_ctx.font_pixel_size,
                vk::BufferUsageFlagBits::eTransferSrc,
                vkw::HOST_VISIB_COHER_PROPS);
        // Send to buffer
        SendToDevice(device, g_ctx.font_buf_pack, g_ctx.font_pixel_p,
                     g_ctx.font_pixel_size);
        // Send buffer to image
        vkw::CopyBufferToImage(dst_cmd_buf, g_ctx.font_buf_pack,
                               g_ctx.font_img_pack);
    }

    if (g_ctx.img_format != dst_img_format) {
        g_ctx.img_format = dst_img_format;
        // Create render pass
        g_ctx.render_pass_pack = vkw::CreateRenderPassPack();
        // Add color attachment
        vkw::AddAttachientDesc(g_ctx.render_pass_pack, dst_img_format,
                               vk::AttachmentLoadOp::eLoad,
                               vk::AttachmentStoreOp::eStore, dst_img_layout);
        // Add subpass
        vkw::AddSubpassDesc(g_ctx.render_pass_pack, {},
                            {{0, vk::ImageLayout::eColorAttachmentOptimal}});
        // Create render pass instance
        vkw::UpdateRenderPass(device, g_ctx.render_pass_pack);

        // Create pipeline
        vkw::PipelineColorBlendAttachInfo pipeine_blend_info;
        pipeine_blend_info.blend_enable = true;
        vkw::PipelineInfo pipeline_info;
        pipeline_info.color_blend_infos = {pipeine_blend_info};
        pipeline_info.depth_test_enable = false;
        pipeline_info.face_culling = vk::CullModeFlagBits::eNone;
        g_ctx.pipeline_pack = vkw::CreateGraphicsPipeline(
                device,
                {g_ctx.vert_shader_module_pack, g_ctx.frag_shader_module_pack},
                {{0, sizeof(ImDrawVert), vk::VertexInputRate::eVertex}},
                {{0, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, pos)},
                 {1, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, uv)},
                 {2, 0, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col)}},
                pipeline_info, {g_ctx.desc_set_pack}, g_ctx.render_pass_pack);
    }

    // Update uniform buffer
    auto& unif_buf = g_ctx.unif_buf;
    unif_buf.scale[0] = 2.f / draw_data->DisplaySize.x;
    unif_buf.scale[1] = 2.f / draw_data->DisplaySize.y;
    unif_buf.shift[0] = -1.f - draw_data->DisplayPos.x * unif_buf.scale[0];
    unif_buf.shift[1] = -1.f - draw_data->DisplayPos.y * unif_buf.scale[1];
    vkw::SendToDevice(device, g_ctx.unif_buf_pack, &unif_buf, sizeof(UnifBuf));

    // Create frame buffer
    g_ctx.frame_buffer_pack = CreateFrameBuffer(device, g_ctx.render_pass_pack,
                                                {dst_img_view}, dst_img_size);

    // Record commands
    vkw::CmdBeginRenderPass(dst_cmd_buf, g_ctx.render_pass_pack,
                            g_ctx.frame_buffer_pack, CLEAR_VALS);
    vkw::CmdBindPipeline(dst_cmd_buf, g_ctx.pipeline_pack);
    vkw::CmdBindDescSets(dst_cmd_buf, g_ctx.pipeline_pack,
                         {g_ctx.desc_set_pack}, {0});
    vkw::CmdBindVertexBuffers(dst_cmd_buf, 0, {g_ctx.vtx_buf_pack});
    vkw::CmdBindIndexBuffer(dst_cmd_buf, g_ctx.idx_buf_pack, 0, IDX_TYPE);
    vkw::CmdSetViewport(dst_cmd_buf, dst_img_size);

    const ImVec2& clip_off = draw_data->DisplayPos;
    const ImVec2& clip_scale = draw_data->FramebufferScale;
    uint32_t global_vtx_offset = 0;
    uint32_t global_idx_offset = 0;
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr) {
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState) {
                    // TODO: Reset render state
                } else {
                    pcmd->UserCallback(cmd_list, pcmd);
                }
            } else {
                ImVec4 clip_rect = {
                        (pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                        (pcmd->ClipRect.y - clip_off.y) * clip_scale.y,
                        (pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                        (pcmd->ClipRect.w - clip_off.y) * clip_scale.y};

                if (clip_rect.x < fb_width_f && clip_rect.y < fb_height_f &&
                    clip_rect.z >= 0.f && clip_rect.w >= 0.f) {
                    // Negative offsets are illegal for vkCmdSetScissor
                    clip_rect.x = std::max(clip_rect.x, 0.f);
                    clip_rect.y = std::max(clip_rect.y, 0.f);

                    // Apply scissor/clipping rectangle
                    vk::Rect2D scissor = {
                            {static_cast<int32_t>(clip_rect.x),
                             static_cast<int32_t>(clip_rect.y)},
                            {static_cast<uint32_t>(clip_rect.z - clip_rect.x),
                             static_cast<uint32_t>(clip_rect.w - clip_rect.y)}};
                    vkw::CmdSetScissor(dst_cmd_buf, scissor);

                    // Draw
                    vkw::CmdDrawIndexed(dst_cmd_buf,
                                        static_cast<uint32_t>(pcmd->ElemCount),
                                        1, pcmd->IdxOffset + global_idx_offset,
                                        static_cast<int32_t>(pcmd->VtxOffset +
                                                             global_vtx_offset),
                                        0);
                }
            }
        }

        global_idx_offset += static_cast<uint32_t>(cmd_list->IdxBuffer.Size);
        global_vtx_offset += static_cast<uint32_t>(cmd_list->VtxBuffer.Size);
    }

    vkw::CmdEndRenderPass(dst_cmd_buf);

    vkw::EndCommand(dst_cmd_buf);
    return;
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
