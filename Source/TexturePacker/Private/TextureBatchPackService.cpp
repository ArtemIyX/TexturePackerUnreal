// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureBatchPackService.h"

#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"

namespace
{
	FString GetBatchPackPreviewMessage(const FTexturePackRequest& InRequest)
	{
		if (!InRequest.redTexture || !InRequest.greenTexture || !InRequest.blueTexture)
		{
			return TEXT("Missing channel texture");
		}

		if (InRequest.redTexture == InRequest.greenTexture || InRequest.redTexture == InRequest.blueTexture || InRequest.greenTexture == InRequest.blueTexture)
		{
			return TEXT("R, G, and B textures must be different");
		}

		if (InRequest.outputName.TrimStartAndEnd().IsEmpty())
		{
			return TEXT("Output name is empty");
		}

		return FString();
	}
}

FTextureBatchPackPreview FTextureBatchPackService::BuildPreview(const FTextureBatchPackRequest& InRequest)
{
	FTextureBatchPackPreview preview;
	preview.groupCount = InRequest.requests.Num();

	for (const FTexturePackRequest& request : InRequest.requests)
	{
		FTextureBatchPackPreviewItem& item = preview.items.AddDefaulted_GetRef();
		item.baseName = request.redTexture ? request.redTexture->GetName() : TEXT("Invalid");
		item.outputName = request.outputName;

		const FString message = GetBatchPackPreviewMessage(request);
		if (!message.IsEmpty())
		{
			item.type = ETextureBatchPackPreviewItemType::Invalid;
			item.message = message;
			preview.invalidGroupCount++;
			continue;
		}

		const FString packagePath = FPackageName::GetLongPackagePath(request.redTexture->GetOutermost()->GetName());
		item.packageName = packagePath / request.outputName;
		if (FindObject<UTexture2D>(nullptr, *item.packageName))
		{
			item.conflictCount = 1;
			preview.conflictCount++;
		}

		item.type = ETextureBatchPackPreviewItemType::Ready;
		item.message = TEXT("Ready");
		preview.readyGroupCount++;
		preview.outputsToCreate++;
	}

	return preview;
}

FTexturePackResult FTextureBatchPackService::PackTexture(const FTexturePackRequest& InRequest)
{
	return FTexturePackService::PackTexture(InRequest);
}

void FTextureBatchPackService::Accumulate(FTextureBatchPackResult& InOutResult, FTexturePackResult&& InResult)
{
	InOutResult.processedGroupCount++;
	switch (InResult.type)
	{
	case ETexturePackResultType::Created:
		InOutResult.createdCount++;
		break;
	case ETexturePackResultType::Invalid:
		InOutResult.invalidCount++;
		break;
	case ETexturePackResultType::Failed:
	default:
		InOutResult.failedCount++;
		break;
	}

	InOutResult.groupResults.Add(MoveTemp(InResult));
}
