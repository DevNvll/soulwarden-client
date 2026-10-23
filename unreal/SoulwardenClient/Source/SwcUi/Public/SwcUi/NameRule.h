// The name rule of the character creation and the replies of the name check and of the create request (stage Q3,
// docs/FRONTEND.md "Q3, the character creation"). Plain C++: tools/test-menu.sh tests it with no engine.
//
// The local rule of the client (Soulwarden protocol notes 2.2.2, static proof 0x41F6530 and 0x5CF59A0): a name that it refuses
// gets no request. UiCharacterServiceRequestBus.PreValidateNameCheck gives its result to namevalidationcommon.luac.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "SwcUi/Export.h"

namespace swc::menu {

/** The results of the local rule, in the numbers of the client (namevalidationcommon.luac shows a text for each). */
enum class ENameCheck : std::uint8_t
{
	Valid = 0,
	/** @mm_invalidname_minlength: below 3 UTF-16 units. */
	TooShort = 1,
	/** @mm_invalidname_maxlength: above 15 UTF-16 units. */
	TooLong = 2,
	/** @mm_invalidname_corrupt: a character out of the pattern, white space at an end, or more than 6 digits. */
	InvalidCharacters = 3,
	/** @mm_invalidname_badwords: the word check of the client (0x5CF5DAB). Its list is not read [U]: this rule never gives it. */
	BadWords = 4,
	/** @mm_invalidname_multiplespaces: two spaces in sequence. */
	MultipleSpaces = 5,
};

inline constexpr std::size_t kNameMinUnits = 3;
inline constexpr std::size_t kNameMaxUnits = 15;
inline constexpr std::size_t kNameMaxDigits = 6;

/**
 * The local rule for a UTF-8 name (the text of the name field after TrimString of the script). The order of the checks: the
 * length, an end with white space, two spaces in sequence, the characters, the digits; the order of the native checks for a
 * name with two faults is not proved [I]. A UTF-8 fault is an invalid character (checked first).
 */
[[nodiscard]] SWCUI_EXPORT ENameCheck CheckName(std::string_view Utf8);

/** @return True for a character of the pattern of the client (the space, ' , - . , the digits, the ASCII letters, the ranges). */
[[nodiscard]] SWCUI_EXPORT bool IsNameCharacter(std::uint32_t CodePoint);

/** The kind of a reply of the gateway: the status, the error name of `x-amzn-ErrorType` or of the body member `__type`. */
struct FGatewayReply
{
	int Status = 0;
	/** The error name, for example "NameNotAvailableError"; empty for a success or a reply with no name. */
	std::string ErrorName;
};

/**
 * @return The error name of a reply: the header value when it is not empty (its text before a ':', as the AWS SDK of the
 * client reads it), else the string member "__type" of the JSON body (its text after a '#'), else empty.
 */
[[nodiscard]] SWCUI_EXPORT std::string ReadErrorName(std::string_view HeaderValue, std::string_view Body);

/** The state of the name field after the reply of the name check (UiCharacterServiceNotificationBus.ValidateNameResult). */
enum class ENameValidation : std::uint8_t
{
	Valid,
	/** The name has a character of another account: @mm_loginservices_NameNotAvailableError. */
	NotAvailable,
	/** The backend rule: @mm_loginservices_NameLengthError, NameValidationError, or NameUsingBadWordsError. */
	Refused,
	/** No reply, a reply with no error name, or an other error: @mm_invalidname_unknown in the field [I]. */
	Failed,
};

/** @return The state of the name field for a reply of the name check, and in `TextKey` the text key of the field ("" for Valid). */
[[nodiscard]] SWCUI_EXPORT ENameValidation ReadValidateReply(const FGatewayReply& Reply, std::string& TextKey);

/**
 * The result of a create reply for CreateCharacterResult(characterId, title, message): the character id of a 200 reply with
 * Character.CharacterId; else the title @mm_loginservices_CreateCharacterFailed [I] and the message
 * @mm_loginservices_<error name> (the text keys of the client), or @mm_loginservices_CreateCharacterFailed for a reply with no
 * error name.
 */
struct FCreateResult
{
	std::string CharacterId;
	std::string Title;
	std::string Message;
};

/** @return The text key of an error name: "@mm_loginservices_" + the name. */
[[nodiscard]] SWCUI_EXPORT std::string ErrorTextKey(std::string_view ErrorName);

/** @return The result of a create reply: `CharacterId` is Character.CharacterId of the body (empty when the body has none). */
[[nodiscard]] SWCUI_EXPORT FCreateResult ReadCreateReply(const FGatewayReply& Reply, std::string_view CharacterId);

} // namespace swc::menu
