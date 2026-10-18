#include "Interact/SwcInteractor.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SwcBackendSubsystem.h"

namespace
{
FString ContentFolder()
{
	const FString Content = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_CONTENT"));
	if (!Content.IsEmpty())
	{
		return Content;
	}
	const FString Work = FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_WORK"));
	return FPaths::Combine(Work.IsEmpty() ? FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT("Games/soulwarden-client")) : Work,
		TEXT("content"));
}

uint64 HexNumber(const FString& Text)
{
	return FCString::Strtoui64(*Text, nullptr, 16);
}

swc::net::ActorId HexBytes(const FString& Text)
{
	swc::net::ActorId Out{};
	for (int32 Index = 0; Index + 1 < Text.Len() && Index / 2 < static_cast<int32>(Out.size()); Index += 2)
	{
		Out[static_cast<size_t>(Index / 2)] = static_cast<uint8>(FCString::Strtoi(*Text.Mid(Index, 2), nullptr, 16));
	}
	return Out;
}
} // namespace

bool FSwcInteractor::Load(FString& OutReport)
{
	const FString Path = FPaths::Combine(ContentFolder(), TEXT("player/interactions.json"));
	FString Text;
	TSharedPtr<FJsonObject> Document;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Document)
		|| !Document.IsValid() || !Document->TryGetArrayField(TEXT("templates"), List))
	{
		OutReport = FString::Printf(TEXT("interactions: no %s (tools/convert-player.sh interactions)"), *Path);
		return false;
	}
	Templates.clear();
	int32 Options = 0;
	for (const TSharedPtr<FJsonValue>& Value : *List)
	{
		const TSharedPtr<FJsonObject> Info = Value->AsObject();
		swc::interact::Template Template;
		Template.name = TCHAR_TO_UTF8(*Info->GetStringField(TEXT("name")));
		Template.asset = HexBytes(Info->GetStringField(TEXT("asset")));
		Template.sub = static_cast<uint32>(Info->GetNumberField(TEXT("sub")));
		Template.root = HexNumber(Info->GetStringField(TEXT("root")));
		for (const TSharedPtr<FJsonValue>& BoundValue : Info->GetArrayField(TEXT("bounds")))
		{
			const TSharedPtr<FJsonObject> BoundInfo = BoundValue->AsObject();
			swc::interact::Bound Bound;
			Bound.slot = static_cast<uint32>(BoundInfo->GetNumberField(TEXT("slot")));
			Bound.component = HexNumber(BoundInfo->GetStringField(TEXT("component")));
			Bound.entity = HexNumber(BoundInfo->GetStringField(TEXT("entity")));
			const TArray<TSharedPtr<FJsonValue>>& Position = BoundInfo->GetArrayField(TEXT("position"));
			for (int32 Axis = 0; Axis < 3 && Axis < Position.Num(); ++Axis)
			{
				Bound.position[Axis] = static_cast<float>(Position[Axis]->AsNumber());
			}
			Bound.rangeSq = static_cast<float>(BoundInfo->GetNumberField(TEXT("rangeSq")));
			for (const TSharedPtr<FJsonValue>& OptionValue : BoundInfo->GetArrayField(TEXT("options")))
			{
				const TSharedPtr<FJsonObject> OptionInfo = OptionValue->AsObject();
				swc::interact::Option Option;
				Option.name = TCHAR_TO_UTF8(*OptionInfo->GetStringField(TEXT("name")));
				Option.id = static_cast<uint32>(OptionInfo->GetNumberField(TEXT("id")));
				Option.action = static_cast<uint8>(OptionInfo->GetNumberField(TEXT("action")));
				Option.committed = OptionInfo->GetBoolField(TEXT("committed"));
				Option.delay = static_cast<float>(OptionInfo->GetNumberField(TEXT("delay")));
				Option.hold = static_cast<float>(OptionInfo->GetNumberField(TEXT("hold")));
				Option.entity = HexNumber(OptionInfo->GetStringField(TEXT("entity")));
				Bound.options.push_back(Option);
				++Options;
			}
			Template.bounds.push_back(Bound);
		}
		Templates.push_back(Template);
	}
	OutReport = FString::Printf(TEXT("interactions: %d templates of server objects, %d options (%s)"), static_cast<int32>(Templates.size()), Options, *Path);
	return !Templates.empty();
}

