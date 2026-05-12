// Copyright Epic Games, Inc. All Rights Reserved.

#include "STexturePackerBatchUnpackWindow.h"

#include "ContentBrowserModule.h"
#include "Containers/Ticker.h"
#include "IContentBrowserSingleton.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AsyncTaskNotification.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"
#include "TextureBatchUnpackService.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "STexturePackerBatchUnpackWindow"

namespace
{
	enum class EBatchUnpackSuffixPreset : uint8
	{
		Long,
		Short
	};

	enum class EBatchUnpackChannel : uint8
	{
		R,
		G,
		B
	};

	struct FBatchUnpackChannelConfig
	{
		EBatchUnpackChannel channel = EBatchUnpackChannel::R;
		const TCHAR* shortLabel = TEXT("");
		const TCHAR* displayName = TEXT("");
		const TCHAR* longSuffix = TEXT("");
		const TCHAR* shortSuffix = TEXT("");
		FLinearColor color = FLinearColor::White;
	};

	struct FBatchUnpackTextureItem
	{
		TWeakObjectPtr<UTexture2D> texture;
		FString objectPath;
	};

	struct FBatchUnpackWindowSettings
	{
		bool bInitialized = false;
		bool bUseAlphaCompression = true;
		int32 textureGroupIndex = 0;
		FString suffixes[3];
	};

	constexpr FBatchUnpackChannelConfig BatchUnpackChannelConfigs[] = {
		{ EBatchUnpackChannel::R, TEXT("R"), TEXT("Occlusion"), TEXT("_Occlusion"), TEXT("_O"), FLinearColor(0.78f, 0.18f, 0.18f) },
		{ EBatchUnpackChannel::G, TEXT("G"), TEXT("Roughness"), TEXT("_Roughness"), TEXT("_R"), FLinearColor(0.18f, 0.65f, 0.22f) },
		{ EBatchUnpackChannel::B, TEXT("B"), TEXT("Metallic"), TEXT("_Metallic"), TEXT("_M"), FLinearColor(0.20f, 0.40f, 0.82f) }
	};

	FBatchUnpackWindowSettings GBatchUnpackWindowSettings;

	int32 BatchUnpackToIndex(const EBatchUnpackChannel InChannel)
	{
		return static_cast<int32>(InChannel);
	}

	FString BuildBatchUnpackBaseName(UTexture2D* InTexture)
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

	bool BatchUnpackHasThreeColorChannels(UTexture2D* InTexture)
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

	class SBatchUnpackTextureRow final : public SMultiColumnTableRow<TSharedPtr<FBatchUnpackTextureItem>>
	{
	public:
		SLATE_BEGIN_ARGS(SBatchUnpackTextureRow) {}
			SLATE_ARGUMENT(TSharedPtr<FBatchUnpackTextureItem>, Item)
			SLATE_ARGUMENT(TArray<FString>, OutputNames)
			SLATE_ARGUMENT(bool, bHasRgb)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
		{
			item = InArgs._Item;
			outputNames = InArgs._OutputNames;
			bHasRgb = InArgs._bHasRgb;

			SMultiColumnTableRow<TSharedPtr<FBatchUnpackTextureItem>>::Construct(
				FSuperRowType::FArguments()
					.Padding(FMargin(4.f, 2.f)),
				InOwnerTableView);
		}

		TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
		{
			UTexture2D* texture = item.IsValid() ? item->texture.Get() : nullptr;

			if (InColumnName == "Icon")
			{
				return SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("ClassIcon.Texture2D"))
						.ToolTipText(LOCTEXT("BatchUnpackTableIconTooltip", "Selected Texture2D asset. Double click the row to browse to it in the Content Browser."))
					];
			}

			if (InColumnName == "Name")
			{
				return SNew(STextBlock)
					.Text(FText::FromString(texture ? texture->GetName() : item->objectPath))
					.ToolTipText(FText::FromString(item->objectPath));
			}

