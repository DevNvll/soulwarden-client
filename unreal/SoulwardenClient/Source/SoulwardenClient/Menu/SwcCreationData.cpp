#include "Menu/SwcCreationData.h"

#include "zlib.h"

namespace swc::menu {
namespace {

// The binary object stream (the form of the game data files and of the menu document; research character-creation.md 3.2).
constexpr std::uint8_t kStreamTagBinary = 0;
constexpr std::uint32_t kStreamVersion = 3;
constexpr std::uint8_t kElementMark = 0x08;
constexpr std::uint8_t kElementEnd = 0;
constexpr std::uint8_t kElementHasValue = 0x10;
constexpr std::uint8_t kElementSizeField = 0x20;
constexpr std::uint8_t kElementHasName = 0x40;
constexpr std::uint8_t kElementHasVersion = 0x80;
constexpr std::size_t kMaxSizeInFlags = 7;

using FTypeId = std::array<std::uint8_t, 16>;
// The type ids of the create request of the client (protocol notes 2.2.5).
constexpr FTypeId kTextType{0x03, 0xAA, 0xAB, 0x3F, 0x5C, 0x47, 0x5A, 0x66, 0x9E, 0xBC, 0xD5, 0xFA, 0x4D, 0xB3, 0x53, 0xC9};
constexpr FTypeId kSaveDataType{0x14, 0x4C, 0x75, 0x76, 0x7F, 0xBA, 0x42, 0x50, 0xB0, 0xC8, 0xF0, 0x68, 0x03, 0x65, 0x7D, 0x79};
constexpr std::uint8_t kSaveDataVersion = 3;
constexpr FTypeId kByteType{0x72, 0xB9, 0x40, 0x9A, 0x7D, 0x1A, 0x48, 0x31, 0x9C, 0xFE, 0xFC, 0xB3, 0xFA, 0xDD, 0x34, 0x26};
constexpr FTypeId kAppearanceDataType{0x7E, 0xF0, 0xE8, 0x08, 0xE5, 0xB8, 0x51, 0x16, 0xBB, 0x2B, 0x00, 0xA1, 0x61, 0x0B, 0xF1, 0xD3};
constexpr FTypeId kEquipmentType{0x44, 0xC9, 0x3A, 0x82, 0x18, 0x97, 0x56, 0xD6, 0x8C, 0xF7, 0x4B, 0xC0, 0x28, 0x99, 0x15, 0xDE};
constexpr FTypeId kEquipmentPairType{0xEF, 0xF0, 0x30, 0xFB, 0x33, 0x58, 0x56, 0x13, 0x87, 0x40, 0x93, 0x04, 0x13, 0x34, 0xC2, 0x06};
constexpr FTypeId kEquipmentSaveDataType{0x90, 0x3C, 0x97, 0x03, 0xA7, 0x91, 0x45, 0x4D, 0xA5, 0x76, 0x45, 0xED, 0xE8, 0x40, 0x5B, 0x70};
constexpr std::uint8_t kEquipmentSaveDataVersion = 1;
constexpr FTypeId kCrc32Type{0x9F, 0x4E, 0x06, 0x2E, 0x06, 0xA0, 0x46, 0xD4, 0x85, 0xDF, 0xE0, 0xDA, 0x96, 0x46, 0x7D, 0x3A};
constexpr FTypeId kCrcValueType{0x43, 0xDA, 0x90, 0x6B, 0x7D, 0xEF, 0x4C, 0xA8, 0x97, 0x90, 0x85, 0x41, 0x06, 0xD3, 0xF9, 0x83};
constexpr FTypeId kDyeDataType{0x2F, 0xA6, 0x19, 0xC9, 0xD7, 0x1E, 0x4E, 0x88, 0x9B, 0xE9, 0x16, 0x05, 0x6F, 0x7E, 0x38, 0xFA};
constexpr std::uint8_t kDyeDataVersion = 3;
constexpr FTypeId kDyeChannelType{0xFA, 0xAD, 0x74, 0x88, 0x08, 0xC8, 0x5F, 0xCB, 0x8F, 0xEE, 0xAB, 0x1E, 0xBE, 0x47, 0x26, 0xB3};

// The field names are the CRC-32 of the lower-case name (big-endian in the stream).
constexpr std::array<std::string_view, 4> kDyeChannels{"m_rColorId", "m_gColorId", "m_bColorId", "m_aColorId"};
constexpr std::string_view kMaleEntityClass = "PlayerMale";
constexpr std::string_view kFemaleEntityClass = "PlayerFemale";

void PushU32(std::vector<std::uint8_t>& Stream, std::uint32_t Value)
{
	for (int Shift = 24; Shift >= 0; Shift -= 8)
	{
		Stream.push_back(static_cast<std::uint8_t>(Value >> Shift));
	}
}

/** Starts an element: the flags, the name (0: none), the class version (0: none), the type id, and the value (null: no value). */
void BeginElement(std::vector<std::uint8_t>& Stream, std::uint32_t Name, std::uint8_t ClassVersion, const FTypeId& Type,
                  const std::string_view* Value = nullptr)
{
	std::uint8_t Flags = kElementMark;
	Flags |= Name != 0 ? kElementHasName : 0;
	Flags |= ClassVersion != 0 ? kElementHasVersion : 0;
	std::size_t Width = 0;
	if (Value != nullptr)
	{
		Flags |= kElementHasValue;
		if (Value->size() <= kMaxSizeInFlags)
		{
			Flags |= static_cast<std::uint8_t>(Value->size());
		}
		else
		{
			// A size field of 1, 2, or 4 bytes (big-endian).
			Width = Value->size() <= 0xFF ? 1 : (Value->size() <= 0xFFFF ? 2 : 4);
			Flags |= kElementSizeField | static_cast<std::uint8_t>(Width);
		}
	}
	Stream.push_back(Flags);
	if (Name != 0)
	{
		PushU32(Stream, Name);
	}
	if (ClassVersion != 0)
	{
		Stream.push_back(ClassVersion);
	}
	Stream.insert(Stream.end(), Type.begin(), Type.end());
	if (Value != nullptr)
	{
		for (std::size_t Byte = Width; Byte > 0; --Byte)
		{
			Stream.push_back(static_cast<std::uint8_t>(Value->size() >> (8 * (Byte - 1))));
		}
		Stream.insert(Stream.end(), Value->begin(), Value->end());
	}
}

void TextField(std::vector<std::uint8_t>& Stream, std::string_view Name, std::string_view Text)
{
	BeginElement(Stream, NameCrc(Name), 0, kTextType, &Text);
	Stream.push_back(kElementEnd);
}

/** One pair of the map `equipment`: the key `value1` and the EquipmentSaveData `value2` (itemName empty, no dye). */
void EquipmentEntry(std::vector<std::uint8_t>& Stream, const FCreationEquipment& Entry)
{
	BeginElement(Stream, NameCrc("element"), 0, kEquipmentPairType);
	TextField(Stream, "value1", Entry.Slot);
	BeginElement(Stream, NameCrc("value2"), kEquipmentSaveDataVersion, kEquipmentSaveDataType);
	TextField(Stream, "itemName", "");
	BeginElement(Stream, NameCrc("appearanceId"), 0, kCrc32Type);
	const char Bytes[4] = {static_cast<char>(Entry.AppearanceId >> 24), static_cast<char>(Entry.AppearanceId >> 16),
	                       static_cast<char>(Entry.AppearanceId >> 8), static_cast<char>(Entry.AppearanceId)};
	const std::string_view Crc(Bytes, 4);
	BeginElement(Stream, NameCrc("value"), 0, kCrcValueType, &Crc);
	Stream.push_back(kElementEnd); // value
	Stream.push_back(kElementEnd); // appearanceId
	// A new character has no dye: the palette index 0 in each channel (the create function of the client).
	BeginElement(Stream, NameCrc("dyeSettings"), kDyeDataVersion, kDyeDataType);
	const std::string_view Zero("\0", 1);
	for (const std::string_view Channel : kDyeChannels)
	{
		BeginElement(Stream, NameCrc(Channel), 0, kDyeChannelType, &Zero);
		Stream.push_back(kElementEnd);
	}
	Stream.push_back(kElementEnd); // dyeSettings
	Stream.push_back(kElementEnd); // value2
	Stream.push_back(kElementEnd); // the pair
}

/** Adds bytes as a JSON string with the rules of the JSON writer of the client. */
void AppendJsonBytes(std::string& Document, const std::vector<std::uint8_t>& Bytes)
{
	constexpr char kHex[] = "0123456789ABCDEF";
	Document.push_back('"');
	for (const std::uint8_t Byte : Bytes)
	{
		switch (Byte)
		{
		case 0x08: Document += "\\b"; break;
		case 0x0C: Document += "\\f"; break;
		case 0x0A: Document += "\\n"; break;
		case 0x0D: Document += "\\r"; break;
		case 0x09: Document += "\\t"; break;
		case '"': Document += "\\\""; break;
		case '\\': Document += "\\\\"; break;
		default:
			if (Byte < 0x20)
			{
				Document += "\\u00";
				Document.push_back(kHex[Byte >> 4]);
				Document.push_back(kHex[Byte & 0x0F]);
			}
			else
			{
				Document.push_back(static_cast<char>(Byte));
			}
		}
	}
	Document.push_back('"');
}

} // namespace

std::uint32_t NameCrc(std::string_view Name)
{
	std::string Lower(Name);
	for (char& Character : Lower)
	{
		if (Character >= 'A' && Character <= 'Z')
		{
			Character = static_cast<char>(Character - 'A' + 'a');
		}
	}
	return static_cast<std::uint32_t>(crc32(0L, reinterpret_cast<const Bytef*>(Lower.data()), static_cast<uInt>(Lower.size())));
}

std::vector<std::uint8_t> WriteSaveData(const FCreation& Values)
{
	std::vector<std::uint8_t> Stream{kStreamTagBinary};
	PushU32(Stream, kStreamVersion);
	BeginElement(Stream, 0, kSaveDataVersion, kSaveDataType);
	TextField(Stream, "playerName", Values.PlayerName);
	TextField(Stream, "companyName", Values.CompanyName);
	TextField(Stream, "entityClassName", Values.Appearance[0] == "Female" ? kFemaleEntityClass : kMaleEntityClass);
	// The client writes the console variable g_debugPlayerGuildCrest: empty.
	TextField(Stream, "guildCrestName", "");
	for (std::size_t Field = 0; Field < kAppearanceFieldCount; ++Field)
	{
		TextField(Stream, kAppearanceFields[Field], Values.Appearance[Field]);
	}
	TextField(Stream, "backstory", Values.Backstory);
	const char Faction = static_cast<char>(Values.StartingFaction);
	const std::string_view FactionByte(&Faction, 1);
	BeginElement(Stream, NameCrc("startingFaction"), 0, kByteType, &FactionByte);
	Stream.push_back(kElementEnd);
	// The create function does not set appearanceData: an empty list.
	BeginElement(Stream, NameCrc("appearanceData"), 0, kAppearanceDataType);
	Stream.push_back(kElementEnd);
	BeginElement(Stream, NameCrc("equipment"), 0, kEquipmentType);
	for (const FCreationEquipment& Entry : Values.Equipment)
	{
		EquipmentEntry(Stream, Entry);
	}
	Stream.push_back(kElementEnd); // equipment
	Stream.push_back(kElementEnd); // the root element
	Stream.push_back(kElementEnd); // the stream
	return Stream;
}

std::string WriteCreationDocument(const FCreation& Values)
{
	// The create function sets currency 0, the position 0, and currentLevel 0 (research character-creation.md 3.1).
	std::string Document = R"({"currency":0,"visualsBlob":)";
	AppendJsonBytes(Document, WriteSaveData(Values));
	Document += R"(,"position":{"x":0.0,"y":0.0,"z":0.0},"currentLevel":0,"worldId":")";
	// A world id is a UUID text: it needs no escape.
	Document += Values.WorldId;
	Document += "\"}";
	return Document;
}

