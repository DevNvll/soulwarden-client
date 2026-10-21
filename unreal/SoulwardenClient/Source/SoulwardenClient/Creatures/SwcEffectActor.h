#pragma once

#include "CoreMinimal.h"
#include "Creatures/SwcCreatureData.h"
#include "Creatures/SwcEffects.h"
#include "GameFramework/Actor.h"
#include "SwcEffectActor.generated.h"

class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UStaticMeshComponent;
struct FSwcProjectileSlice;
struct FSwcSpellRow;
struct FSwcSpellSlice;

/**
 * A shot or a danger area of a creature (stage P2.9, docs/CREATURES.md): an entity of the server with a Projectile state, or with
 * the states of a spell. The times and the places are the ones of the server (Creatures/SwcEffects.h).
 *
 * AST-230 (docs/EFFECTS.md, the slice effect player): the particle entities of the slice play their effects at the root of the actor
 * with their transform in the slice: an entity that is active at the start plays from the start of the spell (a shot: from its launch);
 * an activate (2) or deactivate (3) delayed event of the slice that targets the entity starts or stops it at its due time (the event
 * times of the server: swc::creatures::event_due_us). A deactivation stops the emission (Kill On Deactivate: the particles go too). A
 * late observer does not start an event burst older than LateBurstSeconds. A hostile area shows only its particles (the decision of
 * the user of 2026-10-21, "Match retail now": no ground warning; the stand-in column is gone). A shot with a particle effect shows no
 * shaft. A spell of a character and a summon keep their disc and pillar of stage P2.9 with their particles.
 */
UCLASS(Transient)
class SOULWARDENCLIENT_API ASwcEffectActor : public AActor
{
	GENERATED_BODY()

public:
	ASwcEffectActor();

	void SetupProjectile(const FSwcProjectileSlice* Slice);
	/**
	 * @param bFriendly A spell of a character (the vortex, the trap, the Beacon): blue, not the orange and red of a creature attack.
	 * @param bSummon A spell with health (the Pylon, the trap) or with a volume wider than 8 m: a pillar at its place, not a disc.
	 */
	void SetupArea(const FSwcSpellSlice* Slice, const FSwcSpellRow* Row, bool bFriendly, bool bSummon);

	/**
	 * Shows the entity at a time of the server clock (the render time of the creatures).
	 * @return The text of a new phase for the log (the contact of a shot; the warning, the danger, and the end of an area), or an
	 *         empty text.
	 */
	FString UpdateFromEntity(const swc::creatures::Entity& Entity, uint64 RenderTimeUs, const FVector* Follow = nullptr);

	uint64 GetSerial() const { return Serial; }
	void SetSerial(uint64 Value) { Serial = Value; }
	bool IsArea() const { return bArea; }
	/** The radius of the danger area (m). */
	float GetRadius() const { return Radius; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetColor(const FLinearColor& Color);
	/** AST-230: starts and stops the particle entities at a server time. @return The text of the starts and the stops for the log. */
	FString UpdateParticles(const swc::creatures::SpellParts* Parts, uint64 StartUs, uint64 RenderTimeUs);
	/** Sets the colors again when the exposure of the frame changed by more than 10 percent (stage MVP-I). */
	void FollowExposure();

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Shape;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;
	/** The colors before the exposure scale, and the scale of the last set. */
	FLinearColor ShapeColor = FLinearColor::White;
	double AppliedScale = 1.0;
	/** AST-230: the particle entities of the slice, their components, and their state (on or off). */
	TArray<FSwcSliceParticle> Particles;
	TArray<TWeakObjectPtr<UNiagaraComponent>> ParticleComponents;
	TArray<bool> ParticleOn;
	bool bShaft = true;

	uint64 Serial = 0;
	bool bArea = false;
	bool bHeld = false;
	FVector LastDirection = FVector::ForwardVector;
	swc::creatures::ProjectileFlight Flight;
	std::vector<swc::creatures::SpellEventData> Events;
	swc::creatures::SpellVolumeData Volume;
	bool bHasVolume = false;
	float Radius = 0.0f;
	int32 LastPhase = -1;
	bool bFriendly = false;
	bool bSummon = false;
	/** The place of the last ground line and the height from the place of the server to the ground there (cm). */
	FVector LastPlace = FVector(TNumericLimits<double>::Max());
	double GroundOffset = 0.0;
};
