// The particle systems of the importer (AST-210, docs/PARTICLES.md "The systems"): one Niagara system for each effect root,
// one stateless (lightweight) emitter for each source emitter of its tree. A stateless emitter is data only: a fixed set of
// modules with distributions and no script graph. The importer (Scripts/particle_systems.py) gives the record of
// SWC_CONTENT/particles/systems.json with the material of each emitter.

#include "SwcEditorLibrary.h"

#include "Dom/JsonObject.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Stateless/Modules/NiagaraStatelessModule_AccelerationForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_AddVelocity.h"
#include "Stateless/Modules/NiagaraStatelessModule_ApplyOwnerScaleToAttributes.h"
#include "Stateless/Modules/NiagaraStatelessModule_CameraOffset.h"
#include "Stateless/Modules/NiagaraStatelessModule_CurlNoiseForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_Drag.h"
#include "Stateless/Modules/NiagaraStatelessModule_GravityForce.h"
#include "Stateless/Modules/NiagaraStatelessModule_InitializeParticle.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleColor.h"
#include "Stateless/Modules/NiagaraStatelessModule_ScaleSpriteSize.h"
#include "Stateless/Modules/NiagaraStatelessModule_ShapeLocation.h"
#include "Stateless/Modules/NiagaraStatelessModule_SolveVelocitiesAndForces.h"
#include "Stateless/Modules/NiagaraStatelessModule_SpriteRotationRate.h"
#include "Stateless/Modules/NiagaraStatelessModule_SubUVAnimation.h"
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/NiagaraStatelessEmitterTemplate.h"

