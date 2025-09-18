#ifndef IMR_H
#define IMR_H

#include "vulkan/vulkan_core.h"
#include "GLFW/glfw3.h"
#include "VkBootstrap.h"

#include <functional>
#include <memory>
#include <optional>

#include <cstdio>

#define CHECK_VK(op, else) if (op != VK_SUCCESS) { fprintf(stderr, "Check failed at %s\n", #op); else; }

template<typename T>
/// A lot of Vulkan calls require pointers to structures, but often the data we want to provide is just some expression.
/// This utility method is used to leak the address of a temporary, which in C++ is specified to live for the duration of the statement
inline T* tmpPtr(T&& t) { return &t; }

namespace imr {

struct Swapchain;
struct AccelerationStructure;

struct Context {
    Context(std::function<void(vkb::InstanceBuilder&)>&& instance_custom = [](auto&) {});
    Context(Context&) = delete;
    ~Context();

    vkb::Instance instance;
    vkb::InstanceDispatchTable dispatch;

    std::vector<vkb::PhysicalDevice> available_devices(std::function<void(vkb::PhysicalDeviceSelector&)>&& device_custom = [](auto&) {});
};

struct Device {
    Device(Context&, std::function<void(vkb::PhysicalDeviceSelector&)>&& device_custom = [](auto&) {});
    Device(Context&, vkb::PhysicalDevice);
    Device(Device&) = delete;
    ~Device();

    Context& context;

    vkb::PhysicalDevice physical_device;
    vkb::Device device;

    VkQueue main_queue;
    uint32_t main_queue_idx;

    VkCommandPool pool;

    vkb::DispatchTable dispatch;

    void executeCommandsSync(std::function<void(VkCommandBuffer)>);

    class Impl;
    std::unique_ptr<Impl> _impl;
};

struct Buffer {
    Buffer(Device&, size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_property = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, void* initial_data = nullptr);
    Buffer(Buffer&) = delete;
    ~Buffer();

    size_t const size;
    VkBuffer handle;
    /// query 64-bit virtual address of the buffer on the GPU
    VkDeviceAddress device_address();
    /// Managed by the allocator, required for mapping the buffer
    VkDeviceMemory memory;
    size_t memory_offset;

    void uploadDataSync(uint64_t offset, uint64_t size, void* data);

    struct Impl;
    std::unique_ptr<Impl> _impl;
};

/// Deals with the common use-cases for images, allocating memory for you and tracking properties.
/// Does not track image layouts for you, much of the framework assumes VK_IMAGE_LAYOUT_GENERAL
struct Image {
    VkImage handle() const;

    VkImageType type() const;
    VkExtent3D size() const;
    VkFormat format() const;

    Image(Device&, VkImageType dim, VkExtent3D size, VkFormat format, VkImageUsageFlagBits usage);
    Image(Image&) = delete;
    Image(Image&&);
    ~Image();

    VkImageView whole_image_view();
    VkImageSubresourceRange whole_image_subresource_range() const;
    VkImageSubresourceLayers whole_image_subresource_layers() const;

    struct Impl;
    Image(Impl&&);
private:
    std::unique_ptr<Impl> _impl;
};

struct ShaderModule {
    ShaderModule(imr::Device& device, std::string&& filename) noexcept(false);
    ShaderModule(const ShaderModule&) = delete;
    ShaderModule(ShaderModule&&) = default;

    VkShaderModule vk_shader_module() const;

    ~ShaderModule();

    struct Impl;
    std::unique_ptr<Impl> _impl;
};

struct ShaderEntryPoint {
    ShaderEntryPoint(ShaderModule& module, VkShaderStageFlagBits stage, const std::string& entrypoint_name);
    ~ShaderEntryPoint();

    VkShaderStageFlagBits stage() const;
    const std::string& name() const;
    const ShaderModule& module() const;

    struct Impl;
    std::unique_ptr<Impl> _impl;
};

/// Helper class that allocates, populates and binds descriptor sets for us
/// Since it owns the descriptor sets internally, it must live as they are in use
/// Therefore, it should not be stack-allocated inside e.g. the beginFrame lambda !
struct DescriptorBindHelper {
    class Impl;

