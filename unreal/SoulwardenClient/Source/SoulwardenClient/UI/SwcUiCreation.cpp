// The character creation of the front end (stage Q3, docs/FRONTEND.md "Q3, the character creation"): the host side of
// swc/creation.lua. StartIntro ("menu intro") shows the scene of the creation level FTUE_V2_CC (SWC_CONTENT/frontend/
// creation-scene.json, swc_convert.frontend creation-scene) in a second preview actor, gives the time ranges of its sequences to
// the script, and starts the screen. The look of the customizable character goes through the resolver of F3. The name check and
// the create use the login client (Net/SwcLoginClient: the validator and create routes) with the creation data writer
// (Menu/SwcCreationData.h).

#include "UI/SwcUiSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Character/SwcPlayerLooks.h"
#include "Menu/SwcCreationData.h"
#include "Menu/SwcMenuPreview.h"
#include "SwcBackendSubsystem.h"
#include "SwcUi/NameRule.h"
#include "SwcUi/Runtime.h"

namespace
{
using FArg = swc::ui::Runtime::Arg;

std::string Text8(const FString& Text)
{
	return std::string(TCHAR_TO_UTF8(*Text));
}

/** The slot keys of the menu look (the paperdoll slots 0 to 4: research frontend-native.md FE5). */
const TCHAR* const SlotKeys[] = {TEXT("hat"), TEXT("shirt"), TEXT("gloves"), TEXT("pants"), TEXT("boots")};

/** Reads "slot=id,slot=id" ("-": none) into the armor map of the resolver (slot index -> appearance id). */
TMap<uint32, uint32> ReadArmor(const FString& Text)
{
	TMap<uint32, uint32> Armor;
	TArray<FString> Entries;
	Text.ParseIntoArray(Entries, TEXT(","));
	for (const FString& Entry : Entries)
	{
		FString Key, Value;
		if (!Entry.Split(TEXT("="), &Key, &Value))
		{
			continue;
		}
		for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(SlotKeys); ++Slot)
		{
			if (Key == SlotKeys[Slot])
			{
				Armor.Add(static_cast<uint32>(Slot), static_cast<uint32>(FCString::Strtoui64(*Value, nullptr, 10)));
			}
		}
	}
	return Armor;
}
} // namespace

void USwcUiSubsystem::StartCreation(USwcBackendSubsystem* Backend)
{
	if (!Runtime.IsValid())
	{
		return;
	}
	UWorld* World = GEngine != nullptr && GEngine->GameViewport != nullptr ? GEngine->GameViewport->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}
	const FString Path = FPaths::Combine(ContentDir, TEXT("frontend/creation-scene.json"));
	if (!CreationPreview.IsValid())
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		CreationPreview = World->SpawnActor<ASwcMenuPreview>(FVector::ZeroVector, FRotator::ZeroRotator, Parameters);
		if (CreationPreview.IsValid())
		{
			CreationPreview->SetSourceFalloff(true);
			CreationPreview->LoadScene(Path, TEXT("Player_Entity_Int"), TEXT("Character_Create_Camera"), TEXT("#-Camera_Transitions"));
			// The SimpleAnimationComponent of Player_Entity_Int: character_create_player once (the body stands up and turns to the
			// camera; the entity faces away from it).
			CreationPreview->SetClipOnce(true);
		}
		CreationClips = FindMenuClips(FPaths::Combine(ContentDir, TEXT("player/imported.json")), TEXT("creation"));
	}
	if (APlayerController* Controller = World->GetFirstPlayerController(); Controller != nullptr && CreationPreview.IsValid())
	{
		Controller->SetViewTarget(CreationPreview.Get());
	}
	// The time ranges of the sequences of the record (the events of the script): "name=seconds;name=seconds".
	FString Ranges;
	FString Text;
	TSharedPtr<FJsonObject> Doc;
	const TSharedPtr<FJsonObject>* Sequences = nullptr;
	if (FFileHelper::LoadFileToString(Text, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) && Doc.IsValid()
		&& Doc->TryGetObjectField(TEXT("sequences"), Sequences))
	{
		for (const auto& Item : (*Sequences)->Values)
		{
			const TSharedPtr<FJsonObject> Record = Item.Value->AsObject();
			if (Record.IsValid())
			{
				const double Seconds = Record->GetNumberField(TEXT("end")) - Record->GetNumberField(TEXT("start"));
				Ranges += FString::Printf(TEXT("%s=%.3f;"), *Item.Key, Seconds);
			}
		}
	}
	else
	{
		UE_LOG(LogSwcUi, Warning, TEXT("ui: the creation: no scene record %s (python3 -B -m swc_convert.frontend creation-scene)"), *Path);
	}
	bCreating = false;
	CreationNames.Reset();
	CreationArmor.Reset();
	UE_LOG(LogSwcUi, Log, TEXT("ui: the creation starts (the scene of FTUE_V2_CC)"));
	Runtime->call("HostCreationSequences", {FArg::of(Text8(Ranges))});
	Runtime->call("CreationStart", {});
}

