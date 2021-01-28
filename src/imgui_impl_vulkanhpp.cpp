#include <vkw/vkw.h>

BEGIN_VKW_SUPPRESS_WARNING
#include "imgui_impl_vulkanhpp.h"
END_VKW_SUPPRESS_WARNING

#include <iostream>
#include <map>
#include <tuple>

namespace {

// -----------------------------------------------------------------------------
// --------------------------------- Constants ---------------------------------
// -----------------------------------------------------------------------------
constexpr auto IDX_TYPE = (sizeof(ImDrawIdx) == 2) ? vk::IndexType::eUint16 :
                                                     vk::IndexType::eUint32;

// -----------------------------------------------------------------------------
// ---------------------------------- Shaders ----------------------------------
// -----------------------------------------------------------------------------
const std::string BG_VERT_SOURCE = R"(
#version 460
layout (location = 0) out vec2 vtx_uv;
void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 screen_pos = uv * 2.0f - 1.0f;
    gl_Position = vec4(screen_pos, 0.0f, 1.0f);
    vtx_uv = uv;
}
)";
const std::string BG_FRAG_SOURCE = R"(
#version 460
layout (set = 0, binding = 0) uniform sampler2D tex;
layout (location = 0) in vec2 vtx_uv;
layout (location = 0) out vec4 frag_color;
void main() {
    frag_color = texture(tex, vtx_uv);
}
)";

const std::string IMGUI_VERT_SOURCE = R"(
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
const std::string IMGUI_FRAG_SOURCE = R"(
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

    vkw::ShaderModulePackPtr bg_vert_shader_pack;
    vkw::ShaderModulePackPtr bg_frag_shader_pack;
    vkw::ShaderModulePackPtr imgui_vert_shader_pack;
    vkw::ShaderModulePackPtr imgui_frag_shader_pack;

    vkw::BufferPackPtr unif_buf_pack;
    UnifBuf unif_buf;

    uint8_t* font_pixel_p = nullptr;
    size_t font_pixel_size = 0;
    vkw::ImagePackPtr font_img_pack;
    vkw::TexturePackPtr font_tex_pack;
    bool is_font_tex_sent = false;
    vkw::BufferPackPtr font_buf_pack;

    vkw::DescSetPackPtr imgui_desc_set_pack;
    vkw::WriteDescSetPackPtr imgui_write_desc_set_pack;
    vkw::DescSetPackPtr bg_desc_set_pack;
    vkw::WriteDescSetPackPtr bg_write_desc_set_pack;

    vk::UniqueSampler bg_sampler;

    vk::Format dst_img_format = vk::Format::eUndefined;
    vk::ImageLayout dst_final_layout = vk::ImageLayout::eUndefined;
    vk::ImageView bg_img_view;
    vk::ImageLayout bg_img_layout;

    vkw::RenderPassPackPtr render_pass_pack;
    vkw::PipelinePackPtr bg_pipeline_pack;
    vkw::PipelinePackPtr imgui_pipeline_pack;

    using FrameBufKey = std::tuple<const vk::ImageView*, const vk::Extent2D*>;
    std::map<FrameBufKey, vkw::FrameBufferPackPtr> frame_buf_map;

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
// ------------------------------- ImGui Utility -------------------------------
// -----------------------------------------------------------------------------
ImVec2 ObtainImDrawSize(ImDrawData* draw_data) {
    auto fb_width_f = draw_data->DisplaySize.x * draw_data->FramebufferScale.x;
    auto fb_height_f = draw_data->DisplaySize.y * draw_data->FramebufferScale.y;
    return {fb_width_f, fb_height_f};
}

