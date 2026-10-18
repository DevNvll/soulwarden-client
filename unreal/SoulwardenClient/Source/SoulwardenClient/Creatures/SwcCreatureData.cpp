#include "Creatures/SwcCreatureData.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

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

FString WorkFolder()
{
	FString Work = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_WORK"));
	if (Work.IsEmpty())
	{
		Work = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT("Games/soulwarden-client"));
	}
	return Work;
}

float Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, float Default)
{
	double Value = Default;
	return Object.IsValid() && Object->TryGetNumberField(Name, Value) ? static_cast<float>(Value) : Default;
}

FSwcFragmentItem::EKind KindOf(const FString& Text)
{
	if (Text == TEXT("clip"))
	{
		return FSwcFragmentItem::EKind::Clip;
	}
	if (Text == TEXT("blendSpace"))
	{
		return FSwcFragmentItem::EKind::BlendSpace;
	}
	if (Text == TEXT("comb"))
	{
		return FSwcFragmentItem::EKind::Comb;
	}
	if (Text == TEXT("end"))
	{
		return FSwcFragmentItem::EKind::End;
	}
	return FSwcFragmentItem::EKind::Missing;
}
} // namespace

FString FSwcCreatureLibrary::ContentFolder()
{
	FString Content = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_CONTENT"));
	return Content.IsEmpty() ? FPaths::Combine(WorkFolder(), TEXT("content")) : Content;
}

