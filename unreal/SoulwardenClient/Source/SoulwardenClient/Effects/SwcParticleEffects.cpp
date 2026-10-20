#include "Effects/SwcParticleEffects.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "NiagaraSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "World/SwcLoadingGate.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcEffects, Log, All);

namespace
{
constexpr double RegionM = 2048.0;
constexpr double CellCm = 6400.0;              // the cells of the placement lookup (64 m)
constexpr double UpdateSeconds = 0.25;         // the update in play; each frame during a loading screen
constexpr int32 PlayBudget = 48;               // the components started in one update in play (no limit during a screen)
constexpr double ReleaseFactor = 1.1;          // a component goes back to the pool beyond this x its range (no flicker at the edge)
constexpr double WarmSeconds = 1.0;            // a loading screen waits until the effects near the place ran this long
constexpr double DefaultViewCm = 5000.0;       // the view distance of an effect with no LOD end (systems.json gives it)

TAutoConsoleVariable<float> CVarEmission(TEXT("swc.Effects.Emission"), 1.0f,
	TEXT("The scale of the source particle shader units (the ambient and the glow terms, particle-rules.md 1) to the engine units of "
		 "MPC_SwcMaterials EmissiveScale (1: one engine unit of 10,000 cd/m2 x swc.LightScale)."));

// The view distance rule of an emitter (particle-rules.md 4) is off by default: with the public default 0.5 and M 0.4 m it gives 9.5 m for a
// torch of the Oxboro town hall (V 0.1, S 0.7), and the retail take retail-torch2 shows that torch at about 23 m (docs/PARTICLES.md).
TAutoConsoleVariable<float> CVarMinDrawPixels(TEXT("swc.Effects.MinDrawPixels"), 0.0f,
	TEXT("e_ParticlesMinDrawPixels of the view distance of an emitter: Dmax = screen height / vertical FOV x max(this, 0.125) x M x S x V "
		 "(particle-rules.md 4). 0 (the default): the rule is off, the LOD bounds only."));

TAutoConsoleVariable<int32> CVarShow(TEXT("swc.Effects.Show"), 1,
	TEXT("1: the particle effects are visible; 0: hidden (the components stay; a matched view with and without the effects)."));

// swc.Effects.Test KEY,COUNT[,DISTANCE_M[,SCALE[,RIGHT_M]]]: COUNT copies of an effect in a grid on the ground in front of the character (a matched view variant:
// --variant VIEW@NAME="swc.Effects.Test Bld_Global_Ambience.Candle01,9,2"); swc.Effects.Test with no argument removes them.
FAutoConsoleCommandWithWorldAndArgs CommandTest(TEXT("swc.Effects.Test"),
	TEXT("KEY,COUNT[,DISTANCE_M[,SCALE[,RIGHT_M]]]: copies of an effect in front of the camera; no argument: remove them."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (USwcEffectSubsystem* Effects = World != nullptr ? World->GetSubsystem<USwcEffectSubsystem>() : nullptr)
		{
			Effects->RunTest(FString::Join(Args, TEXT(" ")));
		}
	}));

// swc.Effects.Dump: one log line for each running effect component: its system, place, and for each emitter its state and
// particle count (the check of a system that draws nothing).
FAutoConsoleCommandWithWorld CommandDump(TEXT("swc.Effects.Dump"), TEXT("Log the emitters and particle counts of the running effects."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		for (TObjectIterator<UNiagaraComponent> It; It; ++It)
		{
			if (It->GetWorld() != World || !It->IsActive() || It->GetOwner() == nullptr || !It->GetOwner()->GetName().StartsWith(TEXT("SwcEffects")))
			{
				continue;
			}
			FString Line = FString::Printf(TEXT("effects: dump %s at %s:"), *GetNameSafe(It->GetAsset()), *It->GetComponentLocation().ToCompactString());
			if (const FNiagaraSystemInstanceControllerConstPtr Controller = It->GetSystemInstanceController(); Controller.IsValid())
			{
				if (const FNiagaraSystemInstance* Instance = Controller->GetSystemInstance_Unsafe())
				{
					for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters())
					{
						Line += FString::Printf(TEXT(" [%s %d %d]"), *Emitter->GetEmitterHandle().GetName().ToString(),
							int32(Emitter->GetExecutionState()), Emitter->GetNumParticles());
					}
				}
			}
			UE_LOG(LogSwcEffects, Log, TEXT("%s"), *Line);
		}
	}));

