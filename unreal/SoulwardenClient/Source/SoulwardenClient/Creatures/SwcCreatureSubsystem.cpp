#include "Creatures/SwcCreatureSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "Creatures/SwcCreature.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/SwcSpace.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "SwcBackendSubsystem.h"

DEFINE_LOG_CATEGORY(LogSwcCreatures);

namespace
{
/** The plate of a development picture (-SwcPlates=all): each creature within this distance (m). */
constexpr double AllPlatesRange = 40.0;
/** The crosshair rule of the marker (m_nearAngle at the near range, down to m_farAngle at m_angleShrinkDistance): a plate
 *  farther than this shows only by the crosshair (an inference: the far limit of the marker is not read). */
constexpr double CrosshairRange = 50.0;
/** The plate of a dead creature goes away some seconds after the dead mark (protocol notes 4.14; the time is an inference). */
constexpr double DeadPlateSeconds = 3.0;
/** The body of a dead creature: the client removes it about 5 minutes later (the delayed event of 300 s of the slice). */
constexpr double DeadBodySeconds = 300.0;
const FLinearColor PlateBack(0.02f, 0.02f, 0.02f, 0.7f);
const FLinearColor HealthFill(0.62f, 0.07f, 0.05f, 1.0f);
const FLinearColor ImmuneFill(0.55f, 0.55f, 0.55f, 1.0f);
} // namespace

void USwcCreatureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Plates;
	bAllPlates = FParse::Value(FCommandLine::Get(), TEXT("SwcPlates="), Plates) && Plates == TEXT("all");
	DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &USwcCreatureSubsystem::DrawPlates));
}

void USwcCreatureSubsystem::Deinitialize()
{
	UDebugDrawService::Unregister(DrawHandle);
	Clear(TEXT("the world ends"));
	Super::Deinitialize();
}

bool USwcCreatureSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcCreatureSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcCreatureSubsystem, STATGROUP_Tickables);
}

void USwcCreatureSubsystem::Clear(const TCHAR* Cause)
{
	if (Spawned > 0 || !Actors.IsEmpty())
	{
		UE_LOG(LogSwcCreatures, Log, TEXT("creatures: %s: %d creature(s) made (%d placeholder(s)), %d removed, %d action(s), %d death(s), %d kind(s)"), Cause, Spawned,
			Placeholders, Removed, Actions, Deaths, Kinds.Num());
	}
	for (const TPair<uint32, TWeakObjectPtr<ASwcCreature>>& Pair : Actors)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->Destroy();
		}
	}
	Actors.Reset();
	Table.clear();
	Table.take_changes();
	Spawned = Removed = Actions = Deaths = Placeholders = 0;
	Kinds.Reset();
}

void USwcCreatureSubsystem::Spawn(const swc::creatures::Entity& Entity)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	if (!Library.IsLoaded())
	{
		FString Report;
		Library.Load(Report);
		UE_LOG(LogSwcCreatures, Log, TEXT("%s"), *Report);
	}
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Feet = SwcEngineToUnreal(Entity.samples.back().position);
	ASwcCreature* Creature = World->SpawnActor<ASwcCreature>(Feet, FRotator::ZeroRotator, Parameters);
	if (Creature == nullptr)
	{
		return;
	}
	Creature->Setup(Library, Entity);
	Actors.Add(Entity.interest, Creature);
	++Spawned;
	const FString Key = UTF8_TO_TCHAR(Entity.slice_key().c_str());
	if (!Creature->HasModel())
	{
		++Placeholders;
		if (Library.AddWanted(Key))
		{
			UE_LOG(LogSwcCreatures, Warning, TEXT("creature: the slice %s (%s) has no imported model: it is in the wanted list (tools/convert-creatures.sh wanted, tools/import-creatures.sh)"),
				*Key, *Creature->GetSlicePath());
		}
	}
	const auto& Position = Entity.samples.back().position;
	UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: %s, level %u, health %.0f/%.0f%s, slice %s, %s, at %.2f %.2f %.2f"), Entity.interest,
		Entity.hasVitals ? *Library.Name(Entity.categoryId, Entity.vitalsId) : TEXT("(no vitals yet)"), Entity.level, Entity.health, Entity.healthMax,
		Entity.dead ? TEXT(", dead") : TEXT(""), *Creature->GetSlicePath(), Creature->HasModel() ? TEXT("body") : TEXT("placeholder"), Position.x, Position.y,
		Position.z);
}

void USwcCreatureSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UGameInstance* GameInstance = GetWorld() != nullptr ? GetWorld()->GetGameInstance() : nullptr;
	USwcBackendSubsystem* Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USwcBackendSubsystem>() : nullptr;
	std::vector<swc::net::RepClient::OtherUpdate> Updates;
	if (Backend == nullptr || !Backend->TakeOtherUpdates(Updates))
	{
		if (bConnected)
		{
			bConnected = false;
			Clear(TEXT("the connection ended"));
		}
		return;
	}
	bConnected = true;
	for (const swc::net::RepClient::OtherUpdate& Update : Updates)
	{
		if (Update.stop)
		{
			Table.stop(Update.interest);
		}
		else
		{
			Table.apply(Update.record);
		}
	}
	for (const swc::creatures::Change& Change : Table.take_changes())
	{
		const swc::creatures::Entity* Entity = Table.find(Change.interest);
		TWeakObjectPtr<ASwcCreature>* Actor = Actors.Find(Change.interest);
		const bool bStale = Actor != nullptr && (!Actor->IsValid() || (*Actor)->GetSerial() != Change.serial);
		if (Change.kind == swc::creatures::Change::Kind::removed || bStale)
		{
			if (Actor != nullptr && Actor->IsValid() && (Change.kind == swc::creatures::Change::Kind::removed ? (*Actor)->GetSerial() == Change.serial : true))
			{
				UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: removed (%s)"), Change.interest, *(*Actor)->GetDisplayName());
				(*Actor)->Destroy();
				Actors.Remove(Change.interest);
				++Removed;
			}
			continue;
		}
		if (Entity == nullptr || Actor == nullptr || !Actor->IsValid())
		{
			continue;
		}
		if (Change.kind == swc::creatures::Change::Kind::health)
		{
			UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: %s health %.1f/%.1f"), Change.interest, *(*Actor)->GetDisplayName(), Entity->health, Entity->healthMax);
		}
		else if (Change.kind == swc::creatures::Change::Kind::death)
		{
			++Deaths;
			UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: %s is dead"), Change.interest, *(*Actor)->GetDisplayName());
		}
	}
	const uint64 ServerNowUs = Backend->GetServerTimeMicroseconds();
	if (ServerNowUs == 0)
	{
		return;
	}
	const uint64 RenderTimeUs = ServerNowUs - static_cast<uint64>(InterpolationDelaySeconds * 1.0e6);
	for (const auto& [Interest, Entity] : Table.entities())
	{
		TWeakObjectPtr<ASwcCreature>* Actor = Actors.Find(Interest);
		if (Actor == nullptr || !Actor->IsValid())
		{
			if (Entity.hasMetadata && Entity.hasPosition())
			{
				Spawn(Entity);
				Actor = Actors.Find(Interest);
			}
			if (Actor == nullptr || !Actor->IsValid())
			{
				continue;
			}
		}
		ASwcCreature* Creature = Actor->Get();
		const FString Action = Creature->UpdateFromEntity(Library, Entity, RenderTimeUs, DeltaTime);
		if (!Action.IsEmpty())
		{
			++Actions;
			Kinds.Add(Creature->GetDisplayName());
			UE_LOG(LogSwcCreatures, Log, TEXT("creature %u: %s: %s"), Interest, *Creature->GetDisplayName(), *Action);
		}
		// The body of a dead creature goes away after the delayed event of its slice (the server keeps the entity).
		const double DeadSeconds = Creature->GetDeadSeconds();
		Creature->SetActorHiddenInGame(DeadSeconds > DeadBodySeconds);
	}
}

void USwcCreatureSubsystem::GetTargets(TArray<FSwcCreatureTarget>& Out) const
{
	Out.Reset();
	for (const auto& [Interest, Entity] : Table.entities())
	{
		const TWeakObjectPtr<ASwcCreature>* Actor = Actors.Find(Interest);
		if (Actor != nullptr && Actor->IsValid())
		{
			Out.Add({Interest, (*Actor)->GetActorLocation(), Entity.dead});
		}
	}
}

