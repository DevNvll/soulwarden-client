#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SwcMaterialScale.generated.h"

class UMaterialParameterCollection;

/**
 * The global values of the world materials (the materials track, docs/MATERIALS.md): EmissiveScale of
 * /SwcGameContent/Base/MPC_SwcMaterials. The emission of the surface parents is in the engine units of the source (the
 * Emittance RGB x A x 0.1 of GetMaterial_Emittance: kcd/m2 to units of 10,000 cd/m2, RENDERER_LIGHT_UNIT_SCALE).
 * EmissiveScale = 10,000 cd/m2 x the look scale of the local lights (the console variable swc.LightScale of the
 * environment work, default 3), so that a fixture and its light stay in balance. A change of swc.LightScale applies
 * at the next frame.
 *
 * The effects (AST-207): /SwcGameContent/Base/MPC_SwcEffects of M_SwcBeam (the GeometryBeam fog sheets and cards):
 * SunColorSource = the sun color of the source in its engine units (the main directional light: its color x its
 * intensity / pi, the luminance of the water work, / SourceUnitNits) and FogColorSource = the inscattering color of the
 * height fog (M_SwcBeam normalizes it). Four times in a second.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcMaterialScale : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** One engine unit of the source in cd/m2 (RENDERER_LIGHT_UNIT_SCALE of the source renderer). */
	static constexpr float SourceUnitNits = 10000.0f;
	/** The look scale when swc.LightScale does not exist (its default in the environment work). */
	static constexpr float DefaultLightScale = 1.0f;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	float LightScale() const;

	void ApplyEffects();

	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> Collection;

	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> Effects;

	float Applied = -1.0f;
	double NextEffects = 0.0;
};
