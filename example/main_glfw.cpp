#include <vkw/warning_suppressor.h>

BEGIN_VKW_SUPPRESS_WARNING
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkanhpp.h>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
END_VKW_SUPPRESS_WARNING

#include <vkw/vkw.h>

#include <iostream>
#include <string>

#include "cube.h"

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
const std::string VERT_SOURCE = R"(
#version 460
    layout(binding = 0) uniform UniformBuffer {
    mat4 mvp;
}
uniform_buf;
layout(location = 0) in vec4 pos;
layout(location = 1) in vec4 col;
layout(location = 0) out vec4 vtx_col;
void main() {
    gl_Position = uniform_buf.mvp * pos;
    vtx_col = col;
}
)";

const std::string FRAG_SOURCE = R"(
#version 460
layout (location = 0) in vec4 vtx_col;
layout(location = 0) out vec4 frag_col;
void main() {
    frag_col = vtx_col;
}
)";

struct UniformBuffer {
    glm::mat4 mvp;
};

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
class RotatedMVPC {
public:
    void resize(const vk::Extent2D& screen_size) {
        m_aspect = static_cast<float>(screen_size.width) /
                   static_cast<float>(screen_size.height);
    }

    glm::mat4 next() {
        // Update rotation matrix
        m_rot_mat = glm::rotate(0.001f, glm::vec3(1.f, 0.f, 0.f)) * m_rot_mat;
        // Build MVP matrix
        auto proj_mat = glm::perspective(FOV, m_aspect, NEAR, FAR);
        return CLIP_MAT * proj_mat * VIEW_MAT * m_rot_mat * MODEL_MAT;
    }

private:
    // Constants
    const glm::mat4 MODEL_MAT = glm::mat4(1.0f);
    const glm::mat4 VIEW_MAT =
            glm::lookAt(glm::vec3(5.f, 3.f, 10.f), glm::vec3(0.f, 0.f, 0.f),
                        glm::vec3(0.f, 1.f, 0.f));
    const float FOV = glm::radians(45.f);
    const float NEAR = 0.1f;
    const float FAR = 100.f;
    const glm::mat4 CLIP_MAT = {
            1.f, 0.f, 0.f,  0.f, 0.f, -1.f, 0.f,  0.f,
            0.f, 0.f, 0.5f, 0.f, 0.f, 0.f,  0.5f, 1.f};  // Vulkan clip space
                                                         // has inverted Y and
                                                         // half Z.

