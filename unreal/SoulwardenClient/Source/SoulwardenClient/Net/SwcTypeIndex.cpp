#include "Net/SwcTypeIndex.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/Codec/SwcRepMessages.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FSwcTypeIndex::Check(FString& OutReport)
{
	FString GameDirectory = FPlatformMisc::GetEnvironmentVariable(TEXT("SOULWARDEN_GAME_DIR"));
	if (GameDirectory.IsEmpty())
	{
		GameDirectory = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT("Games/soulwarden/game"));
	}
	const FString Path = FPaths::Combine(GameDirectory, TEXT("typeindex.json"));
	FString Text;
	TSharedPtr<FJsonObject> Document;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Document)
		|| !Document.IsValid() || !Document->TryGetArrayField(TEXT("typeIndex"), List))
	{
		OutReport = FString::Printf(TEXT("no type index at %s"), *Path);
		return false;
	}
	TArray<FString> Faults;
	for (const swc::net::MessageType& Type : swc::net::kUsedTypes)
	{
		const FString Expected(UTF8_TO_TCHAR(Type.typeId));
		const FString Found = static_cast<int32>(Type.index) < List->Num() ? (*List)[Type.index]->AsString() : FString();
		if (!Found.Equals(Expected, ESearchCase::IgnoreCase))
		{
			Faults.Add(FString::Printf(TEXT("%s: index %u has %s"), UTF8_TO_TCHAR(Type.name), Type.index, Found.IsEmpty() ? TEXT("nothing") : *Found));
		}
	}
	if (Faults.Num() > 0)
	{
		OutReport = FString::Printf(TEXT("the type index of %s is not the type index of the backend: %s"), *Path, *FString::Join(Faults, TEXT("; ")));
		return false;
	}
	OutReport = FString::Printf(TEXT("type index: %d types in %s; the %d types of the client have the indexes of the backend"), List->Num(), *Path,
		static_cast<int32>(swc::net::kUsedTypes.size()));
	return true;
}
