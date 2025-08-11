#ifndef IMR_SHADER_PRIVATE_H
#define IMR_SHADER_PRIVATE_H

#include "imr_private.h"

namespace imr {

using SPIRVModule = std::vector<uint32_t>;
SPIRVModule load_spirv_module(const std::string& filename);

/// Generates set layouts and pipeline layouts from the SPIR-V module by parsing it as a shady module and using the IR inspection API to find bindings and such
struct ReflectedLayout {
    VkShaderStageFlags stages;
    std::unordered_map<int, std::vector<VkDescriptorSetLayoutBinding>> set_bindings;
    std::vector<VkPushConstantRange> push_constants;

    ReflectedLayout() = default;
    ReflectedLayout(SPIRVModule& spirv_module, VkShaderStageFlags stage);
    ReflectedLayout(ReflectedLayout& a, ReflectedLayout& b);
};

/// Turns the ReflectedLayout into the VkDescriptorSetLayout s and VkPipelineLayout
struct PipelineLayout {
    imr::Device& device;

    std::vector<VkDescriptorSetLayout> set_layouts;
    VkPipelineLayout pipeline_layout;

    PipelineLayout(imr::Device& device, ReflectedLayout& reflected_layout);
    ~PipelineLayout();
};

struct ShaderModule::Impl {
    imr::Device& device;
    SPIRVModule spirv_module;
    VkShaderModule vk_shader_module;

    Impl(imr::Device& device, SPIRVModule&& spirv_module) noexcept(false);

    Impl(const Impl&) = delete;
    Impl(Impl&&) = default;

    ~Impl();
};

struct ShaderEntryPoint::Impl {
    ShaderModule& module;
    VkShaderStageFlagBits stage;
    std::string name;
    std::unique_ptr<ReflectedLayout> reflected;

    Impl(ShaderModule& module, VkShaderStageFlagBits stage, const std::string& entrypoint_name);
    ~Impl();
};

struct ComputePipeline::Impl {
    Device& device;
    std::unique_ptr<PipelineLayout> layout;
    VkPipeline pipeline;

    std::unique_ptr<ShaderModule> module;
    std::unique_ptr<ShaderEntryPoint> entry_point;

    Impl(imr::Device& device, std::unique_ptr<ShaderModule>&& module, std::unique_ptr<ShaderEntryPoint>&& ep);
    Impl(imr::Device& device, ShaderEntryPoint& entry_point);
    ~Impl();
};

struct RayTracingPipeline::Impl {
    Device& device_;
    std::unique_ptr<PipelineLayout> layout;
    VkPipeline pipeline;

    std::vector<std::unique_ptr<ShaderEntryPoint>> entry_points;

    // Shader Binding Table
    std::unique_ptr<Buffer> sbt_buffer;
    VkStridedDeviceAddressRegionKHR raygen_region{};
    VkStridedDeviceAddressRegionKHR miss_region{};
    VkStridedDeviceAddressRegionKHR hit_region{};
    VkStridedDeviceAddressRegionKHR callable_region{};

    void create_shader_binding_table();

    Impl(imr::Device& device,
         std::vector<ShaderEntryPoint*>&& stages);

    ~Impl();
};

struct GraphicsPipeline::Impl {
    Impl(Device& device, std::vector<ShaderEntryPoint*>&& stages, RenderTargetsState, StateBuilder);

    ~Impl();

    Device& device_;
    std::unique_ptr<PipelineLayout> layout;
    ReflectedLayout final_layout;
    VkPipeline pipeline;
};

}

#endif
