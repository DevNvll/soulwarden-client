#include "Character/SwcActions.h"

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

float Number(const TSharedPtr<FJsonValue>& Value, float Default = 0.0f)
{
	return Value.IsValid() && Value->Type == EJson::Number ? static_cast<float>(Value->AsNumber()) : Default;
}

/** @return The progression ids of a list of names of actions.json (stage P3.5: the nodes and the perks of a condition). */
std::vector<std::uint32_t> IdsOf(const TSharedPtr<FJsonValue>& Value)
{
	std::vector<std::uint32_t> Out;
	if (Value.IsValid() && Value->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Name : Value->AsArray())
		{
			Out.push_back(swc::combat::progression_id(TCHAR_TO_UTF8(*Name->AsString())));
		}
	}
	return Out;
}

/** A number of an attribute text of the action list ("1.50"), or Default. */
float AttrNumber(const TSharedPtr<FJsonObject>& Attrs, const TCHAR* Name, float Default)
{
	FString Text;
	return Attrs.IsValid() && Attrs->TryGetStringField(Name, Text) && !Text.IsEmpty() ? FCString::Atof(*Text) : Default;
}

FSwcCondition ReadCondition(const TSharedPtr<FJsonObject>& Object)
{
	FSwcCondition Out;
	if (!Object.IsValid())
	{
		return Out;
	}
	Out.Type = Object->GetStringField(TEXT("type"));
	if (const TSharedPtr<FJsonObject>* Attrs = nullptr; Object->TryGetObjectField(TEXT("attrs"), Attrs))
	{
		for (const auto& Pair : (*Attrs)->Values)
		{
			Out.Attrs.Add(FString(Pair.Key.ToView()), Pair.Value->AsString());
		}
	}
	if (const TArray<TSharedPtr<FJsonValue>>* Children = nullptr; Object->TryGetArrayField(TEXT("children"), Children))
	{
		for (const TSharedPtr<FJsonValue>& Child : *Children)
		{
			Out.Children.Add(ReadCondition(Child->AsObject()));
		}
	}
	return Out;
}

/** The motion parameters of one parameter set; the values that it does not have stay those of Base. */
FSwcMotionRule ReadMotion(const TSharedPtr<FJsonObject>& Attrs, const FSwcMotionRule& Base)
{
	FSwcMotionRule Out = Base;
	if (!Attrs.IsValid())
	{
		return Out;
	}
	// A parameter set with no name has no condition (no warning of the JSON reader).
	Out.Condition.Reset();
	Attrs->TryGetStringField(TEXT("Name"), Out.Condition);
	Out.MotionMultiplier = AttrNumber(Attrs, TEXT("MotionMultiplier"), Base.MotionMultiplier);
	FString Text;
	if (Attrs->TryGetStringField(TEXT("Decel"), Text))
	{
		TArray<FString> Values;
		Text.ParseIntoArray(Values, TEXT(","));
		if (Values.Num() == 3)
		{
			Out.Decel = FVector(FCString::Atof(*Values[0]), FCString::Atof(*Values[1]), FCString::Atof(*Values[2]));
		}
	}
	if (Attrs->TryGetStringField(TEXT("InheritVelocity"), Text))
	{
		Out.bInheritVelocity = Text.Equals(TEXT("true"), ESearchCase::IgnoreCase);
	}
	Out.InheritVelocityMaxSpeed = AttrNumber(Attrs, TEXT("InheritVelocityMaxSpeed"), Base.InheritVelocityMaxSpeed);
	return Out;
}

FSwcFacingRule ReadFacing(const TSharedPtr<FJsonObject>& Attrs)
{
	FSwcFacingRule Out;
	if (!Attrs.IsValid())
	{
		return Out;
	}
	Attrs->TryGetStringField(TEXT("Name"), Out.Condition);
	Out.TurnRate = AttrNumber(Attrs, TEXT("TurnRate"), 0.0f);
	FString Text;
	Out.bExponential = Attrs->TryGetStringField(TEXT("UseExponentialTurnRate"), Text) && Text.Equals(TEXT("true"), ESearchCase::IgnoreCase);
	return Out;
}

// The ranged programs (stage P3.6, Combat/SwcRanged): the same fields as the reader of the tests (tests/ranged/ranged_load.h).
std::string Utf8(const FString& Text)
{
	return std::string(TCHAR_TO_UTF8(*Text));
}

std::string LowerUtf8(const FString& Text)
{
	return Utf8(Text.ToLower());
}

std::map<std::string, std::string> TextMap(const TSharedPtr<FJsonValue>& Value)
{
	std::map<std::string, std::string> Out;
	if (Value.IsValid() && Value->Type == EJson::Object)
	{
		for (const auto& Pair : Value->AsObject()->Values)
		{
			FString Text;
			if (Pair.Value->TryGetString(Text))
			{
				Out[Utf8(FString(Pair.Key.ToView()))] = Utf8(Text);
			}
		}
	}
	return Out;
}

swc::ranged::Node RangedNode(const TSharedPtr<FJsonValue>& Value)
{
	swc::ranged::Node Node;
	const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
	if (Row.Num() < 3)
	{
		return Node;
	}
	Node.type = Utf8(Row[0]->AsString());
	Node.attrs = TextMap(Row[1]);
	for (const TSharedPtr<FJsonValue>& Child : Row[2]->AsArray())
	{
		Node.children.push_back(RangedNode(Child));
	}
	return Node;
}

swc::ranged::ItemKind RangedItemKind(const FString& Type)
{
	using swc::ranged::ItemKind;
	return Type == TEXT("CAGE-TransitionInstant") ? ItemKind::transitionInstant
		: Type == TEXT("CAGE-TransitionBuffered") ? ItemKind::transitionBuffered
		: Type == TEXT("CAGE-RangedAttack")       ? ItemKind::rangedAttack
		: Type == TEXT("CAGE-CastSpell")          ? ItemKind::castSpell
		: Type == TEXT("CAGE-Named")              ? ItemKind::named
		: Type == TEXT("CAGE-PayManaCost")        ? ItemKind::payMana
		: Type == TEXT("CAGE-Damage")             ? ItemKind::damage
												  : ItemKind::other;
}

