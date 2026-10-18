#pragma once

#include "CoreMinimal.h"

/**
 * The type index of the game copy (typeindex.json in the game root, read only): the position of a type id in the list
 * is the type index of a message on the wire (Soulwarden protocol notes 3.3). The retail client and the backend use
 * the same indexes. The client checks the index of each type that it uses (swc::net::kUsedTypes) before it connects:
 * a different index is a different build of the game (the retail client gets the result TypeIndexMismatch, -90).
 */
struct FSwcTypeIndex
{
	/** Reads SOULWARDEN_GAME_DIR/typeindex.json (default ~/Games/soulwarden/game) and checks the used types. */
	static bool Check(FString& OutReport);
};
