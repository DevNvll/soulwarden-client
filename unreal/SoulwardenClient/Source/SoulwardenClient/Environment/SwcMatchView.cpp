#include "Environment/SwcMatchView.h"

#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "ContentStreaming.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Environment/SwcEnvironmentSubsystem.h"
#include "Environment/SwcLocalLights.h"
#include "Environment/SwcMeterProbe.h"
#include "Environment/SwcWorldTimeSubsystem.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "HighResScreenshot.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Net/SwcSpace.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SwcBackendSubsystem.h"
#include "UnrealClient.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

namespace
{

/** The aspect of the retail pictures (1280 x 720): the camera manager keeps the vertical field of view at each aspect. */
constexpr double PictureAspect = 16.0 / 9.0;
/** The character is at the feet when the server place and the pawn are this near (m, horizontal). */
constexpr double FeetToleranceMeters = 2.0;
/** The streaming must be quiet this long before the view is in place (s). */
constexpr double QuietSeconds = 1.0;
/** The longest wait for the hour of the picture after the settle time (s). */
constexpr double HourWaitSeconds = 120.0;

FVector Vector3Of(const TSharedPtr<FJsonObject>& Object, const TCHAR* X, const TCHAR* Y, const TCHAR* Z)
{
	return Object.IsValid() ? FVector(Object->GetNumberField(X), Object->GetNumberField(Y), Object->GetNumberField(Z)) : FVector::ZeroVector;
}

/** @return The Unreal yaw (degrees) of an engine yaw counterclockwise from north: north is engine +Y, Unreal -Y. */
double UnrealYawOfEngineYaw(double EngineYawDegrees)
{
	const double Radians = FMath::DegreesToRadians(EngineYawDegrees);
	return FMath::RadiansToDegrees(FMath::Atan2(-FMath::Cos(Radians), -FMath::Sin(Radians)));
}

FVector UnrealOfEngine(const FVector& Engine)
{
	return SwcEngineToUnreal(swc::net::WorldPosition{static_cast<float>(Engine.X), static_cast<float>(Engine.Y), static_cast<float>(Engine.Z)});
}

}

FSwcViewOverride& FSwcViewOverride::Get()
{
	static FSwcViewOverride Override;
	return Override;
}

void FSwcViewOverride::Apply(FMinimalViewInfo& View)
{
	const FSwcViewOverride& Override = Get();
	if (!Override.bActive)
	{
		return;
	}
	View.Location = Override.Location;
	View.Rotation = Override.Rotation;
	View.FOV = static_cast<float>(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Override.VerticalFovDegrees) * 0.5) * PictureAspect)));
	View.AspectRatio = static_cast<float>(PictureAspect);
	View.bConstrainAspectRatio = false;
	View.AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV;
}

namespace
{
/**
 * The fixed eye of a check (AST-269; development runs): "swc.EyeView hold" keeps the place and the rotation of the camera of this
 * frame (the override of a matched view), "swc.EyeView at X Y Z HEADING PITCH" sets an eye at a game place (engine m),
 * "swc.EyeView turn DYAW DPITCH" turns the view around that fixed eye (degrees; a pan with no
 * change of the eye place, unlike the orbit of the camera of the player), "swc.EyeView off" gives the camera back.
 */
FAutoConsoleCommandWithWorldAndArgs GEyeViewCommand(TEXT("swc.EyeView"),
	TEXT("hold | at X Y Z HEADING PITCH | turn DYAW DPITCH | off: a fixed camera eye for the checks (the override of the matched views; development runs)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		FSwcViewOverride& Override = FSwcViewOverride::Get();
		const FString Verb = Args.Num() > 0 ? Args[0] : FString();
		if (Verb == TEXT("off"))
		{
			Override.bActive = false;
		}
		else if (Verb == TEXT("hold"))
		{
			const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
			const APlayerCameraManager* Camera = Controller != nullptr ? Controller->PlayerCameraManager.Get() : nullptr;
			if (Camera == nullptr)
			{
				UE_LOG(LogTemp, Warning, TEXT("swc.EyeView: no camera"));
				return;
			}
			Override.Location = Camera->GetCameraLocation();
			Override.Rotation = Camera->GetCameraRotation();
			Override.Rotation.Roll = 0.0;
			// The vertical field of view of the horizontal one at 16:9 (the rule of the camera manager).
			Override.VerticalFovDegrees = static_cast<float>(FMath::RadiansToDegrees(
				2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Camera->GetFOVAngle()) * 0.5) * 9.0 / 16.0)));
			Override.bActive = true;
		}
		else if (Verb == TEXT("at") && Args.Num() >= 6)
		{
			// An eye at a game place (engine m) with a compass heading (degrees clockwise from north) and a pitch (positive up).
			Override.Location = UnrealOfEngine(FVector(FCString::Atod(*Args[1]), FCString::Atod(*Args[2]), FCString::Atod(*Args[3])));
			Override.Rotation = FRotator(FCString::Atod(*Args[5]), FCString::Atod(*Args[4]) - 90.0, 0.0);
			Override.VerticalFovDegrees = 57.0f;
			Override.bActive = true;
		}
		else if (Verb == TEXT("turn") && Args.Num() >= 3 && Override.bActive)
		{
			Override.Rotation.Yaw += FCString::Atod(*Args[1]);
			Override.Rotation.Pitch = FMath::Clamp(Override.Rotation.Pitch + FCString::Atod(*Args[2]), -89.0, 89.0);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("swc.EyeView: hold | at X Y Z HEADING PITCH | turn DYAW DPITCH | off"));
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("swc.EyeView %s: %s, eye %s, rotation %s, vertical fov %.1f"), *Verb, Override.bActive ? TEXT("on") : TEXT("off"),
			*(Override.Location / 100.0).ToString(), *Override.Rotation.ToString(), Override.VerticalFovDegrees);
	}));
} // namespace

void USwcMatchViewSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Plan;
	if (FParse::Value(FCommandLine::Get(), TEXT("SwcMatchView="), Plan))
	{
		bEnabled = LoadPlan(Plan);
	}
}

bool USwcMatchViewSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USwcMatchViewSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USwcMatchViewSubsystem, STATGROUP_Tickables);
}

bool USwcMatchViewSubsystem::LoadPlan(const FString& Path)
{
	FString Text;
	TSharedPtr<FJsonObject> Plan;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Plan) || !Plan.IsValid())
	{
		UE_LOG(LogSwcEnvironment, Error, TEXT("matchview: the plan %s does not read"), *Path);
		return false;
	}
	SettleSeconds = Plan->HasField(TEXT("settle")) ? Plan->GetNumberField(TEXT("settle")) : SettleSeconds;
	Width = Plan->HasField(TEXT("width")) ? static_cast<int32>(Plan->GetNumberField(TEXT("width"))) : Width;
	Height = Plan->HasField(TEXT("height")) ? static_cast<int32>(Plan->GetNumberField(TEXT("height"))) : Height;
	for (const TSharedPtr<FJsonValue>& Value : Plan->GetArrayField(TEXT("views")))
	{
		const TSharedPtr<FJsonObject> Entry = Value->AsObject();
		FString ViewText;
		TSharedPtr<FJsonObject> Source;
		if (!Entry.IsValid() || !FFileHelper::LoadFileToString(ViewText, *Entry->GetStringField(TEXT("json")))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ViewText), Source) || !Source.IsValid())
		{
			UE_LOG(LogSwcEnvironment, Error, TEXT("matchview: a view of the plan does not read"));
			continue;
		}
		FView View;
		View.Name = Entry->GetStringField(TEXT("name"));
		View.Out = Entry->GetStringField(TEXT("out"));
		const TSharedPtr<FJsonObject> Feet = Source->GetObjectField(TEXT("feet"));
		const TSharedPtr<FJsonObject> Camera = Source->GetObjectField(TEXT("camera"));
		View.Feet = Vector3Of(Feet, TEXT("x"), TEXT("y"), TEXT("z"));
		View.FeetYaw = Feet->GetNumberField(TEXT("yaw_deg_ccw_from_north"));
		View.CameraPlace = Vector3Of(Camera, TEXT("x"), TEXT("y"), TEXT("z"));
		const TArray<TSharedPtr<FJsonValue>>& Forward = Camera->GetArrayField(TEXT("forward_vector"));
		if (Forward.Num() == 3)
		{
			View.Forward = FVector(Forward[0]->AsNumber(), Forward[1]->AsNumber(), Forward[2]->AsNumber());
		}
		View.Roll = Camera->HasField(TEXT("roll_deg")) ? Camera->GetNumberField(TEXT("roll_deg")) : 0.0;
		View.VerticalFov = Camera->GetObjectField(TEXT("fov"))->GetNumberField(TEXT("vertical_deg"));
		View.TargetHour = Source->GetObjectField(TEXT("world_hour"))->GetNumberField(TEXT("client_hour_at_picture"));
		const TArray<TSharedPtr<FJsonValue>>* Commands = nullptr;
		if (Entry->TryGetArrayField(TEXT("commands"), Commands))
		{
			for (const TSharedPtr<FJsonValue>& Command : *Commands)
			{
				View.Commands.Add(Command->AsString());
			}
		}
		Views.Add(MoveTemp(View));
	}
	UE_LOG(LogSwcEnvironment, Log, TEXT("matchview: plan %s: %d view(s), %dx%d, settle %.1f s"), *Path, Views.Num(), Width, Height, SettleSeconds);
	return !Views.IsEmpty();
}

