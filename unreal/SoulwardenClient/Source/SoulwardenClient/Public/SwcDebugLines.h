#pragma once

#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

/**
 * The debug text lines of a run (AddOnScreenDebugMessage: the InGame line, the movement, the stamina, the life and the prompt, the
 * world time): off by default, on with -SwcDebugLines (stage WF7). The check tools give the switch when their videos use the lines
 * to find frames (tools/creature-check.sh). The matched views hide the lines in any case (USwcMatchViewSubsystem).
 */
inline bool SwcDebugLines()
{
	static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("SwcDebugLines"));
	return bOn;
}

/**
 * The strike trace (usertest-1024 item 4, research usertest-greataxe-hit.md section 5): a log line for each strike query (the shape, the
 * joint, the capsule ends, the feet) and for each near creature (its places, its hit volumes, the first cause of a rejection). Off by
 * default; on with -SwcStrikeTrace (the development checks) or SOULWARDEN_HARNESS=1 in the environment of the client.
 */
inline bool SwcStrikeTrace()
{
	static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("SwcStrikeTrace"))
		|| FPlatformMisc::GetEnvironmentVariable(TEXT("SOULWARDEN_HARNESS")) == TEXT("1");
	return bOn;
}
