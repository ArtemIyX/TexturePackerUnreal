// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;

enum class ETexturePackerColorChannel : uint8
{
	R,
	G,
	B
};

enum class ETextureUnpackItemResultType : uint8
{
	Created,
	Skipped,
	Invalid,
	Failed
};

struct FTextureUnpackChannelRequest
{
	ETexturePackerColorChannel channel = ETexturePackerColorChannel::R;
	bool bEnabled = true;
	FString suffix;
};

struct FTextureUnpackRequest
{
	UTexture2D* sourceTexture = nullptr;
	FString baseName;
	TextureGroup textureGroup = TEXTUREGROUP_World;
	bool bUseAlphaCompression = true;
	TArray<FTextureUnpackChannelRequest> channels;
};

struct FTextureUnpackItemResult
{
	ETextureUnpackItemResultType type = ETextureUnpackItemResultType::Failed;
	ETexturePackerColorChannel channel = ETexturePackerColorChannel::R;
	TWeakObjectPtr<UTexture2D> texture;
	FString assetName;
	FString packageName;
	FString message;

	bool IsSuccess() const
	{
		return type == ETextureUnpackItemResultType::Created;
	}
};

struct FTextureUnpackResult
{
	TArray<FTextureUnpackItemResult> items;
	int32 createdCount = 0;
	int32 skippedCount = 0;
	int32 invalidCount = 0;
	int32 failedCount = 0;
};

class FTextureUnpackService
{
public:
	static FTextureUnpackResult UnpackTexture(const FTextureUnpackRequest& InRequest);
};
