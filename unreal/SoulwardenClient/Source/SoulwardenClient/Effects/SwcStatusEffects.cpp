#include "Effects/SwcStatusEffects.h"

#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Effects/SwcParticleEffects.h"

DEFINE_LOG_CATEGORY_STATIC(LogSwcStatusFx, Log, All);

namespace
{
struct FNode
{
	FString Effect;
	FString Material;
	FString Alias;
	FString Joint;
	bool bToJoint = true;
	bool bFollow = true;
	bool bIgnoreRotation = false;
	bool bKill = false;
	bool bKeep = false;
	float Scale = 1.0f;
	float Delay = 0.0f;
	bool bHasPosition = false;
	FVector Position = FVector::ZeroVector;
	bool bHasRotation = false;
	FVector Rotation = FVector::ZeroVector;
};

struct FRow
{
	FString Id;
	FString On;
	FString Off;
	bool bIgnoreZero = false;
};

struct FData
{
	TMap<uint32, FRow> Rows;
	TMap<FString, TMap<FString, TArray<FNode>>> Scripts;
	TMap<FString, TPair<TArray<FString>, TMap<FString, FString>>> Slices;
	FString PlayerList;
	FString NpcList;
	TMap<FString, FString> PlayerAliases;
	TMap<FString, FString> NpcAliases;
};

TMap<FString, FString> AliasMap(const TSharedPtr<FJsonObject>& Object)
{
	TMap<FString, FString> Out;
	if (Object.IsValid())
	{
		for (const auto& Pair : Object->Values)
		{
			Out.Add(FString(Pair.Key.ToView()), Pair.Value->AsString());
		}
	}
	return Out;
}

FVector Vector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool& bHas)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	bHas = Object->TryGetArrayField(Field, Values) && Values->Num() >= 3;
	return bHas ? FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber()) : FVector::ZeroVector;
}

const FData& Data()
{
	static FData Out;
	static bool bLoaded = false;
	if (bLoaded)
	{
		return Out;
	}
	bLoaded = true;
	const FString Path = FPaths::Combine(USwcEffectSubsystem::Folder(), TEXT("status-fx.json"));
	FString Text;
	TSharedPtr<FJsonObject> Doc;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) || !Doc.IsValid())
	{
		UE_LOG(LogSwcStatusFx, Warning, TEXT("status effects: no %s (tools/convert-particles.sh skills)"), *Path);
		return Out;
	}
	for (const auto& Pair : Doc->GetObjectField(TEXT("statuses"))->Values)
	{
		const TSharedPtr<FJsonObject> Row = Pair.Value->AsObject();
		FRow& Item = Out.Rows.Add(uint32(FCString::Atoi(*FString(Pair.Key.ToView()))));
		Item.Id = Row->GetStringField(TEXT("id"));
		Item.On = Row->GetStringField(TEXT("on"));
		Item.Off = Row->GetStringField(TEXT("off"));
		Item.bIgnoreZero = Row->GetBoolField(TEXT("ignoreZero"));
	}
	for (const auto& List : Doc->GetObjectField(TEXT("scripts"))->Values)
	{
		TMap<FString, TArray<FNode>>& Scripts = Out.Scripts.Add(FString(List.Key.ToView()));
		for (const auto& Script : List.Value->AsObject()->Values)
		{
			TArray<FNode>& Nodes = Scripts.Add(FString(Script.Key.ToView()));
			for (const TSharedPtr<FJsonValue>& Value : Script.Value->AsArray())
			{
				const TSharedPtr<FJsonObject> Fields = Value->AsObject();
				FNode& Node = Nodes.AddDefaulted_GetRef();
				Fields->TryGetStringField(TEXT("effect"), Node.Effect);
				Fields->TryGetStringField(TEXT("material"), Node.Material);
				Fields->TryGetStringField(TEXT("alias"), Node.Alias);
				Fields->TryGetStringField(TEXT("joint"), Node.Joint);
				Fields->TryGetBoolField(TEXT("toJoint"), Node.bToJoint);
				Fields->TryGetBoolField(TEXT("follow"), Node.bFollow);
				Fields->TryGetBoolField(TEXT("ignoreRotation"), Node.bIgnoreRotation);
				Fields->TryGetBoolField(TEXT("kill"), Node.bKill);
				Fields->TryGetBoolField(TEXT("keep"), Node.bKeep);
				double Number = 1.0;
				if (Fields->TryGetNumberField(TEXT("scale"), Number))
				{
					Node.Scale = float(Number);
				}
				if (Fields->TryGetNumberField(TEXT("delay"), Number))
				{
					Node.Delay = float(Number);
				}
				Node.Position = Vector(Fields, TEXT("position"), Node.bHasPosition);
				Node.Rotation = Vector(Fields, TEXT("rotation"), Node.bHasRotation);
			}
		}
	}
	for (const auto& Slice : Doc->GetObjectField(TEXT("slices"))->Values)
	{
		const TSharedPtr<FJsonObject> Item = Slice.Value->AsObject();
		TPair<TArray<FString>, TMap<FString, FString>>& Entry = Out.Slices.Add(FString(Slice.Key.ToView()));
		for (const TSharedPtr<FJsonValue>& List : Item->GetArrayField(TEXT("lists")))
		{
			Entry.Key.Add(List->AsString());
		}
		Entry.Value = AliasMap(Item->GetObjectField(TEXT("aliases")));
	}
	Out.PlayerList = Doc->GetStringField(TEXT("player"));
	Out.NpcList = Doc->GetStringField(TEXT("npc"));
	const TSharedPtr<FJsonObject> Aliases = Doc->GetObjectField(TEXT("aliases"));
	Out.PlayerAliases = AliasMap(Aliases->GetObjectField(TEXT("player_male")));
	Out.NpcAliases = AliasMap(Aliases->GetObjectField(TEXT("npc_skel")));
	UE_LOG(LogSwcStatusFx, Log, TEXT("status effects: %d status rows with a script, %d lists, %d creature slices"), Out.Rows.Num(),
		Out.Scripts.Num(), Out.Slices.Num());
	return Out;
}

