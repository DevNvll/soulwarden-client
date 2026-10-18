#include "Creatures/SwcEffectActor.h"

#include "Components/StaticMeshComponent.h"
#include "Creatures/SwcCreatureData.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
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

void ASwcEffectActor::SetColor(const FLinearColor& Color)
{
	if (Material == nullptr)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TranslucentMaterial);
		if (Base == nullptr)
		{
			return;
		}
		Material = UMaterialInstanceDynamic::Create(Base, this);
		Shape->SetMaterial(0, Material);
	}
	Material->SetVectorParameterValue(TEXT("Color"), Color);
	Material->SetScalarParameterValue(TEXT("Opacity"), Color.A);
}

void ASwcEffectActor::SetupProjectile(const FSwcProjectileSlice* Slice)
{
	bArea = false;
	if (Slice != nullptr)
	{
		Flight = Slice->Flight;
	}
	Shape->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, CylinderMesh));
	// The shaft along local Z, its tip at the place of the shot.
	Shape->SetRelativeScale3D(FVector(ShaftWidth, ShaftWidth, ShaftLength));
	Shape->SetRelativeLocation(FVector(0.0, 0.0, -ShaftLength * 50.0));
	SetColor(ShotColor);
}

void ASwcEffectActor::SetupArea(const FSwcSpellSlice* Slice, const FSwcSpellRow* Row)
{
	bArea = true;
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
	Shape->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, CylinderMesh));
	Shape->SetRelativeScale3D(FVector(Radius * 2.0, Radius * 2.0, DiscHeight));
	Shape->SetRelativeLocation(FVector(0.0, 0.0, DiscHeight * 50.0));
	SetColor(WarningColor);
}

FString ASwcEffectActor::UpdateFromEntity(const swc::creatures::Entity& Entity, uint64 RenderTimeUs)
{
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
		const swc::net::WorldPosition& Place = Parts.hasSpell ? Parts.place : Parts.position;
		SetActorLocation(SwcEngineToUnreal(Place));
		const swc::creatures::AreaTimes Times = bHasVolume ? swc::creatures::area_times(Parts, Volume, Events)
														  : swc::creatures::AreaTimes{swc::creatures::spell_start_us(Parts, Events), 0, 0};
		const swc::creatures::AreaPhase Phase = swc::creatures::area_phase(Times, RenderTimeUs);
		const bool bStarted = RenderTimeUs >= Times.startUs;
		Shape->SetVisibility(bStarted && Phase != swc::creatures::AreaPhase::ended && Radius > 0.0f);
		const int32 PhaseIndex = bStarted ? static_cast<int32>(Phase) : -1;
		if (PhaseIndex != LastPhase)
		{
			LastPhase = PhaseIndex;
			const double Since = (static_cast<double>(RenderTimeUs) - static_cast<double>(Times.startUs)) / 1.0e6;
			switch (Phase)
			{
			case swc::creatures::AreaPhase::warning:
				SetColor(WarningColor);
				Text = FString::Printf(TEXT("the warning of the area (radius %.2f m) at %.2f s"), Radius, Since);
				break;
			case swc::creatures::AreaPhase::danger:
				SetColor(DangerColor);
				Text = FString::Printf(TEXT("the danger of the area at %.2f s"), Since);
				break;
			case swc::creatures::AreaPhase::ended:
				Text = FString::Printf(TEXT("the end of the area at %.2f s"), Since);
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
