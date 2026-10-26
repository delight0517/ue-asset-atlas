#include "ArchivePanelWidget.h"

#include "ArchiveCatalogSubsystem.h"
#include "ArchiveCatalogTypes.h"
#include "ArchiveActionButton.h"
#include "ArchiveShowroomActor.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
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
	UButton* MakeButton(UWidgetTree* Tree, UVerticalBox* Parent, const FString& Label, FSimpleDelegate OnClick)
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
		UVerticalBoxSlot* Slot = Parent->AddChildToVerticalBox(Button);
		Slot->SetPadding(FMargin(4.0f, 3.0f));
		return Button;
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
			Rebuild();
			if (Ids.Num() > 0) SelectAsset(Ids[0]);
			if (SelectedStatus) SelectedStatus->SetText(FText::FromString(FString::Printf(TEXT("Loaded %d saved asset IDs for this project."), Ids.Num())));
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
	MakeButton(WidgetTree, FolderList, TEXT("All project folders"), FSimpleDelegate::CreateLambda([this]() { SelectFolder(FString()); }));
	TMap<FString, int32> FolderCounts;
	TArray<FString> FolderIds;
	for (const FBLArchiveEntry& Entry : Catalog->GetEntries())
	{
		if (Entry.Project != ActiveProjectId) continue;
		const FString FolderId = Entry.Folder.IsEmpty() ? Entry.Group : Entry.Folder;
		if (!FolderId.IsEmpty())
		{
			++FolderCounts.FindOrAdd(FolderId);
			FolderIds.AddUnique(FolderId);
		}
	}
	FolderIds.Sort();
	for (const FString& FolderId : FolderIds)
	{
		const FString Label = FString::Printf(TEXT("%s  (%d)"), *FolderId, FolderCounts[FolderId]);
		MakeButton(WidgetTree, FolderList, Label, FSimpleDelegate::CreateLambda([this, Id=FolderId]() { SelectFolder(Id); }));
	}
	bool bSelectionVisible = false;
	FString FirstVisibleAssetId;
	for (const FBLArchiveEntry& Entry : Catalog->GetEntries())
	{
		if (Entry.Project != ActiveProjectId) continue;
		if (bCollectionFilterActive && !ActiveAssetIds.Contains(Entry.Id)) continue;
		const FString FolderId = Entry.Folder.IsEmpty() ? Entry.Group : Entry.Folder;
		if (!ActiveFolderId.IsEmpty() && FolderId != ActiveFolderId) continue;
		const FString TypeTag = Entry.AssetType.IsEmpty() ? Entry.Kind : Entry.AssetType;
		MakeButton(WidgetTree, AssetList, FString::Printf(TEXT("%s   ·   %s"), *Entry.Name, *TypeTag), FSimpleDelegate::CreateLambda([this, Id=Entry.Id]() { SelectAsset(Id); }));
		if (FirstVisibleAssetId.IsEmpty()) FirstVisibleAssetId = Entry.Id;
		if (Entry.Id == SelectedAssetId) bSelectionVisible = true;
	}
	if (!bSelectionVisible && !FirstVisibleAssetId.IsEmpty()) SelectAsset(FirstVisibleAssetId);
}

void UArchivePanelWidget::SelectFolder(const FString& FolderId)
{
	ActiveFolderId = FolderId;
	Rebuild();
}

void UArchivePanelWidget::SelectAsset(const FString& Id)
{
	UGameInstance* GI = GetGameInstance();
	UArchiveCatalogSubsystem* Catalog = GI ? GI->GetSubsystem<UArchiveCatalogSubsystem>() : nullptr;
	const FBLArchiveEntry* Entry = Catalog ? Catalog->FindEntry(Id) : nullptr;
	if (!Entry || Entry->Project != ActiveProjectId) return;
	SelectedAssetId = Id;
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
			TArray<uint8> Compressed;
			const int64 FileSize = IFileManager::Get().FileSize(*Entry->PreviewThumbnail);
			if (FileSize > 0 && FileSize <= 12 * 1024 * 1024 && FFileHelper::LoadFileToArray(Compressed, *Entry->PreviewThumbnail))
			{
				const FString Extension = FPaths::GetExtension(Entry->PreviewThumbnail, true).ToLower();
				const EImageFormat Format = Extension == TEXT(".png") ? EImageFormat::PNG : EImageFormat::JPEG;
				IImageWrapperModule& ImageModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
				TSharedPtr<IImageWrapper> Wrapper = ImageModule.CreateImageWrapper(Format);
				if (Wrapper.IsValid() && Wrapper->SetCompressed(Compressed.GetData(), Compressed.Num()) && Wrapper->GetWidth() > 0 && Wrapper->GetHeight() > 0 && Wrapper->GetWidth() <= 4096 && Wrapper->GetHeight() <= 4096)
				{
					TArray<uint8> Raw;
					if (Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw) && Raw.Num() == Wrapper->GetWidth() * Wrapper->GetHeight() * 4)
					{
						PreviewTexture = UTexture2D::CreateTransient(Wrapper->GetWidth(), Wrapper->GetHeight(), PF_B8G8R8A8);
						if (PreviewTexture && PreviewTexture->GetPlatformData() && PreviewTexture->GetPlatformData()->Mips.Num() > 0)
						{
							FTexture2DMipMap& Mip = PreviewTexture->GetPlatformData()->Mips[0];
							void* Destination = Mip.BulkData.Lock(LOCK_READ_WRITE);
							if (Destination)
							{
								FMemory::Memcpy(Destination, Raw.GetData(), Raw.Num());
								Mip.BulkData.Unlock();
								PreviewTexture->UpdateResource();
								PreviewImage->SetBrushFromTexture(PreviewTexture, true);
								PreviewImage->SetVisibility(ESlateVisibility::Visible);
							}
							else
							{
								PreviewTexture = nullptr;
							}
						}
					}
				}
			}
		}
		if (PreviewImage->GetVisibility() != ESlateVisibility::Visible && Entry->AssetType == TEXT("Texture2D") && !Entry->PreviewAsset.IsEmpty())
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