bool FSwcCreatureLibrary::Load(FString& OutReport)
{
	bLoaded = true;
	const FString Folder = FPaths::Combine(ContentFolder(), TEXT("creatures"));
	const TSharedPtr<FJsonObject> Index = ReadObject(FPaths::Combine(Folder, TEXT("slices.json")));
	const TSharedPtr<FJsonObject> Names = ReadObject(FPaths::Combine(Folder, TEXT("names.json")));
	if (Names.IsValid())
	{
		for (const TCHAR* Table : {TEXT("categories"), TEXT("vitals")})
		{
			const TSharedPtr<FJsonObject>* Rows = nullptr;
			if (!Names->TryGetObjectField(Table, Rows))
			{
				continue;
			}
			TMap<uint32, TPair<FString, FString>>& Out = FCString::Strcmp(Table, TEXT("categories")) == 0 ? Categories : VitalsRows;
			for (const auto& Row : (*Rows)->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>& Values = Row.Value->AsArray();
				if (Values.Num() == 2)
				{
					Out.Add(static_cast<uint32>(FCString::Strtoui64(*FString(Row.Key.ToView()), nullptr, 10)),
						TPair<FString, FString>(Values[0]->AsString(), Values[1]->AsString()));
				}
			}
		}
	}
	if (Names.IsValid())
	{
		const TSharedPtr<FJsonObject>* Rows = nullptr;
		if (Names->TryGetObjectField(TEXT("scales"), Rows))
		{
			for (const auto& Row : (*Rows)->Values)
			{
				Scales.Add(static_cast<uint32>(FCString::Strtoui64(*FString(Row.Key.ToView()), nullptr, 10)), static_cast<float>(Row.Value->AsNumber()));
			}
		}
	}
	// The wanted list of earlier runs.
	if (const TSharedPtr<FJsonObject> WantedFile = ReadObject(FPaths::Combine(WorkFolder(), TEXT("creatures/wanted.json"))))
	{
		const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
		if (WantedFile->TryGetArrayField(TEXT("slices"), Keys))
		{
			for (const TSharedPtr<FJsonValue>& Key : *Keys)
			{
				Wanted.Add(Key->AsString());
			}
		}
	}
	if (!Index.IsValid())
	{
		OutReport = TEXT("no creatures/slices.json (tools/convert-creatures.sh convert, tools/import-creatures.sh): each creature is a placeholder");
		return false;
	}
	// The object paths of the assets in the shared content plugin (/SwcGameContent, tools/import.sh): the converter writes
	// them with the naming rule of Interchange (converter/swc_convert/creature.py, object_path).
	const TSharedPtr<FJsonObject>* Bodies = nullptr;
	Index->TryGetObjectField(TEXT("bodies"), Bodies);
	const TSharedPtr<FJsonObject>* WeaponModels = nullptr;
	Index->TryGetObjectField(TEXT("weapons"), WeaponModels);
	const TSharedPtr<FJsonObject>* Skeletons = nullptr;
	Index->TryGetObjectField(TEXT("skeletons"), Skeletons);
	const auto MeshOf = [](const TSharedPtr<FJsonObject>* Table, const FString& Key) -> FString
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		FString Mesh;
		if (Table != nullptr && (*Table)->TryGetObjectField(Key, Found))
		{
			(*Found)->TryGetStringField(TEXT("mesh"), Mesh);
		}
		return Mesh;
	};
	const auto Floats = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
	{
		TArray<double> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object->TryGetArrayField(Name, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				Out.Add(Value->AsNumber());
			}
		}
		return Out;
	};
	int32 SkeletonCount = 0;
	if (Skeletons != nullptr)
	{
		SkeletonCount = (*Skeletons)->Values.Num();
	}
	const TSharedPtr<FJsonObject>* SliceObjects = nullptr;
	int32 WithMesh = 0;
	if (Index->TryGetObjectField(TEXT("slices"), SliceObjects))
	{
		for (const auto& Pair : (*SliceObjects)->Values)
		{
			const TSharedPtr<FJsonObject> Entry = Pair.Value->AsObject();
			FSwcCreatureSlice Slice;
			Slice.Path = Entry->GetStringField(TEXT("path"));
			Slice.Body = Entry->GetStringField(TEXT("body"));
			Slice.AnimationKey = Entry->GetStringField(TEXT("animations"));
			Slice.MeshPath = MeshOf(Bodies, Slice.Body);
			Slice.SkeletonMeshPath = MeshOf(Skeletons, Entry->GetStringField(TEXT("skeleton")));
			WithMesh += Slice.MeshPath.IsEmpty() ? 0 : 1;
			const TSharedPtr<FJsonObject>* Variants = nullptr;
			if (Entry->TryGetObjectField(TEXT("variants"), Variants))
			{
				for (const auto& Variant : (*Variants)->Values)
				{
					const FString Mesh = MeshOf(Bodies, Variant.Value->AsString());
					if (!Mesh.IsEmpty())
					{
						Slice.VariantMeshes.Add(static_cast<uint32>(FCString::Strtoui64(*FString(Variant.Key.ToView()), nullptr, 10)), Mesh);
					}
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* ActionNames = nullptr;
			if (Entry->TryGetArrayField(TEXT("actions"), ActionNames))
			{
				for (const TSharedPtr<FJsonValue>& Name : *ActionNames)
				{
					Slice.Actions.Add(Name->AsString());
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Weapons = nullptr;
			if (Entry->TryGetArrayField(TEXT("weapons"), Weapons))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Weapons)
				{
					const TSharedPtr<FJsonObject> Weapon = Value->AsObject();
					FSwcCreatureWeapon& Out = Slice.Weapons.AddDefaulted_GetRef();
					Out.Slot = static_cast<int32>(Number(Weapon, TEXT("slot"), 0.0f));
					Out.Item = Weapon->GetStringField(TEXT("item"));
					Out.Bone = FName(*Weapon->GetStringField(TEXT("bone")));
					Out.MeshPath = MeshOf(WeaponModels, Weapon->GetStringField(TEXT("model")));
					const TArray<double> Position = Floats(Weapon, TEXT("position"));
					const TArray<double> Rotation = Floats(Weapon, TEXT("rotation"));
					if (Position.Num() == 3)
					{
						Out.EnginePosition = FVector(Position[0], Position[1], Position[2]);
					}
					if (Rotation.Num() == 4)
					{
						// CryEngine writes RelRotation as w, x, y, z.
						Out.EngineRotation = FQuat(Rotation[1], Rotation[2], Rotation[3], Rotation[0]).GetNormalized();
					}
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Volumes = nullptr;
			if (Entry->TryGetArrayField(TEXT("hitVolumes"), Volumes))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Volumes)
				{
					const TSharedPtr<FJsonObject> Volume = Value->AsObject();
					FSwcHitVolume& Out = Slice.HitVolumes.AddDefaulted_GetRef();
					Out.Name = Volume->GetStringField(TEXT("name"));
					Out.Bone = FName(*Volume->GetStringField(TEXT("bone")));
					const FString Shape = Volume->GetStringField(TEXT("shape"));
					Out.Shape = Shape == TEXT("sphere") ? FSwcHitVolume::EShape::Sphere
						: Shape == TEXT("box")          ? FSwcHitVolume::EShape::Box
														: FSwcHitVolume::EShape::Capsule;
					const auto Vector = [&](const TCHAR* Name, const FVector& Default)
					{
						const TArray<double> Values = Floats(Volume, Name);
						return Values.Num() == 3 ? FVector(Values[0], Values[1], Values[2]) : Default;
					};
					Out.EngineCenter = Vector(TEXT("center"), FVector::ZeroVector);
					Out.EngineAxis = Vector(TEXT("axis"), FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
					Out.EngineHalfSize = Vector(TEXT("box"), FVector::ZeroVector);
					Out.Radius = Number(Volume, TEXT("radius"), 0.0f);
					Out.Height = Number(Volume, TEXT("height"), 0.0f);
					Out.DamageMult = Number(Volume, TEXT("damageMult"), 1.0f);
					bool bFlag = false;
					Out.bHeadshot = Volume->TryGetBoolField(TEXT("headshot"), bFlag) && bFlag;
					bFlag = false;
					Out.bLegshot = Volume->TryGetBoolField(TEXT("legshot"), bFlag) && bFlag;
					Out.Component = FCString::Strtoui64(*Volume->GetStringField(TEXT("component")), nullptr, 16);
					Out.Local = FCString::Strtoui64(*Volume->GetStringField(TEXT("local")), nullptr, 16);
				}
			}
			const TSharedPtr<FJsonObject>* Marker = nullptr;
			if (Entry->TryGetObjectField(TEXT("marker"), Marker))
			{
				Slice.NearRange = Number(*Marker, TEXT("nearRange"), Slice.NearRange);
				Slice.NearAngle = Number(*Marker, TEXT("nearAngle"), Slice.NearAngle);
				Slice.FarAngle = Number(*Marker, TEXT("farAngle"), Slice.FarAngle);
				Slice.AngleShrinkDistance = Number(*Marker, TEXT("angleShrinkDistance"), Slice.AngleShrinkDistance);
				Slice.VerticalOffset = Number(*Marker, TEXT("verticalOffset"), Slice.VerticalOffset);
				bool bNever = false;
				Slice.bNeverShowHealthBar = (*Marker)->TryGetBoolField(TEXT("neverShowHealthBar"), bNever) && bNever;
			}
			Slices.Add(FString(Pair.Key.ToView()), Slice);
		}
	}
	OutReport = FString::Printf(TEXT("creatures: %d converted slices (%d with a body), %d skeletons, %d names"), Slices.Num(), WithMesh, SkeletonCount,
		Categories.Num());
	return true;
}

TSharedPtr<FSwcCreatureAnimations> FSwcCreatureLibrary::Animations(const FString& Key)
{
	if (const TSharedPtr<FSwcCreatureAnimations>* Found = Sets.Find(Key))
	{
		return *Found;
	}
	TSharedPtr<FSwcCreatureAnimations>& Set = Sets.Add(Key);
	const TSharedPtr<FJsonObject> Document = ReadObject(FPaths::Combine(ContentFolder(), TEXT("creatures/animations"), Key + TEXT(".json")));
	if (!Document.IsValid())
	{
		return Set;
	}
	Set = MakeShared<FSwcCreatureAnimations>();
	Set->Key = Key;
	for (const TSharedPtr<FJsonValue>& Value : Document->GetArrayField(TEXT("fragmentNames")))
	{
		Set->FragmentNames.Add(Value->AsString());
	}
	TMap<FString, int32> ClipIndex;
	const TSharedPtr<FJsonObject>* ClipObjects = nullptr;
	if (Document->TryGetObjectField(TEXT("clips"), ClipObjects))
	{
		for (const auto& Pair : (*ClipObjects)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			FSwcClip& Clip = Set->Clips.AddDefaulted_GetRef();
			Clip.Name = FString(Pair.Key.ToView());
			Clip.Duration = Number(Info, TEXT("duration"), 0.0f);
			Clip.MoveSpeed = Number(Info, TEXT("moveSpeed"), 0.0f);
			Info->TryGetStringField(TEXT("asset"), Clip.AssetPath);
			TArray<FName>& Bones = Set->ClipBones.AddDefaulted_GetRef();
			const TArray<TSharedPtr<FJsonValue>>* BoneNames = nullptr;
			if (Info->TryGetArrayField(TEXT("bones"), BoneNames))
			{
				for (const TSharedPtr<FJsonValue>& Bone : *BoneNames)
				{
					Bones.Add(FName(*Bone->AsString()));
				}
			}
			bool bAdditive = false;
			Set->ClipAdditive.Add(Info->TryGetBoolField(TEXT("additive"), bAdditive) && bAdditive);
			Set->ImportedClips += Clip.AssetPath.IsEmpty() ? 0 : 1;
			ClipIndex.Add(Clip.Name, Set->Clips.Num() - 1);
		}
	}
	TMap<FString, int32> SpaceIndex;
	const TSharedPtr<FJsonObject>* SpaceObjects = nullptr;
	if (Document->TryGetObjectField(TEXT("blendSpaces"), SpaceObjects))
	{
		for (const auto& Pair : (*SpaceObjects)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			FSwcBlendSpace& Space = Set->BlendSpaces.AddDefaulted_GetRef();
			for (const TSharedPtr<FJsonValue>& Value : Info->GetArrayField(TEXT("params")))
			{
				const TSharedPtr<FJsonObject> Param = Value->AsObject();
				Space.Params.Add({Param->GetStringField(TEXT("name")), Number(Param, TEXT("min"), 0.0f), Number(Param, TEXT("max"), 0.0f),
					static_cast<int32>(Number(Param, TEXT("cells"), 1.0f))});
			}
			for (const TSharedPtr<FJsonValue>& Value : Info->GetArrayField(TEXT("examples")))
			{
				const TSharedPtr<FJsonObject> Example = Value->AsObject();
				const int32* Index = ClipIndex.Find(Example->GetStringField(TEXT("clip")));
				Space.ExampleClips.Add(Index != nullptr ? *Index : INDEX_NONE);
				Space.ExampleScales.Add(Number(Example, TEXT("scale"), 1.0f));
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
			SpaceIndex.Add(FString(Pair.Key.ToView()), Set->BlendSpaces.Num() - 1);
		}
	}
	TMap<FString, int32> CombIndex;
	const TSharedPtr<FJsonObject>* CombObjects = nullptr;
	if (Document->TryGetObjectField(TEXT("combs"), CombObjects))
	{
		for (const auto& Pair : (*CombObjects)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			FSwcComb& Comb = Set->Combs.AddDefaulted_GetRef();
			const TArray<TSharedPtr<FJsonValue>>& Params = Info->GetArrayField(TEXT("params"));
			if (Params.Num() > 0)
			{
				const TSharedPtr<FJsonObject> Param = Params[0]->AsObject();
				Comb.Param = Param->GetStringField(TEXT("name"));
				Comb.Min = Number(Param, TEXT("min"), 0.0f);
				Comb.Max = Number(Param, TEXT("max"), 0.0f);
			}
			const TArray<TSharedPtr<FJsonValue>>* SpaceNames = nullptr;
			if (Info->TryGetArrayField(TEXT("names"), SpaceNames))
			{
				for (const TSharedPtr<FJsonValue>& Name : *SpaceNames)
				{
					const int32* Index = SpaceIndex.Find(Name->AsString());
					Comb.Spaces.Add(Index != nullptr ? *Index : INDEX_NONE);
				}
			}
			CombIndex.Add(FString(Pair.Key.ToView()), Set->Combs.Num() - 1);
		}
	}
	const TSharedPtr<FJsonObject>* FragmentObjects = nullptr;
	if (Document->TryGetObjectField(TEXT("fragments"), FragmentObjects))
	{
		for (const auto& Pair : (*FragmentObjects)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			const int32 Id = FCString::Atoi(*FString(Pair.Key.ToView()));
			FSwcCreatureFragment Fragment;
			Fragment.Name = Info->GetStringField(TEXT("name"));
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (!Info->TryGetArrayField(TEXT("items"), Items))
			{
				continue;
			}
			Fragment.Length = Number(Info, TEXT("length"), -1.0f);
			Fragment.BlendOut = Number(Info, TEXT("blendOut"), 0.2f);
			bool bLoop = false;
			Fragment.bLoop = Info->TryGetBoolField(TEXT("loop"), bLoop) && bLoop;
			const TArray<TSharedPtr<FJsonValue>>* Scopes = nullptr;
			if (Info->TryGetArrayField(TEXT("scopes"), Scopes))
			{
				for (const TSharedPtr<FJsonValue>& Scope : *Scopes)
				{
					Fragment.Scopes.Add(Scope->AsString());
				}
			}
			// The start of each item: its "at" time, or the end of the clip before it (ExitTime -1).
			const auto ReadItems = [&Set, &ClipIndex, &SpaceIndex, &CombIndex](const TArray<TSharedPtr<FJsonValue>>& Values)
			{
				TArray<FSwcFragmentItem> Out;
				float PreviousEnd = 0.0f;
				for (const TSharedPtr<FJsonValue>& Value : Values)
				{
					const TSharedPtr<FJsonObject> Entry = Value->AsObject();
					FSwcFragmentItem Item;
					Item.Kind = KindOf(Entry->GetStringField(TEXT("kind")));
					const FString Name = Entry->GetStringField(TEXT("clip"));
					const int32* Index = Item.Kind == FSwcFragmentItem::EKind::Clip ? ClipIndex.Find(Name)
						: Item.Kind == FSwcFragmentItem::EKind::BlendSpace ? SpaceIndex.Find(Name)
						: Item.Kind == FSwcFragmentItem::EKind::Comb ? CombIndex.Find(Name) : nullptr;
					Item.Index = Index != nullptr ? *Index : INDEX_NONE;
					if (Item.Index == INDEX_NONE && Item.Kind != FSwcFragmentItem::EKind::End)
					{
						Item.Kind = FSwcFragmentItem::EKind::Missing;
					}
					bool bAfter = false;
					Entry->TryGetBoolField(TEXT("afterPrevious"), bAfter);
					Item.Start = bAfter && Out.Num() > 0 ? PreviousEnd : Number(Entry, TEXT("at"), 0.0f);
					Item.ClipStart = Number(Entry, TEXT("clipStart"), 0.0f);
					Item.BlendIn = Number(Entry, TEXT("blendIn"), 0.0f);
					Item.Speed = FMath::Max(Number(Entry, TEXT("speed"), 1.0f), 0.001f);
					Item.Weight = Number(Entry, TEXT("weight"), 1.0f);
					bool bItemLoop = false;
					Item.bLoop = Entry->TryGetBoolField(TEXT("loop"), bItemLoop) && bItemLoop;
					if (Item.Kind == FSwcFragmentItem::EKind::Clip)
					{
						PreviousEnd = Item.Start + FMath::Max(Set->Clips[Item.Index].Duration - Item.ClipStart, 0.0f) / Item.Speed;
					}
					Out.Add(Item);
				}
				return Out;
			};
			Fragment.Items = ReadItems(*Items);
			const TArray<TSharedPtr<FJsonValue>>* Extra = nullptr;
			if (Info->TryGetArrayField(TEXT("layers"), Extra))
			{
				for (const TSharedPtr<FJsonValue>& Layer : *Extra)
				{
					Fragment.ExtraLayers.Add(ReadItems(Layer->AsArray()));
				}
			}
			Set->Fragments.Add(Id, MoveTemp(Fragment));
		}
	}
	for (const TPair<int32, FSwcCreatureFragment>& Pair : Set->Fragments)
	{
		if (Pair.Value.Name.Equals(TEXT("Idle"), ESearchCase::IgnoreCase))
		{
			Set->IdleFragment = Pair.Key;
		}
		else if (Pair.Value.Name.Equals(TEXT("Move"), ESearchCase::IgnoreCase))
		{
			Set->MoveFragment = Pair.Key;
		}
	}
	return Set;
}

FString FSwcCreatureLibrary::Name(uint32 CategoryId, uint32 VitalsId) const
{
	if (const TPair<FString, FString>* Found = Categories.Find(CategoryId))
	{
		return Found->Key;
	}
	if (const TPair<FString, FString>* Found = VitalsRows.Find(VitalsId))
	{
		return Found->Key;
	}
	return FString::Printf(TEXT("creature %08x"), VitalsId);
}

bool FSwcCreatureLibrary::HidesLevel(uint32 CategoryId, uint32 VitalsId) const
{
	const TPair<FString, FString>* Found = Categories.Find(CategoryId);
	if (Found == nullptr)
	{
		Found = VitalsRows.Find(VitalsId);
	}
	return Found != nullptr && Found->Value == TEXT("Critter");
}

float FSwcCreatureLibrary::Scale(uint32 VitalsId) const
{
	const float* Found = Scales.Find(VitalsId);
	return Found != nullptr && *Found > 0.0f ? *Found : 1.0f;
}

bool FSwcCreatureLibrary::AddWanted(const FString& Key)
{
	if (Wanted.Contains(Key))
	{
		return false;
	}
	Wanted.Add(Key);
	TArray<FString> Sorted = Wanted.Array();
	Sorted.Sort();
	FString Text = TEXT("{\"slices\": [");
	for (int32 Index = 0; Index < Sorted.Num(); ++Index)
	{
		Text += FString::Printf(TEXT("%s\"%s\""), Index > 0 ? TEXT(", ") : TEXT(""), *Sorted[Index]);
	}
	Text += TEXT("]}\n");
	const FString Path = FPaths::Combine(WorkFolder(), TEXT("creatures/wanted.json"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	FFileHelper::SaveStringToFile(Text, *Path);
	return true;
}
