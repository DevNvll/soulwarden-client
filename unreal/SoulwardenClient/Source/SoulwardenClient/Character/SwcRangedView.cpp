#include "Character/SwcRangedView.h"

#include "Camera/PlayerCameraManager.h"
#include "Character/SwcActions.h"
#include "Combat/SwcAttackGraph.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SwcBackendSubsystem.h"

namespace
{
/** The muzzle of a shot: 1.0 m along the look and 1.46 m above the feet (the live bow item, protocol notes 4.27; RepClient). */
constexpr double kMuzzleForward = 100.0;
constexpr double kMuzzleUp = 146.0;
/** The aim ray: the range of the bow attacks (Bow/Basic maxRange 100 m, projectile-data.json). */
constexpr double kAimRange = 10000.0;
/** The heal number: UIStyle COLOR_NAMEPLATE_HEALING (COLOR_GREEN, 48 229 171; research p36-data.md Q6), 1.6 m above the feet
 *  (Hud.LocalPlayer.DamageNumbers.Player.ZOffsetBase of damagenumber.luac), about 2 s (protocol notes 4.31, live). The rise and the
 *  fade are the values of the damage numbers of the creature track (not from the data). */
const FLinearColor kHealColor(48.0f / 255.0f, 229.0f / 255.0f, 171.0f / 255.0f, 1.0f);
constexpr double kNumberUp = 160.0;
constexpr double kNumberSeconds = 2.0;
constexpr double kNumberRise = 70.0;
constexpr double kNumberFade = 0.4;

/** @return An engine-space vector (m) as an Unreal vector (cm). */
FVector EngineVector(const std::array<float, 3>& Value)
{
	return FVector(Value[0] * 100.0, -Value[1] * 100.0, Value[2] * 100.0);
}
} // namespace

bool USwcRangedView::bUiHealNumbers = false;

USwcRangedView::USwcRangedView()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void USwcRangedView::Setup(const TSharedPtr<FSwcActions>& InData, const std::shared_ptr<swc::combat::Driver>& InDriver)
{
	Data = InData;
	Driver = InDriver;
}

void USwcRangedView::BeginPlay()
{
	Super::BeginPlay();
	DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &USwcRangedView::DrawNumbers));
}

void USwcRangedView::EndPlay(const EEndPlayReason::Type Reason)
{
	UDebugDrawService::Unregister(DrawHandle);
	for (FShot& Shot : Shots)
	{
		if (Shot.Mesh.IsValid())
		{
			Shot.Mesh->DestroyComponent();
		}
	}
	Shots.Reset();
	Super::EndPlay(Reason);
}

bool USwcRangedView::GetAimPoint(FVector& Out) const
{
	Out = AimPoint;
	return bHasAim;
}

UStaticMeshComponent* USwcRangedView::MakeMesh(const TCHAR* MeshPath, const FLinearColor& Color)
{
	AActor* Owner = GetOwner();
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath);
	if (Owner == nullptr || Mesh == nullptr)
	{
		return nullptr;
	}
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Owner);
	Component->SetStaticMesh(Mesh);
	Component->SetUsingAbsoluteLocation(true);
	Component->SetUsingAbsoluteRotation(true);
	Component->SetUsingAbsoluteScale(true);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Component);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Component->SetMaterial(0, Material);
	}
	Component->RegisterComponent();
	return Component;
}

void USwcRangedView::UpdateAim()
{
	const APawn* Owner = Cast<APawn>(GetOwner());
	const APlayerController* Controller = Owner != nullptr ? Cast<APlayerController>(Owner->GetController()) : nullptr;
	if (Controller == nullptr || Controller->PlayerCameraManager == nullptr)
	{
		bHasAim = false;
		return;
	}
	const FVector Start = Controller->PlayerCameraManager->GetCameraLocation();
	const FVector End = Start + Controller->PlayerCameraManager->GetCameraRotation().Vector() * kAimRange;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SwcAim), false, Owner);
	FHitResult Hit;
	AimPoint = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params) ? Hit.ImpactPoint : End;
	bHasAim = true;
}

