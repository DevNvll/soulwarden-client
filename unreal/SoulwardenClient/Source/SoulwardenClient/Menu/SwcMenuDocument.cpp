#include "Menu/SwcMenuDocument.h"

#include <cstdlib>
#include <cstring>

#include "zlib.h"

namespace swc::menu {
namespace {

// The binary object stream of the engine (big-endian): tag 0, stream version, then elements.
//   element := flags:u8 [name:u32 if 0x40] [classVersion:u8 if 0x80] typeId:16 bytes [value if 0x10] element* 0x00
//   value   := size bytes; the size is the low 3 bits of the flags, or with flag 0x20 a number of that many bytes.
// The name of a field is the CRC-32 of its name in lower case. The values below are the ones of the backend writer
// (state/creation_data.cpp) and of the create requests of the game (protocol notes 2.2.5).
constexpr std::uint8_t kStreamTagBinary = 0;
constexpr std::uint8_t kElementMark = 0x08;
constexpr std::uint8_t kElementHasValue = 0x10;
constexpr std::uint8_t kElementSizeField = 0x20;
constexpr std::uint8_t kElementHasName = 0x40;
constexpr std::uint8_t kElementHasVersion = 0x80;
constexpr std::uint8_t kElementSizeMask = 0x07;
constexpr std::size_t kTypeIdSize = 16;
using FTypeId = std::array<std::uint8_t, kTypeIdSize>;
/** A text value: {03AAAB3F-5C47-5A66-9EBC-D5FA4DB353C9}. */
constexpr FTypeId kTextType{0x03, 0xAA, 0xAB, 0x3F, 0x5C, 0x47, 0x5A, 0x66, 0x9E, 0xBC, 0xD5, 0xFA, 0x4D, 0xB3, 0x53, 0xC9};
/** The root, class PlayerSaveData: {144C7576-7FBA-4250-B0C8-F06803657D79}. */
constexpr FTypeId kSaveDataType{0x14, 0x4C, 0x75, 0x76, 0x7F, 0xBA, 0x42, 0x50, 0xB0, 0xC8, 0xF0, 0x68, 0x03, 0x65, 0x7D, 0x79};

constexpr std::array<std::uint32_t, kAppearanceFieldCount> kAppearanceFieldNames{
	0xC7470A42, // gender
	0xDA6FBBAF, // race
	0xA6C5AE93, // skinTone
	0xB0A87DBC, // hairstyle
	0x16F8DF7E, // facialHair
	0xE5438D3F, // hairColor
	0xF9CC2837, // facialHairColor
	0x7C94C553, // eyeColor
	0xC6E599A0, // faceMark
	0xD0B2EFE1, // scar
	0x0DEE4C6F, // tattoo
	0xBA24CC17, // tattooColor
};
constexpr std::uint32_t kBackstoryName = 0x7DC4476D;      // backstory
constexpr std::uint32_t kPlayerNameName = 0x6E6926DF;     // playerName
constexpr std::uint32_t kCompanyNameName = 0x17BBE394;    // companyName
constexpr std::uint32_t kEntityClassName = 0xE9F5E620;    // entityClassName
constexpr std::uint32_t kGuildCrestName = 0x04DFB561;     // guildCrestName
constexpr std::uint32_t kEquipmentName = 0xD338D583;      // equipment
constexpr std::uint32_t kPairName = 0x41405E39;           // element
constexpr std::uint32_t kPairKeyName = 0xA2756C5A;        // value1
constexpr std::uint32_t kPairValueName = 0x3B7C3DE0;      // value2
constexpr std::uint32_t kItemNameName = 0xF45D1CCE;       // itemName
constexpr std::uint32_t kAppearanceIdName = 0xCDCEA5F5;   // appearanceId
constexpr std::uint32_t kCrcValueName = 0x1D775834;       // value
constexpr std::uint32_t kDyeSettingsName = 0x17454143;    // dyeSettings
constexpr std::array<std::uint32_t, 4> kDyeChannelNames{
	0x51E3A083, // m_rColorId
	0x65B89FCC, // m_gColorId
	0x2D5891A8, // m_bColorId
	0xA3D7964B, // m_aColorId
};

/** One element of the object stream: its value is a span of the stream. */
struct FElement
{
	std::uint32_t Name = 0;
	FTypeId Type{};
	std::size_t ValueOffset = 0;
	std::size_t ValueSize = 0;
	bool bHasValue = false;
	std::vector<std::size_t> Children;
};

class FStreamReader
{
public:
	explicit FStreamReader(std::span<const std::uint8_t> InStream) : Stream(InStream) {}

