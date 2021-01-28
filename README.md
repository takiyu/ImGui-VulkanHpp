# ImGui-VulkanHpp
ImGui Backend Implementation for VulkanHpp.

## Usage
```cpp
    // Initialize Vulkan
    ...

    // Initialize ImGui
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkanHpp_Init();

    // Rendering loop
    while (true) {
        // Render 3D Objects
        ...

        // Start ImGui Frame
        ImGui_ImplVulkanHpp_NewFrame(physical_device, device);
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Create ImGui window
        ImGui::ShowDemoWindow();

        // Render ImGui (Stack commands on `imgui_cmd_buf`)
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        ImGui_ImplVulkanHpp_RenderDrawData(
                draw_data, imgui_cmd_buf, swapchain_img_view,
                swapchain_img_format, swapchain_img_size);

        // Submit ImGui Rendering command
        ...

        glfwPollEvents();
    }

    // Clean up ImGui
    ImGui_ImplVulkanHpp_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
```

## Examples
### Standard
See `examples/main_glfw.cpp`.

<img src="https://raw.githubusercontent.com/takiyu/ImGui-VulkanHpp/master/data/example.png">

Most of latest GPUs could be worked on. (RTX2080, ...)
This is the fastest version.

### With Background Drawing
See `examples/main_glfw_bg.cpp`.

Supporting drawing background image for GTX10xx (GTX1060, GTX1080, ...),
because their SwapChain's behavior is something strange.

To solve this problem, there is background drawing mode.
Please pass image view and layout into `ImGui_ImplVulkanHpp_RenderDrawData`;

(If someone knows why standard example dose not work on GTX10xx,
 please open issues.)
