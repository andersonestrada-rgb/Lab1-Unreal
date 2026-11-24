// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

#include "AnimGraphInspectorToolset.h"

class FAnimGraphInspectorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolsetRegistry::RegisterToolsetClass(UAnimGraphInspectorToolset::StaticClass());
	}

	virtual void ShutdownModule() override
	{
		if (UToolsetRegistry::IsAvailable())
		{
			UToolsetRegistry::UnregisterToolsetClass(UAnimGraphInspectorToolset::StaticClass());
		}
	}
};

IMPLEMENT_MODULE(FAnimGraphInspectorModule, AnimGraphInspector);
