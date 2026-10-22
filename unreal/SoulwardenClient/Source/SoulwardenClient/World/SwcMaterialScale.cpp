#include "World/SwcMaterialScale.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/World.h"
#include "Environment/SwcEnvironmentSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcMaterialScale, Log, All);

// The scale of the GeometryBeam effects (M_SwcBeam, AST-207): BeamScale of MPC_SwcEffects (its asset default 0).
static TAutoConsoleVariable<float> CVarBeamScale(TEXT("swc.BeamScale"), 1.0f,
	TEXT("The scale of the GeometryBeam fog sheets and cards (M_SwcBeam); 0 hides them."));
// The camera fade of the vegetation (M_SwcFoliage version 4; research/ground-cover.md "Camera fade"): the method of
// VegCameraFade of MPC_SwcMaterials (the start 2 m and the end 3 m stay).
// The look scales of the Hair parent (M_SwcHair version 5, MPC_SwcHair): the Scatter (the Kajiya diffuse of the Unreal Hair) and D
// in the base color (the color of the strand lobes): a test of the level of light hair against retail (AST-247).
static TAutoConsoleVariable<float> CVarHairScatter(TEXT("swc.HairScatter"), 1.0f,
	TEXT("A scale of the Scatter of M_SwcHair (the diffuse of the Unreal Hair; 1: the rule of the parent)."));
static TAutoConsoleVariable<float> CVarHairBase(TEXT("swc.HairBase"), 1.0f,
	TEXT("A scale of D in the base color (D^2) of M_SwcHair (the strand lobes and the diffuse; 1: the rule of the parent)."));
static TAutoConsoleVariable<int32> CVarCameraFade(TEXT("swc.Foliage.CameraFade"), 1,
	TEXT("1: the vegetation near the render camera dissolves (2 to 3 m, the source rule); 0: off (a before and after test)."));

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
	// The sun of the sheets: the source sun of the hour on the ground (swc.SunSource 1: research/beam-scale.md, the sun input
	// Csun = SunColor / Y x SunLux / (N pi)); else the brightest visible directional light. The sun light of Unreal holds the light
	// before its atmosphere (its color over the ground transmittance), which is bluer and stronger at a low sun.
	const USwcEnvironmentSubsystem* Environment = World->GetSubsystem<USwcEnvironmentSubsystem>();
	const FLinearColor Source = Environment != nullptr ? Environment->GetSourceSunLight() : FLinearColor::Transparent;
	const bool bSource = Source.A > 0.0f;
	const FLinearColor Sun = bSource ? FLinearColor(Source.R, Source.G, Source.B) * (1.0f / UE_PI / SourceUnitNits)
		: Main ? Main->GetLightColor() * (Main->Intensity / UE_PI / SourceUnitNits) : FLinearColor::Black;
	// M_SwcBeam multiplies by EmissiveScale (10,000 cd/m2 x swc.LightScale). The light of the sheets comes from the sun and the
	// ambient of the source (%APPLY_SUN_COLOR, AmbStrength), not from a local light: the source rule is 10,000 cd/m2 for one
	// source unit with no look scale, so BeamScale removes swc.LightScale (research emission-fogcards.md, "one shared conversion").
	const float Look = LightScale();
	Instance->SetScalarParameterValue(TEXT("BeamScale"),
		Look > 0.0f ? FMath::Max(0.0f, CVarBeamScale.GetValueOnGameThread()) / Look : 0.0f);
	Instance->SetVectorParameterValue(TEXT("SunColorSource"), FLinearColor(Sun.R, Sun.G, Sun.B, bSource || Main ? 1.0f : 0.0f));
	if (World->GetTimeSeconds() >= NextEffectsLog)
	{
		NextEffectsLog = World->GetTimeSeconds() + 10.0;
		UE_LOG(LogSwcMaterialScale, Log, TEXT("SWC material scale: the sun of the effects %.4f %.4f %.4f (%s), BeamScale %.3f"), Sun.R, Sun.G, Sun.B,
			bSource ? TEXT("the source sun of the hour") : Main ? TEXT("the brightest directional light") : TEXT("none"),
			Look > 0.0f ? FMath::Max(0.0f, CVarBeamScale.GetValueOnGameThread()) / Look : 0.0f);
	}
	const FLinearColor Inscatter = Fog ? Fog->FogInscatteringLuminance : FLinearColor::White;
	Instance->SetVectorParameterValue(TEXT("FogColorSource"), FLinearColor(Inscatter.R, Inscatter.G, Inscatter.B, Fog ? 1.0f : 0.0f));
}

void USwcMaterialScale::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!Hair && !bHairTried)
	{
		bHairTried = true;
		Hair = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/SwcGameContent/Base/MPC_SwcHair.MPC_SwcHair"), nullptr,
			LOAD_Quiet | LOAD_NoWarn);
	}
	if (Hair)
	{
		const FVector2f Values(CVarHairScatter.GetValueOnGameThread(), CVarHairBase.GetValueOnGameThread());
		if (Values != AppliedHair)
		{
			UMaterialParameterCollectionInstance* Instance = GetWorld()->GetParameterCollectionInstance(Hair);
			if (Instance && Instance->SetScalarParameterValue(TEXT("HairScatterScale"), Values.X)
				&& Instance->SetScalarParameterValue(TEXT("HairBaseScale"), Values.Y))
			{
				UE_LOG(LogSwcMaterialScale, Log, TEXT("SWC material scale: hair scatter %.3f, base %.3f"), Values.X, Values.Y);
				AppliedHair = Values;
			}
		}
	}
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
	if (const int32 Fade = CVarCameraFade.GetValueOnGameThread() != 0 ? 1 : 0; Fade != AppliedFade)
	{
		UMaterialParameterCollectionInstance* Instance = GetWorld()->GetParameterCollectionInstance(Collection);
		if (Instance && Instance->SetVectorParameterValue(TEXT("VegCameraFade"), FLinearColor(static_cast<float>(Fade), 2.0f, 3.0f, 0.0f)))
		{
			UE_LOG(LogSwcMaterialScale, Log, TEXT("SWC material scale: the camera fade of the vegetation %d"), Fade);
			AppliedFade = Fade;
		}
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
