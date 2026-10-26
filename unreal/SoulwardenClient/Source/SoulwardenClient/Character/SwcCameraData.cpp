#include "Character/SwcCameraData.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
FString CameraFile()
{
	FString Content = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_CONTENT"));
	if (Content.IsEmpty())
	{
		FString Work = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_WORK"));
		if (Work.IsEmpty())
		{
			Work = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT("Games/soulwarden-client"));
		}
		Content = FPaths::Combine(Work, TEXT("content"));
	}
	return FPaths::Combine(Content, TEXT("player/camera.json"));
}

void Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, float& Out)
{
	double Value = 0.0;
	if (Object.IsValid() && Object->TryGetNumberField(Name, Value))
	{
		Out = static_cast<float>(Value);
	}
}

void Flag(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, bool& Out)
{
	bool Value = false;
	if (Object.IsValid() && Object->TryGetBoolField(Name, Value))
	{
		Out = Value;
	}
}

void Numbers(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, TArray<float>& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (Object.IsValid() && Object->TryGetArrayField(Name, Values))
	{
		Out.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			Out.Add(static_cast<float>(Value->AsNumber()));
		}
	}
}
} // namespace

bool FSwcCameraData::Load(FString& OutReport)
{
	Default.Name = TEXT("MidCamera");
	FString Text;
	TSharedPtr<FJsonObject> Document;
	if (!FFileHelper::LoadFileToString(Text, *CameraFile()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Document)
		|| !Document.IsValid())
	{
		OutReport = TEXT("no player/camera.json (tools/convert-player.sh camera): the camera uses the values of MidCamera");
		return false;
	}
	const TSharedPtr<FJsonObject>* Settings = nullptr;
	if (Document->TryGetObjectField(TEXT("settings"), Settings))
	{
		Number(*Settings, TEXT("zoomAmount"), ZoomAmount);
		Number(*Settings, TEXT("zoomLag"), ZoomLag);
		Number(*Settings, TEXT("collisionSphereCastRadius"), CastRadius);
		Number(*Settings, TEXT("focusPointOffsetCollisionSphereCastRadius"), FocusCastRadius);
		Number(*Settings, TEXT("focusPointOffsetCastExtension"), CastExtension);
		Number(*Settings, TEXT("offCollisionSmoothTime"), OffCollisionSmoothTime);
		Numbers(*Settings, TEXT("zSmoothingStrengthCurveDefault"), ZCurveDefault);
		Numbers(*Settings, TEXT("zSmoothingStrengthCurveAirborne"), ZCurveAirborne);
		Numbers(*Settings, TEXT("zSmoothingStrengthCurveAiming"), ZCurveAiming);
		Number(*Settings, TEXT("blendTimeOutOfOptionalZSmoothingCurve"), ZCurveBlendOut);
		Number(*Settings, TEXT("zSmoothingUpperZLimitDistanceGradient"), ZUpperGradient);
		Numbers(*Settings, TEXT("zSmoothingLowerZLimitFOVBasedScalar"), ZFovScalar);
		Number(*Settings, TEXT("lookAtPositionSmoothingTime"), LookAtSmoothingTime);
	}
	const TSharedPtr<FJsonObject>* StateObjects = nullptr;
	if (Document->TryGetObjectField(TEXT("states"), StateObjects))
	{
		for (const auto& Pair : (*StateObjects)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			const TSharedPtr<FJsonObject> Params = Info.IsValid() ? Info->GetObjectField(TEXT("params")) : nullptr;
			FSwcCameraState State;
			State.Name = FString(Pair.Key.ToView());
			Number(Params, TEXT("Distance"), State.Distance);
			TArray<float> Offset;
			Numbers(Params, TEXT("Offset"), Offset);
			if (Offset.Num() == 3)
			{
				State.Offset = FVector3f(Offset[0], Offset[1], Offset[2]);
			}
			Number(Params, TEXT("FOV"), State.Fov);
			Flag(Params, TEXT("UseUserDefinedFov"), State.bUserFov);
			Number(Params, TEXT("PitchDown"), State.PitchDown);
			Number(Params, TEXT("PitchUp"), State.PitchUp);
			Number(Params, TEXT("MinZoom"), State.MinZoom);
			Number(Params, TEXT("MaxZoom"), State.MaxZoom);
			Number(Params, TEXT("DistanceDampening"), State.DistanceDampening);
			Number(Params, TEXT("RotationDampening"), State.RotationDampening);
			Number(Params, TEXT("HorizontalSensitivity"), State.HorizontalSensitivity);
			Number(Params, TEXT("VerticalSensitivity"), State.VerticalSensitivity);
			Flag(Params, TEXT("IsADS"), State.bAds);
			Flag(Params, TEXT("AllowFreeLook"), State.bAllowFreeLook);
			Flag(Params, TEXT("CameraCollides"), State.bCollides);
			bool bNoZSmoothing = false;
			Flag(Params, TEXT("DisableZSmoothing"), bNoZSmoothing);
			State.bZSmoothing = !bNoZSmoothing;
			Number(Params, TEXT("ZoomLagOverride"), State.ZoomLag);
			Number(Params, TEXT("NearPlane"), State.NearPlane);
			const TSharedPtr<FJsonObject>* Transitions = nullptr;
			if (Info.IsValid() && Info->TryGetObjectField(TEXT("transitions"), Transitions))
			{
				for (const auto& Transition : (*Transitions)->Values)
				{
					State.Transitions.Add(FString(Transition.Key.ToView()), static_cast<float>(Transition.Value->AsNumber()));
				}
			}
			States.Add(State.Name, MoveTemp(State));
		}
	}
	const TSharedPtr<FJsonObject>* ActivationObjects = nullptr;
	if (Document->TryGetObjectField(TEXT("activations"), ActivationObjects))
	{
		for (const auto& Pair : (*ActivationObjects)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			if (Info.IsValid())
			{
				Activations.Add(FString(Pair.Key.ToView()),
					TPair<FString, float>(Info->GetStringField(TEXT("camera")), static_cast<float>(Info->GetNumberField(TEXT("smoothTime")))));
			}
		}
	}
	if (const FSwcCameraState* Mid = States.Find(TEXT("MidCamera")))
	{
		Default = *Mid;
	}
	OutReport = FString::Printf(TEXT("camera: %d states, %d entries of the camera action list"), States.Num(), Activations.Num());
	return States.Contains(TEXT("MidCamera"));
}

const FSwcCameraState& FSwcCameraData::State(const FString& Name) const
{
	const FSwcCameraState* Found = States.Find(Name);
	return Found != nullptr ? *Found : Default;
}

bool FSwcCameraData::Activation(const FString& Entry, FString& OutCamera, float& OutSmoothTime) const
{
	const TPair<FString, float>* Found = Activations.Find(Entry);
	if (Found == nullptr)
	{
		return false;
	}
	OutCamera = Found->Key;
	OutSmoothTime = Found->Value;
	return true;
}
