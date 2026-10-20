#include "World/SwcMaterialScale.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcMaterialScale, Log, All);

// The scale of the GeometryBeam effects (M_SwcBeam, AST-207): BeamScale of MPC_SwcEffects (its asset default 0).
static TAutoConsoleVariable<float> CVarBeamScale(TEXT("swc.BeamScale"), 1.0f,
	TEXT("The scale of the GeometryBeam fog sheets and cards (M_SwcBeam); 0 hides them."));

bool USwcMaterialScale::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcMaterialScale::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcMaterialScale, STATGROUP_Tickables);
}

void USwcMaterialScale::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	// The collection of Scripts/swc_materials.py (it exists after the first apply of the materials).
	Collection = LoadObject<UMaterialParameterCollection>(nullptr,
		TEXT("/SwcGameContent/Base/MPC_SwcMaterials.MPC_SwcMaterials"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	if (!Collection)
	{
		UE_LOG(LogSwcMaterialScale, Log, TEXT("SWC material scale: no MPC_SwcMaterials"));
	}
	Effects = LoadObject<UMaterialParameterCollection>(nullptr,
		TEXT("/SwcGameContent/Base/MPC_SwcEffects.MPC_SwcEffects"), nullptr, LOAD_Quiet | LOAD_NoWarn);
}

float USwcMaterialScale::LightScale() const
{
	// The console variable of the local lights (the environment work); by name: this module does not own it.
	static IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("swc.LightScale"));
	if (!Variable)
	{
		Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("swc.LightScale"));
	}
	return Variable ? FMath::Max(0.0f, Variable->GetFloat()) : DefaultLightScale;
}

void USwcMaterialScale::ApplyEffects()
{
	UWorld* World = GetWorld();
	UMaterialParameterCollectionInstance* Instance = World ? World->GetParameterCollectionInstance(Effects) : nullptr;
	if (!Instance)
	{
		return;
	}
	const UDirectionalLightComponent* Main = nullptr;
	const UExponentialHeightFogComponent* Fog = nullptr;
	for (TObjectIterator<UDirectionalLightComponent> It; It; ++It)
	{
		if (It->GetWorld() == World && It->IsVisible() && (Main == nullptr || It->Intensity > Main->Intensity))
		{
			Main = *It;
		}
	}
	for (TObjectIterator<UExponentialHeightFogComponent> It; It; ++It)
	{
		if (It->GetWorld() == World && It->IsVisible())
		{
			Fog = *It;
			break;
		}
	}
	const FLinearColor Sun = Main ? Main->GetLightColor() * (Main->Intensity / UE_PI / SourceUnitNits) : FLinearColor::Black;
	// M_SwcBeam multiplies by EmissiveScale (10,000 cd/m2 x swc.LightScale). The light of the sheets comes from the sun and the
	// ambient of the source (%APPLY_SUN_COLOR, AmbStrength), not from a local light: the source rule is 10,000 cd/m2 for one
	// source unit with no look scale, so BeamScale removes swc.LightScale (research emission-fogcards.md, "one shared conversion").
	const float Look = LightScale();
	Instance->SetScalarParameterValue(TEXT("BeamScale"),
		Look > 0.0f ? FMath::Max(0.0f, CVarBeamScale.GetValueOnGameThread()) / Look : 0.0f);
	Instance->SetVectorParameterValue(TEXT("SunColorSource"), FLinearColor(Sun.R, Sun.G, Sun.B, Main ? 1.0f : 0.0f));
	const FLinearColor Inscatter = Fog ? Fog->FogInscatteringLuminance : FLinearColor::White;
	Instance->SetVectorParameterValue(TEXT("FogColorSource"), FLinearColor(Inscatter.R, Inscatter.G, Inscatter.B, Fog ? 1.0f : 0.0f));
}

void USwcMaterialScale::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Effects)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now >= NextEffects)
		{
			NextEffects = Now + 0.25;
			ApplyEffects();
		}
	}
	if (!Collection)
	{
		return;
	}
	const float Scale = SourceUnitNits * LightScale();
	if (FMath::IsNearlyEqual(Scale, Applied))
	{
		return;
	}
	UMaterialParameterCollectionInstance* Instance = GetWorld()->GetParameterCollectionInstance(Collection);
	if (Instance && Instance->SetScalarParameterValue(TEXT("EmissiveScale"), Scale))
	{
		UE_LOG(LogSwcMaterialScale, Log, TEXT("SWC material scale: EmissiveScale %.0f"), Scale);
		Applied = Scale;
	}
}