    explicit DescriptorBindHelper(std::unique_ptr<Impl>&&);
    DescriptorBindHelper(DescriptorBindHelper&) = delete;
    ~DescriptorBindHelper();

    void set_storage_image(uint32_t set, uint32_t binding, VkImageView, uint32_t array_element = 0);
    void set_sampler(uint32_t set, uint32_t binding, VkSampler, uint32_t array_element = 0);
    void set_texture_image(uint32_t set, uint32_t binding, VkImageView, uint32_t array_element = 0);
    void set_acceleration_structure(uint32_t set, uint32_t binding, imr::AccelerationStructure&);
    void set_uniform_buffer(uint32_t set, uint32_t binding, imr::Buffer&, uint64_t offset = 0);
    void set_storage_buffer(uint32_t set, uint32_t binding, imr::Buffer&, uint64_t offset = 0);
    void commit(VkCommandBuffer);

    std::unique_ptr<Impl> _impl;
};

struct ComputePipeline {
    ComputePipeline(Device&, std::string&& spirv_filename, std::string&& entrypoint_name = "main");
    ComputePipeline(ComputePipeline&) = delete;
    ~ComputePipeline();

    VkPipeline pipeline() const;
    VkPipelineLayout layout() const;
    VkDescriptorSetLayout set_layout(unsigned) const;

    DescriptorBindHelper* create_bind_helper();

    struct Impl;
    std::unique_ptr<Impl> _impl;
};

struct RayTracingPipeline {
    struct HitShadersTriple {
        ShaderEntryPoint* closest_hit = nullptr;
        ShaderEntryPoint* any_hit = nullptr;
        ShaderEntryPoint* intersection = nullptr;
    };

    // needs a raygen, hit shader groups (at least one), miss shaders (at least one) then optionally callables
    RayTracingPipeline(imr::Device&, ShaderEntryPoint* raygen, std::vector<HitShadersTriple> hit_shaders, std::vector<ShaderEntryPoint*> miss_shaders, std::vector<ShaderEntryPoint*> callables = {});

    RayTracingPipeline(const RayTracingPipeline&) = delete;
    ~RayTracingPipeline();

    VkPipeline pipeline() const;
    VkPipelineLayout layout() const;

    uint32_t getHandleSizeAligned() const;

    DescriptorBindHelper* create_bind_helper();

    void traceRays(VkCommandBuffer cmdbuf, uint16_t width, uint16_t height, uint16_t depth = 1);

    struct Impl;
    std::unique_ptr<Impl> _impl;
};

struct GraphicsPipeline {
    struct RenderTarget {
        VkFormat format;
        VkPipelineColorBlendAttachmentState blending = {};
    };

    struct RenderTargetsState {
        std::vector<RenderTarget> color;
        std::optional<RenderTarget> depth;
        VkPipelineColorBlendStateCreateInfo all_targets_blend_state = {};
    };

    struct StateBuilder {
        std::optional<VkPipelineVertexInputStateCreateInfo>      vertexInputState;
        std::optional<VkPipelineInputAssemblyStateCreateInfo>    inputAssemblyState;
        std::optional<VkPipelineTessellationStateCreateInfo>     tessellationState;
        std::optional<VkPipelineViewportStateCreateInfo>         viewportState;
        std::optional<VkPipelineRasterizationStateCreateInfo>    rasterizationState;
        std::optional<VkPipelineMultisampleStateCreateInfo>      multisampleState;
        std::optional<VkPipelineDepthStencilStateCreateInfo>     depthStencilState;
    };

    // These helpers contain sensible defaults for most pieces of state
    static VkPipelineViewportStateCreateInfo one_dynamically_sized_viewport();
    static VkPipelineVertexInputStateCreateInfo no_vertex_input();
    static VkPipelineInputAssemblyStateCreateInfo simple_triangle_input_assembly();
    static VkPipelineMultisampleStateCreateInfo one_spp();
    static VkPipelineRasterizationStateCreateInfo solid_filled_polygons();
    static VkPipelineDepthStencilStateCreateInfo simple_depth_testing();

