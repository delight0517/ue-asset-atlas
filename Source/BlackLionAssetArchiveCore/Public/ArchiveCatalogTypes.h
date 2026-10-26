#pragma once

#include "CoreMinimal.h"

struct FBLArchiveFileLink
{
	FString Label;
	FString Path;
};

struct FBLArchiveEntry
{
	FString Id;
	FString Name;
	FString Project;
	FString Group;
	FString Folder;
	FString Kind;
	FString PreviewMesh;
	FString PreviewAsset;
	FString PreviewThumbnail;
	FString Status;
	FString AssetType;
	TArray<FString> Todos;
	TArray<FBLArchiveFileLink> Files;
};
