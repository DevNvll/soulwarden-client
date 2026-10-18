using UnrealBuildTool;

public class SoulwardenClient : ModuleRules
{
	public SoulwardenClient(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });
		// The connection to the Soulwarden backend: sockets, HTTP, JSON (docs/NETWORK.md).
		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "Networking", "HTTP", "Json", "JsonUtilities" });
		// The folder Net/ of the connection: "Net/SwcLoginClient.h".
		PrivateIncludePaths.Add(ModuleDirectory);
	}
}
