#include "Character/SwcLocomotion.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
TSharedPtr<FJsonObject> ReadObject(const FString& Path)
{
	FString Text;
	TSharedPtr<FJsonObject> Object;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object))
	{
		return nullptr;
	}
	return Object;
}

FString ContentFolder()
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
	return Content;
}
} // namespace

int32 FSwcBlendSpace::ParamIndex(const TCHAR* Name) const
{
	return Params.IndexOfByPredicate([Name](const FParam& Param) { return Param.Name == Name; });
}

void FSwcBlendSpace::Evaluate(const TArray<float>& Values, TMap<int32, float>& OutExampleWeights) const
{
	const int32 Dimensions = Params.Num();
	if (Dimensions == 0 || GridWeights.Num() == 0)
	{
		return;
	}
	// The cell coordinates of each parameter: the cells go from Min to Max inclusive (research R3.2.7).
	TArray<int32, TInlineAllocator<4>> Low, High, Stride;
	TArray<float, TInlineAllocator<4>> Fraction;
	int32 StrideValue = 1;
	for (int32 Dimension = 0; Dimension < Dimensions; ++Dimension)
	{
		const FParam& Param = Params[Dimension];
		const float Value = Values.IsValidIndex(Dimension) ? Values[Dimension] : 0.0f;
		const float Span = Param.Max - Param.Min;
		const float Position = Param.Cells > 1 && Span > 0.0f
			? FMath::Clamp((Value - Param.Min) / Span * static_cast<float>(Param.Cells - 1), 0.0f, static_cast<float>(Param.Cells - 1))
			: 0.0f;
		const int32 Floor = FMath::FloorToInt(Position);
		Low.Add(Floor);
		High.Add(FMath::Min(Floor + 1, Param.Cells - 1));
		Fraction.Add(Position - static_cast<float>(Floor));
		Stride.Add(StrideValue);
		StrideValue *= Param.Cells;
	}
	// Each corner of the cell: the product of the shares of the dimensions.
	for (int32 Corner = 0; Corner < (1 << Dimensions); ++Corner)
	{
		float Share = 1.0f;
		int32 Cell = 0;
		for (int32 Dimension = 0; Dimension < Dimensions; ++Dimension)
		{
			const bool bHigh = (Corner & (1 << Dimension)) != 0;
			Share *= bHigh ? Fraction[Dimension] : 1.0f - Fraction[Dimension];
			Cell += (bHigh ? High[Dimension] : Low[Dimension]) * Stride[Dimension];
		}
		if (Share <= 0.0f || Cell * 4 + 3 >= GridWeights.Num())
		{
			continue;
		}
		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			const float Weight = GridWeights[Cell * 4 + Slot] * Share;
			if (Weight > 0.0f)
			{
				OutExampleWeights.FindOrAdd(GridIndexes[Cell * 4 + Slot]) += Weight;
			}
		}
	}
}

FVector FSwcClip::RootAt(float Time) const
{
	if (Root.IsEmpty())
	{
		return FVector::ZeroVector;
	}
	if (Time <= Root[0].X)
	{
		return FVector(Root[0].Y, Root[0].Z, Root[0].W);
	}
	for (int32 Index = 1; Index < Root.Num(); ++Index)
	{
		const FVector4f& A = Root[Index - 1];
		const FVector4f& B = Root[Index];
		if (Time <= B.X)
		{
			const float Alpha = B.X > A.X ? (Time - A.X) / (B.X - A.X) : 1.0f;
			return FVector(FMath::Lerp(A.Y, B.Y, Alpha), FMath::Lerp(A.Z, B.Z, Alpha), FMath::Lerp(A.W, B.W, Alpha));
		}
	}
	const FVector4f& Last = Root.Last();
	return FVector(Last.Y, Last.Z, Last.W);
}

float FSwcLocomotion::Length(const FSwcFragment& Fragment) const
{
	float Duration = 0.0f;
	if (Clips.IsValidIndex(Fragment.Clip))
	{
		Duration = Clips[Fragment.Clip].Duration;
	}
	else if (BlendSpaces.IsValidIndex(Fragment.BlendSpace))
	{
		const FSwcBlendSpace& Space = BlendSpaces[Fragment.BlendSpace];
		for (int32 Example = 0; Example < Space.ExampleClips.Num(); ++Example)
		{
			if (Clips.IsValidIndex(Space.ExampleClips[Example]))
			{
				Duration = FMath::Max(Duration, Clips[Space.ExampleClips[Example]].Duration / FMath::Max(Space.ExampleScales[Example], 0.01f));
			}
		}
	}
	return Duration / FMath::Max(Fragment.Speed, 0.01f);
}

