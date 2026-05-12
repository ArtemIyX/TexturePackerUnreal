// Copyright Epic Games, Inc. All Rights Reserved.

#include "STexturePackerUnpackWindow.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "TextureUnpackService.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "STexturePackerUnpackWindow"

namespace
{
	enum class ETexturePackerChannel : uint8
	{
		R,
		G,
		B
	};

	struct FChannelConfig
	{
		ETexturePackerChannel channel = ETexturePackerChannel::R;
		const TCHAR* shortLabel = TEXT("");
		const TCHAR* displayName = TEXT("");
		const TCHAR* suffix = TEXT("");
		FLinearColor color = FLinearColor::White;
	};

	struct FChannelState
	{
		bool bEnabled = true;
		FString suffix;
	};

	struct FUnpackSessionSettings
	{
		bool bInitialized = false;
		bool bUseAlphaCompression = true;
		FChannelState channelStates[3];
	};

	constexpr FChannelConfig ChannelConfigs[] = {
		{ ETexturePackerChannel::R, TEXT("R"), TEXT("Occlusion"), TEXT("_Occlusion"), FLinearColor(0.78f, 0.18f, 0.18f) },
		{ ETexturePackerChannel::G, TEXT("G"), TEXT("Roughness"), TEXT("_Roughness"), FLinearColor(0.18f, 0.65f, 0.22f) },
		{ ETexturePackerChannel::B, TEXT("B"), TEXT("Metallic"), TEXT("_Metallic"), FLinearColor(0.20f, 0.40f, 0.82f) }
	};

	const TCHAR* ShortChannelSuffixes[] = {
		TEXT("_O"),
		TEXT("_R"),
		TEXT("_M")
	};

	FUnpackSessionSettings GUnpackSessionSettings;

	FText GetChannelDisplayName(const ETexturePackerChannel InChannel)
	{
		switch (InChannel)
		{
		case ETexturePackerChannel::R:
			return LOCTEXT("ChannelR", "Red");
		case ETexturePackerChannel::G:
			return LOCTEXT("ChannelG", "Green");
		case ETexturePackerChannel::B:
		default:
			return LOCTEXT("ChannelB", "Blue");
		}
	}

	FString BuildBaseTextureName(UTexture2D* InTexture)
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

	bool HasThreeColorChannels(UTexture2D* InTexture)
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

	int32 ToIndex(const ETexturePackerChannel InChannel)
	{
		return static_cast<int32>(InChannel);
	}

