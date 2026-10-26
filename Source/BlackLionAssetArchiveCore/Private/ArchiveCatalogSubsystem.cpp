#include "ArchiveCatalogSubsystem.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

namespace
{
	const FString SharedPluginRoot = TEXT("BlackLionAssetArchive");

	bool IsAllowedExtension(const FString& Path)
	{
		const FString Ext = FPaths::GetExtension(Path, true).ToLower();
		return Ext == TEXT(".blend") || Ext == TEXT(".spp") || Ext == TEXT(".fbx") || Ext == TEXT(".png") || Ext == TEXT(".jpg") || Ext == TEXT(".jpeg") || Ext == TEXT(".uproject") || Ext == TEXT(".umap");
	}

	bool IsAllowedPath(const FString& Input, FString& OutFullPath)
	{
		OutFullPath = FPaths::ConvertRelativePathToFull(Input);
		FPaths::NormalizeFilename(OutFullPath);
		if (!IsAllowedExtension(OutFullPath)) return false;
		FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FPaths::NormalizeDirectoryName(ProjectRoot);
		ProjectRoot += TEXT("/");
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(SharedPluginRoot);
		FString SharedRoot = Plugin.IsValid() ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()) : FString();
		if (!SharedRoot.IsEmpty())
		{
			FPaths::NormalizeDirectoryName(SharedRoot);
			SharedRoot += TEXT("/");
		}
		if (OutFullPath.StartsWith(ProjectRoot, ESearchCase::IgnoreCase)
			|| (!SharedRoot.IsEmpty() && OutFullPath.StartsWith(SharedRoot, ESearchCase::IgnoreCase)))
		{
			return true;
		}

		FString IdentityRaw;
		const FString IdentityPath = FPaths::ProjectDir() / TEXT("AssetArchive_Inbox/project.json");
		TSharedPtr<FJsonObject> Identity;
		if (!FFileHelper::LoadFileToString(IdentityRaw, *IdentityPath)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(IdentityRaw), Identity)
			|| !Identity.IsValid())
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* ApprovedRoots = nullptr;
		if (!Identity->TryGetArrayField(TEXT("approved_source_roots"), ApprovedRoots) || !ApprovedRoots)
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& RootValue : *ApprovedRoots)
		{
			if (!RootValue.IsValid() || RootValue->Type != EJson::String) continue;
			FString ApprovedRoot = FPaths::ConvertRelativePathToFull(RootValue->AsString());
			FPaths::NormalizeDirectoryName(ApprovedRoot);
			ApprovedRoot += TEXT("/");
			if (OutFullPath.StartsWith(ApprovedRoot, ESearchCase::IgnoreCase)) return true;
		}
		return false;
	}
}

void UArchiveCatalogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Error;
	LoadCatalog(Error);
	LastError = Error;
}

FString UArchiveCatalogSubsystem::GetCatalogPath() const
{
	return FPaths::ProjectSavedDir() / TEXT("BlackLionAssetArchive/catalog.json");
}

const FBLArchiveEntry* UArchiveCatalogSubsystem::FindEntry(const FString& Id) const
{
	return Entries.FindByPredicate([&Id](const FBLArchiveEntry& Entry) { return Entry.Id == Id; });
}

bool UArchiveCatalogSubsystem::ReloadCatalog(FString& OutError)
{
	const bool bLoaded = LoadCatalog(OutError);
	LastError = bLoaded ? FString() : OutError;
	return bLoaded;
}

