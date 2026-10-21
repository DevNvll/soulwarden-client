#include "Creatures/SwcEffectActor.h"

#include "Components/StaticMeshComponent.h"
#include "Creatures/SwcCreatureData.h"
#include "Engine/StaticMesh.h"
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
/** The light of a hostile area (the decision of the user, 2026-10-21: "Match retail now"). Retail draws no warning on the ground; it
 *  shows the light effect of the spell at its hit time (the strike of the shaman: a column of light at its danger time; Soulwarden
 *  evidence creature-areas s1-fresh). Until the particle stage the client shows a bright column at the danger time only (at least
 *  0.3 s, the danger minimum of the area), and no disc and nothing in the warning. A spell of a character and a summon keep their
 *  disc and pillar. Values of this client. */
constexpr double ColumnHeight = 4.0;
constexpr double ColumnWidth = 1.0;   // m: a bolt, not the width of the area (a column of the area width veiled the screen near it)
const FLinearColor ColumnFlashColor(1.0f, 0.95f, 0.8f, 0.6f);
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
	Column = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Column"));
	Column->SetupAttachment(Root);
	Column->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Column->SetCastShadow(false);
	Column->SetVisibility(false);
}

void ASwcEffectActor::SetColumnColor(const FLinearColor& Color)
{
	if (ColumnMaterial == nullptr)
	{
		UMaterialInterface* Base = FindObject<UMaterialInterface>(nullptr, TranslucentMaterial);
		if (Base == nullptr)
		{
			return;
		}
		ColumnMaterial = UMaterialInstanceDynamic::Create(Base, this);
		Column->SetMaterial(0, ColumnMaterial);
	}
	ColumnColor = Color;
	ColumnMaterial->SetVectorParameterValue(TEXT("Color"), Exposed(Color, ExposureScale()));
	ColumnMaterial->SetScalarParameterValue(TEXT("Opacity"), Color.A);
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
	if (ColumnMaterial != nullptr)
	{
		ColumnMaterial->SetVectorParameterValue(TEXT("Color"), Exposed(ColumnColor, AppliedScale));
	}
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
	}
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
		if (!bFriendly)
		{
			Column->SetStaticMesh(FindObject<UStaticMesh>(nullptr, CylinderMesh));
			Column->SetRelativeScale3D(FVector(ColumnWidth, ColumnWidth, ColumnHeight));
			Column->SetRelativeLocation(FVector(0.0, 0.0, ColumnHeight * 50.0));
			SetColumnColor(ColumnFlashColor);
		}
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
		Shape->SetVisibility(Shot.hasLaunch ? RenderTimeUs >= Shot.launchUs : Shot.collided);
		if (bNowHeld && !bHeld)
		{
			Text = FString::Printf(TEXT("the shot holds at the contact %.2f %.2f %.2f"), Shot.contact.x, Shot.contact.y, Shot.contact.z);
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
		// A hostile area: no disc, the column of light in the danger only (the decision of the user above).
		const bool bHostile = !bSummon && !bFriendly;
		Shape->SetVisibility(bStarted && !bHostile && (bSummon || (Phase != swc::creatures::AreaPhase::ended && Radius > 0.0f)));
		Column->SetVisibility(bStarted && bHostile && Phase == swc::creatures::AreaPhase::danger && Radius > 0.0f);
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
		return Text;
	}
	return Text;
}
