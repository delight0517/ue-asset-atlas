#include "ArchivePanelWidget.h"

#include "ArchiveCatalogSubsystem.h"
#include "ArchiveCatalogTypes.h"
#include "ArchiveActionButton.h"
#include "ArchiveShowroomActor.h"
#include "ArchiveUiPolicy.h"
#include "Async/Async.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/SlateTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/UnrealMemory.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "RHI.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	UButton* MakeButton(UWidgetTree* Tree, UPanelWidget* Parent, const FString& Label, FSimpleDelegate OnClick)
	{
		UArchiveActionButton* Button = Tree->ConstructWidget<UArchiveActionButton>(UArchiveActionButton::StaticClass());
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.12f, 0.13f, 0.20f, 0.95f), 8.0f));
		Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.20f, 0.24f, 0.34f, 1.0f), 8.0f));
		Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.10f, 0.45f, 0.51f, 1.0f), 8.0f));
		Style.SetNormalPadding(FMargin(12.0f, 9.0f));
		Style.SetPressedPadding(FMargin(13.0f, 10.0f, 11.0f, 8.0f));
		Button->SetStyle(Style);
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Text->SetText(FText::FromString(Label));
		Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.91f, 1.0f)));
		Text->SetJustification(ETextJustify::Left);
		Button->AddChild(Text);
		Button->SetClickAction(MoveTemp(OnClick));
		UPanelSlot* Slot = Parent->AddChild(Button);
		if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(Slot)) VerticalSlot->SetPadding(FMargin(4.0f, 3.0f));
		if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(Slot)) HorizontalSlot->SetPadding(FMargin(4.0f, 3.0f));
		return Button;
	}

	bool ResampleBGRA(const TArray<uint8>& Source, int32 SourceWidth, int32 SourceHeight, int32 MaxDimension, TArray<uint8>& OutPixels, int32& OutWidth, int32& OutHeight)
	{
		if (SourceWidth <= 0 || SourceHeight <= 0 || MaxDimension <= 0 || Source.Num() != int64(SourceWidth) * SourceHeight * 4) return false;
		const float Scale = FMath::Min(1.0f, float(MaxDimension) / float(FMath::Max(SourceWidth, SourceHeight)));
		OutWidth = FMath::Max(1, FMath::RoundToInt(SourceWidth * Scale));
		OutHeight = FMath::Max(1, FMath::RoundToInt(SourceHeight * Scale));
		OutPixels.SetNumUninitialized(OutWidth * OutHeight * 4);

		for (int32 Y = 0; Y < OutHeight; ++Y)
		{
			const float SourceY = FMath::Clamp((float(Y) + 0.5f) * float(SourceHeight) / float(OutHeight) - 0.5f, 0.0f, float(SourceHeight - 1));
			const int32 Y0 = FMath::FloorToInt(SourceY);
			const int32 Y1 = FMath::Min(Y0 + 1, SourceHeight - 1);
			const float FY = SourceY - float(Y0);
			for (int32 X = 0; X < OutWidth; ++X)
			{
				const float SourceX = FMath::Clamp((float(X) + 0.5f) * float(SourceWidth) / float(OutWidth) - 0.5f, 0.0f, float(SourceWidth - 1));
				const int32 X0 = FMath::FloorToInt(SourceX);
				const int32 X1 = FMath::Min(X0 + 1, SourceWidth - 1);
				const float FX = SourceX - float(X0);
				const int32 DestinationOffset = (Y * OutWidth + X) * 4;
				const int32 TopLeft = (Y0 * SourceWidth + X0) * 4;
				const int32 TopRight = (Y0 * SourceWidth + X1) * 4;
				const int32 BottomLeft = (Y1 * SourceWidth + X0) * 4;
				const int32 BottomRight = (Y1 * SourceWidth + X1) * 4;
				for (int32 Channel = 0; Channel < 4; ++Channel)
				{
					const float Top = FMath::Lerp(float(Source[TopLeft + Channel]), float(Source[TopRight + Channel]), FX);
					const float Bottom = FMath::Lerp(float(Source[BottomLeft + Channel]), float(Source[BottomRight + Channel]), FX);
					OutPixels[DestinationOffset + Channel] = uint8(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(Top, Bottom, FY)), 0, 255));
				}
			}
		}
		return true;
	}
}