FString ContentRoot()
{
	FString Content = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_CONTENT"));
	if (Content.IsEmpty())
	{
		FString Work = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_WORK"));
		if (Work.IsEmpty())
		{
			Work = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT("Games/soulwarden-client"));
		}
		Content = FPaths::Combine(Work, TEXT("content"));
	}
	return Content;
}

/** @return The Unreal transform of a source placement matrix (rows 3 x 4, engine m): C M C^-1, C = diag(100, -100, 100). */
FTransform UnrealTransform(const TArray<TSharedPtr<FJsonValue>>& M)
{
	double L[3][4];
	for (int32 I = 0; I < 12; ++I)
	{
		L[I / 4][I % 4] = M[I]->AsNumber();
	}
	// S L S with S = diag(1, -1, 1): the entries (r, c) with exactly one index 1 change sign.
	const auto Entry = [&L](int32 R, int32 C) { return ((R == 1) != (C == 1)) ? -L[R][C] : L[R][C]; };
	// The rows of an FMatrix are the basis axes: the axis c of Unreal is the column c of S L S.
	const FMatrix Matrix(FPlane(Entry(0, 0), Entry(1, 0), Entry(2, 0), 0.0), FPlane(Entry(0, 1), Entry(1, 1), Entry(2, 1), 0.0),
	                     FPlane(Entry(0, 2), Entry(1, 2), Entry(2, 2), 0.0), FPlane(L[0][3] * 100.0, -L[1][3] * 100.0, L[2][3] * 100.0, 1.0));
	return FTransform(Matrix);
}

FSwcEffectSettings SettingsOf(const TSharedPtr<FJsonObject>& Object)
{
	FSwcEffectSettings Settings;
	if (!Object.IsValid())
	{
		return Settings;
	}
	const TArray<TSharedPtr<FJsonValue>>* Color = nullptr;
	if (Object->TryGetArrayField(TEXT("Color"), Color) && Color->Num() >= 3)
	{
		Settings.Tint = FLinearColor((*Color)[0]->AsNumber(), (*Color)[1]->AsNumber(), (*Color)[2]->AsNumber(), 1.0f);
	}
	double Value = 0.0;
	if (Object->TryGetNumberField(TEXT("Alpha Scale"), Value)) Settings.AlphaScale = float(Value);
	if (Object->TryGetNumberField(TEXT("GlobalSizeScale"), Value)) Settings.SizeScale = float(Value);
	if (Object->TryGetNumberField(TEXT("Time Scale"), Value)) Settings.TimeScale = float(Value);
	if (Object->TryGetNumberField(TEXT("View Distance Multiplier"), Value)) Settings.ViewDistanceMultiplier = float(Value);
	return Settings;
}

/** @return False when a placement does not show at the start (Visible or Enable false: a script starts it, stage 7). */
bool StartsShown(const TSharedPtr<FJsonObject>& Object)
{
	bool bValue = true;
	return !(Object.IsValid() && ((Object->TryGetBoolField(TEXT("Visible"), bValue) && !bValue)
		|| (Object->TryGetBoolField(TEXT("Enable"), bValue) && !bValue)));
}

FIntPoint CellOf(const FVector& Location)
{
	return FIntPoint(FMath::FloorToInt(Location.X / CellCm), FMath::FloorToInt(Location.Y / CellCm));
}
} // namespace

FString USwcEffectSubsystem::Folder()
{
	return FPaths::Combine(ContentRoot(), TEXT("particles"));
}

bool USwcEffectSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcEffectSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcEffectSubsystem, STATGROUP_Tickables);
}

void USwcEffectSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	LoadTypes();
	bWorld = !FParse::Param(FCommandLine::Get(), TEXT("SwcNoEffects")) && FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_EFFECTS")) != TEXT("0")
		&& !Types.IsEmpty();
	FParse::Value(FCommandLine::Get(), TEXT("SwcEffectTest="), TestSpec);
	UE_LOG(LogSwcEffects, Log, TEXT("effects: %d system(s), the world placements %s, the largest view %.0f m (%s)"), Types.Num(),
		bWorld ? TEXT("on") : TEXT("off"), MaxViewCm / 100.0, *Folder());
	if (bWorld)
	{
		SwcLoadingGate::Register(&World, TEXT("effects"), [this](const FVector& Place, FString& Why) { return ReadyNear(Place, Why); });
	}
}

