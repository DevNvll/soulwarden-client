using UnrealBuildTool;

/** Editor tools of the world track: helpers for the import, world build, and proof scripts (Python). */
public class SoulwardenClientEditor : ModuleRules
{
	public SoulwardenClientEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI", "ImageWrapper", "UnrealEd", "Landscape", "InterchangeCore", "InterchangeFactoryNodes" });
	}
}
