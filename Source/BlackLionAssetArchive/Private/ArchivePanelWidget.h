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
	UImage* PreviewImage = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> PreviewTexture;
	UEditableTextBox* ProjectIdInput = nullptr;
	TArray<FString> ActiveAssetIds;
	FString ActiveFolderId;
	FString ActiveProjectId;
	FString SelectedAssetId;
	bool bCollectionFilterActive = false;
};
