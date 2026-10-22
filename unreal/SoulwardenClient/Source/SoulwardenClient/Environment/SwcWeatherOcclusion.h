#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "WorldCollision.h"

class UTexture2D;
class UTextureRenderTarget2D;
class UWorld;
class AActor;

/**
 * The weather occlusion from above (AST-257; research/snow.md section 5 "Roof occlusion"): a height map of the first surface under the
 * sky around the camera, for the falling snow (and later the surface snow). Retail renders a 256 x 256 overhead depth map of up to 768
 * occluders (the shared weather map; research/e5-source-rain.md section 4). This client [I]: Size x Size downward lines (the channel
 * Visibility; the pawns do not block it; a first hit on the terrain, a rock, or a plant is open sky: only structures are roofs) on a grid of TexelCm around a
 * center snapped to the grid, as async traces, into a float texture (PF_R32_FLOAT, the height in Unreal cm; no hit: -1e7). The map is
 * made again when the camera is HalfMoveCm from its center, and each RefreshSeconds (the streamed collision comes later).
 */
class FSwcWeatherOcclusion
{
public:
	static constexpr int32 Size = 96;
	static constexpr double TexelCm = 100.0;
	static constexpr double HalfMoveCm = 1600.0;
	static constexpr double RefreshSeconds = 4.0;
	/** The lines start this far above the camera and end this far below it (cm). */
	static constexpr double AboveCm = 6000.0;
	static constexpr double BelowCm = 6000.0;
	/** The lines sent in one frame (the rest in the next frames). */
	static constexpr int32 LinesPerFrame = 1024;

	FSwcWeatherOcclusion();
	~FSwcWeatherOcclusion();

	/** Each frame: the camera (Unreal cm). Ignored: the pawn of the player. */
	void Update(UWorld& World, const FVector& Camera, const AActor* Ignored);

	/** @return The texture of the map: the render target asset RT_SwcWeatherOcclusion (the materials of the world read it through
	 *  MF_SwcSnowSurface), else a transient texture; null before the first map. */
	UTexture* GetTexture() const;
	/** @return X, Y of the low corner of the map (cm), its side (cm), and 1 when a map is ready (else 0). */
	FLinearColor GetOrigin() const;
	/** @return The low corner of the map (cm, double: the material gets it relative to the camera). */
	FVector2D GetCorner() const { return Origin; }
	/** @return The height of the map at a point (cm), or -1e7 (no map, outside, or no hit). */
	double HeightAt(const FVector& Point) const;
	/** @return A short text for the log. */
	FString Describe() const;

private:
	void Start(UWorld& World, const FVector& Center, const AActor* Ignored);
	bool Collect();
	void Issue(UWorld& World);

	TStrongObjectPtr<UTexture2D> Texture;
	TStrongObjectPtr<UTextureRenderTarget2D> Target;
	bool bTargetLoaded = false;
	TArray<float> Heights;
	FVector2D Origin = FVector2D::ZeroVector;
	bool bReady = false;
	/** The map in work (the callbacks of its async lines write into it), its center and its low corner. */
	struct FBuild;
	TSharedPtr<FBuild> Building;
	FVector PendingCenter = FVector::ZeroVector;
	TWeakObjectPtr<const AActor> PendingIgnored;
	int32 NextLine = 0;
	FVector2D PendingOrigin = FVector2D::ZeroVector;
	FVector MapCenter = FVector(TNumericLimits<double>::Max());
	double NextRefresh = 0.0;
	int32 Builds = 0;
	int32 LastHits = 0;
	double LastBuildMs = 0.0;
	double StartTime = 0.0;
};