namespace
{
float Float(const TSharedPtr<FJsonValue>& Value)
{
	return Value.IsValid() && Value->Type == EJson::Number ? static_cast<float>(Value->AsNumber()) : 0.0f;
}

swc::move::Condition ConditionOf(const TSharedPtr<FJsonValue>& Value)
{
	swc::move::Condition Out;
	const TArray<TSharedPtr<FJsonValue>>& Parts = Value->AsArray();
	if (Parts.Num() != 3)
	{
		return Out;
	}
	Out.type = TCHAR_TO_UTF8(*Parts[0]->AsString());
	for (const auto& Attr : Parts[1]->AsObject()->Values)
	{
		Out.attrs[TCHAR_TO_UTF8(*FString(Attr.Key.ToView()))] = TCHAR_TO_UTF8(*Attr.Value->AsString());
	}
	for (const TSharedPtr<FJsonValue>& Child : Parts[2]->AsArray())
	{
		Out.children.push_back(ConditionOf(Child));
	}
	return Out;
}

swc::move::Motion MotionOf(const TSharedPtr<FJsonObject>& Info)
{
	swc::move::Motion Out;
	FString Name;
	if (Info->TryGetStringField(TEXT("name"), Name))
	{
		Out.name = TCHAR_TO_UTF8(*Name);
	}
	const TCHAR* const Fields[3] = {TEXT("speed"), TEXT("accel"), TEXT("decel")};
	float* const Targets[3] = {Out.speed, Out.accel, Out.decel};
	for (int32 Field = 0; Field < 3; ++Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Info->TryGetArrayField(Fields[Field], Values) && Values->Num() == 3)
		{
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				Targets[Field][Axis] = Float((*Values)[Axis]);
			}
		}
	}
	Out.backwards = Info->HasField(TEXT("backwards")) ? static_cast<float>(Info->GetNumberField(TEXT("backwards"))) : 1.0f;
	return Out;
}
} // namespace

void FSwcLocomotion::ReadRules(const TSharedPtr<FJsonObject>& Document, swc::move::Data& Out)
{
	// The same fields as tests/movement/movement_tests.cpp data_of.
	Out = swc::move::Data();
	if (!Document.IsValid())
	{
		return;
	}
	const TSharedPtr<FJsonObject>* Motions = nullptr;
	if (Document->TryGetObjectField(TEXT("motions"), Motions))
	{
		for (const auto& Pair : (*Motions)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			swc::move::MotionList List;
			const TSharedPtr<FJsonObject>* Default = nullptr;
			if (Info->TryGetObjectField(TEXT("default"), Default))
			{
				List.fallback = MotionOf(*Default);
			}
			for (const TSharedPtr<FJsonValue>& Item : Info->GetArrayField(TEXT("conditionals")))
			{
				List.conditionals.push_back(MotionOf(Item->AsObject()));
			}
			Out.motions[TCHAR_TO_UTF8(*FString(Pair.Key.ToView()))] = MoveTemp(List);
		}
	}
	const TSharedPtr<FJsonObject>* Facings = nullptr;
	if (Document->TryGetObjectField(TEXT("facings"), Facings))
	{
		for (const auto& Pair : (*Facings)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			swc::move::FacingList List;
			List.fallback = static_cast<float>(Info->GetNumberField(TEXT("default")));
			for (const TSharedPtr<FJsonValue>& Item : Info->GetArrayField(TEXT("conditionals")))
			{
				const TArray<TSharedPtr<FJsonValue>>& Row = Item->AsArray();
				if (Row.Num() == 2)
				{
					List.conditionals.emplace_back(TCHAR_TO_UTF8(*Row[0]->AsString()), Float(Row[1]));
				}
			}
			Out.facings[TCHAR_TO_UTF8(*FString(Pair.Key.ToView()))] = MoveTemp(List);
		}
	}
	const TSharedPtr<FJsonObject>* Conditions = nullptr;
	if (Document->TryGetObjectField(TEXT("conditions"), Conditions))
	{
		for (const auto& Pair : (*Conditions)->Values)
		{
			Out.conditions[TCHAR_TO_UTF8(*FString(Pair.Key.ToView()))] = ConditionOf(Pair.Value);
		}
	}
	const TSharedPtr<FJsonObject>* Statuses = nullptr;
	if (Document->TryGetObjectField(TEXT("statuses"), Statuses))
	{
		for (const auto& Pair : (*Statuses)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			swc::move::StatusRow Row;
			Row.id = TCHAR_TO_UTF8(*Info->GetStringField(TEXT("id")));
			Row.noNav = Info->GetBoolField(TEXT("noNav"));
			Row.noRun = Info->GetBoolField(TEXT("noRun"));
			Row.noSprint = Info->GetBoolField(TEXT("noSprint"));
			Row.noDodge = Info->GetBoolField(TEXT("noDodge"));
			Row.stunned = Info->GetBoolField(TEXT("stunned"));
			Row.silenced = Info->GetBoolField(TEXT("silenced"));
			Row.moveSpeedMod = static_cast<float>(Info->GetNumberField(TEXT("moveSpeedMod")));
			Row.sprintSpeedMod = static_cast<float>(Info->GetNumberField(TEXT("sprintSpeedMod")));
			Row.categories = TCHAR_TO_UTF8(*Info->GetStringField(TEXT("categories")));
			Out.statuses[static_cast<std::uint16_t>(FCString::Atoi(*FString(Pair.Key.ToView())))] = MoveTemp(Row);
		}
	}
}