bool UArchiveCatalogSubsystem::LoadCatalog(FString& OutError)
{
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *GetCatalogPath()))
	{
		OutError = TEXT("Project catalog not found. Run the local archive sync once to create it.");
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("Project catalog JSON is invalid; the previous in-memory catalog was preserved.");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Root->TryGetArrayField(TEXT("assets"), Items) || !Items)
	{
		OutError = TEXT("Project catalog has no assets array; the previous in-memory catalog was preserved.");
		return false;
	}
	FString NewProjectId;
	Root->TryGetStringField(TEXT("project_id"), NewProjectId);
	if (NewProjectId.IsEmpty())
	{
		OutError = TEXT("Project catalog must declare project_id; cross-project loading was refused.");
		return false;
	}
	FString IdentityRaw;
	const FString IdentityPath = FPaths::ProjectDir() / TEXT("AssetArchive_Inbox/project.json");
	TSharedPtr<FJsonObject> Identity;
	if (!FFileHelper::LoadFileToString(IdentityRaw, *IdentityPath)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(IdentityRaw), Identity)
		|| !Identity.IsValid())
	{
		OutError = TEXT("Project identity file is missing or invalid; archive loading was refused.");
		return false;
	}
	FString ExpectedProjectId;
	Identity->TryGetStringField(TEXT("project_id"), ExpectedProjectId);
	if (ExpectedProjectId.IsEmpty() || ExpectedProjectId != NewProjectId)
	{
		OutError = TEXT("Catalog project_id does not match this Unreal project's identity; cross-project loading was refused.");
		return false;
	}
	TArray<FBLArchiveEntry> NewEntries;
	for (const TSharedPtr<FJsonValue>& Value : *Items)
	{
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid()) continue;
		FBLArchiveEntry Entry;
		Obj->TryGetStringField(TEXT("id"), Entry.Id);
		Obj->TryGetStringField(TEXT("name"), Entry.Name);
		Obj->TryGetStringField(TEXT("project"), Entry.Project);
		Obj->TryGetStringField(TEXT("group"), Entry.Group);
		Obj->TryGetStringField(TEXT("folder"), Entry.Folder);
		Obj->TryGetStringField(TEXT("kind"), Entry.Kind);
		Obj->TryGetStringField(TEXT("preview_mesh"), Entry.PreviewMesh);
		Obj->TryGetStringField(TEXT("preview_asset"), Entry.PreviewAsset);
		Obj->TryGetStringField(TEXT("preview_thumbnail"), Entry.PreviewThumbnail);
		Obj->TryGetStringField(TEXT("status"), Entry.Status);
		Obj->TryGetStringField(TEXT("asset_type"), Entry.AssetType);
		const TArray<TSharedPtr<FJsonValue>>* TodoValues = nullptr;
		if (Obj->TryGetArrayField(TEXT("todos"), TodoValues) && TodoValues)
		{
			for (const TSharedPtr<FJsonValue>& TodoValue : *TodoValues)
			{
				const FString Todo = TodoValue->AsString().TrimStartAndEnd();
				if (!Todo.IsEmpty()) Entry.Todos.AddUnique(Todo);
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* FileValues = nullptr;
		if (Obj->TryGetArrayField(TEXT("files"), FileValues) && FileValues)
		{
			for (const TSharedPtr<FJsonValue>& FileValue : *FileValues)
			{
				const TSharedPtr<FJsonObject> FileObj = FileValue->AsObject();
				if (!FileObj.IsValid()) continue;
				FBLArchiveFileLink Link;
				FileObj->TryGetStringField(TEXT("label"), Link.Label);
				FileObj->TryGetStringField(TEXT("path"), Link.Path);
				Entry.Files.Add(MoveTemp(Link));
			}
		}
		if (!Entry.Id.IsEmpty() && !Entry.Name.IsEmpty() && Entry.Project == NewProjectId) NewEntries.Add(MoveTemp(Entry));
	}
	ProjectId = MoveTemp(NewProjectId);
	Entries = MoveTemp(NewEntries);
	OutError.Reset();
	return true;
}

bool UArchiveCatalogSubsystem::OpenLinkedFile(const FString& Id, const FString& FilePath, FString& OutError) const
{
	const FBLArchiveEntry* Entry = FindEntry(Id);
	if (!Entry || !Entry->Files.ContainsByPredicate([&FilePath](const FBLArchiveFileLink& Link) { return Link.Path == FilePath; }))
	{
		OutError = TEXT("The file is not a link registered for this archive item.");
		return false;
	}
	FString FullPath;
	if (!IsAllowedPath(FilePath, FullPath) || !FPaths::FileExists(FullPath))
	{
		OutError = TEXT("The registered path is missing or outside the archive's allowed local roots.");
		return false;
	}
	FPlatformProcess::LaunchFileInDefaultExternalApplication(*FullPath);
	return true;
}

