#include "Water/SwcWaterSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"

DEFINE_LOG_CATEGORY(LogSwcWater);

namespace
{
/** The source water material of tools/import-water.sh (Scripts/water_materials.py; AST-200, docs/WATER.md). */
const TCHAR* ParentPath = TEXT("/SwcGameContent/Water/M_SwcWaterSource.M_SwcWaterSource");
/** A surface builds when the view is this near to it (cm), at most BuildsPerTick in one tick (decisions of the client). */
constexpr double BuildRange = 300000.0;
constexpr int32 BuildsPerTick = 6;
/** The draw distance of a surface beyond its radius (cm), as a cell of the world loads in 1 km (docs/WORLD.md). */
constexpr float DrawDistance = 500000.0f;
/** The ocean grid: 64 x 64 cells of 312.5 m (20 km) that follow the view in steps of one cell (no move of its world UV). */
constexpr int32 OceanCells = 64;
constexpr double OceanCell = 31250.0;
/** The texture coordinates of a water volume: the mesh UV of the converter (the world m x SurfaceUScale, SurfaceVScale), as the
 *  source mesh UV q of watervolume.cfx (B.xy = q x Tilling). The ocean uses the world m x 0.005 of water.cfx (in the material). */
constexpr float VolumeUVScale = 1.0f;
/** The slope of the static wave normal texture in the source normal sum (a calibration of the client: the source normal field is
 *  generated at run time and is flatter than oceanwaves_ddn; -SwcWater=NormalStrength=X). */
constexpr float NormalStrength = 0.05f;
/** The radiance of one source unit (cd/m2; docs/ENVIRONMENT.md): the gain of the environment cube of the ocean material. */
constexpr float SourceUnit = 10000.0f;

/** Parameter values for each water instance at run time (the calibration and the debug views of the matched views: match-view
 *  --variant VIEW@NAME='swc.WaterParams Debug=3'); the same form as -SwcWater, added to its values. */
TAutoConsoleVariable<FString> CVarWaterParams(TEXT("swc.WaterParams"), TEXT(""),
	TEXT("Water material values for each instance, Name=Value,... (docs/WATER.md; Debug=1..4 the debug views)."));

void ParseValues(const FString& Values, TMap<FName, float>& Out)
{
	TArray<FString> Pairs;
	Values.ParseIntoArray(Pairs, TEXT(","));
	for (const FString& Pair : Pairs)
	{
		FString Name;
		FString Number;
		if (Pair.TrimStartAndEnd().Split(TEXT("="), &Name, &Number))
		{
			Out.Add(FName(*Name), FCString::Atof(*Number));
		}
	}
}

FString ContentFolder()
{
	const FString Content = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_CONTENT"));
	if (!Content.IsEmpty())
	{
		return Content;
	}
	const FString Work = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_WORK"));
	return FPaths::Combine(Work.IsEmpty() ? FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT("Games/soulwarden-client")) : Work,
		TEXT("content"));
}

TSharedPtr<FJsonObject> ReadJson(const FString& Path)
{
	FString Text;
	TSharedPtr<FJsonObject> Out;
	if (FFileHelper::LoadFileToString(Text, *Path))
	{
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out);
	}
	return Out;
}

float NumberOf(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, float Default)
{
	double Value = Default;
	return Object.IsValid() && Object->TryGetNumberField(Key, Value) ? static_cast<float>(Value) : Default;
}

FVector3f ColorOf(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, const FVector3f& Default)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Object.IsValid() || !Object->TryGetArrayField(Key, Values) || Values->Num() < 3)
	{
		return Default;
	}
	return FVector3f((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
}

/** @return An engine point (m) in Unreal space (cm). */
FVector ToUnreal(double X, double Y, double Z)
{
	return FVector(X * 100.0, -Y * 100.0, Z * 100.0);
}

/** @return The inverse of a 3 x 3 matrix (rows), or the identity for a singular one. */
void Invert3(const double M[9], double Out[9])
{
	const double A = M[4] * M[8] - M[5] * M[7];
	const double B = M[5] * M[6] - M[3] * M[8];
	const double C = M[3] * M[7] - M[4] * M[6];
	const double Det = M[0] * A + M[1] * B + M[2] * C;
	if (FMath::Abs(Det) < 1e-12)
	{
		const double Identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
		FMemory::Memcpy(Out, Identity, sizeof(Identity));
		return;
	}
	const double I = 1.0 / Det;
	Out[0] = A * I;
	Out[1] = (M[2] * M[7] - M[1] * M[8]) * I;
	Out[2] = (M[1] * M[5] - M[2] * M[4]) * I;
	Out[3] = B * I;
	Out[4] = (M[0] * M[8] - M[2] * M[6]) * I;
	Out[5] = (M[2] * M[3] - M[0] * M[5]) * I;
	Out[6] = C * I;
	Out[7] = (M[1] * M[6] - M[0] * M[7]) * I;
	Out[8] = (M[0] * M[4] - M[1] * M[3]) * I;
}
} // namespace

void USwcWaterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

bool USwcWaterSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcWaterSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcWaterSubsystem, STATGROUP_Tickables);
}

void USwcWaterSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bLoadTried)
	{
		bLoadTried = true;
		Load();
	}
	FVector View;
	if (!bLoaded || !GetViewLocation(View))
	{
		return;
	}
	const FString Params = CVarWaterParams.GetValueOnGameThread();
	if (Params != ConsoleParams)
	{
		ConsoleParams = Params;
		// A debug view ends when the variable no longer names it (match-view sets the variable back after a variant).
		CalibrationValues.Add(TEXT("Debug"), 0.0f);
		ParseValues(Params, CalibrationValues);
		UE_LOG(LogSwcWater, Log, TEXT("water: swc.WaterParams %s"), *Params);
		for (UMaterialInstanceDynamic* Instance : SurfaceMaterials)
		{
			if (Instance != nullptr)
			{
				ApplyOverrides(Instance);
			}
		}
	}
	UpdateSkyCube();
	UpdateBodies(View);
	UpdateOcean(View, DeltaTime);
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextWorldOceanCheck && WorldOceanChecks < 30)
	{
		NextWorldOceanCheck = Now + 2.0;
		++WorldOceanChecks;
		HideWorldOcean();
	}
}

void USwcWaterSubsystem::Load()
{
	const FString Folder = FPaths::Combine(ContentFolder(), TEXT("water"));
	const TSharedPtr<FJsonObject> Index = ReadJson(FPaths::Combine(Folder, TEXT("water.json")));
	if (!Index.IsValid())
	{
		UE_LOG(LogSwcWater, Warning, TEXT("water: no %s/water.json (tools/convert-water.sh write): no water"), *Folder);
		return;
	}
	for (const TSharedPtr<FJsonValue>& Value : Index->GetArrayField(TEXT("materials")))
	{
		const TSharedPtr<FJsonObject> Info = Value->AsObject();
		FMaterialRecord Record;
		Record.Path = Info->GetStringField(TEXT("path"));
		Record.Shader = Info->GetStringField(TEXT("shader"));
		// The root values of the .mtl: Specular (the reflectance of the sun lobes and of the material cube) and Shininess (the gloss
		// g = Shininess / 255).
		FString Text;
		TArray<FString> Parts;
		if (Info->TryGetStringField(TEXT("specular"), Text) && Text.ParseIntoArray(Parts, TEXT(",")) >= 3)
		{
			Record.Params.Add(TEXT("Specular"), FCString::Atof(*Parts[1]));
		}
		if (Info->TryGetStringField(TEXT("shininess"), Text))
		{
			Record.Params.Add(TEXT("Gloss"), FMath::Clamp(FCString::Atof(*Text) / 255.0f, 0.0f, 1.0f));
		}
		const TSharedPtr<FJsonObject>* Params = nullptr;
		if (Info->TryGetObjectField(TEXT("params"), Params))
		{
			for (const auto& Pair : (*Params)->Values)
			{
				double Number = 0.0;
				if (Pair.Value->TryGetNumber(Number))
				{
					Record.Params.Add(FName(*FString(Pair.Key)), static_cast<float>(Number));
				}
			}
		}
		Materials.Add(MoveTemp(Record));
	}
	// The ocean: the level of terrain.json, the material, the animation, and the water fog of the time of day of the mission.
	const TSharedPtr<FJsonObject> OceanInfo = Index->GetObjectField(TEXT("ocean"));
	OceanLevel = OceanInfo->GetNumberField(TEXT("level"));
	const TSharedPtr<FJsonObject> BaseFog = OceanInfo->GetObjectField(TEXT("fog"));
	OceanBase = OceanValues(static_cast<int32>(OceanInfo->GetNumberField(TEXT("material"))), OceanInfo->GetObjectField(TEXT("animation")),
		ColorOf(BaseFog, TEXT("Ocean fog color"), FVector3f(0.005f, 0.01f, 0.012f)), NumberOf(BaseFog, TEXT("Ocean fog color multiplier"), 1.0f),
		NumberOf(BaseFog, TEXT("Ocean fog density"), 0.6f), FVector3f::ZeroVector);
	for (const TSharedPtr<FJsonValue>& Value : OceanInfo->GetArrayField(TEXT("overrides")))
	{
		const TSharedPtr<FJsonObject> Info = Value->AsObject();
		FOceanOverride Override;
		Override.Entity = Info->GetStringField(TEXT("entity"));
		Override.Shape = Info->GetStringField(TEXT("shape"));
		const TArray<TSharedPtr<FJsonValue>>& Rows = Info->GetArrayField(TEXT("matrix"));
		double Rotation[9];
		for (int32 Row = 0; Row < 3; ++Row)
		{
			const TArray<TSharedPtr<FJsonValue>>& Cells = Rows[Row]->AsArray();
			for (int32 Column = 0; Column < 3; ++Column)
			{
				Rotation[Row * 3 + Column] = Cells[Column]->AsNumber();
			}
			Override.Translation[Row] = Cells[3]->AsNumber();
		}
		Invert3(Rotation, Override.Inverse);
		Override.Radius = NumberOf(Info, TEXT("radius"), 0.0f);
		Override.Height = NumberOf(Info, TEXT("height"), 0.0f);
		const TArray<TSharedPtr<FJsonValue>>* Dimensions = nullptr;
		if (Info->TryGetArrayField(TEXT("dimensions"), Dimensions) && Dimensions->Num() == 3)
		{
			Override.Dimensions = FVector3d((*Dimensions)[0]->AsNumber(), (*Dimensions)[1]->AsNumber(), (*Dimensions)[2]->AsNumber());
		}
		Override.Transition = NumberOf(Info, TEXT("transition"), 0.0f);
		double MaterialIndex = OceanInfo->GetNumberField(TEXT("material"));
		Info->TryGetNumberField(TEXT("materialIndex"), MaterialIndex);   // an empty material keeps the base material
		Override.Material = static_cast<int32>(MaterialIndex);
		const TSharedPtr<FJsonObject> Fog = Info->GetObjectField(TEXT("fog"));
		// The override animation has the prefix "animation" (animationWavesSpeed); the base names have none (WavesSpeed).
		const TSharedPtr<FJsonObject> Animation = MakeShared<FJsonObject>();
		for (const auto& Pair : Info->GetObjectField(TEXT("animation"))->Values)
		{
			const FString Key(Pair.Key);
			Animation->SetField(Key.StartsWith(TEXT("animation")) ? Key.Mid(9) : Key, Pair.Value);
		}
		Override.Values = OceanValues(Override.Material, Animation, ColorOf(Fog, TEXT("FogColor"), FVector3f(0.005f, 0.01f, 0.012f)),
			NumberOf(Fog, TEXT("fogColorMultiplier"), 1.0f), NumberOf(Fog, TEXT("fogDensity"), 0.6f), ColorOf(Fog, TEXT("nearFogColor"), FVector3f::ZeroVector));
		Overrides.Add(MoveTemp(Override));
	}
	// The surfaces and the quadtrees of the regions.
	int32 Volumes = 0;
	int32 Rivers = 0;
	int32 Faults = 0;
	for (const auto& Pair : Index->GetObjectField(TEXT("regions"))->Values)
	{
		const FString Name(Pair.Key);
		const auto Region = swc::water::region_of(TCHAR_TO_UTF8(*Name));
		const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
		if (!Region)
		{
			continue;
		}
		FString File;
		TArray<uint8> Bytes;
		std::string Error;
		if (Info->TryGetStringField(TEXT("file"), File) && FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(Folder, File)))
		{
			std::vector<swc::water::Body> Read;
			if (!swc::water::read_bodies(std::span<const uint8>(Bytes.GetData(), Bytes.Num()), Read, Error))
			{
				UE_LOG(LogSwcWater, Warning, TEXT("water: %s: %s"), *File, UTF8_TO_TCHAR(Error.c_str()));
				++Faults;
			}
			for (swc::water::Body& Body : Read)
			{
				FBodyEntry Entry;
				const FVector Low = ToUnreal(Body.low[0], Body.high[1], Body.low[2]);
				const FVector High = ToUnreal(Body.high[0], Body.low[1], Body.high[2]);
				Entry.Center = (Low + High) * 0.5;
				Entry.Radius = (High - Low).Size() * 0.5;
				(Body.kind == swc::water::Kind::river ? Rivers : Volumes) += 1;
				Entry.Body = MoveTemp(Body);
				Bodies.Add(MoveTemp(Entry));
			}
		}
		if (Info->TryGetStringField(TEXT("treeFile"), File) && FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(Folder, File)))
		{
			const TSharedPtr<swc::water::Tree> Tree = MakeShared<swc::water::Tree>();
			if (Tree->read(std::span<const uint8>(Bytes.GetData(), Bytes.Num()), Error))
			{
				Trees.Add(FIntPoint(Region->first, Region->second), Tree);
			}
			else
			{
				UE_LOG(LogSwcWater, Warning, TEXT("water: %s: %s"), *File, UTF8_TO_TCHAR(Error.c_str()));
				++Faults;
			}
		}
	}
	FString Values;
	if (FParse::Value(FCommandLine::Get(), TEXT("-SwcWater="), Values, false))
	{
		ParseValues(Values, CalibrationValues);
		UE_LOG(LogSwcWater, Log, TEXT("water: %d parameter value(s) of -SwcWater for each instance: %s"), CalibrationValues.Num(), *Values);
	}
	Parent = LoadObject<UMaterialInterface>(nullptr, ParentPath);
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient;
	WaterActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
	USceneComponent* Root = NewObject<USceneComponent>(WaterActor, TEXT("Root"));
	WaterActor->SetRootComponent(Root);
	Root->RegisterComponent();
	bLoaded = true;
	UE_LOG(LogSwcWater, Log, TEXT("water: %d volumes, %d rivers, %d quadtrees, %d materials, %d ocean overrides, ocean at %.1f m, %s%s"), Volumes, Rivers,
		Trees.Num(), Materials.Num(), Overrides.Num(), OceanLevel, Parent ? TEXT("material M_SwcWaterSource") : TEXT("no material M_SwcWaterSource (tools/import-water.sh): no surface"),
		Faults ? *FString::Printf(TEXT(", %d file fault(s)"), Faults) : TEXT(""));
}

