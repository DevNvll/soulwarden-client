#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SwcParticleEffects.generated.h"

class FJsonObject;
class FJsonValue;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;
class USwcRibbonComponent;

/** The values of one effect placement (the ParticleComponent settings of the data, docs/PARTICLES.md "The placement"). */
struct SOULWARDENCLIENT_API FSwcEffectSettings
{
	FLinearColor Tint = FLinearColor::White;   // Color
	float AlphaScale = 1.0f;                   // Alpha Scale
	float SizeScale = 1.0f;                    // GlobalSizeScale (the scale of the whole effect, docs/PARTICLES.md)
	float TimeScale = 1.0f;                    // Time Scale
	float ViewDistanceMultiplier = 1.0f;       // View Distance Multiplier
};

/**
 * The particle effects of the game (AST-210, docs/PARTICLES.md): one effect interface for every caller (the menu scene, the
 * world placements, later the abilities and the entity scripts), and the world placements near the view.
 *
 * The systems: SWC_CONTENT/particles/systems.json lists the converted effect roots and their Niagara systems
 * (/SwcGameContent/Particles/Systems/NS_*, tools/import-particles.sh). An effect key that has no system is skipped (the support
 * report of the converter lists why).
 * The world placements: SWC_CONTENT/particles/placements/REGION.json (tools/convert-particles.sh placements), read off the game
 * thread for the regions near the view. A placement gets a pooled Niagara component when the view is within the view distance
 * of its effect (the outer bound of its LOD bands, x View Distance Multiplier); the distance bands of each emitter are in the
 * system (the distance culling of each emitter). A loading screen waits for the effects within SwcLoadingGate::NearCm of the
 * place (the check "effects"): each has its component and has run for WarmSeconds, so that no flame grows after the screen.
 * On by default: -SwcNoEffects or SWC_EFFECTS=0 turns the world placements off; swc.Effects.Show 0 hides all effects.
 * -SwcEffectTest=KEY,COUNT places COUNT copies of an effect in a grid in front of the view (the frame cost check).
 */
