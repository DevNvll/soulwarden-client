using UnrealBuildTool;

public class SoulwardenClientTarget : TargetRules
{
	public SoulwardenClientTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SoulwardenClient");
	}
}