namespace
{
	TArray<double> Numbers(const TSharedPtr<FJsonObject>& Object, const FString& Field)
	{
		TArray<double> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				Out.Add(Value->AsNumber());
			}
		}
		return Out;
	}

	double Number(const TSharedPtr<FJsonObject>& Object, const FString& Field, double Default = 0.0)
	{
		double Value = Default;
		return Object.IsValid() && Object->TryGetNumberField(Field, Value) ? Value : Default;
	}

	TSharedPtr<FJsonObject> Child(const TSharedPtr<FJsonObject>& Object, const FString& Field)
	{
		const TSharedPtr<FJsonObject>* Value = nullptr;
		return Object.IsValid() && Object->TryGetObjectField(Field, Value) ? *Value : TSharedPtr<FJsonObject>();
	}

	/** A curve channel of equally spaced samples (t = i / (n - 1)), linear keys. */
	FRichCurve SampleCurve(const TArray<double>& Samples)
	{
		FRichCurve Curve;
		const int32 Count = Samples.Num();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Time = Count > 1 ? float(Index) / float(Count - 1) : 0.0f;
			const FKeyHandle Key = Curve.AddKey(Time, float(Samples[Index]));
			Curve.SetKeyInterpMode(Key, RCIM_Linear);
		}
		return Curve;
	}

	void SetRange(FNiagaraDistributionRangeFloat& Distribution, const TArray<double>& Range, double Scale = 1.0)
	{
		if (Range.Num() >= 2 && Range[0] != Range[1])
		{
			Distribution.InitRange(float(Range[0] * Scale), float(Range[1] * Scale));
		}
		else
		{
			Distribution.InitConstant(Range.Num() > 0 ? float(Range[0] * Scale) : 0.0f);
		}
	}

	FVector3f Vec3(const TArray<double>& Values)
	{
		return Values.Num() >= 3 ? FVector3f(float(Values[0]), float(Values[1]), float(Values[2])) : FVector3f::ZeroVector;
	}

	void SetRangeVector3(FNiagaraDistributionRangeVector3& Distribution, const FVector3f& Min, const FVector3f& Max)
	{
		if (Min.Equals(Max))
		{
			Distribution.InitConstant(Min);
			return;
		}
		Distribution.Mode = ENiagaraDistributionMode::NonUniformRange;
		Distribution.Min = Min;
		Distribution.Max = Max;
#if WITH_EDITORONLY_DATA
		Distribution.ChannelConstantsAndRanges = {Min.X, Min.Y, Min.Z, Max.X, Max.Y, Max.Z};
		Distribution.UpdateValuesFromDistribution();
#endif
	}

	/** @return A protected member of an object by its reflected property (the stateless emitter keeps its state protected). */
	template <typename T>
	T* Member(UObject* Object, FName Name)
	{
		const FProperty* Property = Object->GetClass()->FindPropertyByName(Name);
		check(Property != nullptr);
		return Property->ContainerPtrToValuePtr<T>(Object);
	}

	template <typename TModule>
	TModule* Module(UNiagaraStatelessEmitter* Emitter, bool bEnabled = true)
	{
		TModule* Found = Cast<TModule>(Emitter->GetModule(TModule::StaticClass()));
		if (Found != nullptr)
		{
			Found->SetIsModuleEnabled(bEnabled);
		}
		return Found;
	}

	/** Builds one stateless emitter. @return An error text, or empty. */
	FString BuildEmitter(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Spec, UNiagaraStatelessEmitterTemplate* Template)
	{
		const FString Name = Spec->GetStringField(TEXT("emitterName"));
		UNiagaraStatelessEmitter* Emitter = NewObject<UNiagaraStatelessEmitter>(System, FName(*Name), RF_Transactional);
		Emitter->SetUniqueEmitterName(Name);
		Emitter->SetEmitterTemplate(Template);

		// The spawn: a rate (Count / life) or a burst of Count at the start; the start delay; the loop.
		const TSharedPtr<FJsonObject> Spawn = Child(Spec, TEXT("spawn"));
		FNiagaraStatelessSpawnInfo& SpawnInfo = Emitter->AddSpawnInfo();
		const double Delay = Number(Spec, TEXT("delay"));
		if (Spawn->GetStringField(TEXT("type")) == TEXT("rate"))
		{
			SpawnInfo.Type = ENiagaraStatelessSpawnInfoType::Rate;
			SetRange(SpawnInfo.Rate, Numbers(Spawn, TEXT("rate")));
		}
		else
		{
			SpawnInfo.Type = ENiagaraStatelessSpawnInfoType::Burst;
			const TArray<double> Amount = Numbers(Spawn, TEXT("amount"));
			SpawnInfo.Amount.Min = Amount.Num() > 0 ? int32(Amount[0]) : 1;
			SpawnInfo.Amount.Max = Amount.Num() > 1 ? int32(Amount[1]) : SpawnInfo.Amount.Min;
			SpawnInfo.Amount.Mode = SpawnInfo.Amount.Min == SpawnInfo.Amount.Max ? ENiagaraDistributionMode::UniformConstant
			                                                                     : ENiagaraDistributionMode::UniformRange;
			SpawnInfo.SpawnTime = float(Delay);
		}
		FNiagaraEmitterStateData& State = *Member<FNiagaraEmitterStateData>(Emitter, TEXT("EmitterState"));
		const double Pulse = Number(Spec, TEXT("pulse"));
		const double Life = Number(Spec, TEXT("emitterLife"));
		State.LoopBehavior = ENiagaraLoopBehavior::Infinite;
		State.LoopDuration.InitConstant(float(Pulse > 0.0 ? Pulse : (Life > 0.0 ? Life : 1.0)));
		if (Delay > 0.0 && Spawn->GetStringField(TEXT("type")) == TEXT("rate"))
		{
			State.bLoopDelayEnabled = true;
			State.bDelayFirstLoopOnly = true;
			State.LoopDelay.InitConstant(float(Delay));
		}
		if (Life > 0.0 && Pulse <= 0.0)
		{
			State.LoopBehavior = ENiagaraLoopBehavior::Once;
		}
		// The distance bands: the emitter sleeps beyond its max (its particles finish) and wakes again within it.
		const TSharedPtr<FJsonObject> Distance = Child(Spec, TEXT("distance"));
		const double MinDistance = Number(Distance, TEXT("min"));
		const double MaxDistance = Number(Distance, TEXT("max"));
		State.bEnableDistanceCulling = MinDistance > 0.0 || MaxDistance > 0.0;
		State.bMinDistanceEnabled = MinDistance > 0.0;
		State.MinDistance = float(MinDistance);
		State.MinDistanceReaction = ENiagaraExecutionStateManagement::SleepAndLetParticlesFinish;
		State.bMaxDistanceEnabled = MaxDistance > 0.0;
		State.MaxDistance = float(MaxDistance);
		State.MaxDistanceReaction = ENiagaraExecutionStateManagement::SleepAndLetParticlesFinish;

		// The initial values: life, color (base x emission x glow, the alpha base), size, rotation.
		UNiagaraStatelessModule_InitializeParticle* Init = Module<UNiagaraStatelessModule_InitializeParticle>(Emitter);
		SetRange(Init->LifetimeDistribution, Numbers(Spec, TEXT("life")));
		const TSharedPtr<FJsonObject> Color = Child(Spec, TEXT("color"));
		const TArray<double> Base = Numbers(Color, TEXT("base"));
		const TArray<double> Glow = Numbers(Color, TEXT("glow"));
		// A lit emitter has its emission in its material (MI: Emissive); an unlit one carries it in the HDR particle color.
		bool bLit = false;
		Spec->TryGetBoolField(TEXT("lit"), bLit);
		const double Emissive = bLit ? 1.0 : Number(Color, TEXT("emissive"), 1.0);
		const double ColorRandom = Number(Color, TEXT("random"));
		const TArray<double> AlphaRange = Numbers(Child(Spec, TEXT("alpha")), TEXT("base"));
		FLinearColor High(float(Base[0] * Glow[0] * Emissive), float(Base[1] * Glow[1] * Emissive), float(Base[2] * Glow[2] * Emissive),
		                  float(AlphaRange.Num() > 1 ? AlphaRange[1] : 1.0));
		FLinearColor Low = High * float(1.0 - ColorRandom);
		Low.A = float(AlphaRange.Num() > 0 ? AlphaRange[0] : 1.0);
		Init->ColorDistribution.Mode = ENiagaraDistributionMode::NonUniformRange;
#if WITH_EDITORONLY_DATA
		Init->ColorDistribution.ChannelConstantsAndRanges = {Low.R, Low.G, Low.B, Low.A, High.R, High.G, High.B, High.A};
		Init->ColorDistribution.UpdateValuesFromDistribution();
#else
		Init->ColorDistribution.Values = {Low, High};
#endif
		const TSharedPtr<FJsonObject> Size = Child(Spec, TEXT("size"));
		const TArray<double> SizeX = Numbers(Size, TEXT("x"));
		const TArray<double> SizeY = Numbers(Size, TEXT("y"));
		Init->SpriteSizeDistribution.Mode = ENiagaraDistributionMode::NonUniformRange;
		Init->SpriteSizeDistribution.Min = FVector2f(float(SizeX[0]), float(SizeY[0]));
		Init->SpriteSizeDistribution.Max = FVector2f(float(SizeX[1]), float(SizeY[1]));
#if WITH_EDITORONLY_DATA
		Init->SpriteSizeDistribution.ChannelConstantsAndRanges = {float(SizeX[0]), float(SizeY[0]), float(SizeX[1]), float(SizeY[1])};
#endif
		SetRange(Init->SpriteRotationDistribution, Numbers(Spec, TEXT("rotation")));

		// The spawn place: the local offset and the random box or ellipsoid.
		UNiagaraStatelessModule_ShapeLocation* Shape = Module<UNiagaraStatelessModule_ShapeLocation>(Emitter);
		const FVector3f Extent = Vec3(Numbers(Spec, TEXT("randomOffset")));
		if (Number(Spec, TEXT("roundness")) >= 0.5 && !Extent.IsNearlyZero())
		{
			Shape->ShapePrimitive = ENSM_ShapePrimitive::Sphere;
			Shape->SphereRadius.InitRange(0.0f, 1.0f);
			Shape->ShapeScale.InitConstant(Extent);
		}
		else
		{
			Shape->ShapePrimitive = ENSM_ShapePrimitive::Box;
			Shape->BoxSize.InitConstant(Extent * 2.0f);
		}
		Shape->TransformOffset = FNiagaraDistributionPosition(Vec3(Numbers(Spec, TEXT("offset"))));

		// The motion: the speed in the cone about local +Z (or world up), the gravity and acceleration, the drag, the noise.
		UNiagaraStatelessModule_AddVelocity* Velocity = Module<UNiagaraStatelessModule_AddVelocity>(Emitter);
		Velocity->VelocityType = ENSM_VelocityType::InCone;
		SetRange(Velocity->ConeVelocityDistribution, Numbers(Spec, TEXT("speed")));
		Velocity->ConeAngle = FMath::Clamp(float(Number(Spec, TEXT("cone"))), 0.0f, 180.0f);
		Velocity->InnerCone = FMath::Clamp(float(Number(Spec, TEXT("innerCone"))), 0.0f, Velocity->ConeAngle);
		Velocity->ConeDirection.InitConstant(FVector3f::ZAxisVector);
		Velocity->CoordinateSpace = Spec->GetBoolField(TEXT("gravityUp")) ? ENiagaraCoordinateSpace::World : ENiagaraCoordinateSpace::Local;
		const TSharedPtr<FJsonObject> Gravity = Child(Spec, TEXT("gravity"));
		UNiagaraStatelessModule_GravityForce* GravityForce = Module<UNiagaraStatelessModule_GravityForce>(Emitter);
		SetRangeVector3(GravityForce->GravityDistribution, Vec3(Numbers(Gravity, TEXT("min"))), Vec3(Numbers(Gravity, TEXT("max"))));
		const TArray<double> Drag = Numbers(Spec, TEXT("drag"));
		if (Drag.Num() > 1 && Drag[1] > 0.0)
		{
			SetRange(Module<UNiagaraStatelessModule_Drag>(Emitter)->DragDistribution, Drag);
		}
		const double Turbulence = Number(Spec, TEXT("turbulence"));
		if (Turbulence > 0.0)
		{
			UNiagaraStatelessModule_CurlNoiseForce* Noise = Module<UNiagaraStatelessModule_CurlNoiseForce>(Emitter);
			Noise->NoiseStrength = float(Turbulence);
		}
		Module<UNiagaraStatelessModule_SolveVelocitiesAndForces>(Emitter);
		Module<UNiagaraStatelessModule_ApplyOwnerScaleToAttributes>(Emitter);
		const double CameraOffset = Number(Spec, TEXT("cameraOffset"));
		if (CameraOffset != 0.0)
		{
			Module<UNiagaraStatelessModule_CameraOffset>(Emitter)->CameraOffsetDistribution.InitConstant(float(CameraOffset));
		}

		// The curves by the particle age: the size (2 channels), the color and the alpha (4 channels).
		UNiagaraStatelessModule_ScaleSpriteSize* ScaleSize = Module<UNiagaraStatelessModule_ScaleSpriteSize>(Emitter);
		ScaleSize->ScaleDistribution.Mode = ENiagaraDistributionMode::NonUniformCurve;
#if WITH_EDITORONLY_DATA
		ScaleSize->ScaleDistribution.ChannelCurves = {SampleCurve(Numbers(Size, TEXT("curveX"))), SampleCurve(Numbers(Size, TEXT("curveY")))};
		ScaleSize->ScaleDistribution.UpdateValuesFromDistribution();
#endif
		UNiagaraStatelessModule_ScaleColor* ScaleColor = Module<UNiagaraStatelessModule_ScaleColor>(Emitter);
		ScaleColor->ScaleDistribution.Mode = ENiagaraDistributionMode::NonUniformCurve;
#if WITH_EDITORONLY_DATA
		{
			const TArray<TSharedPtr<FJsonValue>>& Keys = Color->GetArrayField(TEXT("curve"));
			TArray<double> R, G, B;
			for (const TSharedPtr<FJsonValue>& Key : Keys)
			{
				const TArray<TSharedPtr<FJsonValue>>& RGB = Key->AsArray();
				R.Add(RGB[0]->AsNumber());
				G.Add(RGB[1]->AsNumber());
				B.Add(RGB[2]->AsNumber());
			}
			ScaleColor->ScaleDistribution.ChannelCurves = {SampleCurve(R), SampleCurve(G), SampleCurve(B),
			                                               SampleCurve(Numbers(Child(Spec, TEXT("alpha")), TEXT("curve")))};
			ScaleColor->ScaleDistribution.UpdateValuesFromDistribution();
		}
#endif

		// The rotation rate and the facing.
		const TArray<double> Rate = Numbers(Spec, TEXT("rotationRate"));
		if (Rate.Num() > 1 && (Rate[0] != 0.0 || Rate[1] != 0.0))
		{
			SetRange(Module<UNiagaraStatelessModule_SpriteRotationRate>(Emitter)->RotationRateDistribution, Rate);
		}
		// CameraX (upright, a candle): the custom alignment of the stateless module drew nothing in the first check (2026-10-20); the
		// sprite faces the camera with its height up the screen [approximation: the source keeps it upright in the world].
		const FString Facing = Spec->GetStringField(TEXT("facing"));

		// The atlas: the frames from FirstTile, one cycle per life (fps 0) or a loop at fps, or a random variant.
		const TSharedPtr<FJsonObject> Atlas = Child(Spec, TEXT("atlas"));
		const TArray<double> Tiles = Numbers(Atlas, TEXT("tiles"));
		const int32 TileCount = int32(Tiles[0] * Tiles[1]);
		const int32 First = int32(Number(Atlas, TEXT("firstTile")));
		const int32 Frames = int32(Number(Atlas, TEXT("frames")));
		const int32 Variants = int32(Number(Atlas, TEXT("variants"), 1.0));
		if (TileCount > 1)
		{
			UNiagaraStatelessModule_SubUVAnimation* SubUV = Module<UNiagaraStatelessModule_SubUVAnimation>(Emitter);
			SubUV->NumFrames = TileCount;
			const int32 Last = Frames > 0 ? First + Frames - 1 : First + FMath::Max(Variants, 1) - 1;
			SubUV->bStartFrameRangeOverride_Enabled = true;
			SubUV->StartFrameRangeOverride = First;
			SubUV->bEndFrameRangeOverride_Enabled = true;
			SubUV->EndFrameRangeOverride = FMath::Min(Last, TileCount - 1);
			const double Fps = Number(Atlas, TEXT("fps"));
			if (Frames <= 0)
			{
				SubUV->AnimationMode = ENSMSubUVAnimation_Mode::Random;
				SubUV->RandomChangeInterval = 1000.0f;
			}
			else if (Fps > 0.0 && Atlas->GetStringField(TEXT("cycle")) != TEXT("Once"))
			{
				SubUV->AnimationMode = ENSMSubUVAnimation_Mode::InfiniteLoop;
				SubUV->LoopsPerSecond = float(Fps / double(Frames));
			}
			else
			{
				SubUV->AnimationMode = ENSMSubUVAnimation_Mode::Linear;
			}
		}

		// The renderer: the sprite, its material, its alignment, the atlas grid, the pivot, the sort, the material bindings of
		// the placement values (the tint and the alpha scale of the component).
		UNiagaraSpriteRendererProperties* Renderer = NewObject<UNiagaraSpriteRendererProperties>(Emitter, TEXT("Renderer"), RF_Transactional);
		Renderer->Material = LoadObject<UMaterialInterface>(nullptr, *Spec->GetStringField(TEXT("materialPath")));
		if (Renderer->Material == nullptr)
		{
			return FString::Printf(TEXT("%s: no material %s"), *Name, *Spec->GetStringField(TEXT("materialPath")));
		}
		Renderer->Alignment = Facing == TEXT("velocity") ? ENiagaraSpriteAlignment::VelocityAligned : ENiagaraSpriteAlignment::Unaligned;
		Renderer->FacingMode = ENiagaraSpriteFacingMode::FaceCamera;
		Renderer->SubImageSize = FVector2D(Tiles[0], Tiles[1]);
		Renderer->bSubImageBlend = Atlas->GetBoolField(TEXT("blend"));
		Renderer->SortMode = ENiagaraSortMode::ViewDepth;
		Renderer->bCastShadows = false;
		const TArray<double> Pivot = Numbers(Spec, TEXT("pivot"));
		Renderer->PivotInUVSpace = FVector2D(0.5 + 0.5 * Pivot[0], 0.5 + 0.5 * Pivot[1]);
		Renderer->SortOrderHint = int32(Number(Spec, TEXT("sort")));
		for (const TPair<FName, FName>& Binding : {TPair<FName, FName>(TEXT("Tint"), TEXT("User.SwcTint")),
		                                          TPair<FName, FName>(TEXT("AlphaScale"), TEXT("User.SwcAlphaScale"))})
		{
			FNiagaraMaterialAttributeBinding& Attribute = Renderer->MaterialParameters.AttributeBindings.AddDefaulted_GetRef();
			Attribute.MaterialParameterName = Binding.Key;
			Attribute.NiagaraVariable = Binding.Key == TEXT("Tint")
				? FNiagaraVariableBase(FNiagaraTypeDefinition::GetColorDef(), Binding.Value)
				: FNiagaraVariableBase(FNiagaraTypeDefinition::GetFloatDef(), Binding.Value);
		}
		Emitter->AddRenderer(Renderer, FGuid());

		// The bounds: the reach of the motion over the longest life (a fixed box; the stateless emitter has no dynamic bounds).
		const TArray<double> Speed = Numbers(Spec, TEXT("speed"));
		const TArray<double> LifeRange = Numbers(Spec, TEXT("life"));
		const double MaxLife = LifeRange.Num() > 1 ? LifeRange[1] : 1.0;
		const double Reach = FMath::Max(FMath::Abs(Speed[0]), FMath::Abs(Speed[1])) * MaxLife
			+ 0.5 * FMath::Max(FMath::Abs(Vec3(Numbers(Gravity, TEXT("min"))).Z), FMath::Abs(Vec3(Numbers(Gravity, TEXT("max"))).Z)) * MaxLife * MaxLife
			+ FMath::Max(SizeX[1], SizeY[1]) + Extent.GetMax() + Vec3(Numbers(Spec, TEXT("offset"))).GetAbsMax() + Turbulence * MaxLife;
		*Member<FBox>(Emitter, TEXT("FixedBounds")) = FBox(FVector(-Reach), FVector(Reach));
		Emitter->PostEditChange();

		FNiagaraEmitterHandle Handle(*Emitter);
		System->AddEmitterHandleDirect(Handle);
		return FString();
	}
}

