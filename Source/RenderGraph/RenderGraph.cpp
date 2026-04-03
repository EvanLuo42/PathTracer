#include "RenderGraph.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <cassert>
#include <queue>
#include <set>

using namespace rhi;

RenderGraphSlot RenderPass::AddInput(const std::string& name, PassSlot::Access access)
{
    PassSlot slot;
    slot.direction = PassSlot::Direction::Input;
    slot.access = access;
    slots[name] = slot;
    return {graphPassIndex, name};
}

RenderGraphSlot RenderPass::AddOutput(const std::string& name, Format format, PassSlot::Access access,
                                       SizePolicy sizePolicy, LoadOp loadOp, uint32_t rtSlot)
{
    PassSlot slot;
    slot.direction = PassSlot::Direction::Output;
    slot.access = access;
    slot.desc = {name, format, sizePolicy};
    slot.loadOp = loadOp;
    slot.rtSlot = rtSlot;
    slots[name] = slot;
    return {graphPassIndex, name};
}

void RenderPass::SetRenderTarget(uint32_t slot, ITexture* texture, LoadOp loadOp, const float clearColor[4])
{
    RenderPassColorAttachment attachment = {};
    attachment.view = texture->getDefaultView();
    attachment.loadOp = loadOp;
    attachment.storeOp = StoreOp::Store;
    if (clearColor)
    {
        attachment.clearValue[0] = clearColor[0];
        attachment.clearValue[1] = clearColor[1];
        attachment.clearValue[2] = clearColor[2];
        attachment.clearValue[3] = clearColor[3];
    }

    if (slot >= colorAttachments.size())
        colorAttachments.resize(slot + 1);
    colorAttachments[slot] = attachment;
}

void RenderPass::SetDepthStencil(ITexture* texture, LoadOp loadOp, float depthClear)
{
    depthStencilAttachment = {};
    depthStencilAttachment.view = texture->getDefaultView();
    depthStencilAttachment.depthLoadOp = loadOp;
    depthStencilAttachment.depthStoreOp = StoreOp::Store;
    depthStencilAttachment.depthClearValue = depthClear;
    hasDepthStencil = true;
}

IRenderPassEncoder* RenderPass::BeginRenderPass(ICommandEncoder* encoder)
{
    RenderPassDesc desc = {};
    desc.colorAttachments = colorAttachments.data();
    desc.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
    if (hasDepthStencil)
        desc.depthStencilAttachment = &depthStencilAttachment;

    auto* passEncoder = encoder->beginRenderPass(desc);

    colorAttachments.clear();
    hasDepthStencil = false;

    return passEncoder;
}

ITexture* RenderGraphResources::GetTexture(const std::string& slot) const
{
    auto it = textures.find(slot);
    if (it == textures.end())
        return nullptr;
    return it->second;
}

RenderGraph::RenderGraph(IDevice* device) : device(device) {}

void RenderGraph::AddPassInternal(const std::string& name, std::unique_ptr<RenderPass> pass)
{
    const auto index = static_cast<uint32_t>(passes.size());
    pass->graphPassIndex = index;
    pass->Setup();
    passIndexByName[name] = index;
    passes.push_back({name, std::move(pass)});
}

uint32_t RenderGraph::GetPassIndex(const std::string& name) const
{
    auto it = passIndexByName.find(name);
    assert(it != passIndexByName.end());
    return it->second;
}

void RenderGraph::AddEdge(const RenderGraphSlot& src, const RenderGraphSlot& dst)
{
    edges.push_back({src.passIndex, dst.passIndex, src.name, dst.name});
}