	/** Reads the stream. @return False for a stream that is not in the form above, or out of the limits. */
	bool Read()
	{
		if (Stream.size() < 5 || Stream[0] != kStreamTagBinary)
		{
			return false;
		}
		Position = 5;
		// The stream holds elements up to its end mark.
		while (true)
		{
			if (Position >= Stream.size())
			{
				return false;
			}
			if (Stream[Position] == 0)
			{
				++Position;
				return Position == Stream.size();
			}
			std::size_t Index = 0;
			if (!ReadElement(1, Index))
			{
				return false;
			}
			Roots.push_back(Index);
		}
	}

	std::vector<FElement> Elements;
	std::vector<std::size_t> Roots;

	std::string_view Value(const FElement& Element) const
	{
		return std::string_view(reinterpret_cast<const char*>(Stream.data()) + Element.ValueOffset, Element.ValueSize);
	}

	/** @return The first child with the name, or nullptr. */
	const FElement* Child(const FElement& Parent, std::uint32_t Name) const
	{
		for (const std::size_t Index : Parent.Children)
		{
			if (Elements[Index].Name == Name)
			{
				return &Elements[Index];
			}
		}
		return nullptr;
	}

	/** @return True and the text of a child text field. */
	bool Text(const FElement& Parent, std::uint32_t Name, std::string& Out) const
	{
		const FElement* Field = Child(Parent, Name);
		if (Field == nullptr || Field->Type != kTextType || !Field->bHasValue)
		{
			return false;
		}
		Out.assign(Value(*Field));
		return true;
	}

private:
	bool Take(std::size_t Count)
	{
		if (Count > Stream.size() - Position)
		{
			return false;
		}
		Position += Count;
		return true;
	}

	bool ReadElement(std::size_t Depth, std::size_t& OutIndex)
	{
		if (Depth > kMaxElementDepth || Elements.size() >= kMaxElements || Position >= Stream.size())
		{
			return false;
		}
		const std::uint8_t Flags = Stream[Position++];
		if ((Flags & kElementMark) == 0)
		{
			return false;
		}
		FElement Element;
		if ((Flags & kElementHasName) != 0)
		{
			if (!Take(4))
			{
				return false;
			}
			const std::uint8_t* Bytes = Stream.data() + Position - 4;
			Element.Name = (std::uint32_t(Bytes[0]) << 24) | (std::uint32_t(Bytes[1]) << 16) | (std::uint32_t(Bytes[2]) << 8) | Bytes[3];
		}
		if ((Flags & kElementHasVersion) != 0 && !Take(1))
		{
			return false;
		}
		if (!Take(kTypeIdSize))
		{
			return false;
		}
		std::memcpy(Element.Type.data(), Stream.data() + Position - kTypeIdSize, kTypeIdSize);
		if ((Flags & kElementHasValue) != 0)
		{
			std::size_t Size = Flags & kElementSizeMask;
			if ((Flags & kElementSizeField) != 0)
			{
				// The size has `Size` bytes, big-endian; four bytes at most.
				const std::size_t Count = Size;
				if (Count == 0 || Count > 4 || !Take(Count))
				{
					return false;
				}
				Size = 0;
				for (std::size_t I = 0; I < Count; ++I)
				{
					Size = (Size << 8) | Stream[Position - Count + I];
				}
			}
			if (Size > kMaxVisualsBytes)
			{
				return false;
			}
			Element.ValueOffset = Position;
			Element.ValueSize = Size;
			Element.bHasValue = true;
			if (!Take(Size))
			{
				return false;
			}
		}
		OutIndex = Elements.size();
		Elements.push_back(std::move(Element));
		// The children, up to the end mark of this element.
		while (true)
		{
			if (Position >= Stream.size())
			{
				return false;
			}
			if (Stream[Position] == 0)
			{
				++Position;
				return true;
			}
			std::size_t Child = 0;
			if (!ReadElement(Depth + 1, Child))
			{
				return false;
			}
			Elements[OutIndex].Children.push_back(Child);
		}
	}