bool UpdateVtxIdxBufs(ImDrawData* draw_data) {
    auto&& physical_device = *g_ctx.physical_device_p;
    auto&& device = *g_ctx.device_p;

    // Create Host Visible Buffers
    const size_t& vtx_size =
            static_cast<size_t>(draw_data->TotalVtxCount) * sizeof(ImDrawVert);
    const size_t& idx_size =
            static_cast<size_t>(draw_data->TotalIdxCount) * sizeof(ImDrawIdx);
    if (vtx_size == 0 || idx_size == 0) {
        return false;  // Failed
    }
    if (vtx_size != g_ctx.vtx_size) {  // Create Vertex Buffer
        g_ctx.vtx_size = vtx_size;
        g_ctx.vtx_buf_pack =
                vkw::CreateBufferPack(physical_device, device, vtx_size,
                                      vk::BufferUsageFlagBits::eVertexBuffer,
                                      vkw::HOST_VISIB_COHER_PROPS);
    }
    if (idx_size != g_ctx.idx_size) {  // Create Index Buffer
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

    return true;
}

void UpdateFontTex(const vk::UniqueCommandBuffer& dst_cmd_buf) {
    if (g_ctx.is_font_tex_sent) {
        // Release transferring resources
        g_ctx.font_buf_pack = nullptr;

        return;  // Already created
    }
    g_ctx.is_font_tex_sent = true;

    auto&& physical_device = *g_ctx.physical_device_p;
    auto&& device = *g_ctx.device_p;

    // Create transferring buffer
    g_ctx.font_buf_pack = vkw::CreateBufferPack(
            physical_device, device, g_ctx.font_pixel_size,
            vk::BufferUsageFlagBits::eTransferSrc, vkw::HOST_VISIB_COHER_PROPS);

    // Send from CPU to buffer (TODO: Async)
    SendToDevice(device, g_ctx.font_buf_pack, g_ctx.font_pixel_p,
                 g_ctx.font_pixel_size);

    // Copy from buffer to image
    vkw::CopyBufferToImage(dst_cmd_buf, g_ctx.font_buf_pack,
                           g_ctx.font_img_pack);
}

vkw::FrameBufferPackPtr UpdateRenderPipeline(
        const vk::Format& dst_img_format, const vk::ImageView& dst_img_view,
        const vk::Extent2D& dst_img_size,
        const vk::ImageLayout& dst_final_layout,
        const vk::ImageView& bg_img_view,
        const vk::ImageLayout& bg_img_layout) {
    auto&& device = *g_ctx.device_p;

    const bool needs_update = g_ctx.dst_img_format != dst_img_format ||
                              g_ctx.dst_final_layout != dst_final_layout ||
                              g_ctx.bg_img_view != bg_img_view ||
                              g_ctx.bg_img_layout != bg_img_layout;
    if (needs_update) {
        g_ctx.dst_img_format = dst_img_format;
        g_ctx.dst_final_layout = dst_final_layout;
        g_ctx.bg_img_view = bg_img_view;
        g_ctx.bg_img_layout = bg_img_layout;

        if (bg_img_view) {
            // Bind descriptor set with actual buffer (BG)
            g_ctx.bg_write_desc_set_pack = vkw::CreateWriteDescSetPack();
            std::vector<vk::DescriptorImageInfo> desc_img_infos = {
                    {g_ctx.bg_sampler.get(), bg_img_view, bg_img_layout}};
            vkw::AddWriteDescSet(g_ctx.bg_write_desc_set_pack,
                                 g_ctx.bg_desc_set_pack, 0, desc_img_infos);
            vkw::UpdateDescriptorSets(device, g_ctx.bg_write_desc_set_pack);
        }

        // Create render pass
        g_ctx.render_pass_pack = vkw::CreateRenderPassPack();
        // Add color attachment
        vkw::AddAttachientDesc(g_ctx.render_pass_pack, dst_img_format,
                               vk::AttachmentLoadOp::eLoad,
                               vk::AttachmentStoreOp::eStore, dst_final_layout);
        // Add subpass
        if (bg_img_view) {
            vkw::AddSubpassDesc(
                    g_ctx.render_pass_pack, {},
                    {{0, vk::ImageLayout::eColorAttachmentOptimal}});
            vkw::AddSubpassDepend(
                    g_ctx.render_pass_pack,
                    {0, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                     vk::AccessFlagBits::eColorAttachmentWrite},
                    {1, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                     vk::AccessFlagBits::eColorAttachmentRead},
                    vk::DependencyFlagBits::eByRegion);
        }
        vkw::AddSubpassDesc(g_ctx.render_pass_pack, {},
                            {{0, vk::ImageLayout::eColorAttachmentOptimal}});
        // Create render pass instance
        vkw::UpdateRenderPass(device, g_ctx.render_pass_pack);

        if (bg_img_view) {
            // Create pipeline (BG)
            vkw::PipelineInfo bg_pipeline_info;
            bg_pipeline_info.color_blend_infos.resize(1);
            bg_pipeline_info.depth_test_enable = false;
            g_ctx.bg_pipeline_pack = vkw::CreateGraphicsPipeline(
                    device,
                    {g_ctx.bg_vert_shader_pack, g_ctx.bg_frag_shader_pack}, {},
                    {}, bg_pipeline_info, {g_ctx.bg_desc_set_pack},
                    g_ctx.render_pass_pack, 0);
        }
        // Create pipeline (ImGui)
        vkw::PipelineColorBlendAttachInfo imgui_pipeine_blend_info;
        imgui_pipeine_blend_info.blend_enable = true;
        vkw::PipelineInfo imgui_pipeline_info;
        imgui_pipeline_info.color_blend_infos = {imgui_pipeine_blend_info};
        imgui_pipeline_info.depth_test_enable = false;
        imgui_pipeline_info.face_culling = vk::CullModeFlagBits::eNone;
        g_ctx.imgui_pipeline_pack = vkw::CreateGraphicsPipeline(
                device,
                {g_ctx.imgui_vert_shader_pack, g_ctx.imgui_frag_shader_pack},
                {{0, sizeof(ImDrawVert), vk::VertexInputRate::eVertex}},
                {{0, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, pos)},
                 {1, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, uv)},
                 {2, 0, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col)}},
                imgui_pipeline_info, {g_ctx.imgui_desc_set_pack},
                g_ctx.render_pass_pack, bg_img_view ? 1 : 0);
    }

    // Create frame buffer
    const Context::FrameBufKey& key = {&dst_img_view, &dst_img_size};
    auto& frame_buf = g_ctx.frame_buf_map[key];
    if (!frame_buf || needs_update) {
        // Create & Register
        frame_buf = CreateFrameBuffer(device, g_ctx.render_pass_pack,
                                      {dst_img_view}, dst_img_size);
    }
    return frame_buf;
}

