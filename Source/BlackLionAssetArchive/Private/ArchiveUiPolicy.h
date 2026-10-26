#pragma once

#include "CoreMinimal.h"

namespace BlackLionArchiveUiPolicy
{
	inline constexpr int32 AssetPageSize = 24;
	inline constexpr int32 FolderPageSize = 12;

	inline int32 GetPageCount(int32 ItemCount, int32 PageSize)
	{
		return ItemCount <= 0 || PageSize <= 0 ? 0 : FMath::DivideAndRoundUp(ItemCount, PageSize);
	}

	inline int32 ClampPageIndex(int32 PageIndex, int32 ItemCount, int32 PageSize)
	{
		const int32 PageCount = GetPageCount(ItemCount, PageSize);
		return PageCount == 0 ? 0 : FMath::Clamp(PageIndex, 0, PageCount - 1);
	}

	inline TArray<FString> GetPageItems(const TArray<FString>& Items, int32 PageIndex, int32 PageSize)
	{
		TArray<FString> Result;
		if (PageSize <= 0 || Items.IsEmpty()) return Result;

		const int32 SafePageIndex = ClampPageIndex(PageIndex, Items.Num(), PageSize);
		const int32 FirstIndex = SafePageIndex * PageSize;
		const int32 Count = FMath::Min(PageSize, Items.Num() - FirstIndex);
		Result.Reserve(Count);
		for (int32 Index = FirstIndex; Index < FirstIndex + Count; ++Index)
		{
			Result.Add(Items[Index]);
		}
		return Result;
	}

	inline int32 GetThumbnailMaxDimension(int32 ProjectAssetCount)
	{
		if (ProjectAssetCount > 256) return 384;
		if (ProjectAssetCount > 64) return 512;
		return 768;
	}

	// SetForcedLodModel uses 0 for automatic selection and 1-based values for
	// explicit LODs. Never ask a mesh for an LOD that it does not contain.
	inline int32 GetPreviewForcedLod(int32 ProjectAssetCount, int32 MeshLodCount)
	{
		if (MeshLodCount <= 1) return 0;
		if (ProjectAssetCount > 512 && MeshLodCount >= 3) return 3;
		if (ProjectAssetCount > 128 && MeshLodCount >= 2) return 2;
		return 0;
	}

	inline bool ShouldCastPreviewShadow(int32 ProjectAssetCount)
	{
		return ProjectAssetCount <= 256;
	}
}
