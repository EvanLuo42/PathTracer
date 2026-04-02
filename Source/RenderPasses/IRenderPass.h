#pragma once

#include "../Resources.h"

#include <slang-rhi.h>

class IRenderPass
{
public:
    virtual ~IRenderPass() = default;

    virtual void Execute(rhi::ICommandEncoder* encoder, Resources& resources) = 0;
};
