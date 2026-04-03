#pragma once

#include <slang-rhi.h>

class IRenderPass
{
public:
    virtual ~IRenderPass() = default;

    virtual void Execute(rhi::ICommandEncoder* encoder, Resources& resources) = 0;
    virtual void OnRenderUI() {}
    [[nodiscard]] virtual const char* GetName() const = 0;
};
