#include "Creatures/SwcEffectActor.h"

#include "Components/StaticMeshComponent.h"
#include "Creatures/SwcCreatureData.h"
#include "Effects/SwcParticleEffects.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Environment/SwcMeterProbe.h"
#include "Net/SwcSpace.h"

namespace
{
/** The engine cylinder: 1 m high and 1 m wide, its origin at its center. */
const TCHAR* CylinderMesh = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
/** An unlit translucent material of the engine with the vector parameter Color. */
const TCHAR* TranslucentMaterial = TEXT("/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent.M_SimpleUnlitTranslucent");
/** The look (values of this client): the shaft of a shot (m), and the colors of the area and of the shot. */
constexpr double ShaftLength = 0.8;
constexpr double ShaftWidth = 0.04;
constexpr double DiscHeight = 0.03;
const FLinearColor ShotColor(1.0f, 0.9f, 0.6f, 0.9f);
const FLinearColor WarningColor(1.0f, 0.45f, 0.0f, 0.35f);
const FLinearColor DangerColor(1.0f, 0.05f, 0.02f, 0.6f);
/** A spell of a character (values of this client): blue before its volume is active, light blue while it is active. */
const FLinearColor FriendlyWarningColor(0.2f, 0.5f, 1.0f, 0.3f);
const FLinearColor FriendlyActiveColor(0.2f, 0.85f, 1.0f, 0.45f);
/** The pillar of a summon (m). */
constexpr double PillarWidth = 0.6;
constexpr double PillarHeight = 2.0;
constexpr double WidestDisc = 8.0;
/** AST-230: an observer that sees a spell later than this after an activation event does not start its effect (an old burst; a value
 *  of this client, docs/EFFECTS.md). An entity that is active from the start of the spell (a field) starts at any time. */
constexpr double LateBurstSeconds = 0.5;
/** Stage MVP-I: the unlit colors follow the exposure of the frame. The scene has physical light units (an exposure of about
 *  1/(1.2 x 2^13.7) at noon), and an unlit color of 1.0 drew near black (the disc of the strike in mvpi-area1). The color is
 *  DisplayLevel / exposure: about the same brightness on the screen at day and at night. The exposure: the last measure of the
 *  meter probe (Environment/SwcMeterProbe); none yet: a color of 1. */
constexpr double DisplayLevel = 0.6;

double ExposureScale()
{
	SwcMeterProbe::FResult Measure;
	return SwcMeterProbe::GetLatest(Measure) && Measure.Exposure > 0.0 ? DisplayLevel / Measure.Exposure : 1.0;
}

FLinearColor Exposed(const FLinearColor& Color, double Scale)
{
	return FLinearColor(Color.R * Scale, Color.G * Scale, Color.B * Scale, Color.A);
}
} // namespace

ASwcEffectActor::ASwcEffectActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Shape = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shape"));
	Shape->SetupAttachment(Root);
	Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shape->SetCastShadow(false);
	Shape->SetVisibility(false);
}

void ASwcEffectActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The particles of the slice end with its entity; their tails finish.
	if (USwcEffectSubsystem* Effects = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USwcEffectSubsystem>() : nullptr)
	{
		for (const TWeakObjectPtr<UNiagaraComponent>& Component : ParticleComponents)
		{
			if (Component.IsValid())
			{
				Effects->Stop(Component.Get(), false);
			}
		}
	}
	ParticleComponents.Reset();
	Super::EndPlay(EndPlayReason);
}

FString ASwcEffectActor::UpdateParticles(const swc::creatures::SpellParts* Parts, uint64 StartUs, uint64 RenderTimeUs)
{
	FString Text;
	USwcEffectSubsystem* Effects = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USwcEffectSubsystem>() : nullptr;
	if (Effects == nullptr || Particles.IsEmpty())
	{
		return Text;
	}
	ParticleComponents.SetNum(Particles.Num());
	ParticleOn.SetNum(Particles.Num());
	const auto Add = [&Text](const FString& Line) { Text += (Text.IsEmpty() ? TEXT("") : TEXT("; ")) + Line; };
	for (int32 Index = 0; Index < Particles.Num(); ++Index)
	{
		const FSwcSliceParticle& Part = Particles[Index];
		bool bOn = Part.bActive && RenderTimeUs >= StartUs;
		uint64 Changed = StartUs;
		if (Parts != nullptr)
		{
			const std::string Target = TCHAR_TO_UTF8(*Part.Entity);
			for (const swc::creatures::SpellEventData& Event : Events)
			{
				if (Event.target != Target || (Event.type != 2 && Event.type != 3))
				{
					continue;
				}
				const uint64 Due = swc::creatures::event_due_us(*Parts, Event, StartUs);
				if (Due <= RenderTimeUs && Due >= Changed)
				{
					bOn = Event.type == 2;
					Changed = Due;
				}
			}
		}
		const double Since = (static_cast<double>(RenderTimeUs) - static_cast<double>(StartUs)) / 1.0e6;
		if (bOn && !ParticleOn[Index])
		{
			ParticleOn[Index] = true;
			const double Late = static_cast<double>(RenderTimeUs - Changed) / 1.0e6;
			if (Changed > StartUs && Late > LateBurstSeconds)
			{
				Add(FString::Printf(TEXT("the effect %s of %s: an old activation (%.2f s), not started"), *Part.Effect, *Part.Entity, Late));
				continue;
			}
			UNiagaraComponent* Component = Effects->StartAttached(Part.Effect, Root, NAME_None, Part.Transform, true, Part.Settings);
			ParticleComponents[Index] = Component;
			Add(FString::Printf(TEXT("the effect %s of %s at %.2f s%s"), *Part.Effect, *Part.Entity, Since,
				Component != nullptr ? TEXT("") : TEXT(" (no system)")));
		}
		else if (!bOn && ParticleOn[Index])
		{
			ParticleOn[Index] = false;
			if (UNiagaraComponent* Component = ParticleComponents[Index].Get())
			{
				Effects->Stop(Component, Part.bKillOnDeactivate);
			}
			ParticleComponents[Index] = nullptr;
			Add(FString::Printf(TEXT("the effect %s of %s stops at %.2f s"), *Part.Effect, *Part.Entity, Since));
		}
	}
	return Text;
}

