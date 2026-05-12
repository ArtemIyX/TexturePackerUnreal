// Copyright Epic Games, Inc. All Rights Reserved.

#include "STexturePackerPackWindow.h"

#include "Engine/Texture2D.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "TexturePackService.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "STexturePackerPackWindow"

namespace
{
	enum class EPackChannel : uint8
	{
		R,
		G,
		B
	};

	struct FPackChannelConfig
	{
		EPackChannel channel = EPackChannel::R;
		const TCHAR* shortLabel = TEXT("");
		const TCHAR* displayName = TEXT("");
		FLinearColor color = FLinearColor::White;
	};

	constexpr FPackChannelConfig PackChannelConfigs[] = {
		{ EPackChannel::R, TEXT("R"), TEXT("Red"), FLinearColor(0.78f, 0.18f, 0.18f) },
		{ EPackChannel::G, TEXT("G"), TEXT("Green"), FLinearColor(0.18f, 0.65f, 0.22f) },
		{ EPackChannel::B, TEXT("B"), TEXT("Blue"), FLinearColor(0.20f, 0.40f, 0.82f) }
	};

	int32 ToIndex(const EPackChannel InChannel)
	{
		return static_cast<int32>(InChannel);
	}

	FString BuildPackBaseTextureName(UTexture2D* InTexture)
	{
		if (!InTexture)
		{
			return FString();
		}

		const FString textureName = InTexture->GetName();
		int32 lastUnderscoreIndex = INDEX_NONE;
		if (textureName.FindLastChar(TEXT('_'), lastUnderscoreIndex) && lastUnderscoreIndex > 0)
		{
			return textureName.Left(lastUnderscoreIndex);
		}

		return textureName;
	}

	FText GetChannelDisplayName(const EPackChannel InChannel)
	{
		switch (InChannel)
		{
		case EPackChannel::R:
			return LOCTEXT("PackChannelR", "Red");
		case EPackChannel::G:
			return LOCTEXT("PackChannelG", "Green");
		case EPackChannel::B:
		default:
			return LOCTEXT("PackChannelB", "Blue");
		}
	}

	class STexturePackerPackWindow final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(STexturePackerPackWindow) {}
			SLATE_ARGUMENT(TArray<FString>, TexturePaths)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			BuildTextureGroupOptions();
			ResolveInitialTextures(InArgs._TexturePaths);
			ResetOutputNameFromSelection();
			ResetTextureGroupFromSelection();

