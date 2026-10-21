#pragma once

#include "CoreMinimal.h"
#include "Creatures/SwcEffects.h"
#include "GameFramework/Actor.h"
#include "SwcEffectActor.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;
struct FSwcProjectileSlice;
struct FSwcSpellRow;
struct FSwcSpellSlice;

/**
 * A shot or a danger area of a creature (stage P2.9, docs/CREATURES.md): an entity of the server with a Projectile state, or with
 * the states of a spell. The look is simple and readable (the effects of the game are for a later stage): a shot is a thin
 * shaft along its flight; a danger area is a flat disc of the size of its volume on the ground, orange for the warning and red
 * for the danger. The times and the places are the ones of the server (Creatures/SwcEffects.h).
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

private:
	void SetColor(const FLinearColor& Color);
	void SetColumnColor(const FLinearColor& Color);
	/** Sets the colors again when the exposure of the frame changed by more than 10 percent (stage MVP-I). */
	void FollowExposure();

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Shape;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;
	/** Stage MVP-I: a low translucent column over the disc of a hostile area (the disc alone hides under the ground cover). */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Column;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ColumnMaterial;
	/** The colors before the exposure scale, and the scale of the last set. */
	FLinearColor ShapeColor = FLinearColor::White;
	FLinearColor ColumnColor = FLinearColor::White;
	double AppliedScale = 1.0;

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