void USwcUiSubsystem::EndCreation()
{
	if (CreationPreview.IsValid())
	{
		CreationPreview->Destroy();
	}
	CreationPreview.Reset();
	bCreating = false;
	// The end of a preview actor clears the menu scene of the environment: the menu scene is built again (as at the menu start).
	EndPreview();
	SpawnPreview();
	bPreviewDirty = true;
}

void USwcUiSubsystem::HandleCreationRequest(const FString& Request, USwcBackendSubsystem* Backend)
{
	FString Kind = Request.Mid(9);   // after "creation "
	FString Rest;
	if (!Kind.Split(TEXT(" "), &Kind, &Rest))
	{
		Rest.Reset();
	}
	if (Kind == TEXT("look"))
	{
		// "creation look N1|...|N12 slot=id,... HELMET": the body of the customizable character with the resolver of F3.
		TArray<FString> Parts;
		Rest.ParseIntoArray(Parts, TEXT(" "));
		if (Parts.Num() < 1)
		{
			return;
		}
		Parts[0].ParseIntoArray(CreationNames, TEXT("|"), false);
		CreationArmor = ReadArmor(Parts.IsValidIndex(1) ? Parts[1] : FString());
		// HELMET 1: ForceHelmetHidden (cacappearancetab.luac hides the headgear on the appearance step, so that the face and the hair
		// show): no hat on the preview (AST-267: the helmet of the archetype hid each change of the face and the hair).
		if (Parts.IsValidIndex(2) && Parts[2] == TEXT("1"))
		{
			CreationArmor.Remove(0);   // the slot hat (SlotKeys)
		}
		const FSwcPlayerLooks* Looks = Backend != nullptr ? Backend->GetPlayerLooks() : nullptr;
		if (Looks == nullptr || !CreationPreview.IsValid() || CreationNames.Num() != swc::menu::kAppearanceFieldCount)
		{
			return;
		}
		const FSwcPlayerLook Look = Looks->ResolveMenu(CreationNames, CreationArmor);
		const FString& Clip = CreationClips[Look.bFemale ? 1 : 0];
		CreationPreview->ShowLook(Look, Looks->BaseMesh(Look.bFemale), Clip.IsEmpty() ? Looks->IdleAnimation(Look.bFemale) : Clip);
		UE_LOG(LogSwcUi, Log, TEXT("ui: the creation: the look %s, %d armor row(s), %d part(s)"), *Parts[0], CreationArmor.Num(), Look.Parts.Num());
	}
	else if (Kind == TEXT("validate"))
	{
		// "creation validate WORLD<TAB>NAME": the name check route; the result goes back with the name (a stale one is ignored).
		FString World, Name;
		if (!Rest.Split(TEXT("\t"), &World, &Name) || Backend == nullptr)
		{
			return;
		}
		TWeakObjectPtr<USwcUiSubsystem> Self(this);
		Backend->ValidateCharacterName(World, Name, [Self, Name](int32 Status, const FString& ErrorName)
			{
				USwcUiSubsystem* Ui = Self.Get();
				if (Ui == nullptr || !Ui->Runtime.IsValid())
				{
					return;
				}
				swc::menu::FGatewayReply Reply;
				Reply.Status = Status;
				Reply.ErrorName = Text8(ErrorName);
				std::string Key;
				const bool bValid = swc::menu::ReadValidateReply(Reply, Key) == swc::menu::ENameValidation::Valid;
				Ui->Runtime->call("HostCreationValidate", {FArg::of(Text8(Name)), FArg::of(bValid), FArg::of(Key)});
			});
	}
	else if (Kind == TEXT("create"))
	{
		// "creation create WORLD<TAB>NAME<TAB>COMPANY<TAB>BACKSTORY<TAB>FACTION<TAB>EQUIPMENT": the creation data of the last look.
		TArray<FString> Fields;
		Rest.ParseIntoArray(Fields, TEXT("\t"), false);
		if (Fields.Num() < 6 || Backend == nullptr || bCreating || CreationNames.Num() != swc::menu::kAppearanceFieldCount)
		{
			UE_LOG(LogSwcUi, Warning, TEXT("ui: the creation: no create (%s)"), bCreating ? TEXT("a create runs") : TEXT("no look or no field"));
			if (!bCreating && Runtime.IsValid())
			{
				Runtime->call("HostCreationResult", {FArg::of(std::string()), FArg::of(std::string("@mm_loginservices_CreateCharacterFailed")),
					FArg::of(std::string("@mm_loginservices_CreateCharacterFailed"))});
			}
			return;
		}
		swc::menu::FCreation Values;
		Values.PlayerName = Text8(Fields[1]);
		Values.CompanyName = Text8(Fields[2]);
		for (int32 Field = 0; Field < CreationNames.Num(); ++Field)
		{
			Values.Appearance[Field] = Text8(CreationNames[Field]);
		}
		Values.Backstory = Text8(Fields[3]);
		Values.StartingFaction = static_cast<uint8>(FCString::Atoi(*Fields[4]));
		Values.WorldId = Text8(Fields[0]);
		// The equipment of the archetype in the key order of the client (the script sends it in that order).
		TArray<FString> Entries;
		Fields[5].ParseIntoArray(Entries, TEXT(","));
		for (const FString& Entry : Entries)
		{
			FString Key, Value;
			if (Entry.Split(TEXT("="), &Key, &Value))
			{
				Values.Equipment.push_back({Text8(Key), static_cast<uint32>(FCString::Strtoui64(*Value, nullptr, 10))});
			}
		}
		const std::string Params = swc::menu::EncodeCreation(Values);
		bCreating = true;
		UE_LOG(LogSwcUi, Log, TEXT("ui: the creation: the create (%s, %d equipment entries, %d bytes of creation data)"), *Fields[3],
			static_cast<int32>(Values.Equipment.size()), static_cast<int32>(Params.size()));
		TWeakObjectPtr<USwcUiSubsystem> Self(this);
		Backend->CreateCharacter(Fields[0], Fields[1], UTF8_TO_TCHAR(Params.c_str()),
			[Self](int32 Status, const FString& ErrorName, const FString& CharacterId)
			{
				USwcUiSubsystem* Ui = Self.Get();
				if (Ui == nullptr || !Ui->Runtime.IsValid())
				{
					return;
				}
				Ui->bCreating = false;
				swc::menu::FGatewayReply Reply;
				Reply.Status = Status;
				Reply.ErrorName = Text8(ErrorName);
				const swc::menu::FCreateResult Result = swc::menu::ReadCreateReply(Reply, Text8(CharacterId));
				Ui->Runtime->call("HostCreationResult", {FArg::of(Result.CharacterId), FArg::of(Result.Title), FArg::of(Result.Message)});
			});
	}
	else if (Kind == TEXT("exit"))
	{
		UE_LOG(LogSwcUi, Log, TEXT("ui: the creation ends with no character (OnExit): back to the landing screen"));
		EndCreation();
	}
}
