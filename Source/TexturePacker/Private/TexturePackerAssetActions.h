// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ContentBrowserDelegates.h"
#include "CoreMinimal.h"

class FExtender;

class FTexturePackerAssetActions
{
public:
	void Register();
	void Unregister();

private:
	TSharedRef<FExtender> OnExtendAssetMenu(const TArray<FAssetData>& InSelectedAssets);

private:
	FDelegateHandle extenderHandle;
};