TSharedPtr<swc::ranged::Program> RangedProgram(const TArray<FString>& Tags, const TSharedPtr<FJsonObject>& Info)
{
	TSharedPtr<swc::ranged::Program> Program = MakeShared<swc::ranged::Program>();
	for (const FString& Tag : Tags)
	{
		Program->weaponTags.insert(LowerUtf8(Tag));
	}
	for (const auto& Pair : Info->GetObjectField(TEXT("fragments"))->Values)
	{
		swc::ranged::Fragment Fragment;
		Fragment.name = Utf8(FString(Pair.Key.ToView()));
		for (const TSharedPtr<FJsonValue>& EntryValue : Pair.Value->AsArray())
		{
			const TSharedPtr<FJsonObject> Entry = EntryValue->AsObject();
			swc::ranged::Variant Variant;
			for (const TSharedPtr<FJsonValue>& Tag : Entry->GetArrayField(TEXT("tags")))
			{
				Variant.tags.insert(LowerUtf8(Tag->AsString()));
			}
			Variant.record = Utf8(Entry->GetStringField(TEXT("fragment")));
			Variant.length = Number(Entry->TryGetField(TEXT("length")));
			Entry->TryGetBoolField(TEXT("loop"), Variant.loop);
			for (const TSharedPtr<FJsonValue>& RowValue : Entry->GetArrayField(TEXT("items")))
			{
				const TArray<TSharedPtr<FJsonValue>>& Row = RowValue->AsArray();
				swc::ranged::Item Item;
				Item.kind = RangedItemKind(Row[0]->AsString());
				Item.start = Number(Row[1]);
				Item.end = Number(Row[2], -1.0f);
				Item.params = TextMap(Row[3]);
				Variant.items.push_back(Item);
			}
			Fragment.variants.push_back(Variant);
		}
		Program->fragments[LowerUtf8(FString(Pair.Key.ToView()))] = Fragment;
	}
	for (const auto& Pair : Info->GetObjectField(TEXT("actions"))->Values)
	{
		const TSharedPtr<FJsonObject> Value = Pair.Value->AsObject();
		swc::ranged::Action Action;
		Action.name = Utf8(FString(Pair.Key.ToView()));
		Action.fragment = Utf8(Value->GetStringField(TEXT("fragment")));
		if (const TSharedPtr<FJsonValue> Activation = Value->TryGetField(TEXT("activation")); Activation.IsValid() && Activation->Type == EJson::Array)
		{
			Action.activation = RangedNode(Activation);
		}
		if (const TSharedPtr<FJsonValue> Deactivation = Value->TryGetField(TEXT("deactivation")); Deactivation.IsValid() && Deactivation->Type == EJson::Array)
		{
			Action.deactivation = RangedNode(Deactivation);
		}
		for (const TSharedPtr<FJsonValue>& RowValue : Value->GetArrayField(TEXT("activities")))
		{
			const TArray<TSharedPtr<FJsonValue>>& Row = RowValue->AsArray();
			swc::ranged::Activity Activity;
			Activity.type = Utf8(Row[0]->AsString());
			Activity.attrs = TextMap(Row[1]);
			if (Row[2]->Type == EJson::Array)
			{
				Activity.guard = RangedNode(Row[2]);
			}
			Action.activities.push_back(Activity);
		}
		Program->actions[LowerUtf8(FString(Pair.Key.ToView()))] = Action;
	}
	for (const TSharedPtr<FJsonValue>& AbilityValue : Info->GetArrayField(TEXT("abilities")))
	{
		const TSharedPtr<FJsonObject> Value = AbilityValue->AsObject();
		swc::ranged::Ability Ability;
		Ability.id = Utf8(Value->GetStringField(TEXT("id")));
		Ability.crc = static_cast<std::uint32_t>(Value->GetNumberField(TEXT("crc")));
		for (const TSharedPtr<FJsonValue>& StartValue : Value->GetArrayField(TEXT("actions")))
		{
			const TSharedPtr<FJsonObject> Start = StartValue->AsObject();
			Ability.starts.push_back({Utf8(Start->GetStringField(TEXT("action"))), Start->GetBoolField(TEXT("self"))});
		}
		Ability.cooldown = Number(Value->TryGetField(TEXT("cooldown")), -1.0f);
		Ability.minCooldown = Number(Value->TryGetField(TEXT("minCooldown")), -1.0f);
		Ability.costId = Utf8(Value->GetStringField(TEXT("costId")));
		Ability.manaCost = Number(Value->TryGetField(TEXT("manaCost")));
		Program->abilities.push_back(Ability);
	}
	for (const auto& Pair : Info->GetObjectField(TEXT("conditions"))->Values)
	{
		Program->conditions[LowerUtf8(FString(Pair.Key.ToView()))] = RangedNode(Pair.Value);
	}
	for (const TSharedPtr<FJsonValue>& Name : Info->GetArrayField(TEXT("start")))
	{
		Program->start.push_back(Utf8(Name->AsString()));
	}
	for (const TSharedPtr<FJsonValue>& Name : Info->GetArrayField(TEXT("order")))
	{
		Program->order.push_back(Utf8(Name->AsString()));
	}
	FString Ammo;
	Info->TryGetStringField(TEXT("ammo"), Ammo);
	Program->ammo = Utf8(Ammo);
	return Program;
}

void RangedShared(const TSharedPtr<FJsonObject>& Info, swc::ranged::Shared& Out)
{
	for (const TSharedPtr<FJsonValue>& RowValue : Info->GetArrayField(TEXT("shots")))
	{
		const TSharedPtr<FJsonObject> Row = RowValue->AsObject();
		swc::ranged::ShotRule Rule;
		Rule.profile = Utf8(Row->GetStringField(TEXT("profile")));
		Rule.attack = Utf8(Row->GetStringField(TEXT("attack")));
		Rule.attackCrc = static_cast<std::uint32_t>(Row->GetNumberField(TEXT("attackCrc")));
		Rule.row = Utf8(Row->GetStringField(TEXT("row")));
		Rule.rowCrc = static_cast<std::uint32_t>(Row->GetNumberField(TEXT("rowCrc")));
		Rule.charge = Number(Row->TryGetField(TEXT("charge")));
		Rule.spell = Utf8(Row->GetStringField(TEXT("spell")));
		Row->TryGetBoolField(TEXT("heavy"), Rule.heavy);
		Row->TryGetBoolField(TEXT("ability"), Rule.ability);
		if (const TSharedPtr<FJsonObject>* Flight = nullptr; Row->TryGetObjectField(TEXT("flight"), Flight))
		{
			Rule.flight.known = true;
			Rule.flight.speed = Number((*Flight)->TryGetField(TEXT("speed")));
			const TArray<TSharedPtr<FJsonValue>>& Accel = (*Flight)->GetArrayField(TEXT("accel"));
			for (int32 Axis = 0; Axis < 3 && Axis < Accel.Num(); ++Axis)
			{
				Rule.flight.accel[Axis] = Number(Accel[Axis]);
			}
			Rule.flight.life = Number((*Flight)->TryGetField(TEXT("life")));
			Rule.flight.accelDelay = Number((*Flight)->TryGetField(TEXT("accelDelay")));
			Rule.flight.maxRange = Number((*Flight)->TryGetField(TEXT("maxRange")));
			Rule.flight.radius = Number((*Flight)->TryGetField(TEXT("radius")));
			Rule.flight.nearRange = Number((*Flight)->TryGetField(TEXT("nearRange")));
		}
		Out.shots.push_back(Rule);
	}
	for (const auto& Pair : Info->GetObjectField(TEXT("spells"))->Values)
	{
		const TSharedPtr<FJsonObject> Value = Pair.Value->AsObject();
		swc::ranged::Spell Spell;
		Spell.id = Utf8(Value->GetStringField(TEXT("id")));
		Spell.types = Utf8(Value->GetStringField(TEXT("types")));
		Spell.attack = Utf8(Value->GetStringField(TEXT("attack")));
		Spell.attackCrc = static_cast<std::uint32_t>(Value->GetNumberField(TEXT("attackCrc")));
		Spell.castDistance = Number(Value->TryGetField(TEXT("castDistance")));
		Spell.castHeight = Number(Value->TryGetField(TEXT("castHeight")));
		Spell.radius = Number(Value->TryGetField(TEXT("radius")));
		Spell.height = Number(Value->TryGetField(TEXT("height")));
		Spell.duration = Number(Value->TryGetField(TEXT("duration")));
		Value->TryGetBoolField(TEXT("cameraLock"), Spell.cameraLock);
		FString ManaCost;
		Value->TryGetStringField(TEXT("manaCost"), ManaCost);
		Spell.manaCost = Utf8(ManaCost);
		Out.spells[Utf8(FString(Pair.Key.ToView()))] = Spell;
	}
	for (const auto& Pair : Info->GetObjectField(TEXT("manaCosts"))->Values)
	{
		Out.manaCosts[Utf8(FString(Pair.Key.ToView()))] = Number(Pair.Value);
	}
	for (const auto& Pair : Info->GetObjectField(TEXT("weapons"))->Values)
	{
		const TSharedPtr<FJsonObject> Value = Pair.Value->AsObject();
		Out.weapons[Utf8(FString(Pair.Key.ToView()))] = {Utf8(Value->GetStringField(TEXT("profile"))), Utf8(Value->GetStringField(TEXT("ammo")))};
	}
}
} // namespace

