using System.IO;
using UnrealBuildTool;

/**
 * The global shaders of the client (stage E5 of docs/ENVIRONMENT.md): the shader folder of the project (Shaders/, the virtual
 * path /SwcShaders) and the passes that use it. A global shader type must be known before the engine starts, so this module
 * loads at PostConfigInit (SoulwardenClient.uproject); it has no UObject. The game module calls its passes from a scene view
 * extension.
 */
public class SwcShaders : ModuleRules
{
	public SwcShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "RenderCore", "RHI", "Engine" });
		PrivateDependencyModuleNames.Add("Projects");
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
	}
}
