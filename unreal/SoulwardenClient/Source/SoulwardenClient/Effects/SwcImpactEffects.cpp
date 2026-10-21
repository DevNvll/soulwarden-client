#include "Effects/SwcImpactEffects.h"

#include "Components/MeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Character/SwcActions.h"
#include "Effects/SwcFragmentEffects.h"
#include "Effects/SwcParticleEffects.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcImpacts, Log, All);

bool FSwcImpactEffects::Load()
{
	if (bLoaded)
	{
		return bValid;
	}
	bLoaded = true;
	const FString Path = FPaths::Combine(USwcEffectSubsystem::Folder(), TEXT("impacts.json"));
	FString Text;
	TSharedPtr<FJsonObject> Doc;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) || !Doc.IsValid())
	{
		UE_LOG(LogSwcImpacts, Warning, TEXT("impacts: no %s (tools/convert-particles.sh skills)"), *Path);
		return false;
	}
	for (const auto& Row : Doc->GetObjectField(TEXT("tables"))->GetObjectField(TEXT("characters"))->Values)
	{
		TMap<FString, FString>& Columns = Characters.Add(FString(Row.Key.ToView()));
		for (const auto& Column : Row.Value->AsObject()->Values)
		{
			Columns.Add(FString(Column.Key.ToView()), Column.Value->AsString());
		}
	}
	for (const auto& Function : Doc->GetObjectField(TEXT("functions"))->Values)
	{
		TArray<FCall>& Calls = Functions.Add(FString(Function.Key.ToView()));
		for (const TSharedPtr<FJsonValue>& Value : Function.Value->AsObject()->GetArrayField(TEXT("calls")))
		{
			const TSharedPtr<FJsonObject> Data = Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>& Args = Data->GetArrayField(TEXT("args"));
			FCall& Call = Calls.AddDefaulted_GetRef();
			Call.Call = Data->GetStringField(TEXT("call"));
			const auto Arg = [&Args](int32 Index) { return Args.IsValidIndex(Index) && Args[Index]->Type == EJson::String ? Args[Index]->AsString() : FString(); };
			if (Call.Call == TEXT("SpawnParticle"))
			{
				// SpawnParticle(key, a1 the point, the direction that the script selects: a2 or a3 with a multiplier, or a fixed vector).
				Call.Key = Arg(0);
				if (Args.IsValidIndex(2) && Args[2]->Type == EJson::Array && Args[2]->AsArray().Num() == 3)
				{
					const TArray<TSharedPtr<FJsonValue>>& V = Args[2]->AsArray();
					const FVector Game(V[0]->AsNumber(), V[1]->AsNumber(), V[2]->AsNumber());
					Call.DirectionSource = Game.IsNearlyZero() ? 0 : 4;
					Call.FixedDirection = FVector(Game.X, -Game.Y, Game.Z).GetSafeNormal();
				}
				else
				{
					FString Name = Arg(2), Right;
					Call.DirectionScale = 1.0f;
					if (Name.Split(TEXT("*"), &Name, &Right))
					{
						Call.DirectionScale = FCString::Atof(*Right);
					}
					Call.DirectionSource = Name == TEXT("a3") ? 3 : (Name == TEXT("a2") ? 2 : 0);
				}
			}
			else if (Call.Call == TEXT("SpawnParticleAtAttachment") || Call.Call == TEXT("SpawnParticleAtAttachmentOffset"))
			{
				// SpawnParticleAtAttachment(the weapon, the helper, key, offset, ..., follow).
				Call.Helper = Arg(1);
				Call.Key = Arg(2);
			}
		}
	}
	for (const auto& Row : Doc->GetObjectField(TEXT("damage"))->Values)
	{
		Categories.Add(FString(Row.Key.ToView()), Row.Value->AsObject()->GetStringField(TEXT("category")));
	}
	for (const auto& Slice : Doc->GetObjectField(TEXT("volumes"))->Values)
	{
		TMap<FString, FString>& Out = Volumes.Add(FString(Slice.Key.ToView()).ToLower());
		for (const auto& Volume : Slice.Value->AsObject()->Values)
		{
			Out.Add(FString(Volume.Key.ToView()), Volume.Value->AsString());
		}
	}
	bValid = true;
	UE_LOG(LogSwcImpacts, Log, TEXT("impacts: %d rows, %d functions, %d damage rows, %d slices"), Characters.Num(), Functions.Num(), Categories.Num(),
		Volumes.Num());
	return true;
}

