#if WITH_DEV_AUTOMATION_TESTS

#include "ArchiveUiPolicy.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlackLionArchiveLargeCatalogPagingTest,
	"BlackLion.AssetArchive.Paging.LargeCatalogNoLossOrDuplicates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlackLionArchiveLargeCatalogPagingTest::RunTest(const FString& Parameters)
{
	constexpr int32 SyntheticAssetCount = 5000;
	TArray<FString> SourceIds;
	TMap<FString, int32> SourceIndexById;
	SourceIds.Reserve(SyntheticAssetCount);
	SourceIndexById.Reserve(SyntheticAssetCount);
	for (int32 Index = 0; Index < SyntheticAssetCount; ++Index)
	{
		const FString Id = FString::Printf(TEXT("synthetic_asset_%04d"), Index);
		SourceIds.Add(Id);
		SourceIndexById.Add(Id, Index);
	}

	TArray<int32> SeenCounts;
	SeenCounts.Init(0, SyntheticAssetCount);
	int32 TotalRenderedRows = 0;
	const int32 PageCount = BlackLionArchiveUiPolicy::GetPageCount(
		SyntheticAssetCount, BlackLionArchiveUiPolicy::AssetPageSize);
	TestEqual(TEXT("5000 assets produce the expected page count"), PageCount, 209);

	for (int32 PageIndex = 0; PageIndex < PageCount; ++PageIndex)
	{
		const TArray<FString> PageItems = BlackLionArchiveUiPolicy::GetPageItems(
			SourceIds, PageIndex, BlackLionArchiveUiPolicy::AssetPageSize);
		TestTrue(TEXT("A rendered page stays within the widget budget"), PageItems.Num() <= BlackLionArchiveUiPolicy::AssetPageSize);
		TotalRenderedRows += PageItems.Num();
		for (const FString& Id : PageItems)
		{
			const int32* SourceIndex = SourceIndexById.Find(Id);
			if (!SourceIndex)
			{
				AddError(FString::Printf(TEXT("Unknown ID returned by a page: %s"), *Id));
				continue;
			}
			++SeenCounts[*SourceIndex];
		}
	}

	TestEqual(TEXT("Every catalog ID is rendered once across all pages"), TotalRenderedRows, SyntheticAssetCount);
	for (int32 SeenCount : SeenCounts)
	{
		TestEqual(TEXT("No asset is duplicated or skipped"), SeenCount, 1);
	}
	TestEqual(TEXT("Empty catalogs have no pages"), BlackLionArchiveUiPolicy::GetPageCount(0, BlackLionArchiveUiPolicy::AssetPageSize), 0);
	TestEqual(TEXT("Out-of-range page indexes clamp safely"), BlackLionArchiveUiPolicy::ClampPageIndex(999, 25, 24), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlackLionArchiveAdaptivePreviewQualityTest,
	"BlackLion.AssetArchive.Preview.AdaptiveQualityRespectsAssetBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlackLionArchiveAdaptivePreviewQualityTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Small projects keep 768px thumbnails"), BlackLionArchiveUiPolicy::GetThumbnailMaxDimension(64), 768);
	TestEqual(TEXT("Medium projects reduce thumbnails to 512px"), BlackLionArchiveUiPolicy::GetThumbnailMaxDimension(65), 512);
	TestEqual(TEXT("Large projects use 384px thumbnails"), BlackLionArchiveUiPolicy::GetThumbnailMaxDimension(257), 384);
	TestEqual(TEXT("Single-LOD meshes stay on automatic LOD"), BlackLionArchiveUiPolicy::GetPreviewForcedLod(1000, 1), 0);
	TestEqual(TEXT("Medium projects request an available lower LOD"), BlackLionArchiveUiPolicy::GetPreviewForcedLod(129, 2), 2);
	TestEqual(TEXT("Large projects use the lowest available LOD"), BlackLionArchiveUiPolicy::GetPreviewForcedLod(513, 3), 3);
	TestEqual(TEXT("Large projects fall back when only two LODs exist"), BlackLionArchiveUiPolicy::GetPreviewForcedLod(513, 2), 2);
	TestFalse(TEXT("Very large catalogs disable preview shadows"), BlackLionArchiveUiPolicy::ShouldCastPreviewShadow(257));
	TestTrue(TEXT("Small catalogs retain preview shadows"), BlackLionArchiveUiPolicy::ShouldCastPreviewShadow(256));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