void USwcRangedView::SpawnShots()
{
	const ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Driver || !Data.IsValid() || Owner == nullptr)
	{
		return;
	}
	const swc::combat::Driver::Snapshot Snapshot = Driver->snapshot();
	for (const swc::combat::Driver::Snapshot::Ranged::Recent& Recent : Snapshot.ranged.recent)
	{
		if (Recent.serial <= SeenRecent)
		{
			continue;
		}
		SeenRecent = Recent.serial;
		if (!Recent.shot)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("ranged: the cast of %s at input %u"), UTF8_TO_TCHAR(Recent.spell.c_str()), static_cast<unsigned>(Recent.input));
			continue;
		}
		const swc::ranged::ShotRule* Rule = Data->GetRangedShared().rule(Recent.attack, Recent.row, Recent.spell);
		const swc::ranged::Flight Flight = Rule != nullptr ? Rule->flight : swc::ranged::Flight();
		const double Half = Owner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Feet = Owner->GetActorLocation() - FVector(0.0, 0.0, Half);
		const APlayerController* Controller = Cast<APlayerController>(Owner->GetController());
		const FRotator Look = Controller != nullptr && Controller->PlayerCameraManager != nullptr ? Controller->PlayerCameraManager->GetCameraRotation()
																								  : Owner->GetActorRotation();
		const FVector Forward = FRotator(0.0, Look.Yaw, 0.0).Vector();
		const FVector Muzzle = Feet + Forward * kMuzzleForward + FVector(0.0, 0.0, kMuzzleUp);
		FVector Direction = Look.Vector();
		if (bHasAim && FVector::Dist(AimPoint, Muzzle) > 50.0)
		{
			Direction = (AimPoint - Muzzle).GetSafeNormal();
		}
		FShot Shot;
		Shot.bArrow = Rule != nullptr && Rule->spell.empty() && Rule->profile == "Bow";
		const bool bFire = Rule != nullptr && Rule->profile == "FireBall";
		Shot.Position = Muzzle;
		Shot.Velocity = Direction * (Flight.known ? Flight.speed * 100.0 : 10000.0);
		Shot.Accel = Flight.known ? EngineVector(Flight.accel) : FVector::ZeroVector;
		Shot.AccelDelay = Flight.accelDelay * 100.0f;
		Shot.MaxRange = (Flight.known ? Flight.maxRange : 50.0f) * 100.0f;
		Shot.Life = Flight.known ? Flight.life : 0.5f;
		Shot.Radius = FMath::Max(Flight.radius, 0.05f) * 100.0f;
		Shot.Stuck = -1.0f;
		const FLinearColor Color = Shot.bArrow ? FLinearColor(0.35f, 0.22f, 0.1f) : bFire ? FLinearColor(1.0f, 0.45f, 0.05f) : FLinearColor(0.6f, 1.0f, 0.4f);
		UStaticMeshComponent* Mesh = MakeMesh(Shot.bArrow ? TEXT("/Engine/BasicShapes/Cylinder.Cylinder") : TEXT("/Engine/BasicShapes/Sphere.Sphere"), Color);
		if (Mesh != nullptr)
		{
			// The basic shapes are 1 m: an arrow 0.8 m long and 3 cm thick along its flight; a magic shot the diameter of its radius.
			Mesh->SetWorldScale3D(Shot.bArrow ? FVector(0.03, 0.03, 0.8) : FVector(Shot.Radius * 2.0 / 100.0));
			Mesh->SetWorldLocationAndRotation(Shot.Position, FRotationMatrix::MakeFromZ(Direction).ToQuat());
		}
		Shot.Mesh = Mesh;
		Shots.Add(Shot);
		++Projectiles;
		UE_LOG(LogSwcBackend, Log, TEXT("ranged: the own projectile of the shot %08x row %08x %s at input %u: %s %.0f m/s, range %.0f m, life %.2f s, to the aim point %.1f m away"),
			Recent.attack, Recent.row, UTF8_TO_TCHAR(Recent.spell.c_str()), static_cast<unsigned>(Recent.input),
			Rule != nullptr ? UTF8_TO_TCHAR((Rule->profile + "/" + Rule->attack).c_str()) : TEXT("(no rule)"), Shot.Velocity.Size() / 100.0,
			Shot.MaxRange / 100.0, Shot.Life, bHasAim ? FVector::Dist(AimPoint, Muzzle) / 100.0 : -1.0);
	}
}

