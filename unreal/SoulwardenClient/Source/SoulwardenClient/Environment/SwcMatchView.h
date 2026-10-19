#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SwcMatchView.generated.h"

struct FMinimalViewInfo;

/**
 * The camera override of a matched view (docs/ENVIRONMENT.md, "The matched views"): when it is active, the camera manager of the
 * player (one hook line in ASwcCameraManager::UpdateViewTarget) uses this exact place, rotation, and vertical field of view in
 * place of its own camera.
 */
struct SOULWARDENCLIENT_API FSwcViewOverride
{
	bool bActive = false;
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float VerticalFovDegrees = 57.0f;

	static FSwcViewOverride& Get();
	/** Sets the view of the camera manager when the override is active (the horizontal field of view at 16:9 for the vertical one, the
	 *  vertical one kept at each aspect: the rule of the camera manager). */
	static void Apply(FMinimalViewInfo& View);
};

/**
 * The matched views of the fidelity checks (tools/match-view.sh; -SwcMatchView=PLAN.json): for each view of a retail reference set
 * (the JSON of a picture: the feet, the camera, the field of view, the measured world hour), the client
 *   1. logs "matchview: view K NAME: ready" (the tool then moves the character to the feet with the travel route),
 *   2. waits for the character at the feet, turns the body to the yaw of the feet, sets the camera override, hides the HUD and the
 *      debug text, and waits for the level streaming and the asynchronous loads to end, then logs "in place" (the tool then sets
 *      the world hour of the backend a lead time before the hour of the picture),
 *   3. waits for the new time record, at least the settle time, and the hour of the picture on its own world clock,
 *   4. takes a picture of PlanWidth x PlanHeight (a high resolution screenshot: the scene with no user interface), and logs
 *      "picture" with the client hour, the target hour, and the place.
 * After the last view it logs "matchview: done".
 */
UCLASS()
class SOULWARDENCLIENT_API USwcMatchViewSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	struct FView
	{
		FString Name;
		FString Out;
		/** Engine m. */
		FVector Feet = FVector::ZeroVector;
		/** Degrees counterclockwise from north (engine). */
		double FeetYaw = 0.0;
		FVector CameraPlace = FVector::ZeroVector;
		/** The forward vector of the camera (engine). */
		FVector Forward = FVector(0.0, 1.0, 0.0);
		double Roll = 0.0;
		double VerticalFov = 57.0;
		/** The hour of the retail picture (the measured hour of the client). */
		double TargetHour = 12.0;
		/** The console commands of a variant of the view (tools/match-view.sh --variant), run when the view is in place: a look
		 *  value to compare in one run. */
		TArray<FString> Commands;
	};
	enum class EStep : uint8 { Idle, Ready, InPlace, Settle, Shot, Done };

	bool LoadPlan(const FString& Path);
	void Begin(int32 Index);
	void HideOverlays();
	bool StreamingDone() const;
	void Shoot();

	TArray<FView> Views;
	/** The console variables that the variant commands changed, with their values before the first change. */
	TMap<FString, FString> VariantBase;
	int32 Current = -1;
	EStep Step = EStep::Idle;
	double StepSince = 0.0;
	double QuietSince = -1.0;
	double PlacedSince = -1.0;
	double NextStatus = 0.0;
	/** The longest wait for the streaming at the feet (s): a world can load in the background all the time. */
	double StreamingWaitSeconds = 25.0;
	uint32 RecordRevisionAtReady = 0;
	bool bRecordSeen = false;
	double SettleSeconds = 4.0;
	double PlaceTimeoutSeconds = 90.0;
	int32 Width = 1280;
	int32 Height = 720;
	bool bPlanLoaded = false;
	bool bEnabled = false;
};
