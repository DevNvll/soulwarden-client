#include "Effects/SwcRibbons.h"

#include "Dom/JsonObject.h"
#include "DynamicMeshBuilder.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialRenderProxy.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PrimitiveSceneProxy.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Effects/SwcParticleEffects.h"

#include "Materials/Material.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcRibbons, Log, All);

namespace
{
FVector2f Range(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, float Default)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (Object->TryGetArrayField(Field, Values) && Values->Num() >= 2)
	{
		return FVector2f(float((*Values)[0]->AsNumber()), float((*Values)[1]->AsNumber()));
	}
	return FVector2f(Default, Default);
}

TArray<float> Curve(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
	TArray<float> Out;
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (Object->TryGetArrayField(Field, Values))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			Out.Add(float(Value->AsNumber()));
		}
	}
	return Out;
}

/** The value of a curve of uniform samples over the age 0 to 1 (an empty curve: 1). */
float Sample(const TArray<float>& Values, float T)
{
	if (Values.IsEmpty())
	{
		return 1.0f;
	}
	const float X = FMath::Clamp(T, 0.0f, 1.0f) * float(Values.Num() - 1);
	const int32 I = FMath::Min(int32(X), Values.Num() - 1);
	const int32 J = FMath::Min(I + 1, Values.Num() - 1);
	return FMath::Lerp(Values[I], Values[J], X - float(I));
}

FVector3f Sample(const TArray<FVector3f>& Values, float T)
{
	if (Values.IsEmpty())
	{
		return FVector3f::OneVector;
	}
	const float X = FMath::Clamp(T, 0.0f, 1.0f) * float(Values.Num() - 1);
	const int32 I = FMath::Min(int32(X), Values.Num() - 1);
	const int32 J = FMath::Min(I + 1, Values.Num() - 1);
	return FMath::Lerp(Values[I], Values[J], X - float(I));
}

const TMap<FString, TArray<FSwcRibbonSpec>>& Specs()
{
	static TMap<FString, TArray<FSwcRibbonSpec>> Out;
	static bool bLoaded = false;
	if (bLoaded)
	{
		return Out;
	}
	bLoaded = true;
	const FString Path = FPaths::Combine(USwcEffectSubsystem::Folder(), TEXT("ribbons.json"));
	FString Text;
	TSharedPtr<FJsonObject> Doc;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) || !Doc.IsValid())
	{
		UE_LOG(LogSwcRibbons, Warning, TEXT("ribbons: no %s (tools/import-particles.sh)"), *Path);
		return Out;
	}
	int32 Count = 0;
	for (const auto& Effect : Doc->GetObjectField(TEXT("effects"))->Values)
	{
		TArray<FSwcRibbonSpec>& List = Out.Add(FString(Effect.Key.ToView()));
		for (const TSharedPtr<FJsonValue>& Value : Effect.Value->AsArray())
		{
			const TSharedPtr<FJsonObject> Data = Value->AsObject();
			FSwcRibbonSpec& Spec = List.AddDefaulted_GetRef();
			Spec.Name = Data->GetStringField(TEXT("name"));
			Spec.Material = Data->GetStringField(TEXT("material"));
			Spec.Delay = float(Data->GetNumberField(TEXT("delay")));
			Spec.EmitterLife = float(Data->GetNumberField(TEXT("emitterLife")));
			const FVector2f Rate = Range(Data, TEXT("rate"), 0.0f);
			Spec.Rate = 0.5f * (Rate.X + Rate.Y);
			Spec.Life = Range(Data, TEXT("life"), 1.0f);
			Spec.Width = Range(Data, TEXT("width"), 10.0f);
			Spec.WidthCurve = Curve(Data, TEXT("widthCurve"));
			const TArray<float> Color = Curve(Data, TEXT("color"));
			if (Color.Num() >= 3)
			{
				Spec.Color = FLinearColor(Color[0], Color[1], Color[2]);
			}
			const TArray<TSharedPtr<FJsonValue>>* ColorCurve = nullptr;
			if (Data->TryGetArrayField(TEXT("colorCurve"), ColorCurve))
			{
				for (const TSharedPtr<FJsonValue>& Rgb : *ColorCurve)
				{
					const TArray<TSharedPtr<FJsonValue>>& C = Rgb->AsArray();
					Spec.ColorCurve.Add(C.Num() >= 3 ? FVector3f(C[0]->AsNumber(), C[1]->AsNumber(), C[2]->AsNumber()) : FVector3f::OneVector);
				}
			}
			Spec.Alpha = Range(Data, TEXT("alpha"), 1.0f);
			Spec.AlphaCurve = Curve(Data, TEXT("alphaCurve"));
			Spec.bStreamUV = Data->GetStringField(TEXT("uv")) == TEXT("stream");
			Spec.Frequency = float(Data->GetNumberField(TEXT("frequency")));
			const TArray<float> Offset = Curve(Data, TEXT("offset"));
			if (Offset.Num() >= 3)
			{
				Spec.Offset = FVector(Offset[0], Offset[1], Offset[2]);
			}
			++Count;
		}
	}
	UE_LOG(LogSwcRibbons, Log, TEXT("ribbons: %d ribbons of %d effects"), Count, Out.Num());
	return Out;
}