			if (InColumnName == "Path")
			{
				return SNew(STextBlock)
					.Text(FText::FromString(item->objectPath))
					.ToolTipText(FText::FromString(item->objectPath))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f)));
			}

			if (InColumnName == "Outputs")
			{
				return SNew(STextBlock)
					.Text(FText::FromString(FString::Join(outputNames, TEXT(" | "))))
					.ToolTipText(FText::FromString(FString::Join(outputNames, TEXT("\n"))));
			}

			if (InColumnName == "Status")
			{
				return SNew(STextBlock)
					.Text(bHasRgb ? LOCTEXT("BatchUnpackReadyStatus", "Ready") : LOCTEXT("BatchUnpackSkippedStatus", "Skipped"))
					.ToolTipText(bHasRgb ? LOCTEXT("BatchUnpackReadyTooltip", "This texture has RGB channels and can be batch unpacked.") : LOCTEXT("BatchUnpackSkippedTooltip", "This texture does not contain RGB channels and will be skipped."))
					.ColorAndOpacity(bHasRgb ? FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f)) : FSlateColor(FLinearColor(0.85f, 0.30f, 0.30f)));
			}

			return SNew(STextBlock).Text(FText::GetEmpty());
		}

	private:
		TSharedPtr<FBatchUnpackTextureItem> item;
		TArray<FString> outputNames;
		bool bHasRgb = false;
	};

	class FTextureBatchUnpackRunner final : public TSharedFromThis<FTextureBatchUnpackRunner>
	{
	public:
		FTextureBatchUnpackRunner(
			FTextureBatchUnpackRequest&& InRequest,
			TFunction<void(const FTextureBatchUnpackResult&, bool)>&& InOnFinished)
			: request(MoveTemp(InRequest))
			, onFinished(MoveTemp(InOnFinished))
		{
		}

		void Start()
		{
			FAsyncTaskNotificationConfig config;
			config.TitleText = LOCTEXT("BatchUnpackProgressTitle", "Batch unpacking textures");
			config.ProgressText = GetProgressText();
			config.bCanCancel = true;
			config.bKeepOpenOnSuccess = true;
			config.bKeepOpenOnFailure = true;

			notification = MakeUnique<FAsyncTaskNotification>(config);
			tickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				TEXT("TexturePackerBatchUnpackRunner"),
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
				Finish(false, LOCTEXT("BatchUnpackCanceledTitle", "Batch unpack canceled"));
				return false;
			}

			if (!transaction.IsValid())
			{
				transaction = MakeUnique<FScopedTransaction>(LOCTEXT("BatchUnpackTransaction", "Batch Unpack Textures"));
			}

			if (!request.requests.IsValidIndex(currentIndex))
			{
				Finish(true, LOCTEXT("BatchUnpackFinishedTitle", "Batch unpack finished"));
				return false;
			}

			FTextureBatchUnpackService::Accumulate(
				batchResult,
				FTextureBatchUnpackService::UnpackTexture(request.requests[currentIndex]));
			++currentIndex;

			if (notification.IsValid())
			{
				notification->SetProgressText(GetProgressText());
			}

			if (currentIndex >= request.requests.Num())
			{
				Finish(true, LOCTEXT("BatchUnpackFinishedTitle", "Batch unpack finished"));
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
					TEXT("Textures: %d | Created: %d | Skipped: %d | Invalid: %d | Failed: %d"),
					batchResult.processedTextureCount,
					batchResult.createdCount,
					batchResult.skippedCount,
					batchResult.invalidCount,
					batchResult.failedCount));

				notification->SetHyperlink(
					FSimpleDelegate::CreateLambda([summary = batchResult]() {
						FString lines;
						for (const FTextureUnpackResult& textureResult : summary.textureResults)
						{
							for (const FTextureUnpackItemResult& item : textureResult.items)
							{
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

								lines += FString::Printf(TEXT("[%s] %s | %s\n"), typeLabel, *item.assetName, *item.message);
							}
						}

						FMessageDialog::Open(
							EAppMsgType::Ok,
							FText::FromString(FString::Printf(
								TEXT("Processed textures: %d\nCreated: %d\nSkipped: %d\nInvalid: %d\nFailed: %d\n\n%s"),
								summary.processedTextureCount,
								summary.createdCount,
								summary.skippedCount,
								summary.invalidCount,
								summary.failedCount,
								*lines)),
							LOCTEXT("BatchUnpackResultDialogTitle", "Batch Unpack Results"));
					}),
					LOCTEXT("BatchUnpackResultDetailsLink", "Details"));
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
				LOCTEXT("BatchUnpackProgressText", "{0} / {1} textures"),
				currentIndex,
				request.requests.Num());
		}

		FTextureBatchUnpackRequest request;
		FTextureBatchUnpackResult batchResult;
		int32 currentIndex = 0;
		bool bCanceled = false;
		FTSTicker::FDelegateHandle tickerHandle;
		TUniquePtr<FAsyncTaskNotification> notification;
		TUniquePtr<FScopedTransaction> transaction;
		TFunction<void(const FTextureBatchUnpackResult&, bool)> onFinished;
	};

	class STexturePackerBatchUnpackWindow final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(STexturePackerBatchUnpackWindow) {}
			SLATE_ARGUMENT(TArray<FString>, TexturePaths)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			RestoreSettings();
			BuildTextureGroupOptions();
			ResolveTextures(InArgs._TexturePaths);

			ChildSlot
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(16.f, 16.f, 16.f, 0.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("BatchUnpackTitle", "Batch Unpack Textures"))
							.Font(FAppStyle::GetFontStyle("HeadingMedium"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
						[
							SNew(STextBlock)
							.Text(FText::Format(LOCTEXT("BatchUnpackDescription", "Prepare unpack outputs for {0} selected textures."), textureItems.Num()))
							.AutoWrapText(true)
							.ToolTipText(LOCTEXT("BatchUnpackDescriptionTooltip", "Every listed texture will create R, G, and B outputs in the same folder as the source asset."))
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
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					.Padding(16.f, 12.f, 16.f, 0.f)
					[
						BuildTextureListPanel()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(16.f, 12.f, 16.f, 0.f))
					[
						SAssignNew(validationTextBlock, STextBlock)
						.Text(this, &STexturePackerBatchUnpackWindow::GetValidationText)
						.ColorAndOpacity(FLinearColor(0.85f, 0.30f, 0.30f))
						.AutoWrapText(true)
						.ToolTipText(this, &STexturePackerBatchUnpackWindow::GetValidationText)
						.Visibility(this, &STexturePackerBatchUnpackWindow::GetValidationVisibility)
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
								.Text(LOCTEXT("BatchUnpackCancel", "Cancel"))
								.ToolTipText(LOCTEXT("BatchUnpackCancelTooltip", "Close the batch unpack window without doing anything."))
								.OnClicked(this, &STexturePackerBatchUnpackWindow::OnCancelClicked)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
								.Text(LOCTEXT("BatchUnpackRun", "Batch Unpack"))
								.ToolTipText(this, &STexturePackerBatchUnpackWindow::GetRunButtonTooltip)
								.IsEnabled(this, &STexturePackerBatchUnpackWindow::CanExecute)
								.OnClicked(this, &STexturePackerBatchUnpackWindow::OnRunClicked)
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
						.Text(LOCTEXT("BatchSettingsLabel", "Batch Settings"))
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
						[
							SNew(SCheckBox)
							.IsChecked(this, &STexturePackerBatchUnpackWindow::GetAlphaCompressionCheckState)
							.OnCheckStateChanged(this, &STexturePackerBatchUnpackWindow::OnAlphaCompressionCheckStateChanged)
							.ToolTipText(LOCTEXT("BatchAlphaCompressionTooltip", "New textures should use alpha compression for single-channel data read from red only."))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(8.f, 0.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("BatchAlphaCompressionLabel", "Compression: Alpha"))
							.ToolTipText(LOCTEXT("BatchAlphaCompressionLabelTooltip", "Enable alpha compression for all unpacked textures in this batch."))
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
							.Text(LOCTEXT("BatchTextureGroupLabel", "Texture Group"))
							.ToolTipText(LOCTEXT("BatchTextureGroupTooltip", "Choose a shared texture group or keep the original group per source texture."))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						[
							SNew(STextComboBox)
							.OptionsSource(&textureGroupOptions)
							.InitiallySelectedItem(selectedTextureGroupOption)
							.OnSelectionChanged(this, &STexturePackerBatchUnpackWindow::OnTextureGroupSelectionChanged)
							.ToolTipText(LOCTEXT("BatchTextureGroupComboTooltip", "Select the texture group for all unpacked textures in this batch."))
						]
					]
				];
		}

		TSharedRef<SWidget> BuildSuffixPanel()
		{
			TSharedRef<SVerticalBox> suffixRows = SNew(SVerticalBox);
			for (const FBatchUnpackChannelConfig& config : BatchUnpackChannelConfigs)
			{
				suffixRows->AddSlot()
				.AutoHeight()
				.Padding(0.f, config.channel == EBatchUnpackChannel::R ? 0.f : 8.f, 0.f, 0.f)
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
						.Text(LOCTEXT("BatchSuffixesLabel", "Suffixes"))
						.ToolTipText(LOCTEXT("BatchSuffixesTooltip", "Shared output suffixes used for every selected texture in this batch."))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("BatchLongPreset", "Long"))
						.ToolTipText(LOCTEXT("BatchLongPresetTooltip", "Use _Occlusion, _Roughness, and _Metallic for every texture."))
						.OnClicked(this, &STexturePackerBatchUnpackWindow::OnLongPresetClicked)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("BatchShortPreset", "Short"))
						.ToolTipText(LOCTEXT("BatchShortPresetTooltip", "Use _O, _R, and _M for every texture."))
						.OnClicked(this, &STexturePackerBatchUnpackWindow::OnShortPresetClicked)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
				[
					suffixRows
				];
		}

		TSharedRef<SWidget> BuildSuffixRow(const FBatchUnpackChannelConfig& InConfig)
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
					.ToolTipText(FText::Format(LOCTEXT("BatchSuffixBadgeTooltip", "{0} channel suffix."), FText::FromString(InConfig.displayName)))
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
					.Text(this, &STexturePackerBatchUnpackWindow::GetSuffixText, InConfig.channel)
					.OnTextChanged(this, &STexturePackerBatchUnpackWindow::OnSuffixChanged, InConfig.channel)
					.ToolTipText(FText::Format(LOCTEXT("BatchSuffixTextTooltip", "Suffix appended to each base texture name for the {0} channel."), FText::FromString(InConfig.displayName)))
				];
		}

		TSharedRef<SWidget> BuildTextureListPanel()
		{
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 10.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SelectedTexturesLabel", "Selected Textures"))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
					.ToolTipText(LOCTEXT("SelectedTexturesTooltip", "Double click a row to browse that texture in the Content Browser."))
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SAssignNew(textureListView, SListView<TSharedPtr<FBatchUnpackTextureItem>>)
					.ListItemsSource(&textureItems)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &STexturePackerBatchUnpackWindow::OnGenerateTextureRow)
					.OnMouseButtonDoubleClick(this, &STexturePackerBatchUnpackWindow::OnTextureRowDoubleClicked)
					.HeaderRow(
						SNew(SHeaderRow)
						+ SHeaderRow::Column("Icon").FixedWidth(40.f).DefaultLabel(FText::GetEmpty())
						+ SHeaderRow::Column("Name").FillWidth(0.20f).DefaultLabel(LOCTEXT("BatchNameColumn", "Name"))
						+ SHeaderRow::Column("Path").FillWidth(0.42f).DefaultLabel(LOCTEXT("BatchPathColumn", "Path"))
						+ SHeaderRow::Column("Outputs").FillWidth(0.26f).DefaultLabel(LOCTEXT("BatchOutputsColumn", "Outputs"))
						+ SHeaderRow::Column("Status").FillWidth(0.12f).DefaultLabel(LOCTEXT("BatchStatusColumn", "Status")))
					.ToolTipText(LOCTEXT("BatchTextureListTooltip", "Selected textures for batch unpack. Output names use the shared suffixes above."))
				];
		}

		TSharedRef<ITableRow> OnGenerateTextureRow(TSharedPtr<FBatchUnpackTextureItem> InItem, const TSharedRef<STableViewBase>& InOwnerTable)
		{
			return SNew(SBatchUnpackTextureRow, InOwnerTable)
				.Item(InItem)
				.OutputNames(BuildOutputNames(InItem.Get()))
				.bHasRgb(InItem.IsValid() && BatchUnpackHasThreeColorChannels(InItem->texture.Get()));
		}

		void OnTextureRowDoubleClicked(TSharedPtr<FBatchUnpackTextureItem> InItem)
		{
			if (!InItem.IsValid())
			{
				return;
			}

			if (UTexture2D* texture = InItem->texture.Get())
			{
				TArray<FAssetData> assets;
				assets.Emplace(texture);
				FContentBrowserModule& contentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
				contentBrowserModule.Get().SyncBrowserToAssets(assets);
			}
		}

		TArray<FString> BuildOutputNames(const FBatchUnpackTextureItem* InItem) const
		{
			TArray<FString> outputNames;
			outputNames.Reserve(UE_ARRAY_COUNT(BatchUnpackChannelConfigs));

			const FString baseName = BuildBatchUnpackBaseName(InItem ? InItem->texture.Get() : nullptr);
			for (const FBatchUnpackChannelConfig& config : BatchUnpackChannelConfigs)
			{
				outputNames.Add(baseName + suffixes[BatchUnpackToIndex(config.channel)]);
			}

			return outputNames;
		}

		FTextureBatchUnpackRequest BuildRequest() const
		{
			FTextureBatchUnpackRequest request;
			request.requests.Reserve(textureItems.Num());

			for (const TSharedPtr<FBatchUnpackTextureItem>& item : textureItems)
			{
				UTexture2D* texture = item.IsValid() ? item->texture.Get() : nullptr;

				FTextureUnpackRequest& unpackRequest = request.requests.AddDefaulted_GetRef();
				unpackRequest.sourceTexture = texture;
				unpackRequest.baseName = BuildBatchUnpackBaseName(texture);
				unpackRequest.textureGroup = selectedTextureGroupIndex == 0 || !texture
					? (texture ? static_cast<TextureGroup>(texture->LODGroup.GetValue()) : TEXTUREGROUP_World)
					: static_cast<TextureGroup>(selectedTextureGroupIndex - 1);
				unpackRequest.bUseAlphaCompression = bUseAlphaCompression;
				unpackRequest.channels.Reserve(UE_ARRAY_COUNT(BatchUnpackChannelConfigs));

				for (const FBatchUnpackChannelConfig& config : BatchUnpackChannelConfigs)
				{
					FTextureUnpackChannelRequest& channel = unpackRequest.channels.AddDefaulted_GetRef();
					channel.channel = static_cast<ETexturePackerColorChannel>(config.channel);
					channel.bEnabled = true;
					channel.suffix = suffixes[BatchUnpackToIndex(config.channel)].TrimStartAndEnd();
				}
			}

			return request;
		}

		bool ConfirmBatchUnpack(const FTextureBatchUnpackPreview& InPreview) const
		{
			return FMessageDialog::Open(
					   EAppMsgType::OkCancel,
					   FText::FromString(FString::Printf(
						   TEXT("Create %d textures from %d selected source textures?\n\nReady source textures: %d\nInvalid source textures: %d\nExisting asset conflicts: %d\nCompression: %s\nTexture Group: %s\nDestination: each texture is created in the same folder as its source asset.\nUndo: one undo step will revert created assets.\nCancel during progress stops remaining textures and keeps already created assets."),
						   InPreview.outputsToCreate,
						   InPreview.textureCount,
						   InPreview.readyTextureCount,
						   InPreview.invalidTextureCount,
						   InPreview.conflictCount,
						   bUseAlphaCompression ? TEXT("Alpha") : TEXT("Copy source compression"),
						   selectedTextureGroupOption.IsValid() ? **selectedTextureGroupOption : TEXT("Same As Original"))))
				   == EAppReturnType::Ok;
		}

		void ResolveTextures(const TArray<FString>& InTexturePaths)
		{
			textureItems.Reset();
			for (const FString& texturePath : InTexturePaths)
			{
				TSharedPtr<FBatchUnpackTextureItem> item = MakeShared<FBatchUnpackTextureItem>();
				item->objectPath = texturePath;
				item->texture = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, *texturePath));
				textureItems.Add(item);
			}
		}

		void BuildTextureGroupOptions()
		{
			textureGroupOptions.Reset();
			textureGroupOptions.Add(MakeShared<FString>(LOCTEXT("BatchSameAsOriginal", "Same As Original").ToString()));

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
				: textureGroupOptions[0];
		}

		void RestoreSettings()
		{
			if (!GBatchUnpackWindowSettings.bInitialized)
			{
				ApplyPreset(EBatchUnpackSuffixPreset::Long);
				SaveSettings();
				return;
			}

			bUseAlphaCompression = GBatchUnpackWindowSettings.bUseAlphaCompression;
			selectedTextureGroupIndex = GBatchUnpackWindowSettings.textureGroupIndex;
			for (const FBatchUnpackChannelConfig& config : BatchUnpackChannelConfigs)
			{
				suffixes[BatchUnpackToIndex(config.channel)] = GBatchUnpackWindowSettings.suffixes[BatchUnpackToIndex(config.channel)];
			}
		}

		void SaveSettings()
		{
			GBatchUnpackWindowSettings.bInitialized = true;
			GBatchUnpackWindowSettings.bUseAlphaCompression = bUseAlphaCompression;
			GBatchUnpackWindowSettings.textureGroupIndex = selectedTextureGroupIndex;
			for (const FBatchUnpackChannelConfig& config : BatchUnpackChannelConfigs)
			{
				GBatchUnpackWindowSettings.suffixes[BatchUnpackToIndex(config.channel)] = suffixes[BatchUnpackToIndex(config.channel)];
			}
		}

		void ApplyPreset(EBatchUnpackSuffixPreset InPreset)
		{
			for (const FBatchUnpackChannelConfig& config : BatchUnpackChannelConfigs)
			{
				suffixes[BatchUnpackToIndex(config.channel)] = InPreset == EBatchUnpackSuffixPreset::Short ? config.shortSuffix : config.longSuffix;
			}
		}

		void RefreshList()
		{
			if (textureListView.IsValid())
			{
				textureListView->RequestListRefresh();
			}
		}

		void CloseWindow() const
		{
			if (const TSharedPtr<SWindow> window = FSlateApplication::Get().FindWidgetWindow(AsShared()))
			{
				window->RequestDestroyWindow();
			}
		}

		ECheckBoxState GetAlphaCompressionCheckState() const
		{
			return bUseAlphaCompression ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}

		void OnAlphaCompressionCheckStateChanged(ECheckBoxState InCheckState)
		{
			bUseAlphaCompression = InCheckState == ECheckBoxState::Checked;
			SaveSettings();
		}

		void OnTextureGroupSelectionChanged(TSharedPtr<FString> InItem, ESelectInfo::Type)
		{
			selectedTextureGroupOption = InItem;
			selectedTextureGroupIndex = textureGroupOptions.IndexOfByKey(InItem);
			if (selectedTextureGroupIndex == INDEX_NONE)
			{
				selectedTextureGroupIndex = 0;
			}

			SaveSettings();
		}

		FText GetSuffixText(EBatchUnpackChannel InChannel) const
		{
			return FText::FromString(suffixes[BatchUnpackToIndex(InChannel)]);
		}

		void OnSuffixChanged(const FText& InText, EBatchUnpackChannel InChannel)
		{
			suffixes[BatchUnpackToIndex(InChannel)] = InText.ToString();
			SaveSettings();
			RefreshList();
		}

		FReply OnLongPresetClicked()
		{
			ApplyPreset(EBatchUnpackSuffixPreset::Long);
			SaveSettings();
			RefreshList();
			return FReply::Handled();
		}

		FReply OnShortPresetClicked()
		{
			ApplyPreset(EBatchUnpackSuffixPreset::Short);
			SaveSettings();
			RefreshList();
			return FReply::Handled();
		}

		struct FValidationResult
		{
			bool bIsValid = false;
			FText message;
		};

		FValidationResult Validate() const
		{
			if (textureItems.Num() < 2)
			{
				return { false, LOCTEXT("BatchUnpackNeedTwo", "Select at least 2 textures.") };
			}

			for (const FBatchUnpackChannelConfig& config : BatchUnpackChannelConfigs)
			{
				if (suffixes[BatchUnpackToIndex(config.channel)].TrimStartAndEnd().IsEmpty())
				{
					return { false, FText::Format(LOCTEXT("BatchUnpackSuffixRequired", "{0} suffix is required."), FText::FromString(config.displayName)) };
				}
			}

			bool bHasValidTexture = false;
			for (const TSharedPtr<FBatchUnpackTextureItem>& item : textureItems)
			{
				if (item.IsValid() && item->texture.IsValid() && BatchUnpackHasThreeColorChannels(item->texture.Get()))
				{
					bHasValidTexture = true;
					break;
				}
			}

			if (!bHasValidTexture)
			{
				return { false, LOCTEXT("BatchUnpackNeedRgb", "At least one selected texture must contain RGB channels.") };
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
				return LOCTEXT("BatchUnpackAlreadyRunningTooltip", "A batch unpack operation is already running.");
			}

			const FValidationResult validation = Validate();
			return validation.bIsValid
				? LOCTEXT("BatchUnpackRunTooltip", "Review the creation summary and start batch unpack.")
				: validation.message;
		}

		FReply OnRunClicked()
		{
			const FValidationResult validation = Validate();
			if (!validation.bIsValid || activeRunner.IsValid())
			{
				return FReply::Handled();
			}

			FTextureBatchUnpackRequest request = BuildRequest();
			const FTextureBatchUnpackPreview preview = FTextureBatchUnpackService::BuildPreview(request);
			if (!ConfirmBatchUnpack(preview))
			{
				return FReply::Handled();
			}

			TWeakPtr<STexturePackerBatchUnpackWindow> weakThis = StaticCastSharedRef<STexturePackerBatchUnpackWindow>(AsShared());
			activeRunner = MakeShared<FTextureBatchUnpackRunner>(
				MoveTemp(request),
				[weakThis](const FTextureBatchUnpackResult&, bool) {
					if (TSharedPtr<STexturePackerBatchUnpackWindow> pinned = weakThis.Pin())
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

		TArray<TSharedPtr<FBatchUnpackTextureItem>> textureItems;
		bool bUseAlphaCompression = true;
		FString suffixes[3];
		int32 selectedTextureGroupIndex = 0;
		TArray<TSharedPtr<FString>> textureGroupOptions;
		TSharedPtr<FString> selectedTextureGroupOption;
		TSharedPtr<SListView<TSharedPtr<FBatchUnpackTextureItem>>> textureListView;
		TSharedPtr<STextBlock> validationTextBlock;
		TSharedPtr<FTextureBatchUnpackRunner> activeRunner;
	};
}

TSharedRef<SWidget> CreateTexturePackerBatchUnpackWindow(const TArray<FString>& InTexturePaths)
{
	return SNew(STexturePackerBatchUnpackWindow)
		.TexturePaths(InTexturePaths);
}

#undef LOCTEXT_NAMESPACE
