#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SwcLandmarks.generated.h"

class UMaterialParameterCollection;
class UPrimitiveComponent;
class USceneComponent;

/**
 * The landmarks of the world (AST-209, docs/WORLD.md "The landmarks"): far meshes as one persistent actor, outside the cells of the
 * World Partition (they stay when the cells load or unload). Now the sky maelstrom of the Shattered Mountain: the converter writes
 * SWC_CONTENT/landmarks/maelstrom.json (converter/swc_convert/landmarks.py), tools/import-landmarks.sh imports its meshes and
 * materials into /SwcGameContent/Landmarks/Maelstrom. Six static mesh components (no Nanite, no shadow, the draw range of the source)
 * under two pivots: CenterPivot_Top stays still; CenterPivot_Bottom turns about its local Z by the script rule of the source (each
 * 0.05 s one step of +0.143239 degrees in Unreal; an accumulator that resets to 0, no delta time; research/maelstrom.md).
 * The back light of the translucent layers (%TRANSMITTANCE of the source Illum) needs the light of the hour: each frame the subsystem
 * sets SunDirection (toward the light) and SunColor (lux: the source sun on the ground, or the Unreal light) in MPC_SwcLandmarks.
 * -SwcNoLandmarks turns it off; swc.Landmarks 0 hides it in a run (2: only the fog cards of the column are hidden).
 */
UCLASS()
class SOULWARDENCLIENT_API USwcLandmarks : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void Build();
	void UpdateLight();

	TWeakObjectPtr<AActor> Owner;
	TWeakObjectPtr<USceneComponent> Turning;
	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> Collection;
	double StepSeconds = 0.05;
	double StepDegrees = 0.0;
	double Accumulator = 0.0;
	int32 ShownMode = 1;
	/** The fog cards of the column (swc.Landmarks 2 hides them alone). */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Cards;
};
