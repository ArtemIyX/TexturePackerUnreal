// Copyright Epic Games, Inc. All Rights Reserved.

#include "TexturePacker.h"

#include "TexturePackerAssetActions.h"

#define LOCTEXT_NAMESPACE "FTexturePackerModule"

void FTexturePackerModule::StartupModule()
{
	assetActions = MakeUnique<FTexturePackerAssetActions>();
	assetActions->Register();
}

void FTexturePackerModule::ShutdownModule()
{
	if (assetActions.IsValid())
	{
		assetActions->Unregister();
		assetActions.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTexturePackerModule, TexturePacker)
