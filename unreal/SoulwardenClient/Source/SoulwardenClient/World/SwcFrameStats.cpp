#include "World/SwcFrameStats.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RHI.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcFrameStats, Log, All);

namespace
{
float Percentile(TArray<float>& Values, double Fraction)
{
	if (Values.IsEmpty())
	{
		return 0.0f;
	}
	Values.Sort();
	return Values[FMath::Clamp(FMath::FloorToInt(Fraction * (Values.Num() - 1) + 0.5), 0, Values.Num() - 1)];
}
} // namespace

bool USwcFrameStats::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcFrameStats::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcFrameStats, STATGROUP_Tickables);
}

void USwcFrameStats::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	FParse::Value(FCommandLine::Get(), TEXT("SwcFrameStats="), Seconds);
	WindowStart = FPlatformTime::Seconds();
}

void USwcFrameStats::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Seconds <= 0.0)
	{
		return;
	}
	FrameMs.Add(DeltaTime * 1000.0f);
	GpuMs.Add(static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles())));
	const double Now = FPlatformTime::Seconds();
	if (Now - WindowStart < Seconds)
	{
		return;
	}
	FVector View = FVector::ZeroVector;
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (Controller && Controller->PlayerCameraManager)
	{
		View = Controller->PlayerCameraManager->GetCameraLocation();
	}
	const int32 Count = FrameMs.Num();
	const float FrameMax = FMath::Max(FrameMs);
	const float GpuMax = FMath::Max(GpuMs);
	UE_LOG(LogSwcFrameStats, Log,
		TEXT("SWC FRAMES {\"seconds\": %.1f, \"frames\": %d, \"frame p50 ms\": %.2f, \"frame p95 ms\": %.2f, \"frame max ms\": %.2f, \"gpu p50 ms\": %.2f, \"gpu p95 ms\": %.2f, \"gpu max ms\": %.2f, \"view m\": [%.1f, %.1f, %.1f]}"),
		Now - WindowStart, Count, Percentile(FrameMs, 0.5), Percentile(FrameMs, 0.95), FrameMax, Percentile(GpuMs, 0.5),
		Percentile(GpuMs, 0.95), GpuMax, View.X / 100.0, -View.Y / 100.0, View.Z / 100.0);
	FrameMs.Reset();
	GpuMs.Reset();
	WindowStart = Now;
}
