#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ArchivePanelWidget.generated.h"

class AArchiveShowroomActor;
class UVerticalBox;
class UTextBlock;
class UEditableTextBox;
class UImage;
class UTexture2D;

UCLASS()
class BLACKLIONASSETARCHIVE_API UArchivePanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Configure(AArchiveShowroomActor* InShowroom);

protected:
	virtual void NativeOnInitialized() override;

private:
	void Rebuild();
	void SelectFolder(const FString& FolderId);
	void SelectAsset(const FString& Id);
	void OpenFile(const FString& Id, const FString& Path);
	TWeakObjectPtr<AArchiveShowroomActor> Showroom;
	UVerticalBox* AssetList = nullptr;
	UVerticalBox* FolderList = nullptr;
	UVerticalBox* FileList = nullptr;
	UVerticalBox* TodoList = nullptr;
	UTextBlock* SelectedTitle = nullptr;
	UTextBlock* SelectedStatus = nullptr;
	UTextBlock* FolderPageStatus = nullptr;
	UTextBlock* AssetPageStatus = nullptr;
	UTextBlock* PreviewQualityStatus = nullptr;
	UImage* PreviewImage = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> PreviewTexture;
	UEditableTextBox* ProjectIdInput = nullptr;
	TArray<FString> ActiveAssetIds;
	int32 FolderPageIndex = 0;
	int32 AssetPageIndex = 0;
	int64 PreviewRequestSerial = 0;
	FString ActiveFolderId;
	FString ActiveProjectId;
	FString SelectedAssetId;
	bool bCollectionFilterActive = false;
};
