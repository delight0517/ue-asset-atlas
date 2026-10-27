#include "Modules/ModuleManager.h"

#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/OutputDeviceNull.h"

#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogBlackLionArchiveHotReload, Log, All);

class FBlackLionAssetArchiveEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlackLionAssetArchive"));
		if (!Plugin.IsValid())
		{
			UE_LOG(LogBlackLionArchiveHotReload, Error, TEXT("Hot reload could not find the BlackLionAssetArchive plugin descriptor."));
			return;
		}

		PluginBaseDirectory = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
		SourceDirectory = FPaths::Combine(PluginBaseDirectory, TEXT("Source"));
		PluginDescriptor = FPaths::Combine(PluginBaseDirectory, TEXT("BlackLionAssetArchive.uplugin"));
		ObservedFiles = ScanSourceFiles();

		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FBlackLionAssetArchiveEditorModule::Tick),
			ScanIntervalSeconds);
		RegisterConsoleCommands();
		UE_LOG(LogBlackLionArchiveHotReload, Display, TEXT("Hot reload is idle. Enable it for this editor session with BlackLionAssetArchive.HotReload.Enable."));
	}

	virtual void ShutdownModule() override
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		UnregisterConsoleCommands();
#if PLATFORM_WINDOWS
		if (ILiveCodingModule* LiveCoding = GetLiveCodingModule(false); LiveCoding && bPatchDelegateBound)
		{
			LiveCoding->GetOnPatchCompleteDelegate().RemoveAll(this);
		}
#endif
	}

