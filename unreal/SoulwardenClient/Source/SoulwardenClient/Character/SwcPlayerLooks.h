#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/** The mask values of the appearance row of a weapon item (weapons.json "appearance": MaskR, MaskRColor, MaskROverride, ...,
 *  MaskASpec, MaskASpecColor, MaskAGloss, MaskAGlossShift; research weapon-materials.md section 1). The colors are the bytes
 *  of the table text over 255 (gamma values, as the XML mask colors). SwcLookMaterials::ApplyItemMasks puts them on. */
struct FSwcItemMasks
{
	FLinearColor Color[3] = {FLinearColor(0, 0, 0, 0), FLinearColor(0, 0, 0, 0), FLinearColor(0, 0, 0, 0)};
	float Amount[3] = {0.0f, 0.0f, 0.0f};
	float Override[3] = {0.0f, 0.0f, 0.0f};
	FLinearColor SpecColor = FLinearColor(0, 0, 0, 0);
	float SpecAmount = 0.0f;
	float GlossAmount = 0.0f;
	float GlossShift = 0.0f;
	/** The row has mask columns (an armor row: all its groups go on, zeros too). */
	bool bKnown = false;
	/** True when no group has a positive amount (then the material keeps its values). */
	bool IsEmpty() const
	{
		return Amount[0] <= 0.0f && Amount[1] <= 0.0f && Amount[2] <= 0.0f && SpecAmount <= 0.0f && GlossAmount <= 0.0f;
	}
};

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
	/** The mask values of the armor appearance row of the part (looks.json "appearance"); the gear path copies all groups. */
	FSwcItemMasks Masks;
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
	/** The key of the converted mesh (weapons.json), and the mesh when it was imported: a static mesh, or a skeletal mesh for a
	 *  weapon with weapon clips (AST-202: the bow, the flail) with the clip assets on its skeleton. */
	FString MeshKey;
	FString MeshPath;
	bool bSkeletal = false;
	TArray<FString> ClipPaths;
	/** The mask values of the appearance row of the item (none for an accessory). */
	FSwcItemMasks Masks;
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
	/**
	 * The look of the menu preview (AST-196 F3, docs/FRONTEND.md): the 12 appearance names of the menu document (the names of the
	 * creation databases: gender first) and the armor appearance ids of its map `equipment` by paperdoll slot (hat 0, shirt 1,
	 * gloves 2, pants 3, boots 4; the CRC-32 of the lower-case name of an armor row). The same rules as Resolve; no gear.
	 */
	FSwcPlayerLook ResolveMenu(const TArray<FString>& AppearanceNames, const TMap<uint32, uint32>& ArmorCrcs) const;
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
	/**
	 * The appearance preflight (AST-215): each part, each weapon of the gear, and the base of the gender of a look is imported.
	 * @return True when all are; OutText: one line, the counts and each missing part (slot, skin|material), weapon (slot, item),
	 * or base, for the log of the menu and of the world entry.
	 */
	bool Preflight(const FSwcPlayerLook& Look, FString& OutText) const;

	/** A socket of the base character file of the gender (weapons.json "attachments", for example righthandattach on right_hand_attach)
	 *  with no equip offset (usertest-1024: the ammunition of a CAGE-Attachment clip of the body). Not valid when the file has none. */
	FSwcAttachPlace SocketPlace(bool bFemale, const FString& Attachment) const { return Place(bFemale, Attachment, nullptr, TEXT("")); }

private:
	FString ImportedObject(const FString& Key, const TCHAR* Class) const;
	TArray<FString> ImportedObjects(const FString& Key, const TCHAR* Class) const;
	/** The shared code of Resolve and ResolveMenu: `ArmorRows` (the armor row name by paperdoll slot) replaces the item names. */
	FSwcPlayerLook ResolveRows(const TArray<uint8>& Appearance, const TMap<uint32, uint32>& Equipment, const TMap<uint32, FString>* ArmorRows) const;
	void ResolveGear(const TMap<uint32, uint32>& Equipment, FSwcPlayerLook& Look, TMap<FName, FSwcLookPart>& Slots) const;
	FSwcAttachPlace Place(bool bFemale, const FString& Attachment, const TSharedPtr<FJsonObject>& Side, const TCHAR* Kind) const;

	TSharedPtr<FJsonObject> Looks;
	TSharedPtr<FJsonObject> Weapons;
	TSharedPtr<FJsonObject> Imported;
	/** Item index - 1 -> [id, armorAppearanceM, armorAppearanceF]. */
	TArray<TArray<FString>> Items;
	TArray<int32> Weights;
	/** The armor row names by the CRC-32 of their lower-case name (ResolveMenu; made at its first call). */
	mutable TMap<uint32, FString> ArmorByCrc;
};
