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
	OutReport = FString::Printf(TEXT("locomotion: %d fragments, %d blend spaces, %d clips (%d not imported for the %s base)"), Fragments.Num(),
		BlendSpaces.Num(), Clips.Num(), Missing, *Gender);
	return Fragments.Num() > 0 && Missing < Clips.Num();
}
