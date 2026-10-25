#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Codec/SwcRepClient.h"

#include <memory>
#include <string>
#include <vector>

#include "SwcRangedView.generated.h"

class FSwcActions;
class UCanvas;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UStaticMeshComponent;
namespace swc::combat
{
class Driver;
}

/**
 * The pictures of the ranged and magic actions of the player (stage P3.6, docs/RANGED.md):
 * - the aim point: the camera ray to the first hit (the channel Pawn: the world and the bodies of the creatures) or to 100 m; the shot
 *   items of the session go to it (RepClient::Motion::aimPoint);
 * - the own projectile of each shot: the server sends no record of a player shot (protocol notes 4.27), thus the client flies its own
 *   picture with the flight of the data (projectile-data.json: the speed and the acceleration of the slice after the acceleration
 *   delay of the attack, to its range or its life), from the muzzle (1.0 m along the look, 1.46 m above the feet) to the aim point; it
 *   ends at the first hit of a sphere of its radius on the channel Pawn (a wall or a creature body; an arrow stays there 8 s, the stuck
 *   life of basic_arrow);
 * - the ground target of an aimed ability (CastSpellTargeting of the data): a disc of the radius of the spell at the ground of the
 *   camera ray in its cast distance, and the result for the raycast of the confirm (ActionConditionSpellSpawnLocationState);
 * - the heal numbers: a green "+N" (UIStyle COLOR_NAMEPLATE_HEALING, RGB 48 229 171) at the feet for 2 s.
 * The look is the default look of the engine (the basic shapes), as the decision of the user permits.
 */
UCLASS(ClassGroup = (Soulwarden))
class SOULWARDENCLIENT_API USwcRangedView : public UActorComponent
{
	GENERATED_BODY()

public:
	USwcRangedView();

	void Setup(const TSharedPtr<FSwcActions>& InData, const std::shared_ptr<swc::combat::Driver>& InDriver);
	/** @return True with the aim point of the last frame (Unreal cm). */
	bool GetAimPoint(FVector& Out) const;
	/** The ground target of the last frame is valid (in the cast distance of the targeting spell, on a ground). */
	bool IsGroundValid() const { return bGroundValid; }
	/** The heal numbers of the read path (OwnEntity::heals); each new one shows. */
	void SetHeals(uint32 Revision, const std::vector<swc::net::RepClient::OwnEntity::Heal>& Heals);
	/** The UI shows the heal numbers in the original damage number canvas (stage WF7): the simple heal numbers of each ranged view go
	 *  off (default on; the log lines stay). */
	static void SetUiHealNumbers(bool bUi) { bUiHealNumbers = bUi; }

	/** The counts of the run (the log at the leave). */
	uint32 GetProjectiles() const { return Projectiles; }
	uint32 GetProjectileHits() const { return ProjectileHits; }
	uint32 GetHealNumbers() const { return HealNumbers; }
	uint32 GetTargetings() const { return Targetings; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FShot
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		/** The trail of the attack on the projectile (AST-230, Effects/SwcTracers). */
		TWeakObjectPtr<UNiagaraComponent> Trail;
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector Accel = FVector::ZeroVector;
		float AccelDelay = 0.0f;
		float MaxRange = 0.0f;
		float Life = 0.0f;
		float Radius = 0.0f;
		float Traveled = 0.0f;
		float Age = 0.0f;
		/** The time that it stays after a hit (an arrow: the stuck life); below 0: it flies. */
		float Stuck = -1.0f;
		bool bArrow = false;
		/** The mesh is the mesh of the ammunition in the frame of the game (its tip at the origin, its flight on its local Unreal -Y);
		 *  else a basic shape along its local +Z (usertest-1024). */
		bool bGameFrame = false;
	};
	struct FNumber
	{
		FVector Point = FVector::ZeroVector;
		FString Text;
		double Start = 0.0;
	};

	void UpdateAim();
	void SpawnShots();
	void UpdateShots(float DeltaTime);
	void UpdateTarget();
	void DrawNumbers(UCanvas* Canvas, APlayerController* Controller);
	UStaticMeshComponent* MakeMesh(const TCHAR* MeshPath, const FLinearColor& Color, bool bTint = true);
	/** The rotation of a projectile mesh along a flight direction (bGameFrame: a mesh of the game; usertest-1024). */
	static FQuat FlightRotation(const FVector& Direction, bool bGameFrame);
	/** @return The object path of the mesh of the ammunition of a shot of a ranged profile ("Bow"), or empty (usertest-1024). */
	FString ArrowMesh(const std::string& Profile) const;

	TSharedPtr<FSwcActions> Data;
	std::shared_ptr<swc::combat::Driver> Driver;
	bool bHasAim = false;
	FVector AimPoint = FVector::ZeroVector;
	uint32 SeenRecent = 0;
	TArray<FShot> Shots;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Indicator;
	FString IndicatorSpell;
	bool bGroundValid = false;
	uint32 SeenHeals = 0;
	TArray<FNumber> Numbers;
	FDelegateHandle DrawHandle;
	uint32 Projectiles = 0;
	uint32 ProjectileHits = 0;
	uint32 HealNumbers = 0;
	uint32 Targetings = 0;
	static bool bUiHealNumbers;
};
