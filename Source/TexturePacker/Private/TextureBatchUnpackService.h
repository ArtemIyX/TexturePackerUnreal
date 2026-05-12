// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TextureUnpackService.h"

class UTexture2D;

enum class ETextureBatchUnpackPreviewItemType : uint8
{
	Ready,
	Invalid
};

struct FTextureBatchUnpackPreviewItem
{
	ETextureBatchUnpackPreviewItemType type = ETextureBatchUnpackPreviewItemType::Invalid;
	TWeakObjectPtr<UTexture2D> sourceTexture;
	FString sourcePath;
	FString baseName;
	int32 outputsToCreate = 0;
	int32 conflictCount = 0;
	FString message;
};

struct FTextureBatchUnpackPreview
{
	TArray<FTextureBatchUnpackPreviewItem> items;
	int32 textureCount = 0;
	int32 readyTextureCount = 0;
	int32 invalidTextureCount = 0;
	int32 outputsToCreate = 0;
	int32 conflictCount = 0;
};

struct FTextureBatchUnpackRequest
{
	TArray<FTextureUnpackRequest> requests;
};

struct FTextureBatchUnpackResult
{
	TArray<FTextureUnpackResult> textureResults;
	int32 processedTextureCount = 0;
	int32 createdCount = 0;
	int32 skippedCount = 0;
	int32 invalidCount = 0;
	int32 failedCount = 0;
};

class FTextureBatchUnpackService
{
public:
	static FTextureBatchUnpackPreview BuildPreview(const FTextureBatchUnpackRequest& InRequest);
	static FTextureUnpackResult UnpackTexture(const FTextureUnpackRequest& InRequest);
	static void Accumulate(FTextureBatchUnpackResult& InOutResult, FTextureUnpackResult&& InResult);
};
