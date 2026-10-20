#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "SwcWeaponAnimInstance.generated.h"

class FSwcActions;
class UAnimSequence;
class USwcPlayerAnimInstance;

/**
 * The animation of a skeletal weapon (AST-202, docs/GEAR.md "The weapon clips"; research weapon-props.md "The rule for the
 * client"): the weapon scope of the Mannequin data. Each frame it takes the weapon visits of its leader (the player animation:
 * the weapon record of each fragment that owns the scope Weapon or WeaponTransition, with the time and the weight of that
 * fragment) and plays their clips on the skeleton of the weapon over its bind pose. No clock of its own: the weapon clip stays in
 * step with the body clip of the same fragment. With no leader (a weapon in its sheath, an inactive weapon) the weapon is in its
 * bind pose.
 */
UCLASS(Transient)
class SOULWARDENCLIENT_API USwcWeaponAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/**
	 * Gives the action data and the clip assets of the weapon (imported.json). The AnimSequence of a clip is "<prefix><clip>": the
	 * prefix is the name of the glTF file of the weapon, which is the name of its skeletal mesh. Interchange cuts a long name in its
	 * middle ("<start>_<end>"): such a name goes to the one weapon clip whose full name starts and ends with the two parts.
	 */
	void Setup(const TSharedPtr<FSwcActions>& Data, const FString& MeshPath, const TArray<FString>& ClipPaths);
	/** The player animation that this weapon follows, or null (the bind pose). */
	void SetLeader(USwcPlayerAnimInstance* InLeader);
	bool HasLeader() const { return Leader.IsValid(); }
	/** The clip assets that were found (a log line). */
	int32 GetClipCount() const { return Sequences.Num(); }

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	const TArray<int32>& ClipBones(int32 Clip);

	TSharedPtr<FSwcActions> Actions;
	TWeakObjectPtr<USwcPlayerAnimInstance> Leader;
	/** The clip of the action data -> its asset on the skeleton of the weapon. */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UAnimSequence>> Sequences;
	TMap<int32, TArray<int32>> BoneCache;
};