private:
	static constexpr float ScanIntervalSeconds = 0.5f;
	static constexpr double StableWriteSeconds = 1.25;

	FString PluginBaseDirectory;
	FString SourceDirectory;
	FString PluginDescriptor;
	TMap<FString, FString> ObservedFiles;
	TMap<FString, FString> PendingFiles;
	FTSTicker::FDelegateHandle TickerHandle;
	double PendingSince = 0.0;
	bool bHasPendingChange = false;
	bool bPendingCppChange = false;
	bool bPendingHeaderChange = false;
	bool bPendingBuildOrDescriptorChange = false;
	bool bAutoCompileEnabled = false;
	bool bHeaderCompileRequired = false;
	bool bRestartRequired = false;
	bool bCompilePending = false;
	bool bPatchApplied = false;
	bool bManualCompileInFlight = false;
	bool bPatchDelegateBound = false;
	IConsoleObject* EnableCommand = nullptr;
	IConsoleObject* DisableCommand = nullptr;
	IConsoleObject* CompileCommand = nullptr;
	IConsoleObject* StatusCommand = nullptr;

	TMap<FString, FString> ScanSourceFiles() const
	{
		TMap<FString, FString> Result;
		if (!FPaths::DirectoryExists(SourceDirectory)) return Result;

		TArray<FString> Files;
		IFileManager& FileManager = IFileManager::Get();
		FileManager.FindFilesRecursive(Files, *SourceDirectory, TEXT("*.cpp"), true, false);
		FileManager.FindFilesRecursive(Files, *SourceDirectory, TEXT("*.h"), true, false);
		FileManager.FindFilesRecursive(Files, *SourceDirectory, TEXT("*.cs"), true, false);
		if (FPaths::FileExists(PluginDescriptor)) Files.Add(PluginDescriptor);

		for (FString& File : Files)
		{
			FPaths::NormalizeFilename(File);
			File = FPaths::ConvertRelativePathToFull(File);
			const int64 Size = FileManager.FileSize(*File);
			const int64 ModifiedTicks = FileManager.GetTimeStamp(*File).GetTicks();
			Result.Add(File, FString::Printf(TEXT("%lld:%lld"), Size, ModifiedTicks));
		}
		return Result;
	}

	static bool HasChanged(const TMap<FString, FString>& Previous, const TMap<FString, FString>& Current)
	{
		if (Previous.Num() != Current.Num()) return true;
		for (const TPair<FString, FString>& File : Current)
		{
			const FString* PreviousStamp = Previous.Find(File.Key);
			if (!PreviousStamp || *PreviousStamp != File.Value) return true;
		}
		return false;
	}

	static void ClassifyChanges(const TMap<FString, FString>& Previous, const TMap<FString, FString>& Current,
		bool& bOutCpp, bool& bOutHeader, bool& bOutBuildOrDescriptor)
	{
		bOutCpp = false;
		bOutHeader = false;
		bOutBuildOrDescriptor = false;
		auto Classify = [&bOutCpp, &bOutHeader, &bOutBuildOrDescriptor](const FString& File)
		{
			const FString Extension = FPaths::GetExtension(File, false).ToLower();
			if (Extension == TEXT("cpp")) bOutCpp = true;
			else if (Extension == TEXT("h")) bOutHeader = true;
			else if (Extension == TEXT("cs") || Extension == TEXT("uplugin")) bOutBuildOrDescriptor = true;
		};

		for (const TPair<FString, FString>& File : Current)
		{
			const FString* OldStamp = Previous.Find(File.Key);
			if (!OldStamp || *OldStamp != File.Value) Classify(File.Key);
		}
		for (const TPair<FString, FString>& File : Previous)
		{
			if (!Current.Contains(File.Key)) Classify(File.Key);
		}
	}

	bool Tick(float DeltaSeconds)
	{
		const double Now = FPlatformTime::Seconds();
		const TMap<FString, FString> CurrentFiles = ScanSourceFiles();
		if (HasChanged(ObservedFiles, CurrentFiles))
		{
			bool bCpp = false;
			bool bHeader = false;
			bool bBuildOrDescriptor = false;
			ClassifyChanges(ObservedFiles, CurrentFiles, bCpp, bHeader, bBuildOrDescriptor);
			ObservedFiles = CurrentFiles;
			PendingFiles = CurrentFiles;
			PendingSince = Now;
			bHasPendingChange = true;
			bPendingCppChange |= bCpp;
			bPendingHeaderChange |= bHeader;
			bPendingBuildOrDescriptorChange |= bBuildOrDescriptor;
		}

		if (bHasPendingChange && !HasChanged(PendingFiles, CurrentFiles) && Now - PendingSince >= StableWriteSeconds)
		{
			bHasPendingChange = false;
			PendingFiles.Reset();
			if (bPendingBuildOrDescriptorChange)
			{
				bRestartRequired = true;
				UE_LOG(LogBlackLionArchiveHotReload, Warning, TEXT("Build.cs or .uplugin changed. Live patching is held; regenerate project files/build and restart this editor session."));
			}
			else if (bPendingHeaderChange)
			{
				bHeaderCompileRequired = true;
				UE_LOG(LogBlackLionArchiveHotReload, Warning, TEXT("A C++ header changed. Auto compile is held so reflected/UHT changes are not applied blindly. Review the change, then run BlackLionAssetArchive.HotReload.Compile or restart the editor."));
			}
			else if (bPendingCppChange)
			{
				if (bAutoCompileEnabled) RequestCompile(false);
				else UE_LOG(LogBlackLionArchiveHotReload, Display, TEXT("C++ source changed; auto compile is disabled. Run BlackLionAssetArchive.HotReload.Compile or enable it for this session."));
			}
			bPendingCppChange = false;
			bPendingHeaderChange = false;
			bPendingBuildOrDescriptorChange = false;
		}

		#if PLATFORM_WINDOWS
		if (bCompilePending)
		{
			if (ILiveCodingModule* LiveCoding = GetLiveCodingModule(false); LiveCoding && !LiveCoding->IsCompiling())
			{
				bCompilePending = false;
				if (!bPatchApplied)
				{
					UE_LOG(LogBlackLionArchiveHotReload, Display, TEXT("Live Coding finished without a patch notification. Check the Live Coding console for compile diagnostics."));
				}
				bPatchApplied = false;
				bManualCompileInFlight = false;
			}
		}
		#endif

		return true;
	}

	void RegisterConsoleCommands()
	{
		IConsoleManager& Console = IConsoleManager::Get();
		EnableCommand = Console.RegisterConsoleCommand(
			TEXT("BlackLionAssetArchive.HotReload.Enable"), TEXT("Enable debounced .cpp Live Coding for this editor session."),
			FConsoleCommandDelegate::CreateRaw(this, &FBlackLionAssetArchiveEditorModule::EnableAutoCompile), ECVF_Default);
		DisableCommand = Console.RegisterConsoleCommand(
			TEXT("BlackLionAssetArchive.HotReload.Disable"), TEXT("Disable automatic source compilation for this editor session."),
			FConsoleCommandDelegate::CreateRaw(this, &FBlackLionAssetArchiveEditorModule::DisableAutoCompile), ECVF_Default);
		CompileCommand = Console.RegisterConsoleCommand(
			TEXT("BlackLionAssetArchive.HotReload.Compile"), TEXT("Request one Live Coding compile for this editor session."),
			FConsoleCommandDelegate::CreateRaw(this, &FBlackLionAssetArchiveEditorModule::CompileManually), ECVF_Default);
		StatusCommand = Console.RegisterConsoleCommand(
			TEXT("BlackLionAssetArchive.HotReload.Status"), TEXT("Report the local hot reload state."),
			FConsoleCommandDelegate::CreateRaw(this, &FBlackLionAssetArchiveEditorModule::ReportStatus), ECVF_Default);
	}

	void UnregisterConsoleCommands()
	{
		IConsoleManager& Console = IConsoleManager::Get();
		for (IConsoleObject* Command : { EnableCommand, DisableCommand, CompileCommand, StatusCommand })
		{
			if (Command) Console.UnregisterConsoleObject(Command, false);
		}
	}

	void EnableAutoCompile()
	{
		if (bRestartRequired)
		{
			UE_LOG(LogBlackLionArchiveHotReload, Error, TEXT("Restart required: plugin descriptor or build rules changed."));
			return;
		}
		if (!EnsureLiveCodingEnabled()) return;
		bAutoCompileEnabled = true;
		UE_LOG(LogBlackLionArchiveHotReload, Display, TEXT("Auto Live Coding enabled for .cpp saves in %s. Header/build-rule changes remain guarded."), *SourceDirectory);
	}

	void DisableAutoCompile()
	{
		bAutoCompileEnabled = false;
		UE_LOG(LogBlackLionArchiveHotReload, Display, TEXT("Automatic compilation disabled for this editor session. The Live Coding session itself remains unchanged."));
	}

	void CompileManually()
	{
		if (bRestartRequired)
		{
			UE_LOG(LogBlackLionArchiveHotReload, Error, TEXT("Restart required: plugin descriptor or build rules changed."));
			return;
		}
		if (bHeaderCompileRequired)
		{
			UE_LOG(LogBlackLionArchiveHotReload, Warning, TEXT("Manual header compile requested. Reflected type changes still require a compatible Live Coding reinstancing setup; restart if the patch cannot safely apply."));
		}
		RequestCompile(true);
	}

	bool EnsureLiveCodingEnabled()
	{
		#if PLATFORM_WINDOWS
		ILiveCodingModule* LiveCoding = GetLiveCodingModule(true);
		if (!LiveCoding) return false;
		if (!LiveCoding->IsEnabledForSession())
		{
			if (!LiveCoding->CanEnableForSession())
			{
				UE_LOG(LogBlackLionArchiveHotReload, Error, TEXT("Live Coding cannot be enabled in this session: %s"), *LiveCoding->GetEnableErrorText().ToString());
				return false;
			}
			LiveCoding->EnableForSession(true);
		}
		if (!LiveCoding->HasStarted() || !LiveCoding->IsEnabledForSession())
		{
			UE_LOG(LogBlackLionArchiveHotReload, Error, TEXT("The Live Coding console did not start. Check Unreal Editor Live Coding settings and the Live Coding console."));
			return false;
		}
		return true;
		#else
		UE_LOG(LogBlackLionArchiveHotReload, Error, TEXT("This editor hot reload module currently supports Windows Live Coding only."));
		return false;
		#endif
	}

	void RequestCompile(bool bManual)
	{
		#if PLATFORM_WINDOWS
		if (!EnsureLiveCodingEnabled()) return;
		ILiveCodingModule* LiveCoding = GetLiveCodingModule(false);
		if (!LiveCoding) return;
		if (LiveCoding->IsCompiling())
		{
			UE_LOG(LogBlackLionArchiveHotReload, Display, TEXT("A Live Coding compile is already active; this request is not queued."));
			return;
		}

		ELiveCodingCompileResult Result = ELiveCodingCompileResult::Failure;
		if (!LiveCoding->Compile(ELiveCodingCompileFlags::None, &Result))
		{
			UE_LOG(LogBlackLionArchiveHotReload, Error, TEXT("Live Coding did not start (result %d). Check the Live Coding console."), static_cast<int32>(Result));
			return;
		}
		if (Result == ELiveCodingCompileResult::InProgress)
		{
			bCompilePending = true;
			bPatchApplied = false;
			bManualCompileInFlight = bManual;
			if (!bPatchDelegateBound)
			{
				LiveCoding->GetOnPatchCompleteDelegate().AddRaw(this, &FBlackLionAssetArchiveEditorModule::HandlePatchComplete);
				bPatchDelegateBound = true;
			}
			UE_LOG(LogBlackLionArchiveHotReload, Display, TEXT("Live Coding compile started. The editor stays open; diagnostics are in the Live Coding console."));
		}
		else if (Result == ELiveCodingCompileResult::NoChanges)
		{
			bHeaderCompileRequired = false;
			UE_LOG(LogBlackLionArchiveHotReload, Display, TEXT("Live Coding found no code changes."));
		}
		else
		{
			UE_LOG(LogBlackLionArchiveHotReload, Display, TEXT("Live Coding returned result %d."), static_cast<int32>(Result));
		}
		#else
		UE_LOG(LogBlackLionArchiveHotReload, Error, TEXT("This editor hot reload module currently supports Windows Live Coding only."));
		#endif
	}

	void HandlePatchComplete()
	{
		bPatchApplied = true;
		bHeaderCompileRequired = false;
		UE_LOG(LogBlackLionArchiveHotReload, Display, TEXT("Live Coding patch applied to this editor process."));
	}

	void ReportStatus()
	{
		const TCHAR* AutoState = bAutoCompileEnabled ? TEXT("ON") : TEXT("OFF");
		const TCHAR* HeaderState = bHeaderCompileRequired ? TEXT("manual compile required") : TEXT("clear");
		const TCHAR* RestartState = bRestartRequired ? TEXT("restart required") : TEXT("clear");
		UE_LOG(LogBlackLionArchiveHotReload, Display, TEXT("Auto .cpp compile: %s | header gate: %s | build/descriptor gate: %s | plugin source: %s"),
			AutoState, HeaderState, RestartState, *SourceDirectory);
	}

#if PLATFORM_WINDOWS
	ILiveCodingModule* GetLiveCodingModule(bool bLoad) const
	{
		return bLoad
			? FModuleManager::Get().LoadModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME)
			: FModuleManager::Get().GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	}
#endif
};

IMPLEMENT_MODULE(FBlackLionAssetArchiveEditorModule, BlackLionAssetArchiveEditor)