RenderGraphSlot RenderGraph::ImportTexture(const std::string& name, ITexture* texture, ResourceState currentState)
{
    auto it = passIndexByName.find(name);
    if (it != passIndexByName.end())
    {
        auto slotKey = std::to_string(it->second) + ":out";
        auto physIt = slotToPhysical.find(slotKey);
        if (physIt != slotToPhysical.end())
        {
            auto& phys = physicalResources[physIt->second];
            phys.texture = texture;
            phys.externalState = currentState;
        }
        return {it->second, "out"};
    }

    class ImportPass : public RenderPass
    {
    public:
        explicit ImportPass(const char* n) : passName(n) {}
        void Setup() override { AddOutput("out", Format::Undefined, PassSlot::Access::ShaderResource); }
        void Execute(ICommandEncoder*, const RenderGraphResources&) override {}
        const char* GetName() const override { return passName; }
    private:
        const char* passName;
    };

    const auto index = static_cast<uint32_t>(passes.size());

    auto& entry = passes.emplace_back();
    entry.name = name;
    entry.pass = std::make_unique<ImportPass>(entry.name.c_str());
    entry.pass->graphPassIndex = index;
    passIndexByName[name] = index;
    entry.pass->Setup();

    PhysicalResource phys;
    phys.id = static_cast<uint32_t>(physicalResources.size());
    phys.external = true;
    phys.texture = texture;
    phys.externalState = currentState;
    phys.desc.name = name;
    if (texture)
        phys.desc.format = texture->getDesc().format;

    slotToPhysical[std::to_string(index) + ":out"] = phys.id;
    physicalResources.push_back(std::move(phys));

    return {index, "out"};
}

void RenderGraph::MarkOutput(const RenderGraphSlot& slot)
{
    outputPass = slot.passIndex;
    outputSlot = slot.name;
}

TextureUsage RenderGraph::AccessToUsage(PassSlot::Access access)
{
    switch (access)
    {
    case PassSlot::Access::ShaderResource:  return TextureUsage::ShaderResource;
    case PassSlot::Access::UnorderedAccess: return TextureUsage::UnorderedAccess;
    case PassSlot::Access::RenderTarget:    return TextureUsage::RenderTarget;
    case PassSlot::Access::DepthStencil:    return TextureUsage::DepthStencil;
    }
    return TextureUsage::None;
}

ResourceState RenderGraph::AccessToState(PassSlot::Access access)
{
    switch (access)
    {
    case PassSlot::Access::ShaderResource:  return ResourceState::ShaderResource;
    case PassSlot::Access::UnorderedAccess: return ResourceState::UnorderedAccess;
    case PassSlot::Access::RenderTarget:    return ResourceState::RenderTarget;
    case PassSlot::Access::DepthStencil:    return ResourceState::DepthWrite;
    }
    return ResourceState::Undefined;
}

void RenderGraph::AssignPhysicalResources()
{
    for (uint32_t i = 0; i < passes.size(); i++)
    {
        for (const auto& [slotName, slot] : passes[i].pass->GetSlots())
        {
            if (slot.direction != PassSlot::Direction::Output)
                continue;
            auto key = std::to_string(i) + ":" + slotName;
            if (slotToPhysical.count(key))
                continue;

            PhysicalResource phys;
            phys.id = static_cast<uint32_t>(physicalResources.size());
            phys.desc = slot.desc;
            slotToPhysical[key] = phys.id;
            physicalResources.push_back(std::move(phys));
        }
    }

    for (const auto& edge : edges)
    {
        auto srcKey = std::to_string(edge.srcPass) + ":" + edge.srcSlot;
        auto dstKey = std::to_string(edge.dstPass) + ":" + edge.dstSlot;
        auto srcIt = slotToPhysical.find(srcKey);
        assert(srcIt != slotToPhysical.end());
        slotToPhysical[dstKey] = srcIt->second;
    }

    for (uint32_t i = 0; i < passes.size(); i++)
    {
        for (const auto& [slotName, slot] : passes[i].pass->GetSlots())
        {
            auto key = std::to_string(i) + ":" + slotName;
            auto it = slotToPhysical.find(key);
            if (it == slotToPhysical.end())
                continue;

            auto& phys = physicalResources[it->second];
            phys.accumulatedUsage |= AccessToUsage(slot.access);

            if (!phys.external && slot.direction == PassSlot::Direction::Output &&
                slot.desc.format != Format::Undefined)
                phys.desc = slot.desc;
        }
    }
}