float FSwcActionItem::ClipTime(float Time, float Duration) const
{
	const float Value = Start + FMath::Max(Time - At, 0.0f) * Speed;
	if (Duration <= 0.0f)
	{
		return Value;
	}
	return bLoop ? FMath::Fmod(Value, Duration) : FMath::Min(Value, Duration);
}

float FSwcActions::FullIn(const FSwcActionFragment& Fragment)
{
	float Out = 0.0f;
	for (const TArray<FSwcActionItem>& Items : Fragment.Layers)
	{
		if (!Items.IsEmpty() && Items[0].Weight > 0.0f)
		{
			Out = FMath::Max(Out, Items[0].At + Items[0].BlendIn);
		}
	}
	return Out;
}

float FSwcActionFragment::MarkTime(const TCHAR* MarkName, float Default) const
{
	for (const swc::combat::Mark& Mark : Marks)
	{
		if (FString(UTF8_TO_TCHAR(Mark.name.c_str())).Equals(MarkName, ESearchCase::IgnoreCase))
		{
			return Mark.start;
		}
	}
	return Default;
}

bool FSwcActions::Load(FString& OutReport)
{
	const TSharedPtr<FJsonObject> Document = ReadObject(FPaths::Combine(ContentFolder(), TEXT("player/actions.json")));
	if (!Document.IsValid())
	{
		OutReport = TEXT("no player/actions.json (tools/convert-player.sh actions)");
		return false;
	}
	// The clips.
	if (const TSharedPtr<FJsonObject>* Objects = nullptr; Document->TryGetObjectField(TEXT("consumables"), Objects))
	{
		for (const auto& Pair : (*Objects)->Values)
		{
			ConsumableTags.Add(FString(Pair.Key.ToView()).ToLower(), Pair.Value->AsString());
		}
	}
	if (const TSharedPtr<FJsonObject>* Objects = nullptr; Document->TryGetObjectField(TEXT("clips"), Objects))
	{
		for (const auto& Pair : (*Objects)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			FSwcActionClip& Clip = Clips.AddDefaulted_GetRef();
			Clip.Name = FString(Pair.Key.ToView());
			ClipIndex.Add(Clip.Name, Clips.Num() - 1);
			if (!Info.IsValid() || Info->HasField(TEXT("error")))
			{
				continue;
			}
			Clip.Duration = Number(Info->TryGetField(TEXT("duration")));
			Clip.bAdditive = Info->GetBoolField(TEXT("additive"));
			Clip.bProp = Info->HasTypedField<EJson::Boolean>(TEXT("prop")) && Info->GetBoolField(TEXT("prop"));
			for (const TSharedPtr<FJsonValue>& Bone : Info->GetArrayField(TEXT("bones")))
			{
				Clip.Bones.Add(FName(*Bone->AsString()));
			}
			if (const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr; Info->TryGetArrayField(TEXT("root"), Keys))
			{
				for (const TSharedPtr<FJsonValue>& Key : *Keys)
				{
					const TArray<TSharedPtr<FJsonValue>>& Values = Key->AsArray();
					if (Values.Num() == 4)
					{
						Clip.Root.Add(FVector4f(Number(Values[0]), Number(Values[1]), Number(Values[2]), Number(Values[3])));
					}
				}
			}
		}
	}
	// The blend spaces (the same form as locomotion.json).
	TMap<FString, int32> SpaceIndex;
	if (const TSharedPtr<FJsonObject>* Objects = nullptr; Document->TryGetObjectField(TEXT("blendSpaces"), Objects))
	{
		for (const auto& Pair : (*Objects)->Values)
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
	// The fragments: the items of each layer with their times in the fragment.
	TMap<FString, int32> FragmentIndex;
	if (const TSharedPtr<FJsonObject>* Objects = nullptr; Document->TryGetObjectField(TEXT("fragments"), Objects))
	{
		for (const auto& Pair : (*Objects)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			FSwcActionFragment& Fragment = Fragments.AddDefaulted_GetRef();
			Fragment.Key = FString(Pair.Key.ToView());
			Fragment.Name = Info->GetStringField(TEXT("name"));
			Fragment.Tags = Info->GetStringField(TEXT("tags"));
			Fragment.BlendOut = static_cast<float>(Info->GetNumberField(TEXT("blendOut")));
			for (const TSharedPtr<FJsonValue>& LayerValue : Info->GetArrayField(TEXT("layers")))
			{
				TArray<FSwcActionItem>& Layer = Fragment.Layers.AddDefaulted_GetRef();
				for (const TSharedPtr<FJsonValue>& ItemValue : LayerValue->AsArray())
				{
					const TSharedPtr<FJsonObject> Data = ItemValue->AsObject();
					FSwcActionItem Item;
					const float Exit = Number(Data->TryGetField(TEXT("exit")));
					Item.Start = Number(Data->TryGetField(TEXT("start")));
					Item.BlendIn = Number(Data->TryGetField(TEXT("blendIn")));
					Item.Speed = FMath::Max(Number(Data->TryGetField(TEXT("speed")), 1.0f), 0.01f);
					Item.Weight = Number(Data->TryGetField(TEXT("weight")), 1.0f);
					Item.bLoop = Data->GetBoolField(TEXT("loop"));
					FString Name;
					if (Data->TryGetStringField(TEXT("blendSpace"), Name))
					{
						Item.BlendSpace = SpaceIndex.FindRef(Name, INDEX_NONE);
					}
					else if (Data->TryGetStringField(TEXT("clip"), Name) && !Name.IsEmpty())
					{
						Item.Clip = ClipIndex.FindRef(Name, INDEX_NONE);
					}
					// The exit time counts from the start of the item before; -1: at the end of its clip (an inference).
					if (!Layer.IsEmpty())
					{
						const FSwcActionItem& Before = Layer.Last();
						const float BeforeLength = Before.Clip != INDEX_NONE
							? FMath::Max(Clips[Before.Clip].Duration - Before.Start, 0.0f) / Before.Speed : 0.0f;
						Item.At = Before.At + (Exit < 0.0f ? BeforeLength : Exit);
					}
					else
					{
						Item.At = FMath::Max(Exit, 0.0f);
					}
					Layer.Add(Item);
				}
			}
			for (const TSharedPtr<FJsonValue>& MarkValue : Info->GetArrayField(TEXT("markers")))
			{
				const TArray<TSharedPtr<FJsonValue>>& Row = MarkValue->AsArray();
				if (Row.Num() == 3)
				{
					Fragment.Marks.push_back({TCHAR_TO_UTF8(*Row[0]->AsString()), Number(Row[1]), Row[2]->IsNull() ? -1.0f : Number(Row[2])});
				}
			}
			if (const TArray<TSharedPtr<FJsonValue>>* Homing = nullptr; Info->TryGetArrayField(TEXT("homing"), Homing))
			{
				for (const TSharedPtr<FJsonValue>& ClipValue : *Homing)
				{
					const TSharedPtr<FJsonObject> Clip = ClipValue->AsObject();
					FSwcHoming& Out = Fragment.Homing.AddDefaulted_GetRef();
					Out.Start = Number(Clip->TryGetField(TEXT("start")));
					Out.End = Number(Clip->TryGetField(TEXT("end")), -1.0f);
					Out.bMoveToTarget = Clip->HasTypedField<EJson::Boolean>(TEXT("moveToTarget")) && Clip->GetBoolField(TEXT("moveToTarget"));
					Out.MoveToDistance = Number(Clip->TryGetField(TEXT("moveToDistance")), 1.0f);
					Out.MoveToDuration = Number(Clip->TryGetField(TEXT("moveToDuration")), 0.2f);
					Out.MaxMoveDistance = Number(Clip->TryGetField(TEXT("maxMoveDistance")));
					Out.Radius = Number(Clip->TryGetField(TEXT("radius")));
					Out.MaxAngle = Number(Clip->TryGetField(TEXT("maxAngle")));
					Out.Height = Number(Clip->TryGetField(TEXT("height")));
					Out.TurnRate = Number(Clip->TryGetField(TEXT("turnRate")));
					Out.bAllowUpwards = Clip->HasTypedField<EJson::Boolean>(TEXT("allowUpwardsMovement")) && Clip->GetBoolField(TEXT("allowUpwardsMovement"));
				}
			}
			// The length: the end of the last item of layer 0 (the rule of the backend, protocol notes 4.32).
			if (!Fragment.Layers.IsEmpty() && !Fragment.Layers[0].IsEmpty())
			{
				const FSwcActionItem& Last = Fragment.Layers[0].Last();
				if (Last.IsEmpty())
				{
					Fragment.Length = Last.At;
				}
				else if (Last.Clip != INDEX_NONE && !Last.bLoop)
				{
					Fragment.Length = Last.At + FMath::Max(Clips[Last.Clip].Duration - Last.Start, 0.0f) / Last.Speed;
				}
			}
			FragmentIndex.Add(Fragment.Key, Fragments.Num() - 1);
		}
	}
	// The weapon tag sets.
	int32 Graphs = 0;
	for (const TSharedPtr<FJsonValue>& Value : Document->GetArrayField(TEXT("weapons")))
	{
		const TSharedPtr<FJsonObject> Info = Value->AsObject();
		const TSharedPtr<FSwcWeaponActions> Weapon = MakeShared<FSwcWeaponActions>();
		for (const TSharedPtr<FJsonValue>& Tag : Info->GetArrayField(TEXT("tags")))
		{
			Weapon->Tags.Add(Tag->AsString());
		}
		Weapon->ClipKey = Weapon->Tags.IsEmpty() ? FString(TEXT("unarmed")) : FString::Join(Weapon->Tags, TEXT("_")).ToLower();
		for (const TCHAR* Kind : {TEXT("stance"), TEXT("offhand"), TEXT("dodge"), TEXT("life"), TEXT("use")})
		{
			const TSharedPtr<FJsonObject>* Map = nullptr;
			if (Info->TryGetObjectField(Kind, Map))
			{
				for (const auto& Pair : (*Map)->Values)
				{
					if (const int32* Index = FragmentIndex.Find(Pair.Value->AsString()))
					{
						TMap<FString, int32>& Out = FCString::Strcmp(Kind, TEXT("stance")) == 0 ? Weapon->Stance
							: FCString::Strcmp(Kind, TEXT("offhand")) == 0                       ? Weapon->Offhand
							: FCString::Strcmp(Kind, TEXT("dodge")) == 0                         ? Weapon->Dodge
							: FCString::Strcmp(Kind, TEXT("life")) == 0                          ? Weapon->Life
																								 : Weapon->Use;
						Out.Add(FString(Pair.Key.ToView()), *Index);
					}
				}
			}
		}
		// The ranged program of a bow, a fire staff, or a life staff (stage P3.6).
		if (const TSharedPtr<FJsonObject>* Ranged = nullptr; Info->TryGetObjectField(TEXT("ranged"), Ranged))
		{
			Weapon->Ranged = RangedProgram(Weapon->Tags, *Ranged);
		}
		FString Key;
		Weapon->Draw = Info->TryGetStringField(TEXT("draw"), Key) ? FragmentIndex.FindRef(Key, INDEX_NONE) : INDEX_NONE;
		Weapon->Sheathe = Info->TryGetStringField(TEXT("sheathe"), Key) ? FragmentIndex.FindRef(Key, INDEX_NONE) : INDEX_NONE;
		// The block (stage P3.G): Block_UpperBody_Raise and Block_UpperBody of the tag set.
		if (const TSharedPtr<FJsonObject>* Block = nullptr; Info->TryGetObjectField(TEXT("block"), Block))
		{
			Weapon->BlockRaise = (*Block)->TryGetStringField(TEXT("raise"), Key) ? FragmentIndex.FindRef(Key, INDEX_NONE) : INDEX_NONE;
			Weapon->BlockLoop = (*Block)->TryGetStringField(TEXT("loop"), Key) ? FragmentIndex.FindRef(Key, INDEX_NONE) : INDEX_NONE;
		}
		// The weapon scopes (AST-202): the body record -> the weapon record of the attachment database.
		if (const TSharedPtr<FJsonObject>* Scope = nullptr; Info->TryGetObjectField(TEXT("weaponScope"), Scope))
		{
			if (const TSharedPtr<FJsonObject>* Records = nullptr; (*Scope)->TryGetObjectField(TEXT("records"), Records))
			{
				for (const auto& Pair : (*Records)->Values)
				{
					const int32 Body = FragmentIndex.FindRef(FString(Pair.Key.ToView()), INDEX_NONE);
					const TSharedPtr<FJsonObject> Entry = Pair.Value->AsObject();
					const int32 Record = Entry.IsValid() ? FragmentIndex.FindRef(Entry->GetStringField(TEXT("record")), INDEX_NONE) : INDEX_NONE;
					if (Body != INDEX_NONE && Record != INDEX_NONE)
					{
						Weapon->WeaponRecords.Add(Body, Record);
					}
				}
			}
		}
		if (const TArray<TSharedPtr<FJsonValue>>* Graph = nullptr; Info->TryGetArrayField(TEXT("graph"), Graph))
		{
			// The graph of the backend copy (the same reader as tests/actions/actions_tests.cpp).
			for (const TSharedPtr<FJsonValue>& ItemValue : *Graph)
			{
				const TSharedPtr<FJsonObject> Item = ItemValue->AsObject();
				swc::combat::Fragment Fragment;
				Fragment.name = TCHAR_TO_UTF8(*Item->GetStringField(TEXT("name")));
				Fragment.length = Number(Item->TryGetField(TEXT("length")));
				Fragment.canTransition = Number(Item->TryGetField(TEXT("canTransition")), -1.0f);
				const TArray<TSharedPtr<FJsonValue>>& Switch = Item->GetArrayField(TEXT("switch"));
				Fragment.switchBuffer = Switch.Num() == 2 ? Number(Switch[0], -1.0f) : -1.0f;
				Fragment.switchAt = Switch.Num() == 2 ? Number(Switch[1], -1.0f) : -1.0f;
				const TArray<TSharedPtr<FJsonValue>>* Markers = nullptr;
				if (Item->TryGetArrayField(TEXT("markers"), Markers))
				{
					for (const TSharedPtr<FJsonValue>& MarkerValue : *Markers)
					{
						const TArray<TSharedPtr<FJsonValue>>& Row = MarkerValue->AsArray();
						if (Row.Num() >= 2)
						{
							Fragment.markers.push_back({TCHAR_TO_UTF8(*Row[0]->AsString()), Number(Row[1], -1.0f)});
						}
					}
				}
				Weapon->Graph.fragments.push_back(Fragment);
				Weapon->GraphFragments.Add(Item->TryGetStringField(TEXT("fragment"), Key) ? FragmentIndex.FindRef(Key, INDEX_NONE) : INDEX_NONE);
				TArray<FSwcStrike>& Strikes = Weapon->Strikes.AddDefaulted_GetRef();
				const TArray<TSharedPtr<FJsonValue>>* StrikeValues = nullptr;
				if (Item->TryGetArrayField(TEXT("strikes"), StrikeValues))
				{
					for (const TSharedPtr<FJsonValue>& StrikeValue : *StrikeValues)
					{
						const TSharedPtr<FJsonObject> StrikeInfo = StrikeValue->AsObject();
						FSwcStrike& Strike = Strikes.AddDefaulted_GetRef();
						Strike.Key = StrikeInfo->GetStringField(TEXT("key"));
						Strike.Start = Number(StrikeInfo->TryGetField(TEXT("start")));
						Strike.End = Number(StrikeInfo->TryGetField(TEXT("end")));
						int32 DataIndex = -1;
						for (const TSharedPtr<FJsonValue>& ShapeValue : StrikeInfo->GetArrayField(TEXT("shapes")))
						{
							++DataIndex;
							const TSharedPtr<FJsonObject> ShapeInfo = ShapeValue->AsObject();
							const FString Type = ShapeInfo->GetStringField(TEXT("type"));
							if (Type != TEXT("sphere") && Type != TEXT("capsule") && Type != TEXT("box"))
							{
								continue; // MeleeAttackShapeCastTypeNone: no shape.
							}
							FSwcStrikeShape& Shape = Strike.Shapes.AddDefaulted_GetRef();
							Shape.DataIndex = DataIndex;
							Shape.Kind = Type == TEXT("sphere") ? FSwcStrikeShape::EKind::Sphere : Type == TEXT("box") ? FSwcStrikeShape::EKind::Box
																														: FSwcStrikeShape::EKind::Capsule;
							Shape.Radius = Number(ShapeInfo->TryGetField(TEXT("radius")));
							Shape.HalfHeight = Number(ShapeInfo->TryGetField(TEXT("halfHeight")));
							const auto Vector = [&ShapeInfo](const TCHAR* Name)
							{
								const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
								return ShapeInfo->TryGetArrayField(Name, Values) && Values->Num() == 3
									? FVector(Number((*Values)[0]), Number((*Values)[1]), Number((*Values)[2])) : FVector::ZeroVector;
							};
							Shape.Box = Vector(TEXT("box"));
							Shape.Offset = Vector(TEXT("offset"));
							Shape.Rotation = Vector(TEXT("rotation"));
							Shape.Joint = FName(*ShapeInfo->GetStringField(TEXT("joint")));
							Shape.bOffhand = ShapeInfo->GetBoolField(TEXT("offhand"));
							Shape.bEndAsCenter = ShapeInfo->GetBoolField(TEXT("endAsCenter"));
							Shape.bNoSight = ShapeInfo->GetBoolField(TEXT("noSight"));
						}
						const TArray<TSharedPtr<FJsonValue>>* RowValues = nullptr;
						if (StrikeInfo->TryGetArrayField(TEXT("rows"), RowValues))
						{
							for (const TSharedPtr<FJsonValue>& RowValue : *RowValues)
							{
								const TArray<TSharedPtr<FJsonValue>>& Row = RowValue->AsArray();
								if (Row.Num() == 4)
								{
									Strike.Rows.Add({IdsOf(Row[1]), IdsOf(Row[2]), static_cast<int32>(Number(Row[3]))});
								}
							}
						}
					}
				}
			}
			std::size_t Index = 0;
			for (const TSharedPtr<FJsonValue>& ItemValue : *Graph)
			{
				for (const TSharedPtr<FJsonValue>& RowValue : ItemValue->AsObject()->GetArrayField(TEXT("transitions")))
				{
					const TArray<TSharedPtr<FJsonValue>>& Row = RowValue->AsArray();
					if (Row.Num() < 5)
					{
						continue;
					}
					swc::combat::Transition Transition;
					Transition.buffered = Row[0]->AsString() == TEXT("buffered");
					Transition.start = Number(Row[1]);
					Transition.end = Row[2]->IsNull() ? -1.0f : Number(Row[2]);
					Transition.input = swc::combat::input_of(TCHAR_TO_UTF8(*Row[3]->AsString()));
					Transition.to = Weapon->Graph.index_of(TCHAR_TO_UTF8(*Row[4]->AsString()));
					// Stage P3.5: the ability of a start of an ability, the terms of a transition inside an ability (row 6; the same
					// reader as tests/actions/actions_tests.cpp graph_of).
					if (Transition.input == swc::combat::Input::ability && Row.Num() > 6 && Row[6]->Type == EJson::String)
					{
						Transition.ability = swc::combat::progression_id(TCHAR_TO_UTF8(*Row[6]->AsString()));
					}
					else if (Transition.input == swc::combat::Input::terms && Row.Num() > 6 && Row[6]->Type == EJson::Array)
					{
						for (const TSharedPtr<FJsonValue>& Term : Row[6]->AsArray())
						{
							std::vector<swc::combat::Literal> Literals;
							bool bKnown = true;
							for (const TSharedPtr<FJsonValue>& LiteralValue : Term->AsArray())
							{
								const TArray<TSharedPtr<FJsonValue>>& Parts = LiteralValue->AsArray();
								swc::combat::Literal Literal;
								const bool bCount = Parts.Num() == 3 && Parts[1]->Type == EJson::Number;
								bKnown = bKnown && Parts.Num() == 3
									&& swc::combat::literal_of(TCHAR_TO_UTF8(*Parts[0]->AsString()), bCount ? std::string() : std::string(TCHAR_TO_UTF8(*Parts[1]->AsString())),
										bCount ? static_cast<std::uint32_t>(Parts[1]->AsNumber()) : 0, Parts[2]->AsBool(), Literal);
								Literals.push_back(Literal);
							}
							if (bKnown)
							{
								Transition.terms.push_back(MoveTemp(Literals));
							}
						}
					}
					Weapon->Graph.fragments[Index].transitions.push_back(Transition);
				}
				++Index;
			}
			FString Start;
			Weapon->Graph.start = Info->TryGetStringField(TEXT("start"), Start) ? Weapon->Graph.index_of(TCHAR_TO_UTF8(*Start)) : -1;
			// Stage P3.5: the abilities of the weapon (the fragment of the action, the selections by the nodes and the perks, the
			// cooldown of the data, the shots of the casts of a projectile spell).
			const TArray<TSharedPtr<FJsonValue>>* AbilityValues = nullptr;
			if (Info->TryGetArrayField(TEXT("abilities"), AbilityValues))
			{
				for (const TSharedPtr<FJsonValue>& AbilityValue : *AbilityValues)
				{
					const TSharedPtr<FJsonObject> AbilityInfo = AbilityValue->AsObject();
					swc::combat::Ability Ability;
					Ability.name = TCHAR_TO_UTF8(*AbilityInfo->GetStringField(TEXT("ability")));
					Ability.id = static_cast<std::uint32_t>(AbilityInfo->GetNumberField(TEXT("crc")));
					Ability.fragment = Weapon->Graph.index_of(TCHAR_TO_UTF8(*AbilityInfo->GetStringField(TEXT("fragment"))));
					for (const TSharedPtr<FJsonValue>& SelectionValue : AbilityInfo->GetArrayField(TEXT("selections")))
					{
						const TArray<TSharedPtr<FJsonValue>>& Row = SelectionValue->AsArray();
						if (Row.Num() == 5)
						{
							Ability.selections.push_back({Weapon->Graph.index_of(TCHAR_TO_UTF8(*Row[0]->AsString())), IdsOf(Row[1]), IdsOf(Row[2]), IdsOf(Row[3]), IdsOf(Row[4])});
						}
					}
					Ability.cooldown = Number(AbilityInfo->TryGetField(TEXT("cooldown")));
					Ability.minCooldown = Number(AbilityInfo->TryGetField(TEXT("minCooldown")));
					const TArray<TSharedPtr<FJsonValue>>* ShotValues = nullptr;
					if (AbilityInfo->TryGetArrayField(TEXT("shots"), ShotValues))
					{
						for (const TSharedPtr<FJsonValue>& ShotValue : *ShotValues)
						{
							const TArray<TSharedPtr<FJsonValue>>& Row = ShotValue->AsArray();
							if (Row.Num() == 6)
							{
								Ability.shots.push_back({TCHAR_TO_UTF8(*Row[0]->AsString()), static_cast<std::uint32_t>(Row[3]->AsNumber()), IdsOf(Row[4]), IdsOf(Row[5])});
							}
						}
					}
					Weapon->Graph.abilities.push_back(MoveTemp(Ability));
				}
			}
			Graphs += Weapon->HasGraph() ? 1 : 0;
		}
		Weapons.Add(Weapon);
	}
	// The joints of the weapon skeletons: position (m) and rotation (w, x, y, z) in the model, through the space map of
	// stage 1.1 (the gear places of docs/GEAR.md use the same map).
	if (const TSharedPtr<FJsonObject>* Objects = nullptr; Document->TryGetObjectField(TEXT("weaponJoints"), Objects))
	{
		for (const auto& Model : (*Objects)->Values)
		{
			TMap<FName, FTransform>& Joints = WeaponJoints.Add(FString(Model.Key.ToView()).ToLower());
			for (const auto& Joint : Model.Value->AsObject()->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>& V = Joint.Value->AsArray();
				if (V.Num() == 7)
				{
					const FQuat Game(Number(V[4]), Number(V[5]), Number(V[6]), Number(V[3]));
					Joints.Add(FName(FString(Joint.Key.ToView())), FTransform(FQuat(-Game.X, Game.Y, -Game.Z, Game.W).GetNormalized(),
						FVector(Number(V[0]), -Number(V[1]), Number(V[2])) * 100.0));
				}
			}
		}
	}
	// The shared data of the ranged programs (stage P3.6): the shot rules with their flight, the spells, the mana costs, the weapons.
	if (const TSharedPtr<FJsonObject>* Ranged = nullptr; Document->TryGetObjectField(TEXT("ranged"), Ranged))
	{
		RangedData = swc::ranged::Shared();
		RangedShared(*Ranged, RangedData);
	}
	RecordIndex = FragmentIndex;
	// The stamina and the dodge (stage P2.5): the data of the backend copy, the encumbrance row, the loads of the armor items.
	if (const TSharedPtr<FJsonObject>* Stamina = nullptr; Document->TryGetObjectField(TEXT("stamina"), Stamina))
	{
		const TSharedPtr<FJsonObject> Info = (*Stamina)->GetObjectField(TEXT("data"));
		const TSharedPtr<FJsonObject> Component = Info->GetObjectField(TEXT("component"));
		const TSharedPtr<FJsonObject> Vitals = Info->GetObjectField(TEXT("vitals"));
		const TSharedPtr<FJsonObject> Dodge = Info->GetObjectField(TEXT("dodge"));
		StaminaData.min = Number(Component->TryGetField(TEXT("min")));
		StaminaData.max = Number(Component->TryGetField(TEXT("max")), 100.0f);
		StaminaData.regenRate = Number(Component->TryGetField(TEXT("regenRate")), 33.33f);
		StaminaData.maxWindedTime = Number(Component->TryGetField(TEXT("maxWindedTime")), 2.0f);
		StaminaData.regenDelay = Number(Vitals->TryGetField(TEXT("regenDelay")), 1.0f);
		StaminaData.regenDelayOnEmpty = Number(Vitals->TryGetField(TEXT("regenDelayOnEmpty")), 1.0f);
		StaminaData.dodgeExitDelay = Number(Dodge->TryGetField(TEXT("regenDelayOnExit")), 1.0f);
		const TCHAR* Classes[4] = {TEXT("light"), TEXT("medium"), TEXT("heavy"), TEXT("overburdened")};
		const TSharedPtr<FJsonObject> Load = Info->GetObjectField(TEXT("load"));
		for (int32 Index = 0; Index < 4; ++Index)
		{
			StaminaData.costMult[Index] = Number(Load->GetObjectField(TEXT("costMult"))->TryGetField(Classes[Index]), 1.0f);
			StaminaData.regenMult[Index] = Number(Load->GetObjectField(TEXT("regenMult"))->TryGetField(Classes[Index]), 1.0f);
		}
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			if (!Dodge->GetObjectField(TEXT("classes"))->TryGetObjectField(Classes[Index], Row))
			{
				continue;
			}
			const FString Name = (*Row)->GetStringField(TEXT("fragment"));
			const TSharedPtr<FJsonObject> Marks = Dodge->GetObjectField(TEXT("fragments"))->GetObjectField(Name);
			swc::combat::DodgeFragment Fragment;
			Fragment.name = TCHAR_TO_UTF8(*Name);
			Fragment.pay = Number(Marks->TryGetField(TEXT("pay")), 0.1f);
			Fragment.immuneEnd = Number(Marks->GetArrayField(TEXT("immune"))[1], 0.5f);
			Fragment.canTransition = Number(Marks->TryGetField(TEXT("canTransition")), 0.5f);
			Fragment.attackCancel = Number(Marks->TryGetField(TEXT("attackCancel")));
			for (const TSharedPtr<FJsonValue>& Window : Marks->GetArrayField(TEXT("cancel")))
			{
				const TArray<TSharedPtr<FJsonValue>>& W = Window->AsArray();
				swc::combat::DodgeFragment::Window Cancel;
				Cancel.start = Number(W[0]);
				if (!W[1]->IsNull())
				{
					Cancel.end = Number(W[1]);
				}
				Cancel.buffered = W[2]->AsBool();
				Fragment.cancel.push_back(Cancel);
			}
			StaminaData.dodges[Index] = Fragment;
			StaminaData.dodgeCosts[Index] = Number((*Row)->TryGetField(TEXT("cost")), 45.0f);
		}
		StaminaData.loaded = true;
		const TSharedPtr<FJsonObject> Encumbrance = (*Stamina)->GetObjectField(TEXT("encumbrance"));
		LoadMax = Number(Encumbrance->TryGetField(TEXT("equipLoadMax")), 500.0f);
		// The bag limit in tenths (EncumbranceBaseMax of the row Player: 2000; stage P3.3).
		EncumbranceMax = Number(Encumbrance->TryGetField(TEXT("encumbranceMax")), 2000.0f);
		RatioMedium = Number(Encumbrance->TryGetField(TEXT("ratioMedium")), 26.0f);
		RatioHeavy = Number(Encumbrance->TryGetField(TEXT("ratioHeavy")), 46.0f);
		RatioOverburdened = Number(Encumbrance->TryGetField(TEXT("ratioOverburdened")), 100.0f);
		for (const auto& Pair : (*Stamina)->GetObjectField(TEXT("itemLoads"))->Values)
		{
			ItemLoads.Add(FString(Pair.Key.ToView()), Number(Pair.Value));
		}
	}
	// The motion and the facing of the attack actions, and the named conditions.
	if (const TSharedPtr<FJsonObject>* Objects = nullptr; Document->TryGetObjectField(TEXT("conditions"), Objects))
	{
		for (const auto& Pair : (*Objects)->Values)
		{
			Conditions.Add(FString(Pair.Key.ToView()), ReadCondition(Pair.Value->AsObject()));
		}
	}
	if (const TSharedPtr<FJsonObject>* Objects = nullptr; Document->TryGetObjectField(TEXT("activities"), Objects))
	{
		for (const auto& Pair : (*Objects)->Values)
		{
			const TSharedPtr<FJsonObject> Info = Pair.Value->AsObject();
			FSwcActivity Activity;
			if (const TSharedPtr<FJsonObject>* Motion = nullptr; Info->TryGetObjectField(TEXT("AnimationDrivenMotion"), Motion))
			{
				Activity.MotionDefault = ReadMotion((*Motion)->GetObjectField(TEXT("default")), FSwcMotionRule());
				Activity.MotionDefault.Condition.Reset();
				for (const TSharedPtr<FJsonValue>& Conditional : (*Motion)->GetArrayField(TEXT("conditionals")))
				{
					Activity.Motion.Add(ReadMotion(Conditional->AsObject(), Activity.MotionDefault));
				}
			}
			for (const TCHAR* Kind : {TEXT("CameraRelativeFacing"), TEXT("InputRelativeFacing")})
			{
				const TSharedPtr<FJsonObject>* Facing = nullptr;
				if (!Info->TryGetObjectField(Kind, Facing))
				{
					continue;
				}
				const bool bCamera = FCString::Strcmp(Kind, TEXT("CameraRelativeFacing")) == 0;
				(bCamera ? Activity.CameraDefault : Activity.InputDefault) = ReadFacing((*Facing)->GetObjectField(TEXT("default")));
				for (const TSharedPtr<FJsonValue>& Conditional : (*Facing)->GetArrayField(TEXT("conditionals")))
				{
					(bCamera ? Activity.CameraFacing : Activity.InputFacing).Add(ReadFacing(Conditional->AsObject()));
				}
			}
			Activities.Add(FString(Pair.Key.ToView()), Activity);
		}
	}
	OutReport = FString::Printf(TEXT("actions: %d weapon tag sets (%d attack graphs), %d fragments, %d blend spaces, %d clips, %d actions, %d conditions"),
		Weapons.Num(), Graphs, Fragments.Num(), BlendSpaces.Num(), Clips.Num(), Activities.Num(), Conditions.Num());
	return !Weapons.IsEmpty();
}