void UArchivePanelWidget::Configure(AArchiveShowroomActor* InShowroom)
{
	Showroom = InShowroom;
	Rebuild();
}

void UArchivePanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	Rebuild();
}

void UArchivePanelWidget::Rebuild()
{
	if (!WidgetTree) return;
	if (!WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		WidgetTree->RootWidget = Canvas;
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Panel->SetBrushColor(FLinearColor(0.055f, 0.045f, 0.11f, 0.90f));
		Panel->SetPadding(FMargin(16.0f, 18.0f));
		Canvas->AddChild(Panel);
		UCanvasPanelSlot* PanelSlot = Cast<UCanvasPanelSlot>(Panel->Slot);
		PanelSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 1.0f));
		PanelSlot->SetAlignment(FVector2D(1.0f, 0.0f));
		PanelSlot->SetOffsets(FMargin(-480.0f, 24.0f, -24.0f, -24.0f));
		UScrollBox* MainScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		Panel->SetContent(MainScroll);
		UVerticalBox* Layout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		MainScroll->AddChild(Layout);
		UTextBlock* Header = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Header->SetText(FText::FromString(TEXT("BLACK LION  /  ASSET ARCHIVE")));
		Header->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.74f, 1.0f)));
		Layout->AddChildToVerticalBox(Header);
		UTextBlock* Hint = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Hint->SetText(FText::FromString(TEXT("Browse project folders, then select a model or space to preview it.")));
		Hint->SetColorAndOpacity(FSlateColor(FLinearColor(0.68f, 0.66f, 0.76f)));
		Layout->AddChildToVerticalBox(Hint);
		USizeBox* PreviewFrame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		PreviewFrame->SetWidthOverride(320.0f);
		PreviewFrame->SetHeightOverride(156.0f);
		PreviewImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		PreviewImage->SetVisibility(ESlateVisibility::Collapsed);
		PreviewFrame->SetContent(PreviewImage);
		Layout->AddChildToVerticalBox(PreviewFrame);
		PreviewQualityStatus = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		PreviewQualityStatus->SetColorAndOpacity(FSlateColor(FLinearColor(0.50f, 0.78f, 0.73f)));
		Layout->AddChildToVerticalBox(PreviewQualityStatus);
		UTextBlock* FolderHeader = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FolderHeader->SetText(FText::FromString(TEXT("PROJECT FOLDERS")));
		FolderHeader->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.74f, 1.0f)));
		Layout->AddChildToVerticalBox(FolderHeader);
		FolderPageStatus = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FolderPageStatus->SetColorAndOpacity(FSlateColor(FLinearColor(0.68f, 0.66f, 0.76f)));
		Layout->AddChildToVerticalBox(FolderPageStatus);
		UHorizontalBox* FolderPager = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Layout->AddChildToVerticalBox(FolderPager);
		MakeButton(WidgetTree, FolderPager, TEXT("‹ folders"), FSimpleDelegate::CreateLambda([this]() { FolderPageIndex = FMath::Max(0, FolderPageIndex - 1); Rebuild(); }));
		MakeButton(WidgetTree, FolderPager, TEXT("folders ›"), FSimpleDelegate::CreateLambda([this]() { ++FolderPageIndex; Rebuild(); }));
		UScrollBox* FolderScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		Layout->AddChildToVerticalBox(FolderScroll);
		FolderList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		FolderScroll->AddChild(FolderList);
		SelectedTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		SelectedTitle->SetText(FText::FromString(TEXT("Choose an asset")));
		SelectedTitle->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Layout->AddChildToVerticalBox(SelectedTitle);
		SelectedStatus = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		SelectedStatus->SetColorAndOpacity(FSlateColor(FLinearColor(0.83f, 0.78f, 0.64f)));
		Layout->AddChildToVerticalBox(SelectedStatus);
		UTextBlock* TodoHeader = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		TodoHeader->SetText(FText::FromString(TEXT("NEXT  /  ASSET TASKS")));
		TodoHeader->SetColorAndOpacity(FSlateColor(FLinearColor(0.48f, 0.82f, 0.86f)));
		Layout->AddChildToVerticalBox(TodoHeader);
		UScrollBox* TodoScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		Layout->AddChildToVerticalBox(TodoScroll);
		TodoList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		TodoScroll->AddChild(TodoList);
		UTextBlock* AssetHeader = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		AssetHeader->SetText(FText::FromString(TEXT("ASSETS  /  SELECT ONE TO LOAD")));
		AssetHeader->SetColorAndOpacity(FSlateColor(FLinearColor(0.48f, 0.82f, 0.86f)));
		Layout->AddChildToVerticalBox(AssetHeader);
		AssetPageStatus = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		AssetPageStatus->SetColorAndOpacity(FSlateColor(FLinearColor(0.68f, 0.66f, 0.76f)));
		Layout->AddChildToVerticalBox(AssetPageStatus);
		UHorizontalBox* AssetPager = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Layout->AddChildToVerticalBox(AssetPager);
		MakeButton(WidgetTree, AssetPager, TEXT("‹ previous"), FSimpleDelegate::CreateLambda([this]() { AssetPageIndex = FMath::Max(0, AssetPageIndex - 1); Rebuild(); }));
		MakeButton(WidgetTree, AssetPager, TEXT("next ›"), FSimpleDelegate::CreateLambda([this]() { ++AssetPageIndex; Rebuild(); }));
		UScrollBox* AssetScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		Layout->AddChildToVerticalBox(AssetScroll);
		AssetList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		AssetScroll->AddChild(AssetList);
		UTextBlock* FileHeader = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FileHeader->SetText(FText::FromString(TEXT("LINKED SOURCE FILES")));
		FileHeader->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.74f, 1.0f)));
		Layout->AddChildToVerticalBox(FileHeader);
		UScrollBox* FileScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		Layout->AddChildToVerticalBox(FileScroll);
		FileList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		FileScroll->AddChild(FileList);
		UTextBlock* CollectionHeader = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		CollectionHeader->SetText(FText::FromString(TEXT("PROJECT WORKSPACE  /  COLLECTION")));
		CollectionHeader->SetColorAndOpacity(FSlateColor(FLinearColor(0.48f, 0.82f, 0.86f)));
		Layout->AddChildToVerticalBox(CollectionHeader);
		ProjectIdInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		ProjectIdInput->SetHintText(FText::FromString(TEXT("Project ID from this catalog")));
		Layout->AddChildToVerticalBox(ProjectIdInput);
		MakeButton(WidgetTree, Layout, TEXT("Open project workspace"), FSimpleDelegate::CreateLambda([this]() {
			UGameInstance* Game = GetGameInstance();
			UArchiveCatalogSubsystem* Data = Game ? Game->GetSubsystem<UArchiveCatalogSubsystem>() : nullptr;
			if (!Data || !ProjectIdInput) return;
			const FString Requested = ProjectIdInput->GetText().ToString().TrimStartAndEnd();
			if (Requested != Data->GetProjectId())
			{
				if (SelectedStatus) SelectedStatus->SetText(FText::FromString(TEXT("Project mismatch. This Unreal project can open only its own catalog.")));
				return;
			}
			ActiveProjectId = Requested;
			ActiveAssetIds.Reset();
			bCollectionFilterActive = false;
			ActiveFolderId.Reset();
			Rebuild();
			if (SelectedStatus) SelectedStatus->SetText(FText::FromString(FString::Printf(TEXT("Project scope locked: %s"), *ActiveProjectId)));
		}));
		MakeButton(WidgetTree, Layout, TEXT("Refresh local catalog"), FSimpleDelegate::CreateLambda([this]() {
			UGameInstance* Game = GetGameInstance();
			UArchiveCatalogSubsystem* Data = Game ? Game->GetSubsystem<UArchiveCatalogSubsystem>() : nullptr;
			if (!Data) return;
			const FString PreviousSelection = SelectedAssetId;
			FString Error;
			if (!Data->ReloadCatalog(Error))
			{
				if (SelectedStatus) SelectedStatus->SetText(FText::FromString(Error));
				return;
			}
			ActiveProjectId = Data->GetProjectId();
			if (ProjectIdInput) ProjectIdInput->SetText(FText::FromString(ActiveProjectId));
			Rebuild();
			if (Data->FindEntry(PreviousSelection)) SelectAsset(PreviousSelection);
			if (SelectedStatus) SelectedStatus->SetText(FText::FromString(FString::Printf(TEXT("Catalog refreshed: %d assets for %s."), Data->GetEntries().Num(), *ActiveProjectId)));
		}));
		MakeButton(WidgetTree, Layout, TEXT("Save project collection"), FSimpleDelegate::CreateLambda([this]() {
			UGameInstance* Game = GetGameInstance();
			UArchiveCatalogSubsystem* Data = Game ? Game->GetSubsystem<UArchiveCatalogSubsystem>() : nullptr;
			if (!Data || !ProjectIdInput) return;
			TArray<FString> Ids;
			for (const FBLArchiveEntry& Entry : Data->GetEntries())
			{
				if (Entry.Project != ActiveProjectId) continue;
				const FString FolderId = Entry.Folder.IsEmpty() ? Entry.Group : Entry.Folder;
				if (bCollectionFilterActive && !ActiveAssetIds.Contains(Entry.Id)) continue;
				if (!ActiveFolderId.IsEmpty() && FolderId != ActiveFolderId) continue;
				Ids.Add(Entry.Id);
			}
			FString Error;
			const bool bSaved = Data->SaveProjectCollection(ProjectIdInput->GetText().ToString(), Ids, Error);
			if (SelectedStatus) SelectedStatus->SetText(FText::FromString(bSaved ? FString::Printf(TEXT("Saved %d assets to this project's local collection."), Ids.Num()) : Error));
		}));
		MakeButton(WidgetTree, Layout, TEXT("Load project collection"), FSimpleDelegate::CreateLambda([this]() {
			UGameInstance* Game = GetGameInstance();
			UArchiveCatalogSubsystem* Data = Game ? Game->GetSubsystem<UArchiveCatalogSubsystem>() : nullptr;
			if (!Data || !ProjectIdInput) return;
			TArray<FString> Ids;
			FString Error;
			if (!Data->LoadProjectCollection(ProjectIdInput->GetText().ToString(), Ids, Error))
			{
				if (SelectedStatus) SelectedStatus->SetText(FText::FromString(Error));
				return;
			}
			ActiveAssetIds = Ids;
			bCollectionFilterActive = true;
			ActiveFolderId.Reset();
			FolderPageIndex = 0;
			AssetPageIndex = 0;
			Rebuild();
			if (SelectedStatus) SelectedStatus->SetText(FText::FromString(FString::Printf(TEXT("Loaded %d saved asset IDs. Select an item to load its preview."), Ids.Num())));
		}));
	}
	if (!AssetList || !FolderList || !FileList || !TodoList) return;
	FolderList->ClearChildren();
	AssetList->ClearChildren();
	FileList->ClearChildren();
	TodoList->ClearChildren();
	UGameInstance* GI = GetGameInstance();
	UArchiveCatalogSubsystem* Catalog = GI ? GI->GetSubsystem<UArchiveCatalogSubsystem>() : nullptr;
	if (!Catalog)
	{
		if (SelectedStatus) SelectedStatus->SetText(FText::FromString(TEXT("Catalog is unavailable. Import the archive FBX groups first.")));
		return;
	}
	if (Catalog->GetEntries().IsEmpty())
	{
		if (SelectedStatus) SelectedStatus->SetText(FText::FromString(Catalog->GetLastError().IsEmpty() ? TEXT("This project has no registered archive assets yet.") : Catalog->GetLastError()));
	}
	if (ActiveProjectId.IsEmpty()) ActiveProjectId = Catalog->GetProjectId();
	if (ProjectIdInput && ProjectIdInput->GetText().IsEmpty()) ProjectIdInput->SetText(FText::FromString(ActiveProjectId));
	int32 ProjectAssetCount = 0;
	for (const FBLArchiveEntry& Entry : Catalog->GetEntries())
	{
		if (Entry.Project == ActiveProjectId) ++ProjectAssetCount;
	}
	const int32 ThumbnailMaxDimension = BlackLionArchiveUiPolicy::GetThumbnailMaxDimension(ProjectAssetCount);
	if (PreviewQualityStatus)
	{
		const TCHAR* QualityName = ThumbnailMaxDimension == 768 ? TEXT("HIGH") : (ThumbnailMaxDimension == 512 ? TEXT("BALANCED") : TEXT("ECONOMY"));
		PreviewQualityStatus->SetText(FText::FromString(FString::Printf(TEXT("AUTO PREVIEW  /  %s  ·  thumbnail max %d px  ·  one mesh loaded at a time"), QualityName, ThumbnailMaxDimension)));
	}
	TMap<FString, int32> FolderCounts;
	TSet<FString> FolderIdSet;
	for (const FBLArchiveEntry& Entry : Catalog->GetEntries())
	{
		if (Entry.Project != ActiveProjectId) continue;
		const FString FolderId = Entry.Folder.IsEmpty() ? Entry.Group : Entry.Folder;
		if (!FolderId.IsEmpty())
		{
			++FolderCounts.FindOrAdd(FolderId);
			FolderIdSet.Add(FolderId);
		}
	}
	TArray<FString> FolderIds = FolderIdSet.Array();
	FolderIds.Sort();
	TArray<FString> FolderPageIds;
	FolderPageIds.Reserve(FolderIds.Num() + 1);
	FolderPageIds.Add(FString());
	FolderPageIds.Append(FolderIds);
	FolderPageIndex = BlackLionArchiveUiPolicy::ClampPageIndex(FolderPageIndex, FolderPageIds.Num(), BlackLionArchiveUiPolicy::FolderPageSize);
	const TArray<FString> VisibleFolderIds = BlackLionArchiveUiPolicy::GetPageItems(FolderPageIds, FolderPageIndex, BlackLionArchiveUiPolicy::FolderPageSize);
	if (FolderPageStatus)
	{
		const int32 PageCount = BlackLionArchiveUiPolicy::GetPageCount(FolderPageIds.Num(), BlackLionArchiveUiPolicy::FolderPageSize);
		FolderPageStatus->SetText(FText::FromString(FString::Printf(TEXT("%d folders · page %d / %d"), FolderIds.Num(), PageCount == 0 ? 0 : FolderPageIndex + 1, PageCount)));
	}
	for (const FString& FolderId : VisibleFolderIds)
	{
		const FString Label = FolderId.IsEmpty() ? FString(TEXT("All project folders")) : FString::Printf(TEXT("%s  (%d)"), *FolderId, FolderCounts.FindRef(FolderId));
		MakeButton(WidgetTree, FolderList, Label, FSimpleDelegate::CreateLambda([this, Id=FolderId]() { SelectFolder(Id); }));
	}
	TArray<FString> FilteredAssetIds;
	TSet<FString> CollectionIdSet;
	if (bCollectionFilterActive)
	{
		CollectionIdSet.Reserve(ActiveAssetIds.Num());
		for (const FString& Id : ActiveAssetIds) CollectionIdSet.Add(Id);
	}
	FilteredAssetIds.Reserve(ProjectAssetCount);
	for (const FBLArchiveEntry& Entry : Catalog->GetEntries())
	{
		if (Entry.Project != ActiveProjectId) continue;
		if (bCollectionFilterActive && !CollectionIdSet.Contains(Entry.Id)) continue;
		const FString FolderId = Entry.Folder.IsEmpty() ? Entry.Group : Entry.Folder;
		if (!ActiveFolderId.IsEmpty() && FolderId != ActiveFolderId) continue;
		FilteredAssetIds.Add(Entry.Id);
	}
	AssetPageIndex = BlackLionArchiveUiPolicy::ClampPageIndex(AssetPageIndex, FilteredAssetIds.Num(), BlackLionArchiveUiPolicy::AssetPageSize);
	const int32 AssetPageCount = BlackLionArchiveUiPolicy::GetPageCount(FilteredAssetIds.Num(), BlackLionArchiveUiPolicy::AssetPageSize);
	if (AssetPageStatus)
	{
		const int32 PageStart = FilteredAssetIds.IsEmpty() ? 0 : AssetPageIndex * BlackLionArchiveUiPolicy::AssetPageSize + 1;
		const int32 PageEnd = FMath::Min((AssetPageIndex + 1) * BlackLionArchiveUiPolicy::AssetPageSize, FilteredAssetIds.Num());
		AssetPageStatus->SetText(FilteredAssetIds.IsEmpty()
			? FText::FromString(TEXT("No assets in this view."))
			: FText::FromString(FString::Printf(TEXT("Showing %d–%d of %d · page %d / %d · max %d rows created"), PageStart, PageEnd, FilteredAssetIds.Num(), AssetPageIndex + 1, AssetPageCount, BlackLionArchiveUiPolicy::AssetPageSize)));
	}
	for (const FString& Id : BlackLionArchiveUiPolicy::GetPageItems(FilteredAssetIds, AssetPageIndex, BlackLionArchiveUiPolicy::AssetPageSize))
	{
		const FBLArchiveEntry* Entry = Catalog->FindEntry(Id);
		if (!Entry) continue;
		const FString TypeTag = Entry->AssetType.IsEmpty() ? Entry->Kind : Entry->AssetType;
		MakeButton(WidgetTree, AssetList, FString::Printf(TEXT("%s   ·   %s"), *Entry->Name, *TypeTag), FSimpleDelegate::CreateLambda([this, Id]() { SelectAsset(Id); }));
	}
	const FBLArchiveEntry* SelectedEntry = SelectedAssetId.IsEmpty() ? nullptr : Catalog->FindEntry(SelectedAssetId);
	const bool bSelectedStillInFilter = SelectedEntry && SelectedEntry->Project == ActiveProjectId &&
		(!bCollectionFilterActive || ActiveAssetIds.Contains(SelectedEntry->Id)) &&
		(ActiveFolderId.IsEmpty() || (SelectedEntry->Folder.IsEmpty() ? SelectedEntry->Group : SelectedEntry->Folder) == ActiveFolderId);
	if (!SelectedAssetId.IsEmpty() && !bSelectedStillInFilter)
	{
		++PreviewRequestSerial;
		SelectedAssetId.Reset();
		PreviewTexture = nullptr;
		if (PreviewImage) PreviewImage->SetVisibility(ESlateVisibility::Collapsed);
		if (Showroom.IsValid()) Showroom->ClearPreview();
		if (SelectedTitle) SelectedTitle->SetText(FText::FromString(TEXT("Choose an asset")));
		if (FileList) FileList->ClearChildren();
		if (TodoList) TodoList->ClearChildren();
	}
}

