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

    // --- Build ---

    template<typename T, typename... Args>
    T* addPass(const std::string& name, Args&&... args)
    {
        auto pass = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = pass.get();
        addPassInternal(name, std::move(pass));
        return ptr;
    }

    // Connect output slot of one pass to input slot of another.
    // Usage: graph->addEdge(vbufPass->vBuffer, shadingPass->vBuffer);
    void addEdge(const RenderGraphSlot& src, const RenderGraphSlot& dst);

    // Import an external texture (backBuffer, envMap, etc.)
    // Returns a slot handle that can be used in addEdge.
    RenderGraphSlot importTexture(const std::string& name, rhi::ITexture* texture,
                                   rhi::ResourceState currentState = rhi::ResourceState::Undefined);

    // Mark which slot is the final output (for dead-pass culling).
    void markOutput(const RenderGraphSlot& slot);

    // --- Compile & Execute ---

    void compile(uint32_t backBufferWidth, uint32_t backBufferHeight);
    void execute(rhi::ICommandEncoder* encoder);
    void resize(uint32_t width, uint32_t height);
    void onRenderUI();

    // Get the physical texture backing a slot (after compile)
    rhi::ITexture* getTexture(const RenderGraphSlot& slot) const;

private:
    void addPassInternal(const std::string& name, std::unique_ptr<RenderPass> pass);

    // Physical resource — one per unique connected resource
    struct PhysicalResource
    {
        uint32_t id;
        TextureResourceDesc desc;
        Slang::ComPtr<rhi::ITexture> texture;
        rhi::TextureUsage accumulatedUsage = rhi::TextureUsage::None;
        bool external = false;
        rhi::ResourceState externalState = rhi::ResourceState::Undefined;
    };

    // An edge in the graph
    struct Edge
    {
        uint32_t srcPass, dstPass;
        std::string srcSlot, dstSlot;
    };

    // Per-pass compiled info
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

    // Internal helpers
    uint32_t getPassIndex(const std::string& name) const;
    uint32_t getOrCreatePhysicalResource(uint32_t passIndex, const std::string& slotName);
    void assignPhysicalResources();
    void createGpuResources();
    void topologicalSort();
    void planBarriers();

    static rhi::TextureUsage accessToUsage(PassSlot::Access access);
    static rhi::ResourceState accessToState(PassSlot::Access access);

    // Data
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

    // slot → physical resource mapping: key = "passIndex:slotName"
    std::unordered_map<std::string, uint32_t> slotToPhysical;

    // Output
    uint32_t outputPass = UINT32_MAX;
    std::string outputSlot;
};
