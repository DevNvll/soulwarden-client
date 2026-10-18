using System.IO;
using UnrealBuildTool;

public class SoulwardenClient : ModuleRules
{
	public SoulwardenClient(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });
		// The connection to the Soulwarden backend: sockets, HTTP, JSON, and the OpenSSL of the engine for DTLS 1.2
		// (docs/NETWORK.md).
		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "Networking", "HTTP", "Json", "JsonUtilities" });
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
		// The UI (docs/UI.md): the runtime of the module SwcUi (the canvases and Lua), drawn with Slate.
		PrivateDependencyModuleNames.AddRange(new string[] { "SwcUi", "Slate", "SlateCore", "RHI", "ImageCore" });
		// The folder Net/ of the connection ("Net/SwcLoginClient.h"), and the vendored wire codec and state reader of
		// the Soulwarden backend ("server/hub/wire.h", "support/state_decoder.h"; Net/Vendor/soulwarden/NOTICE.md).
		PrivateIncludePaths.Add(ModuleDirectory);
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Net", "Vendor", "soulwarden", "src"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Net", "Vendor", "soulwarden", "tests"));
	}
}