void USwcEffectSubsystem::Deinitialize()
{
	SwcLoadingGate::Unregister(GetWorld(), TEXT("effects"));
	Super::Deinitialize();
}

void USwcEffectSubsystem::LoadTypes()
{
	Types.Reset();
	TypeIndex.Reset();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Folder(), TEXT("systems.json"))))
	{
		return;
	}
	TSharedPtr<FJsonObject> Doc;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) || !Doc.IsValid())
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& Value : Doc->GetArrayField(TEXT("systems")))
	{
		const TSharedPtr<FJsonObject> System = Value->AsObject();
		FString Asset;
		if (!System->TryGetStringField(TEXT("asset"), Asset))
		{
			continue;   // a missing effect name
		}
		FType Type;
		Type.Key = System->GetStringField(TEXT("key"));
		Type.Path = FSoftObjectPath(FString::Printf(TEXT("/SwcGameContent/Particles/Systems/%s.%s"), *Asset, *Asset));
		Type.ViewCm = System->GetNumberField(TEXT("view"));
		Type.MaxSize = System->HasField(TEXT("maxSize")) ? System->GetNumberField(TEXT("maxSize")) : 0.0;
		if (Type.ViewCm <= 0.0)
		{
			Type.ViewCm = DefaultViewCm;
		}
		MaxViewCm = FMath::Max(MaxViewCm, Type.ViewCm);
		TypeIndex.Add(Type.Key.ToLower(), Types.Num());
		Types.Add(MoveTemp(Type));
	}
}

int32 USwcEffectSubsystem::TypeOf(const FString& Effect) const
{
	const int32* Found = TypeIndex.Find(Effect.ToLower());
	return Found ? *Found : INDEX_NONE;
}

bool USwcEffectSubsystem::HasEffect(const FString& Effect) const
{
	const int32 Type = TypeOf(Effect);
	return Type != INDEX_NONE && (Types[Type].System != nullptr || !Types[Type].bTried);
}

UNiagaraSystem* USwcEffectSubsystem::SystemOf(int32 Type)
{
	FType& Entry = Types[Type];
	if (!Entry.bTried)
	{
		Entry.bTried = true;
		// A system is small (a few stateless emitters and one atlas each): it loads at its first use.
		Entry.System = Cast<UNiagaraSystem>(Entry.Path.TryLoad());
		UE_LOG(LogSwcEffects, Log, TEXT("effects: %s -> %s"), *Entry.Key, Entry.System ? *Entry.Path.ToString() : TEXT("no system (not imported)"));
	}
	return Entry.System;
}

AActor* USwcEffectSubsystem::OwnerActor()
{
	if (AActor* Actor = Owner.Get())
	{
		return Actor;
	}
	FActorSpawnParameters Parameters;
	Parameters.Name = MakeUniqueObjectName(GetWorld(), AActor::StaticClass(), TEXT("SwcEffects"));
	Parameters.ObjectFlags |= RF_Transient;
	AActor* Actor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Parameters);
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Owner = Actor;
	return Actor;
}

UNiagaraComponent* USwcEffectSubsystem::Acquire(int32 Type)
{
	UNiagaraSystem* System = SystemOf(Type);
	if (System == nullptr)
	{
		return nullptr;
	}
	TArray<TWeakObjectPtr<UNiagaraComponent>>& Pool = Types[Type].Pool;
	while (!Pool.IsEmpty())
	{
		if (UNiagaraComponent* Component = Pool.Pop().Get())
		{
			return Component;
		}
	}
	UNiagaraComponent* Component = NewObject<UNiagaraComponent>(OwnerActor());
	Component->SetAutoActivate(false);
	Component->SetAsset(System);
	Component->SetUsingAbsoluteLocation(true);
	Component->SetUsingAbsoluteRotation(true);
	Component->SetUsingAbsoluteScale(true);
	Component->SetVisibility(bShown);
	Component->RegisterComponent();
	return Component;
}

void USwcEffectSubsystem::Release(int32 Type, UNiagaraComponent* Component, bool bKill)
{
	if (Component == nullptr)
	{
		return;
	}
	if (bKill)
	{
		Component->DeactivateImmediate();
	}
	else
	{
		Component->Deactivate();
	}
	if (Types.IsValidIndex(Type))
	{
		Types[Type].Pool.Add(Component);
	}
}

