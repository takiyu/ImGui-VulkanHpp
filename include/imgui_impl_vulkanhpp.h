#ifndef IMGUI_IMPL_VULKANHPP_H_20201230
#define IMGUI_IMPL_VULKANHPP_H_20201230

#include <imgui.h>  // IMGUI_IMPL_API

#include <vulkan/vulkan.hpp>

IMGUI_IMPL_API bool ImGui_ImplVulkanHpp_Init(
        const vk::PhysicalDevice& physical_device,
        const vk::UniqueDevice& device);
IMGUI_IMPL_API void ImGui_ImplVulkanHpp_Shutdown();
IMGUI_IMPL_API void ImGui_ImplVulkanHpp_NewFrame();
IMGUI_IMPL_API void ImGui_ImplVulkanHpp_RenderDrawData(
        ImDrawData* draw_data, const vk::UniqueCommandBuffer& dst_cmd_buf,
        const vk::ImageView& dst_img_view, const vk::Format& dst_img_format,
        const vk::Extent2D& dst_img_size,
        const vk::ImageLayout& dst_img_layout =
                vk::ImageLayout::ePresentSrcKHR);

#endif /* end of include guard */
