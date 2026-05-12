// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;

enum class ETexturePackResultType : uint8
{
	Created,
	Invalid,
	Failed
};

struct FTexturePackRequest
{
	UTexture2D* redTexture = nullptr;
	UTexture2D* greenTexture = nullptr;
	UTexture2D* blueTexture = nullptr;
	FString outputName;
	TextureGroup textureGroup = TEXTUREGROUP_World;
};

struct FTexturePackResult
{
	ETexturePackResultType type = ETexturePackResultType::Failed;
	TWeakObjectPtr<UTexture2D> texture;
	FString assetName;
	FString packageName;
	FString message;

	bool IsSuccess() const
	{
		return type == ETexturePackResultType::Created;
	}
};

class FTexturePackService
{
public:
	static FTexturePackResult PackTexture(const FTexturePackRequest& InRequest);
};