bool USwcWaterSubsystem::GetViewLocation(FVector& Out) const
{
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	if (Controller == nullptr)
	{
		return false;
	}
	if (Controller->PlayerCameraManager != nullptr)
	{
		Out = Controller->PlayerCameraManager->GetCameraLocation();
		return true;
	}
	if (const APawn* Pawn = Controller->GetPawn())
	{
		Out = Pawn->GetActorLocation();
		return true;
	}
	return false;
}

TMap<FName, float> USwcWaterSubsystem::OceanValues(int32 Material, const TSharedPtr<FJsonObject>& Animation, const FVector3f& FogColor, float FogScale,
	float FogDensity, const FVector3f& NearFogColor) const
{
	const TMap<FName, float> None;
	const TMap<FName, float>& Params = Materials.IsValidIndex(Material) ? Materials[Material].Params : None;
	const auto Param = [&Params](const TCHAR* Name, float Default) { const float* Found = Params.Find(FName(Name)); return Found ? *Found : Default; };
	TMap<FName, float> Out;
	// The ocean rule of water.cfx (Kind 1; research water-shader.md) with the values of the source material (ocean_flatwater01,
	// ocean_roughwater01: .mtl PublicParams, Specular, Shininess). The waves move with the animation of the mission or of the
	// override (WavesSpeed, WindSpeed, WindDirection in radians): WavesSpeed x (1 + WindSpeed / 50) [inference: a mapping of the
	// client; the CPU map of OceanParams is not known].
	Out.Add(TEXT("Kind"), 1.0f);
	Out.Add(TEXT("UseMeshUV"), 0.0f);
	Out.Add(TEXT("Tilling"), Param(TEXT("Tilling"), 32.0f));
	Out.Add(TEXT("DetailTilling"), Param(TEXT("DetailTilling"), 3.0f));
	Out.Add(TEXT("NormalsScale"), Param(TEXT("NormalsScale"), 1.5f));
	Out.Add(TEXT("DetailNormalsScale"), Param(TEXT("DetailNormalsScale"), 0.5f));
	Out.Add(TEXT("NormalStrength"), NormalStrength);
	Out.Add(TEXT("WaveSpeed"), FMath::Max(0.05f, NumberOf(Animation, TEXT("WavesSpeed"), 1.0f)) * (1.0f + NumberOf(Animation, TEXT("WindSpeed"), 1.0f) / 50.0f));
	Out.Add(TEXT("WindDirection"), NumberOf(Animation, TEXT("WindDirection"), 0.0f));
	Out.Add(TEXT("SoftIntersectionFactor"), Param(TEXT("SoftIntersectionFactor"), 1.0f));
	Out.Add(TEXT("RefractionBumpScale"), Param(TEXT("RefractionBumpScale"), 0.1f));
	Out.Add(TEXT("ReflectionBumpScale"), Param(TEXT("ReflectionBumpScale"), 0.1f));
	Out.Add(TEXT("FresnelGloss"), Param(TEXT("FresnelGloss"), 0.98f));
	Out.Add(TEXT("ReflectionScale"), Param(TEXT("ReflectionScale"), 0.2f));
	Out.Add(TEXT("Specular"), Param(TEXT("Specular"), 0.42f));
	Out.Add(TEXT("Gloss"), Param(TEXT("Gloss"), 1.0f));
	// The sky fallback of the reflection trace (SkyCubemapParams.x) and the simple surface fog after the ocean fog pass: the research does
	// not prove these branches for the ocean [U]; the retail swim views show the sky in the near water and only the head of the swimmer
	// above a blue surface (AST-200 check p1h: both on). The environment cube of the material x ReflectionScale stays under the trace.
	Out.Add(TEXT("SkyFallback"), 1.0f);
	Out.Add(TEXT("SurfaceFog"), 1.0f);
	Out.Add(TEXT("CubeGain"), SourceUnit);
	Out.Add(TEXT("FoamAmount"), Param(TEXT("FoamAmount"), 0.5f));
	Out.Add(TEXT("FoamTilling"), Param(TEXT("FoamTilling"), 1.0f));
	Out.Add(TEXT("FoamSoftIntersectionFactor"), Param(TEXT("FoamSoftIntersectionFactor"), 1.0f));
	Out.Add(TEXT("FoamCrestAmount"), Param(TEXT("FoamCrestAmount"), 0.0f));
	Out.Add(TEXT("FoamTransitionSpeed"), Param(TEXT("FoamTransitionSpeed"), 5.0f));
	// The water fog of the ocean pass (WaterFogVolume/OceanInto): the deep color FogColor x fogColorMultiplier and the shallow color
	// nearFogColor, both x the sun color; the density per m.
	Out.Add(TEXT("FogColorR"), FogColor.X);
	Out.Add(TEXT("FogColorG"), FogColor.Y);
	Out.Add(TEXT("FogColorB"), FogColor.Z);
	Out.Add(TEXT("NearFogColorR"), NearFogColor.X);
	Out.Add(TEXT("NearFogColorG"), NearFogColor.Y);
	Out.Add(TEXT("NearFogColorB"), NearFogColor.Z);
	Out.Add(TEXT("FogScale"), FogScale);
	Out.Add(TEXT("Density"), FogDensity);
	return Out;
}