    // Dynamics
    float m_aspect = 1.f;
    glm::mat4 m_rot_mat = glm::mat4(1.f);
};

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
int main(int argc, char const* argv[]) {
    (void)argc, (void)argv;

    // Initialize with display environment
    const bool DISPLAY_ENABLE = true;
    const bool DEBUG_ENABLE = true;
    const bool VSYNC_ENABLE = false;
    const uint32_t N_QUEUES = 1;
    const std::string TITLE_STR = "ImGui-VulkanHpp Example";

    // -------------------------------------------------------------------------
    // -------------------------- Common Environment ---------------------------
    // -------------------------------------------------------------------------
    // Create window
    auto window = vkw::InitGLFWWindow(TITLE_STR);
    // Create instance
    auto instance = vkw::CreateInstance(TITLE_STR, 1, "ImGui-VulkanHpp", 0,
                                        DEBUG_ENABLE, DISPLAY_ENABLE);
    // Get a physical_device
    auto physical_device = vkw::GetFirstPhysicalDevice(instance);
    // Create surface
    auto surface = vkw::CreateSurface(instance, window);
    auto surface_format = vkw::GetSurfaceFormat(physical_device, surface);
    // Select queue family
    uint32_t queue_family_idx =
            vkw::GetGraphicPresentQueueFamilyIdx(physical_device, surface);
    // Create device
    auto device = vkw::CreateDevice(queue_family_idx, physical_device, N_QUEUES,
                                    DISPLAY_ENABLE);
    // Get queues
    auto queues = vkw::GetQueues(device, queue_family_idx, N_QUEUES);
    // Depth format
    const auto DEPTH_FORMAT = vk::Format::eD16Unorm;

    // -------------------------------------------------------------------------
    // --------------------- Scene Specific Static Objects ---------------------
    // -------------------------------------------------------------------------
    // Shader compile
    vkw::GLSLCompiler glsl_compiler;
    auto vert_shader_module_pack = glsl_compiler.compileFromString(
            device, VERT_SOURCE, vk::ShaderStageFlagBits::eVertex);
    auto frag_shader_module_pack = glsl_compiler.compileFromString(
            device, FRAG_SOURCE, vk::ShaderStageFlagBits::eFragment);
    // Uniform buffer
    auto uniform_buf_pack = vkw::CreateBufferPack(
            physical_device, device, sizeof(UniformBuffer),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vkw::HOST_VISIB_COHER_PROPS);
    // Descriptor set for Uniform buffer
    auto desc_set_pack = vkw::CreateDescriptorSetPack(
            device, {{vk::DescriptorType::eUniformBufferDynamic, 1,
                      vk::ShaderStageFlagBits::eVertex}});
    auto write_desc_set_pack = vkw::CreateWriteDescSetPack();
    vkw::AddWriteDescSet(write_desc_set_pack, desc_set_pack, 0,
                         {uniform_buf_pack});
    vkw::UpdateDescriptorSets(device, write_desc_set_pack);
    // Render pass
    auto render_pass_pack = vkw::CreateRenderPassPack();
    vkw::AddAttachientDesc(
            render_pass_pack, surface_format, vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore, vk::ImageLayout::ePresentSrcKHR);
    vkw::AddAttachientDesc(render_pass_pack, DEPTH_FORMAT,
                           vk::AttachmentLoadOp::eClear,
                           vk::AttachmentStoreOp::eDontCare,
                           vk::ImageLayout::eDepthStencilAttachmentOptimal);
    // Subpass
    vkw::AddSubpassDesc(render_pass_pack, {},
                        {{0, vk::ImageLayout::eColorAttachmentOptimal}},
                        {1, vk::ImageLayout::eDepthStencilAttachmentOptimal});
    vkw::UpdateRenderPass(device, render_pass_pack);
    // Pipeline
    vkw::PipelineInfo pipeline_info;
    pipeline_info.color_blend_infos.resize(1);
    auto pipeline_pack = vkw::CreateGraphicsPipeline(
            device, {vert_shader_module_pack, frag_shader_module_pack},
            {{0, sizeof(Vertex), vk::VertexInputRate::eVertex}},
            {{0, 0, vk::Format::eR32G32B32A32Sfloat, 0},
             {1, 0, vk::Format::eR32G32B32A32Sfloat, 16}},
            pipeline_info, {desc_set_pack}, render_pass_pack);
    // Vertex buffer
    const size_t vertex_buf_size = CUBE_VERTICES.size() * sizeof(Vertex);
    auto vertex_buf_pack =
            vkw::CreateBufferPack(physical_device, device, vertex_buf_size,
                                  vk::BufferUsageFlagBits::eVertexBuffer,
                                  vkw::HOST_VISIB_COHER_PROPS);
    vkw::SendToDevice(device, vertex_buf_pack, CUBE_VERTICES.data(),
                      vertex_buf_size);

    // -------------------------------------------------------------------------
    // ---------------------- Window Size Dynamic Objects ----------------------
    // -------------------------------------------------------------------------
    // Create swapchain
    auto present_mode = VSYNC_ENABLE ? vk::PresentModeKHR::eFifo :
                                       vk::PresentModeKHR::eImmediate;
    auto swapchain_pack = vkw::CreateSwapchainPack(
            physical_device, device, surface, surface_format,
            vk::ImageUsageFlagBits::eColorAttachment, present_mode);
    // Depth buffer
    auto depth_img_pack = vkw::CreateImagePack(
            physical_device, device, DEPTH_FORMAT, swapchain_pack->size, 1,
            vk::ImageUsageFlagBits::eDepthStencilAttachment, {},
            true,  // tiling
            vk::ImageAspectFlagBits::eDepth);
    // Frame buffer
    auto frame_buffer_packs =
            vkw::CreateFrameBuffers(device, render_pass_pack,
                                    {nullptr, depth_img_pack}, swapchain_pack);
    // Command buffer
    auto n_cmd_bufs = static_cast<uint32_t>(frame_buffer_packs.size());
    auto cube_cmd_bufs_pack =
            vkw::CreateCommandBuffersPack(device, queue_family_idx, n_cmd_bufs);
    auto imgui_cmd_bufs_pack =
            vkw::CreateCommandBuffersPack(device, queue_family_idx, n_cmd_bufs);

    // Record commands
    for (uint32_t cmd_idx = 0; cmd_idx < n_cmd_bufs; cmd_idx++) {
        auto& cmd_buf = cube_cmd_bufs_pack->cmd_bufs[cmd_idx];
        vkw::ResetCommand(cmd_buf);
        vkw::BeginCommand(cmd_buf);

        const std::array<float, 4> clear_color = {0.2f, 0.2f, 0.2f, 0.2f};
        vkw::CmdBeginRenderPass(cmd_buf, render_pass_pack,
                                frame_buffer_packs[cmd_idx],
                                {vk::ClearColorValue(clear_color),
                                 vk::ClearDepthStencilValue(1.f, 0)});
        vkw::CmdBindPipeline(cmd_buf, pipeline_pack);

        const std::vector<uint32_t> dynamic_offsets = {0};
        vkw::CmdBindDescSets(cmd_buf, pipeline_pack, {desc_set_pack},
                             dynamic_offsets);

        vkw::CmdBindVertexBuffers(cmd_buf, 0, {vertex_buf_pack});

        vkw::CmdSetViewport(cmd_buf, swapchain_pack->size);
        vkw::CmdSetScissor(cmd_buf, swapchain_pack->size);

        vkw::CmdDraw(cmd_buf, static_cast<uint32_t>(CUBE_VERTICES.size()));

        vkw::CmdEndRenderPass(cmd_buf);
        vkw::EndCommand(cmd_buf);
    }

    // -------------------------------------------------------------------------
    // ----------------------- Setup Dear ImGui context ------------------------
    // -------------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan(window.get(), true);
    ImGui_ImplVulkanHpp_Init(physical_device, *device);

    // -------------------------------------------------------------------------
    // ------------------------------- Main Loop -------------------------------
    // -------------------------------------------------------------------------
    RotatedMVPC mvpc_generator;
    mvpc_generator.resize(swapchain_pack->size);
    while (!glfwWindowShouldClose(window.get())) {
        // Update uniform buffer
        auto mvpc_mat = mvpc_generator.next();
        vkw::SendToDevice(device, uniform_buf_pack, &mvpc_mat[0],
                          sizeof(mvpc_mat));

        // Acquire swapchain image
        auto img_acquired_semaphore = vkw::CreateSemaphore(device);
        uint32_t curr_img_idx = vkw::AcquireNextImage(
                device, swapchain_pack, img_acquired_semaphore, nullptr);
        // Get frame buffer and command buffers
        auto& frame_buffer = frame_buffer_packs[curr_img_idx]->frame_buffer;
        auto& cube_cmd_buf = cube_cmd_bufs_pack->cmd_bufs[curr_img_idx];
        auto& imgui_cmd_buf = imgui_cmd_bufs_pack->cmd_bufs[curr_img_idx];

        // New frame of ImGui
        ImGui_ImplVulkanHpp_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        // Create ImGui window
        ImGui::ShowDemoWindow();
        // Render ImGui
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        ImGui_ImplVulkanHpp_RenderDrawData(draw_data, imgui_cmd_buf.get(),
                                           frame_buffer.get());

        // Submit
        auto draw_cube_semaphore = vkw::CreateSemaphore(device);
        vkw::QueueSubmit(queues[0], cube_cmd_buf, nullptr,
                         {{img_acquired_semaphore,
                           vk::PipelineStageFlagBits::eColorAttachmentOutput}},
                         {draw_cube_semaphore});
        auto draw_imgui_semaphore = vkw::CreateSemaphore(device);
        auto draw_imgui_fence = vkw::CreateFence(device);
        vkw::QueueSubmit(queues[0], imgui_cmd_buf, draw_imgui_fence,
                         {{draw_cube_semaphore,
                           vk::PipelineStageFlagBits::eColorAttachmentOutput}},
                         {draw_imgui_semaphore});

        // Present
        vkw::QueuePresent(queues[0], swapchain_pack, curr_img_idx,
                          {draw_imgui_semaphore});
        vkw::WaitForFences(device, {draw_imgui_fence});

        // Window update
        vkw::PrintFps();
        glfwPollEvents();
    }

    // Clean up ImGui
    ImGui_ImplVulkanHpp_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    return 0;
}