void ASwcEffectActor::SetColor(const FLinearColor& Color)
{
	if (Material == nullptr)
	{
		UMaterialInterface* Base = FindObject<UMaterialInterface>(nullptr, TranslucentMaterial);   // in memory: the common assets of the creature view
		if (Base == nullptr)
		{
			return;
		}
		Material = UMaterialInstanceDynamic::Create(Base, this);
		Shape->SetMaterial(0, Material);
	}
	ShapeColor = Color;
	AppliedScale = ExposureScale();
	Material->SetVectorParameterValue(TEXT("Color"), Exposed(Color, AppliedScale));
	Material->SetScalarParameterValue(TEXT("Opacity"), Color.A);
}

void ASwcEffectActor::FollowExposure()
{
	const double Scale = ExposureScale();
	if (Material != nullptr && FMath::Abs(Scale - AppliedScale) > 0.1 * AppliedScale)
	{
		SetColor(ShapeColor);
	}
}

void ASwcEffectActor::SetupProjectile(const FSwcProjectileSlice* Slice)
{
	bArea = false;
	if (Slice != nullptr)
	{
		Flight = Slice->Flight;
		Particles = Slice->Particles;
	}
	// A shot with a particle effect of the game shows that effect, not the shaft.
	USwcEffectSubsystem* Effects = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USwcEffectSubsystem>() : nullptr;
	bShaft = !Particles.ContainsByPredicate([Effects](const FSwcSliceParticle& Part) { return Effects != nullptr && Effects->HasEffect(Part.Effect); });
	Shape->SetStaticMesh(FindObject<UStaticMesh>(nullptr, CylinderMesh));
	// The shaft along local Z, its tip at the place of the shot.
	Shape->SetRelativeScale3D(FVector(ShaftWidth, ShaftWidth, ShaftLength));
	Shape->SetRelativeLocation(FVector(0.0, 0.0, -ShaftLength * 50.0));
	SetColor(ShotColor);
}

void ASwcEffectActor::SetupArea(const FSwcSpellSlice* Slice, const FSwcSpellRow* Row, bool bInFriendly, bool bInSummon)
{
	bArea = true;
	bFriendly = bInFriendly;
	if (Slice != nullptr)
	{
		Events = Slice->Events;
		bHasVolume = Slice->bHasVolume;
		Volume = Slice->Volume;
		Particles = Slice->Particles;
	}
	// The size of the area: the sphere or the cylinder of the volume of the slice; a volume with no shape takes the shape of the
	// spell row (ShapeTypes Sphere, Radius: the lightning strike).
	if (bHasVolume && Volume.shape == "sphere")
	{
		Radius = Volume.radius;
	}
	else if (bHasVolume && Volume.shape == "cylinder")
	{
		Radius = Volume.shapeRadius;
	}
	else if (Row != nullptr)
	{
		Radius = Row->Radius;
	}
	Shape->SetStaticMesh(FindObject<UStaticMesh>(nullptr, CylinderMesh));
	bSummon = bInSummon || Radius > WidestDisc;
	if (bSummon)
	{
		Shape->SetRelativeScale3D(FVector(PillarWidth, PillarWidth, PillarHeight));
		Shape->SetRelativeLocation(FVector(0.0, 0.0, PillarHeight * 50.0));
	}
	else
	{
		Shape->SetRelativeScale3D(FVector(Radius * 2.0, Radius * 2.0, DiscHeight));
		Shape->SetRelativeLocation(FVector(0.0, 0.0, DiscHeight * 50.0));
	}
	SetColor(bFriendly ? FriendlyWarningColor : WarningColor);
}

