#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"

#include "SwcRibbons.generated.h"

class UMaterialInterface;

/** One ribbon emitter of an effect (SWC_CONTENT/particles/ribbons.json, the import of tools/import-particles.sh). Units: cm, s, linear. */
struct FSwcRibbonSpec
{
	FString Name;
	FString Material;
	float Delay = 0.0f;
	/** The time of the emission after the delay (0: until the effect stops). */
	float EmitterLife = 0.0f;
	/** The points each second (Count / ParticleLifeTime of the source). */
	float Rate = 0.0f;
	FVector2f Life = FVector2f(1.0f, 1.0f);
	FVector2f Width = FVector2f(10.0f, 10.0f);
	TArray<float> WidthCurve;
	FLinearColor Color = FLinearColor::White;
	TArray<FVector3f> ColorCurve;
	FVector2f Alpha = FVector2f(1.0f, 1.0f);
	TArray<float> AlphaCurve;
	/** TextureMapping PerStream: the texture over the whole ribbon; else on each segment. TextureFrequency: the repeats. */
	bool bStreamUV = false;
	float Frequency = 1.0f;
	FVector Offset = FVector::ZeroVector;
};

namespace SwcRibbons
{
/** @return The ribbons of an effect key, or null. */
SOULWARDENCLIENT_API const TArray<FSwcRibbonSpec>* Of(const FString& Effect);
} // namespace SwcRibbons

/**
 * AST-230 (docs/EFFECTS.md "The ribbons"): a trail emitter of the source (EmitterShape TRAIL). Its particles stay in the world where the
 * emitter is when it emits them (the stateless emitters of Niagara cannot leave particles behind), and connect into one strip that faces
 * the camera: the width, the color, and the alpha of each point follow the curves of its age. The component follows its parent (the
 * effect component); it emits from its delay for its emitter life (or until Stop) and ends when its last point dies.
 */
UCLASS(ClassGroup = (Soulwarden))
class SOULWARDENCLIENT_API USwcRibbonComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	USwcRibbonComponent();

	/** The data of the ribbon, the tint and the alpha scale of the effect, the scale of the width, the time scale of the effect. */
	void Setup(const FSwcRibbonSpec& InSpec, const FLinearColor& InTint, float InAlphaScale, float InWidthScale, float InTimeScale);
	/** Stops the emission; bKill also removes the points. */
	void Stop(bool bKill);

	struct FRenderPoint
	{
		FVector Position = FVector::ZeroVector;
		float HalfWidth = 0.0f;
		float U = 0.0f;
		FColor Color = FColor::White;
	};

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual int32 GetNumMaterials() const override { return 1; }
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;

protected:
	virtual void SendRenderDynamicData_Concurrent() override;

private:
	struct FPoint
	{
		FVector Position = FVector::ZeroVector;
		float Age = 0.0f;
		float Life = 1.0f;
		float Width = 0.0f;
		float Alpha = 1.0f;
	};
	void BuildRenderPoints();

	FSwcRibbonSpec Spec;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> RibbonMaterial;
	FLinearColor Tint = FLinearColor::White;
	float AlphaScale = 1.0f;
	float WidthScale = 1.0f;
	float TimeScale = 1.0f;
	TArray<FPoint> Points;
	TArray<FRenderPoint> RenderPoints;
	float Clock = 0.0f;
	float Pending = 0.0f;
	FVector LastPosition = FVector::ZeroVector;
	bool bHasLast = false;
	bool bStopped = false;
	FBox PointBounds = FBox(ForceInit);
};