void USwcWaterSubsystem::ApplyOverrides(UMaterialInstanceDynamic* Material) const
{
	for (const TPair<FName, float>& Pair : CalibrationValues)
	{
		Material->SetScalarParameterValue(Pair.Key, Pair.Value);
	}
}

UMaterialInstanceDynamic* USwcWaterSubsystem::MakeMaterial(const swc::water::Body& Body)
{
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, WaterActor);
	const TMap<FName, float> None;
	const TMap<FName, float>& Params = Materials.IsValidIndex(Body.material) ? Materials[Body.material].Params : None;
	const auto Param = [&Params](const TCHAR* Name, float Default) { const float* Found = Params.Find(FName(Name)); return Found ? *Found : Default; };
	// The volume rule of watervolume.cfx (Kind 0; research water-shader.md) with the values of the source material (.mtl
	// PublicParams of the shader WaterVolume, Specular, Shininess) and of the body (the water fog of its component: FogColor,
	// fogDensity / WaterFogDensity, fogColorMultiplier / FogColorScale; FogColorAffectedBySun is true in 334 of the 345 river components
	// and is not in the body file: true).
	const bool bRiver = Body.kind == swc::water::Kind::river;
	Material->SetScalarParameterValue(TEXT("Kind"), 0.0f);
	Material->SetScalarParameterValue(TEXT("UseMeshUV"), bRiver ? 1.0f : 0.0f);
	Material->SetScalarParameterValue(TEXT("UVScale"), VolumeUVScale);
	Material->SetScalarParameterValue(TEXT("Tilling"), Param(TEXT("Tilling"), 1.0f));
	Material->SetScalarParameterValue(TEXT("DetailTilling"), Param(TEXT("DetailTilling"), 2.0f));
	Material->SetScalarParameterValue(TEXT("NormalsScale"), Param(TEXT("NormalsScale"), 0.5f));
	Material->SetScalarParameterValue(TEXT("DetailNormalsScale"), Param(TEXT("DetailNormalsScale"), 0.5f));
	Material->SetScalarParameterValue(TEXT("NormalStrength"), NormalStrength);
	Material->SetScalarParameterValue(TEXT("FlowSpeed"), bRiver ? Param(TEXT("WaterFlowSpeed"), 0.0f) : 0.0f);
	Material->SetScalarParameterValue(TEXT("WaveSpeed"), bRiver ? 0.0f : 0.3f);
	Material->SetScalarParameterValue(TEXT("SoftIntersectionFactor"), Param(TEXT("SoftIntersectionFactor"), 1.0f));
	// The literal constants of watervolume.cfx: the refraction offset 0.1, the reflection normal 0.3 (in the material).
	Material->SetScalarParameterValue(TEXT("RefractionBumpScale"), 0.1f);
	Material->SetScalarParameterValue(TEXT("Specular"), Param(TEXT("Specular"), 0.05f));
	Material->SetScalarParameterValue(TEXT("Gloss"), Param(TEXT("Gloss"), 1.0f));
	Material->SetScalarParameterValue(TEXT("RealtimeReflMul"), Param(TEXT("RealtimeReflMul"), 1.0f));
	Material->SetScalarParameterValue(TEXT("EnvCubeReflMul"), Param(TEXT("EnvCubeReflMul"), 1.0f));
	Material->SetScalarParameterValue(TEXT("SkyFallback"), 1.0f);
	Material->SetScalarParameterValue(TEXT("SunAffected"), 1.0f);
	Material->SetScalarParameterValue(TEXT("FoamAmount"), Param(TEXT("FoamAmount"), 0.0f));
	Material->SetScalarParameterValue(TEXT("FoamTilling"), Param(TEXT("FoamTilling"), 1.0f));
	Material->SetScalarParameterValue(TEXT("FoamSoftIntersectionFactor"), Param(TEXT("FoamSoftIntersectionFactor"), 1.0f));
	Material->SetVectorParameterValue(TEXT("FogColor"), FLinearColor(Body.fog[0], Body.fog[1], Body.fog[2], 1.0f));
	Material->SetScalarParameterValue(TEXT("FogScale"), Body.scale);
	Material->SetScalarParameterValue(TEXT("Density"), Body.density);
	if (SkyCube != nullptr)
	{
		Material->SetTextureParameterValue(TEXT("SkyCube"), SkyCube);
	}
	ApplyOverrides(Material);
	SurfaceMaterials.Add(Material);
	return Material;
}

