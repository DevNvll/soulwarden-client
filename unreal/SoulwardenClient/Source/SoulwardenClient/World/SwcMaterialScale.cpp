#include "World/SwcMaterialScale.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcMaterialScale, Log, All);

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

void USwcMaterialScale::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
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
