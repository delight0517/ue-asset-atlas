#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ArchiveCatalogTypes.h"
#include "ArchiveCatalogSubsystem.generated.h"

UCLASS()
class BLACKLIONASSETARCHIVECORE_API UArchiveCatalogSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	const TArray<FBLArchiveEntry>& GetEntries() const { return Entries; }
	const FString& GetProjectId() const { return ProjectId; }
	const FString& GetLastError() const { return LastError; }
	bool ReloadCatalog(FString& OutError);
	const FBLArchiveEntry* FindEntry(const FString& Id) const;
	bool OpenLinkedFile(const FString& Id, const FString& FilePath, FString& OutError) const;
	bool IsRegisteredPreviewImage(const FString& Id, const FString& FilePath) const;
	bool SaveProjectCollection(const FString& CollectionProjectId, const TArray<FString>& AssetIds, FString& OutError) const;
	bool LoadProjectCollection(const FString& CollectionProjectId, TArray<FString>& OutAssetIds, FString& OutError) const;
	FString GetCatalogPath() const;

private:
	bool LoadCatalog(FString& OutError);
	TArray<FBLArchiveEntry> Entries;
	FString ProjectId;
	FString LastError;
};
