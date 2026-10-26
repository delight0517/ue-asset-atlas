#include "ArchiveShowroomActor.h"

#include "ArchiveCatalogSubsystem.h"
#include "ArchivePanelWidget.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"

AArchiveShowroomActor::AArchiveShowroomActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ArchiveRoot"));
	SetRootComponent(SceneRoot);
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
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UArchiveCatalogSubsystem* Catalog = GI->GetSubsystem<UArchiveCatalogSubsystem>(); Catalog && Catalog->GetEntries().Num() > 0)
		{
			ShowAsset(Catalog->GetEntries()[0].Id);
		}
	}
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
	UStaticMesh* Mesh = Entry->PreviewMesh.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *Entry->PreviewMesh);
	PreviewMesh->SetStaticMesh(Mesh);
	PreviewMesh->SetRelativeRotation(FRotator::ZeroRotator);
	if (Mesh)
	{
		// Fit only the display component into the showroom frame. The imported
		// StaticMesh asset and its source scale remain unchanged.
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const float Radius = FMath::Max(Bounds.SphereRadius, 1.0f);
		const float DisplayScale = FMath::Clamp(150.0f / Radius, 0.01f, 100.0f);
		PreviewMesh->SetRelativeScale3D(FVector(DisplayScale));
		PreviewMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f) - Bounds.Origin * DisplayScale);
	}
	else
	{
		PreviewMesh->SetRelativeScale3D(FVector::OneVector);
		PreviewMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
	}
	PreviewMesh->SetVisibility(Mesh != nullptr, true);
}