std::string EncodeBase64(const std::vector<std::uint8_t>& Bytes)
{
	constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string Out;
	Out.reserve((Bytes.size() + 2) / 3 * 4);
	for (std::size_t Index = 0; Index < Bytes.size(); Index += 3)
	{
		const bool bSecond = Index + 1 < Bytes.size();
		const bool bThird = Index + 2 < Bytes.size();
		const std::uint32_t Value = (std::uint32_t(Bytes[Index]) << 16) | (bSecond ? std::uint32_t(Bytes[Index + 1]) << 8 : 0u)
			| (bThird ? std::uint32_t(Bytes[Index + 2]) : 0u);
		Out.push_back(kAlphabet[(Value >> 18) & 63]);
		Out.push_back(kAlphabet[(Value >> 12) & 63]);
		Out.push_back(bSecond ? kAlphabet[(Value >> 6) & 63] : '=');
		Out.push_back(bThird ? kAlphabet[Value & 63] : '=');
	}
	return Out;
}

std::string EncodeCreation(const FCreation& Values)
{
	const std::string Document = WriteCreationDocument(Values);
	uLongf Size = compressBound(static_cast<uLong>(Document.size()));
	std::vector<std::uint8_t> Compressed(Size);
	// The level 9: the zlib header 78 DA of the create requests of the client.
	if (compress2(Compressed.data(), &Size, reinterpret_cast<const Bytef*>(Document.data()), static_cast<uLong>(Document.size()),
	              Z_BEST_COMPRESSION) != Z_OK)
	{
		return std::string();
	}
	Compressed.resize(Size);
	return EncodeBase64(Compressed);
}

} // namespace swc::menu
