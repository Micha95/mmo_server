// MMOClientModule.h
#pragma once

#include "Modules/ModuleManager.h" // Use angle brackets for engine headers

class FMMOClientModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