void UArchivePanelWidget::SelectFolder(const FString& FolderId)
{
	ActiveFolderId = FolderId == TEXT("All project folders") ? FString() : FolderId;
	AssetPageIndex = 0;
	Rebuild();
}

void UArchivePanelWidget::SelectAsset(const FString& Id)
{
	UGameInstance* GI = GetGameInstance();
	UArchiveCatalogSubsystem* Catalog = GI ? GI->GetSubsystem<UArchiveCatalogSubsystem>() : nullptr;
	const FBLArchiveEntry* Entry = Catalog ? Catalog->FindEntry(Id) : nullptr;
	if (!Entry || Entry->Project != ActiveProjectId) return;
	SelectedAssetId = Id;
	const int64 RequestSerial = ++PreviewRequestSerial;
	if (SelectedTitle) SelectedTitle->SetText(FText::FromString(Entry->Name));
	FString Description = Entry->Status;
	if (Entry->Kind.Equals(TEXT("space"), ESearchCase::IgnoreCase))
	{
		Description = FString::Printf(TEXT("SPACE  /  %s"), *Description);
	}
	if (!Entry->PreviewAsset.IsEmpty())
	{
		Description += FString::Printf(TEXT("\nUNREAL ASSET  /  %s"), *Entry->PreviewAsset);
	}
	if (SelectedStatus) SelectedStatus->SetText(FText::FromString(Description));
	if (Showroom.IsValid()) Showroom->ShowAsset(Id);
	PreviewTexture = nullptr;
	if (PreviewImage)
	{
		PreviewImage->SetVisibility(ESlateVisibility::Collapsed);
		if (!Entry->PreviewThumbnail.IsEmpty() && Catalog->IsRegisteredPreviewImage(Id, Entry->PreviewThumbnail))
		{
			const FString ThumbnailPath = Entry->PreviewThumbnail;
			int32 ProjectAssetCount = 0;
			for (const FBLArchiveEntry& ProjectEntry : Catalog->GetEntries())
			{
				if (ProjectEntry.Project == ActiveProjectId) ++ProjectAssetCount;
			}
			const int32 MaxDimension = BlackLionArchiveUiPolicy::GetThumbnailMaxDimension(ProjectAssetCount);
			IImageWrapperModule* ImageModule = &FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			TWeakObjectPtr<UArchivePanelWidget> WeakThis(this);
			Async(EAsyncExecution::ThreadPool, [WeakThis, ImageModule, ThumbnailPath, Id, RequestSerial, MaxDimension]()
			{
				TArray<uint8> Compressed;
				const int64 FileSize = IFileManager::Get().FileSize(*ThumbnailPath);
				if (FileSize <= 0 || FileSize > 12 * 1024 * 1024 || !FFileHelper::LoadFileToArray(Compressed, *ThumbnailPath)) return;
				const FString Extension = FPaths::GetExtension(ThumbnailPath, true).ToLower();
				const EImageFormat Format = Extension == TEXT(".png") ? EImageFormat::PNG : EImageFormat::JPEG;
				TSharedPtr<IImageWrapper> Wrapper = ImageModule->CreateImageWrapper(Format);
				if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Compressed.GetData(), Compressed.Num()) || Wrapper->GetWidth() <= 0 || Wrapper->GetHeight() <= 0 || Wrapper->GetWidth() > 4096 || Wrapper->GetHeight() > 4096) return;
				TArray<uint8> Raw;
				TArray<uint8> Reduced;
				int32 Width = 0;
				int32 Height = 0;
				if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw) || !ResampleBGRA(Raw, Wrapper->GetWidth(), Wrapper->GetHeight(), MaxDimension, Reduced, Width, Height)) return;
				AsyncTask(ENamedThreads::GameThread, [WeakThis, Id, RequestSerial, Width, Height, Pixels=MoveTemp(Reduced)]() mutable
				{
					if (!WeakThis.IsValid() || WeakThis->PreviewRequestSerial != RequestSerial || WeakThis->SelectedAssetId != Id || !WeakThis->PreviewImage) return;
					UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
					if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty()) return;
					FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
					void* Destination = Mip.BulkData.Lock(LOCK_READ_WRITE);
					if (!Destination) return;
					FMemory::Memcpy(Destination, Pixels.GetData(), Pixels.Num());
					Mip.BulkData.Unlock();
					Texture->UpdateResource();
					WeakThis->PreviewTexture = Texture;
					WeakThis->PreviewImage->SetBrushFromTexture(Texture, true);
					WeakThis->PreviewImage->SetVisibility(ESlateVisibility::Visible);
				});
			});
		}
		else if (Entry->AssetType == TEXT("Texture2D") && !Entry->PreviewAsset.IsEmpty())
		{
			if (UTexture2D* AssetTexture = LoadObject<UTexture2D>(nullptr, *Entry->PreviewAsset))
			{
				PreviewTexture = AssetTexture;
				PreviewImage->SetBrushFromTexture(PreviewTexture, true);
				PreviewImage->SetVisibility(ESlateVisibility::Visible);
			}
		}
	}
	if (FileList)
	{
		FileList->ClearChildren();
		for (const FBLArchiveFileLink& Link : Entry->Files)
		{
			MakeButton(WidgetTree, FileList, Link.Label, FSimpleDelegate::CreateLambda([this, Id, Path=Link.Path]() { OpenFile(Id, Path); }));
		}
	}
	if (TodoList)
	{
		TodoList->ClearChildren();
		if (Entry->Todos.IsEmpty())
		{
			MakeButton(WidgetTree, TodoList, TEXT("No open tasks registered for this asset."), FSimpleDelegate());
		}
		else
		{
			for (int32 Index = 0; Index < Entry->Todos.Num(); ++Index)
			{
				const FString Row = FString::Printf(TEXT("%02d  %s"), Index + 1, *Entry->Todos[Index]);
				MakeButton(WidgetTree, TodoList, Row, FSimpleDelegate());
			}
		}
	}
}

void UArchivePanelWidget::OpenFile(const FString& Id, const FString& Path)
{
	UGameInstance* GI = GetGameInstance();
	if (UArchiveCatalogSubsystem* Catalog = GI ? GI->GetSubsystem<UArchiveCatalogSubsystem>() : nullptr)
	{
		FString Error;
		if (!Catalog->OpenLinkedFile(Id, Path, Error) && SelectedStatus) SelectedStatus->SetText(FText::FromString(Error));
	}
}
