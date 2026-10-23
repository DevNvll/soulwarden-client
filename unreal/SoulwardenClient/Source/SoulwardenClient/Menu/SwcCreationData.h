// The creation data of a create request (stage Q3, docs/FRONTEND.md "Q3, the character creation"): the member
// CharacterCreationParams of CreateCharacterRequest. It has the form of the menu document (Menu/SwcMenuDocument.h):
//
//   text        := base64( zlib( document ) )
//   document    := {"currency":0,"visualsBlob":"<bytes>","position":{"x":0.0,"y":0.0,"z":0.0},"currentLevel":0,"worldId":"<id>"}
//   visualsBlob := a binary object stream (version 3) of one PlayerSaveData (class version 3) with the fields of the client in the
//                  order of its create function 0x2EB0FE0: playerName, companyName, entityClassName, guildCrestName, the 12
//                  appearance names, backstory, startingFaction, appearanceData, equipment.
//
// The JSON writer of the client (rapidjson, 0x17A75B0) writes a byte below 0x20 as an escape, a quote and a backslash with a
// backslash, and each other byte with no change: the document is not valid UTF-8 text (research character-creation.md 3.1). The
// zlib stream is a deflate of the level 9 (the header 78 DA of the client; the backend reads each zlib form). The backend reads
// the backstory and the 12 appearance names (state/creation_data.cpp of the Soulwarden backend, read only); tools/test-menu.sh
// compares this writer with the backend writer make_menu_document, byte for byte, with no engine.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Menu/SwcMenuDocument.h"

namespace swc::menu {

/** One entry of the map `equipment`: the key ("hat", "shirt", "gloves", "pants", "boots") and the armor appearance id. */
struct FCreationEquipment
{
	std::string Slot;
	/** The CRC-32 of the lower-case name of an armor appearance row (AZ::Crc32 of the client). */
	std::uint32_t AppearanceId = 0;
};

/** The values of a new character (the getters of CustomizableCharacterComponent and the create arguments of the script). */
struct FCreation
{
	/** The validated name (the Lua argument `name` of UiCharacterServiceRequestBus.CreateCharacter). */
	std::string PlayerName;
	/** The text of @guild_default_name with the name (the Lua argument `companyName`), for example "Name's Company". */
	std::string CompanyName;
	/** The 12 names of the character creation databases, the gender first ("Male" or "Female"). */
	std::array<std::string, kAppearanceFieldCount> Appearance;
	/** The backstory of the archetype, for example "Archetype_Soldier". */
	std::string Backstory;
	/** The faction of a seasonal character; 0 for a regular character. */
	std::uint8_t StartingFaction = 0;
	/** The entries of `equipment`, in this sequence (the client: the order of its hash map, gloves, shirt, hat, boots, pants). */
	std::vector<FCreationEquipment> Equipment;
	/** The world id of the create request. */
	std::string WorldId;
};

/** The key order of the map `equipment` in the create requests of the client (5 of 5 captures; protocol notes 2.2.5). */
inline constexpr std::array<std::string_view, 5> kCreationSlotOrder{"gloves", "shirt", "hat", "boots", "pants"};

/** @return The PlayerSaveData object stream of the values (the bytes of visualsBlob). */
[[nodiscard]] std::vector<std::uint8_t> WriteSaveData(const FCreation& Values);
/** @return The JSON document with the object stream as the bytes of visualsBlob (not valid UTF-8). */
[[nodiscard]] std::string WriteCreationDocument(const FCreation& Values);
/** @return The text of CharacterCreationParams: base64 of the zlib stream of the document. Empty: the zlib deflate failed. */
[[nodiscard]] std::string EncodeCreation(const FCreation& Values);

/** @return Standard base64 with padding. */
[[nodiscard]] std::string EncodeBase64(const std::vector<std::uint8_t>& Bytes);
/** @return The CRC-32 (zlib, ISO-HDLC) of the lower-case ASCII form of a text: the AZ::Crc32 of a name. */
[[nodiscard]] std::uint32_t NameCrc(std::string_view Name);

} // namespace swc::menu
