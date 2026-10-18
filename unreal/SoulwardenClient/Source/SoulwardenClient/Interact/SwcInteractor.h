#pragma once

#include "CoreMinimal.h"
#include "Interact/SwcInteraction.h"
#include "Net/SwcRepConnection.h"

#include <optional>
#include <vector>

/**
 * The interactor of the player (stage P2.10, docs/INTERACTIONS.md): the objects of the server with a template of
 * SWC_CONTENT/player/interactions.json (tools/convert-player.sh interactions: the fixtures and the personal loot bag of the backend
 * copy), the focus (the nearest bound in its range), and the use of its option with the key E (swc::interact::Use). The request
 * goes into the input entries; the replies 3743 and the ends 3746 of the session change the use. The prompt of the UI comes later
 * (the UI track): a development run shows it in the debug text.
 */
class FSwcInteractor
{
public:
	/** Reads interactions.json (no UObject: the worker thread of the player tables can call it). */
	bool Load(FString& OutReport);
	/** The key E went down or up. */
	void SetKey(bool bDown) { bKeyDown = bDown; }
	/**
	 * One frame: the focus at the feet of the character (engine m), the key, the timers, the replies and the ends of the session,
	 * and the effects for the connection. bLocked: the player cannot use an object now (the death, a loading screen).
	 */
	void Update(const FSwcRepConnection::FSnapshot& Snapshot, FSwcRepConnection& Connection, const swc::net::WorldPosition& Feet, bool bLocked,
		double Now);
	/** A timed use plays (the action DelayedInteraction: the kneel, the weapons in their sheaths, no movement). */
	bool IsTimedUse() const { return Use.timed() && Use.phase() == swc::interact::Use::Phase::requested; }
	/** The prompt and the progress for the debug text of a development run. */
	FString DebugText(double Now) const;
	const swc::interact::Use& GetUse() const { return Use; }

private:
	std::vector<swc::interact::Template> Templates;
	swc::interact::Use Use;
	std::optional<swc::interact::Candidate> Focus;
	FString FocusName;
	bool bKeyDown = false;
	bool bKeyWas = false;
	uint32 SeenCancels = 0;
	uint32 SeenResults = 0;
	bool bSeeded = false;
};
