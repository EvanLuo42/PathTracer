#pragma once

#include "IRenderPass.h"
#include "../Scene.h"
#include "../Program.h"

#include <slang-rhi.h>
#include <slang-com-ptr.h>
#include <memory>

class BaseColorDebugPass : public IRenderPass
{
public:
    BaseColorDebugPass(rhi::IDevice* device, std::shared_ptr<Scene> scene);
    void Execute(rhi::ICommandEncoder* encoder, Resources& resources) override;
    const char* GetName() const override { return "BaseColorDebug"; }

private:
    rhi::IDevice* device;
    std::shared_ptr<Scene> scene;
    Slang::ComPtr<rhi::IComputePipeline> pipeline;
};
