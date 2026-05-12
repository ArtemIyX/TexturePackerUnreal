// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureUnpackService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace
{
	bool IsSourceLayoutSupported(const UTexture2D* InTexture)
	{
		return InTexture
			&& InTexture->Source.GetNumBlocks() == 1
			&& InTexture->Source.GetNumLayers() == 1
			&& InTexture->Source.GetNumSlices() == 1;
	}

	bool HasThreeColorChannels(const UTexture2D* InTexture)
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

	float GetChannelValue(const FLinearColor& InColor, const ETexturePackerColorChannel InChannel)
	{
		switch (InChannel)
		{
		case ETexturePackerColorChannel::R:
			return InColor.R;
		case ETexturePackerColorChannel::G:
			return InColor.G;
		case ETexturePackerColorChannel::B:
		default:
			return InColor.B;
		}
	}

	FString GetChannelLabel(const ETexturePackerColorChannel InChannel)
	{
		switch (InChannel)
		{
		case ETexturePackerColorChannel::R:
			return TEXT("R");
		case ETexturePackerColorChannel::G:
			return TEXT("G");
		case ETexturePackerColorChannel::B:
		default:
			return TEXT("B");
		}
	}

	FTextureUnpackItemResult MakeResult(
		const ETextureUnpackItemResultType InType,
		const ETexturePackerColorChannel InChannel,
		UTexture2D* InTexture,
		const FString& InAssetName,
		const FString& InPackageName,
		const FString& InMessage)
	{
		FTextureUnpackItemResult result;
		result.type = InType;
		result.channel = InChannel;
		result.texture = InTexture;
		result.assetName = InAssetName;
		result.packageName = InPackageName;
		result.message = InMessage;
		return result;
	}

	void AccumulateResult(FTextureUnpackResult& InOutResult, FTextureUnpackItemResult&& InItem)
	{
		switch (InItem.type)
		{
		case ETextureUnpackItemResultType::Created:
			InOutResult.createdCount++;
			break;
		case ETextureUnpackItemResultType::Skipped:
			InOutResult.skippedCount++;
			break;
		case ETextureUnpackItemResultType::Invalid:
			InOutResult.invalidCount++;
			break;
		case ETextureUnpackItemResultType::Failed:
		default:
			InOutResult.failedCount++;
			break;
		}

		InOutResult.items.Add(MoveTemp(InItem));
	}

	bool BuildChannelImage(const FImageView& InSourceImage, const ETexturePackerColorChannel InChannel, FImage& OutImage)
	{
		OutImage = FImage(InSourceImage.SizeX, InSourceImage.SizeY, ERawImageFormat::G8, EGammaSpace::Linear);
		if (!OutImage.IsImageInfoValid())
		{
			return false;
		}

		TArrayView64<uint8> outputPixels = OutImage.AsG8();
		int64 pixelIndex = 0;
		for (int32 y = 0; y < InSourceImage.SizeY; ++y)
		{
			for (int32 x = 0; x < InSourceImage.SizeX; ++x, ++pixelIndex)
			{
				const float channelValue = GetChannelValue(InSourceImage.GetOnePixelLinear(x, y), InChannel);
				outputPixels[pixelIndex] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(channelValue * 255.f), 0, 255));
			}
		}

		return true;
	}

	void CopyTextureSettings(const UTexture2D* InSourceTexture, UTexture2D* InTargetTexture, const TextureGroup InTextureGroup, const bool bInUseAlphaCompression)
	{
		InTargetTexture->MipGenSettings = InSourceTexture->MipGenSettings;
		InTargetTexture->LODGroup = InTextureGroup;
		InTargetTexture->LODBias = InSourceTexture->LODBias;
		InTargetTexture->Filter = InSourceTexture->Filter;
		InTargetTexture->MipLoadOptions = InSourceTexture->MipLoadOptions;
		InTargetTexture->CompressionQuality = InSourceTexture->CompressionQuality;
		InTargetTexture->PowerOfTwoMode = InSourceTexture->PowerOfTwoMode;
		InTargetTexture->PaddingColor = InSourceTexture->PaddingColor;
		InTargetTexture->bPadWithBorderColor = InSourceTexture->bPadWithBorderColor;
		InTargetTexture->ResizeDuringBuildX = InSourceTexture->ResizeDuringBuildX;
		InTargetTexture->ResizeDuringBuildY = InSourceTexture->ResizeDuringBuildY;
		InTargetTexture->Downscale = InSourceTexture->Downscale;
		InTargetTexture->DownscaleOptions = InSourceTexture->DownscaleOptions;
		InTargetTexture->VirtualTextureStreaming = InSourceTexture->VirtualTextureStreaming;
		InTargetTexture->bUseLegacyGamma = false;
		InTargetTexture->SRGB = false;
		InTargetTexture->AddressX = InSourceTexture->AddressX;
		InTargetTexture->AddressY = InSourceTexture->AddressY;

		if (bInUseAlphaCompression)
		{
			InTargetTexture->CompressionSettings = TC_Alpha;
			InTargetTexture->CompressionNoAlpha = false;
			InTargetTexture->CompressionForceAlpha = false;
			InTargetTexture->CompressionNone = false;
			InTargetTexture->CompressionYCoCg = false;
		}
		else
		{
			InTargetTexture->CompressionSettings = InSourceTexture->CompressionSettings;
			InTargetTexture->CompressionNoAlpha = InSourceTexture->CompressionNoAlpha;
			InTargetTexture->CompressionForceAlpha = InSourceTexture->CompressionForceAlpha;
			InTargetTexture->CompressionNone = InSourceTexture->CompressionNone;
			InTargetTexture->CompressionYCoCg = InSourceTexture->CompressionYCoCg;
		}
	}

	FTextureUnpackItemResult CreateChannelTexture(
		const UTexture2D* InSourceTexture,
		const FString& InBaseName,
		const TextureGroup InTextureGroup,
		const bool bInUseAlphaCompression,
		const FTextureUnpackChannelRequest& InChannelRequest,
		const FImageView& InSourceImage)
	{
		const FString trimmedSuffix = InChannelRequest.suffix.TrimStartAndEnd();
		const FString assetName = InBaseName + trimmedSuffix;
		const FString packagePath = FPackageName::GetLongPackagePath(InSourceTexture->GetOutermost()->GetName());
		const FString packageName = packagePath / assetName;

		if (trimmedSuffix.IsEmpty())
		{
			return MakeResult(ETextureUnpackItemResultType::Invalid, InChannelRequest.channel, nullptr, assetName, packageName, TEXT("Suffix is empty"));
		}

		if (!FPackageName::IsValidLongPackageName(packageName))
		{
			return MakeResult(ETextureUnpackItemResultType::Invalid, InChannelRequest.channel, nullptr, assetName, packageName, TEXT("Package name is invalid"));
		}

		if (FindObject<UTexture2D>(nullptr, *packageName))
		{
			return MakeResult(ETextureUnpackItemResultType::Failed, InChannelRequest.channel, nullptr, assetName, packageName, TEXT("Asset already exists"));
		}

		FImage outputImage;
		if (!BuildChannelImage(InSourceImage, InChannelRequest.channel, outputImage))
		{
			return MakeResult(ETextureUnpackItemResultType::Failed, InChannelRequest.channel, nullptr, assetName, packageName, TEXT("Failed to build output image"));
		}

		UPackage* package = CreatePackage(*packageName);
		if (!package)
		{
			return MakeResult(ETextureUnpackItemResultType::Failed, InChannelRequest.channel, nullptr, assetName, packageName, TEXT("Failed to create package"));
		}

		UTexture2D* newTexture = NewObject<UTexture2D>(package, *assetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!newTexture)
		{
			return MakeResult(ETextureUnpackItemResultType::Failed, InChannelRequest.channel, nullptr, assetName, packageName, TEXT("Failed to create texture asset"));
		}

		(void)newTexture->Modify();
		newTexture->PreEditChange(nullptr);
		newTexture->Source.Init(outputImage);
		CopyTextureSettings(InSourceTexture, newTexture, InTextureGroup, bInUseAlphaCompression);
		newTexture->PostEditChange();
		(void)newTexture->MarkPackageDirty();
		newTexture->UpdateResource();
		FAssetRegistryModule::AssetCreated(newTexture);

		return MakeResult(ETextureUnpackItemResultType::Created, InChannelRequest.channel, newTexture, assetName, packageName, TEXT("Texture created"));
	}
}