	std::span<const std::uint8_t> Stream;
	std::size_t Position = 0;
};

/** A reader of the JSON document that keeps the bytes of each text (the writer of the game copies high bytes). */
class FJsonReader
{
public:
	explicit FJsonReader(std::span<const std::uint8_t> InJson) : Json(InJson) {}

	Status Read(FDocument& Out, std::vector<std::uint8_t>& Visuals, bool& bHasVisuals)
	{
		bHasVisuals = false;
		SkipSpace();
		if (!Expect('{'))
		{
			return Status::BadJson;
		}
		SkipSpace();
		if (Peek() == '}')
		{
			++Position;
			return End() ? Status::Ok : Status::BadJson;
		}
		while (true)
		{
			std::string Key;
			SkipSpace();
			if (!ReadString(Key, false) || (SkipSpace(), !Expect(':')))
			{
				return Status::BadJson;
			}
			SkipSpace();
			bool bRead = true;
			if (Key == "visualsBlob" && Peek() == '"')
			{
				std::string Bytes;
				bRead = ReadString(Bytes, true) && Bytes.size() <= kMaxVisualsBytes;
				Visuals.assign(Bytes.begin(), Bytes.end());
				bHasVisuals = bRead;
			}
			else if (Key == "worldId" && Peek() == '"')
			{
				bRead = ReadString(Out.WorldId, false);
			}
			else if (Key == "currency")
			{
				bRead = ReadNumber(Out.Currency);
			}
			else if (Key == "currentLevel")
			{
				double Level = 0.0;
				bRead = ReadNumber(Level) && Level >= 0.0 && Level <= 1.0e6;
				Out.CurrentLevel = static_cast<std::int64_t>(Level);
			}
			else if (Key == "position" && Peek() == '{')
			{
				bRead = ReadPosition(Out);
			}
			else
			{
				bRead = SkipValue(1);
			}
			if (!bRead)
			{
				return Status::BadJson;
			}
			SkipSpace();
			if (Peek() == ',')
			{
				++Position;
				continue;
			}
			if (!Expect('}'))
			{
				return Status::BadJson;
			}
			return End() ? Status::Ok : Status::BadJson;
		}
	}

private:
	static constexpr std::size_t kMaxDepth = 32;

	int Peek() const { return Position < Json.size() ? Json[Position] : -1; }

	bool Expect(char Character)
	{
		if (Peek() != static_cast<unsigned char>(Character))
		{
			return false;
		}
		++Position;
		return true;
	}

	void SkipSpace()
	{
		while (Position < Json.size() && (Json[Position] == ' ' || Json[Position] == '\t' || Json[Position] == '\n' || Json[Position] == '\r'))
		{
			++Position;
		}
	}

	bool End()
	{
		SkipSpace();
		// The inflated document can end with a zero byte (a C text).
		while (Position < Json.size() && Json[Position] == 0)
		{
			++Position;
		}
		return Position == Json.size();
	}

	static int Hex(std::uint8_t Character)
	{
		if (Character >= '0' && Character <= '9')
		{
			return Character - '0';
		}
		if (Character >= 'a' && Character <= 'f')
		{
			return Character - 'a' + 10;
		}
		if (Character >= 'A' && Character <= 'F')
		{
			return Character - 'A' + 10;
		}
		return -1;
	}

	static void AppendUtf8(std::string& Out, std::uint32_t Code)
	{
		if (Code < 0x80)
		{
			Out.push_back(static_cast<char>(Code));
		}
		else if (Code < 0x800)
		{
			Out.push_back(static_cast<char>(0xC0 | (Code >> 6)));
			Out.push_back(static_cast<char>(0x80 | (Code & 0x3F)));
		}
		else
		{
			Out.push_back(static_cast<char>(0xE0 | (Code >> 12)));
			Out.push_back(static_cast<char>(0x80 | ((Code >> 6) & 0x3F)));
			Out.push_back(static_cast<char>(0x80 | (Code & 0x3F)));
		}
	}

