// Copyright Epic Games, Inc. All Rights Reserved.

#include "STexturePackerBatchPackWindow.h"

#include "Engine/Texture2D.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/AsyncTaskNotification.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"
#include "TextureBatchPackService.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "STexturePackerBatchPackWindow"

namespace
{
	enum class EBatchPackChannel : uint8
	{
		R,
		G,
		B
	};

	enum class EBatchPackSuffixPreset : uint8
	{
		Long,
		Short
	};

	struct FBatchPackChannelConfig
	{
		EBatchPackChannel channel = EBatchPackChannel::R;
		const TCHAR* shortLabel = TEXT("");
		const TCHAR* displayName = TEXT("");
		const TCHAR* longSuffix = TEXT("");
		const TCHAR* shortSuffix = TEXT("");
		FLinearColor color = FLinearColor::White;
	};

	struct FBatchPackGroupItem
	{
		FString baseName;
		TWeakObjectPtr<UTexture2D> textures[3];
		FString outputName;
		FString statusText;
		bool bValid = false;
	};

	struct FBatchPackWindowSettings
	{
		bool bInitialized = false;
		int32 textureGroupIndex = 0;
		FString suffixes[3];
	};

	constexpr FBatchPackChannelConfig BatchPackChannelConfigs[] = {
		{ EBatchPackChannel::R, TEXT("R"), TEXT("Occlusion"), TEXT("_Occlusion"), TEXT("_O"), FLinearColor(0.78f, 0.18f, 0.18f) },
		{ EBatchPackChannel::G, TEXT("G"), TEXT("Roughness"), TEXT("_Roughness"), TEXT("_R"), FLinearColor(0.18f, 0.65f, 0.22f) },
		{ EBatchPackChannel::B, TEXT("B"), TEXT("Metallic"), TEXT("_Metallic"), TEXT("_M"), FLinearColor(0.20f, 0.40f, 0.82f) }
	};

	FBatchPackWindowSettings GBatchPackWindowSettings;

	int32 BatchPackToIndex(const EBatchPackChannel InChannel)
	{
		return static_cast<int32>(InChannel);
	}

	FString BuildBatchPackOutputName(const FString& InBaseName)
	{
		return InBaseName.IsEmpty() ? TEXT("T_Packed_ORM") : InBaseName + TEXT("_ORM");
	}

	FText GetBatchPackChannelDisplayName(const EBatchPackChannel InChannel)
	{
		switch (InChannel)
		{
		case EBatchPackChannel::R:
			return LOCTEXT("BatchPackChannelR", "Red");
		case EBatchPackChannel::G:
			return LOCTEXT("BatchPackChannelG", "Green");
		case EBatchPackChannel::B:
		default:
			return LOCTEXT("BatchPackChannelB", "Blue");
		}
	}

	bool TryMatchBatchPackTexture(
		const FString& InTextureName,
		const FString InSuffixes[3],
		FString& OutBaseName,
		EBatchPackChannel& OutChannel)
	{
		int32 bestSuffixLength = -1;
		bool bMatched = false;

		for (const FBatchPackChannelConfig& config : BatchPackChannelConfigs)
		{
			TArray<FString, TInlineAllocator<3>> candidateSuffixes;
			candidateSuffixes.AddUnique(InSuffixes[BatchPackToIndex(config.channel)].TrimStartAndEnd());
			candidateSuffixes.AddUnique(config.longSuffix);
			candidateSuffixes.AddUnique(config.shortSuffix);

			for (const FString& suffix : candidateSuffixes)
			{
				if (suffix.IsEmpty() || !InTextureName.EndsWith(suffix, ESearchCase::IgnoreCase))
				{
					continue;
				}

				if (suffix.Len() <= bestSuffixLength)
				{
					continue;
				}

				bestSuffixLength = suffix.Len();
				OutBaseName = InTextureName.LeftChop(suffix.Len());
				OutChannel = config.channel;
				bMatched = true;
			}
		}

		return bMatched;
	}