void USwcWaterSubsystem::UpdateSkyCube()
{
	// The sky fallback of the reflection: the cube of the sky light of the environment (the world probe of the source, docs/ENVIRONMENT.md);
	// the material multiplies it by the color and intensity of the sky light (View.SkyLightColor).
	UTextureCube* Found = nullptr;
	for (TObjectIterator<USkyLightComponent> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld() && It->IsVisible() && It->Cubemap != nullptr)
		{
			Found = It->Cubemap;
			break;
		}
	}
	if (Found == nullptr || Found == SkyCube)
	{
		return;
	}
	SkyCube = Found;
	UE_LOG(LogSwcWater, Log, TEXT("water: the sky cube of the reflection is %s"), *Found->GetName());
	SurfaceMaterials.RemoveAll([](const TObjectPtr<UMaterialInstanceDynamic>& Instance) { return Instance == nullptr; });
	for (UMaterialInstanceDynamic* Instance : SurfaceMaterials)
	{
		Instance->SetTextureParameterValue(TEXT("SkyCube"), SkyCube);
	}
	if (OceanMaterial != nullptr)
	{
		OceanMaterial->SetTextureParameterValue(TEXT("SkyCube"), SkyCube);
	}
}

UStaticMesh* USwcWaterSubsystem::BuildMesh(const swc::water::Body& Body, const FVector& Center, UMaterialInterface* Material) const
{
	FMeshDescription Description;
	FStaticMeshAttributes Attributes(Description);
	Attributes.Register();
	TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	UVs.SetNumChannels(1);
	const FPolygonGroupID Group = Description.CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Water");
	const int32 Count = static_cast<int32>(Body.vertex_count());
	TArray<FVector3f> Points;
	Points.SetNum(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float* V = &Body.vertices[static_cast<size_t>(Index) * 5];
		Points[Index] = FVector3f(ToUnreal(V[0], V[1], V[2]) - Center);
	}
	// The tangent basis of the UV of the mesh (a river: u across, v along the flow; the material reads it); a volume reads the
	// world UV of Unreal (x, y): its tangent is +X.
	TArray<FVector3f> AlongU;
	TArray<FVector3f> AlongV;
	AlongU.Init(FVector3f::ZeroVector, Count);
	AlongV.Init(FVector3f::ZeroVector, Count);
	if (Body.kind == swc::water::Kind::river)
	{
		for (size_t K = 0; K + 2 < Body.indices.size(); K += 3)
		{
			const uint32 I[3] = {Body.indices[K], Body.indices[K + 1], Body.indices[K + 2]};
			const FVector3f E1 = Points[I[1]] - Points[I[0]];
			const FVector3f E2 = Points[I[2]] - Points[I[0]];
			const float* T0 = &Body.vertices[I[0] * 5 + 3];
			const float* T1 = &Body.vertices[I[1] * 5 + 3];
			const float* T2 = &Body.vertices[I[2] * 5 + 3];
			const float Du1 = T1[0] - T0[0], Dv1 = T1[1] - T0[1], Du2 = T2[0] - T0[0], Dv2 = T2[1] - T0[1];
			const float Det = Du1 * Dv2 - Du2 * Dv1;
			if (FMath::Abs(Det) < 1e-12f)
			{
				continue;
			}
			const FVector3f U = (E1 * Dv2 - E2 * Dv1) / Det;
			const FVector3f V = (E2 * Du1 - E1 * Du2) / Det;
			for (const uint32 Vertex : I)
			{
				AlongU[Vertex] += U;
				AlongV[Vertex] += V;
			}
		}
	}
	TArray<FVertexInstanceID> Instances;
	Instances.SetNum(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVertexID Vertex = Description.CreateVertex();
		Positions[Vertex] = Points[Index];
		const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
		const FVector3f Up(0.0f, 0.0f, 1.0f);
		FVector3f Tangent(AlongU[Index].X, AlongU[Index].Y, 0.0f);
		float Sign = 1.0f;
		if (Tangent.Normalize())
		{
			Sign = FVector3f::DotProduct(FVector3f::CrossProduct(Up, Tangent), AlongV[Index]) >= 0.0f ? 1.0f : -1.0f;
		}
		else
		{
			Tangent = FVector3f(1.0f, 0.0f, 0.0f);
		}
		Normals[Instance] = Up;
		Tangents[Instance] = Tangent;
		Signs[Instance] = Sign;
		UVs.Set(Instance, 0, FVector2f(Body.vertices[static_cast<size_t>(Index) * 5 + 3], Body.vertices[static_cast<size_t>(Index) * 5 + 4]));
		Instances[Index] = Instance;
	}
	// The triangles: counterclockwise from above in engine space; the mirror of y (Unreal space) gives the order of an upward face.
	for (size_t K = 0; K + 2 < Body.indices.size(); K += 3)
	{
		Description.CreateTriangle(Group, {Instances[Body.indices[K]], Instances[Body.indices[K + 1]], Instances[Body.indices[K + 2]]});
	}
	UStaticMesh* Mesh = NewObject<UStaticMesh>(WaterActor);
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, TEXT("Water")));
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;
	Params.bMarkPackageDirty = false;
	Params.bBuildSimpleCollision = false;
	Params.bCommitMeshDescription = false;
	Mesh->BuildFromMeshDescriptions({&Description}, Params);
	return Mesh;
}

