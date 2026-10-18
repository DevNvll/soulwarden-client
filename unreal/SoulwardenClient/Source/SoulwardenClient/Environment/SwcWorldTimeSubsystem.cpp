#include "Environment/SwcWorldTimeSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SwcBackendSubsystem.h"
#include "SwcDebugLines.h"

DEFINE_LOG_CATEGORY(LogSwcEnvironment);

namespace
{

/** The key of the debug line of the world time on the screen (a fixed key: the line changes in place). */
constexpr uint64 DebugLineKey = 0x5357434C4F434B31ULL;

}

void USwcWorldTimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bProbe = FParse::Param(FCommandLine::Get(), TEXT("SwcWorldTimeProbe"));
}

bool USwcWorldTimeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcWorldTimeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcWorldTimeSubsystem, STATGROUP_Tickables);
}

void USwcWorldTimeSubsystem::ReadRecord()
{
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	USwcBackendSubsystem* Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USwcBackendSubsystem>() : nullptr;
	swc::net::RepClient::OwnEntity Own;
	if (Backend == nullptr || !Backend->GetLocalPlayer(Own))
	{
		return;
	}
	if (Own.dayPhase != ServerPhase && bHasRecord)
	{
		UE_LOG(LogSwcEnvironment, Log, TEXT("world time: the phase of the server: %hs (from %hs)"), swc::net::day_phase_text(Own.dayPhase),
			swc::net::day_phase_text(ServerPhase));
	}
	ServerPhase = Own.dayPhase;
	if (!Own.timeRecord.has_value() || Own.timeRevision == RecordRevision)
	{
		return;
	}
	Record = *Own.timeRecord;
	RecordRevision = Own.timeRevision;
	const bool bFirst = !bHasRecord;
	bHasRecord = true;
	NextLog = 0.0;
	ShowLine(bFirst ? TEXT("the first record of the server") : TEXT("a new record of the server"));
}

void USwcWorldTimeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextRead)
	{
		NextRead = Now + RecordSeconds;
		ReadRecord();
	}
	if (!bHasRecord)
	{
		return;
	}
	// The time in each frame from the record and the server clock (client code 0x6A0ADC0, 0x6A09E60, 0x6A08270).
	const UGameInstance* GameInstance = GetWorld() != nullptr ? GetWorld()->GetGameInstance() : nullptr;
	const USwcBackendSubsystem* Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USwcBackendSubsystem>() : nullptr;
	const uint64 ServerUs = Backend != nullptr ? Backend->GetServerTimeMicroseconds() : 0;
	if (ServerUs == 0)
	{
		return;
	}
	const double Part = swc::net::cycle_part(Record, ServerUs * 1000ULL);
	GameSecond = swc::net::game_second(Record, Part);
	const uint32 NewPhase = swc::net::phase_at(Record, Part);
	if (NewPhase != Phase)
	{
		Phase = NewPhase;
		ShowLine(TEXT("a phase edge of the record"));
	}
	if (Now >= NextLog)
	{
		ShowLine(nullptr);
	}
	if (Now >= NextShow && GEngine != nullptr && SwcDebugLines())
	{
		NextShow = Now + ShowSeconds;
		GEngine->AddOnScreenDebugMessage(DebugLineKey, static_cast<float>(ShowSeconds * 4.0), FColor(170, 200, 255),
			FString::Printf(TEXT("world time %hs (%hs), hour %.3f, phase of the server %hs"), swc::net::clock_text(GameSecond).c_str(),
				swc::net::day_phase_text(Phase), GetHour(), swc::net::day_phase_text(ServerPhase)));
	}
}

void USwcWorldTimeSubsystem::ShowLine(const TCHAR* Cause)
{
	const double Now = FPlatformTime::Seconds();
	NextLog = Now + (bProbe ? 1.0 : LogSeconds);
	const UGameInstance* GameInstance = GetWorld() != nullptr ? GetWorld()->GetGameInstance() : nullptr;
	const USwcBackendSubsystem* Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USwcBackendSubsystem>() : nullptr;
	const uint64 ServerUs = Backend != nullptr ? Backend->GetServerTimeMicroseconds() : 0;
	if (ServerUs != 0)
	{
		const double Part = swc::net::cycle_part(Record, ServerUs * 1000ULL);
		GameSecond = swc::net::game_second(Record, Part);
		Phase = swc::net::phase_at(Record, Part);
	}
	UE_LOG(LogSwcEnvironment, Log, TEXT("world time: %hs (%hs), hour %.4f, part %.6f of the cycle, server clock %llu us%s%s"),
		swc::net::clock_text(GameSecond).c_str(), swc::net::day_phase_text(Phase), GameSecond / 3600.0,
		ServerUs != 0 ? swc::net::cycle_part(Record, ServerUs * 1000ULL) : 0.0, static_cast<unsigned long long>(ServerUs),
		Cause != nullptr ? TEXT(": ") : TEXT(""), Cause != nullptr ? Cause : TEXT(""));
}