int32 FSwcActions::ClipAssets(bool bFemale, TArray<FString>& OutPaths) const
{
	OutPaths.Init(FString(), Clips.Num());
	const TSharedPtr<FJsonObject> Imported = ReadObject(FPaths::Combine(ContentFolder(), TEXT("player/imported.json")));
	const TSharedPtr<FJsonObject>* Assets = nullptr;
	if (!Imported.IsValid() || !Imported->TryGetObjectField(TEXT("assets"), Assets))
	{
		return 0;
	}
	const FString Gender = bFemale ? TEXT("female") : TEXT("male");
	int32 Found = 0;
	// The AnimSequence of a clip of a pack is "clips<clip>" (the file stem and the animation name); of the base "<gender><clip>".
	const auto Take = [&](const TSharedPtr<FJsonObject>& Entry, const FString& Prefix)
	{
		const TArray<TSharedPtr<FJsonValue>>* Sequences = nullptr;
		if (!Entry.IsValid() || !Entry->TryGetArrayField(TEXT("AnimSequence"), Sequences))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Sequences)
		{
			const FString Path = Value->AsString();
			const FString Name = FPaths::GetBaseFilename(Path).ToLower();
			if (!Name.StartsWith(Prefix))
			{
				continue;
			}
			const int32* Index = ClipIndex.Find(Name.RightChop(Prefix.Len()));
			if (Index != nullptr && OutPaths[*Index].IsEmpty())
			{
				OutPaths[*Index] = Path;
				++Found;
			}
		}
	};
	for (const auto& Pair : (*Assets)->Values)
	{
		if (FString(Pair.Key.ToView()).StartsWith(TEXT("clips|") + Gender + TEXT("|")))
		{
			Take(Pair.Value->AsObject(), TEXT("clips"));
		}
	}
	if (const TSharedPtr<FJsonObject>* Base = nullptr; (*Assets)->TryGetObjectField(TEXT("base|") + Gender, Base))
	{
		Take(*Base, Gender);
	}
	return Found;
}