/** The space map of stage 1.1: a position of the game (m) in Unreal (cm), and the Euler angles of the game (degrees, x first). */
FVector UnrealOffset(const FVector& Meters)
{
	return FVector(Meters.X, -Meters.Y, Meters.Z) * 100.0;
}

FQuat UnrealEuler(const FVector& Degrees)
{
	const FQuat Game = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Degrees.Z)) * FQuat(FVector::YAxisVector, FMath::DegreesToRadians(Degrees.Y))
		* FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Degrees.X));
	return FQuat(-Game.X, Game.Y, -Game.Z, Game.W).GetNormalized();
}
} // namespace

void FSwcStatusEffects::Note(const FString& Text)
{
	if (!Logged.Contains(Text))
	{
		Logged.Add(Text);
		UE_LOG(LogSwcStatusFx, Log, TEXT("status effects: %s"), *Text);
	}
}

void FSwcStatusEffects::Setup(bool bPlayer, const FString& SlicePath)
{
	const FData& D = Data();
	bReady = true;
	if (bPlayer)
	{
		Lists = {D.PlayerList};
		Aliases = D.PlayerAliases;
		return;
	}
	if (const TPair<TArray<FString>, TMap<FString, FString>>* Slice = D.Slices.Find(SlicePath.ToLower()))
	{
		Lists = Slice->Key;
		Aliases = Slice->Value;
	}
	if (Lists.IsEmpty())
	{
		Lists = {D.NpcList};
		Aliases = D.NpcAliases;
	}
}

