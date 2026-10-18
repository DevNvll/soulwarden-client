#include "World/SwcFlythrough.h"

#include "Camera/CameraComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/SwcSpace.h"
#include "RenderTimer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcFlythrough, Log, All);

ASwcFlythrough::ASwcFlythrough()
{
	PrimaryActorTick.bCanEverTick = true;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetFieldOfView(90.0f);
	RootComponent = Camera;
	Streaming = CreateDefaultSubobject<UWorldPartitionStreamingSourceComponent>(TEXT("Streaming"));
}

bool ASwcFlythrough::LoadPlan(const FString& File)
{
	FString Text;
	TSharedPtr<FJsonObject> Plan;
	if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Plan) || !Plan.IsValid())
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!Plan->TryGetArrayField(TEXT("points"), List))
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *List)
	{
		const TArray<TSharedPtr<FJsonValue>>& P = Value->AsArray();
		if (P.Num() == 3)
		{
			Points.Add(SwcEngineToUnreal(swc::net::WorldPosition{static_cast<float>(P[0]->AsNumber()), static_cast<float>(P[1]->AsNumber()),
				static_cast<float>(P[2]->AsNumber())}));
		}
	}
	Speed = Plan->HasField(TEXT("speed")) ? Plan->GetNumberField(TEXT("speed")) * 100.0 : Speed;
	LookAhead = Plan->HasField(TEXT("look ahead")) ? Plan->GetNumberField(TEXT("look ahead")) * 100.0 : LookAhead;
	PitchDown = Plan->HasField(TEXT("pitch")) ? Plan->GetNumberField(TEXT("pitch")) : PitchDown;
	const TSharedPtr<FJsonObject>* FramesPlan = nullptr;
	if (Plan->TryGetObjectField(TEXT("frames"), FramesPlan))
	{
		FrameFolder = (*FramesPlan)->GetStringField(TEXT("folder"));
		FrameEvery = static_cast<int32>((*FramesPlan)->GetNumberField(TEXT("every")));
	}
	Distances.Add(0.0);
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		Distances.Add(Distances.Last() + FVector::Distance(Points[Index - 1], Points[Index]));
	}
	if (Points.Num() < 2)
	{
		return false;
	}
	SetActorLocation(Points[0]);
	SetActorRotation((PointAt(LookAhead) - Points[0]).Rotation() + FRotator(-PitchDown, 0.0, 0.0));
	Streaming->EnableStreamingSource();
	StartWait = FPlatformTime::Seconds();
	UE_LOG(LogSwcFlythrough, Log, TEXT("flythrough: %d points, %.0f m, %.1f m/s"), Points.Num(), Distances.Last() / 100.0, Speed / 100.0);
	return true;
}

FVector ASwcFlythrough::PointAt(double Distance) const
{
	Distance = FMath::Clamp(Distance, 0.0, Distances.Last());
	int32 Index = 1;
	while (Index < Distances.Num() - 1 && Distances[Index] < Distance)
	{
		++Index;
	}
	const double Length = FMath::Max(Distances[Index] - Distances[Index - 1], 1.0);
	return FMath::Lerp(Points[Index - 1], Points[Index], (Distance - Distances[Index - 1]) / Length);
}

void ASwcFlythrough::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Points.Num() < 2)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (!bStarted)
	{
		// The start waits for the cells of the first place (at most 60 s).
		if (!Streaming->IsStreamingCompleted() && Now - StartWait < 60.0)
		{
			return;
		}
		bStarted = true;
		SecondStart = Now;
		UE_LOG(LogSwcFlythrough, Log, TEXT("flythrough: start after %.1f s of streaming"), Now - StartWait);
		return;
	}
	Travelled += Speed * DeltaSeconds;
	const FVector Location = PointAt(Travelled);
	const FRotator Target = (PointAt(Travelled + LookAhead) - Location).Rotation() + FRotator(-PitchDown, 0.0, 0.0);
	SetActorLocationAndRotation(Location, FMath::RInterpTo(GetActorRotation(), Target, DeltaSeconds, 2.0f));
	++Frames;
	++SecondFrames;
	FrameTimes.Add(DeltaSeconds);
	GameThreadTimes.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
	GpuTimes.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
	if (FrameEvery > 0 && Frames % FrameEvery == 0)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FrameFolder, FString::Printf(TEXT("frame_%05d.png"), Pictures++)), false, false);
	}
	if (Now - SecondStart >= 1.0)
	{
		const bool bStreamed = Streaming->IsStreamingCompleted();
		StreamingIncompleteSeconds += bStreamed ? 0 : 1;
		const FVector Engine = FVector(Location.X / 100.0, -Location.Y / 100.0, Location.Z / 100.0);
		UE_LOG(LogSwcFlythrough, Log, TEXT("flythrough: %.0f m of %.0f, engine %.0f %.0f %.0f m, %.1f fps, streaming %s"), Travelled / 100.0,
			Distances.Last() / 100.0, Engine.X, Engine.Y, Engine.Z, SecondFrames / (Now - SecondStart), bStreamed ? TEXT("complete") : TEXT("loading"));
		SecondStart = Now;
		SecondFrames = 0;
	}
	if (Travelled >= Distances.Last())
	{
		TArray<float> Sorted = FrameTimes;
		Sorted.Sort();
		double Total = 0.0;
		for (float Time : FrameTimes)
		{
			Total += Time;
		}
		const float Worst1 = Sorted[FMath::Clamp(static_cast<int32>(Sorted.Num() * 0.99), 0, Sorted.Num() - 1)];
		// The percentiles of the frame time, the game thread, and the GPU (ms; stat unit), with no picture capture.
		auto Percentile = [](TArray<float> Values, double Fraction)
		{
			Values.Sort();
			return Values.IsEmpty() ? 0.0f : Values[FMath::Clamp(static_cast<int32>(Values.Num() * Fraction), 0, Values.Num() - 1)];
		};
		UE_LOG(LogSwcFlythrough, Log, TEXT("SWC FLYTHROUGH {\"frames\": %d, \"seconds\": %.1f, \"average fps\": %.1f, \"1%% low fps\": %.1f, \"longest frame ms\": %.1f, \"seconds with streaming not complete\": %d, \"pictures\": %d, \"frame ms p50\": %.2f, \"frame ms p95\": %.2f, \"game thread ms p50\": %.2f, \"game thread ms p95\": %.2f, \"gpu ms p50\": %.2f, \"gpu ms p95\": %.2f}"),
			Frames, Total, Frames / FMath::Max(Total, 0.001), 1.0f / FMath::Max(Worst1, 0.0001f), Sorted.Last() * 1000.0f, StreamingIncompleteSeconds, Pictures,
			Percentile(FrameTimes, 0.5) * 1000.0f, Percentile(FrameTimes, 0.95) * 1000.0f, Percentile(GameThreadTimes, 0.5), Percentile(GameThreadTimes, 0.95),
			Percentile(GpuTimes, 0.5), Percentile(GpuTimes, 0.95));
		Points.Reset();
		FGenericPlatformMisc::RequestExit(false);
	}
}