FTextureUnpackResult FTextureUnpackService::UnpackTexture(const FTextureUnpackRequest& InRequest)
{
	FTextureUnpackResult result;

	if (!InRequest.sourceTexture)
	{
		AccumulateResult(result, MakeResult(ETextureUnpackItemResultType::Invalid, ETexturePackerColorChannel::R, nullptr, FString(), FString(), TEXT("Missing source texture")));
		return result;
	}

	if (InRequest.baseName.TrimStartAndEnd().IsEmpty())
	{
		AccumulateResult(result, MakeResult(ETextureUnpackItemResultType::Invalid, ETexturePackerColorChannel::R, nullptr, FString(), FString(), TEXT("Base name is empty")));
		return result;
	}

	if (!InRequest.sourceTexture->Source.IsValid())
	{
		AccumulateResult(result, MakeResult(ETextureUnpackItemResultType::Invalid, ETexturePackerColorChannel::R, nullptr, FString(), FString(), TEXT("Texture source is missing")));
		return result;
	}

	if (!IsSourceLayoutSupported(InRequest.sourceTexture))
	{
		AccumulateResult(result, MakeResult(ETextureUnpackItemResultType::Invalid, ETexturePackerColorChannel::R, nullptr, FString(), FString(), TEXT("Only single-block single-layer 2D textures are supported")));
		return result;
	}

	if (!HasThreeColorChannels(InRequest.sourceTexture))
	{
		AccumulateResult(result, MakeResult(ETextureUnpackItemResultType::Invalid, ETexturePackerColorChannel::R, nullptr, FString(), FString(), TEXT("Texture must contain RGB color channels")));
		return result;
	}

	TSet<FString> usedNames;
	bool bHasEnabledChannel = false;
	for (const FTextureUnpackChannelRequest& channelRequest : InRequest.channels)
	{
		if (!channelRequest.bEnabled)
		{
			AccumulateResult(
				result,
				MakeResult(
					ETextureUnpackItemResultType::Skipped,
					channelRequest.channel,
					nullptr,
					InRequest.baseName + channelRequest.suffix,
					FString(),
					TEXT("Channel disabled")));
			continue;
		}

		bHasEnabledChannel = true;
		const FString fullName = InRequest.baseName + channelRequest.suffix.TrimStartAndEnd();
		if (fullName == InRequest.baseName)
		{
			AccumulateResult(result, MakeResult(ETextureUnpackItemResultType::Invalid, channelRequest.channel, nullptr, fullName, FString(), TEXT("Suffix is empty")));
			continue;
		}

		if (usedNames.Contains(fullName))
		{
			AccumulateResult(result, MakeResult(ETextureUnpackItemResultType::Invalid, channelRequest.channel, nullptr, fullName, FString(), TEXT("Output name is duplicated")));
			continue;
		}

		usedNames.Add(fullName);
	}

	if (!bHasEnabledChannel)
	{
		AccumulateResult(result, MakeResult(ETextureUnpackItemResultType::Invalid, ETexturePackerColorChannel::R, nullptr, FString(), FString(), TEXT("No enabled channels")));
		return result;
	}

	FImage sourceImage;
	if (!InRequest.sourceTexture->Source.GetMipImage(sourceImage, 0))
	{
		AccumulateResult(result, MakeResult(ETextureUnpackItemResultType::Failed, ETexturePackerColorChannel::R, nullptr, FString(), FString(), TEXT("Failed to read source image")));
		return result;
	}

	if (!sourceImage.IsImageInfoValid())
	{
		AccumulateResult(result, MakeResult(ETextureUnpackItemResultType::Failed, ETexturePackerColorChannel::R, nullptr, FString(), FString(), TEXT("Source image data is invalid")));
		return result;
	}

	const FImageView sourceImageView = sourceImage;
	for (const FTextureUnpackChannelRequest& channelRequest : InRequest.channels)
	{
		if (!channelRequest.bEnabled)
		{
			continue;
		}

		AccumulateResult(
			result,
			CreateChannelTexture(
				InRequest.sourceTexture,
				InRequest.baseName,
				InRequest.textureGroup,
				InRequest.bUseAlphaCompression,
				channelRequest,
				sourceImageView));
	}

	return result;
}
