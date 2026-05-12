// Copyright Epic Games, Inc. All Rights Reserved.

#include "TexturePackService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace
{
	bool IsPackSourceLayoutSupported(const UTexture2D* InTexture)
	{
		return InTexture
			&& InTexture->Source.GetNumBlocks() == 1
			&& InTexture->Source.GetNumLayers() == 1
			&& InTexture->Source.GetNumSlices() == 1;
	}

	FTexturePackResult MakePackResult(
		const ETexturePackResultType InType,
		UTexture2D* InTexture,
		const FString& InAssetName,
		const FString& InPackageName,
		const FString& InMessage)
	{
		FTexturePackResult result;
		result.type = InType;
		result.texture = InTexture;
		result.assetName = InAssetName;
		result.packageName = InPackageName;
		result.message = InMessage;
		return result;
	}

	bool ValidatePackSourceTexture(const UTexture2D* InTexture, const TCHAR* InLabel, FString& OutMessage)
	{
		if (!InTexture)
		{
			OutMessage = FString::Printf(TEXT("%s texture is missing"), InLabel);
			return false;
		}

		if (!InTexture->Source.IsValid())
		{
			OutMessage = FString::Printf(TEXT("%s texture source is missing"), InLabel);
			return false;
		}

		if (!IsPackSourceLayoutSupported(InTexture))
		{
			OutMessage = FString::Printf(TEXT("%s texture must be a single-block single-layer 2D texture"), InLabel);
			return false;
		}

		return true;
	}

	bool ValidatePackSourceSizes(const UTexture2D* InRedTexture, const UTexture2D* InGreenTexture, const UTexture2D* InBlueTexture, FString& OutMessage)
	{
		const FIntPoint redSize(static_cast<int32>(InRedTexture->Source.GetSizeX()), static_cast<int32>(InRedTexture->Source.GetSizeY()));
		const FIntPoint greenSize(static_cast<int32>(InGreenTexture->Source.GetSizeX()), static_cast<int32>(InGreenTexture->Source.GetSizeY()));
		const FIntPoint blueSize(static_cast<int32>(InBlueTexture->Source.GetSizeX()), static_cast<int32>(InBlueTexture->Source.GetSizeY()));
		if (redSize != greenSize || redSize != blueSize)
		{
			OutMessage = TEXT("R, G, and B textures must have the same dimensions");
			return false;
		}

		return true;
	}

	bool BuildPackImage(const FImageView& InRedImage, const FImageView& InGreenImage, const FImageView& InBlueImage, FImage& OutImage)
	{
		OutImage = FImage(InRedImage.SizeX, InRedImage.SizeY, ERawImageFormat::BGRA8, EGammaSpace::Linear);
		if (!OutImage.IsImageInfoValid())
		{
			return false;
		}

		TArrayView64<FColor> outputPixels = OutImage.AsBGRA8();
		int64 pixelIndex = 0;
		for (int32 y = 0; y < InRedImage.SizeY; ++y)
		{
			for (int32 x = 0; x < InRedImage.SizeX; ++x, ++pixelIndex)
			{
				const uint8 redValue = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(InRedImage.GetOnePixelLinear(x, y).R * 255.f), 0, 255));
				const uint8 greenValue = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(InGreenImage.GetOnePixelLinear(x, y).R * 255.f), 0, 255));
				const uint8 blueValue = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(InBlueImage.GetOnePixelLinear(x, y).R * 255.f), 0, 255));
				outputPixels[pixelIndex] = FColor(redValue, greenValue, blueValue, 255);
			}
		}

		return true;
	}

	void CopyPackTextureSettings(const UTexture2D* InSourceTexture, UTexture2D* InTargetTexture, const TextureGroup InTextureGroup)
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
		InTargetTexture->AddressX = InSourceTexture->AddressX;
		InTargetTexture->AddressY = InSourceTexture->AddressY;
		InTargetTexture->bUseLegacyGamma = false;
		InTargetTexture->SRGB = false;
		InTargetTexture->CompressionSettings = TC_Masks;
		InTargetTexture->CompressionNoAlpha = true;
		InTargetTexture->CompressionForceAlpha = false;
		InTargetTexture->CompressionNone = false;
		InTargetTexture->CompressionYCoCg = false;
	}
}