	/**
	 * Reads a JSON text. `bBytes` true: an escape \u00XX gives the byte XX (the writer of the game writes a byte below 0x20 so),
	 * and a larger escape is a fault. Else an escape gives UTF-8. Each other byte stays as it is.
	 */
	bool ReadString(std::string& Out, bool bBytes)
	{
		if (!Expect('"'))
		{
			return false;
		}
		Out.clear();
		while (Position < Json.size())
		{
			const std::uint8_t Byte = Json[Position++];
			if (Byte == '"')
			{
				return true;
			}
			if (Byte != '\\')
			{
				Out.push_back(static_cast<char>(Byte));
				continue;
			}
			if (Position >= Json.size())
			{
				return false;
			}
			const std::uint8_t Escape = Json[Position++];
			switch (Escape)
			{
			case '"': Out.push_back('"'); break;
			case '\\': Out.push_back('\\'); break;
			case '/': Out.push_back('/'); break;
			case 'b': Out.push_back('\b'); break;
			case 'f': Out.push_back('\f'); break;
			case 'n': Out.push_back('\n'); break;
			case 'r': Out.push_back('\r'); break;
			case 't': Out.push_back('\t'); break;
			case 'u':
			{
				if (Json.size() - Position < 4)
				{
					return false;
				}
				std::uint32_t Code = 0;
				for (int I = 0; I < 4; ++I)
				{
					const int Digit = Hex(Json[Position++]);
					if (Digit < 0)
					{
						return false;
					}
					Code = (Code << 4) | static_cast<std::uint32_t>(Digit);
				}
				if (bBytes)
				{
					if (Code > 0xFF)
					{
						return false;
					}
					Out.push_back(static_cast<char>(Code));
				}
				else
				{
					AppendUtf8(Out, Code);
				}
				break;
			}
			default:
				return false;
			}
		}
		return false;
	}

	bool ReadNumber(double& Out)
	{
		const std::size_t Start = Position;
		while (Position < Json.size() && std::strchr("+-0123456789.eE", Json[Position]) != nullptr && Json[Position] != 0)
		{
			++Position;
		}
		if (Position == Start || Position - Start > 64)
		{
			return false;
		}
		const std::string Text(reinterpret_cast<const char*>(Json.data()) + Start, Position - Start);
		char* End = nullptr;
		Out = std::strtod(Text.c_str(), &End);
		return End == Text.c_str() + Text.size();
	}

	bool ReadPosition(FDocument& Out)
	{
		if (!Expect('{'))
		{
			return false;
		}
		SkipSpace();
		if (Expect('}'))
		{
			return true;
		}
		while (true)
		{
			std::string Key;
			SkipSpace();
			if (!ReadString(Key, false) || (SkipSpace(), !Expect(':')))
			{
				return false;
			}
			SkipSpace();
			const int Axis = Key == "x" ? 0 : Key == "y" ? 1 : Key == "z" ? 2 : -1;
			if (!(Axis >= 0 ? ReadNumber(Out.Position[Axis]) : SkipValue(2)))
			{
				return false;
			}
			SkipSpace();
			if (Expect(','))
			{
				continue;
			}
			return Expect('}');
		}
	}

	bool SkipValue(std::size_t Depth)
	{
		if (Depth > kMaxDepth)
		{
			return false;
		}
		SkipSpace();
		const int Character = Peek();
		if (Character == '"')
		{
			std::string Ignored;
			return ReadString(Ignored, true);
		}
		if (Character == '{' || Character == '[')
		{
			const char Close = Character == '{' ? '}' : ']';
			++Position;
			SkipSpace();
			if (Expect(Close))
			{
				return true;
			}
			while (true)
			{
				SkipSpace();
				if (Character == '{')
				{
					std::string Key;
					if (!ReadString(Key, false) || (SkipSpace(), !Expect(':')))
					{
						return false;
					}
				}
				if (!SkipValue(Depth + 1))
				{
					return false;
				}
				SkipSpace();
				if (Expect(','))
				{
					continue;
				}
				return Expect(Close);
			}
		}
		for (const char* Word : {"true", "false", "null"})
		{
			const std::size_t Length = std::strlen(Word);
			if (Json.size() - Position >= Length && std::memcmp(Json.data() + Position, Word, Length) == 0)
			{
				Position += Length;
				return true;
			}
		}
		double Ignored = 0.0;
		return ReadNumber(Ignored);
	}

