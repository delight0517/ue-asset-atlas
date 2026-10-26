using UnrealBuildTool;

public class BlackLionAssetArchive : ModuleRules
{
	public BlackLionAssetArchive(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "UMG", "Slate", "SlateCore", "ImageWrapper", "BlackLionAssetArchiveCore" });
	}
}