bool UArchiveCatalogSubsystem::IsRegisteredPreviewImage(const FString& Id, const FString& FilePath) const
{
	const FBLArchiveEntry* Entry = FindEntry(Id);
	if (!Entry || Entry->PreviewThumbnail != FilePath || !Entry->Files.ContainsByPredicate([&FilePath](const FBLArchiveFileLink& Link) { return Link.Path == FilePath; })) return false;
	FString FullPath;
	const FString Ext = FPaths::GetExtension(FilePath, true).ToLower();
	return (Ext == TEXT(".png") || Ext == TEXT(".jpg") || Ext == TEXT(".jpeg")) && IsAllowedPath(FilePath, FullPath) && FPaths::FileExists(FullPath);
}

bool UArchiveCatalogSubsystem::SaveProjectCollection(const FString& CollectionProjectId, const TArray<FString>& AssetIds, FString& OutError) const
{
	FString SafeId;
	for (TCHAR C : CollectionProjectId) if (FChar::IsAlnum(C) || C == TEXT('-') || C == TEXT('_')) SafeId.AppendChar(C);
	if (SafeId.IsEmpty()) { OutError = TEXT("Project ID has no safe filename characters."); return false; }
	if (SafeId != this->ProjectId) { OutError = TEXT("Collection project_id must match the currently loaded project catalog."); return false; }
	TArray<TSharedPtr<FJsonValue>> IdValues;
	for (const FString& Id : AssetIds)
	{
		const FBLArchiveEntry* Entry = FindEntry(Id);
		if (Entry && Entry->Project == this->ProjectId) IdValues.Add(MakeShared<FJsonValueString>(Id));
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), TEXT("blacklion.asset-archive.collection.v1"));
	Root->SetStringField(TEXT("project_id"), SafeId);
	Root->SetArrayField(TEXT("asset_ids"), IdValues);
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("BlackLionAssetArchive/Collections");
	IFileManager::Get().MakeDirectory(*Directory, true);
	if (!FFileHelper::SaveStringToFile(Output, *(Directory / (SafeId + TEXT(".json"))))) { OutError = TEXT("Collection file could not be written."); return false; }
	return true;
}

bool UArchiveCatalogSubsystem::LoadProjectCollection(const FString& CollectionProjectId, TArray<FString>& OutAssetIds, FString& OutError) const
{
	FString SafeId;
	for (TCHAR C : CollectionProjectId) if (FChar::IsAlnum(C) || C == TEXT('-') || C == TEXT('_')) SafeId.AppendChar(C);
	if (SafeId.IsEmpty()) { OutError = TEXT("Project ID has no safe filename characters."); return false; }
	if (SafeId != this->ProjectId) { OutError = TEXT("Collection project_id must match the currently loaded project catalog."); return false; }
	FString Raw;
	const FString Path = FPaths::ProjectSavedDir() / TEXT("BlackLionAssetArchive/Collections") / (SafeId + TEXT(".json"));
	if (!FFileHelper::LoadFileToString(Raw, *Path)) { OutError = TEXT("No saved collection exists for this project yet."); return false; }
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) { OutError = TEXT("Collection JSON is invalid."); return false; }
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Root->TryGetArrayField(TEXT("asset_ids"), Values) || !Values) { OutError = TEXT("Collection has no asset_ids list."); return false; }
	OutAssetIds.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		const FString Id = Value->AsString();
		const FBLArchiveEntry* Entry = FindEntry(Id);
		if (Entry && Entry->Project == this->ProjectId) OutAssetIds.Add(Id);
	}
	return true;
}