void USwcWaterSubsystem::UpdateBodies(const FVector& View)
{
	if (Parent == nullptr)
	{
		return;
	}
	// The nearest surfaces that are not built, in the build range.
	TArray<TPair<double, int32>> Wanted;
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		const FBodyEntry& Entry = Bodies[Index];
		const double Distance = FVector::Dist(View, Entry.Center) - Entry.Radius;
		if (!Entry.bBuilt && Distance <= BuildRange)
		{
			Wanted.Emplace(Distance, Index);
		}
	}
	Wanted.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	for (int32 K = 0; K < Wanted.Num() && K < BuildsPerTick; ++K)
	{
		FBodyEntry& Entry = Bodies[Wanted[K].Value];
		Entry.bBuilt = true;
		UMaterialInstanceDynamic* Material = MakeMaterial(Entry.Body);
		UStaticMesh* Mesh = BuildMesh(Entry.Body, Entry.Center, Material);
		Meshes.Add(Mesh);
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(WaterActor);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		Component->SetMaterial(0, Material);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->bAffectDistanceFieldLighting = false;
		Component->SetCullDistance(DrawDistance + static_cast<float>(Entry.Radius));
		Component->SetupAttachment(WaterActor->GetRootComponent());
		Component->RegisterComponent();
		Component->SetWorldLocation(Entry.Center);
		UE_LOG(LogSwcWater, Verbose, TEXT("water: %s (%s, %d vertices) at %.0f m"), UTF8_TO_TCHAR(Entry.Body.name.c_str()),
			Entry.Body.kind == swc::water::Kind::river ? TEXT("river") : TEXT("volume"), static_cast<int32>(Entry.Body.vertex_count()), Wanted[K].Key / 100.0);
	}
}