const FSwcWeaponActions* FSwcActions::Weapon(const FString& TagText) const
{
	TArray<FString> Wanted;
	TagText.ParseIntoArray(Wanted, TEXT(","));
	for (FString& Tag : Wanted)
	{
		Tag = Tag.TrimStartAndEnd().ToLower();
	}
	for (const TSharedPtr<FSwcWeaponActions>& Weapon : Weapons)
	{
		if (Weapon->Tags.Num() != Wanted.Num())
		{
			continue;
		}
		bool bSame = true;
		for (int32 Index = 0; Index < Wanted.Num() && bSame; ++Index)
		{
			bSame = Weapon->Tags[Index].ToLower() == Wanted[Index];
		}
		if (bSame)
		{
			return Weapon.Get();
		}
	}
	return nullptr;
}

swc::combat::LoadClass FSwcActions::LoadClassOf(const TArray<FString>& ArmorItems, float& OutLoad) const
{
	OutLoad = 0.0f;
	for (const FString& Item : ArmorItems)
	{
		OutLoad += FMath::FloorToFloat(ItemLoads.FindRef(Item.ToLower()));
	}
	return swc::combat::load_class_of(OutLoad, LoadMax, RatioMedium, RatioHeavy, RatioOverburdened);
}

const FTransform* FSwcActions::WeaponJoint(const FString& Model, FName Joint) const
{
	const TMap<FName, FTransform>* Joints = WeaponJoints.Find(Model.ToLower());
	return Joints != nullptr ? Joints->Find(Joint) : nullptr;
}

