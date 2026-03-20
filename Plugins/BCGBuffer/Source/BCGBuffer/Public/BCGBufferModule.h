#pragma once

#include "Modules/ModuleInterface.h"

class FBCGBufferModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
