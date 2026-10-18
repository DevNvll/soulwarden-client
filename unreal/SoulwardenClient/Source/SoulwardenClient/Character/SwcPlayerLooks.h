#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/** One visible part of the modular body: a skin with one material on one attachment slot of the player. */
struct FSwcLookPart
{
	/** The attachment slot of player_<gender>.cdf (head1, shirt1, shirt2, gloves1, hair, facial_hair, ...). */
	FName Slot;
	/** Game paths: the skin attachment and its material. Together they are the key of the converted part. */
	FString Skin;
	FString Material;
	/** The hair color: a tint of the base color (BaseColorFactor of the material). No tint: alpha 0. */
	FLinearColor Tint = FLinearColor(0, 0, 0, 0);
	/** The skeletal mesh of the part, when it was imported (tools/import-player.sh). */
	FString MeshPath;
};

/** A place of a gear part: a bone attachment of player_<gender>.cdf (CA_BONE) with its offset in Unreal bone space. */
struct FSwcAttachPlace
{
	FString Name;
	FName Bone;
	FTransform Offset;
	bool IsValid() const { return !Bone.IsNone(); }
};

/** One weapon of the paperdoll, or the accessory of a weapon (the quiver of a bow): its mesh and its two places. */
struct FSwcGearPart
{
	/** The paperdoll slot of the weapon: 24 to 26 the weapons 1 to 3, 27 to 29 their off hand (protocol notes 4.11). */
	uint32 PaperdollSlot = 0;
	FString Item;
	FString EquipType;
	/** The Mannequin tags of the weapon (MannequinTag of its weapon row, "1H_Melee,Sword"): its actions (actions.json). */
	FString Tags;
	/** The skeleton of the weapon model (the frames of the damage shapes of the weapon, actions.json weaponJoints). */
	FString Model;
	/** The key of the converted mesh (weapons.json), and the static mesh when it was imported. */
	FString MeshKey;
	FString MeshPath;
	FSwcAttachPlace Sheathed;
	FSwcAttachPlace Hand;
	bool bAccessory = false;
	/** SheathMode ALWAYS_VISIBLE of an accessory; HideMainWeaponMeshWhileSheathed of a weapon. */
	bool bAlwaysVisible = false;
	bool bHideSheathed = false;
	/** The swap times of the Mannequin data (s from the start of the action): the marker Unsheathe_Weapon (or
	 *  Unsheathe_Offhand for an off-hand part) of the draw fragment, and sheathe_weapon of the sheathe fragment. */
	float DrawTime = 0.0f;
	float SheatheTime = 0.0f;
};

/** The look of one character: its gender, its parts, and its gear. */
struct FSwcPlayerLook
{
	bool bFemale = false;
	TArray<FSwcLookPart> Parts;
	TArray<FSwcGearPart> Gear;
	/** Values that the resolver could not apply (a log line each), for example an armor with its own character file. */
	TArray<FString> Notes;
};

/**
 * The look of a player from its appearance state (12 index bytes, Soulwarden protocol notes 4.5) and its paperdoll
 * (paperdoll slot -> item index on the wire, 4.11). The data:
 *   SWC_CONTENT/player/looks.json      the character creation databases and the armor appearance rows (tools/convert-player.sh tables)
 *   SWC_CONTENT/player/imported.json   the imported assets of the parts and of the base (tools/import-player.sh)
 *   SWC_CONTENT/player/weapons.json    the weapon look of each weapon item, the bone attachments of the player, and the
 *                                      attachment rules of each EquipType (tools/convert-player.sh weapons)
 *   SWC_GAMEDATA_DIR/item-catalog.json the item list of the wire (the item with index N is entry N - 1) with the armor
 *                                      appearance names of each item (format 5), the game data of the backend copy
 * The rules are in docs/CHARACTER.md. A part that is not imported goes to the wanted list
 * (SWC_WORK/player/wanted.json): tools/convert-player.sh wanted and tools/import-player.sh make it.
 */
class FSwcPlayerLooks
{
public:
	/** Reads the three files. @return False with a reason when looks.json is not there. */
	bool Load(FString& OutReport);

	FSwcPlayerLook Resolve(const TArray<uint8>& Appearance, const TMap<uint32, uint32>& Equipment) const;
	/** @return The item id of an item index of the wire (item-catalog.json), or an empty text. */
	FString ItemId(uint32 ItemIndex) const;
	/** @return The weight of one item of an item index of the wire in tenths (the column weight of item-catalog.json: the rule of
	 *  the backend Inventory::encumbrance, stage P3.3), or 0. */
	int32 ItemWeight(uint32 ItemIndex) const;

	/** The base of a gender: the skeletal mesh (the leader) and the idle clip. Empty when it was not imported. */
	FString BaseMesh(bool bFemale) const;
	FString IdleAnimation(bool bFemale) const;

	/**
	 * Adds the parts and the weapons with no mesh, and the clip packs of the weapon tags that are not imported (stage P2.7),
	 * to SWC_WORK/player/wanted.json. @return The count of new entries.
	 */
	int32 WriteWanted(const FSwcPlayerLook& Look) const;

private:
	FString ImportedObject(const FString& Key, const TCHAR* Class) const;
	void ResolveGear(const TMap<uint32, uint32>& Equipment, FSwcPlayerLook& Look, TMap<FName, FSwcLookPart>& Slots) const;
	FSwcAttachPlace Place(bool bFemale, const FString& Attachment, const TSharedPtr<FJsonObject>& Side, const TCHAR* Kind) const;

	TSharedPtr<FJsonObject> Looks;
	TSharedPtr<FJsonObject> Weapons;
	TSharedPtr<FJsonObject> Imported;
	/** Item index - 1 -> [id, armorAppearanceM, armorAppearanceF]. */
	TArray<TArray<FString>> Items;
	TArray<int32> Weights;
};