const FSwcWeaponActions* FSwcActions::WeaponOfGraph(const swc::combat::Graph* Graph) const
{
	for (const TSharedPtr<FSwcWeaponActions>& Weapon : Weapons)
	{
		if (&Weapon->Graph == Graph)
		{
			return Weapon.Get();
		}
	}
	return nullptr;
}

bool FSwcActions::Evaluate(const FSwcCondition& Condition, const FSwcConditionFacts& Facts, int32 Depth) const
{
	if (Depth > 16)
	{
		return false;
	}
	const FString& Type = Condition.Type;
	if (Type == TEXT("And"))
	{
		for (const FSwcCondition& Child : Condition.Children)
		{
			if (!Evaluate(Child, Facts, Depth + 1))
			{
				return false;
			}
		}
		return true;
	}
	if (Type == TEXT("Or"))
	{
		for (const FSwcCondition& Child : Condition.Children)
		{
			if (Evaluate(Child, Facts, Depth + 1))
			{
				return true;
			}
		}
		return false;
	}
	if (Type == TEXT("Not"))
	{
		return !Condition.Children.IsEmpty() && !Evaluate(Condition.Children[0], Facts, Depth + 1);
	}
	if (Type == TEXT("True"))
	{
		return true;
	}
	if (Type == TEXT("IfMannequinMarker"))
	{
		return Facts.Marks != nullptr
			&& swc::combat::mark_active(*Facts.Marks, TCHAR_TO_UTF8(*Condition.Attrs.FindRef(TEXT("MarkerName"))), Facts.Time);
	}
	if (Type == TEXT("IfMannequinTag"))
	{
		return Facts.Tags.Contains(Condition.Attrs.FindRef(TEXT("TagName")).ToLower());
	}
	if (Type == TEXT("IfCameraLockActive"))
	{
		return Facts.bCameraLock;
	}
	if (Type.Equals(TEXT("IfInput"), ESearchCase::IgnoreCase))
	{
		const FString Input = Condition.Attrs.FindRef(TEXT("InputName"));
		if (Input == TEXT("attack_primary_hold"))
		{
			return Facts.bAttackHold;
		}
		if (Input == TEXT("attack_primary") || Input == TEXT("attack_primary_virtual"))
		{
			return Facts.bAttackPress || (Input == TEXT("attack_primary_virtual") && Facts.bAttackHold);
		}
		return false;
	}
	if (Type == TEXT("IfFacingVelocityAngleDiff"))
	{
		if (Facts.FacingVelocityAngle < 0.0f)
		{
			return false;
		}
		const float Limit = FCString::Atof(*Condition.Attrs.FindRef(TEXT("AngleDiff")));
		const FString Comparison = Condition.Attrs.FindRef(TEXT("Comparison"));
		return Comparison == TEXT("LessThanOrEqual") ? Facts.FacingVelocityAngle <= Limit
			: Comparison == TEXT("LessThan")         ? Facts.FacingVelocityAngle < Limit
			: Comparison == TEXT("GreaterThan")      ? Facts.FacingVelocityAngle > Limit
													 : Facts.FacingVelocityAngle >= Limit;
	}
	// The other types (player settings, items, status effects, water, stamina, ...) are not known on the client: false.
	return false;
}

