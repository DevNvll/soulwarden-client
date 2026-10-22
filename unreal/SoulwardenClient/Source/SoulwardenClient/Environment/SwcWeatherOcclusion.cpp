#include "Environment/SwcWeatherOcclusion.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RHICommandList.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "RenderingThread.h"
#include "TextureResource.h"

namespace
{
constexpr float NoHit = -1.0e7f;

/** @return True when a hit is a roof for the weather: a structure. The terrain, the rocks and cliffs, and the plants are open sky [I]
 *  (retail accepts selected occluders, up to 768; with the first hit of each line a tall rock covered its own lower faces: hard
 *  straight edges of the snow along the cells of the map, AST-257). */
bool IsRoof(const UPrimitiveComponent* Component)
{
	if (Component == nullptr || Component->GetClass()->GetName().Contains(TEXT("Landscape")))
	{
		return false;
	}
	const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component);
	const FString Path = Mesh != nullptr && Mesh->GetStaticMesh() != nullptr ? Mesh->GetStaticMesh()->GetPathName().ToLower() : FString();
	static const TCHAR* const Nature[] = {TEXT("/nature/"), TEXT("_nat_"), TEXT("rock"), TEXT("cliff"), TEXT("boulder"), TEXT("stone"),
		TEXT("tree"), TEXT("veg_"), TEXT("terrain")};
	for (const TCHAR* Word : Nature)
	{
		if (Path.Contains(Word))
		{
			return false;
		}
	}
	return true;
}
} // namespace

/** The lines of one map in work: the heights and the lines that did not answer yet. The callbacks of the async traces write into it
 *  (a weak pointer: a callback after the end of the map does nothing). */
struct FSwcWeatherOcclusion::FBuild
{
	TArray<float> Heights;
	int32 Left = 0;
	int32 Hits = 0;
};

FSwcWeatherOcclusion::FSwcWeatherOcclusion() = default;

FSwcWeatherOcclusion::~FSwcWeatherOcclusion() = default;

UTexture* FSwcWeatherOcclusion::GetTexture() const
{
	if (!bReady)
	{
		return nullptr;
	}
	return Target.IsValid() ? static_cast<UTexture*>(Target.Get()) : static_cast<UTexture*>(Texture.Get());
}

FLinearColor FSwcWeatherOcclusion::GetOrigin() const
{
	return FLinearColor(static_cast<float>(Origin.X), static_cast<float>(Origin.Y), static_cast<float>(Size * TexelCm), bReady ? 1.0f : 0.0f);
}

double FSwcWeatherOcclusion::HeightAt(const FVector& Point) const
{
	if (!bReady)
	{
		return NoHit;
	}
	const int32 X = FMath::FloorToInt32((Point.X - Origin.X) / TexelCm);
	const int32 Y = FMath::FloorToInt32((Point.Y - Origin.Y) / TexelCm);
	return X >= 0 && Y >= 0 && X < Size && Y < Size ? Heights[Y * Size + X] : NoHit;
}

FString FSwcWeatherOcclusion::Describe() const
{
	return FString::Printf(TEXT("the roof map %s (%d maps, %d of %d lines hit, %.1f ms from the start to the texture)"),
		bReady ? TEXT("ready") : TEXT("not ready"), Builds, LastHits, Size * Size, LastBuildMs);
}

void FSwcWeatherOcclusion::Start(UWorld& World, const FVector& Center, const AActor* Ignored)
{
	TSharedPtr<FBuild> Build = MakeShared<FBuild>();
	Build->Heights.Init(NoHit, Size * Size);
	Build->Left = Size * Size;
	Building = Build;
	PendingCenter = Center;
	PendingOrigin = FVector2D(Center.X - Size * TexelCm * 0.5, Center.Y - Size * TexelCm * 0.5);
	StartTime = FPlatformTime::Seconds();
	PendingIgnored = Ignored;
	NextLine = 0;
	Issue(World);
}

