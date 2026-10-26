#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> PreviewMesh;
	UPROPERTY()
	TObjectPtr<UArchivePanelWidget> Panel;
	FString SelectedId;
};