FTexturePackResult FTexturePackService::PackTexture(const FTexturePackRequest& InRequest)
{
	const FString outputName = InRequest.outputName.TrimStartAndEnd();
	if (outputName.IsEmpty())
	{
		return MakePackResult(ETexturePackResultType::Invalid, nullptr, FString(), FString(), TEXT("Output name is empty"));
	}

	if (InRequest.redTexture == InRequest.greenTexture || InRequest.redTexture == InRequest.blueTexture || InRequest.greenTexture == InRequest.blueTexture)
	{
		return MakePackResult(ETexturePackResultType::Invalid, nullptr, outputName, FString(), TEXT("R, G, and B textures must be different"));
	}

	FString validationMessage;
	if (!ValidatePackSourceTexture(InRequest.redTexture, TEXT("R"), validationMessage))
	{
		return MakePackResult(ETexturePackResultType::Invalid, nullptr, outputName, FString(), validationMessage);
	}

	if (!ValidatePackSourceTexture(InRequest.greenTexture, TEXT("G"), validationMessage))
	{
		return MakePackResult(ETexturePackResultType::Invalid, nullptr, outputName, FString(), validationMessage);
	}

	if (!ValidatePackSourceTexture(InRequest.blueTexture, TEXT("B"), validationMessage))
	{
		return MakePackResult(ETexturePackResultType::Invalid, nullptr, outputName, FString(), validationMessage);
	}

	if (!ValidatePackSourceSizes(InRequest.redTexture, InRequest.greenTexture, InRequest.blueTexture, validationMessage))
	{
		return MakePackResult(ETexturePackResultType::Invalid, nullptr, outputName, FString(), validationMessage);
	}

	const FString packagePath = FPackageName::GetLongPackagePath(InRequest.redTexture->GetOutermost()->GetName());
	const FString packageName = packagePath / outputName;
	if (!FPackageName::IsValidLongPackageName(packageName))
	{
		return MakePackResult(ETexturePackResultType::Invalid, nullptr, outputName, packageName, TEXT("Package name is invalid"));
	}

	if (FindObject<UTexture2D>(nullptr, *packageName))
	{
		return MakePackResult(ETexturePackResultType::Failed, nullptr, outputName, packageName, TEXT("Asset already exists"));
	}

	FImage redImage;
	FImage greenImage;
	FImage blueImage;
	if (!InRequest.redTexture->Source.GetMipImage(redImage, 0))
	{
		return MakePackResult(ETexturePackResultType::Failed, nullptr, outputName, packageName, TEXT("Failed to read R source image"));
	}

	if (!InRequest.greenTexture->Source.GetMipImage(greenImage, 0))
	{
		return MakePackResult(ETexturePackResultType::Failed, nullptr, outputName, packageName, TEXT("Failed to read G source image"));
	}

	if (!InRequest.blueTexture->Source.GetMipImage(blueImage, 0))
	{
		return MakePackResult(ETexturePackResultType::Failed, nullptr, outputName, packageName, TEXT("Failed to read B source image"));
	}

	if (!redImage.IsImageInfoValid() || !greenImage.IsImageInfoValid() || !blueImage.IsImageInfoValid())
	{
		return MakePackResult(ETexturePackResultType::Failed, nullptr, outputName, packageName, TEXT("One or more source images are invalid"));
	}

	FImage packedImage;
	if (!BuildPackImage(redImage, greenImage, blueImage, packedImage))
	{
		return MakePackResult(ETexturePackResultType::Failed, nullptr, outputName, packageName, TEXT("Failed to build packed image"));
	}

	UPackage* package = CreatePackage(*packageName);
	if (!package)
	{
		return MakePackResult(ETexturePackResultType::Failed, nullptr, outputName, packageName, TEXT("Failed to create package"));
	}

	UTexture2D* newTexture = NewObject<UTexture2D>(package, *outputName, RF_Public | RF_Standalone | RF_Transactional);
	if (!newTexture)
	{
		return MakePackResult(ETexturePackResultType::Failed, nullptr, outputName, packageName, TEXT("Failed to create texture asset"));
	}

	(void)newTexture->Modify();
	newTexture->PreEditChange(nullptr);
	newTexture->Source.Init(packedImage);
	CopyPackTextureSettings(InRequest.redTexture, newTexture, InRequest.textureGroup);
	newTexture->PostEditChange();
	(void)newTexture->MarkPackageDirty();
	newTexture->UpdateResource();
	FAssetRegistryModule::AssetCreated(newTexture);

	return MakePackResult(ETexturePackResultType::Created, newTexture, outputName, packageName, TEXT("Texture created"));
}
