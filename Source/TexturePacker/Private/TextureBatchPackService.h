// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TexturePackService.h"

class UTexture2D;

enum class ETextureBatchPackPreviewItemType : uint8
{
	Ready,
	Invalid
};

struct FTextureBatchPackPreviewItem
{
	ETextureBatchPackPreviewItemType type = ETextureBatchPackPreviewItemType::Invalid;
	FString baseName;
	FString outputName;
	FString packageName;
	int32 conflictCount = 0;
	FString message;
};

struct FTextureBatchPackPreview
{
	TArray<FTextureBatchPackPreviewItem> items;
	int32 groupCount = 0;
	int32 readyGroupCount = 0;
	int32 invalidGroupCount = 0;
	int32 outputsToCreate = 0;
	int32 conflictCount = 0;
};

struct FTextureBatchPackRequest
{
	TArray<FTexturePackRequest> requests;
};

struct FTextureBatchPackResult
{
	TArray<FTexturePackResult> groupResults;
	int32 processedGroupCount = 0;
	int32 createdCount = 0;
	int32 invalidCount = 0;
	int32 failedCount = 0;
};

class FTextureBatchPackService
{
public:
	static FTextureBatchPackPreview BuildPreview(const FTextureBatchPackRequest& InRequest);
	static FTexturePackResult PackTexture(const FTexturePackRequest& InRequest);
	static void Accumulate(FTextureBatchPackResult& InOutResult, FTexturePackResult&& InResult);
};
