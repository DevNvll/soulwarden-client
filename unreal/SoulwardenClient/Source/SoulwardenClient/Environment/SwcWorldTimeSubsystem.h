#pragma once

#include "CoreMinimal.h"
#include "Net/Codec/SwcWorldTime.h"
#include "Subsystems/WorldSubsystem.h"

#include "SwcWorldTimeSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSwcEnvironment, Log, All);

/**
 * The world clock of the client (docs/ENVIRONMENT.md; Soulwarden protocol notes 4.43): the time of day of the one cycle of the world,
 * from the record of the backend (7036, the answer to the request 4248 of the session at the world entry) and the copy of the server
 * clock of the client (the registration reply and 335). The client calculates the time itself in each frame with the formulas of the
 * retail client (Net/Codec/SwcWorldTime.h): the backend sends no hour stream, only a new record at a command and one cycle after the
 * last one, and the phase (4247) at each phase change.
 *
 * Before the first record there is no world time (HasWorldTime false): the retail client then keeps the hour of the mission.
 *
 * Development aids: a debug line with the time on the screen, a log line at each record, at each phase change, and each 60 s;
 * -SwcWorldTimeProbe logs the time each 1 s.
 */
UCLASS()
class SOULWARDENCLIENT_API USwcWorldTimeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** @return True after the first record of the backend in this world entry. */
	bool HasWorldTime() const { return bHasRecord; }
	/** @return The game second of the day now (0 to 86,400; the formula of the client, not in steps). */
	double GetGameSecond() const { return GameSecond; }
	/** @return The hour of the engine now (0 to 24, in steps of one game second: client code 0x6434700). */
	double GetHour() const { return swc::net::engine_hour(GameSecond); }
	/** @return The phase now from the edges of the record (night 0, dawn 1, day 2, dusk 3: client code 0x6A08270). */
	uint32 GetPhase() const { return Phase; }
	/** @return The phase of the last message of the server (7036 or 4247): the phase of the gameplay of the backend. */
	uint32 GetServerPhase() const { return ServerPhase; }
	/** @return The record of the backend (valid when HasWorldTime). */
	const swc::net::TimeRecord& GetRecord() const { return Record; }

private:
	/** The record and the phase of the server from the snapshot of the session (each RecordSeconds). */
	void ReadRecord();
	void ShowLine(const TCHAR* Cause);

	swc::net::TimeRecord Record;
	uint32 RecordRevision = 0;
	bool bHasRecord = false;
	double GameSecond = 0.0;
	uint32 Phase = swc::net::kPhaseDay;
	uint32 ServerPhase = swc::net::kPhaseDay;
	double NextRead = 0.0;
	double NextLog = 0.0;
	double NextShow = 0.0;
	bool bProbe = false;

	/** The read of the snapshot of the session (s): a record comes at the world entry, at a command, and each cycle. */
	static constexpr double RecordSeconds = 0.25;
	/** The log line with the time (s); -SwcWorldTimeProbe: each 1 s. */
	static constexpr double LogSeconds = 60.0;
	/** The update of the debug line on the screen (s). */
	static constexpr double ShowSeconds = 0.5;
};