void USwcEffectSubsystem::SetParameters(UNiagaraComponent* Component, const FSwcEffectSettings& Settings, const FVector& PlacementScale)
{
	if (Component == nullptr)
	{
		return;
	}
	Component->SetVariableLinearColor(TEXT("User.SwcTint"), Settings.Tint);
	Component->SetVariableFloat(TEXT("User.SwcAlphaScale"), Settings.AlphaScale);
	Component->SetWorldScale3D(PlacementScale * double(Settings.SizeScale));
	Component->SetCustomTimeDilation(Settings.TimeScale);
}

void USwcEffectSubsystem::Restart(UNiagaraComponent* Component)
{
	if (Component != nullptr)
	{
		Component->Activate(true);
	}
}

UNiagaraComponent* USwcEffectSubsystem::Start(const FString& Effect, const FTransform& Transform, const FSwcEffectSettings& Settings,
                                              USceneComponent* Parent)
{
	const int32 Type = TypeOf(Effect);
	UNiagaraComponent* Component = Type != INDEX_NONE ? Acquire(Type) : nullptr;
	if (Component == nullptr)
	{
		return nullptr;
	}
	const FTransform World = Parent != nullptr ? Transform * Parent->GetComponentTransform() : Transform;
	Component->SetWorldLocationAndRotation(World.GetLocation(), World.GetRotation());
	SetParameters(Component, Settings, World.GetScale3D());
	Component->Activate(true);
	Started.Add(Component);
	return Component;
}

void USwcEffectSubsystem::Stop(UNiagaraComponent* Component, bool bKill)
{
	if (Component == nullptr)
	{
		return;
	}
	UNiagaraSystem* Asset = Component->GetAsset();
	const int32 Type = Types.IndexOfByPredicate([Asset](const FType& T) { return T.System == Asset; });
	Started.Remove(Component);
	Release(Type, Component, bKill);
}

TArray<UNiagaraComponent*> USwcEffectSubsystem::StartList(const FString& File, double RadiusM, USceneComponent* Parent)
{
	TArray<UNiagaraComponent*> Out;
	FString Text;
	TSharedPtr<FJsonObject> Doc;
	if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) || !Doc.IsValid())
	{
		UE_LOG(LogSwcEffects, Warning, TEXT("effects: no placement list %s"), *File);
		return Out;
	}
	if (Types.IsEmpty())
	{
		LoadTypes();
	}
	int32 Skipped = 0;
	for (const TSharedPtr<FJsonValue>& Value : Doc->GetArrayField(TEXT("rows")))
	{
		const TSharedPtr<FJsonObject> Row = Value->AsObject();
		const FTransform Transform = UnrealTransform(Row->GetArrayField(TEXT("matrix")));
		const TSharedPtr<FJsonObject> Settings = Row->GetObjectField(TEXT("settings"));
		double Distance = 0.0;
		if ((Row->TryGetNumberField(TEXT("distance"), Distance) && Distance > RadiusM) || !StartsShown(Settings))
		{
			continue;
		}
		if (UNiagaraComponent* Component = Start(Row->GetStringField(TEXT("effect")), Transform, SettingsOf(Settings), Parent))
		{
			Out.Add(Component);
		}
		else
		{
			++Skipped;
		}
	}
	UE_LOG(LogSwcEffects, Log, TEXT("effects: %s: %d started within %.0f m, %d with no system"), *FPaths::GetCleanFilename(File), Out.Num(),
		RadiusM, Skipped);
	return Out;
}

