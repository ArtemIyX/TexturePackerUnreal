// Copyright Epic Games, Inc. All Rights Reserved.

#include "TexturePackerAssetActions.h"

#include "ContentBrowserModule.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "STexturePackerBatchPackWindow.h"
#include "STexturePackerBatchUnpackWindow.h"
#include "STexturePackerPackWindow.h"
#include "STexturePackerUnpackWindow.h"
#include "Styling/AppStyle.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "TexturePackerAssetActions"

namespace
{
	struct FTexturePackerSelection
	{
		TArray<FAssetData> textureAssets;

		bool HasTextures() const
		{
			return !textureAssets.IsEmpty();
		}

		bool CanPack() const
		{
			return textureAssets.Num() == 3;
		}

		bool CanUnpack() const
		{
			return textureAssets.Num() == 1;
		}

		bool CanBatchUnpack() const
		{
			return textureAssets.Num() >= 2;
		}

		bool CanBatchPack() const
		{
			return textureAssets.Num() >= 3 && textureAssets.Num() % 3 == 0;
		}

		FText GetPackTooltip() const
		{
			if (textureAssets.Num() == 3)
			{
				return LOCTEXT("PackTooltip", "Pack exactly 3 selected Texture2D assets.");
			}

			return FText::Format(
				LOCTEXT("PackDisabledTooltip", "Pack requires exactly 3 selected Texture2D assets. Current selection: {0}."),
				textureAssets.Num());
		}

		FText GetUnpackTooltip() const
		{
			if (textureAssets.Num() == 1)
			{
				return LOCTEXT("UnpackTooltip", "Unpack exactly 1 selected Texture2D asset.");
			}

			return FText::Format(
				LOCTEXT("UnpackDisabledTooltip", "Unpack requires exactly 1 selected Texture2D asset. Current selection: {0}."),
				textureAssets.Num());
		}

		FText GetBatchUnpackTooltip() const
		{
			if (textureAssets.Num() >= 2)
			{
				return LOCTEXT("BatchUnpackTooltip", "Batch unpack 2 or more selected Texture2D assets.");
			}

			return FText::Format(
				LOCTEXT("BatchUnpackDisabledTooltip", "Batch Unpack requires at least 2 selected Texture2D assets. Current selection: {0}."),
				textureAssets.Num());
		}

		FText GetBatchPackTooltip() const
		{
			if (textureAssets.Num() >= 3 && textureAssets.Num() % 3 == 0)
			{
				return LOCTEXT("BatchPackTooltip", "Batch pack selected Texture2D assets in groups of 3.");
			}

			return FText::Format(
				LOCTEXT("BatchPackDisabledTooltip", "Batch Pack requires a selected Texture2D count divisible by 3. Current selection: {0}."),
				textureAssets.Num());
		}

		TArray<FString> GetTexturePaths() const
		{
			TArray<FString> paths;
			paths.Reserve(textureAssets.Num());
			for (const FAssetData& assetData : textureAssets)
			{
				paths.Add(assetData.GetObjectPathString());
			}

			return paths;
		}
	};

	FTexturePackerSelection BuildSelection(const TArray<FAssetData>& InSelectedAssets)
	{
		FTexturePackerSelection selection;
		for (const FAssetData& assetData : InSelectedAssets)
		{
			if (assetData.GetClass(EResolveClass::Yes) == UTexture2D::StaticClass())
			{
				selection.textureAssets.Add(assetData);
			}
		}

		return selection;
	}

	void OpenTexturePackerWindow(const FText& InTitle, const TSharedRef<SWidget>& InContent, const FVector2D InClientSize = FVector2D(520.f, 320.f))
	{
		TSharedRef<SWindow> window = SNew(SWindow)
			.Title(InTitle)
			.ClientSize(InClientSize)
			.SupportsMinimize(false)
			.SupportsMaximize(false);

		window->SetContent(InContent);
		FSlateApplication::Get().AddWindow(window);
	}

	FSlateIcon GetPackIcon()
	{
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Import");
	}

	FSlateIcon GetUnpackIcon()
	{
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Export");
	}

	FSlateIcon GetBatchUnpackIcon()
	{
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen");
	}

	FSlateIcon GetBatchPackIcon()
	{
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderClosed");
	}

	class FTexturePackerMenuActions : public TSharedFromThis<FTexturePackerMenuActions>
	{
	public:
		explicit FTexturePackerMenuActions(FTexturePackerSelection&& InSelection)
			: selection(MoveTemp(InSelection))
		{
		}

