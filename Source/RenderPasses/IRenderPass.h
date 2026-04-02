#pragma once

#include "../Resources.h"

#include <slang-rhi.h>

#include <string>

class IRenderPass
{
public:
    virtual ~IRenderPass() = default;

    virtual void Execute(rhi::ICommandEncoder* encoder, Resources& resources) = 0;
    virtual void OnRenderUI() {}
    virtual const char* GetName() const = 0;

protected:
    static constexpr rhi::MarkerColor kPassColor{0.4f, 0.7f, 1.0f};
};