void USwcRangedView::UpdateShots(float DeltaTime)
{
	const AActor* Owner = GetOwner();
	for (int32 Index = Shots.Num() - 1; Index >= 0; --Index)
	{
		FShot& Shot = Shots[Index];
		bool bEnd = false;
		if (Shot.Stuck >= 0.0f)
		{
			Shot.Stuck -= DeltaTime;
			bEnd = Shot.Stuck < 0.0f;
		}
		else
		{
			// The step of the flight (protocol notes 4.27, client code 0x695CCC0): p += (v + a dt / 2) dt, v += a dt; the acceleration
			// after the acceleration delay of the attack.
			const FVector Accel = Shot.Traveled >= Shot.AccelDelay ? Shot.Accel : FVector::ZeroVector;
			const FVector Next = Shot.Position + (Shot.Velocity + Accel * (DeltaTime * 0.5)) * DeltaTime;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(SwcShot), false, Owner);
			FHitResult Hit;
			if (GetWorld()->SweepSingleByChannel(Hit, Shot.Position, Next, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(Shot.Radius), Params))
			{
				Shot.Traveled += FVector::Dist(Shot.Position, Hit.Location);
				Shot.Position = Hit.Location;
				++ProjectileHits;
				const AActor* Other = Hit.GetActor();
				UE_LOG(LogSwcBackend, Log, TEXT("ranged: the own projectile hit %s after %.1f m"), Other != nullptr ? *Other->GetName() : TEXT("the world"),
					Shot.Traveled / 100.0);
				// An arrow stays at its hit (the stuck life of the slice basic_arrow: 8 s); a magic shot ends.
				Shot.Stuck = Shot.bArrow ? 8.0f : 0.0f;
				bEnd = !Shot.bArrow;
			}
			else
			{
				Shot.Traveled += FVector::Dist(Shot.Position, Next);
				Shot.Position = Next;
				Shot.Velocity += Accel * DeltaTime;
				Shot.Age += DeltaTime;
				bEnd = Shot.Traveled >= Shot.MaxRange || Shot.Age >= Shot.Life;
			}
		}
		if (Shot.Mesh.IsValid() && !bEnd)
		{
			Shot.Mesh->SetWorldLocation(Shot.Position);
			if (Shot.Stuck < 0.0f && !Shot.Velocity.IsNearlyZero())
			{
				Shot.Mesh->SetWorldRotation(FRotationMatrix::MakeFromZ(Shot.Velocity.GetSafeNormal()).ToQuat());
			}
		}
		if (bEnd)
		{
			if (Shot.Mesh.IsValid())
			{
				Shot.Mesh->DestroyComponent();
			}
			Shots.RemoveAt(Index);
		}
	}
}

void USwcRangedView::UpdateTarget()
{
	const ACharacter* Owner = Cast<ACharacter>(GetOwner());
	const swc::combat::Driver::Snapshot Snapshot = Driver ? Driver->snapshot() : swc::combat::Driver::Snapshot();
	const swc::ranged::Spell* Spell = Data.IsValid() && !Snapshot.ranged.targeting.empty() ? Data->GetRangedShared().spell(Snapshot.ranged.targeting) : nullptr;
	const APlayerController* Controller = Owner != nullptr ? Cast<APlayerController>(Owner->GetController()) : nullptr;
	if (Spell == nullptr || Controller == nullptr || Controller->PlayerCameraManager == nullptr)
	{
		bGroundValid = false;
		IndicatorSpell.Reset();
		if (Indicator != nullptr)
		{
			Indicator->SetVisibility(false);
		}
		return;
	}
	const FString SpellName = UTF8_TO_TCHAR(Spell->id.c_str());
	if (SpellName != IndicatorSpell)
	{
		IndicatorSpell = SpellName;
		++Targetings;
		UE_LOG(LogSwcBackend, Log, TEXT("ranged: the ground target of %s (cast distance %.0f m, radius %.1f m)"), *SpellName, Spell->castDistance, Spell->radius);
	}
	// The ground under the camera ray in the cast distance from the character (CastDistance of the spell row); beyond it, the ground
	// below the point at the cast distance on the ray (an inference: the native rule CastSpellRaycast is not traced).
	const double Half = Owner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = Owner->GetActorLocation() - FVector(0.0, 0.0, Half);
	const FVector Start = Controller->PlayerCameraManager->GetCameraLocation();
	const FVector Ray = Controller->PlayerCameraManager->GetCameraRotation().Vector();
	const double Reach = Spell->castDistance * 100.0;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SwcGroundTarget), false, Owner);
	FHitResult Hit;
	FVector Point;
	bool bFound = GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Ray * (Reach + FVector::Dist(Start, Feet)), ECC_Visibility, Params);
	if (bFound && FVector::Dist2D(Hit.ImpactPoint, Feet) <= Reach)
	{
		Point = Hit.ImpactPoint;
	}
	else
	{
		const FVector Flat = FVector(Ray.X, Ray.Y, 0.0).GetSafeNormal();
		const FVector Above = Feet + Flat * Reach + FVector(0.0, 0.0, Spell->castHeight * 100.0);
		bFound = GetWorld()->LineTraceSingleByChannel(Hit, Above, Above - FVector(0.0, 0.0, Spell->castHeight * 200.0), ECC_Visibility, Params);
		Point = Hit.ImpactPoint;
	}
	bGroundValid = bFound;
	if (Indicator == nullptr)
	{
		Indicator = MakeMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), kHealColor);
	}
	if (Indicator != nullptr)
	{
		// A disc of the radius of the spell, 2 cm thick.
		Indicator->SetVisibility(bFound);
		Indicator->SetWorldScale3D(FVector(Spell->radius * 2.0, Spell->radius * 2.0, 0.02));
		Indicator->SetWorldLocationAndRotation(Point + FVector(0.0, 0.0, 2.0), FQuat::Identity);
	}
}