void USwcWaterSubsystem::UpdateOcean(const FVector& View, float DeltaTime)
{
	if (Parent == nullptr)
	{
		return;
	}
	if (Ocean == nullptr)
	{
		// The grid: (OceanCells + 1)^2 points, flat, with world UV in the material.
		swc::water::Body Grid;
		Grid.kind = swc::water::Kind::volume;
		const double Half = OceanCells * OceanCell * 0.5 / 100.0;
		for (int32 Y = 0; Y <= OceanCells; ++Y)
		{
			for (int32 X = 0; X <= OceanCells; ++X)
			{
				Grid.vertices.insert(Grid.vertices.end(), {static_cast<float>(-Half + X * OceanCell / 100.0), static_cast<float>(-Half + Y * OceanCell / 100.0), 0.0f, 0.0f, 0.0f});
			}
		}
		for (int32 Y = 0; Y < OceanCells; ++Y)
		{
			for (int32 X = 0; X < OceanCells; ++X)
			{
				const uint32 A = Y * (OceanCells + 1) + X;
				const uint32 B = A + 1;
				const uint32 C = A + OceanCells + 1;
				const uint32 D = C + 1;
				// Counterclockwise from above in engine space (x east, y north).
				Grid.indices.insert(Grid.indices.end(), {A, B, D, A, D, C});
			}
		}
		OceanMaterial = UMaterialInstanceDynamic::Create(Parent, WaterActor);
		if (SkyCube != nullptr)
		{
			OceanMaterial->SetTextureParameterValue(TEXT("SkyCube"), SkyCube);
		}
		UStaticMesh* Mesh = BuildMesh(Grid, FVector::ZeroVector, OceanMaterial);
		Meshes.Add(Mesh);
		Ocean = NewObject<UStaticMeshComponent>(WaterActor);
		Ocean->SetMobility(EComponentMobility::Movable);
		Ocean->SetStaticMesh(Mesh);
		Ocean->SetMaterial(0, OceanMaterial);
		Ocean->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ocean->SetCastShadow(false);
		Ocean->bAffectDistanceFieldLighting = false;
		Ocean->SetupAttachment(WaterActor->GetRootComponent());
		Ocean->RegisterComponent();
		OceanNow = OceanBase;
		OceanTarget = OceanBase;
	}
	// The grid follows the view in steps of one cell.
	Ocean->SetWorldLocation(FVector(FMath::RoundToDouble(View.X / OceanCell) * OceanCell, FMath::RoundToDouble(View.Y / OceanCell) * OceanCell, OceanLevel * 100.0));
	// The local override of the place of the view (the first one whose shape has the view), with its transition time.
	const FVector3d Engine(View.X / 100.0, -View.Y / 100.0, View.Z / 100.0);
	int32 Selected = INDEX_NONE;
	for (int32 Index = 0; Index < Overrides.Num() && Selected == INDEX_NONE; ++Index)
	{
		const FOceanOverride& Override = Overrides[Index];
		const FVector3d D = Engine - Override.Translation;
		const double* M = Override.Inverse;
		const FVector3d Local(M[0] * D.X + M[1] * D.Y + M[2] * D.Z, M[3] * D.X + M[4] * D.Y + M[5] * D.Z, M[6] * D.X + M[7] * D.Y + M[8] * D.Z);
		const bool bInside = Override.Shape == TEXT("cylinder")
			? FVector2d(Local.X, Local.Y).Size() <= Override.Radius && FMath::Abs(Local.Z) <= Override.Height * 0.5
			: Override.Shape == TEXT("box") ? FMath::Abs(Local.X) <= Override.Dimensions.X * 0.5 && FMath::Abs(Local.Y) <= Override.Dimensions.Y * 0.5
				&& FMath::Abs(Local.Z) <= Override.Dimensions.Z * 0.5
			: Override.Shape == TEXT("sphere") && Local.Size() <= Override.Radius;
		Selected = bInside ? Index : INDEX_NONE;
	}
	if (Selected != OceanSelected)
	{
		const double Transition = Overrides.IsValidIndex(Selected) ? Overrides[Selected].Transition
			: Overrides.IsValidIndex(OceanSelected) ? Overrides[OceanSelected].Transition : 0.0;
		UE_LOG(LogSwcWater, Log, TEXT("water: the ocean of %s (transition %.1f s)"), Overrides.IsValidIndex(Selected) ? *Overrides[Selected].Entity : TEXT("the mission"), Transition);
		OceanFrom = OceanNow;
		OceanTarget = Overrides.IsValidIndex(Selected) ? Overrides[Selected].Values : OceanBase;
		OceanBlendTime = OceanSelected == -2 ? 0.0 : Transition;
		OceanBlend = 0.0;
		OceanSelected = Selected;
	}
	OceanBlend = OceanBlendTime <= 0.0 ? 1.0 : FMath::Min(1.0, OceanBlend + DeltaTime / OceanBlendTime);
	for (const TPair<FName, float>& Pair : OceanTarget)
	{
		const float From = OceanFrom.Contains(Pair.Key) ? OceanFrom[Pair.Key] : Pair.Value;
		OceanNow.Add(Pair.Key, FMath::Lerp(From, Pair.Value, static_cast<float>(OceanBlend)));
	}
	for (const TPair<FName, float>& Pair : OceanNow)
	{
		const FString Key = Pair.Key.ToString();
		if (!Key.StartsWith(TEXT("FogColor")) && !Key.StartsWith(TEXT("NearFogColor")))
		{
			OceanMaterial->SetScalarParameterValue(Pair.Key, Pair.Value);
		}
	}
	OceanMaterial->SetVectorParameterValue(TEXT("FogColor"), FLinearColor(OceanNow.FindRef(TEXT("FogColorR")), OceanNow.FindRef(TEXT("FogColorG")),
		OceanNow.FindRef(TEXT("FogColorB")), 1.0f));
	OceanMaterial->SetVectorParameterValue(TEXT("NearFogColor"), FLinearColor(OceanNow.FindRef(TEXT("NearFogColorR")), OceanNow.FindRef(TEXT("NearFogColorG")),
		OceanNow.FindRef(TEXT("NearFogColorB")), 1.0f));
	ApplyOverrides(OceanMaterial);
}

