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
	void SetupArea(const FSwcSpellSlice* Slice, const FSwcSpellRow* Row);

	/**
	 * Shows the entity at a time of the server clock (the render time of the creatures).
	 * @return The text of a new phase for the log (the contact of a shot; the warning, the danger, and the end of an area), or an
	 *         empty text.
	 */
	FString UpdateFromEntity(const swc::creatures::Entity& Entity, uint64 RenderTimeUs);

	uint64 GetSerial() const { return Serial; }
	void SetSerial(uint64 Value) { Serial = Value; }
	bool IsArea() const { return bArea; }
	/** The radius of the danger area (m). */
	float GetRadius() const { return Radius; }

private:
	void SetColor(const FLinearColor& Color);

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Shape;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;

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
};
