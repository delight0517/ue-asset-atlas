#include "ArchiveShowroomActor.h"

#include "ArchiveCatalogSubsystem.h"
#include "ArchivePanelWidget.h"
#include "ArchiveUiPolicy.h"
#include "Camera/CameraComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"

AArchiveShowroomActor::AArchiveShowroomActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ArchiveRoot"));
	SetRootComponent(SceneRoot);
	PrimaryActorTick.TickInterval = 1.0f / 30.0f;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("ArchiveCamera"));
	Camera->SetupAttachment(SceneRoot);
	Camera->SetRelativeLocation(FVector(560.0f, 0.0f, 170.0f));
	Camera->SetRelativeRotation(FRotator(-12.0f, 180.0f, 0.0f));
	PreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SelectedAssetPreview"));
	PreviewMesh->SetupAttachment(SceneRoot);
	PreviewMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetCastShadow(true);
}

void AArchiveShowroomActor::BeginPlay()
{
	Super::BeginPlay();
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return;
	PC->SetViewTarget(this);
	PC->bShowMouseCursor = true;
	PC->bEnableClickEvents = true;
	PC->bEnableMouseOverEvents = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(InputMode);
	Panel = CreateWidget<UArchivePanelWidget>(PC, UArchivePanelWidget::StaticClass());
	if (Panel)
	{
		Panel->Configure(this);
		Panel->AddToViewport(20);
	}
	SetActorTickEnabled(false);
}

void AArchiveShowroomActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (PreviewMesh && PreviewMesh->GetStaticMesh()) PreviewMesh->AddLocalRotation(FRotator(0.0f, 16.0f * DeltaSeconds, 0.0f));
}

void AArchiveShowroomActor::ShowAsset(const FString& Id)
{
	UArchiveCatalogSubsystem* Catalog = GetGameInstance() ? GetGameInstance()->GetSubsystem<UArchiveCatalogSubsystem>() : nullptr;
	const FBLArchiveEntry* Entry = Catalog ? Catalog->FindEntry(Id) : nullptr;
	if (!Entry) return;
	SelectedId = Id;
	const int64 RequestSerial = ++PreviewLoadSerial;
	if (ActivePreviewLoad.IsValid())
	{
		ActivePreviewLoad->CancelHandle();
		ActivePreviewLoad.Reset();
	}
	PreviewMesh->SetStaticMesh(nullptr);
	PreviewMesh->SetVisibility(false, true);
	PreviewMesh->SetCastShadow(false);
	SetActorTickEnabled(false);

	if (Entry->PreviewMesh.IsEmpty()) return;

	const FSoftObjectPath AssetPath(Entry->PreviewMesh);
	int32 ProjectAssetCount = 0;
	for (const FBLArchiveEntry& ProjectEntry : Catalog->GetEntries())
	{
		if (ProjectEntry.Project == Entry->Project) ++ProjectAssetCount;
	}
	TWeakObjectPtr<AArchiveShowroomActor> WeakThis(this);
	ActivePreviewLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetPath,
		FStreamableDelegate::CreateLambda([WeakThis, AssetPath, RequestSerial, ProjectAssetCount]()
		{
			if (!WeakThis.IsValid() || WeakThis->PreviewLoadSerial != RequestSerial) return;
			UStaticMesh* LoadedMesh = Cast<UStaticMesh>(AssetPath.ResolveObject());
			if (!LoadedMesh)
			{
				UE_LOG(LogTemp, Warning, TEXT("Asset Archive preview did not resolve to a StaticMesh: %s"), *AssetPath.ToString());
				return;
			}
			WeakThis->ApplyLoadedPreview(LoadedMesh, ProjectAssetCount);
		}),
		FStreamableManager::DefaultAsyncLoadPriority,
		false,
		false,
		TEXT("BlackLionAssetArchive_SelectedPreview"));
}

void AArchiveShowroomActor::ClearPreview()
{
	++PreviewLoadSerial;
	SelectedId.Reset();
	if (ActivePreviewLoad.IsValid())
	{
		ActivePreviewLoad->CancelHandle();
		ActivePreviewLoad.Reset();
	}
	if (PreviewMesh)
	{
		PreviewMesh->SetStaticMesh(nullptr);
		PreviewMesh->SetVisibility(false, true);
		PreviewMesh->SetCastShadow(false);
	}
	SetActorTickEnabled(false);
}

void AArchiveShowroomActor::ApplyLoadedPreview(UStaticMesh* Mesh, int32 ProjectAssetCount)
{
	if (!PreviewMesh || !Mesh) return;
	PreviewMesh->SetStaticMesh(Mesh);
	PreviewMesh->SetRelativeRotation(FRotator::ZeroRotator);
	// Fit only the display component into the showroom frame. The imported
	// StaticMesh asset and its source scale remain unchanged.
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const float Radius = FMath::Max(Bounds.SphereRadius, 1.0f);
	const float DisplayScale = FMath::Clamp(150.0f / Radius, 0.01f, 100.0f);
	PreviewMesh->SetRelativeScale3D(FVector(DisplayScale));
	PreviewMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f) - Bounds.Origin * DisplayScale);
	PreviewMesh->SetForcedLodModel(BlackLionArchiveUiPolicy::GetPreviewForcedLod(ProjectAssetCount, Mesh->GetNumLODs()));
	PreviewMesh->SetCastShadow(BlackLionArchiveUiPolicy::ShouldCastPreviewShadow(ProjectAssetCount));
	PreviewMesh->SetVisibility(true, true);
	SetActorTickEnabled(true);
}