void RenderGraph::CreateGpuResources()
{
    for (auto& phys : physicalResources)
    {
        if (phys.external || phys.desc.format == Format::Undefined)
            continue;

        uint32_t w = phys.desc.sizePolicy.ResolveWidth(bbWidth);
        uint32_t h = phys.desc.sizePolicy.ResolveHeight(bbHeight);

        if (phys.texture)
        {
            const auto existing = phys.texture->getDesc();
            if (existing.size.width == w && existing.size.height == h)
                continue;
        }

        TextureDesc desc = {};
        desc.type = TextureType::Texture2D;
        desc.size = {w, h, 1};
        desc.format = phys.desc.format;
        desc.usage = phys.accumulatedUsage | TextureUsage::CopySource | TextureUsage::CopyDestination;
        desc.defaultState = ResourceState::Undefined;
        desc.memoryType = MemoryType::DeviceLocal;
        desc.label = phys.desc.name.c_str();

        phys.texture = device->createTexture(desc);
        if (!phys.texture)
            spdlog::error("Failed to create render graph texture: {}", phys.desc.name);
    }
}

void RenderGraph::TopologicalSort()
{
    const auto passCount = static_cast<uint32_t>(passes.size());

    std::vector<std::vector<uint32_t>> adj(passCount);
    std::vector<uint32_t> inDegree(passCount, 0);

    for (const auto& edge : edges)
    {
        spdlog::debug("  edge: {} -> {} (slots {}->{})", edge.srcPass, edge.dstPass, edge.srcSlot, edge.dstSlot);
        if (edge.srcPass >= passCount || edge.dstPass >= passCount)
        {
            spdlog::error("  INVALID edge index! passCount={}", passCount);
            continue;
        }
        adj[edge.srcPass].push_back(edge.dstPass);
        inDegree[edge.dstPass]++;
    }

    std::vector<bool> alive(passCount, false);
    {
        std::queue<uint32_t> q;
        if (outputPass != UINT32_MAX)
        {
            alive[outputPass] = true;
            q.push(outputPass);
        }
        for (uint32_t i = 0; i < passCount; i++)
        {
            if (passes[i].pass->HasSideEffect())
            {
                alive[i] = true;
                q.push(i);
            }
        }

        std::vector<std::vector<uint32_t>> radj(passCount);
        for (const auto& edge : edges)
            radj[edge.dstPass].push_back(edge.srcPass);

        while (!q.empty())
        {
            uint32_t cur = q.front();
            q.pop();
            for (uint32_t pred : radj[cur])
            {
                if (!alive[pred])
                {
                    alive[pred] = true;
                    q.push(pred);
                }
            }
        }
    }

    std::set<uint32_t> ready;
    for (uint32_t i = 0; i < passCount; i++)
    {
        if (inDegree[i] == 0 && alive[i])
            ready.insert(i);
    }

    executionOrder.clear();
    while (!ready.empty())
    {
        uint32_t cur = *ready.begin();
        ready.erase(ready.begin());

        auto slotKey = std::to_string(cur) + ":out";
        auto physIt = slotToPhysical.find(slotKey);
        bool isExternal = false;
        if (physIt != slotToPhysical.end())
        {
            if (physIt->second < physicalResources.size())
                isExternal = physicalResources[physIt->second].external;
            else
                spdlog::error("TopologicalSort: slot '{}' maps to physId {} but only {} resources exist",
                    slotKey, physIt->second, physicalResources.size());
        }

        if (!isExternal)
            executionOrder.push_back({cur, {}});

        for (uint32_t next : adj[cur])
        {
            if (--inDegree[next] == 0 && alive[next])
                ready.insert(next);
        }
    }
}