void USwcWaterSubsystem::HideWorldOcean()
{
	// The flat ocean plane of the world level (build_world.py: the actor "ocean" with the material M_SwcOcean) is in the place of
	// this ocean: it hides while the world track keeps it (docs/WATER.md).
	if (Parent == nullptr)
	{
		return;
	}
	// spawn_mesh_actor made a static mesh actor: only those (the cells of the world are other actors).
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		const UStaticMeshComponent* Component = It->GetStaticMeshComponent();
		const UMaterialInterface* Material = Component != nullptr ? Component->GetMaterial(0) : nullptr;
		if (Material != nullptr && Material->GetPathName().Contains(TEXT("/M_SwcOcean")) && (!It->IsHidden() || It->GetActorEnableCollision()))
		{
			// Its collision too: a character walked on it at 40.000 m (the water of the movement is under the surface).
			It->SetActorHiddenInGame(true);
			It->SetActorEnableCollision(false);
			UE_LOG(LogSwcWater, Log, TEXT("water: the ocean plane of the world level (%s) hides, with no collision"), *It->GetName());
		}
	}
}

std::optional<swc::water::Sample> USwcWaterSubsystem::GetWaterAt(double X, double Y) const
{
	const int32 RegionX = FMath::FloorToInt32(X / swc::water::kRegionSize);
	const int32 RegionY = FMath::FloorToInt32(Y / swc::water::kRegionSize);
	const TSharedPtr<swc::water::Tree>* Tree = Trees.Find(FIntPoint(RegionX, RegionY));
	// The integer cell of the point (the rule of the retail client); where the quadtree has no water, the ocean.
	const std::optional<swc::water::Sample> Leaf = Tree != nullptr
		? (*Tree)->at(FMath::FloorToDouble(X) - RegionX * swc::water::kRegionSize, FMath::FloorToDouble(Y) - RegionY * swc::water::kRegionSize)
		: std::nullopt;
	return swc::water::with_ocean(Leaf, static_cast<float>(OceanLevel));
}

swc::water::DepthClass USwcWaterSubsystem::GetDepthClass(double X, double Y, double FeetZ, float* OutDepth) const
{
	return swc::water::depth_class(GetWaterAt(X, Y), static_cast<float>(FeetZ), OutDepth);
}
