#include "Menu/SwcNameRule.h"

#include <utility>

namespace swc::menu {
namespace {

/** The ranges above U+007F of the pattern of the client (protocol notes 2.2.2). */
constexpr std::pair<std::uint32_t, std::uint32_t> kRanges[] = {
	{0x00C0, 0x00D6}, {0x00D8, 0x00F6}, {0x00F8, 0x0108}, {0x010A, 0x0124}, {0x0126, 0x0127}, {0x012A, 0x012B},
	{0x012D, 0x0133}, {0x0136, 0x014D}, {0x0150, 0x0167}, {0x0169, 0x016B}, {0x016D, 0x0174}, {0x0176, 0x017E},
};

/** Reads one UTF-8 character at `Index`. @return False for a fault (the code point is then not read). */
bool NextCodePoint(std::string_view Text, std::size_t& Index, std::uint32_t& Out)
{
	const auto Byte = [&Text](std::size_t At) { return static_cast<std::uint8_t>(Text[At]); };
	const std::uint8_t Lead = Byte(Index);
	std::size_t Count = 0;
	if (Lead < 0x80)
	{
		Out = Lead;
		Count = 0;
	}
	else if ((Lead & 0xE0) == 0xC0)
	{
		Out = Lead & 0x1F;
		Count = 1;
	}
	else if ((Lead & 0xF0) == 0xE0)
	{
		Out = Lead & 0x0F;
		Count = 2;
	}
	else if ((Lead & 0xF8) == 0xF0)
	{
		Out = Lead & 0x07;
		Count = 3;
	}
	else
	{
		return false;
	}
	for (std::size_t Next = 1; Next <= Count; ++Next)
	{
		if (Index + Next >= Text.size() || (Byte(Index + Next) & 0xC0) != 0x80)
		{
			return false;
		}
		Out = (Out << 6) | (Byte(Index + Next) & 0x3F);
	}
	Index += Count + 1;
	return true;
}

bool IsWhiteSpace(std::uint32_t CodePoint)
{
	return CodePoint == ' ' || (CodePoint >= 0x09 && CodePoint <= 0x0D) || CodePoint == 0xA0;
}

} // namespace

bool IsNameCharacter(std::uint32_t CodePoint)
{
	if ((CodePoint >= 'a' && CodePoint <= 'z') || (CodePoint >= 'A' && CodePoint <= 'Z') || (CodePoint >= '0' && CodePoint <= '9'))
	{
		return true;
	}
	if (CodePoint == ' ' || CodePoint == '\'' || CodePoint == ',' || CodePoint == '-' || CodePoint == '.')
	{
		return true;
	}
	for (const auto& [Low, High] : kRanges)
	{
		if (CodePoint >= Low && CodePoint <= High)
		{
			return true;
		}
	}
	return false;
}

ENameCheck CheckName(std::string_view Utf8)
{
	std::u32string Points;
	bool bFault = false;
	for (std::size_t Index = 0; Index < Utf8.size();)
	{
		std::uint32_t Point = 0;
		if (!NextCodePoint(Utf8, Index, Point))
		{
			bFault = true;
			break;
		}
		Points.push_back(Point);
	}
	// A UTF-8 fault does not come from the text field (UTF-16): an invalid character, before the length.
	if (bFault)
	{
		return ENameCheck::InvalidCharacters;
	}
	// The length in UTF-16 units: a character above U+FFFF is two units.
	std::size_t Units = 0;
	for (const char32_t Point : Points)
	{
		Units += Point > 0xFFFF ? 2 : 1;
	}
	if (Units < kNameMinUnits)
	{
		return ENameCheck::TooShort;
	}
	if (Units > kNameMaxUnits)
	{
		return ENameCheck::TooLong;
	}
	if (IsWhiteSpace(Points.front()) || IsWhiteSpace(Points.back()))
	{
		return ENameCheck::InvalidCharacters;
	}
	for (std::size_t Index = 1; Index < Points.size(); ++Index)
	{
		if (Points[Index] == ' ' && Points[Index - 1] == ' ')
		{
			return ENameCheck::MultipleSpaces;
		}
	}
	std::size_t Digits = 0;
	for (const char32_t Point : Points)
	{
		if (!IsNameCharacter(Point))
		{
			return ENameCheck::InvalidCharacters;
		}
		Digits += Point >= '0' && Point <= '9';
	}
	return Digits > kNameMaxDigits ? ENameCheck::InvalidCharacters : ENameCheck::Valid;
}

std::string ReadErrorName(std::string_view HeaderValue, std::string_view Body)
{
	if (!HeaderValue.empty())
	{
		return std::string(HeaderValue.substr(0, HeaderValue.find(':')));
	}
	// The string member "__type" of the body (a flat search: the body is a small JSON object of the gateway).
	constexpr std::string_view kMember = "\"__type\"";
	const std::size_t Member = Body.find(kMember);
	if (Member == std::string_view::npos)
	{
		return std::string();
	}
	std::size_t At = Body.find_first_not_of(" \t\r\n", Member + kMember.size());
	if (At == std::string_view::npos || Body[At] != ':')
	{
		return std::string();
	}
	At = Body.find_first_not_of(" \t\r\n", At + 1);
	if (At == std::string_view::npos || Body[At] != '"')
	{
		return std::string();
	}
	const std::size_t End = Body.find('"', At + 1);
	if (End == std::string_view::npos)
	{
		return std::string();
	}
	std::string_view Name = Body.substr(At + 1, End - At - 1);
	if (const std::size_t Hash = Name.find('#'); Hash != std::string_view::npos)
	{
		Name = Name.substr(Hash + 1);
	}
	return std::string(Name);
}

std::string ErrorTextKey(std::string_view ErrorName)
{
	return "@mm_loginservices_" + std::string(ErrorName);
}

ENameValidation ReadValidateReply(const FGatewayReply& Reply, std::string& TextKey)
{
	TextKey.clear();
	if (Reply.Status == 200)
	{
		return ENameValidation::Valid;
	}
	if (Reply.ErrorName == "NameNotAvailableError")
	{
		TextKey = ErrorTextKey(Reply.ErrorName);
		return ENameValidation::NotAvailable;
	}
	if (Reply.ErrorName == "NameLengthError" || Reply.ErrorName == "NameValidationError" || Reply.ErrorName == "NameUsingBadWordsError")
	{
		TextKey = ErrorTextKey(Reply.ErrorName);
		return ENameValidation::Refused;
	}
	// No reply or no known name: "This name cannot be used." in the field [I] (the native popup of a failed request is not read).
	TextKey = "@mm_invalidname_unknown";
	return ENameValidation::Failed;
}

FCreateResult ReadCreateReply(const FGatewayReply& Reply, std::string_view CharacterId)
{
	FCreateResult Result;
	if (Reply.Status == 200 && !CharacterId.empty())
	{
		Result.CharacterId = std::string(CharacterId);
		return Result;
	}
	Result.Title = ErrorTextKey("CreateCharacterFailed");
	Result.Message = Reply.ErrorName.empty() ? Result.Title : ErrorTextKey(Reply.ErrorName);
	return Result;
}

} // namespace swc::menu
