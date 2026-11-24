// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

#include "AnimGraphInspectorToolset.h"

class FAnimGraphInspectorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// The toolset registry subsystem does not exist yet while project modules are loading,
		// so poll until the editor (and its subsystem) is available before registering.
		RegisterTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FAnimGraphInspectorModule::TryRegisterToolset), 0.25f);
	}

	virtual void ShutdownModule() override
	{
		if (RegisterTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(RegisterTickerHandle);
			RegisterTickerHandle.Reset();
		}

		if (bRegistered && UToolsetRegistry::IsAvailable())
		{
			UToolsetRegistry::UnregisterToolsetClass(UAnimGraphInspectorToolset::StaticClass());
			bRegistered = false;
		}
	}

private:
	bool TryRegisterToolset(float /*DeltaTime*/)
	{
		if (!UToolsetRegistry::IsAvailable())
		{
			return true; // keep polling
		}

		UToolsetRegistry::RegisterToolsetClass(UAnimGraphInspectorToolset::StaticClass());
		bRegistered = UToolsetRegistry::IsToolsetClassRegistered(UAnimGraphInspectorToolset::StaticClass());
		RegisterTickerHandle.Reset();
		return false; // stop polling
	}

	FTSTicker::FDelegateHandle RegisterTickerHandle;
	bool bRegistered = false;
};

IMPLEMENT_MODULE(FAnimGraphInspectorModule, AnimGraphInspector);
