#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/StreamableManager.h"
#include "ArchiveShowroomActor.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UArchivePanelWidget;

UCLASS()
class BLACKLIONASSETARCHIVE_API AArchiveShowroomActor : public AActor
{
	GENERATED_BODY()

public:
	AArchiveShowroomActor();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	void ShowAsset(const FString& Id);
	void ClearPreview();

private:
	void ApplyLoadedPreview(UStaticMesh* Mesh, int32 ProjectAssetCount);
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> PreviewMesh;
	UPROPERTY()
	TObjectPtr<UArchivePanelWidget> Panel;
	TSharedPtr<FStreamableHandle> ActivePreviewLoad;
	FString SelectedId;
	int64 PreviewLoadSerial = 0;
};
