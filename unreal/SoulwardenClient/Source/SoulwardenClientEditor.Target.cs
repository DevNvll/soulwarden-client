using UnrealBuildTool;

public class SoulwardenClientEditorTarget : TargetRules
{
	public SoulwardenClientEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("SoulwardenClient");
	}
}
