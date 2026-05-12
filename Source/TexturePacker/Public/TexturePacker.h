// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FTexturePackerAssetActions;

class FTexturePackerModule : public IModuleInterface
{
public:
#pragma region IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
#pragma endregion

private:
#pragma region State
	TUniquePtr<FTexturePackerAssetActions> assetActions;
#pragma endregion
};