void RenderGraph::PlanBarriers()
{
    std::unordered_map<uint32_t, ResourceState> currentState;
    for (const auto& phys : physicalResources)
        currentState[phys.id] = phys.external ? phys.externalState : ResourceState::Undefined;

    for (auto& cp : executionOrder)
    {
        cp.barriers.clear();
        for (const auto& [slotName, slot] : passes[cp.passIndex].pass->GetSlots())
        {
            auto key = std::to_string(cp.passIndex) + ":" + slotName;
            auto physIt = slotToPhysical.find(key);
            if (physIt == slotToPhysical.end())
                continue;

            uint32_t physId = physIt->second;
            ResourceState needed = AccessToState(slot.access);

            if (currentState[physId] != needed)
            {
                cp.barriers.push_back({physId, needed});
                currentState[physId] = needed;
            }
        }
    }
}

void RenderGraph::Compile(uint32_t backBufferWidth, uint32_t backBufferHeight)
{
    bbWidth = backBufferWidth;
    bbHeight = backBufferHeight;

    executionOrder.clear();

    {
        std::vector<PhysicalResource> kept;
        std::unordered_map<std::string, uint32_t> keptSlots;
        std::unordered_map<uint32_t, uint32_t> idRemap;

        for (auto& phys : physicalResources)
        {
            if (phys.external)
            {
                uint32_t newId = static_cast<uint32_t>(kept.size());
                idRemap[phys.id] = newId;
                phys.id = newId;
                kept.push_back(std::move(phys));
            }
        }

        for (const auto& [key, physId] : slotToPhysical)
        {
            auto it = idRemap.find(physId);
            if (it != idRemap.end())
                keptSlots[key] = it->second;
        }

        physicalResources = std::move(kept);
        slotToPhysical = std::move(keptSlots);
    }

    AssignPhysicalResources();
    CreateGpuResources();
    TopologicalSort();
    PlanBarriers();

    spdlog::debug("RenderGraph compiled: {} passes, {} resources",
        executionOrder.size(), physicalResources.size());
}

void RenderGraph::Execute(ICommandEncoder* encoder)
{
    for (const auto& cp : executionOrder)
    {
        for (const auto& barrier : cp.barriers)
        {
            auto& phys = physicalResources[barrier.physicalId];
            if (phys.texture)
                encoder->setTextureState(phys.texture, barrier.newState);
        }

        RenderGraphResources resources;
        for (const auto& [slotName, slot] : passes[cp.passIndex].pass->GetSlots())
        {
            auto key = std::to_string(cp.passIndex) + ":" + slotName;
            auto physIt = slotToPhysical.find(key);
            if (physIt == slotToPhysical.end())
                continue;
            auto& phys = physicalResources[physIt->second];
            if (phys.texture)
                resources.textures[slotName] = phys.texture;
        }

        passes[cp.passIndex].pass->Execute(encoder, resources);
    }
}

void RenderGraph::Resize(uint32_t width, uint32_t height)
{
    bbWidth = width;
    bbHeight = height;
    CreateGpuResources();
    PlanBarriers();
}

ITexture* RenderGraph::GetTexture(const RenderGraphSlot& slot) const
{
    auto key = std::to_string(slot.passIndex) + ":" + slot.name;
    auto it = slotToPhysical.find(key);
    if (it == slotToPhysical.end())
        return nullptr;
    return physicalResources[it->second].texture;
}

void RenderGraph::OnRenderUI()
{
    for (const auto& cp : executionOrder)
    {
        auto* pass = passes[cp.passIndex].pass.get();
        if (ImGui::CollapsingHeader(pass->GetName()))
        {
            ImGui::PushID(pass->GetName());
            pass->OnRenderUI();
            ImGui::PopID();
        }
    }
}