bool USwcEffectSubsystem::ReadRegion(const FString& Path, const TMap<FString, int32>& Keys, const TArray<double>& Views,
                                     const TArray<double>& Sizes, FRegionData& Out)
{
	FString Text;
	TSharedPtr<FJsonObject> Doc;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) || !Doc.IsValid())
	{
		return false;
	}
	TSet<FIntVector4> Seen;   // the same effect at the same place (a slice placed twice): one placement
	for (const TSharedPtr<FJsonValue>& Value : Doc->GetArrayField(TEXT("rows")))
	{
		++Out.Rows;
		const TSharedPtr<FJsonObject> Row = Value->AsObject();
		const int32* Type = Keys.Find(Row->GetStringField(TEXT("effect")).ToLower());
		const TSharedPtr<FJsonObject> Settings = Row->GetObjectField(TEXT("settings"));
		if (Type == nullptr || !StartsShown(Settings))
		{
			++Out.Skipped;
			continue;
		}
		FPlacement Placement;
		Placement.Transform = UnrealTransform(Row->GetArrayField(TEXT("matrix")));
		const FVector L = Placement.Transform.GetLocation();
		bool bAlready = false;
		Seen.Add(FIntVector4(FMath::RoundToInt(L.X), FMath::RoundToInt(L.Y), FMath::RoundToInt(L.Z), *Type), &bAlready);
		if (bAlready)
		{
			++Out.Skipped;
			continue;
		}
		Placement.Type = *Type;
		Placement.Settings = SettingsOf(Settings);
		// The range: the outer LOD bound (not multiplied by V), and the view distance of the emitter with M x S x V (RangeOf).
		Placement.RangeCm = Views[*Type];
		Placement.SizeM = Sizes[*Type] * Placement.Settings.SizeScale * Placement.Transform.GetScale3D().GetAbsMax()
			* Placement.Settings.ViewDistanceMultiplier;
		Out.Cells.FindOrAdd(CellOf(L)).Add(Out.Placements.Num());
		Out.Placements.Add(MoveTemp(Placement));
	}
	return true;
}

bool USwcEffectSubsystem::RegionsNear(const FVector& View, double ReachCm, TArray<FRegionData*>& Out)
{
	bool bAllRead = true;
	const double X = View.X / 100.0;
	const double Y = -View.Y / 100.0;
	const double Reach = ReachCm / 100.0;
	TArray<double> Views;
	TArray<double> Sizes;
	for (const FType& Type : Types)
	{
		Views.Add(Type.ViewCm);
		Sizes.Add(Type.MaxSize);
	}
	for (int32 RX = FMath::FloorToInt((X - Reach) / RegionM); RX <= FMath::FloorToInt((X + Reach) / RegionM); ++RX)
	{
		for (int32 RY = FMath::FloorToInt((Y - Reach) / RegionM); RY <= FMath::FloorToInt((Y + Reach) / RegionM); ++RY)
		{
			if (RX < 0 || RY < 0)
			{
				continue;
			}
			const FIntPoint Key(RX, RY);
			FRegion& Region = Regions.FindOrAdd(Key);
			if (!Region.bStarted)
			{
				Region.bStarted = true;
				Region.Data = MakeShared<FRegionData, ESPMode::ThreadSafe>();
				Region.bRead = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
				const FString Path = FPaths::Combine(Folder(), TEXT("placements"), FString::Printf(TEXT("r_+%02d_+%02d.json"), RX, RY));
				if (!FPaths::FileExists(Path))
				{
					Region.bNoFile = true;
					*Region.bRead = true;
					continue;
				}
				TSharedPtr<FRegionData, ESPMode::ThreadSafe> Data = Region.Data;
				TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> bRead = Region.bRead;
				TMap<FString, int32> Keys = TypeIndex;
				Async(EAsyncExecution::ThreadPool, [Path, Keys, Views, Sizes, Data, bRead]()
				{
					ReadRegion(Path, Keys, Views, Sizes, *Data);
					*bRead = true;
				});
			}
			if (!*Region.bRead)
			{
				bAllRead = false;
				continue;
			}
			if (!Region.bNoFile)
			{
				Out.Add(Region.Data.Get());
			}
		}
	}
	return bAllRead;
}

