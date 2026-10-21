#pragma once

#include "CoreMinimal.h"

class FSwcActions;
class UMeshComponent;
class UWorld;

/** One hit of the character on a creature (AST-230 plan step 9): what the selection of the hit function needs. */
struct FSwcImpactHit
{
	/** The contact point and the direction of the strike (Unreal world space). */
	FVector Point = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	/** The damage row of the strike (DamageTable.DamageID), the hit volume and the slice of the creature, the critical flag of the server. */
	FString DamageRow;
	FString Volume;
	FString SlicePath;
	bool bCrit = false;
	/** The weapon of the attacker in the main hand and its file (the helpers of a trail on the weapon). */
	UMeshComponent* Weapon = nullptr;
	FString WeaponFile;
	const FSwcActions* Actions = nullptr;
};

/**
 * The hit effects (AST-230 plan step 9, docs/EFFECTS.md "The hits"; SWC_CONTENT/particles/impacts.json). The selection of the study ("Hit
 * effect lookup" [D, R]): the WeaponCategory of the damage row is the row of ImpactTable_Characters, with the suffix "+Crit" for a critical
 * hit when the table has that row (the native key construction checks the critical suffix first [R]); the column is the material of the
 * hit volume of the creature (Flesh, FleshHead, ...; else Default); the cell is a function of scripts/combatimpact. Its calls play:
 * SpawnParticle at the contact point with its direction (the local +Z of the effect along the direction [I]); SpawnParticleAtAttachment
 * at a helper of the weapon of the attacker (EmitterFollow). Not done: SpawnDecal, the joint calls, the resist suffix, the elements.
 */
class SOULWARDENCLIENT_API FSwcImpactEffects
{
public:
	/** Plays the hit function of a hit. @return The text of the log (the row, the column, the function, the effects). */
	FString Play(UWorld* World, const FSwcImpactHit& Hit);

private:
	struct FCall
	{
		FString Call;
		FString Key;
		FString Helper;
		/** The sign of the direction (a3 * N); 0: another direction (the raw direction of the hit). */
		float DirectionScale = 1.0f;
	};
	bool Load();

	bool bLoaded = false;
	bool bValid = false;
	TMap<FString, TMap<FString, FString>> Characters;
	TMap<FString, TArray<FCall>> Functions;
	TMap<FString, FString> Categories;
	TMap<FString, TMap<FString, FString>> Volumes;
};