bool FSwcLocomotion::Load(bool bFemale, FString& OutReport)
{
	const FString Content = ContentFolder();
	const TSharedPtr<FJsonObject> Document = ReadObject(FPaths::Combine(Content, TEXT("player/locomotion.json")));
	const TSharedPtr<FJsonObject> Imported = ReadObject(FPaths::Combine(Content, TEXT("player/imported.json")));
	if (!Document.IsValid() || !Imported.IsValid())
	{
		OutReport = TEXT("no player/locomotion.json or player/imported.json (tools/convert-player.sh locomotion and base, tools/import-player.sh)");
		return false;
	}
	// The clips: the imported clip of a name is the AnimSequence "<gender><name>" of the base of the gender.
	const FString Gender = bFemale ? TEXT("female") : TEXT("male");
	TMap<FString, FString> Assets;
	const TSharedPtr<FJsonObject>* Assets0 = nullptr;
	const TSharedPtr<FJsonObject>* Base = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Sequences = nullptr;
	if (Imported->TryGetObjectField(TEXT("assets"), Assets0) && (*Assets0)->TryGetObjectField(TEXT("base|") + Gender, Base)
		&& (*Base)->TryGetArrayField(TEXT("AnimSequence"), Sequences))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Sequences)
		{
			const FString Path = Value->AsString();
			Assets.Add(FPaths::GetBaseFilename(Path).ToLower(), Path);
		}
	}
	TMap<FString, int32> ClipIndex;
	const TSharedPtr<FJsonObject>* ClipObjects = nullptr;
	if (Document->TryGetObjectField(TEXT("clips"), ClipObjects))
	{
		for (const auto& Pair : (*ClipObjects)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			FSwcClip& Clip = Clips.AddDefaulted_GetRef();
			Clip.Name = FString(Pair.Key.ToView());
			Clip.Duration = static_cast<float>(Info->GetNumberField(TEXT("duration")));
			Clip.MoveSpeed = static_cast<float>(Info->GetNumberField(TEXT("moveSpeed")));
			const FString* Path = Assets.Find(Gender + Clip.Name);
			Clip.AssetPath = Path != nullptr ? *Path : FString();
			const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
			if (Info->TryGetArrayField(TEXT("root"), Keys))
			{
				for (const TSharedPtr<FJsonValue>& Key : *Keys)
				{
					const TArray<TSharedPtr<FJsonValue>>& Values = Key->AsArray();
					if (Values.Num() == 4)
					{
						Clip.Root.Add(FVector4f(static_cast<float>(Values[0]->AsNumber()), static_cast<float>(Values[1]->AsNumber()),
							static_cast<float>(Values[2]->AsNumber()), static_cast<float>(Values[3]->AsNumber())));
					}
				}
			}
			ClipIndex.Add(Clip.Name, Clips.Num() - 1);
		}
	}
	TMap<FString, int32> SpaceIndex;
	const TSharedPtr<FJsonObject>* SpaceObjects = nullptr;
	if (Document->TryGetObjectField(TEXT("blendSpaces"), SpaceObjects))
	{
		for (const auto& Pair : (*SpaceObjects)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			FSwcBlendSpace& Space = BlendSpaces.AddDefaulted_GetRef();
			for (const TSharedPtr<FJsonValue>& Value : Info->GetArrayField(TEXT("params")))
			{
				const TSharedPtr<FJsonObject> Param = Value->AsObject();
				Space.Params.Add({Param->GetStringField(TEXT("name")), static_cast<float>(Param->GetNumberField(TEXT("min"))),
					static_cast<float>(Param->GetNumberField(TEXT("max"))), static_cast<int32>(Param->GetNumberField(TEXT("cells")))});
			}
			for (const TSharedPtr<FJsonValue>& Value : Info->GetArrayField(TEXT("examples")))
			{
				const TSharedPtr<FJsonObject> Example = Value->AsObject();
				const int32* Index = ClipIndex.Find(Example->GetStringField(TEXT("clip")));
				Space.ExampleClips.Add(Index != nullptr ? *Index : INDEX_NONE);
				Space.ExampleScales.Add(static_cast<float>(Example->GetNumberField(TEXT("scale"))));
			}
			for (const TSharedPtr<FJsonValue>& Value : Info->GetArrayField(TEXT("grid")))
			{
				const TArray<TSharedPtr<FJsonValue>>& Cell = Value->AsArray();
				for (int32 Slot = 0; Slot < 4; ++Slot)
				{
					Space.GridIndexes.Add(static_cast<int32>(Cell[Slot]->AsNumber()));
					Space.GridWeights.Add(static_cast<float>(Cell[4 + Slot]->AsNumber()));
				}
			}
			SpaceIndex.Add(FString(Pair.Key.ToView()), BlendSpaces.Num() - 1);
		}
	}
	const TSharedPtr<FJsonObject>* FragmentObjects = nullptr;
	if (Document->TryGetObjectField(TEXT("fragments"), FragmentObjects))
	{
		for (const auto& Pair : (*FragmentObjects)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			FSwcFragment Fragment;
			FString Name;
			if (Info->TryGetStringField(TEXT("clip"), Name))
			{
				const int32* Index = ClipIndex.Find(Name);
				Fragment.Clip = Index != nullptr ? *Index : INDEX_NONE;
			}
			if (Info->TryGetStringField(TEXT("blendSpace"), Name))
			{
				const int32* Index = SpaceIndex.Find(Name);
				Fragment.BlendSpace = Index != nullptr ? *Index : INDEX_NONE;
			}
			Fragment.BlendIn = static_cast<float>(Info->GetNumberField(TEXT("blendIn")));
			Fragment.BlendOut = static_cast<float>(Info->GetNumberField(TEXT("blendOut")));
			Fragment.bLoop = Info->GetBoolField(TEXT("loop"));
			Fragment.Speed = static_cast<float>(Info->GetNumberField(TEXT("speed")));
			const TSharedPtr<FJsonObject>* Markers = nullptr;
			if (Info->TryGetObjectField(TEXT("markers"), Markers))
			{
				for (const auto& Marker : (*Markers)->Values)
				{
					Fragment.Markers.Add(FString(Marker.Key.ToView()), static_cast<float>(Marker.Value->AsNumber()));
				}
			}
			Fragments.Add(FString(Pair.Key.ToView()), Fragment);
		}
	}
	const int32 Missing = Clips.FilterByPredicate([](const FSwcClip& Clip) { return Clip.AssetPath.IsEmpty(); }).Num();
	ReadRules(ReadObject(FPaths::Combine(Content, TEXT("player/movement.json"))), Rules);
	OutReport = FString::Printf(TEXT("locomotion: %d fragments, %d blend spaces, %d clips (%d not imported for the %s base); movement rules: %d motion lists, %d conditions, %d status rows"),
		Fragments.Num(), BlendSpaces.Num(), Clips.Num(), Missing, *Gender, static_cast<int32>(Rules.motions.size()),
		static_cast<int32>(Rules.conditions.size()), static_cast<int32>(Rules.statuses.size()));
	return Fragments.Num() > 0 && Missing < Clips.Num();
}
