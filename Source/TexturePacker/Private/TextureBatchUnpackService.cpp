// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureBatchUnpackService.h"

#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"

namespace
{
	bool BatchServiceHasThreeColorChannels(const UTexture2D* InTexture)
	{
		if (!InTexture || !InTexture->Source.IsValid())
		{
			return false;
		}

		switch (InTexture->Source.GetFormat())
		{
		case TSF_G8:
		case TSF_G16:
		case TSF_R16F:
		case TSF_R32F:
			return false;
		default:
			return true;
		}
	}

	FString GetPreviewMessage(const FTextureUnpackRequest& InRequest)
	{
		if (!InRequest.sourceTexture)
		{
			return TEXT("Missing source texture");
		}

		if (!InRequest.sourceTexture->Source.IsValid())
		{
			return TEXT("Texture source is missing");
		}

		if (!BatchServiceHasThreeColorChannels(InRequest.sourceTexture))
		{
			return TEXT("Texture must contain RGB color channels");
		}

		if (InRequest.baseName.TrimStartAndEnd().IsEmpty())
		{
			return TEXT("Base name is empty");
		}

		TSet<FString> usedNames;
		bool bHasEnabledChannel = false;
		for (const FTextureUnpackChannelRequest& channelRequest : InRequest.channels)
		{
			if (!channelRequest.bEnabled)
			{
				continue;
			}

			bHasEnabledChannel = true;
			const FString suffix = channelRequest.suffix.TrimStartAndEnd();
			if (suffix.IsEmpty())
			{
				return TEXT("One or more suffixes are empty");
			}

			const FString fullName = InRequest.baseName + suffix;
			if (usedNames.Contains(fullName))
			{
				return TEXT("Output names must be unique");
			}

			usedNames.Add(fullName);
		}

		if (!bHasEnabledChannel)
		{
			return TEXT("No enabled output channels");
		}

		return FString();
	}
}

FTextureBatchUnpackPreview FTextureBatchUnpackService::BuildPreview(const FTextureBatchUnpackRequest& InRequest)
{
	FTextureBatchUnpackPreview preview;
	preview.textureCount = InRequest.requests.Num();

	for (const FTextureUnpackRequest& request : InRequest.requests)
	{
		FTextureBatchUnpackPreviewItem& item = preview.items.AddDefaulted_GetRef();
		item.sourceTexture = request.sourceTexture;
		item.sourcePath = request.sourceTexture ? request.sourceTexture->GetPathName() : FString();
		item.baseName = request.baseName;

		const FString message = GetPreviewMessage(request);
		if (!message.IsEmpty())
		{
			item.type = ETextureBatchUnpackPreviewItemType::Invalid;
			item.message = message;
			preview.invalidTextureCount++;
			continue;
		}

		item.type = ETextureBatchUnpackPreviewItemType::Ready;
		item.message = TEXT("Ready");
		preview.readyTextureCount++;

		for (const FTextureUnpackChannelRequest& channelRequest : request.channels)
		{
			if (!channelRequest.bEnabled)
			{
				continue;
			}

			item.outputsToCreate++;
			preview.outputsToCreate++;

			const FString assetName = request.baseName + channelRequest.suffix.TrimStartAndEnd();
			const FString packagePath = FPackageName::GetLongPackagePath(request.sourceTexture->GetOutermost()->GetName());
			const FString packageName = packagePath / assetName;
			if (FindObject<UTexture2D>(nullptr, *packageName))
			{
				item.conflictCount++;
				preview.conflictCount++;
			}
		}
	}

	return preview;
}

FTextureUnpackResult FTextureBatchUnpackService::UnpackTexture(const FTextureUnpackRequest& InRequest)
{
	return FTextureUnpackService::UnpackTexture(InRequest);
}

void FTextureBatchUnpackService::Accumulate(FTextureBatchUnpackResult& InOutResult, FTextureUnpackResult&& InResult)
{
	InOutResult.processedTextureCount++;
	InOutResult.createdCount += InResult.createdCount;
	InOutResult.skippedCount += InResult.skippedCount;
	InOutResult.invalidCount += InResult.invalidCount;
	InOutResult.failedCount += InResult.failedCount;
	InOutResult.textureResults.Add(MoveTemp(InResult));
}