    GraphicsPipeline(Device&, std::vector<ShaderEntryPoint*>&& stages, RenderTargetsState, StateBuilder);
    GraphicsPipeline(const GraphicsPipeline&) = delete;
    ~GraphicsPipeline();

    VkPipeline pipeline() const;
    VkPipelineLayout layout() const;
    VkDescriptorSetLayout set_layout(unsigned) const;

    DescriptorBindHelper* create_bind_helper();

    struct Impl;
    std::unique_ptr<Impl> _impl;
};

// Ray tracing acceleration structure
struct AccelerationStructure {
    AccelerationStructure(Device&);
    AccelerationStructure(AccelerationStructure&) = delete;
    ~AccelerationStructure();

    VkAccelerationStructureKHR handle() const;
    VkDeviceAddress deviceAddress() const;

    struct TriangleGeometry {
        VkDeviceAddress vertices;
        VkDeviceAddress indices;
        size_t vertices_count;
        uint32_t prim_count;
        VkTransformMatrixKHR matrix;
    };

    void createBottomLevelAccelerationStructure(std::vector<TriangleGeometry>);
    void createTopLevelAccelerationStructure(std::vector<std::tuple<VkTransformMatrixKHR, AccelerationStructure*>>& bottomLevelAS);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

struct Swapchain {
    Swapchain(Device&, GLFWwindow* window);
    ~Swapchain();

    Device& device() const;
    VkFormat format() const;

    /// Approximate FPS cap, avoids melting your GPU on a trivial scene
    int maxFps = 999;

    struct Frame {
        void presentFromBuffer(VkBuffer buffer, VkFence signal_when_reusable, std::optional<VkSemaphore> sem);
        void presentFromImage(VkImage image, VkFence signal_when_reusable, std::optional<VkSemaphore> sem, VkImageLayout src_layout = VK_IMAGE_LAYOUT_GENERAL, std::optional<VkExtent2D> image_size = std::nullopt);

        size_t id;
        Image& image() const;
        VkSemaphore swapchain_image_available;
        VkSemaphore signal_when_ready;
        void queuePresent();

        void addCleanupFence(VkFence fence);
        void addCleanupAction(std::function<void(void)>&& fn);

        void withRenderTargets(VkCommandBuffer, std::vector<Image*> color_images, Image* depth, std::function<void()> f);

        class Impl;
        std::unique_ptr<Impl> _impl;

        Frame(Impl&&);
        Frame(Frame&) = delete;
        ~Frame();
    };

    /// API to obtain a swapchain image, do some rendering with it and then ultimately queuePresent it.
    /// You have to consume the `swapchain_image_available` semaphore
    /// You have to signal the `signal_when_ready` semaphore
    /// You have to call queuePresent() when you're done
    void beginFrame(std::function<void(Swapchain::Frame&)>&& fn);

    struct SimplifiedRenderContext {
        virtual Image& image() const = 0;
        virtual VkCommandBuffer cmdbuf() const = 0;
        virtual Swapchain::Frame& frame() const = 0;

        virtual void addCleanupAction(std::function<void(void)>&& fn) = 0;
    };

    /// Simplified API to draw a frame, deals with cmdbuf allocation, recording and submission, as well as layout transitions in and out of VK_IMAGE_LAYOUT_GENERAL for the swapchain image
    void renderFrameSimplified(std::function<void(SimplifiedRenderContext&)>&& fn);

    void resize();

    /// Waits until all the in-flight frames are done and runs their cleanup jobs
    void drain();

    class Impl;
    std::unique_ptr<Impl> _impl;
};

struct FpsCounter {
    FpsCounter();
    FpsCounter(FpsCounter&) = delete;
    ~FpsCounter();

    void tick();
    int average_fps();
    float average_frametime();
    void updateGlfwWindowTitle(GLFWwindow*);

    class Impl;
    std::unique_ptr<Impl> _impl;
};

}

#endif
