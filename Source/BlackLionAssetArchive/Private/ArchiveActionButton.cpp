#include "ArchiveActionButton.h"

UArchiveActionButton::UArchiveActionButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	OnClicked.AddDynamic(this, &UArchiveActionButton::DispatchClick);
}

void UArchiveActionButton::DispatchClick()
{
	ClickAction.ExecuteIfBound();
}