UCLASS()
class SOULWARDENCLIENT_API USwcEffectSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The effect interface. Start: a pooled component of the effect at Transform (world, or relative to Parent), active.
	 *  @return The component, or null when the effect has no system. */
	UNiagaraComponent* Start(const FString& Effect, const FTransform& Transform, const FSwcEffectSettings& Settings,
	                         USceneComponent* Parent = nullptr);
	/**
	 * AST-230: starts an effect at a socket of a component (a bone of a skeletal mesh; NAME_None: the component). Relative is in the frame
	 * of the socket (its scale is the placement scale). bFollow (EmitterFollows): the effect stays attached; else it starts at the world
	 * place of the socket and stays there. @return The component, or null when the effect has no system.
	 */
	UNiagaraComponent* StartAttached(const FString& Effect, USceneComponent* Parent, FName Socket, const FTransform& Relative, bool bFollow,
	                                 const FSwcEffectSettings& Settings);
	/** Stops the emission; bKill also removes the living particles. The component goes back to the pool. */
	void Stop(UNiagaraComponent* Component, bool bKill = false);
	/** AST-230 (KeepEmitterActive): the component goes back to the pool and stays active until its system ends (at most KeepSeconds). */
	void Keep(UNiagaraComponent* Component);
	/** Starts the effect again from its beginning. */
	static void Restart(UNiagaraComponent* Component);
	/** Sets the placement values of a running effect. */
	static void SetParameters(UNiagaraComponent* Component, const FSwcEffectSettings& Settings, const FVector& PlacementScale = FVector::OneVector);
	/** @return True when the effect has a system. */
	bool HasEffect(const FString& Effect) const;

	/** Starts the effects of a placement list (the menu scene: SWC_CONTENT/particles/menu.json) whose "distance" (m, to the
	 *  Player Entity of the scene) is within RadiusM. @return The started components (the caller stops them). */
	TArray<UNiagaraComponent*> StartList(const FString& File, double RadiusM, USceneComponent* Parent = nullptr);

	/** The test of swc.Effects.Test (KEY,COUNT[,DISTANCE_M]); an empty text removes the copies. */
	void RunTest(const FString& Spec);

	/** The folder SWC_CONTENT/particles. */
	static FString Folder();
	/** The Unreal transform of a source placement matrix (rows 3 x 4, m), and the values of a placement settings object. */
	static FTransform PlacementTransform(const TArray<TSharedPtr<FJsonValue>>& Matrix);
	static FSwcEffectSettings PlacementSettings(const TSharedPtr<FJsonObject>& Settings);

	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FType
	{
		FString Key;
		FSoftObjectPath Path;
		double ViewCm = 0.0;
		double MaxSize = 0.0;   // M (m): the largest half size x ViewDistanceAdjust
		bool bTried = false;
		TObjectPtr<UNiagaraSystem> System = nullptr;
		TArray<TWeakObjectPtr<UNiagaraComponent>> Pool;
	};
	struct FPlacement
	{
		FTransform Transform;
		int32 Type = INDEX_NONE;
		FSwcEffectSettings Settings;
		double RangeCm = 0.0;   // the outer LOD bound
		double SizeM = 0.0;     // M x S x V (m) of the view distance rule
		TWeakObjectPtr<UNiagaraComponent> Component;
		double StartedAt = 0.0;
	};
	struct FRegionData
	{
		TArray<FPlacement> Placements;
		TMap<FIntPoint, TArray<int32>> Cells;   // the placements of each cell of CellCm
		int32 Rows = 0;
		int32 Skipped = 0;
	};
	struct FRegion
	{
		bool bStarted = false;
		bool bNoFile = false;
		TSharedPtr<FRegionData, ESPMode::ThreadSafe> Data;
		TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> bRead;
	};

	void LoadTypes();
	int32 TypeOf(const FString& Effect) const;
	UNiagaraSystem* SystemOf(int32 Type);
	UNiagaraComponent* Acquire(int32 Type);
	void Release(int32 Type, UNiagaraComponent* Component, bool bKill);
	AActor* OwnerActor();
	static bool ReadRegion(const FString& Path, const TMap<FString, int32>& Keys, const TArray<double>& Views, const TArray<double>& Sizes,
	                       FRegionData& Out);
	/** The regions near View (started when needed). @return False while a region near View is still read. */
	bool RegionsNear(const FVector& View, double ReachCm, TArray<FRegionData*>& Out);
	void Update(const FVector& View, int32 Budget, double MarginCm);
	/** @return The range (cm) of a placement: the outer LOD bound and the view distance of the emitter (particle-rules.md 4). */
	double RangeOf(const FPlacement& Placement) const;
	/** The screen height / the vertical field of view (pixels per radian) x P of the view distance rule (0: the rule is off). */
	double PixelsPerRadian = 0.0;
	bool ReadyNear(const FVector& Place, FString& Why);
	void SpawnTest(const FString& Spec);

	TArray<FType> Types;
	TMap<FString, int32> TypeIndex;   // the lower-case key -> Types
	TMap<FIntPoint, FRegion> Regions;
	TSet<TPair<FIntPoint, int32>> Active;   // (region, placement) with a component
	TWeakObjectPtr<AActor> Owner;
	TArray<TWeakObjectPtr<UNiagaraComponent>> Started;   // the components of Start (the frame report)
	/** The kept components (Keep) and the time of their keep: an endless system stops after KeepSeconds (a value of this client). */
	TMap<TWeakObjectPtr<UNiagaraComponent>, double> Kept;
	/** AST-230: the ribbons of a started effect (Effects/SwcRibbons): they start and stop with its component. */
	TMap<TWeakObjectPtr<UNiagaraComponent>, TArray<TWeakObjectPtr<USwcRibbonComponent>>> Ribbons;
	void StartRibbons(const FString& Effect, UNiagaraComponent* Component, const FSwcEffectSettings& Settings);
	void StopRibbons(UNiagaraComponent* Component, bool bKill);
	double MaxViewCm = 0.0;
	double NextUpdate = 0.0;
	double NextReport = 0.0;
	bool bWorld = false;
	bool bShown = true;
	FString TestSpec;
	TArray<TWeakObjectPtr<UNiagaraComponent>> TestComponents;
	UPROPERTY()
	TObjectPtr<class UMaterialParameterCollection> Collection;
	float AppliedEmission = -1.0f;
	float AppliedBrightness = -1.0f;
	float LoggedBrightness = -1.0f;
	bool bTestDone = false;
};