		void BuildMenu(FMenuBuilder& InMenuBuilder)
		{
			InMenuBuilder.AddSubMenu(
				LOCTEXT("TexturePackerSubMenuLabel", "Texture Packer"),
				LOCTEXT("TexturePackerSubMenuTooltip", "Texture packing actions for the selected Texture2D assets."),
				FNewMenuDelegate::CreateSP(this, &FTexturePackerMenuActions::BuildSubMenu),
				false,
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Adjust"));
		}

	private:
		void BuildSubMenu(FMenuBuilder& InMenuBuilder)
		{
			InMenuBuilder.AddMenuEntry(
				LOCTEXT("PackLabel", "Pack"),
				selection.GetPackTooltip(),
				GetPackIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FTexturePackerMenuActions::ExecutePack),
					FCanExecuteAction::CreateSP(this, &FTexturePackerMenuActions::CanExecutePack)));
			InMenuBuilder.AddMenuEntry(
				LOCTEXT("UnpackLabel", "Unpack"),
				selection.GetUnpackTooltip(),
				GetUnpackIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FTexturePackerMenuActions::ExecuteUnpack),
					FCanExecuteAction::CreateSP(this, &FTexturePackerMenuActions::CanExecuteUnpack)));
			InMenuBuilder.AddMenuEntry(
				LOCTEXT("BatchUnpackLabel", "Batch Unpack"),
				selection.GetBatchUnpackTooltip(),
				GetBatchUnpackIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FTexturePackerMenuActions::ExecuteBatchUnpack),
					FCanExecuteAction::CreateSP(this, &FTexturePackerMenuActions::CanExecuteBatchUnpack)));
			InMenuBuilder.AddMenuEntry(
				LOCTEXT("BatchPackLabel", "Batch Pack"),
				selection.GetBatchPackTooltip(),
				GetBatchPackIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FTexturePackerMenuActions::ExecuteBatchPack),
					FCanExecuteAction::CreateSP(this, &FTexturePackerMenuActions::CanExecuteBatchPack)));
		}

		bool CanExecutePack() const
		{
			return selection.CanPack();
		}

		bool CanExecuteUnpack() const
		{
			return selection.CanUnpack();
		}

		bool CanExecuteBatchUnpack() const
		{
			return selection.CanBatchUnpack();
		}

		bool CanExecuteBatchPack() const
		{
			return selection.CanBatchPack();
		}

		void ExecutePack()
		{
			OpenTexturePackerWindow(
				LOCTEXT("PackWindowTitle", "Texture Packer - Pack"),
				CreateTexturePackerPackWindow(selection.GetTexturePaths()),
				FVector2D(560.f, 500.f));
		}

		void ExecuteUnpack()
		{
			OpenTexturePackerWindow(
				LOCTEXT("UnpackWindowTitle", "Texture Packer - Unpack"),
				CreateTexturePackerUnpackWindow(selection.GetTexturePaths()),
				FVector2D(560.f, 500.f));
		}

		void ExecuteBatchUnpack()
		{
			OpenTexturePackerWindow(
				LOCTEXT("BatchUnpackWindowTitle", "Texture Packer - Batch Unpack"),
				CreateTexturePackerBatchUnpackWindow(selection.GetTexturePaths()),
				FVector2D(560.f, 620.f));
		}

		void ExecuteBatchPack()
		{
			OpenTexturePackerWindow(
				LOCTEXT("BatchPackWindowTitle", "Texture Packer - Batch Pack"),
				CreateTexturePackerBatchPackWindow(selection.GetTexturePaths()),
				FVector2D(640.f, 620.f));
		}

		FTexturePackerSelection selection;
	};
}

void FTexturePackerAssetActions::Register()
{
	FContentBrowserModule& contentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FContentBrowserMenuExtender_SelectedAssets>& extenders = contentBrowserModule.GetAllAssetViewContextMenuExtenders();
	FContentBrowserMenuExtender_SelectedAssets delegate = FContentBrowserMenuExtender_SelectedAssets::CreateRaw(this, &FTexturePackerAssetActions::OnExtendAssetMenu);
	extenderHandle = delegate.GetHandle();
	extenders.Add(delegate);
}

void FTexturePackerAssetActions::Unregister()
{
	if (!FModuleManager::Get().IsModuleLoaded("ContentBrowser"))
	{
		return;
	}

	FContentBrowserModule& contentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FContentBrowserMenuExtender_SelectedAssets>& extenders = contentBrowserModule.GetAllAssetViewContextMenuExtenders();
	extenders.RemoveAll([this](const FContentBrowserMenuExtender_SelectedAssets& delegate) {
		return delegate.GetHandle() == extenderHandle;
	});
	extenderHandle.Reset();
}

TSharedRef<FExtender> FTexturePackerAssetActions::OnExtendAssetMenu(const TArray<FAssetData>& InSelectedAssets)
{
	const FTexturePackerSelection selection = BuildSelection(InSelectedAssets);
	TSharedRef<FExtender> extender = MakeShared<FExtender>();
	if (!selection.HasTextures())
	{
		return extender;
	}

	TSharedRef<FTexturePackerMenuActions> menuActions = MakeShared<FTexturePackerMenuActions>(FTexturePackerSelection(selection));
	extender->AddMenuExtension(
		"GetAssetActions",
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateLambda([menuActions](FMenuBuilder& InMenuBuilder) {
			menuActions->BuildMenu(InMenuBuilder);
		}));
	return extender;
}

#undef LOCTEXT_NAMESPACE