void FSwcInteractor::Update(const FSwcRepConnection::FSnapshot& Snapshot, FSwcRepConnection& Connection, const swc::net::WorldPosition& Feet,
	bool bLocked, double Now)
{
	const swc::net::RepClient::OwnEntity& Own = Snapshot.Own;
	if (!bSeeded)
	{
		bSeeded = true;
		SeenCancels = Own.interactionCancels;
		SeenResults = Own.interactionResults;
	}
	// The replies and the ends of the session first: they belong to the request that is out now.
	if (Own.interactionCancels != SeenCancels)
	{
		SeenCancels = Own.interactionCancels;
		if (Use.phase() != swc::interact::Use::Phase::idle)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the server ended the use of %s (3746)"), UTF8_TO_TCHAR(Use.option().name.c_str()));
		}
		Use.server_cancel();
	}
	if (Own.interactionResults != SeenResults)
	{
		SeenResults = Own.interactionResults;
		UE_LOG(LogSwcBackend, Log, TEXT("interact: the reply to %s: %s"), UTF8_TO_TCHAR(Use.option().name.c_str()),
			Own.lastInteractionResult == 1 ? TEXT("accepted") : TEXT("refused"));
		Use.reply(Own.lastInteractionResult, Now);
	}
	// The focus: the objects of the server with a place.
	std::vector<swc::interact::Object> Objects;
	for (const TPair<uint32, swc::net::RepClient::OtherEntity>& Pair : Snapshot.Objects)
	{
		swc::interact::Object Object;
		Object.interest = Pair.Key;
		Object.actor = Pair.Value.actor;
		Object.sliceAsset = Pair.Value.sliceAsset;
		Object.sliceSub = Pair.Value.sliceSubId;
		Object.place = Pair.Value.place;
		Objects.push_back(Object);
	}
	if (Use.phase() == swc::interact::Use::Phase::idle)
	{
		Focus = bLocked ? std::nullopt : swc::interact::focus(Templates, Objects, Feet);
		FocusName = Focus ? FString(UTF8_TO_TCHAR(Focus->object->name.c_str())) : FString();
	}
	if (bLocked)
	{
		Use.abort();
	}
	// The key E.
	if (bKeyDown && !bKeyWas && Focus && !bLocked)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: E at %s (interest %u, %.1f m): %s, action %u, delay %.1f s, hold %.1f s"), *FocusName, Focus->interest,
			FMath::Sqrt(Focus->distanceSq), UTF8_TO_TCHAR(Focus->option->name.c_str()), Focus->option->action, Focus->option->delay, Focus->option->hold);
		Use.press(Now, *Focus);
	}
	else if (!bKeyDown && bKeyWas)
	{
		Use.release(Now);
	}
	bKeyWas = bKeyDown;
	const uint32 Executions = Use.executions();
	Use.tick(Now);
	if (Use.executions() != Executions)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: the execution of %s"), UTF8_TO_TCHAR(Use.option().name.c_str()));
	}
	const swc::interact::Use::Effects Effects = Use.take();
	if (Effects.requestChanged)
	{
		Connection.SetInteraction(Effects.request);
		if (!Effects.request)
		{
			UE_LOG(LogSwcBackend, Log, TEXT("interact: the request ends with no message (the use completed)"));
		}
	}
	if (Effects.cancel)
	{
		UE_LOG(LogSwcBackend, Log, TEXT("interact: the client cancels the use of %s (2473)"), UTF8_TO_TCHAR(Use.option().name.c_str()));
		Connection.CancelInteraction();
	}
}

FString FSwcInteractor::DebugText(double Now) const
{
	if (!Focus)
	{
		return FString();
	}
	const TCHAR* Phase = Use.phase() == swc::interact::Use::Phase::holding ? TEXT("hold") : Use.phase() == swc::interact::Use::Phase::requested ? TEXT("use")
		: Use.phase() == swc::interact::Use::Phase::executed ? TEXT("done") : TEXT("E");
	return FString::Printf(TEXT("%s %s (%s), %.0f %%"), Phase, UTF8_TO_TCHAR(Focus->option->name.c_str()), *FocusName, Use.progress(Now) * 100.0f);
}