void USwcEffectSubsystem::Update(const FVector& View, int32 Budget)
{
	TArray<FRegionData*> Near;
	RegionsNear(View, MaxViewCm * ReleaseFactor, Near);
	const double Now = FPlatformTime::Seconds();
	// Start the placements within their range (the cells near the view).
	const FIntPoint Center = CellOf(View);
	const int32 Span = FMath::CeilToInt(MaxViewCm / CellCm);
	int32 Count = 0;
	for (TPair<FIntPoint, FRegion>& Pair : Regions)
	{
		if (!Pair.Value.bStarted || Pair.Value.bNoFile || !*Pair.Value.bRead)
		{
			continue;
		}
		FRegionData& Data = *Pair.Value.Data;
		for (int32 DX = -Span; DX <= Span && Count < Budget; ++DX)
		{
			for (int32 DY = -Span; DY <= Span && Count < Budget; ++DY)
			{
				const TArray<int32>* Cell = Data.Cells.Find(Center + FIntPoint(DX, DY));
				if (Cell == nullptr)
				{
					continue;
				}
				for (const int32 Index : *Cell)
				{
					FPlacement& Placement = Data.Placements[Index];
					if (Placement.Component.IsValid() || FVector::Dist(Placement.Transform.GetLocation(), View) > RangeOf(Placement))
					{
						continue;
					}
					UNiagaraComponent* Component = Acquire(Placement.Type);
					if (Component == nullptr)
					{
						continue;
					}
					Component->SetWorldLocationAndRotation(Placement.Transform.GetLocation(), Placement.Transform.GetRotation());
					SetParameters(Component, Placement.Settings, Placement.Transform.GetScale3D());
					Component->Activate(true);
					Placement.Component = Component;
					Placement.StartedAt = Now;
					Active.Add(TPair<FIntPoint, int32>(Pair.Key, Index));
					if (++Count >= Budget)
					{
						break;
					}
				}
			}
		}
	}
	// Return the placements beyond their range to the pool.
	for (auto It = Active.CreateIterator(); It; ++It)
	{
		FRegion* Region = Regions.Find(It->Key);
		FPlacement& Placement = Region->Data->Placements[It->Value];
		if (!Placement.Component.IsValid())
		{
			It.RemoveCurrent();
			continue;
		}
		if (FVector::Dist(Placement.Transform.GetLocation(), View) > RangeOf(Placement) * ReleaseFactor)
		{
			Release(Placement.Type, Placement.Component.Get(), true);
			Placement.Component = nullptr;
			It.RemoveCurrent();
		}
	}
}

double USwcEffectSubsystem::RangeOf(const FPlacement& Placement) const
{
	if (PixelsPerRadian <= 0.0 || Placement.SizeM <= 0.0)
	{
		return Placement.RangeCm;
	}
	return FMath::Min(Placement.RangeCm, PixelsPerRadian * Placement.SizeM * 100.0);
}

bool USwcEffectSubsystem::ReadyNear(const FVector& Place, FString& Why)
{
	TArray<FRegionData*> Near;
	if (!RegionsNear(Place, MaxViewCm * ReleaseFactor, Near))
	{
		Why = TEXT("the placement files are read");
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	const double Near2 = FMath::Square(SwcLoadingGate::NearCm);
	int32 Missing = 0;
	int32 Young = 0;
	for (FRegionData* Data : Near)
	{
		for (const FPlacement& Placement : Data->Placements)
		{
			const double D2 = FVector::DistSquared(Placement.Transform.GetLocation(), Place);
			const FType& Type = Types[Placement.Type];
			if (D2 > Near2 || D2 > FMath::Square(RangeOf(Placement)) || (Type.bTried && Type.System == nullptr))
			{
				continue;
			}
			if (!Placement.Component.IsValid())
			{
				++Missing;
			}
			else if (Now - Placement.StartedAt < WarmSeconds)
			{
				++Young;
			}
		}
	}
	Why = FString::Printf(TEXT("%d effect(s) to start, %d warming"), Missing, Young);
	return Missing == 0 && Young == 0;
}

void USwcEffectSubsystem::RunTest(const FString& Spec)
{
	for (const TWeakObjectPtr<UNiagaraComponent>& Component : TestComponents)
	{
		Stop(Component.Get(), true);
	}
	TestComponents.Reset();
	if (!Spec.IsEmpty())
	{
		TestSpec = Spec;
		bTestDone = false;
		SpawnTest(Spec);
	}
}

void USwcEffectSubsystem::SpawnTest(const FString& Spec)
{
	// KEY,COUNT: COUNT copies in a square grid of 1 m, from 5 m in front of the camera (the frame cost check of docs/PARTICLES.md).
	TArray<FString> Parts;
	Spec.ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() < 2)
	{
		bTestDone = true;
		return;
	}
	const FString Key = Parts[0];
	const double DistanceCm = Parts.Num() > 2 ? FCString::Atod(*Parts[2]) * 100.0 : 500.0;
	FSwcEffectSettings Settings;
	Settings.SizeScale = Parts.Num() > 3 ? FCString::Atof(*Parts[3]) : 1.0f;
	const double SideCm = Parts.Num() > 4 ? FCString::Atod(*Parts[4]) * 100.0 : 0.0;
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (Controller == nullptr || Controller->PlayerCameraManager == nullptr || Controller->GetPawn() == nullptr)
	{
		return;
	}
	FVector Place;
	if (SwcLoadingGate::GetPlace(GetWorld(), Place))
	{
		return;   // a loading screen shows
	}
	bTestDone = true;
	const int32 Count = FCString::Atoi(*Parts[1]);
	const int32 Side = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(double(Count))));
	const FVector Camera = Controller->PlayerCameraManager->GetCameraLocation();
	const FRotator Yaw(0.0, Controller->PlayerCameraManager->GetCameraRotation().Yaw, 0.0);
	const FVector Forward = Yaw.Vector();
	const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
	int32 Placed = 0;
	for (int32 I = 0; I < Count; ++I)
	{
		// On the ground in front of the character (its feet: the capsule bottom), in the direction of the camera.
		const APawn* Pawn = Controller->GetPawn();
		const FVector Feet = Pawn->GetActorLocation() - FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight());
		const FVector Location = Feet + Forward * (DistanceCm + (I / Side) * 100.0) + Right * (((I % Side) - Side / 2) * 100.0 + SideCm);
		if (UNiagaraComponent* Component = Start(Key, FTransform(Location), Settings))
		{
			TestComponents.Add(Component);
			++Placed;
		}
	}
	UE_LOG(LogSwcEffects, Log, TEXT("effects: the test placed %d of %d %s"), Placed, Count, *Key);
}

void USwcEffectSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (const bool bShow = CVarShow.GetValueOnGameThread() != 0; bShow != bShown && Owner.IsValid())
	{
		bShown = bShow;
		TArray<UNiagaraComponent*> Components;
		Owner->GetComponents(Components);
		for (UNiagaraComponent* Component : Components)
		{
			Component->SetVisibility(bShown);
		}
	}
	if (const float Emission = CVarEmission.GetValueOnGameThread(); Emission != AppliedEmission)
	{
		if (Collection == nullptr)
		{
			Collection = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/SwcGameContent/Particles/MPC_SwcParticles.MPC_SwcParticles"));
		}
		if (Collection != nullptr)
		{
			UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), Collection, TEXT("EmissionUnit"), Emission);
			UE_LOG(LogSwcEffects, Log, TEXT("effects: the emission unit %.4f"), Emission);
		}
		AppliedEmission = Emission;
	}
	if (!TestSpec.IsEmpty() && !bTestDone)
	{
		SpawnTest(TestSpec);
	}
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextReport && Owner.IsValid())
	{
		NextReport = Now + 10.0;
		TArray<UNiagaraComponent*> Components;
		Owner->GetComponents(Components);
		int32 Running = 0;
		for (const UNiagaraComponent* Component : Components)
		{
			Running += Component->IsActive();
		}
		UE_LOG(LogSwcEffects, Log, TEXT("effects: %d active component(s) of %d, %d world placement(s), %d region file(s), %d started by callers"),
			Running, Components.Num(), Active.Num(), Regions.Num(), Started.Num());
	}
	if (!bWorld)
	{
		return;
	}
	FVector Place;
	const bool bScreen = SwcLoadingGate::GetPlace(GetWorld(), Place);
	if (!bScreen && Now < NextUpdate)
	{
		return;
	}
	NextUpdate = Now + UpdateSeconds;
	FVector View;
	if (!SwcLoadingGate::GetView(GetWorld(), View))
	{
		return;
	}
	// The view distance rule: the pixels per radian of the screen height x P (particle-rules.md 4).
	const float MinPixels = CVarMinDrawPixels.GetValueOnGameThread();
	PixelsPerRadian = 0.0;
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (MinPixels > 0.0f && Controller != nullptr && Controller->PlayerCameraManager != nullptr)
	{
		int32 Width = 0, Height = 0;
		Controller->GetViewportSize(Width, Height);
		const double Aspect = Height > 0 ? double(Width) / double(Height) : 16.0 / 9.0;
		const double Horizontal = FMath::DegreesToRadians(double(Controller->PlayerCameraManager->GetFOVAngle()));
		const double Vertical = 2.0 * FMath::Atan(FMath::Tan(Horizontal * 0.5) / Aspect);
		if (Height > 0 && Vertical > 0.0)
		{
			PixelsPerRadian = double(Height) / Vertical * FMath::Max(double(MinPixels), 0.125);
		}
	}
	Update(View, bScreen ? MAX_int32 : PlayBudget);
}
