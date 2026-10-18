#pragma once

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