void UpdateUnifBuf(ImDrawData* draw_data) {
    auto&& device = *g_ctx.device_p;

    // Send to uniform buffer
    auto& unif_buf = g_ctx.unif_buf;
    unif_buf.scale[0] = 2.f / draw_data->DisplaySize.x;
    unif_buf.scale[1] = 2.f / draw_data->DisplaySize.y;
    unif_buf.shift[0] = -1.f - draw_data->DisplayPos.x * unif_buf.scale[0];
    unif_buf.shift[1] = -1.f - draw_data->DisplayPos.y * unif_buf.scale[1];
    vkw::SendToDevice(device, g_ctx.unif_buf_pack, &unif_buf, sizeof(UnifBuf));
}

void RecordDrawCmds(const vk::UniqueCommandBuffer& dst_cmd_buf,
                    ImDrawData* draw_data, const ImVec2& draw_size,
                    const vkw::FrameBufferPackPtr& frame_buf) {
    // Begin render pass
    vkw::CmdBeginRenderPass(dst_cmd_buf, g_ctx.render_pass_pack, frame_buf, {});
    vkw::CmdSetViewport(dst_cmd_buf,
                        vk::Extent2D{frame_buf->width, frame_buf->height});

    // BG pass
    if (g_ctx.bg_img_view) {
        vkw::CmdSetScissor(dst_cmd_buf,
                           vk::Extent2D{frame_buf->width, frame_buf->height});
        vkw::CmdBindPipeline(dst_cmd_buf, g_ctx.bg_pipeline_pack);
        vkw::CmdBindDescSets(dst_cmd_buf, g_ctx.bg_pipeline_pack,
                             {g_ctx.bg_desc_set_pack});
        vkw::CmdDraw(dst_cmd_buf, 3);
        vkw::CmdNextSubPass(dst_cmd_buf);
    }
    // ImGui pass
    vkw::CmdBindPipeline(dst_cmd_buf, g_ctx.imgui_pipeline_pack);
    vkw::CmdBindDescSets(dst_cmd_buf, g_ctx.imgui_pipeline_pack,
                         {g_ctx.imgui_desc_set_pack}, {0});
    vkw::CmdBindVertexBuffers(dst_cmd_buf, 0, {g_ctx.vtx_buf_pack});
    vkw::CmdBindIndexBuffer(dst_cmd_buf, g_ctx.idx_buf_pack, 0, IDX_TYPE);
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

                if (clip_rect.x < draw_size.x && clip_rect.y < draw_size.y &&
                    0 <= clip_rect.z && 0 <= clip_rect.w) {
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

    // End render pass
    vkw::CmdEndRenderPass(dst_cmd_buf);
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

}  // anonymous namespace

// -----------------------------------------------------------------------------
// -------------------------------- Interfaces ---------------------------------
// -----------------------------------------------------------------------------
IMGUI_IMPL_API bool ImGui_ImplVulkanHpp_Init() {
    // Set backend name
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_vulkanhpp";

    // Turn on fetched flag.
    int32_t width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&g_ctx.font_pixel_p, &width, &height);

    // Clear global context
    g_ctx = {};

    return true;
}

IMGUI_IMPL_API void ImGui_ImplVulkanHpp_Shutdown() {
    // Clear global context
    g_ctx = {};
}