void USwcRangedView::SetHeals(uint32 Revision, const std::vector<swc::net::RepClient::OwnEntity::Heal>& Heals)
{
	if (Revision == SeenHeals)
	{
		return;
	}
	const uint32 Count = FMath::Min<uint32>(Revision - SeenHeals, static_cast<uint32>(Heals.size()));
	SeenHeals = Revision;
	const ACharacter* Owner = Cast<ACharacter>(GetOwner());
	for (std::size_t Index = Heals.size() - Count; Index < Heals.size(); ++Index)
	{
		const swc::net::RepClient::OwnEntity::Heal& Heal = Heals[Index];
		++HealNumbers;
		if (Heal.healed != 0)
		{
			// A heal of a different character (4298): the client shows no other player yet; the number is in the log.
			UE_LOG(LogSwcBackend, Log, TEXT("heal: +%d to a friend (4298, Stats id %016llx)"), FMath::FloorToInt(Heal.amount),
				static_cast<unsigned long long>(Heal.healed));
			continue;
		}
		UE_LOG(LogSwcBackend, Log, TEXT("heal: +%d (the own character, bit 1 of healthChangeFlags)"), FMath::FloorToInt(Heal.amount));
		if (Owner == nullptr)
		{
			continue;
		}
		const double Half = Owner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FNumber& Number = Numbers.AddDefaulted_GetRef();
		Number.Point = Owner->GetActorLocation() - FVector(0.0, 0.0, Half) + FVector(0.0, 0.0, kNumberUp);
		Number.Text = FString::Printf(TEXT("+%d"), FMath::FloorToInt(Heal.amount));
		Number.Start = FPlatformTime::Seconds();
	}
}

void USwcRangedView::DrawNumbers(UCanvas* Canvas, APlayerController* Controller)
{
	const double Now = FPlatformTime::Seconds();
	Numbers.RemoveAll([Now](const FNumber& Number) { return Now - Number.Start > kNumberSeconds; });
	UFont* Font = GEngine != nullptr ? GEngine->GetLargeFont() : nullptr;
	if (Font == nullptr || Canvas == nullptr || bUiHealNumbers)
	{
		return;
	}
	for (const FNumber& Number : Numbers)
	{
		const double Age = Now - Number.Start;
		const FVector Screen = Canvas->Project(Number.Point + FVector(0.0, 0.0, kNumberRise * Age / kNumberSeconds));
		if (Screen.Z <= 0.0)
		{
			continue;
		}
		const float Alpha = static_cast<float>(FMath::Clamp((kNumberSeconds - Age) / kNumberFade, 0.0, 1.0));
		float Width = 0.0f;
		float Height = 0.0f;
		Canvas->StrLen(Font, Number.Text, Width, Height);
		FLinearColor Color = kHealColor;
		Color.A = Alpha;
		FCanvasTextItem Text(FVector2D(Screen.X - Width * 0.5f, Screen.Y - Height * 0.5f), FText::FromString(Number.Text), Font, Color);
		Text.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, Alpha));
		Text.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Text);
	}
}

void USwcRangedView::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateAim();
	SpawnShots();
	UpdateShots(DeltaTime);
	UpdateTarget();
}