			ChildSlot
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(16.f)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(STextBlock)
									.Text(LOCTEXT("PackTitle", "Pack Textures"))
									.Font(FAppStyle::GetFontStyle("HeadingMedium"))
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
								[
									SNew(STextBlock)
									.Text(LOCTEXT("PackDescription", "Pack three Texture2D assets into one RGB mask texture."))
									.AutoWrapText(true)
									.ToolTipText(LOCTEXT("PackDescriptionTooltip", "The red, green, and blue textures are packed into one output texture. Output will use Masks compression and sRGB will be disabled."))
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(FMargin(0.f, 12.f, 0.f, 0.f))
								[
									BuildSourcePanel()
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(FMargin(0.f, 12.f, 0.f, 0.f))
								[
									BuildOutputPanel()
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(FMargin(0.f, 12.f, 0.f, 0.f))
								[
									SAssignNew(validationTextBlock, STextBlock)
									.Text(this, &STexturePackerPackWindow::GetValidationText)
									.ColorAndOpacity(FLinearColor(0.85f, 0.30f, 0.30f))
									.AutoWrapText(true)
									.ToolTipText(this, &STexturePackerPackWindow::GetValidationText)
									.Visibility(this, &STexturePackerPackWindow::GetValidationVisibility)
								]
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(16.f, 0.f, 16.f, 16.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.f, 12.f, 0.f, 12.f))
						[
							SNew(SSeparator)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.f, 0.f, 8.f, 0.f)
							[
								SNew(SButton)
								.Text(LOCTEXT("PackCancelLabel", "Cancel"))
								.ToolTipText(LOCTEXT("PackCancelTooltip", "Close the pack window without doing anything."))
								.OnClicked(this, &STexturePackerPackWindow::OnCancelClicked)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
								.Text(LOCTEXT("PackRunLabel", "Pack"))
								.ToolTipText(this, &STexturePackerPackWindow::GetPackButtonTooltip)
								.IsEnabled(this, &STexturePackerPackWindow::CanExecutePack)
								.OnClicked(this, &STexturePackerPackWindow::OnPackClicked)
							]
						]
					]
				]
			];
		}

	private:
		TSharedRef<SWidget> BuildSourcePanel()
		{
			TSharedRef<SVerticalBox> panel = SNew(SVerticalBox);
			panel->AddSlot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PackSourcesLabel", "Source Textures"))
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.ToolTipText(LOCTEXT("PackSourcesTooltip", "Choose the three different textures that should fill the output red, green, and blue channels."))
			];

			for (const FPackChannelConfig& config : PackChannelConfigs)
			{
				panel->AddSlot()
				.AutoHeight()
				.Padding(FMargin(0.f, 8.f, 0.f, 0.f))
				[
					BuildChannelRow(config)
				];
			}

			return SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
				.Padding(12.f)
				[
					panel
				];
		}

		TSharedRef<SWidget> BuildChannelRow(const FPackChannelConfig& InConfig)
		{
			return SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(10.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 12.f, 0.f)
					[
						BuildChannelBadge(InConfig)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(UTexture2D::StaticClass())
						.DisplayUseSelected(true)
						.DisplayBrowse(true)
						.DisplayThumbnail(true)
						.ObjectPath(this, &STexturePackerPackWindow::GetTexturePath, InConfig.channel)
						.OnObjectChanged(this, &STexturePackerPackWindow::OnTextureChanged, InConfig.channel)
						.ToolTipText(FText::Format(LOCTEXT("PackChannelPickerTooltip", "Choose the texture for the {0} output channel."), GetChannelDisplayName(InConfig.channel)))
					]
				];
		}

		TSharedRef<SWidget> BuildChannelBadge(const FPackChannelConfig& InConfig) const
		{
			return SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(InConfig.color)
				.Padding(FMargin(10.f, 6.f))
				.ToolTipText(FText::Format(LOCTEXT("PackChannelBadgeTooltip", "{0} source texture."), GetChannelDisplayName(InConfig.channel)))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(InConfig.shortLabel))
						.Font(FAppStyle::GetFontStyle("BoldFont"))
						.ColorAndOpacity(FSlateColor(FLinearColor::White))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(8.f, 0.f, 0.f, 0.f))
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.ArrowRight"))
						.ColorAndOpacity(FSlateColor(FLinearColor::White))
					]
				];
		}

		TSharedRef<SWidget> BuildOutputPanel()
		{
			return SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
				.Padding(12.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PackOutputLabel", "Output"))
						.Font(FAppStyle::GetFontStyle("BoldFont"))
						.ToolTipText(LOCTEXT("PackOutputTooltip", "Exact output asset settings for the packed texture."))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.f, 0.f, 8.f, 0.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("PackNameLabel", "Name"))
							.ToolTipText(LOCTEXT("PackNameTooltip", "Exact name of the new packed texture asset."))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						[
							SNew(SEditableTextBox)
							.Text(this, &STexturePackerPackWindow::GetOutputNameText)
							.OnTextChanged(this, &STexturePackerPackWindow::OnOutputNameChanged)
							.ToolTipText(LOCTEXT("PackNameTextBoxTooltip", "Edit the exact asset name for the packed texture."))
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.f, 0.f, 8.f, 0.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("PackTextureGroupLabel", "Texture Group"))
							.ToolTipText(LOCTEXT("PackTextureGroupTooltip", "Texture group for the new packed texture. Defaults to the first selected texture."))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						[
							SNew(STextComboBox)
							.OptionsSource(&textureGroupOptions)
							.InitiallySelectedItem(selectedTextureGroupOption)
							.OnSelectionChanged(this, &STexturePackerPackWindow::OnTextureGroupSelectionChanged)
							.ToolTipText(LOCTEXT("PackTextureGroupComboTooltip", "Choose the texture group for the packed texture."))
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PackOutputInfo", "Output will use sRGB = false and Compression = Masks."))
						.ToolTipText(LOCTEXT("PackOutputInfoTooltip", "These settings are fixed for packed mask output textures."))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f)))
					]
				];
		}

		void ResolveInitialTextures(const TArray<FString>& InTexturePaths)
		{
			for (int32 index = 0; index < UE_ARRAY_COUNT(selectedTextures); ++index)
			{
				selectedTextures[index] = nullptr;
				if (!InTexturePaths.IsValidIndex(index))
				{
					continue;
				}

				selectedTextures[index] = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, *InTexturePaths[index]));
			}
		}

		void BuildTextureGroupOptions()
		{
			textureGroupOptions.Reset();
			const UEnum* textureGroupEnum = StaticEnum<TextureGroup>();
			if (!textureGroupEnum)
			{
				return;
			}

			for (int32 groupIndex = 0; groupIndex < static_cast<int32>(TEXTUREGROUP_MAX); ++groupIndex)
			{
				textureGroupOptions.Add(MakeShared<FString>(textureGroupEnum->GetDisplayNameTextByValue(groupIndex).ToString()));
			}
		}

		void ResetTextureGroupFromSelection()
		{
			selectedTextureGroup = selectedTextures[0].IsValid() ? static_cast<TextureGroup>(selectedTextures[0]->LODGroup.GetValue()) : TEXTUREGROUP_World;
			selectedTextureGroupOption = textureGroupOptions.IsValidIndex(static_cast<int32>(selectedTextureGroup))
				? textureGroupOptions[static_cast<int32>(selectedTextureGroup)]
				: nullptr;
		}

		void ResetOutputNameFromSelection()
		{
			const FString firstBase = BuildPackBaseTextureName(selectedTextures[0].Get());
			const FString secondBase = BuildPackBaseTextureName(selectedTextures[1].Get());
			const FString thirdBase = BuildPackBaseTextureName(selectedTextures[2].Get());

			if (!firstBase.IsEmpty() && firstBase == secondBase && firstBase == thirdBase)
			{
				outputName = firstBase + TEXT("_ORM");
				return;
			}

			if (!firstBase.IsEmpty())
			{
				outputName = firstBase + TEXT("_ORM");
				return;
			}

			outputName = TEXT("T_Packed_ORM");
		}

		FString GetTexturePath(const EPackChannel InChannel) const
		{
			const TWeakObjectPtr<UTexture2D> texture = selectedTextures[ToIndex(InChannel)];
			return texture.IsValid() ? texture->GetPathName() : FString();
		}

		void OnTextureChanged(const FAssetData& InAssetData, EPackChannel InChannel)
		{
			selectedTextures[ToIndex(InChannel)] = Cast<UTexture2D>(InAssetData.GetAsset());
			ResetOutputNameFromSelection();
			ResetTextureGroupFromSelection();
		}

		FText GetOutputNameText() const
		{
			return FText::FromString(outputName);
		}

		void OnOutputNameChanged(const FText& InText)
		{
			outputName = InText.ToString();
		}

		void OnTextureGroupSelectionChanged(TSharedPtr<FString> InItem, ESelectInfo::Type)
		{
			selectedTextureGroupOption = InItem;
			if (!InItem.IsValid())
			{
				return;
			}

			const int32 optionIndex = textureGroupOptions.IndexOfByKey(InItem);
			if (textureGroupOptions.IsValidIndex(optionIndex))
			{
				selectedTextureGroup = static_cast<TextureGroup>(optionIndex);
			}
		}

		struct FValidationResult
		{
			bool bIsValid = false;
			FText message;
		};

		FValidationResult Validate() const
		{
			TSet<UTexture2D*> uniqueTextures;
			for (const FPackChannelConfig& config : PackChannelConfigs)
			{
				UTexture2D* texture = selectedTextures[ToIndex(config.channel)].Get();
				if (!texture)
				{
					return { false, FText::Format(LOCTEXT("PackTextureRequired", "{0} texture is required."), GetChannelDisplayName(config.channel)) };
				}

				if (uniqueTextures.Contains(texture))
				{
					return { false, LOCTEXT("PackTexturesDifferent", "R, G, and B textures must be different assets.") };
				}

				uniqueTextures.Add(texture);
			}

			if (outputName.TrimStartAndEnd().IsEmpty())
			{
				return { false, LOCTEXT("PackOutputNameRequired", "Output name is required.") };
			}

			return { true, FText::GetEmpty() };
		}

		FText GetValidationText() const
		{
			return Validate().message;
		}

		EVisibility GetValidationVisibility() const
		{
			return Validate().message.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
		}

		bool CanExecutePack() const
		{
			return Validate().bIsValid;
		}

		FText GetPackButtonTooltip() const
		{
			const FValidationResult validation = Validate();
			return validation.bIsValid
				? LOCTEXT("PackButtonTooltip", "Review the creation summary and create the packed texture.")
				: validation.message;
		}

		FReply OnPackClicked()
		{
			const FValidationResult validation = Validate();
			if (!validation.bIsValid)
			{
				return FReply::Handled();
			}

			FTexturePackRequest request;
			request.redTexture = selectedTextures[ToIndex(EPackChannel::R)].Get();
			request.greenTexture = selectedTextures[ToIndex(EPackChannel::G)].Get();
			request.blueTexture = selectedTextures[ToIndex(EPackChannel::B)].Get();
			request.outputName = outputName.TrimStartAndEnd();
			request.textureGroup = selectedTextureGroup;

			if (FMessageDialog::Open(
					EAppMsgType::OkCancel,
					FText::FromString(FString::Printf(
						TEXT("Create packed texture '%s'?\n\nSource textures: 3\nDestination folder: %s\nCompression: Masks\nsRGB: false\nUndo: one undo step will revert the created asset."),
						*request.outputName,
						request.redTexture ? *request.redTexture->GetOutermost()->GetName() : TEXT("Unknown"))))
				!= EAppReturnType::Ok)
			{
				return FReply::Handled();
			}

			const FScopedTransaction transaction(LOCTEXT("PackTextureTransaction", "Pack Texture"));
			const FTexturePackResult packResult = FTexturePackService::PackTexture(request);
			const TCHAR* typeLabel = TEXT("Failed");
			SNotificationItem::ECompletionState completionState = SNotificationItem::CS_Fail;
			switch (packResult.type)
			{
			case ETexturePackResultType::Created:
				typeLabel = TEXT("Created");
				completionState = SNotificationItem::CS_Success;
				break;
			case ETexturePackResultType::Invalid:
				typeLabel = TEXT("Invalid");
				break;
			case ETexturePackResultType::Failed:
			default:
				break;
			}

			FNotificationInfo notificationInfo(FText::FromString(FString::Printf(
				TEXT("[%s] %s"),
				typeLabel,
				*packResult.message)));
			notificationInfo.ExpireDuration = 5.0f;
			notificationInfo.HyperlinkText = LOCTEXT("PackResultDetailsLink", "Details");
			notificationInfo.Hyperlink = FSimpleDelegate::CreateLambda([packResult, typeLabel]() {
				FMessageDialog::Open(
					EAppMsgType::Ok,
					FText::FromString(FString::Printf(
						TEXT("[%s]\n%s\n\nAsset: %s"),
						typeLabel,
						*packResult.message,
						*packResult.packageName)),
					LOCTEXT("PackResultTitle", "Pack Textures"));
			});
			if (const TSharedPtr<SNotificationItem> notification = FSlateNotificationManager::Get().AddNotification(notificationInfo))
			{
				notification->SetCompletionState(completionState);
			}

			CloseWindow();
			return FReply::Handled();
		}

		FReply OnCancelClicked()
		{
			CloseWindow();
			return FReply::Handled();
		}

		void CloseWindow() const
		{
			if (const TSharedPtr<SWindow> window = FSlateApplication::Get().FindWidgetWindow(AsShared()))
			{
				window->RequestDestroyWindow();
			}
		}

		TWeakObjectPtr<UTexture2D> selectedTextures[3];
		FString outputName;
		TextureGroup selectedTextureGroup = TEXTUREGROUP_World;
		TArray<TSharedPtr<FString>> textureGroupOptions;
		TSharedPtr<FString> selectedTextureGroupOption;
		TSharedPtr<STextBlock> validationTextBlock;
	};
}

TSharedRef<SWidget> CreateTexturePackerPackWindow(const TArray<FString>& InTexturePaths)
{
	return SNew(STexturePackerPackWindow)
		.TexturePaths(InTexturePaths);
}

#undef LOCTEXT_NAMESPACE
