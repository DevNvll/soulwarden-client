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

/** The look of one character: its gender and its parts. */
struct FSwcPlayerLook
{
	bool bFemale = false;
	TArray<FSwcLookPart> Parts;
	/** Values that the resolver could not apply (a log line each), for example an armor with its own character file. */
	TArray<FString> Notes;
};

/**
 * The look of a player from its appearance state (12 index bytes, Soulwarden protocol notes 4.5) and its paperdoll
 * (paperdoll slot -> item index on the wire, 4.11). The data:
 *   SWC_CONTENT/player/looks.json      the character creation databases and the armor appearance rows (tools/convert-player.sh tables)
 *   SWC_CONTENT/player/imported.json   the imported assets of the parts and of the base (tools/import-player.sh)
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

	/** The base of a gender: the skeletal mesh (the leader) and the idle clip. Empty when it was not imported. */
	FString BaseMesh(bool bFemale) const;
	FString IdleAnimation(bool bFemale) const;

	/** Adds the parts with no mesh to SWC_WORK/player/wanted.json. @return The count of new entries. */
	int32 WriteWanted(const FSwcPlayerLook& Look) const;

private:
	FString ImportedObject(const FString& Key, const TCHAR* Class) const;

	TSharedPtr<FJsonObject> Looks;
	TSharedPtr<FJsonObject> Imported;
	/** Item index - 1 -> [id, armorAppearanceM, armorAppearanceF]. */
	TArray<TArray<FString>> Items;
};
