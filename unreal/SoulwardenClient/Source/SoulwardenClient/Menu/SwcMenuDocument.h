// The menu document of a character (stage F1 of docs/FRONTEND.md): the member PublishedData of a character of the login
// information. The main menu makes the body of the character from it, with no world connection.
//
//   text        := base64( zlib( document ) )            standard alphabet
//   document    := a JSON object: "currency", "visualsBlob", "position" {x, y, z}, "currentLevel", "worldId"
//   visualsBlob := the bytes of a binary object stream of the class PlayerSaveData (class version 3). The JSON writer of the
//                  game copies each byte of 0x80 and more with no change and writes a byte below 0x20 as an escape: the document
//                  is not valid UTF-8 text. This reader keeps the bytes (open point FE4: no conversion to a Unicode string).
//
// The native chain of the retail client: base64 and zlib 0x171A500, JSON 0x16B3850, object stream 0x14236F0 (research
// character-creation.md 5.3, appearance.md 7.1). The one writer of the backend is state/creation_data.cpp (make_menu_document).
// Plain C++ and zlib: tools/test-menu.sh tests it with documents of that writer, with no engine.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace swc::menu {

/** The limits of the reader (open point FE4). The document of the game has 3 to 4 kilobytes. */
inline constexpr std::size_t kMaxTextBytes = 512 * 1024;
inline constexpr std::size_t kMaxDocumentBytes = 256 * 1024;
inline constexpr std::size_t kMaxVisualsBytes = 64 * 1024;
inline constexpr std::size_t kMaxElementDepth = 16;
inline constexpr std::size_t kMaxElements = 4096;
inline constexpr std::size_t kMaxEquipmentEntries = 32;

/** The 12 appearance fields of PlayerSaveData, in the order of the appearance state (the gender first). */
inline constexpr std::size_t kAppearanceFieldCount = 12;
inline constexpr std::array<std::string_view, kAppearanceFieldCount> kAppearanceFields{
	"gender", "race", "skinTone", "hairstyle", "facialHair", "hairColor", "facialHairColor", "eyeColor", "faceMark", "scar",
	"tattoo", "tattooColor"};

/** The result of a read. Each value but Ok leaves the look of the character empty. */
enum class Status : std::uint8_t
{
	Ok,
	/** No text: the backend sends no PublishedData when it has no appearance tables. */
	Empty,
	TextTooLarge,
	BadBase64,
	BadZlib,
	/** The inflated document is larger than kMaxDocumentBytes. */
	DocumentTooLarge,
	BadJson,
	/** The document has no member "visualsBlob". */
	NoVisuals,
	/** The object stream of "visualsBlob" is not a PlayerSaveData stream in the form of the game. */
	BadVisuals,
};

/** @return A short English text for a log line. */
[[nodiscard]] const char* StatusText(Status Value);

/** One entry of the map `equipment` (EquipmentSaveData). */
struct FEquipment
{
	/** The key: "hat", "shirt", "gloves", "pants", "boots" (the five keys of the game); an other key stays as it is. */
	std::string Slot;
	/** The CRC-32 of the lower-case name of an armor appearance row (not an item catalog index). 0: no value. */
	std::uint32_t AppearanceId = 0;
	/** The item id. The known writers leave it empty (open point FE5). */
	std::string ItemName;
	/** The palette index of the four dye channels (m_rColorId, m_gColorId, m_bColorId, m_aColorId). */
	std::array<std::uint8_t, 4> Dyes{};
	bool bHasDyes = false;
};

/** The values of a menu document. */
struct FDocument
{
	/** The JSON members. */
	double Currency = 0.0;
	std::int64_t CurrentLevel = 0;
	double Position[3] = {0.0, 0.0, 0.0};
	std::string WorldId;
	/** The byte count of visualsBlob. */
	std::size_t VisualsBytes = 0;
	/** The fields of PlayerSaveData. */
	std::string PlayerName;
	std::string CompanyName;
	std::string EntityClassName;
	std::string GuildCrestName;
	std::string Backstory;
	/** The names of the character creation databases, for example "skin04"; an empty text when the field is not there. */
	std::array<std::string, kAppearanceFieldCount> Appearance;
	std::vector<FEquipment> Equipment;

	/** @return The level of the screen: currentLevel + 1 (charactercreatebutton.luac SetCharacterInfo). */
	[[nodiscard]] std::int64_t DisplayLevel() const { return CurrentLevel + 1; }
	/** @return True for the gender name "Female" (the backend writer and the entity class PlayerFemale). */
	[[nodiscard]] bool IsFemale() const { return Appearance[0] == "Female"; }
};

/** Reads a menu document. On a status other than Ok, `Out` keeps the values that the reader got before the fault. */
[[nodiscard]] Status Decode(std::string_view Text, FDocument& Out);

// The steps, for the tests.
/** Base64 of the standard alphabet with padding. @return False for a character out of the alphabet or a bad length. */
[[nodiscard]] bool DecodeBase64(std::string_view Text, std::vector<std::uint8_t>& Out);
/** @return False for a bad zlib stream or an output larger than `Limit`. `bTooLarge` tells which. */
[[nodiscard]] bool Inflate(std::span<const std::uint8_t> Input, std::size_t Limit, std::vector<std::uint8_t>& Out, bool& bTooLarge);
/** Reads the JSON members of the document. `Visuals` gets the bytes of visualsBlob. */
[[nodiscard]] Status ReadJson(std::span<const std::uint8_t> Json, FDocument& Out, std::vector<std::uint8_t>& Visuals, bool& bHasVisuals);
/** Reads the PlayerSaveData object stream. */
[[nodiscard]] bool ReadSaveData(std::span<const std::uint8_t> Stream, FDocument& Out);

} // namespace swc::menu