FString FSwcImpactEffects::Play(UWorld* World, const FSwcImpactHit& Hit)
{
	USwcEffectSubsystem* Effects = World != nullptr ? World->GetSubsystem<USwcEffectSubsystem>() : nullptr;
	if (Effects == nullptr || !Load())
	{
		return TEXT("no hit data");
	}
	const FString* Category = Categories.Find(Hit.DamageRow.ToLower());
	if (Category == nullptr || Category->IsEmpty())
	{
		return FString::Printf(TEXT("the damage row %s has no WeaponCategory"), *Hit.DamageRow);
	}
	// The critical row first when the table has it (the native key construction, study "Hit effect lookup").
	FString RowKey = *Category;
	if (Hit.bCrit && Characters.Contains(RowKey + TEXT("+Crit")))
	{
		RowKey += TEXT("+Crit");
	}
	const TMap<FString, FString>* Columns = Characters.Find(RowKey);
	if (Columns == nullptr)
	{
		return FString::Printf(TEXT("no row %s in ImpactTable_Characters"), *RowKey);
	}
	const TMap<FString, FString>* SliceVolumes = Volumes.Find(Hit.SlicePath.ToLower());
	const FString* Material = SliceVolumes != nullptr ? SliceVolumes->Find(Hit.Volume) : nullptr;
	FString Column = Material != nullptr && Columns->Contains(*Material) ? *Material : FString(TEXT("Default"));
	const FString* Function = Columns->Find(Column);
	if (Function == nullptr)
	{
		return FString::Printf(TEXT("the row %s has no column %s"), *RowKey, *Column);
	}
	const TArray<FCall>* Calls = Functions.Find(*Function);
	FString Text = FString::Printf(TEXT("the hit %s %s (%s): %s"), *RowKey, *Column, *Hit.Volume, **Function);
	if (Calls == nullptr)
	{
		return Text + TEXT(": no function");
	}
	for (const FCall& Call : *Calls)
	{
		UNiagaraComponent* Component = nullptr;
		if (Call.Call == TEXT("SpawnParticle") && !Call.Key.IsEmpty())
		{
			// The local +Z of the effect along the direction of the call (research/particle-impacts.md 4 [R]: a look-at matrix; the roll about
			// the axis is not the retail transverse basis yet).
			if (Call.DirectionSource == 0)
			{
				Text += FString::Printf(TEXT("; %s: a direction that the client does not read, not played"), *Call.Key);
				continue;
			}
			const FVector Source = Call.DirectionSource == 4 ? Call.FixedDirection : (Call.DirectionSource == 2 ? Hit.Normal : Hit.Direction);
			const FVector Direction = (Source * Call.DirectionScale).GetSafeNormal();
			const FQuat Rotation = FRotationMatrix::MakeFromZ(Direction.IsNearlyZero() ? FVector::UpVector : Direction).ToQuat();
			Component = Effects->Start(Call.Key, FTransform(Rotation, Hit.Point), FSwcEffectSettings());
		}
		else if (!Call.Helper.IsEmpty() && !Call.Key.IsEmpty() && Hit.Weapon != nullptr && Hit.Actions != nullptr)
		{
			FName Socket;
			FTransform Relative;
			if (SwcWeaponHelperPlace(*Hit.Actions, Hit.WeaponFile, FName(*Call.Helper), Hit.Weapon, Socket, Relative))
			{
				Component = Effects->StartAttached(Call.Key, Hit.Weapon, Socket, Relative, true, FSwcEffectSettings());
				if (Component != nullptr)
				{
					// A trail on the weapon ends by its own life; the component goes back to the pool when its system ends.
					Effects->Keep(Component);
				}
			}
		}
		else
		{
			Text += FString::Printf(TEXT("; %s %s: not played"), *Call.Call, *Call.Key);
			continue;
		}
		Text += FString::Printf(TEXT("; %s%s"), *Call.Key, Component != nullptr ? TEXT("") : TEXT(" (no system)"));
		if (Component != nullptr && Call.Call == TEXT("SpawnParticle"))
		{
			Effects->Keep(Component);
		}
	}
	return Text;
}
