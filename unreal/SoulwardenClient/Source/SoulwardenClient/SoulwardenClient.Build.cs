using UnrealBuildTool;

public class SoulwardenClient : ModuleRules
{
	public SoulwardenClient(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });
		// The connection to the Soulwarden backend (stage P3): sockets, HTTP, JSON.
		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "Networking", "HTTP", "Json", "JsonUtilities" });
	}
}
