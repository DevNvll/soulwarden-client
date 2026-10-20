using System.IO;
using UnrealBuildTool;

/** Editor tools of the world track: helpers for the import, world build, and proof scripts (Python). */
public class SoulwardenClientEditor : ModuleRules
{
	public SoulwardenClientEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI", "ImageWrapper", "UnrealEd", "Landscape", "InterchangeCore", "InterchangeFactoryNodes", "MeshDescription", "StaticMeshDescription", "PhysicsCore", "Chaos" });
		// The particle systems (AST-210, SwcParticleBuilder.cpp): the stateless emitters of Niagara. Their headers are in the
		// Internal folder of the plugin, which a project module does not get by default.
		PrivateDependencyModuleNames.AddRange(new string[] { "Niagara", "NiagaraCore", "NiagaraShader", "Json" });
		string Niagara = Path.Combine(EngineDirectory, "Plugins", "FX", "Niagara", "Source");
		PrivateIncludePaths.Add(Path.Combine(Niagara, "Niagara", "Internal"));
		PrivateIncludePaths.Add(Path.Combine(Niagara, "NiagaraShader", "Internal"));
	}
}
