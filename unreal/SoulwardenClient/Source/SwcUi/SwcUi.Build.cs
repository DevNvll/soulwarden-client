using System.IO;
using UnrealBuildTool;

/**
 * The UI runtime (docs/UI.md): the element model of the converted canvases with the LyShine rules, the draw list, and the
 * embedded Lua 5.1.5 (Source/ThirdParty/Lua51, an unchanged copy, MIT) that runs the original scripts of the game. The
 * code is plain C++ with no engine header (tools/test-ui.sh builds the same files); the game module draws the draw list
 * with Slate (Source/SoulwardenClient/UI).
 *
 * Lua is C++ here: a Lua error is a C++ exception (exceptions on). No unity build and no PCH: the Lua sources and the
 * engine headers must not meet in one translation unit (Lua has the macros G and gt, the engine has check and verify).
 */
public class SwcUi : ModuleRules
{
	public SwcUi(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		bEnableExceptions = true;
		CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Off;
		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Off;
		CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;
		PublicDependencyModuleNames.Add("Core");
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "ThirdParty", "Lua51", "lua-5.1.5", "src"));
	}
}