FString USwcEditorLibrary::BuildParticleSystem(UObject* SystemObject, const FString& SpecJson)
{
	UNiagaraSystem* System = Cast<UNiagaraSystem>(SystemObject);
	if (System == nullptr)
	{
		return TEXT("not a Niagara system");
	}
	TSharedPtr<FJsonObject> Spec;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SpecJson), Spec) || !Spec.IsValid())
	{
		return TEXT("the spec is not JSON");
	}
	UClass* TemplateClass = FindObject<UClass>(nullptr, TEXT("/Script/Niagara.NiagaraStatelessEmitterDefault"));
	UNiagaraStatelessEmitterTemplate* Template = TemplateClass ? Cast<UNiagaraStatelessEmitterTemplate>(TemplateClass->GetDefaultObject()) : nullptr;
	if (Template == nullptr)
	{
		return TEXT("no stateless emitter template");
	}
	// A system made again: its old emitters go first.
	while (System->GetNumEmitters() > 0)
	{
		const FNiagaraEmitterHandle Old = System->GetEmitterHandle(System->GetNumEmitters() - 1);
		System->RemoveEmitterHandle(Old);
	}
	// The user parameters of a placement (Effects/SwcEffectSubsystem): the tint and the alpha scale of the component.
	FNiagaraUserRedirectionParameterStore& User = System->GetExposedParameters();
	FNiagaraVariable Tint(FNiagaraTypeDefinition::GetColorDef(), TEXT("User.SwcTint"));
	Tint.SetValue(FLinearColor::White);
	User.AddParameter(Tint, true);
	User.SetParameterValue(FLinearColor::White, Tint);
	FNiagaraVariable AlphaScale(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.SwcAlphaScale"));
	AlphaScale.SetValue(1.0f);
	User.AddParameter(AlphaScale, true);
	User.SetParameterValue(1.0f, AlphaScale);

	FString Errors;
	for (const TSharedPtr<FJsonValue>& Value : Spec->GetArrayField(TEXT("emitters")))
	{
		const TSharedPtr<FJsonObject> Emitter = Value->AsObject();
		if (!Emitter->GetBoolField(TEXT("enabled")))
		{
			continue;
		}
		const FString Error = BuildEmitter(System, Emitter, Template);
		if (!Error.IsEmpty())
		{
			Errors += Error + TEXT("; ");
		}
	}
	System->bFixedBounds = false;
	System->RequestCompile(false);
	System->WaitForCompilationComplete(true, false);
	System->MarkPackageDirty();
	return Errors;
}
