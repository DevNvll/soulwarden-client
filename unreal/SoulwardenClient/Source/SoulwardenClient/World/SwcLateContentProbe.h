#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SwcLateContentProbe.generated.h"

class UPrimitiveComponent;

/**
 * The check of late content (the rule of the user, 2026-10-20, AST-203: when the loading screen ends, all content near the
 * character is there and visible). -SwcLateProbe[=SECONDS] (default 10): at the end of each loading screen (the spawn of
 * the start, the end of the wait of a travel or a respawn) the probe takes the visible primitive components within
 * 150 m of the character (their meshes and materials), then for SECONDS logs each component within 150 m that becomes
 * visible (new or shown), changes its mesh, or changes a material: "late: SECONDS DISTANCE OWNER COMPONENT MESH". At the
 * start and at the end of the window it logs the engine work that changes the picture: the shader jobs, the meshes and
 * textures that still compile, and the textures that still stream. The summary line "late content: N" (0 is the target).
 * A new creature or effect of the server ("entity": the server sends it at its interest radius) is listed, not counted. The
 * distance of a component is the one of its nearest instance (an instanced component) or of its box; a component beyond its
 * draw distance from the camera draws nothing and does not count. The far cells: the times after the screen when the cells
 * of the World Partition within 300 m, 500 m, and 1,000 m of the character are in the world ("late: the cells within ...").
 * The light: the exposure and the mean scene luminance of the game view each second ("late: light"), and their spread.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcLateContentProbe : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FSeen
	{
		const UObject* Mesh = nullptr;
		uint32 MaterialHash = 0;
		bool bVisible = false;
	};

	bool CharacterPlace(FVector& Out) const;
	void Start(const FVector& Place, const TCHAR* What);
	void Scan(bool bFirst);
	static bool Describe(const UPrimitiveComponent* Component, FSeen& Out);
	/** @return False when the component is beyond its draw distance (or the end cull distance of its instances) from the camera. */
	static bool InDrawRange(const UPrimitiveComponent* Component, const FVector& Camera);
	/** @return The distance from Point to the component: to its nearest instance (an instanced component), else to its box. */
	static double Nearest(const UPrimitiveComponent* Component, const FVector& Point);
	/** @return True for an actor of an entity of the server (a creature, an effect): it shows when its record comes. */
	static bool IsServerEntity(const AActor* Owner);
	void CheckRings(const FVector& Place, double Now);
	FString EngineWork() const;

	TMap<TWeakObjectPtr<const UPrimitiveComponent>, FSeen> Seen;
	TSet<TWeakObjectPtr<const UPrimitiveComponent>> Reported;
	FString What;
	double WindowSeconds = 10.0;
	double WindowStart = 0.0;
	double NextScan = 0.0;
	int32 Late = 0;
	/** The new creatures and effects of the server (the interest radius of the server, not a late load): listed, not counted. */
	int32 Entities = 0;
	/** The seconds after the screen when the cells within each ring were in the world (-1: not yet). */
	TArray<double> RingSeconds;
	double NextRingCheck = 0.0;
	/** The light of the window: a line each second, the low and the high exposure. */
	double NextLight = 0.0;
	float ExposureLow = 0.0f;
	float ExposureHigh = 0.0f;
	bool bEnabled = false;
	bool bActive = false;
	bool bHadPawn = false;
	bool bWasWaiting = false;
};
