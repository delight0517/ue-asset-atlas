#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "ArchiveActionButton.generated.h"

UCLASS()
class BLACKLIONASSETARCHIVE_API UArchiveActionButton : public UButton
{
	GENERATED_BODY()

public:
	UArchiveActionButton(const FObjectInitializer& ObjectInitializer);
	void SetClickAction(FSimpleDelegate InAction) { ClickAction = MoveTemp(InAction); }

private:
	UFUNCTION()
	void DispatchClick();
	FSimpleDelegate ClickAction;
};