void FSwcStatusEffects::Run(UWorld* World, USkeletalMeshComponent* Body, const FString& Script, TArray<FHeld>* Held)
{
	USwcEffectSubsystem* Effects = World != nullptr ? World->GetSubsystem<USwcEffectSubsystem>() : nullptr;
	if (Effects == nullptr || Body == nullptr || Script.IsEmpty())
	{
		return;
	}
	const FData& D = Data();
	const TArray<FNode>* Nodes = nullptr;
	for (const FString& List : Lists)
	{
		const TMap<FString, TArray<FNode>>* Scripts = D.Scripts.Find(List);
		if ((Nodes = Scripts != nullptr ? Scripts->Find(Script) : nullptr) != nullptr)
		{
			break;
		}
	}
	if (Nodes == nullptr)
	{
		Note(FString::Printf(TEXT("%s: not in the lists %s"), *Script, *FString::Join(Lists, TEXT(", "))));
		return;
	}
	for (const FNode& Node : *Nodes)
	{
		if (!Node.Material.IsEmpty())
		{
			Note(FString::Printf(TEXT("%s: the material override %s is not done"), *Script, *Node.Material));
			continue;
		}
		if (Node.Delay > 0.0f)
		{
			Note(FString::Printf(TEXT("%s: %s after %.2f s is not done"), *Script, *Node.Effect, Node.Delay));
			continue;
		}
		FName Socket = NAME_None;
		if (Node.bToJoint)
		{
			const FString* Alias = Node.Alias.IsEmpty() ? nullptr : Aliases.Find(Node.Alias);
			const FName Joint(*(Node.Joint.IsEmpty() ? (Alias != nullptr ? *Alias : FString()) : Node.Joint));
			if (!Joint.IsNone() && Body->GetBoneIndex(Joint) != INDEX_NONE)
			{
				Socket = Joint;
			}
			else if (!Joint.IsNone() || !Node.Alias.IsEmpty())
			{
				Note(FString::Printf(TEXT("%s: no joint %s (alias %s): the root of the body"), *Script, *Joint.ToString(), *Node.Alias));
			}
		}
		const FTransform Relative(Node.bHasRotation ? UnrealEuler(Node.Rotation) : FQuat::Identity,
			Node.bHasPosition ? UnrealOffset(Node.Position) : FVector::ZeroVector, FVector(Node.Scale));
		UNiagaraComponent* Component = Effects->StartAttached(Node.Effect, Body, Socket, Relative, Node.bFollow, FSwcEffectSettings());
		if (Component == nullptr)
		{
			Note(FString::Printf(TEXT("%s: %s has no system"), *Script, *Node.Effect));
			continue;
		}
		if (Node.bFollow && Node.bIgnoreRotation)
		{
			// AttachIgnoreRotation: the effect follows the place of the joint, not its turn [I].
			Component->SetUsingAbsoluteRotation(true);
			Component->SetWorldRotation(Relative.GetRotation());
		}
		Note(FString::Printf(TEXT("%s: %s at %s"), *Script, *Node.Effect, *Socket.ToString()));
		if (Held != nullptr)
		{
			Held->Add({Component, Node.bKill, Node.bKeep});
		}
		else
		{
			Effects->Keep(Component);
		}
	}
}

void FSwcStatusEffects::Update(UWorld* World, USkeletalMeshComponent* Body, const TArray<TPair<uint32, bool>>& Active)
{
	if (!bReady)
	{
		return;
	}
	USwcEffectSubsystem* Effects = World != nullptr ? World->GetSubsystem<USwcEffectSubsystem>() : nullptr;
	const FData& D = Data();
	TSet<uint32> Now;
	for (const TPair<uint32, bool>& Status : Active)
	{
		Now.Add(Status.Key);
	}
	// The ends: the held effects stop by their flags, then the Off script plays once.
	for (auto It = Live.CreateIterator(); It; ++It)
	{
		if (Now.Contains(It->Key))
		{
			continue;
		}
		for (const FHeld& Item : It->Value)
		{
			if (Effects != nullptr && Item.Component.IsValid())
			{
				if (Item.bKill || !Item.bKeep)
				{
					Effects->Stop(Item.Component.Get(), Item.bKill);
				}
				else
				{
					Effects->Keep(Item.Component.Get());
				}
			}
		}
		if (const FRow* Row = D.Rows.Find(It->Key))
		{
			UE_LOG(LogSwcStatusFx, Log, TEXT("status effects: %s ends (%s)"), *Row->Id, Row->Off.IsEmpty() ? TEXT("no Off script") : *Row->Off);
			Run(World, Body, Row->Off, nullptr);
		}
		It.RemoveCurrent();
	}
	// The starts: the On script with its held effects.
	for (const TPair<uint32, bool>& Status : Active)
	{
		if (Live.Contains(Status.Key))
		{
			continue;
		}
		TArray<FHeld>& Held = Live.Add(Status.Key);
		const FRow* Row = D.Rows.Find(Status.Key);
		if (Row == nullptr || (Row->bIgnoreZero && !Status.Value))
		{
			continue;
		}
		UE_LOG(LogSwcStatusFx, Log, TEXT("status effects: %s starts (%s)"), *Row->Id, Row->On.IsEmpty() ? TEXT("no On script") : *Row->On);
		Run(World, Body, Row->On, &Held);
	}
}

void FSwcStatusEffects::StopAll(UWorld* World)
{
	USwcEffectSubsystem* Effects = World != nullptr ? World->GetSubsystem<USwcEffectSubsystem>() : nullptr;
	for (const TPair<uint32, TArray<FHeld>>& Pair : Live)
	{
		for (const FHeld& Item : Pair.Value)
		{
			if (Effects != nullptr && Item.Component.IsValid())
			{
				Effects->Stop(Item.Component.Get(), true);
			}
		}
	}
	Live.Reset();
}