void FSwcWeatherOcclusion::Issue(UWorld& World)
{
	// At most LinesPerFrame lines each frame: all the lines of a map in one frame crowded the async traces of that frame (the camera
	// of a check run stood in a wall; AST-257).
	if (!Building.IsValid() || NextLine >= Size * Size)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SwcWeatherOcclusion), false, PendingIgnored.Get());
	const TWeakPtr<FBuild> Weak = Building;
	FTraceDelegate Done;
	Done.BindLambda([Weak](const FTraceHandle&, FTraceDatum& Datum)
	{
		if (const TSharedPtr<FBuild> B = Weak.Pin())
		{
			const int32 Index = static_cast<int32>(Datum.UserData);
			if (B->Heights.IsValidIndex(Index) && Datum.OutHits.Num() > 0 && Datum.OutHits[0].bBlockingHit && IsRoof(Datum.OutHits[0].GetComponent()))
			{
				B->Heights[Index] = static_cast<float>(Datum.OutHits[0].ImpactPoint.Z);
				++B->Hits;
			}
			--B->Left;
		}
	});
	const int32 Last = FMath::Min(NextLine + LinesPerFrame, Size * Size);
	for (; NextLine < Last; ++NextLine)
	{
		const int32 X = NextLine % Size;
		const int32 Y = NextLine / Size;
		const double PX = PendingOrigin.X + (X + 0.5) * TexelCm;
		const double PY = PendingOrigin.Y + (Y + 0.5) * TexelCm;
		World.AsyncLineTraceByChannel(EAsyncTraceType::Single, FVector(PX, PY, PendingCenter.Z + AboveCm), FVector(PX, PY, PendingCenter.Z - BelowCm),
			ECC_Visibility, Params, FCollisionResponseParams::DefaultResponseParam, &Done, static_cast<uint32>(NextLine));
	}
}

bool FSwcWeatherOcclusion::Collect()
{
	if (!Building.IsValid())
	{
		return false;
	}
	if (Building->Left > 0)
	{
		// A lost line (a world change) must not hold the map for ever.
		if (FPlatformTime::Seconds() - StartTime < 4.0)
		{
			return false;
		}
	}
	Heights = MoveTemp(Building->Heights);
	LastHits = Building->Hits;
	Building.Reset();
	if (!bTargetLoaded)
	{
		bTargetLoaded = true;
		UTextureRenderTarget2D* Asset = LoadObject<UTextureRenderTarget2D>(nullptr, TEXT("/SwcGameContent/Environment/Snow/RT_SwcWeatherOcclusion.RT_SwcWeatherOcclusion"), nullptr, LOAD_NoWarn);
		if (Asset != nullptr && Asset->SizeX == Size && Asset->SizeY == Size && Asset->RenderTargetFormat == RTF_R32f)
		{
			Target.Reset(Asset);
		}
	}
	if (Target.IsValid())
	{
		// The render target asset: the heights go into its texture on the render thread (the materials sample it).
		if (FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource())
		{
			ENQUEUE_RENDER_COMMAND(SwcWeatherOcclusion)([Resource, Copy = Heights](FRHICommandListImmediate& RHICmdList)
			{
				if (FRHITexture* RHITexture = Resource->GetRenderTargetTexture())
				{
					const FUpdateTextureRegion2D Region(0, 0, 0, 0, Size, Size);
					RHICmdList.UpdateTexture2D(RHITexture, 0, Region, Size * sizeof(float), reinterpret_cast<const uint8*>(Copy.GetData()));
				}
			});
		}
	}
	else
	{
		// No render target asset: a transient texture (the flakes only).
		if (!Texture.IsValid())
		{
			UTexture2D* New = UTexture2D::CreateTransient(Size, Size, PF_R32_FLOAT, TEXT("SwcWeatherOcclusion"));
			New->Filter = TF_Nearest;
			New->SRGB = false;
			New->AddressX = TA_Clamp;
			New->AddressY = TA_Clamp;
			New->NeverStream = true;
			New->UpdateResource();
			Texture.Reset(New);
		}
		float* Copy = static_cast<float*>(FMemory::Malloc(Heights.Num() * sizeof(float)));
		FMemory::Memcpy(Copy, Heights.GetData(), Heights.Num() * sizeof(float));
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Size, Size);
		Texture->UpdateTextureRegions(0, 1, Region, Size * sizeof(float), sizeof(float), reinterpret_cast<uint8*>(Copy),
			[](uint8* Data, const FUpdateTextureRegion2D* Regions)
			{
				FMemory::Free(Data);
				delete Regions;
			});
	}
	Origin = PendingOrigin;
	MapCenter = PendingCenter;
	bReady = true;
	++Builds;
	LastBuildMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	return true;
}

void FSwcWeatherOcclusion::Update(UWorld& World, const FVector& Camera, const AActor* Ignored)
{
	if (Building.IsValid())
	{
		if (NextLine < Size * Size)
		{
			Issue(World);
			return;
		}
		Collect();
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const bool bMoved = FVector2D::Distance(FVector2D(Camera), FVector2D(MapCenter)) > HalfMoveCm || FMath::Abs(Camera.Z - MapCenter.Z) > AboveCm * 0.5;
	if (!bMoved && Now < NextRefresh)
	{
		return;
	}
	NextRefresh = Now + RefreshSeconds;
	const FVector Center(FMath::GridSnap(Camera.X, 8.0 * TexelCm), FMath::GridSnap(Camera.Y, 8.0 * TexelCm), Camera.Z);
	Start(World, Center, Ignored);
}