bool FSwcActions::Holds(const FString& Name, const FSwcConditionFacts& Facts) const
{
	const FSwcCondition* Condition = Conditions.Find(Name);
	return Condition != nullptr && Evaluate(*Condition, Facts, 0);
}

FSwcMotionRule FSwcActions::MotionRule(const FSwcActivity& Activity, const FSwcConditionFacts& Facts) const
{
	for (const FSwcMotionRule& Rule : Activity.Motion)
	{
		if (Holds(Rule.Condition, Facts))
		{
			return Rule;
		}
	}
	return Activity.MotionDefault;
}

const FSwcFacingRule* FSwcActions::FacingRule(const TArray<FSwcFacingRule>& Rules, const FSwcFacingRule& Default, const FSwcConditionFacts& Facts) const
{
	for (const FSwcFacingRule& Rule : Rules)
	{
		if (Holds(Rule.Condition, Facts))
		{
			return Rule.TurnRate > 0.0f ? &Rule : nullptr;
		}
	}
	return Default.TurnRate > 0.0f ? &Default : nullptr;
}

bool FSwcActions::RootDelta(const FSwcActionFragment& Fragment, float From, float To, FVector& OutDelta) const
{
	for (const TArray<FSwcActionItem>& Layer : Fragment.Layers)
	{
		// The item of the layer at the time To (the items of one layer follow one another).
		const FSwcActionItem* Item = nullptr;
		for (const FSwcActionItem& Candidate : Layer)
		{
			if (Candidate.At <= To + KINDA_SMALL_NUMBER)
			{
				Item = &Candidate;
			}
		}
		if (Item == nullptr || Item->Clip == INDEX_NONE || Clips[Item->Clip].Root.IsEmpty())
		{
			continue;
		}
		const FSwcActionClip& Clip = Clips[Item->Clip];
		const float A = Item->ClipTime(FMath::Max(From, Item->At), Clip.Duration);
		const float B = Item->ClipTime(To, Clip.Duration);
		OutDelta = B >= A ? Clip.RootAt(B) - Clip.RootAt(A) : FVector::ZeroVector;
		return true;
	}
	return false;
}