	std::span<const std::uint8_t> Json;
	std::size_t Position = 0;
};

} // namespace

const char* StatusText(Status Value)
{
	switch (Value)
	{
	case Status::Ok: return "ok";
	case Status::Empty: return "no text";
	case Status::TextTooLarge: return "the text is too large";
	case Status::BadBase64: return "bad base64";
	case Status::BadZlib: return "bad zlib stream";
	case Status::DocumentTooLarge: return "the document is too large";
	case Status::BadJson: return "bad JSON document";
	case Status::NoVisuals: return "no visualsBlob";
	case Status::BadVisuals: return "bad PlayerSaveData stream";
	}
	return "?";
}

bool DecodeBase64(std::string_view Text, std::vector<std::uint8_t>& Out)
{
	Out.clear();
	if (Text.size() % 4 != 0)
	{
		return false;
	}
	const auto Value = [](char Character) -> int
	{
		if (Character >= 'A' && Character <= 'Z')
		{
			return Character - 'A';
		}
		if (Character >= 'a' && Character <= 'z')
		{
			return Character - 'a' + 26;
		}
		if (Character >= '0' && Character <= '9')
		{
			return Character - '0' + 52;
		}
		if (Character == '+')
		{
			return 62;
		}
		if (Character == '/')
		{
			return 63;
		}
		return -1;
	};
	Out.reserve(Text.size() / 4 * 3);
	for (std::size_t I = 0; I < Text.size(); I += 4)
	{
		const bool bLast = I + 4 == Text.size();
		const int Padding = bLast ? (Text[I + 3] == '=') + (Text[I + 2] == '=') : 0;
		if (Padding == 1 && Text[I + 2] == '=')
		{
			return false;
		}
		std::uint32_t Group = 0;
		for (int J = 0; J < 4; ++J)
		{
			const int Digit = J >= 4 - Padding ? 0 : Value(Text[I + J]);
			if (Digit < 0)
			{
				return false;
			}
			Group = (Group << 6) | static_cast<std::uint32_t>(Digit);
		}
		Out.push_back(static_cast<std::uint8_t>(Group >> 16));
		if (Padding < 2)
		{
			Out.push_back(static_cast<std::uint8_t>(Group >> 8));
		}
		if (Padding < 1)
		{
			Out.push_back(static_cast<std::uint8_t>(Group));
		}
	}
	return true;
}

bool Inflate(std::span<const std::uint8_t> Input, std::size_t Limit, std::vector<std::uint8_t>& Out, bool& bTooLarge)
{
	Out.clear();
	bTooLarge = false;
	z_stream Stream{};
	if (inflateInit(&Stream) != Z_OK)
	{
		return false;
	}
	Stream.next_in = const_cast<Bytef*>(Input.data());
	Stream.avail_in = static_cast<uInt>(Input.size());
	std::array<std::uint8_t, 16 * 1024> Chunk{};
	int Result = Z_OK;
	while (Result == Z_OK)
	{
		Stream.next_out = Chunk.data();
		Stream.avail_out = static_cast<uInt>(Chunk.size());
		Result = inflate(&Stream, Z_NO_FLUSH);
		if (Result != Z_OK && Result != Z_STREAM_END)
		{
			break;
		}
		const std::size_t Produced = Chunk.size() - Stream.avail_out;
		if (Out.size() + Produced > Limit)
		{
			bTooLarge = true;
			break;
		}
		Out.insert(Out.end(), Chunk.data(), Chunk.data() + Produced);
		// No progress with input left: the stream is cut.
		if (Result == Z_OK && Produced == 0 && Stream.avail_in == 0)
		{
			break;
		}
	}
	inflateEnd(&Stream);
	return Result == Z_STREAM_END && !bTooLarge;
}

Status ReadJson(std::span<const std::uint8_t> Json, FDocument& Out, std::vector<std::uint8_t>& Visuals, bool& bHasVisuals)
{
	FJsonReader Reader(Json);
	return Reader.Read(Out, Visuals, bHasVisuals);
}

bool ReadSaveData(std::span<const std::uint8_t> Stream, FDocument& Out)
{
	FStreamReader Reader(Stream);
	if (!Reader.Read() || Reader.Roots.size() != 1)
	{
		return false;
	}
	const FElement& Root = Reader.Elements[Reader.Roots[0]];
	if (Root.Type != kSaveDataType)
	{
		return false;
	}
	Reader.Text(Root, kPlayerNameName, Out.PlayerName);
	Reader.Text(Root, kCompanyNameName, Out.CompanyName);
	Reader.Text(Root, kEntityClassName, Out.EntityClassName);
	Reader.Text(Root, kGuildCrestName, Out.GuildCrestName);
	Reader.Text(Root, kBackstoryName, Out.Backstory);
	for (std::size_t Field = 0; Field < kAppearanceFieldCount; ++Field)
	{
		Reader.Text(Root, kAppearanceFieldNames[Field], Out.Appearance[Field]);
	}
	Out.Equipment.clear();
	if (const FElement* Map = Reader.Child(Root, kEquipmentName))
	{
		for (const std::size_t Index : Map->Children)
		{
			const FElement& Pair = Reader.Elements[Index];
			if (Pair.Name != kPairName)
			{
				continue;
			}
			if (Out.Equipment.size() >= kMaxEquipmentEntries)
			{
				return false;
			}
			FEquipment Entry;
			const FElement* Value = Reader.Child(Pair, kPairValueName);
			if (!Reader.Text(Pair, kPairKeyName, Entry.Slot) || Value == nullptr)
			{
				return false;
			}
			Reader.Text(*Value, kItemNameName, Entry.ItemName);
			if (const FElement* Id = Reader.Child(*Value, kAppearanceIdName))
			{
				const FElement* Crc = Reader.Child(*Id, kCrcValueName);
				if (Crc == nullptr || !Crc->bHasValue || Crc->ValueSize != 4)
				{
					return false;
				}
				const std::string_view Bytes = Reader.Value(*Crc);
				for (const char Byte : Bytes)
				{
					Entry.AppearanceId = (Entry.AppearanceId << 8) | static_cast<std::uint8_t>(Byte);
				}
			}
			if (const FElement* Dyes = Reader.Child(*Value, kDyeSettingsName))
			{
				Entry.bHasDyes = true;
				for (std::size_t Channel = 0; Channel < kDyeChannelNames.size(); ++Channel)
				{
					const FElement* Dye = Reader.Child(*Dyes, kDyeChannelNames[Channel]);
					if (Dye != nullptr && Dye->bHasValue && Dye->ValueSize == 1)
					{
						Entry.Dyes[Channel] = static_cast<std::uint8_t>(Reader.Value(*Dye)[0]);
					}
				}
			}
			Out.Equipment.push_back(std::move(Entry));
		}
	}
	return true;
}

Status Decode(std::string_view Text, FDocument& Out)
{
	Out = FDocument();
	if (Text.empty())
	{
		return Status::Empty;
	}
	if (Text.size() > kMaxTextBytes)
	{
		return Status::TextTooLarge;
	}
	std::vector<std::uint8_t> Compressed;
	if (!DecodeBase64(Text, Compressed))
	{
		return Status::BadBase64;
	}
	std::vector<std::uint8_t> Json;
	bool bTooLarge = false;
	if (!Inflate(Compressed, kMaxDocumentBytes, Json, bTooLarge))
	{
		return bTooLarge ? Status::DocumentTooLarge : Status::BadZlib;
	}
	std::vector<std::uint8_t> Visuals;
	bool bHasVisuals = false;
	const Status JsonStatus = ReadJson(Json, Out, Visuals, bHasVisuals);
	if (JsonStatus != Status::Ok)
	{
		return JsonStatus;
	}
	if (!bHasVisuals)
	{
		return Status::NoVisuals;
	}
	Out.VisualsBytes = Visuals.size();
	if (!ReadSaveData(Visuals, Out))
	{
		return Status::BadVisuals;
	}
	return Status::Ok;
}

} // namespace swc::menu