	class FTextureBatchPackRunner final : public TSharedFromThis<FTextureBatchPackRunner>
	{
	public:
		FTextureBatchPackRunner(
			FTextureBatchPackRequest&& InRequest,
			TFunction<void(const FTextureBatchPackResult&, bool)>&& InOnFinished)
			: request(MoveTemp(InRequest))
			, onFinished(MoveTemp(InOnFinished))
		{
		}

		void Start()
		{
			FAsyncTaskNotificationConfig config;
			config.TitleText = LOCTEXT("BatchPackProgressTitle", "Batch packing textures");
			config.ProgressText = GetProgressText();
			config.bCanCancel = true;
			config.bKeepOpenOnSuccess = true;
			config.bKeepOpenOnFailure = true;

			notification = MakeUnique<FAsyncTaskNotification>(config);
			tickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				TEXT("TexturePackerBatchPackRunner"),
				0.0f,
				[this](float) {
					return Tick();
				});
		}

	private:
		bool Tick()
		{
			if (notification.IsValid() && notification->GetPromptAction() == EAsyncTaskNotificationPromptAction::Cancel)
			{
				bCanceled = true;
			}

			if (bCanceled)
			{
				Finish(false, LOCTEXT("BatchPackCanceledTitle", "Batch pack canceled"));
				return false;
			}

			if (!transaction.IsValid())
			{
				transaction = MakeUnique<FScopedTransaction>(LOCTEXT("BatchPackTransaction", "Batch Pack Textures"));
			}

			if (!request.requests.IsValidIndex(currentIndex))
			{
				Finish(true, LOCTEXT("BatchPackFinishedTitle", "Batch pack finished"));
				return false;
			}

			FTextureBatchPackService::Accumulate(
				batchResult,
				FTextureBatchPackService::PackTexture(request.requests[currentIndex]));
			++currentIndex;

			if (notification.IsValid())
			{
				notification->SetProgressText(GetProgressText());
			}

			if (currentIndex >= request.requests.Num())
			{
				Finish(true, LOCTEXT("BatchPackFinishedTitle", "Batch pack finished"));
				return false;
			}

			return true;
		}

		void Finish(const bool bSuccess, const FText& InTitle)
		{
			transaction.Reset();

			if (notification.IsValid())
			{
				const FText summaryText = FText::FromString(FString::Printf(
					TEXT("Groups: %d | Created: %d | Invalid: %d | Failed: %d"),
					batchResult.processedGroupCount,
					batchResult.createdCount,
					batchResult.invalidCount,
					batchResult.failedCount));

				notification->SetHyperlink(
					FSimpleDelegate::CreateLambda([summary = batchResult]() {
						FString lines;
						for (const FTexturePackResult& item : summary.groupResults)
						{
							const TCHAR* typeLabel = TEXT("Failed");
							switch (item.type)
							{
							case ETexturePackResultType::Created:
								typeLabel = TEXT("Created");
								break;
							case ETexturePackResultType::Invalid:
								typeLabel = TEXT("Invalid");
								break;
							case ETexturePackResultType::Failed:
							default:
								break;
							}

							lines += FString::Printf(TEXT("[%s] %s | %s\n"), typeLabel, *item.assetName, *item.message);
						}

						FMessageDialog::Open(
							EAppMsgType::Ok,
							FText::FromString(FString::Printf(
								TEXT("Processed groups: %d\nCreated: %d\nInvalid: %d\nFailed: %d\n\n%s"),
								summary.processedGroupCount,
								summary.createdCount,
								summary.invalidCount,
								summary.failedCount,
								*lines)),
							LOCTEXT("BatchPackResultDialogTitle", "Batch Pack Results"));
					}),
					LOCTEXT("BatchPackResultDetailsLink", "Details"));
				notification->SetProgressText(summaryText);
				notification->SetComplete(InTitle, summaryText, bSuccess);
			}

			if (onFinished)
			{
				onFinished(batchResult, bCanceled);
			}
		}

