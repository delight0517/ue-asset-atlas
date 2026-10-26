using UnrealBuildTool;

public class BlackLionAssetArchiveCore : ModuleRules
{
	public BlackLionAssetArchiveCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json", "Projects" });
	}
}