IMGUI_IMPL_API void ImGui_ImplVulkanHpp_NewFrame(
        const vk::PhysicalDevice& physical_device,
        const vk::UniqueDevice& device) {
    if (g_ctx.physical_device_p == &physical_device &&
        g_ctx.device_p == &device) {
        // Already initialized -> Skip
        return;
    }

    // Set to global context
    g_ctx.physical_device_p = &physical_device;
    g_ctx.device_p = &device;

    // Compile shaders
    vkw::GLSLCompiler glsl_compiler;
    g_ctx.bg_vert_shader_pack = glsl_compiler.compileFromString(
            device, BG_VERT_SOURCE, vk::ShaderStageFlagBits::eVertex);
    g_ctx.bg_frag_shader_pack = glsl_compiler.compileFromString(
            device, BG_FRAG_SOURCE, vk::ShaderStageFlagBits::eFragment);
    g_ctx.imgui_vert_shader_pack = glsl_compiler.compileFromString(
            device, IMGUI_VERT_SOURCE, vk::ShaderStageFlagBits::eVertex);
    g_ctx.imgui_frag_shader_pack = glsl_compiler.compileFromString(
            device, IMGUI_FRAG_SOURCE, vk::ShaderStageFlagBits::eFragment);

    // Create Uniform Buffer
    g_ctx.unif_buf_pack =
            vkw::CreateBufferPack(physical_device, device, sizeof(UnifBuf),
                                  vk::BufferUsageFlagBits::eUniformBuffer,
                                  vkw::HOST_VISIB_COHER_PROPS);

    // Create font texture
    ImGuiIO& io = ImGui::GetIO();
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

    // Descriptor set (BG)
    g_ctx.bg_desc_set_pack = vkw::CreateDescriptorSetPack(
            device, {{vk::DescriptorType::eCombinedImageSampler, 1,
                      vk::ShaderStageFlagBits::eFragment}});  // BG texture
    // Descriptor set (ImGui)
    g_ctx.imgui_desc_set_pack = vkw::CreateDescriptorSetPack(
            device, {{vk::DescriptorType::eUniformBufferDynamic, 1,
                      vk::ShaderStageFlagBits::eVertex},  // Uniform buffer
                     {vk::DescriptorType::eCombinedImageSampler, 1,
                      vk::ShaderStageFlagBits::eFragment}});  // Font texture
    // Bind descriptor set with actual buffer (ImGui)
    g_ctx.imgui_write_desc_set_pack = vkw::CreateWriteDescSetPack();
    vkw::AddWriteDescSet(g_ctx.imgui_write_desc_set_pack,
                         g_ctx.imgui_desc_set_pack, 0, {g_ctx.unif_buf_pack});
    vkw::AddWriteDescSet(g_ctx.imgui_write_desc_set_pack,
                         g_ctx.imgui_desc_set_pack, 1,
                         {g_ctx.font_tex_pack},  // layout is still undefined.
                         {vk::ImageLayout::eShaderReadOnlyOptimal});
    vkw::UpdateDescriptorSets(device, g_ctx.imgui_write_desc_set_pack);

    // Texture Sampler (BG)
    g_ctx.bg_sampler = vkw::CreateSampler(device);
}

IMGUI_IMPL_API void ImGui_ImplVulkanHpp_RenderDrawData(
        ImDrawData* draw_data, const vk::UniqueCommandBuffer& dst_cmd_buf,
        const vk::ImageView& dst_img_view, const vk::Format& dst_img_format,
        const vk::Extent2D& dst_img_size,
        const vk::ImageLayout& dst_final_layout,
        const vk::ImageView& bg_img_view,
        const vk::ImageLayout& bg_img_layout) {
    // Reset and begin command buffer
    vkw::ResetCommand(dst_cmd_buf);
    vkw::BeginCommand(dst_cmd_buf, true);  // once command

    // Send font texture
    UpdateFontTex(dst_cmd_buf);

    // Compute framebuffer size
    const auto draw_size = ObtainImDrawSize(draw_data);
    if (draw_size.x <= 0.f || draw_size.y <= 0.f) {
        vkw::EndCommand(dst_cmd_buf);  // Empty command
        return;
    }

    // Update vertex and index buffers
    const bool upd_buf_ret = UpdateVtxIdxBufs(draw_data);
    if (!upd_buf_ret) {
        vkw::EndCommand(dst_cmd_buf);  // Empty command
        return;
    }

    // Create Rendering Pipeline
    auto frame_buf =
            UpdateRenderPipeline(dst_img_format, dst_img_view, dst_img_size,
                                 dst_final_layout, bg_img_view, bg_img_layout);

    // Update uniform buffer
    UpdateUnifBuf(draw_data);

    // Record commands
    RecordDrawCmds(dst_cmd_buf, draw_data, draw_size, frame_buf);

    vkw::EndCommand(dst_cmd_buf);
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