void USwcMatchViewSubsystem::Begin(int32 Index)
{
	Current = Index;
	// Each view starts from the values before the variants: the console variables of the earlier variants go back first (the
	// time of the travel and the settle lets the exposure follow).
	for (const TPair<FString, FString>& Saved : VariantBase)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Saved.Key))
		{
			Variable->Set(*Saved.Value, ECVF_SetByConsole);
			UE_LOG(LogSwcEnvironment, Log, TEXT("matchview: %s back to %s"), *Saved.Key, *Saved.Value);
		}
	}
	VariantBase.Reset();
	StepSince = FPlatformTime::Seconds();
	QuietSince = -1.0;
	PlacedSince = -1.0;
	NextStatus = 0.0;
	if (!Views.IsValidIndex(Index))
	{
		Step = EStep::Done;
		FSwcViewOverride::Get().bActive = false;
		UE_LOG(LogSwcEnvironment, Log, TEXT("matchview: done"));
		FPlatformMisc::RequestExit(false);
		return;
	}
	const FView& View = Views[Index];
	swc::net::RepClient::OwnEntity Own;
	const UGameInstance* GameInstance = GetWorld()->GetGameInstance();
	const USwcBackendSubsystem* Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USwcBackendSubsystem>() : nullptr;
	RecordRevisionAtReady = Backend != nullptr && Backend->GetLocalPlayer(Own) ? Own.timeRevision : 0;
	bRecordSeen = false;
	Step = EStep::Ready;
	UE_LOG(LogSwcEnvironment, Log, TEXT("matchview: view %d %s: ready (feet %.4f %.4f %.4f, yaw %.2f; camera %.4f %.4f %.4f; target hour %.4f)"), Index + 1, *View.Name,
		View.Feet.X, View.Feet.Y, View.Feet.Z, View.FeetYaw, View.CameraPlace.X, View.CameraPlace.Y, View.CameraPlace.Z, View.TargetHour);
}

void USwcMatchViewSubsystem::HideOverlays()
{
	if (GEngine != nullptr)
	{
		GEngine->bEnableOnScreenDebugMessages = false;
		GEngine->ClearOnScreenDebugMessages();
	}
	if (APlayerController* Controller = GetWorld()->GetFirstPlayerController(); Controller != nullptr && Controller->GetHUD() != nullptr)
	{
		Controller->GetHUD()->bShowHUD = false;
	}
}

bool USwcMatchViewSubsystem::StreamingDone() const
{
	if (GetNumAsyncPackages() > 0)
	{
		return false;
	}
	const UWorldPartitionSubsystem* Partition = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
	return Partition == nullptr || Partition->IsStreamingCompleted();
}

void USwcMatchViewSubsystem::Shoot()
{
	const FView& View = Views[Current];
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(View.Out), true);
	IFileManager::Get().Delete(*View.Out, false, true, true);
	FHighResScreenshotConfig& Config = GetHighResScreenshotConfig();
	Config.SetResolution(Width, Height, 1.0f);
	Config.SetFilename(View.Out);
	Config.bMaskEnabled = false;
	Config.bCaptureHDR = false;
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && GEngine->GameViewport->Viewport != nullptr)
	{
		GEngine->GameViewport->Viewport->TakeHighResScreenShot();
	}
	Step = EStep::Shot;
	StepSince = FPlatformTime::Seconds();
	// The meter of the source on the frame of the shot next to the exposure of Unreal (stage E5, the light level).
	if (const USwcEnvironmentSubsystem* Environment = GetWorld()->GetSubsystem<USwcEnvironmentSubsystem>())
	{
		const FVector Rule = Environment->GetExposureRule();
		SwcMeterProbe::Request(View.Name, Rule.X, Rule.Y, Rule.Z);
	}
	if (const USwcLocalLightSubsystem* Lights = GetWorld()->GetSubsystem<USwcLocalLightSubsystem>())
	{
		const FSwcLocalLightStats Stats = Lights->GetStats();
		UE_LOG(LogSwcEnvironment, Log, TEXT("match view %s: local lights at the shot: %d active (%d point, %d spot, %d rect; %d bound; %d with a cookie), %d in the view; shadows %d (%d in the view)"),
			*View.Name, Stats.Active, Stats.Points, Stats.Spots, Stats.Rects, Stats.Bound, Stats.Cookies, Stats.InView, Stats.Shadows, Stats.ShadowsInView);
	}
}

void USwcMatchViewSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bEnabled || Step == EStep::Done)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World->GetGameInstance();
	const USwcBackendSubsystem* Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USwcBackendSubsystem>() : nullptr;
	const USwcWorldTimeSubsystem* Clock = World->GetSubsystem<USwcWorldTimeSubsystem>();
	swc::net::RepClient::OwnEntity Own;
	if (Backend == nullptr || Clock == nullptr || !Backend->GetLocalPlayer(Own))
	{
		return;
	}
	if (Step == EStep::Idle)
	{
		if (Clock->HasWorldTime())
		{
			Begin(0);
		}
		return;
	}
	const FView& View = Views[Current];
	APlayerController* Controller = World->GetFirstPlayerController();
	APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	if (Step == EStep::Ready)
	{
		const FVector Server(Own.position.x, Own.position.y, Own.position.z);
		const FVector Target = UnrealOfEngine(View.Feet);
		const bool bServer = FVector::Dist2D(Server, View.Feet) <= FeetToleranceMeters;
		const bool bPawn = Pawn != nullptr && FVector::Dist2D(Pawn->GetActorLocation(), Target) <= FeetToleranceMeters * 100.0;
		const bool bTimeout = Now - StepSince > PlaceTimeoutSeconds;
		if (Now >= NextStatus)
		{
			NextStatus = Now + 5.0;
			const UWorldPartitionSubsystem* Partition = World->GetSubsystem<UWorldPartitionSubsystem>();
			// The place and the class of the pawn (the check of 2026-10-29: "pawn 0" for the whole wait, but the pawn at the feet after the timeout).
			const swc::net::WorldPosition PawnAt = SwcUnrealToEngine(Pawn != nullptr ? Pawn->GetActorLocation() : FVector::ZeroVector);
			UE_LOG(LogSwcEnvironment, Log, TEXT("matchview: view %d %s: waiting %.0f s (server at the feet %d, pawn %d, partition streaming done %d, %d package(s) loading; pawn %s at %.3f %.3f %.3f, %.2f m from the feet)"),
				Current + 1, *View.Name, Now - StepSince, bServer ? 1 : 0, bPawn ? 1 : 0, Partition == nullptr || Partition->IsStreamingCompleted() ? 1 : 0, GetNumAsyncPackages(),
				*GetNameSafe(Pawn), PawnAt.x, PawnAt.y, PawnAt.z, Pawn != nullptr ? FVector::Dist2D(Pawn->GetActorLocation(), Target) / 100.0 : -1.0);
		}
		if (!(bServer && bPawn) && !bTimeout)
		{
			PlacedSince = -1.0;
			return;
		}
		// The body yaw of the picture, and the camera of the picture (the override of the camera manager).
		const FRotator Body(0.0, UnrealYawOfEngineYaw(View.FeetYaw), 0.0);
		if (Pawn != nullptr)
		{
			Pawn->SetActorRotation(Body);
		}
		if (Controller != nullptr)
		{
			Controller->SetControlRotation(Body);
		}
		FSwcViewOverride& Override = FSwcViewOverride::Get();
		Override.Location = UnrealOfEngine(View.CameraPlace);
		FRotator Rotation = FVector(View.Forward.X, -View.Forward.Y, View.Forward.Z).Rotation();
		Rotation.Roll = 0.0;   // the roll of the references is 0 (a nonzero one is logged)
		Override.Rotation = Rotation;
		Override.VerticalFovDegrees = static_cast<float>(View.VerticalFov);
		Override.bActive = true;
		HideOverlays();
		// The streaming: the cells of the partition loaded and no package loading for QuietSeconds; a world that loads in the background
		// all the time goes on after StreamingWaitSeconds at the feet (logged with the count).
		if (PlacedSince < 0.0)
		{
			PlacedSince = Now;
		}
		const bool bStreamingWait = Now - PlacedSince > StreamingWaitSeconds;
		if (!StreamingDone())
		{
			QuietSince = -1.0;
			if (!bTimeout && !bStreamingWait)
			{
				return;
			}
		}
		else if (QuietSince < 0.0)
		{
			QuietSince = Now;
		}
		if (QuietSince >= 0.0 && Now - QuietSince < QuietSeconds && !bTimeout && !bStreamingWait)
		{
			return;
		}
		Step = EStep::InPlace;
		StepSince = Now;
		for (const FString& Command : View.Commands)
		{
			// The value of a console variable before the first change of the variants (the next view sets it back).
			FString Name = Command.TrimStartAndEnd();
			Name.Split(TEXT(" "), &Name, nullptr);
			if (const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name); Variable != nullptr && !VariantBase.Contains(Name))
			{
				VariantBase.Add(Name, Variable->GetString());
			}
			const bool bDone = GEngine != nullptr && GEngine->Exec(GetWorld(), *Command);
			UE_LOG(LogSwcEnvironment, Log, TEXT("matchview: view %d %s: command \"%s\"%s"), Current + 1, *View.Name, *Command, bDone ? TEXT("") : TEXT(" (not run)"));
		}
		const swc::net::WorldPosition PawnPlace = SwcUnrealToEngine(Pawn != nullptr ? Pawn->GetActorLocation() : FVector::ZeroVector);
		UE_LOG(LogSwcEnvironment, Log, TEXT("matchview: view %d %s: in place%s (server %.3f %.3f %.3f; pawn %.3f %.3f %.3f, yaw %.2f; camera pitch %.2f yaw %.2f, roll of the reference %.3f; %s, %d package(s) loading)"),
			Current + 1, *View.Name, bTimeout ? TEXT(" after the timeout") : TEXT(""), Own.position.x, Own.position.y, Own.position.z, PawnPlace.x, PawnPlace.y, PawnPlace.z,
			Pawn != nullptr ? Pawn->GetActorRotation().Yaw : 0.0, Rotation.Pitch, Rotation.Yaw, View.Roll,
			StreamingDone() ? TEXT("streaming done") : TEXT("streaming not done after the wait"), GetNumAsyncPackages());
		return;
	}
	if (Step == EStep::InPlace)
	{
		if (Own.timeRevision == RecordRevisionAtReady && Now - StepSince < PlaceTimeoutSeconds)
		{
			return;   // the tool sets the hour after "in place": the new record comes then
		}
		IStreamingManager::Get().StreamAllResources(5.0f);
		Step = EStep::Settle;
		StepSince = FPlatformTime::Seconds();
		return;
	}
	if (Step == EStep::Settle)
	{
		HideOverlays();
		if (Now - StepSince < SettleSeconds)
		{
			return;
		}
		// The hour of the picture on the client clock: the time left to it on the cycle of 24 hours (passed: more than 12 hours).
		const double Remaining = FMath::Fmod(View.TargetHour - Clock->GetHour() + 48.0, 24.0);
		const bool bReached = Remaining <= 1.0 / 3600.0 || Remaining > 12.0;
		if (!bReached && Now - StepSince < SettleSeconds + HourWaitSeconds)
		{
			return;
		}
		UE_LOG(LogSwcEnvironment, Log, TEXT("matchview: view %d %s: the picture at the client hour %.4f (target %.4f, %hs)"), Current + 1, *View.Name, Clock->GetHour(),
			View.TargetHour, swc::net::clock_text(Clock->GetGameSecond()).c_str());
		Shoot();
		return;
	}
	if (Step == EStep::Shot)
	{
		const bool bWritten = IFileManager::Get().FileSize(*View.Out) > 0;
		if (!bWritten && Now - StepSince < 30.0)
		{
			return;
		}
		UE_LOG(LogSwcEnvironment, Log, TEXT("matchview: view %d %s: picture %s (%s; %dx%d)"), Current + 1, *View.Name, *View.Out,
			bWritten ? TEXT("written") : TEXT("NOT WRITTEN"), Width, Height);
		Begin(Current + 1);
	}
}