void USwcCreatureSubsystem::DrawPlates(UCanvas* Canvas, APlayerController* Controller)
{
	if (Canvas == nullptr || GetWorld() == nullptr || Canvas->SceneView == nullptr || Canvas->SceneView->Family == nullptr
		|| Canvas->SceneView->Family->Scene == nullptr || Canvas->SceneView->Family->Scene->GetWorld() != GetWorld())
	{
		return;
	}
	APlayerController* Player = Controller != nullptr ? Controller : GetWorld()->GetFirstPlayerController();
	if (Player == nullptr || Player->PlayerCameraManager == nullptr)
	{
		return;
	}
	const FVector Eye = Player->PlayerCameraManager->GetCameraLocation();
	const FVector Look = Player->PlayerCameraManager->GetCameraRotation().Vector();
	const FVector Own = Player->GetPawn() != nullptr ? Player->GetPawn()->GetActorLocation() : Eye;
	UFont* Font = GEngine != nullptr ? GEngine->GetSmallFont() : nullptr;
	if (Font == nullptr)
	{
		return;
	}
	for (const auto& [Interest, Entity] : Table.entities())
	{
		const TWeakObjectPtr<ASwcCreature>* Actor = Actors.Find(Interest);
		if (Actor == nullptr || !Actor->IsValid() || (*Actor)->IsHidden() || !Entity.hasVitals)
		{
			continue;
		}
		const ASwcCreature* Creature = Actor->Get();
		const double DeadSeconds = Creature->GetDeadSeconds();
		if (DeadSeconds > DeadPlateSeconds || (Entity.hasMarker && !Entity.markerEnabled))
		{
			continue;
		}
		const FVector Point = Creature->GetPlatePoint();
		const double Distance = FVector::Dist(Own, Creature->GetActorLocation()) / 100.0;
		// The rule of the marker of the slice (MarkerComponent client facet): the near range, or the crosshair within an angle
		// that shrinks from the near angle at the near range to the far angle at the shrink distance.
		const FSwcCreatureSlice* Slice = Creature->GetSlice();
		const double NearRange = Slice != nullptr ? Slice->NearRange : 6.0;
		const double NearAngle = Slice != nullptr ? Slice->NearAngle : 0.1309;
		const double FarAngle = Slice != nullptr ? Slice->FarAngle : 0.0174533;
		const double Shrink = Slice != nullptr ? FMath::Max(Slice->AngleShrinkDistance, NearRange + 0.1) : 20.0;
		const FVector Center = Creature->GetActorLocation() + FVector(0.0, 0.0, 80.0 * Creature->GetActorScale3D().Z);
		const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Look, (Center - Eye).GetSafeNormal()), -1.0, 1.0));
		const double Allowed = FMath::Lerp(NearAngle, FarAngle, FMath::Clamp((Distance - NearRange) / (Shrink - NearRange), 0.0, 1.0));
		const bool bShow = Distance <= NearRange || (Distance <= CrosshairRange && Angle <= Allowed) || (bAllPlates && Distance <= AllPlatesRange);
		if (!bShow)
		{
			continue;
		}
		const FVector Screen = Canvas->Project(Point);
		if (Screen.Z <= 0.0)
		{
			continue;
		}
		// The plate: the name with the level (no level mark for a Critter), and the health bar under it.
		const FString Title = Creature->HidesLevel() || Entity.level == 0 ? Creature->GetDisplayName()
			: FString::Printf(TEXT("%s  %u"), *Creature->GetDisplayName(), Entity.level);
		float TextWidth = 0.0f;
		float TextHeight = 0.0f;
		Canvas->StrLen(Font, Title, TextWidth, TextHeight);
		const float BarWidth = 120.0f;
		const float BarHeight = 7.0f;
		const float X = static_cast<float>(Screen.X);
		const float Y = static_cast<float>(Screen.Y);
		FCanvasTextItem Text(FVector2D(X - TextWidth * 0.5f, Y - TextHeight - BarHeight - 2.0f), FText::FromString(Title), Font, FLinearColor::White);
		Text.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Text);
		if (Slice == nullptr || !Slice->bNeverShowHealthBar)
		{
			const float Share = Entity.healthMax > 0.0f ? FMath::Clamp(Entity.health / Entity.healthMax, 0.0f, 1.0f) : 0.0f;
			FCanvasTileItem Back(FVector2D(X - BarWidth * 0.5f - 1.0f, Y - BarHeight - 1.0f), FVector2D(BarWidth + 2.0f, BarHeight + 2.0f), PlateBack);
			Back.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Back);
			FCanvasTileItem Fill(FVector2D(X - BarWidth * 0.5f, Y - BarHeight), FVector2D(BarWidth * Share, BarHeight), Entity.immune ? ImmuneFill : HealthFill);
			Canvas->DrawItem(Fill);
		}
	}
}
