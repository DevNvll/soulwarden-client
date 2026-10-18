#pragma once

#include "CoreMinimal.h"
#include "Creatures/SwcCreatureData.h"
#include "Creatures/SwcEntityTable.h"
#include "Combat/SwcMeleeShape.h"
#include "Creatures/SwcHitShapes.h"
#include "Creatures/SwcMeleeContact.h"
#include "Subsystems/WorldSubsystem.h"
#include "SwcCreatureSubsystem.generated.h"

class APlayerController;
class ASwcCreature;
class UCanvas;

DECLARE_LOG_CATEGORY_EXTERN(LogSwcCreatures, Log, All);

/** A creature for the target lock of the camera: its interest number, its feet (Unreal cm), and the dead mark. */
struct FSwcCreatureTarget
{
	uint32 Interest = 0;
	FVector Position = FVector::ZeroVector;
	bool bDead = false;
};

/** A melee contact: the weapon shape of the player touches a hit volume of a creature (FindMeleeContacts). */
struct FSwcMeleeContact
{
	uint32 Interest = 0;
	/** The hit volume (Head, Leg_F_L, ...) and its marks. */
	FString Volume;
	bool bHeadshot = false;
	bool bLegshot = false;
	/** The point on the surface of the volume and its normal (Unreal world space). */
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
	/** The item of list 3 of the input entry (engine space; swc::net::write_melee_contact writes its 96 bytes). */
	swc::net::MeleeContactItem Item;
	/** The distance from the feet of the attacker to the feet of the creature (m), for the log. */
	float Distance = 0.0f;
	/** A contact of the development switch -SwcFarContact: a creature out of the reach (the server refuses it: "too far"). */
	bool bDevelopment = false;
};

/**
 * The creatures of the backend in the world of the game (docs/CREATURES.md): the entity table of the other entities (the
 * records and the stops of USwcBackendSubsystem::TakeOtherUpdates), one ASwcCreature for each entity with a slice and a
 * position, the movement states shown with a delay of InterpolationDelay on the server clock, and the name plates (a simple
 * plate drawn on the canvas of the game viewport; the original is the AiMarker template of
 * lyshineui/markers/markermanager.dynamicuicanvas, for the UI track).
 *
 * The damage numbers of the player (the message "on damage dealt", Creatures/SwcDamageMessage.h): a simple number at the
 * creature that rises and fades. The original is the damage number canvas of the game (for the UI runtime later).
 *
 * Command line: -SwcPlates=all (development pictures) draws the plate of each creature within 40 m. -SwcHitVolumes draws the
 * hit volumes of the creatures within 20 m and their body capsules (white). -SwcVolumeProbe tests a capsule in front of the player each 0.5 s with
 * FindMeleeContacts and writes a log line for each contact (it sends nothing).
 */
UCLASS()
class SOULWARDENCLIENT_API USwcCreatureSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The read path of the camera (the target lock): the creatures of the table with a position. */
	void GetTargets(TArray<FSwcCreatureTarget>& Out) const;
	const swc::creatures::EntityTable& GetTable() const { return Table; }

	/**
	 * The melee contacts of one damage shape of a strike at one input step (the player track: FSwcMeleeShape): the hit volumes
	 * of the creatures at the pose that they show (protocol notes 4.15). One contact for each creature (the touched volume
	 * nearest to the center of the shape); none for a dead creature, for a creature with the mark "invulnerable" (the server
	 * refuses it), for a placeholder, or behind a wall (a line from the chest of the attacker to the point hits the static
	 * world; not with bNoSight). The caller keeps one contact for each creature for each strike.
	 *
	 * Command line: -SwcFarContact[=SECONDS] (development runs) adds, when the shape touches no creature, one contact with the
	 * nearest living creature 6 to 10 m from the attacker (from SECONDS of the world time): the server refuses it ("too far",
	 * reach 3.5 m + 1.5 m).
	 * @return The number of contacts in Out (Out is reset).
	 */
	int32 FindMeleeContacts(const FSwcMeleeShape& Shape, TArray<FSwcMeleeContact>& Out) const;
	/** The contact query of the player actions (USwcPlayerActions::SetContactQuery): the items of FindMeleeContacts. */
	void QueryMeleeContacts(const FSwcMeleeShape& Shape, std::vector<swc::net::MeleeContactItem>& Out) const;

	/** The delay of the shown movement states on the server clock: the backend sends a state for each step of 200 ms. */
	static constexpr double InterpolationDelaySeconds = 0.25;

private:
	void Clear(const TCHAR* Cause);
	void Spawn(const swc::creatures::Entity& Entity);
	void DrawPlates(UCanvas* Canvas, APlayerController* Controller);
	void OnDamage(const swc::net::Bytes& Body);
	void DrawDamageNumbers(UCanvas* Canvas);

	/** A damage number on the screen: its point at the creature, its text, its start, and its marks. */
	struct FDamageNumber
	{
		FVector Point = FVector::ZeroVector;
		FString Text;
		double Start = 0.0;
		bool bCrit = false;
		bool bKill = false;
	};
	TArray<FDamageNumber> DamageNumbers;

	swc::creatures::EntityTable Table;
	FSwcCreatureLibrary Library;
	TMap<uint32, TWeakObjectPtr<ASwcCreature>> Actors;
	FDelegateHandle DrawHandle;
	bool bAllPlates = false;
	bool bFarContacts = false;
	float FarContactFrom = 0.0f;
	bool bDrawVolumes = false;
	bool bVolumeProbe = false;
	double NextProbe = 0.0;
	/** The strikes and creatures with a log line of a contact (visit|strike|interest). */
	mutable TSet<FString> LoggedContacts;
	void DevelopmentAids();
	bool bConnected = false;
	/** The counts of a session for the log line at its end. */
	int32 Spawned = 0;
	int32 Removed = 0;
	int32 Actions = 0;
	int32 Deaths = 0;
	int32 Placeholders = 0;
	int32 Damages = 0;
	TSet<FString> Kinds;
};
