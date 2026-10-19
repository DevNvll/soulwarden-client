#include "Environment/SwcLumenTest.h"

#include "Components/StaticMeshComponent.h"
#include "DistanceFieldAtlas.h"
#include "MeshCardBuild.h"
#include "Creatures/SwcCreatureData.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Environment/SwcWorldTimeSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"

namespace
{
/** The scan of the components of the level (s): the cells of the world stream in after the start. */
constexpr double ScanSeconds = 2.0;
TAutoConsoleVariable<int32> CVarLumenTest(TEXT("swc.LumenTest"), 0,
	TEXT("The Lumen mesh data test of stage E2: 1 the copies with distance fields and Lumen cards, 0 the world meshes."));

void SetMesh(UStaticMeshComponent* Component, UStaticMesh* Mesh)
{
	const EComponentMobility::Type Mobility = Component->Mobility;
	Component->SetMobility(EComponentMobility::Movable);   // SetStaticMesh refuses a static component in a game
	Component->SetStaticMesh(Mesh);
	Component->SetMobility(Mobility);
}
}

float USwcLumenTestLibrary::WaitForMeshBuilds()
{
	const double Start = FPlatformTime::Seconds();
#if WITH_EDITOR
	if (GDistanceFieldAsyncQueue != nullptr)
	{
		GDistanceFieldAsyncQueue->BlockUntilAllBuildsComplete();
	}
	if (GCardRepresentationAsyncQueue != nullptr)
	{
		GCardRepresentationAsyncQueue->BlockUntilAllBuildsComplete();
	}
#endif
	return static_cast<float>(FPlatformTime::Seconds() - Start);
}

void USwcLumenTestSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (FParse::Param(FCommandLine::Get(), TEXT("SwcLumenTest")))
	{
		CVarLumenTest->Set(1, ECVF_SetByCommandline);
	}
}

void USwcLumenTestSubsystem::SwapBack()
{
	int32 Count = 0;
	for (const auto& Pair : Originals)
	{
		if (UStaticMeshComponent* Component = Pair.Key.Get())
		{
			SetMesh(Component, Pair.Value);
			++Count;
		}
	}
	Originals.Reset();
	LastLogged = 0;
	UE_LOG(LogSwcEnvironment, Log, TEXT("lumen test: off: %d component(s) have the world meshes again"), Count);
}

bool USwcLumenTestSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcLumenTestSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcLumenTestSubsystem, STATGROUP_Tickables);
}

void USwcLumenTestSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (CVarLumenTest.GetValueOnGameThread() == 0)
	{
		if (!Originals.IsEmpty())
		{
			SwapBack();
		}
		return;
	}
	if (!bLoaded)
	{
		bLoaded = true;
		const FString Path = FPaths::Combine(FSwcCreatureLibrary::ContentFolder(), TEXT("environment"), TEXT("lumen-test.json"));
		FString Text;
		TSharedPtr<FJsonObject> Object;
		const TSharedPtr<FJsonObject>* Meshes = nullptr;
		if (FFileHelper::LoadFileToString(Text, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) && Object.IsValid()
			&& Object->TryGetObjectField(TEXT("meshes"), Meshes))
		{
			for (const auto& Pair : (*Meshes)->Values)
			{
				Copies.Add(FString(Pair.Key), Pair.Value->AsString());
			}
		}
		UE_LOG(LogSwcEnvironment, Log, TEXT("lumen test: %d mesh copies with distance fields and Lumen cards (%s)"), Copies.Num(), *Path);
	}
	const double Now = FPlatformTime::Seconds();
	if (Copies.IsEmpty() || Now < NextScan)
	{
		return;
	}
	NextScan = Now + ScanSeconds;
	UWorld* World = GetWorld();
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		UStaticMeshComponent* Component = *It;
		UStaticMesh* Mesh = Component->GetStaticMesh();
		if (Mesh == nullptr || Component->GetWorld() != World)
		{
			continue;
		}
		const FString* Copy = Copies.Find(Mesh->GetPathName());
		if (Copy == nullptr)
		{
			continue;
		}
		TObjectPtr<UStaticMesh>& Target = Loaded.FindOrAdd(*Copy);
		if (Target == nullptr)
		{
			Target = LoadObject<UStaticMesh>(nullptr, **Copy);
		}
		if (Target != nullptr && Target != Mesh)
		{
			KeepOriginals.AddUnique(Mesh);
			Originals.Add(Component, Mesh);
			SetMesh(Component, Target);
		}
	}
	if (Originals.Num() != LastLogged)
	{
		LastLogged = Originals.Num();
		const float Seconds = USwcLumenTestLibrary::WaitForMeshBuilds();   // the distance fields and the cards of the copies complete
		UE_LOG(LogSwcEnvironment, Log, TEXT("lumen test: on: %d component(s) use the copies; the wait for their distance fields and cards %.2f s"),
			Originals.Num(), Seconds);
	}
}