		FText GetProgressText() const
		{
			return FText::Format(
				LOCTEXT("BatchPackProgressText", "{0} / {1} groups"),
				currentIndex,
				request.requests.Num());
		}

		FTextureBatchPackRequest request;
		FTextureBatchPackResult batchResult;
		int32 currentIndex = 0;
		bool bCanceled = false;
		FTSTicker::FDelegateHandle tickerHandle;
		TUniquePtr<FAsyncTaskNotification> notification;
		TUniquePtr<FScopedTransaction> transaction;
		TFunction<void(const FTextureBatchPackResult&, bool)> onFinished;
	};

	class STexturePackerBatchPackWindow final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(STexturePackerBatchPackWindow) {}
			SLATE_ARGUMENT(TArray<FString>, TexturePaths)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			RestoreSettings();
			BuildTextureGroupOptions();
			texturePaths = InArgs._TexturePaths;
			ResolveGroups(texturePaths);

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
									.Text(LOCTEXT("BatchPackTitle", "Batch Pack Textures"))
									.Font(FAppStyle::GetFontStyle("HeadingMedium"))
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
								[
									SNew(STextBlock)
									.Text(FText::Format(LOCTEXT("BatchPackDescription", "Prepare packed outputs for {0} selected textures."), selectedTextureCount))
									.AutoWrapText(true)
									.ToolTipText(LOCTEXT("BatchPackDescriptionTooltip", "Selected textures are grouped by shared base name and packed into RGB mask outputs."))
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(FMargin(0.f, 12.f, 0.f, 0.f))
								[
									BuildSettingsPanel()
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
									BuildGroupsPanel()
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(FMargin(0.f, 12.f, 0.f, 0.f))
								[
									SAssignNew(validationTextBlock, STextBlock)
									.Text(this, &STexturePackerBatchPackWindow::GetValidationText)
									.ColorAndOpacity(FLinearColor(0.85f, 0.30f, 0.30f))
									.AutoWrapText(true)
									.ToolTipText(this, &STexturePackerBatchPackWindow::GetValidationText)
									.Visibility(this, &STexturePackerBatchPackWindow::GetValidationVisibility)
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
								.Text(LOCTEXT("BatchPackCancel", "Cancel"))
								.ToolTipText(LOCTEXT("BatchPackCancelTooltip", "Close the batch pack window without doing anything."))
								.OnClicked(this, &STexturePackerBatchPackWindow::OnCancelClicked)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
								.Text(LOCTEXT("BatchPackRun", "Batch Pack"))
								.ToolTipText(this, &STexturePackerBatchPackWindow::GetRunButtonTooltip)
								.IsEnabled(this, &STexturePackerBatchPackWindow::CanExecute)
								.OnClicked(this, &STexturePackerBatchPackWindow::OnRunClicked)
							]
						]
					]
				]
			];
		}

	private:
		TSharedRef<SWidget> BuildSettingsPanel()
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
						.Text(LOCTEXT("BatchPackSettingsLabel", "Batch Settings"))
						.Font(FAppStyle::GetFontStyle("BoldFont"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
					[
						BuildSuffixPanel()
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
							.Text(LOCTEXT("BatchPackTextureGroupLabel", "Texture Group"))
							.ToolTipText(LOCTEXT("BatchPackTextureGroupTooltip", "Texture group for newly packed textures."))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						[
							SNew(STextComboBox)
							.OptionsSource(&textureGroupOptions)
							.InitiallySelectedItem(selectedTextureGroupOption)
							.OnSelectionChanged(this, &STexturePackerBatchPackWindow::OnTextureGroupSelectionChanged)
							.ToolTipText(LOCTEXT("BatchPackTextureGroupComboTooltip", "Choose the texture group for new packed textures."))
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("BatchPackCompressionInfo", "Output uses sRGB = false and Compression = Masks."))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f)))
						.ToolTipText(LOCTEXT("BatchPackCompressionInfoTooltip", "Packed textures always use mask compression and disable sRGB."))
					]
				];
		}

		TSharedRef<SWidget> BuildSuffixPanel()
		{
			TSharedRef<SVerticalBox> suffixRows = SNew(SVerticalBox);
			for (const FBatchPackChannelConfig& config : BatchPackChannelConfigs)
			{
				suffixRows->AddSlot()
				.AutoHeight()
				.Padding(0.f, config.channel == EBatchPackChannel::R ? 0.f : 8.f, 0.f, 0.f)
				[
					BuildSuffixRow(config)
				];
			}

			return SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("BatchPackSuffixesLabel", "Input Suffixes"))
						.ToolTipText(LOCTEXT("BatchPackSuffixesTooltip", "Suffixes used to auto-detect which selected texture belongs to R, G, and B in each group."))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("BatchPackLongPreset", "Long"))
						.ToolTipText(LOCTEXT("BatchPackLongPresetTooltip", "Use _Occlusion, _Roughness, and _Metallic for detection."))
						.OnClicked(this, &STexturePackerBatchPackWindow::OnLongPresetClicked)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("BatchPackShortPreset", "Short"))
						.ToolTipText(LOCTEXT("BatchPackShortPresetTooltip", "Use _O, _R, and _M for detection."))
						.OnClicked(this, &STexturePackerBatchPackWindow::OnShortPresetClicked)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
				[
					suffixRows
				];
		}

		TSharedRef<SWidget> BuildSuffixRow(const FBatchPackChannelConfig& InConfig)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(InConfig.color)
					.Padding(FMargin(10.f, 4.f))
					.ToolTipText(FText::Format(LOCTEXT("BatchPackSuffixBadgeTooltip", "{0} channel detection suffix."), FText::FromString(InConfig.displayName)))
					[
						SNew(STextBlock)
						.Text(FText::FromString(InConfig.shortLabel))
						.Font(FAppStyle::GetFontStyle("BoldFont"))
						.ColorAndOpacity(FSlateColor(FLinearColor::White))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(SEditableTextBox)
					.Text(this, &STexturePackerBatchPackWindow::GetSuffixText, InConfig.channel)
					.OnTextChanged(this, &STexturePackerBatchPackWindow::OnSuffixChanged, InConfig.channel)
					.ToolTipText(FText::Format(LOCTEXT("BatchPackSuffixTextTooltip", "Suffix used to detect the {0} channel texture in each group."), GetBatchPackChannelDisplayName(InConfig.channel)))
				];
		}

		TSharedRef<SWidget> BuildGroupsPanel()
		{
			TSharedRef<SVerticalBox> panel = SNew(SVerticalBox);
			panel->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 10.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BatchPackGroupsLabel", "Detected Groups"))
				.Font(FAppStyle::GetFontStyle("BoldFont"))
				.ToolTipText(LOCTEXT("BatchPackGroupsTooltip", "Selected textures grouped by shared base name. You can reassign which texture goes into R, G, and B."))
			];

			for (int32 groupIndex = 0; groupIndex < groups.Num(); ++groupIndex)
			{
				panel->AddSlot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 8.f)
				[
					BuildGroupCard(groupIndex)
				];
			}

			return panel;
		}

		TSharedRef<SWidget> BuildGroupCard(const int32 InGroupIndex)
		{
			FBatchPackGroupItem& group = groups[InGroupIndex];

			return SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(10.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(group.baseName))
							.Font(FAppStyle::GetFontStyle("BoldFont"))
							.ToolTipText(FText::FromString(group.baseName))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(this, &STexturePackerBatchPackWindow::GetGroupStatusText, InGroupIndex)
							.ColorAndOpacity(this, &STexturePackerBatchPackWindow::GetGroupStatusColor, InGroupIndex)
							.ToolTipText(this, &STexturePackerBatchPackWindow::GetGroupStatusTooltip, InGroupIndex)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						BuildChannelPickerRow(InGroupIndex, EBatchPackChannel::R)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						BuildChannelPickerRow(InGroupIndex, EBatchPackChannel::G)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						BuildChannelPickerRow(InGroupIndex, EBatchPackChannel::B)
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
							.Text(LOCTEXT("BatchPackOutputNameLabel", "Output"))
							.ToolTipText(LOCTEXT("BatchPackOutputNameTooltip", "Packed output asset name generated from the group base name."))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						[
							SNew(SEditableTextBox)
							.Text(this, &STexturePackerBatchPackWindow::GetGroupOutputNameText, InGroupIndex)
							.OnTextChanged(this, &STexturePackerBatchPackWindow::OnGroupOutputNameChanged, InGroupIndex)
							.ToolTipText(LOCTEXT("BatchPackOutputNameTextBoxTooltip", "Edit the exact packed texture name for this group."))
						]
					]
				];
		}

		TSharedRef<SWidget> BuildChannelPickerRow(const int32 InGroupIndex, const EBatchPackChannel InChannel)
		{
			const FBatchPackChannelConfig& config = BatchPackChannelConfigs[BatchPackToIndex(InChannel)];

			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 12.f, 0.f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(config.color)
					.Padding(FMargin(10.f, 6.f))
					.ToolTipText(FText::Format(LOCTEXT("BatchPackChannelBadgeTooltip", "{0} source texture."), GetBatchPackChannelDisplayName(InChannel)))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(config.shortLabel))
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
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UTexture2D::StaticClass())
					.DisplayUseSelected(true)
					.DisplayBrowse(true)
					.DisplayThumbnail(true)
					.ObjectPath(this, &STexturePackerBatchPackWindow::GetGroupTexturePath, InGroupIndex, InChannel)
					.OnObjectChanged(this, &STexturePackerBatchPackWindow::OnGroupTextureChanged, InGroupIndex, InChannel)
					.ToolTipText(FText::Format(LOCTEXT("BatchPackChannelPickerTooltip", "Choose which texture should fill the {0} channel in this packed output."), GetBatchPackChannelDisplayName(InChannel)))
				];
		}

		void ResolveGroups(const TArray<FString>& InTexturePaths)
		{
			TMap<FString, int32> groupIndexByBase;
			TArray<TWeakObjectPtr<UTexture2D>> unmatchedTextures;

			groups.Reset();
			selectedTextureCount = InTexturePaths.Num();

			for (const FString& texturePath : InTexturePaths)
			{
				UTexture2D* texture = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, *texturePath));
				if (!texture)
				{
					continue;
				}

				const FString textureName = texture->GetName();
				FString baseName;
				EBatchPackChannel matchedChannel = EBatchPackChannel::R;
				if (TryMatchBatchPackTexture(textureName, suffixes, baseName, matchedChannel))
				{
					const int32 groupIndex = groupIndexByBase.FindOrAdd(baseName, groups.Num());
					if (groupIndex == groups.Num())
					{
						FBatchPackGroupItem& group = groups.AddDefaulted_GetRef();
						group.baseName = baseName;
						group.outputName = BuildBatchPackOutputName(baseName);
					}

					groups[groupIndex].textures[BatchPackToIndex(matchedChannel)] = texture;
				}
				else
				{
					unmatchedTextures.Add(texture);
				}
			}

			for (const TWeakObjectPtr<UTexture2D>& texture : unmatchedTextures)
			{
				const FString baseName = texture.IsValid() ? texture->GetName() : TEXT("Invalid");
				FBatchPackGroupItem& group = groups.AddDefaulted_GetRef();
				group.baseName = baseName;
				group.outputName = BuildBatchPackOutputName(baseName);
				group.statusText = TEXT("Suffix not detected");
			}

			RefreshGroups();
		}

		void RefreshGroups()
		{
			for (FBatchPackGroupItem& group : groups)
			{
				UpdateGroupStatus(group);
			}
		}

		void UpdateGroupStatus(FBatchPackGroupItem& InOutGroup) const
		{
			TSet<UTexture2D*> uniqueTextures;
			for (const TWeakObjectPtr<UTexture2D>& texture : InOutGroup.textures)
			{
				if (!texture.IsValid())
				{
					InOutGroup.bValid = false;
					InOutGroup.statusText = TEXT("Missing channel");
					return;
				}

				if (uniqueTextures.Contains(texture.Get()))
				{
					InOutGroup.bValid = false;
					InOutGroup.statusText = TEXT("Duplicate texture");
					return;
				}

				uniqueTextures.Add(texture.Get());
			}

			if (InOutGroup.outputName.TrimStartAndEnd().IsEmpty())
			{
				InOutGroup.bValid = false;
				InOutGroup.statusText = TEXT("Output name required");
				return;
			}

			InOutGroup.bValid = true;
			InOutGroup.statusText = TEXT("Ready");
		}

		FTextureBatchPackRequest BuildRequest() const
		{
			FTextureBatchPackRequest request;
			for (const FBatchPackGroupItem& group : groups)
			{
				if (!group.bValid)
				{
					continue;
				}

				FTexturePackRequest& packRequest = request.requests.AddDefaulted_GetRef();
				packRequest.redTexture = group.textures[BatchPackToIndex(EBatchPackChannel::R)].Get();
				packRequest.greenTexture = group.textures[BatchPackToIndex(EBatchPackChannel::G)].Get();
				packRequest.blueTexture = group.textures[BatchPackToIndex(EBatchPackChannel::B)].Get();
				packRequest.outputName = group.outputName.TrimStartAndEnd();
				packRequest.textureGroup = static_cast<TextureGroup>(selectedTextureGroupIndex);
			}

			return request;
		}

		bool ConfirmBatchPack(const FTextureBatchPackPreview& InPreview) const
		{
			return FMessageDialog::Open(
					   EAppMsgType::OkCancel,
					   FText::FromString(FString::Printf(
						   TEXT("Create %d packed textures from %d selected source textures?\n\nDetected groups: %d\nReady groups: %d\nInvalid groups: %d\nExisting asset conflicts: %d\nTexture Group: %s\nCompression: Masks\nsRGB: false\nDestination: each packed texture is created in the same folder as its R texture.\nUndo: one undo step will revert created assets.\nCancel during progress stops remaining groups and keeps already created assets."),
						   InPreview.outputsToCreate,
						   selectedTextureCount,
						   InPreview.groupCount,
						   InPreview.readyGroupCount,
						   InPreview.invalidGroupCount,
						   InPreview.conflictCount,
						   selectedTextureGroupOption.IsValid() ? **selectedTextureGroupOption : TEXT("World"))))
				   == EAppReturnType::Ok;
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

			selectedTextureGroupOption = textureGroupOptions.IsValidIndex(selectedTextureGroupIndex)
				? textureGroupOptions[selectedTextureGroupIndex]
				: nullptr;
		}

		void RestoreSettings()
		{
			if (!GBatchPackWindowSettings.bInitialized)
			{
				ApplyPreset(EBatchPackSuffixPreset::Short);
				SaveSettings();
				return;
			}

			selectedTextureGroupIndex = GBatchPackWindowSettings.textureGroupIndex;
			for (const FBatchPackChannelConfig& config : BatchPackChannelConfigs)
			{
				suffixes[BatchPackToIndex(config.channel)] = GBatchPackWindowSettings.suffixes[BatchPackToIndex(config.channel)];
			}
		}

		void SaveSettings()
		{
			GBatchPackWindowSettings.bInitialized = true;
			GBatchPackWindowSettings.textureGroupIndex = selectedTextureGroupIndex;
			for (const FBatchPackChannelConfig& config : BatchPackChannelConfigs)
			{
				GBatchPackWindowSettings.suffixes[BatchPackToIndex(config.channel)] = suffixes[BatchPackToIndex(config.channel)];
			}
		}

		void CloseWindow() const
		{
			if (const TSharedPtr<SWindow> window = FSlateApplication::Get().FindWidgetWindow(AsShared()))
			{
				window->RequestDestroyWindow();
			}
		}

		void ApplyPreset(const EBatchPackSuffixPreset InPreset)
		{
			for (const FBatchPackChannelConfig& config : BatchPackChannelConfigs)
			{
				suffixes[BatchPackToIndex(config.channel)] = InPreset == EBatchPackSuffixPreset::Short ? config.shortSuffix : config.longSuffix;
			}
		}

		FText GetSuffixText(const EBatchPackChannel InChannel) const
		{
			return FText::FromString(suffixes[BatchPackToIndex(InChannel)]);
		}

		void OnSuffixChanged(const FText& InText, const EBatchPackChannel InChannel)
		{
			suffixes[BatchPackToIndex(InChannel)] = InText.ToString();
			SaveSettings();
			ResolveGroups(texturePaths);
		}

		FReply OnLongPresetClicked()
		{
			ApplyPreset(EBatchPackSuffixPreset::Long);
			SaveSettings();
			ResolveGroups(texturePaths);
			return FReply::Handled();
		}

		FReply OnShortPresetClicked()
		{
			ApplyPreset(EBatchPackSuffixPreset::Short);
			SaveSettings();
			ResolveGroups(texturePaths);
			return FReply::Handled();
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
				selectedTextureGroupIndex = optionIndex;
				SaveSettings();
			}
		}

		FString GetGroupTexturePath(const int32 InGroupIndex, const EBatchPackChannel InChannel) const
		{
			if (!groups.IsValidIndex(InGroupIndex))
			{
				return FString();
			}

			const TWeakObjectPtr<UTexture2D> texture = groups[InGroupIndex].textures[BatchPackToIndex(InChannel)];
			return texture.IsValid() ? texture->GetPathName() : FString();
		}

		void OnGroupTextureChanged(const FAssetData& InAssetData, const int32 InGroupIndex, const EBatchPackChannel InChannel)
		{
			if (!groups.IsValidIndex(InGroupIndex))
			{
				return;
			}

			groups[InGroupIndex].textures[BatchPackToIndex(InChannel)] = Cast<UTexture2D>(InAssetData.GetAsset());
			UpdateGroupStatus(groups[InGroupIndex]);
		}

		FText GetGroupOutputNameText(const int32 InGroupIndex) const
		{
			return groups.IsValidIndex(InGroupIndex) ? FText::FromString(groups[InGroupIndex].outputName) : FText::GetEmpty();
		}

		void OnGroupOutputNameChanged(const FText& InText, const int32 InGroupIndex)
		{
			if (!groups.IsValidIndex(InGroupIndex))
			{
				return;
			}

			groups[InGroupIndex].outputName = InText.ToString();
			UpdateGroupStatus(groups[InGroupIndex]);
		}

		FText GetGroupStatusText(const int32 InGroupIndex) const
		{
			return groups.IsValidIndex(InGroupIndex) ? FText::FromString(groups[InGroupIndex].statusText) : FText::GetEmpty();
		}

		FSlateColor GetGroupStatusColor(const int32 InGroupIndex) const
		{
			return groups.IsValidIndex(InGroupIndex) && groups[InGroupIndex].bValid
				? FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f))
				: FSlateColor(FLinearColor(0.85f, 0.30f, 0.30f));
		}

		FText GetGroupStatusTooltip(const int32 InGroupIndex) const
		{
			return groups.IsValidIndex(InGroupIndex)
				? FText::FromString(groups[InGroupIndex].statusText)
				: FText::GetEmpty();
		}

		struct FValidationResult
		{
			bool bIsValid = false;
			FText message;
		};

		FValidationResult Validate() const
		{
			if (selectedTextureCount < 3)
			{
				return { false, LOCTEXT("BatchPackNeedThree", "Select at least 3 textures.") };
			}

			if (selectedTextureCount % 3 != 0)
			{
				return { false, LOCTEXT("BatchPackNeedMultipleOfThree", "Batch Pack requires a texture count divisible by 3.") };
			}

			if (groups.IsEmpty())
			{
				return { false, LOCTEXT("BatchPackNoGroups", "No valid groups were detected from the selected textures.") };
			}

			for (const FBatchPackChannelConfig& config : BatchPackChannelConfigs)
			{
				if (suffixes[BatchPackToIndex(config.channel)].TrimStartAndEnd().IsEmpty())
				{
					return { false, FText::Format(LOCTEXT("BatchPackSuffixRequired", "{0} suffix is required."), FText::FromString(config.displayName)) };
				}
			}

			bool bHasValidGroup = false;
			TSet<FString> outputNames;
			for (const FBatchPackGroupItem& group : groups)
			{
				if (!group.bValid)
				{
					continue;
				}

				bHasValidGroup = true;
				if (outputNames.Contains(group.outputName))
				{
					return { false, LOCTEXT("BatchPackDuplicateOutputNames", "Packed output names must be unique.") };
				}

				outputNames.Add(group.outputName);
			}

			if (!bHasValidGroup)
			{
				return { false, LOCTEXT("BatchPackNoReadyGroups", "At least one complete group with R, G, and B textures is required.") };
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

		bool CanExecute() const
		{
			return Validate().bIsValid && !activeRunner.IsValid();
		}

		FText GetRunButtonTooltip() const
		{
			if (activeRunner.IsValid())
			{
				return LOCTEXT("BatchPackAlreadyRunningTooltip", "A batch pack operation is already running.");
			}

			const FValidationResult validation = Validate();
			return validation.bIsValid
				? LOCTEXT("BatchPackRunTooltip", "Review the creation summary and start batch pack.")
				: validation.message;
		}

		FReply OnRunClicked()
		{
			const FValidationResult validation = Validate();
			if (!validation.bIsValid || activeRunner.IsValid())
			{
				return FReply::Handled();
			}

			FTextureBatchPackRequest request = BuildRequest();
			const FTextureBatchPackPreview preview = FTextureBatchPackService::BuildPreview(request);
			if (!ConfirmBatchPack(preview))
			{
				return FReply::Handled();
			}

			TWeakPtr<STexturePackerBatchPackWindow> weakThis = StaticCastSharedRef<STexturePackerBatchPackWindow>(AsShared());
			activeRunner = MakeShared<FTextureBatchPackRunner>(
				MoveTemp(request),
				[weakThis](const FTextureBatchPackResult&, bool) {
					if (TSharedPtr<STexturePackerBatchPackWindow> pinned = weakThis.Pin())
					{
						pinned->activeRunner.Reset();
						pinned->CloseWindow();
					}
				});
			activeRunner->Start();
			return FReply::Handled();
		}

		FReply OnCancelClicked()
		{
			CloseWindow();
			return FReply::Handled();
		}

		TArray<FString> texturePaths;
		TArray<FBatchPackGroupItem> groups;
		int32 selectedTextureCount = 0;
		FString suffixes[3];
		int32 selectedTextureGroupIndex = 0;
		TArray<TSharedPtr<FString>> textureGroupOptions;
		TSharedPtr<FString> selectedTextureGroupOption;
		TSharedPtr<STextBlock> validationTextBlock;
		TSharedPtr<FTextureBatchPackRunner> activeRunner;
	};
}

TSharedRef<SWidget> CreateTexturePackerBatchPackWindow(const TArray<FString>& InTexturePaths)
{
	return SNew(STexturePackerBatchPackWindow)
		.TexturePaths(InTexturePaths);
}

#undef LOCTEXT_NAMESPACE