/** The strip of a ribbon: for each view, two vertices at each point across the line to the camera. */
class FSwcRibbonProxy final : public FPrimitiveSceneProxy
{
public:
	FSwcRibbonProxy(const USwcRibbonComponent* Component, UMaterialInterface* InMaterial)
		: FPrimitiveSceneProxy(Component)
		, Material(InMaterial != nullptr ? InMaterial : UMaterial::GetDefaultMaterial(MD_Surface))
	{
		MaterialRelevance = Material->GetRelevance_Concurrent(GetScene().GetShaderPlatform());
		bWillEverBeLit = false;
	}

	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap,
	                                    FMeshElementCollector& Collector) const override
	{
		if (Points.Num() < 2)
		{
			return;
		}
		const FMaterialRenderProxy* RenderProxy = Material->GetRenderProxy();
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			if ((VisibilityMap & (1u << ViewIndex)) == 0)
			{
				continue;
			}
			const FVector Eye = Views[ViewIndex]->ViewMatrices.GetViewOrigin();
			FDynamicMeshBuilder Builder(Views[ViewIndex]->GetFeatureLevel());
			FVector LastSide = FVector::ZeroVector;
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				const USwcRibbonComponent::FRenderPoint& Point = Points[Index];
				const FVector Along = Points[FMath::Min(Index + 1, Points.Num() - 1)].Position - Points[FMath::Max(Index - 1, 0)].Position;
				FVector Side = FVector::CrossProduct(Along, Eye - Point.Position).GetSafeNormal();
				if (Side.IsNearlyZero())
				{
					Side = LastSide;
				}
				LastSide = Side;
				const FVector3f Normal = FVector3f((Eye - Point.Position).GetSafeNormal());
				const FVector3f Tangent = FVector3f(Along.GetSafeNormal());
				Builder.AddVertex(FDynamicMeshVertex(FVector3f(Point.Position - Side * Point.HalfWidth), Tangent, Normal, FVector2f(Point.U, 0.0f),
					Point.Color));
				Builder.AddVertex(FDynamicMeshVertex(FVector3f(Point.Position + Side * Point.HalfWidth), Tangent, Normal, FVector2f(Point.U, 1.0f),
					Point.Color));
				if (Index > 0)
				{
					const int32 A = (Index - 1) * 2;
					Builder.AddTriangle(A, A + 1, A + 2);
					Builder.AddTriangle(A + 1, A + 3, A + 2);
				}
			}
			Builder.GetMesh(FMatrix::Identity, RenderProxy, SDPG_World, true, false, ViewIndex, Collector);
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bDynamicRelevance = true;
		Result.bShadowRelevance = false;
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		return Result;
	}

	virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize() + Points.GetAllocatedSize(); }

	TArray<USwcRibbonComponent::FRenderPoint> Points;

private:
	UMaterialInterface* Material = nullptr;
	FMaterialRelevance MaterialRelevance;
};
} // namespace

const TArray<FSwcRibbonSpec>* SwcRibbons::Of(const FString& Effect)
{
	return Specs().Find(Effect);
}

USwcRibbonComponent::USwcRibbonComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CastShadow = false;
	bUseAsOccluder = false;
	bSelectable = false;
}

void USwcRibbonComponent::Setup(const FSwcRibbonSpec& InSpec, const FLinearColor& InTint, float InAlphaScale, float InWidthScale, float InTimeScale)
{
	Spec = InSpec;
	Tint = InTint;
	AlphaScale = InAlphaScale;
	WidthScale = InWidthScale;
	TimeScale = InTimeScale;
	RibbonMaterial = LoadObject<UMaterialInterface>(nullptr, *Spec.Material);
	if (RibbonMaterial == nullptr)
	{
		UE_LOG(LogSwcRibbons, Warning, TEXT("ribbons: no material %s (%s)"), *Spec.Material, *Spec.Name);
	}
	Points.Reset();
	Clock = 0.0f;
	// The first point at the start of the emission (research/particle-rules3.md 3 [R]: no wait of one interval).
	Pending = 1.0f;
	bHasLast = false;
	bStopped = false;
	MarkRenderStateDirty();
}

void USwcRibbonComponent::Stop(bool bKill)
{
	bStopped = true;
	if (bKill)
	{
		Points.Reset();
	}
}

void USwcRibbonComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// The time of the effect (swc.Effects.TimeScale, a check value, and the time scale of the clip).
	static const IConsoleVariable* Scale = IConsoleManager::Get().FindConsoleVariable(TEXT("swc.Effects.TimeScale"));
	const float Dt = DeltaTime * TimeScale * (Scale != nullptr ? FMath::Max(Scale->GetFloat(), 0.001f) : 1.0f);
	const float From = Clock;
	Clock += Dt;
	for (int32 Index = Points.Num() - 1; Index >= 0; --Index)
	{
		Points[Index].Age += Dt;
		if (Points[Index].Age >= Points[Index].Life)
		{
			Points.RemoveAt(Index);
		}
	}
	const FVector Here = GetComponentLocation();
	const float End = Spec.EmitterLife > 0.0f ? Spec.Delay + Spec.EmitterLife : TNumericLimits<float>::Max();
	const bool bEmitting = !bStopped && Clock >= Spec.Delay && From < End;
	if (bEmitting && Spec.Rate > 0.0f)
	{
		// The points of this frame along the move of the emitter (the older ones nearer the last place).
		const float Window = FMath::Min(Clock, End) - FMath::Max(From, Spec.Delay);
		Pending += Spec.Rate * FMath::Max(Window, 0.0f);
		const int32 Count = FMath::FloorToInt(Pending);
		Pending -= float(Count);
		const FVector Start = bHasLast ? LastPosition : Here;
		for (int32 Index = 1; Index <= Count; ++Index)
		{
			const float T = float(Index) / float(Count);
			FPoint& Point = Points.AddDefaulted_GetRef();
			Point.Position = FMath::Lerp(Start, Here, T);
			Point.Age = (1.0f - T) * Dt;
			Point.Life = FMath::Max(FMath::FRandRange(Spec.Life.X, Spec.Life.Y), 0.01f);
			Point.Width = FMath::FRandRange(Spec.Width.X, Spec.Width.Y) * WidthScale;
			Point.Alpha = FMath::FRandRange(Spec.Alpha.X, Spec.Alpha.Y);
		}
	}
	LastPosition = Here;
	bHasLast = true;
	if ((bStopped || Clock >= End) && Points.IsEmpty())
	{
		DestroyComponent();
		return;
	}
	BuildRenderPoints();
	UpdateBounds();
	MarkRenderTransformDirty();
	MarkRenderDynamicDataDirty();
}

void USwcRibbonComponent::BuildRenderPoints()
{
	RenderPoints.Reset(Points.Num());
	PointBounds = FBox(ForceInit);
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const FPoint& Point = Points[Index];
		const float T = Point.Age / Point.Life;
		const FVector3f Rgb = Sample(Spec.ColorCurve, T);
		const float Alpha = FMath::Clamp(Point.Alpha * Sample(Spec.AlphaCurve, T) * AlphaScale, 0.0f, 1.0f);
		FRenderPoint& Out = RenderPoints.AddDefaulted_GetRef();
		Out.Position = Point.Position;
		Out.HalfWidth = 0.5f * Point.Width * FMath::Max(Sample(Spec.WidthCurve, T), 0.0f);
		// The texture coordinate: from the newest point (U 0) to the oldest [I]; over the whole ribbon (PerStream) or each segment.
		const int32 FromHead = Points.Num() - 1 - Index;
		Out.U = Spec.bStreamUV ? Spec.Frequency * float(FromHead) / float(FMath::Max(Points.Num() - 1, 1)) : Spec.Frequency * float(FromHead);
		Out.Color = FLinearColor(Spec.Color.R * Rgb.X * Tint.R, Spec.Color.G * Rgb.Y * Tint.G, Spec.Color.B * Rgb.Z * Tint.B, Alpha).ToFColor(false);
		PointBounds += Point.Position;
	}
}

void USwcRibbonComponent::SendRenderDynamicData_Concurrent()
{
	Super::SendRenderDynamicData_Concurrent();
	if (SceneProxy != nullptr)
	{
		FSwcRibbonProxy* Proxy = static_cast<FSwcRibbonProxy*>(SceneProxy);
		ENQUEUE_RENDER_COMMAND(SwcRibbonPoints)([Proxy, Copy = RenderPoints](FRHICommandListImmediate&) mutable { Proxy->Points = MoveTemp(Copy); });
	}
}

FPrimitiveSceneProxy* USwcRibbonComponent::CreateSceneProxy()
{
	FSwcRibbonProxy* Proxy = new FSwcRibbonProxy(this, RibbonMaterial);
	Proxy->Points = RenderPoints;
	return Proxy;
}

FBoxSphereBounds USwcRibbonComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// The points are in world space (the strip ignores the transform of the component).
	if (!PointBounds.IsValid)
	{
		return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector(50.0), 50.0);
	}
	float Widest = 0.0f;
	for (const FRenderPoint& Point : RenderPoints)
	{
		Widest = FMath::Max(Widest, Point.HalfWidth);
	}
	return FBoxSphereBounds(PointBounds.ExpandBy(Widest + 10.0));
}

UMaterialInterface* USwcRibbonComponent::GetMaterial(int32 ElementIndex) const
{
	return RibbonMaterial;
}

void USwcRibbonComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (RibbonMaterial != nullptr)
	{
		OutMaterials.Add(RibbonMaterial);
	}
}