	class STexturePackerUnpackWindow final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(STexturePackerUnpackWindow) {}
			SLATE_ARGUMENT(TArray<FString>, TexturePaths)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			RestoreSessionSettings();
			BuildTextureGroupOptions();
			selectedTexture = ResolveInitialTexture(InArgs._TexturePaths);
			baseName = BuildBaseTextureName(selectedTexture.Get());
			ResetTextureGroupFromTexture();

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
									.Text(LOCTEXT("UnpackTitle", "Unpack Textures"))
									.Font(FAppStyle::GetFontStyle("HeadingMedium"))
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
								[
									SNew(STextBlock)
									.Text(LOCTEXT("UnpackDescription", "Split the selected texture into separate channel textures."))
									.AutoWrapText(true)
									.ToolTipText(LOCTEXT("UnpackDescriptionTooltip", "This dialog prepares output textures for the red, green, and blue channels."))
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
									BuildSuffixPresetRow()
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(FMargin(0.f, 12.f, 0.f, 0.f))
								[
									BuildCompressionRow()
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(FMargin(0.f, 12.f, 0.f, 0.f))
								[
									BuildTextureGroupRow()
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(FMargin(0.f, 12.f, 0.f, 0.f))
								[
									SNew(SSeparator)
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
									.Text(this, &STexturePackerUnpackWindow::GetValidationText)
									.ColorAndOpacity(FLinearColor(0.85f, 0.30f, 0.30f))
									.AutoWrapText(true)
									.ToolTipText(this, &STexturePackerUnpackWindow::GetValidationText)
									.Visibility(this, &STexturePackerUnpackWindow::GetValidationVisibility)
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
								.Text(LOCTEXT("CancelLabel", "Cancel"))
								.ToolTipText(LOCTEXT("CancelTooltip", "Close the unpack window without doing anything."))
								.OnClicked(this, &STexturePackerUnpackWindow::OnCancelClicked)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
								.Text(LOCTEXT("UnpackLabel", "Unpack"))
								.ToolTipText(this, &STexturePackerUnpackWindow::GetUnpackButtonTooltip)
								.IsEnabled(this, &STexturePackerUnpackWindow::CanExecuteUnpack)
								.OnClicked(this, &STexturePackerUnpackWindow::OnUnpackClicked)
							]
						]
					]
				]
			];
		}

	private:
		TSharedRef<SWidget> BuildSourcePanel()
		{
			return SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
				.Padding(12.f)
				.ToolTipText(LOCTEXT("SourcePanelTooltip", "Texture that will be split into red, green, and blue channel outputs."))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SourcePanelLabel", "Source Texture"))
						.Font(FAppStyle::GetFontStyle("BoldFont"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(UTexture2D::StaticClass())
						.DisplayUseSelected(true)
						.DisplayBrowse(true)
						.DisplayThumbnail(true)
						.ObjectPath(this, &STexturePackerUnpackWindow::GetSelectedTexturePath)
						.OnObjectChanged(this, &STexturePackerUnpackWindow::OnSelectedTextureChanged)
							.ToolTipText(LOCTEXT("SelectedTexturePickerTooltip", "Choose the Texture2D asset to unpack."))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 12.f, 0.f, 0.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.f, 0.f, 8.f, 0.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("BaseNameLabel", "Base Name"))
							.ToolTipText(LOCTEXT("BaseNameTooltip", "Base asset name used to build default output names. Example: T_Base_Occlusion."))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						[
							SNew(SEditableTextBox)
							.Text(this, &STexturePackerUnpackWindow::GetBaseNameText)
							.OnTextChanged(this, &STexturePackerUnpackWindow::OnBaseNameChanged)
							.ToolTipText(LOCTEXT("BaseNameTextBoxTooltip", "Edit the shared base name used to build final output names."))
						]
					]
				];
		}

		TSharedRef<SWidget> BuildSuffixPresetRow()
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SuffixPresetLabel", "Suffix Preset"))
					.ToolTipText(LOCTEXT("SuffixPresetTooltip", "Apply preset suffixes to the channel rows."))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(SButton)
					.Text(LOCTEXT("SuffixPresetLong", "Long"))
					.ToolTipText(LOCTEXT("SuffixPresetLongTooltip", "Use _Occlusion, _Roughness, and _Metallic."))
					.OnClicked(this, &STexturePackerUnpackWindow::OnLongPresetClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("SuffixPresetShort", "Short"))
					.ToolTipText(LOCTEXT("SuffixPresetShortTooltip", "Use _O, _R, and _M."))
					.OnClicked(this, &STexturePackerUnpackWindow::OnShortPresetClicked)
				];
		}

		TSharedRef<SWidget> BuildCompressionRow()
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked(this, &STexturePackerUnpackWindow::GetAlphaCompressionCheckState)
					.OnCheckStateChanged(this, &STexturePackerUnpackWindow::OnAlphaCompressionCheckStateChanged)
					.ToolTipText(LOCTEXT("AlphaCompressionTooltip", "New textures should use alpha compression for single-channel data read from red only."))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AlphaCompressionLabel", "Compression: Alpha"))
					.ToolTipText(LOCTEXT("AlphaCompressionLabelTooltip", "Enable alpha compression for the unpacked textures."))
				];
		}

		TSharedRef<SWidget> BuildTextureGroupRow()
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TextureGroupLabel", "Texture Group"))
					.ToolTipText(LOCTEXT("TextureGroupTooltip", "Texture group that new unpacked textures should use. Defaults to the source texture group."))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(STextComboBox)
					.OptionsSource(&textureGroupOptions)
					.InitiallySelectedItem(selectedTextureGroupOption)
					.OnSelectionChanged(this, &STexturePackerUnpackWindow::OnTextureGroupSelectionChanged)
					.ToolTipText(LOCTEXT("TextureGroupComboTooltip", "Choose the texture group for newly created unpacked textures."))
				];
		}

		TSharedRef<SWidget> BuildOutputPanel()
		{
			TSharedRef<SVerticalBox> panel = SNew(SVerticalBox);
			panel->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 10.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OutputPanelLabel", "Channel Outputs"))
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.ToolTipText(LOCTEXT("OutputPanelTooltip", "Each enabled row creates one output texture from a source color channel."))
			];

			for (const FChannelConfig& config : ChannelConfigs)
			{
				panel->AddSlot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 8.f)
				[
					BuildChannelRow(config)
				];
			}

			return panel;
		}

		TSharedRef<SWidget> BuildChannelRow(const FChannelConfig& InConfig)
		{
			return SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(10.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.f, 0.f, 10.f, 0.f)
					[
						SNew(SCheckBox)
						.IsChecked(this, &STexturePackerUnpackWindow::GetChannelCheckState, InConfig.channel)
						.OnCheckStateChanged(this, &STexturePackerUnpackWindow::OnChannelCheckStateChanged, InConfig.channel)
						.ToolTipText(FText::Format(LOCTEXT("ChannelEnabledTooltip", "Enable output generation for the {0} channel."), GetChannelDisplayName(InConfig.channel)))
					]
					+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.f, 0.f, 12.f, 0.f)
					[
						BuildChannelBadge(InConfig)
					]
					+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(InConfig.displayName))
							.ToolTipText(FText::Format(LOCTEXT("ChannelLabelTooltip", "{0} channel output."), GetChannelDisplayName(InConfig.channel)))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
						[
							SNew(SEditableTextBox)
							.Text(this, &STexturePackerUnpackWindow::GetChannelSuffixText, InConfig.channel)
							.OnTextChanged(this, &STexturePackerUnpackWindow::OnChannelSuffixChanged, InConfig.channel)
							.IsEnabled(this, &STexturePackerUnpackWindow::IsChannelEnabled, InConfig.channel)
							.ToolTipText(FText::Format(LOCTEXT("ChannelNameTooltip", "Editable suffix for the {0} channel output. Full asset name is Base Name plus this suffix."), GetChannelDisplayName(InConfig.channel)))
						]
					]
				];
		}

		TSharedRef<SWidget> BuildChannelBadge(const FChannelConfig& InConfig) const
		{
			return SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(InConfig.color)
				.Padding(FMargin(10.f, 6.f))
				.ToolTipText(FText::Format(LOCTEXT("ChannelArrowTooltip", "{0} channel output."), GetChannelDisplayName(InConfig.channel)))
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

		UTexture2D* ResolveInitialTexture(const TArray<FString>& InTexturePaths) const
		{
			if (InTexturePaths.IsEmpty())
			{
				return nullptr;
			}

			return Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, *InTexturePaths[0]));
		}

		void RestoreSessionSettings()
		{
			if (!GUnpackSessionSettings.bInitialized)
			{
				for (const FChannelConfig& config : ChannelConfigs)
				{
					FChannelState& state = channelStates[ToIndex(config.channel)];
					state.bEnabled = true;
					state.suffix = config.suffix;
				}

				SaveSessionSettings();
				return;
			}

			for (const FChannelConfig& config : ChannelConfigs)
			{
				channelStates[ToIndex(config.channel)] = GUnpackSessionSettings.channelStates[ToIndex(config.channel)];
			}

			bUseAlphaCompression = GUnpackSessionSettings.bUseAlphaCompression;
		}

		void SaveSessionSettings()
		{
			GUnpackSessionSettings.bInitialized = true;
			GUnpackSessionSettings.bUseAlphaCompression = bUseAlphaCompression;
			for (const FChannelConfig& config : ChannelConfigs)
			{
				GUnpackSessionSettings.channelStates[ToIndex(config.channel)] = channelStates[ToIndex(config.channel)];
			}
		}

		FString BuildChannelOutputName(const FChannelConfig& InConfig) const
		{
			return baseName + channelStates[ToIndex(InConfig.channel)].suffix;
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

		void ResetTextureGroupFromTexture()
		{
			selectedTextureGroup = selectedTexture.IsValid() ? static_cast<TextureGroup>(selectedTexture->LODGroup.GetValue()) : TEXTUREGROUP_World;
			selectedTextureGroupOption = textureGroupOptions.IsValidIndex(static_cast<int32>(selectedTextureGroup))
				? textureGroupOptions[static_cast<int32>(selectedTextureGroup)]
				: nullptr;
		}

		FString GetSelectedTexturePath() const
		{
			return selectedTexture.IsValid() ? selectedTexture->GetPathName() : FString();
		}

		void OnSelectedTextureChanged(const FAssetData& InAssetData)
		{
			selectedTexture = Cast<UTexture2D>(InAssetData.GetAsset());
			baseName = BuildBaseTextureName(selectedTexture.Get());
			ResetTextureGroupFromTexture();
		}

		FText GetBaseNameText() const
		{
			return FText::FromString(baseName);
		}

		void OnBaseNameChanged(const FText& InText)
		{
			baseName = InText.ToString();
		}

		ECheckBoxState GetChannelCheckState(const ETexturePackerChannel InChannel) const
		{
			return channelStates[ToIndex(InChannel)].bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}

		void OnChannelCheckStateChanged(ECheckBoxState InCheckState, ETexturePackerChannel InChannel)
		{
			channelStates[ToIndex(InChannel)].bEnabled = InCheckState == ECheckBoxState::Checked;
			SaveSessionSettings();
		}

		bool IsChannelEnabled(const ETexturePackerChannel InChannel) const
		{
			return channelStates[ToIndex(InChannel)].bEnabled;
		}

		FText GetChannelSuffixText(const ETexturePackerChannel InChannel) const
		{
			return FText::FromString(channelStates[ToIndex(InChannel)].suffix);
		}

		void OnChannelSuffixChanged(const FText& InText, ETexturePackerChannel InChannel)
		{
			FChannelState& state = channelStates[ToIndex(InChannel)];
			state.suffix = InText.ToString();
			SaveSessionSettings();
		}

		ECheckBoxState GetAlphaCompressionCheckState() const
		{
			return bUseAlphaCompression ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}

		void OnAlphaCompressionCheckStateChanged(ECheckBoxState InCheckState)
		{
			bUseAlphaCompression = InCheckState == ECheckBoxState::Checked;
			SaveSessionSettings();
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

		FReply OnLongPresetClicked()
		{
			for (const FChannelConfig& config : ChannelConfigs)
			{
				channelStates[ToIndex(config.channel)].suffix = config.suffix;
			}

			SaveSessionSettings();
			return FReply::Handled();
		}

		FReply OnShortPresetClicked()
		{
			for (const FChannelConfig& config : ChannelConfigs)
			{
				channelStates[ToIndex(config.channel)].suffix = ShortChannelSuffixes[ToIndex(config.channel)];
			}

			SaveSessionSettings();
			return FReply::Handled();
		}

		FText GetValidationText() const
		{
			return Validate().message;
		}

		EVisibility GetValidationVisibility() const
		{
			return Validate().message.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
		}

		FText GetUnpackButtonTooltip() const
		{
			const FValidationResult validation = Validate();
			return validation.bIsValid
				? LOCTEXT("UnpackButtonTooltip", "Review the creation summary and create the unpacked textures.")
				: validation.message;
		}

		bool CanExecuteUnpack() const
		{
			return Validate().bIsValid;
		}

		FReply OnUnpackClicked()
		{
			const FValidationResult validation = Validate();
			if (!validation.bIsValid)
			{
				return FReply::Handled();
			}

			FTextureUnpackRequest request;
			request.sourceTexture = selectedTexture.Get();
			request.baseName = baseName.TrimStartAndEnd();
			request.textureGroup = selectedTextureGroup;
			request.bUseAlphaCompression = bUseAlphaCompression;
			request.channels.Reserve(UE_ARRAY_COUNT(channelStates));
			for (const FChannelConfig& config : ChannelConfigs)
			{
				FTextureUnpackChannelRequest& channelRequest = request.channels.AddDefaulted_GetRef();
				channelRequest.channel = static_cast<ETexturePackerColorChannel>(config.channel);
				channelRequest.bEnabled = channelStates[ToIndex(config.channel)].bEnabled;
				channelRequest.suffix = channelStates[ToIndex(config.channel)].suffix.TrimStartAndEnd();
			}

			int32 enabledChannelCount = 0;
			for (const FTextureUnpackChannelRequest& channelRequest : request.channels)
			{
				enabledChannelCount += channelRequest.bEnabled ? 1 : 0;
			}

			if (FMessageDialog::Open(
					EAppMsgType::OkCancel,
					FText::FromString(FString::Printf(
						TEXT("Create %d unpacked textures from '%s'?\n\nBase Name: %s\nCompression: %s\nTexture Group: %s\nDestination: same folder as the source texture.\nUndo: one undo step will revert created assets."),
						enabledChannelCount,
						request.sourceTexture ? *request.sourceTexture->GetName() : TEXT("Unknown"),
						*request.baseName,
						request.bUseAlphaCompression ? TEXT("Alpha") : TEXT("Copy source compression"),
						selectedTextureGroupOption.IsValid() ? **selectedTextureGroupOption : TEXT("World"))))
				!= EAppReturnType::Ok)
			{
				return FReply::Handled();
			}

			const FScopedTransaction transaction(LOCTEXT("UnpackTextureTransaction", "Unpack Texture"));
			const FTextureUnpackResult unpackResult = FTextureUnpackService::UnpackTexture(request);
			FString itemLines;
			for (const FTextureUnpackItemResult& item : unpackResult.items)
			{
				if (!itemLines.IsEmpty())
				{
					itemLines += TEXT("\n");
				}

				const TCHAR* typeLabel = TEXT("Failed");
				switch (item.type)
				{
				case ETextureUnpackItemResultType::Created:
					typeLabel = TEXT("Created");
					break;
				case ETextureUnpackItemResultType::Skipped:
					typeLabel = TEXT("Skipped");
					break;
				case ETextureUnpackItemResultType::Invalid:
					typeLabel = TEXT("Invalid");
					break;
				case ETextureUnpackItemResultType::Failed:
				default:
					break;
				}

				itemLines += FString::Printf(
					TEXT("[%s] %s | %s"),
					typeLabel,
					*item.assetName,
					*item.message);
			}

			SNotificationItem::ECompletionState completionState = unpackResult.failedCount == 0 && unpackResult.invalidCount == 0
				? SNotificationItem::CS_Success
				: SNotificationItem::CS_Fail;
			FNotificationInfo notificationInfo(FText::FromString(FString::Printf(
				TEXT("Created: %d | Skipped: %d | Invalid: %d | Failed: %d"),
				unpackResult.createdCount,
				unpackResult.skippedCount,
				unpackResult.invalidCount,
				unpackResult.failedCount)));
			notificationInfo.ExpireDuration = 5.0f;
			notificationInfo.HyperlinkText = LOCTEXT("UnpackResultDetailsLink", "Details");
			notificationInfo.Hyperlink = FSimpleDelegate::CreateLambda([unpackResult, itemLines]() {
				FMessageDialog::Open(
					EAppMsgType::Ok,
					FText::FromString(FString::Printf(
						TEXT("Created: %d\nSkipped: %d\nInvalid: %d\nFailed: %d\n\n%s"),
						unpackResult.createdCount,
						unpackResult.skippedCount,
						unpackResult.invalidCount,
						unpackResult.failedCount,
						*itemLines)),
					LOCTEXT("UnpackResultTitle", "Unpack Textures"));
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

		struct FValidationResult
		{
			bool bIsValid = false;
			FText message;
		};

		FValidationResult Validate() const
		{
			if (!selectedTexture.IsValid())
			{
				return { false, LOCTEXT("ValidationNoTexture", "Select a texture to unpack.") };
			}

			if (baseName.TrimStartAndEnd().IsEmpty())
			{
				return { false, LOCTEXT("ValidationBaseName", "Base name is required.") };
			}

			if (!HasThreeColorChannels(selectedTexture.Get()))
			{
				return { false, LOCTEXT("ValidationThreeChannels", "Selected texture must contain red, green, and blue color channels.") };
			}

			TSet<FString> usedNames;
			bool bHasEnabledChannel = false;
			for (const FChannelConfig& config : ChannelConfigs)
			{
				const FChannelState& state = channelStates[ToIndex(config.channel)];
				if (!state.bEnabled)
				{
					continue;
				}

				bHasEnabledChannel = true;

				const FString trimmedName = state.suffix.TrimStartAndEnd();
				if (trimmedName.IsEmpty())
				{
					return { false, FText::Format(LOCTEXT("ValidationNameRequired", "{0} suffix is required when enabled."), GetChannelDisplayName(config.channel)) };
				}

				const FString fullName = baseName + trimmedName;
				if (usedNames.Contains(fullName))
				{
					return { false, LOCTEXT("ValidationUniqueNames", "Enabled output names must be different.") };
				}

				usedNames.Add(fullName);
			}

			if (!bHasEnabledChannel)
			{
				return { false, LOCTEXT("ValidationEnabledChannel", "Enable at least one output channel.") };
			}

			return { true, FText::GetEmpty() };
		}

		TWeakObjectPtr<UTexture2D> selectedTexture;
		FString baseName;
		bool bUseAlphaCompression = true;
		TextureGroup selectedTextureGroup = TEXTUREGROUP_World;
		TArray<TSharedPtr<FString>> textureGroupOptions;
		TSharedPtr<FString> selectedTextureGroupOption;
		FChannelState channelStates[3];
		TSharedPtr<STextBlock> validationTextBlock;
	};
}

TSharedRef<SWidget> CreateTexturePackerUnpackWindow(const TArray<FString>& InTexturePaths)
{
	return SNew(STexturePackerUnpackWindow)
		.TexturePaths(InTexturePaths);
}

#undef LOCTEXT_NAMESPACE
