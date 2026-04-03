#pragma once

#include "RenderPass.h"
#include "ResourceDesc.h"

#include <slang-rhi.h>
#include <slang-com-ptr.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class RenderGraph
{
public:
    explicit RenderGraph(rhi::IDevice* device);

    template<typename T, typename... Args>
    T* AddPass(const std::string& name, Args&&... args)
    {
        auto pass = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = pass.get();
        AddPassInternal(name, std::move(pass));
        return ptr;
    }

    void AddEdge(const RenderGraphSlot& src, const RenderGraphSlot& dst);

    RenderGraphSlot ImportTexture(const std::string& name, rhi::ITexture* texture,
                                   rhi::ResourceState currentState = rhi::ResourceState::Undefined);

    void MarkOutput(const RenderGraphSlot& slot);

    void Compile(uint32_t backBufferWidth, uint32_t backBufferHeight);
    void Execute(rhi::ICommandEncoder* encoder);
    void Resize(uint32_t width, uint32_t height);
    void OnRenderUI();

    rhi::ITexture* GetTexture(const RenderGraphSlot& slot) const;

private:
    void AddPassInternal(const std::string& name, std::unique_ptr<RenderPass> pass);

    struct PhysicalResource
    {
        uint32_t id;
        TextureResourceDesc desc;
        Slang::ComPtr<rhi::ITexture> texture;
        rhi::TextureUsage accumulatedUsage = rhi::TextureUsage::None;
        bool external = false;
        rhi::ResourceState externalState = rhi::ResourceState::Undefined;
    };

    struct Edge
    {
        uint32_t srcPass, dstPass;
        std::string srcSlot, dstSlot;
    };

    struct BarrierOp
    {
        uint32_t physicalId;
        rhi::ResourceState newState;
    };

    struct CompiledPass
    {
        uint32_t passIndex;
        std::vector<BarrierOp> barriers;
    };

    uint32_t GetPassIndex(const std::string& name) const;
    uint32_t GetOrCreatePhysicalResource(uint32_t passIndex, const std::string& slotName);
    void AssignPhysicalResources();
    void CreateGpuResources();
    void TopologicalSort();
    void PlanBarriers();

    static rhi::TextureUsage AccessToUsage(PassSlot::Access access);
    static rhi::ResourceState AccessToState(PassSlot::Access access);

    Slang::ComPtr<rhi::IDevice> device;
    uint32_t bbWidth = 0, bbHeight = 0;

    struct PassEntry
    {
        std::string name;
        std::unique_ptr<RenderPass> pass;
    };

    std::vector<PassEntry> passes;
    std::unordered_map<std::string, uint32_t> passIndexByName;
    std::vector<Edge> edges;
    std::vector<PhysicalResource> physicalResources;
    std::vector<CompiledPass> executionOrder;

    std::unordered_map<std::string, uint32_t> slotToPhysical;

    uint32_t outputPass = UINT32_MAX;
    std::string outputSlot;
};
