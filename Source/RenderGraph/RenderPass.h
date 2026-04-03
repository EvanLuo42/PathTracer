#pragma once

#include "ResourceDesc.h"

#include <slang-rhi.h>
#include <slang-com-ptr.h>

#include <string>
#include <unordered_map>

class RenderGraph;

// A handle to a named slot on a pass, used for addEdge
struct RenderGraphSlot
{
    uint32_t passIndex = UINT32_MAX;
    std::string name;
};

struct PassSlot
{
    enum class Direction { Input, Output };
    enum class Access { ShaderResource, UnorderedAccess, RenderTarget, DepthStencil };

    Direction direction;
    Access access;
    TextureResourceDesc desc;

    uint32_t rtSlot = 0;
    rhi::LoadOp loadOp = rhi::LoadOp::Load;
    float clearColor[4] = {0, 0, 0, 0};
    float depthClear = 1.0f;
};

// Provides resolved GPU resources during Execute()
class RenderGraphResources
{
public:
    [[nodiscard]] rhi::ITexture* getTexture(const std::string& slot) const;

private:
    friend class RenderGraph;
    std::unordered_map<std::string, rhi::ITexture*> textures;
};

class RenderPass
{
public:
    virtual ~RenderPass() = default;

    virtual void Setup() = 0;
    virtual void Execute(rhi::ICommandEncoder* encoder, const RenderGraphResources& resources) = 0;
    virtual void OnRenderUI() {}
    [[nodiscard]] virtual const char* GetName() const = 0;

    [[nodiscard]] const std::unordered_map<std::string, PassSlot>& GetSlots() const { return slots; }
    [[nodiscard]] bool HasSideEffect() const { return hasSideEffect; }

protected:
    // --- Slot declaration (call in Setup) ---
    RenderGraphSlot addInput(const std::string& name,
                              PassSlot::Access access = PassSlot::Access::ShaderResource);

    RenderGraphSlot addOutput(const std::string& name,
                               rhi::Format format,
                               PassSlot::Access access = PassSlot::Access::UnorderedAccess,
                               SizePolicy sizePolicy = SizePolicy::BackBuffer(),
                               rhi::LoadOp loadOp = rhi::LoadOp::Load,
                               uint32_t rtSlot = 0);

    void markSideEffect() { hasSideEffect = true; }

    // --- Render pass helpers (call in Execute) ---
    void setRenderTarget(uint32_t slot, rhi::ITexture* texture,
                         rhi::LoadOp loadOp = rhi::LoadOp::Clear,
                         const float clearColor[4] = nullptr);

    void setDepthStencil(rhi::ITexture* texture,
                         rhi::LoadOp loadOp = rhi::LoadOp::Clear,
                         float depthClear = 1.0f);

    rhi::IRenderPassEncoder* beginRenderPass(rhi::ICommandEncoder* encoder);

private:
    friend class RenderGraph;
    uint32_t graphPassIndex = UINT32_MAX;
    std::unordered_map<std::string, PassSlot> slots;
    bool hasSideEffect = false;

    // Accumulated render pass state
    std::vector<rhi::RenderPassColorAttachment> colorAttachments;
    rhi::RenderPassDepthStencilAttachment depthStencilAttachment = {};
    bool hasDepthStencil = false;
};