FString ASwcEffectActor::UpdateFromEntity(const swc::creatures::Entity& Entity, uint64 RenderTimeUs, const FVector* Follow)
{
	FollowExposure();
	FString Text;
	if (!bArea && Entity.hasProjectile)
	{
		const swc::creatures::Projectile& Shot = Entity.projectile;
		bool bNowHeld = false;
		const swc::net::WorldPosition Place = swc::creatures::projectile_place(Shot, Flight, RenderTimeUs, &bNowHeld);
		const std::array<float, 3> Velocity = swc::creatures::projectile_velocity(Shot, Flight, RenderTimeUs);
		// A direction has the map of a position with no scale (Net/SwcSpace.h).
		const FVector Direction(Velocity[0], -Velocity[1], Velocity[2]);
		if (!Direction.IsNearlyZero())
		{
			LastDirection = Direction.GetSafeNormal();
		}
		SetActorLocationAndRotation(SwcEngineToUnreal(Place), FRotationMatrix::MakeFromZ(LastDirection).ToQuat());
		// The shot shows from its launch on the render clock.
		const bool bLaunched = Shot.hasLaunch ? RenderTimeUs >= Shot.launchUs : Shot.collided;
		Shape->SetVisibility(bShaft && bLaunched);
		if (bLaunched)
		{
			Text = UpdateParticles(nullptr, Shot.hasLaunch ? Shot.launchUs : RenderTimeUs, RenderTimeUs);
		}
		if (bNowHeld && !bHeld)
		{
			Text += FString::Printf(TEXT("%sthe shot holds at the contact %.2f %.2f %.2f"), Text.IsEmpty() ? TEXT("") : TEXT("; "),
				Shot.contact.x, Shot.contact.y, Shot.contact.z);
		}
		bHeld = bNowHeld;
		return Text;
	}
	if (bArea && Entity.hasSpellParts)
	{
		const swc::creatures::SpellParts& Parts = Entity.spell;
		const swc::net::WorldPosition& Place = Parts.hasSpell ? Parts.place : Entity.worldPlace;
		// A spell that follows an entity (field 6: the Beacon) is at the shown place of that entity.
		FVector Where = Follow != nullptr ? *Follow : SwcEngineToUnreal(Place);
		// The disc lies on the ground below or above the place of the server (the place can be some cm under the surface of the
		// level): a line of 1 m up and down to the static world.
		if (!bSummon && GetWorld() != nullptr && !Where.Equals(LastPlace, 1.0))
		{
			LastPlace = Where;
			FHitResult Ground;
			if (GetWorld()->LineTraceSingleByObjectType(Ground, Where + FVector(0.0, 0.0, 100.0), Where - FVector(0.0, 0.0, 100.0),
					FCollisionObjectQueryParams(ECC_WorldStatic)))
			{
				GroundOffset = Ground.ImpactPoint.Z - Where.Z;
			}
		}
		Where.Z += GroundOffset;
		SetActorLocation(Where);
		const swc::creatures::AreaTimes Times = bHasVolume ? swc::creatures::area_times(Parts, Volume, Events)
														  : swc::creatures::AreaTimes{swc::creatures::spell_start_us(Parts, Events), 0, 0};
		const swc::creatures::AreaPhase Phase = swc::creatures::area_phase(Times, RenderTimeUs);
		const bool bStarted = RenderTimeUs >= Times.startUs;
		// A hostile area: no disc; only the particle entities of its slice (the decision of the user, AST-230).
		const bool bHostile = !bSummon && !bFriendly;
		Shape->SetVisibility(bStarted && !bHostile && (bSummon || (Phase != swc::creatures::AreaPhase::ended && Radius > 0.0f)));
		const FString ParticleText = UpdateParticles(&Parts, Times.startUs, RenderTimeUs);
		const int32 PhaseIndex = bStarted ? static_cast<int32>(Phase) : -1;
		if (PhaseIndex != LastPhase)
		{
			LastPhase = PhaseIndex;
			const double Since = (static_cast<double>(RenderTimeUs) - static_cast<double>(Times.startUs)) / 1.0e6;
			switch (Phase)
			{
			case swc::creatures::AreaPhase::warning:
				SetColor(bFriendly ? FriendlyWarningColor : WarningColor);
				Text = bFriendly ? FString::Printf(TEXT("the spell before its volume is active (radius %.2f m) at %.2f s"), Radius, Since)
								 : FString::Printf(TEXT("the warning of the area (radius %.2f m) at %.2f s"), Radius, Since);
				break;
			case swc::creatures::AreaPhase::danger:
				SetColor(bFriendly ? FriendlyActiveColor : DangerColor);
				Text = bFriendly ? FString::Printf(TEXT("the volume of the spell is active at %.2f s"), Since)
								 : FString::Printf(TEXT("the danger of the area at %.2f s"), Since);
				break;
			case swc::creatures::AreaPhase::ended:
				Text = bFriendly ? FString::Printf(TEXT("the end of the volume of the spell at %.2f s"), Since)
								 : FString::Printf(TEXT("the end of the area at %.2f s"), Since);
				break;
			}
			if (!bStarted)
			{
				Text.Reset();
			}
		}
		if (!ParticleText.IsEmpty())
		{
			Text += (Text.IsEmpty() ? TEXT("") : TEXT("; ")) + ParticleText;
		}
		return Text;
	}
	return Text;
}
