#include "Effects/SwcTracers.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Effects/SwcParticleEffects.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcTracers, Log, All);

namespace
{
/** The attacks of the shots of the creatures: the slice (lower case) -> the CRC-32 of the AmmoID -> the attack. */
TMap<FString, TMap<uint32, FString>> CreatureShots;

/** The trail effects of each attack by weapon effect (loaded once; the game thread). */
const TMap<FString, TMap<FString, FString>>& Attacks()
{
	static TMap<FString, TMap<FString, FString>> Out;
	static bool bLoaded = false;
	if (bLoaded)
	{
		return Out;
	}
	bLoaded = true;
	const FString Path = FPaths::Combine(USwcEffectSubsystem::Folder(), TEXT("tracers.json"));
	FString Text;
	TSharedPtr<FJsonObject> Doc;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) || !Doc.IsValid())
	{
		UE_LOG(LogSwcTracers, Warning, TEXT("tracers: no %s (tools/convert-particles.sh skills)"), *Path);
		return Out;
	}
	for (const auto& Attack : Doc->GetObjectField(TEXT("attacks"))->Values)
	{
		TMap<FString, FString>& Effects = Out.Add(FString(Attack.Key.ToView()));
		for (const auto& Effect : Attack.Value->AsObject()->GetObjectField(TEXT("effects"))->Values)
		{
			Effects.Add(FString(Effect.Key.ToView()), Effect.Value->AsString());
		}
	}
	const TSharedPtr<FJsonObject>* Shots = nullptr;
	if (Doc->TryGetObjectField(TEXT("creatureShots"), Shots))
	{
		for (const auto& Slice : (*Shots)->Values)
		{
			TMap<uint32, FString>& Ammo = CreatureShots.Add(FString(Slice.Key.ToView()));
			for (const auto& Pair : Slice.Value->AsObject()->Values)
			{
				Ammo.Add(uint32(FCString::Strtoui64(*FString(Pair.Key.ToView()), nullptr, 10)), Pair.Value->AsString());
			}
		}
	}
	UE_LOG(LogSwcTracers, Log, TEXT("tracers: %d attacks of the ranged attack database, the shots of %d creature slices"), Out.Num(),
		CreatureShots.Num());
	return Out;
}
} // namespace

FString SwcTracers::Key(const FString& Attack, const FString& Element)
{
	const TMap<FString, FString>* Effects = Attacks().Find(Attack);
	if (Effects == nullptr)
	{
		return FString();
	}
	const FString* Key = Effects->Find(Element);
	return Key != nullptr ? *Key : Effects->FindRef(TEXT("_Default"));
}

FString SwcTracers::CreatureAttack(const FString& SlicePath, uint32 AmmoKey)
{
	Attacks();
	const TMap<uint32, FString>* Ammo = CreatureShots.Find(SlicePath.ToLower());
	return Ammo != nullptr ? Ammo->FindRef(AmmoKey) : FString();
}

namespace
{
UNiagaraComponent* StartTracerWith(UWorld* World, const FString& Attack, const FString& Element, USceneComponent* Projectile, const FTransform& Relative);
}

UNiagaraComponent* SwcTracers::Start(UWorld* World, const FString& Attack, const FString& Element, USceneComponent* Projectile)
{
	// The local +Y of the game is -Y in Unreal (the space map of stage 1.1): a turn of -90 degrees about X puts it on the +Z of the
	// projectile.
	return StartTracerWith(World, Attack, Element, Projectile, FTransform(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(-90.0))));
}

UNiagaraComponent* SwcTracers::StartInGameFrame(UWorld* World, const FString& Attack, const FString& Element, USceneComponent* Projectile)
{
	return StartTracerWith(World, Attack, Element, Projectile, FTransform::Identity);
}

namespace
{
UNiagaraComponent* StartTracerWith(UWorld* World, const FString& Attack, const FString& Element, USceneComponent* Projectile, const FTransform& Relative)
{
	USwcEffectSubsystem* Effects = World != nullptr ? World->GetSubsystem<USwcEffectSubsystem>() : nullptr;
	const FString Effect = SwcTracers::Key(Attack, Element);
	if (Effects == nullptr || Projectile == nullptr || Effect.IsEmpty())
	{
		return nullptr;
	}
	UNiagaraComponent* Trail = Effects->StartAttached(Effect, Projectile, NAME_None, Relative, true, FSwcEffectSettings());
	if (Trail == nullptr)
	{
		UE_LOG(LogSwcTracers, Log, TEXT("tracers: %s of %s %s has no system"), *Effect, *Attack, *Element);
		return nullptr;
	}
	// The projectile shape has its own scale (an arrow 3 cm x 0.8 m): the trail keeps the scale of the world.
	Trail->SetUsingAbsoluteScale(true);
	Trail->SetWorldScale3D(FVector::OneVector);
	UE_LOG(LogSwcTracers, Log, TEXT("tracers: %s on the projectile of %s (%s)"), *Effect, *Attack, *Element);
	return Trail;
}
} // namespace

void SwcTracers::Stop(UWorld* World, UNiagaraComponent* Trail)
{
	USwcEffectSubsystem* Effects = World != nullptr ? World->GetSubsystem<USwcEffectSubsystem>() : nullptr;
	if (Effects == nullptr || Trail == nullptr)
	{
		return;
	}
	Trail->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Effects->Stop(Trail, false);
}
