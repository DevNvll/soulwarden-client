#include "Character/SwcPlayerLooks.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
/** The visual slot of each paperdoll slot with a look (research equipment.md 2.2: m_paperdollVisualSlotMapping). */
const TCHAR* const VisualSlots[] = {TEXT("hat"), TEXT("shirt"), TEXT("gloves"), TEXT("pants"), TEXT("boots")};
/** The hair variant of a hair chop of a head armor: the variants of a style are base, front, back, brim, hood
 *  (the skin names of the hair databases). The map of the chop names to the variants is an inference. */
const TMap<FString, FString> HairChopVariants = {{TEXT("HELMFRONT"), TEXT("1")}, {TEXT("HELMBACK"), TEXT("2")},
	{TEXT("BRIM"), TEXT("3")}, {TEXT("HOOD"), TEXT("4")}};

FString EnvironmentPath(const TCHAR* Name, const FString& Fallback)
{
	const FString Value = FPlatformMisc::GetEnvironmentVariable(Name);
	return Value.IsEmpty() ? Fallback : Value;
}

FString WorkFolder()
{
	return EnvironmentPath(TEXT("SWC_WORK"), FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT("Games/soulwarden-client")));
}

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

TSharedPtr<FJsonObject> ObjectField(const TSharedPtr<FJsonObject>& Object, const FString& Name)
{
	const TSharedPtr<FJsonObject>* Field = nullptr;
	return Object.IsValid() && Object->TryGetObjectField(Name, Field) ? *Field : nullptr;
}

const TArray<TSharedPtr<FJsonValue>>* ArrayField(const TSharedPtr<FJsonObject>& Object, const FString& Name)
{
	const TArray<TSharedPtr<FJsonValue>>* Field = nullptr;
	return Object.IsValid() && Object->TryGetArrayField(Name, Field) ? Field : nullptr;
}

FString StringField(const TSharedPtr<FJsonObject>& Object, const FString& Name)
{
	FString Value;
	if (Object.IsValid())
	{
		Object->TryGetStringField(Name, Value);
	}
	return Value;
}

bool FlagField(const TSharedPtr<FJsonObject>& Object, const FString& Name)
{
	double Value = 0.0;
	bool bValue = false;
	return Object.IsValid() && ((Object->TryGetNumberField(Name, Value) && Value != 0.0) || (Object->TryGetBoolField(Name, bValue) && bValue));
}

/** @return Entry `Index` of a list, or entry 0 for an index that the list does not have (the retail client takes the
 *  first entry for a name that is not in its database, research appearance.md 2.1). */
TSharedPtr<FJsonObject> Entry(const TArray<TSharedPtr<FJsonValue>>* List, int32 Index)
{
	if (List == nullptr || List->Num() == 0)
	{
		return nullptr;
	}
	const int32 Safe = List->IsValidIndex(Index) ? Index : 0;
	return (*List)[Safe]->AsObject();
}

FLinearColor ColorOf(const TSharedPtr<FJsonObject>& Entry)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = ArrayField(Entry, TEXT("color"));
	if (Values == nullptr || Values->Num() < 3)
	{
		return FLinearColor(0, 0, 0, 0);
	}
	return FLinearColor((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber(), 1.0f);
}
} // namespace

bool FSwcPlayerLooks::Load(FString& OutReport)
{
	const FString Content = EnvironmentPath(TEXT("SWC_CONTENT"), FPaths::Combine(WorkFolder(), TEXT("content")));
	Looks = ReadObject(FPaths::Combine(Content, TEXT("player/looks.json")));
	if (!Looks.IsValid())
	{
		OutReport = FString::Printf(TEXT("no %s/player/looks.json (tools/convert-player.sh tables)"), *Content);
		return false;
	}
	Imported = ReadObject(FPaths::Combine(Content, TEXT("player/imported.json")));
	const FString Catalog = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_GAMEDATA_DIR")), TEXT("item-catalog.json"));
	const TSharedPtr<FJsonObject> ItemCatalog = ReadObject(Catalog);
	Items.Reset();
	if (const TArray<TSharedPtr<FJsonValue>>* Rows = ArrayField(ItemCatalog, TEXT("items")))
	{
		// Columns of format 5: id ... armorAppearanceM (15), armorAppearanceF (16).
		Items.Reserve(Rows->Num());
		for (const TSharedPtr<FJsonValue>& Row : *Rows)
		{
			const TArray<TSharedPtr<FJsonValue>>& Cells = Row->AsArray();
			TArray<FString>& Item = Items.AddDefaulted_GetRef();
			Item.Add(Cells.Num() > 0 ? Cells[0]->AsString() : FString());
			Item.Add(Cells.Num() > 16 ? Cells[15]->AsString() : FString());
			Item.Add(Cells.Num() > 16 ? Cells[16]->AsString() : FString());
		}
	}
	const TSharedPtr<FJsonObject> ImportedAssets = ObjectField(Imported, TEXT("assets"));
	OutReport = FString::Printf(TEXT("player looks: %d armor appearances, %d imported assets, %d items of the catalog %s"),
		ObjectField(Looks, TEXT("armor")).IsValid() ? ObjectField(Looks, TEXT("armor"))->Values.Num() : 0,
		ImportedAssets.IsValid() ? ImportedAssets->Values.Num() : 0, Items.Num(), *Catalog);
	return true;
}

FString FSwcPlayerLooks::ImportedObject(const FString& Key, const TCHAR* Class) const
{
	const TArray<TSharedPtr<FJsonValue>>* Objects = ArrayField(ObjectField(ObjectField(Imported, TEXT("assets")), Key), Class);
	return Objects != nullptr && Objects->Num() > 0 ? (*Objects)[0]->AsString() : FString();
}

FString FSwcPlayerLooks::BaseMesh(bool bFemale) const
{
	return ImportedObject(bFemale ? TEXT("base|female") : TEXT("base|male"), TEXT("SkeletalMesh"));
}

FString FSwcPlayerLooks::IdleAnimation(bool bFemale) const
{
	return ImportedObject(bFemale ? TEXT("base|female") : TEXT("base|male"), TEXT("AnimSequence"));
}

FSwcPlayerLook FSwcPlayerLooks::Resolve(const TArray<uint8>& Appearance, const TMap<uint32, uint32>& Equipment) const
{
	FSwcPlayerLook Look;
	const auto Byte = [&Appearance](int32 Index) { return Appearance.IsValidIndex(Index) ? static_cast<int32>(Appearance[Index]) : 0; };
	// Field 0 playerGender: 0 Male, 1 Female (protocol notes 4.5).
	Look.bFemale = Byte(0) == 1;
	const TSharedPtr<FJsonObject> Gender = ObjectField(ObjectField(Looks, TEXT("genders")), Look.bFemale ? TEXT("Female") : TEXT("Male"));
	const TSharedPtr<FJsonObject> Race = Entry(ArrayField(Gender, TEXT("races")), Byte(1));
	if (!Race.IsValid())
	{
		Look.Notes.Add(TEXT("looks.json has no races"));
		return Look;
	}
	const TSharedPtr<FJsonObject> ToneSets = ObjectField(Looks, TEXT("toneSets"));
	const TSharedPtr<FJsonObject> HairSets = ObjectField(Looks, TEXT("hairSets"));
	const TSharedPtr<FJsonObject> Tone = Entry(ArrayField(ToneSets, StringField(Race, TEXT("tones"))), Byte(2));
	const FString BodyMaterial = StringField(Tone, TEXT("body"));
	const FString HeadMaterial = StringField(Tone, TEXT("head"));
	const TSharedPtr<FJsonObject> HairSet = ObjectField(HairSets, StringField(Race, TEXT("hair")));
	const TSharedPtr<FJsonObject> FacialSet = ObjectField(HairSets, StringField(Race, TEXT("facial")));
	const TSharedPtr<FJsonObject> Hair = Entry(ArrayField(HairSet, TEXT("styles")), Byte(3));
	const TSharedPtr<FJsonObject> Facial = Entry(ArrayField(FacialSet, TEXT("styles")), Byte(4));
	const FLinearColor HairColor = ColorOf(Entry(ArrayField(HairSet, TEXT("colors")), Byte(5)));
	const TArray<TSharedPtr<FJsonValue>>* FacialColors = ArrayField(FacialSet, TEXT("colors"));
	const FLinearColor FacialColor = ColorOf(Entry(FacialColors != nullptr && FacialColors->Num() > 0 ? FacialColors : ArrayField(HairSet, TEXT("colors")), Byte(6)));

	// The default appearance data of the gender: the naked body parts with the body material of the skin tone.
	TMap<FName, FSwcLookPart> Slots;
	const auto Put = [&Slots](const FString& Slot, const FString& Skin, const FString& Material, FLinearColor Tint = FLinearColor(0, 0, 0, 0))
	{
		FSwcLookPart Part;
		Part.Slot = FName(*Slot);
		Part.Skin = Skin;
		Part.Material = Material;
		Part.Tint = Tint;
		Slots.Add(Part.Slot, Part);
	};
	if (const TSharedPtr<FJsonObject> Defaults = ObjectField(Gender, TEXT("slots")))
	{
		for (const auto& Slot : Defaults->Values)
		{
			const TSharedPtr<FJsonObject> Data = Slot.Value->AsObject();
			if (FlagField(Data, TEXT("visible")) && !StringField(Data, TEXT("skin")).IsEmpty())
			{
				Put(FString(Slot.Key.ToView()), StringField(Data, TEXT("skin")), BodyMaterial);
			}
		}
	}
	// The head of the race with the head material of the tone; the hair and the facial hair with their tints.
	Put(TEXT("head1"), StringField(Race, TEXT("head")), HeadMaterial);
	const auto PutHair = [&Put](const TCHAR* Slot, const TSharedPtr<FJsonObject>& Style, const FString& Variant, FLinearColor Tint)
	{
		const TSharedPtr<FJsonObject> Variants = ObjectField(Style, TEXT("variants"));
		if (!Variants.IsValid() || Variants->Values.Num() == 0)
		{
			return;
		}
		FString Skin = StringField(Variants, Variant);
		if (Skin.IsEmpty())
		{
			Skin = StringField(Variants, TEXT("0"));
		}
		Put(Slot, Skin, StringField(Style, TEXT("material")), Tint);
	};
	PutHair(TEXT("hair"), Hair, TEXT("0"), HairColor);
	PutHair(TEXT("facial_hair"), Facial, TEXT("0"), FacialColor);

	// The armor of the paperdoll slots 0 to 4 (research menu-equipment.md, equipment.md 6.1).
	const TSharedPtr<FJsonObject> Armor = ObjectField(Looks, TEXT("armor"));
	for (uint32 PaperdollSlot = 0; PaperdollSlot < UE_ARRAY_COUNT(VisualSlots); ++PaperdollSlot)
	{
		const uint32* ItemIndex = Equipment.Find(PaperdollSlot);
		if (ItemIndex == nullptr || *ItemIndex == 0)
		{
			continue;
		}
		if (!Items.IsValidIndex(static_cast<int32>(*ItemIndex) - 1))
		{
			Look.Notes.Add(FString::Printf(TEXT("paperdoll slot %u: item index %u is not in the item catalog"), PaperdollSlot, *ItemIndex));
			continue;
		}
		const TArray<FString>& Item = Items[*ItemIndex - 1];
		const FString& AppearanceName = Item[Look.bFemale ? 2 : 1];
		const TSharedPtr<FJsonObject> Row = ObjectField(Armor, AppearanceName);
		if (!Row.IsValid())
		{
			Look.Notes.Add(FString::Printf(TEXT("paperdoll slot %u: item %s has no armor appearance (%s)"), PaperdollSlot, *Item[0],
				AppearanceName.IsEmpty() ? TEXT("no name") : *AppearanceName));
			continue;
		}
		if (!StringField(Row, TEXT("cdf")).IsEmpty())
		{
			Look.Notes.Add(FString::Printf(TEXT("paperdoll slot %u: appearance %s has a character file of its own (AppearanceCDF): only its skins are used"),
				PaperdollSlot, *AppearanceName));
		}
		const FString Visual = VisualSlots[PaperdollSlot];
		const FString Skin1 = StringField(Row, TEXT("skin1"));
		const FString Skin2 = StringField(Row, TEXT("skin2"));
		// Skin1 and Skin2 replace the parts of the visual slot; a skin of the body (IsSkin1, IsSkin2) has the body material.
		Slots.Remove(FName(*(Visual + TEXT("1"))));
		Slots.Remove(FName(*(Visual + TEXT("2"))));
		if (!Skin1.IsEmpty())
		{
			Put(Visual + TEXT("1"), Skin1, FlagField(Row, TEXT("isSkin1")) ? BodyMaterial : StringField(Row, TEXT("material1")));
		}
		if (!Skin2.IsEmpty())
		{
			const FString Material2 = StringField(Row, TEXT("material2"));
			Put(Visual + TEXT("2"), Skin2, FlagField(Row, TEXT("isSkin2")) ? BodyMaterial : (Material2.IsEmpty() ? StringField(Row, TEXT("material1")) : Material2));
		}
		if (PaperdollSlot == 0)
		{
			if (FlagField(Row, TEXT("hideHair")))
			{
				Slots.Remove(TEXT("hair"));
			}
			else if (const FString* Variant = HairChopVariants.Find(StringField(Row, TEXT("hairChop"))))
			{
				PutHair(TEXT("hair"), Hair, *Variant, HairColor);
			}
			if (FlagField(Row, TEXT("hideFacialHair")))
			{
				Slots.Remove(TEXT("facial_hair"));
			}
			if (FlagField(Row, TEXT("hideHead")))
			{
				Slots.Remove(TEXT("head1"));
			}
		}
		if (FlagField(Row, TEXT("hideLegs")))
		{
			Look.Notes.Add(FString::Printf(TEXT("appearance %s has HideLegs: not applied (the rule is not known)"), *AppearanceName));
		}
	}
	for (TPair<FName, FSwcLookPart>& Slot : Slots)
	{
		Slot.Value.MeshPath = ImportedObject(Slot.Value.Skin + TEXT("|") + Slot.Value.Material, TEXT("SkeletalMesh"));
		Look.Parts.Add(Slot.Value);
	}
	return Look;
}

int32 FSwcPlayerLooks::WriteWanted(const FSwcPlayerLook& Look) const
{
	const FString Path = FPaths::Combine(WorkFolder(), TEXT("player/wanted.json"));
	TSharedPtr<FJsonObject> Wanted = ReadObject(Path);
	if (!Wanted.IsValid())
	{
		Wanted = MakeShared<FJsonObject>();
	}
	TArray<TSharedPtr<FJsonValue>> Parts;
	if (const TArray<TSharedPtr<FJsonValue>>* Old = ArrayField(Wanted, TEXT("parts")))
	{
		Parts = *Old;
	}
	TSet<FString> Known;
	for (const TSharedPtr<FJsonValue>& Part : Parts)
	{
		const TArray<TSharedPtr<FJsonValue>>& Values = Part->AsArray();
		if (Values.Num() == 3)
		{
			Known.Add(Values[1]->AsString() + TEXT("|") + Values[2]->AsString());
		}
	}
	int32 Added = 0;
	for (const FSwcLookPart& Part : Look.Parts)
	{
		if (!Part.MeshPath.IsEmpty() || Known.Contains(Part.Skin + TEXT("|") + Part.Material))
		{
			continue;
		}
		Known.Add(Part.Skin + TEXT("|") + Part.Material);
		Parts.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueString>(Look.bFemale ? TEXT("Female") : TEXT("Male")),
			MakeShared<FJsonValueString>(Part.Skin), MakeShared<FJsonValueString>(Part.Material)}));
		++Added;
	}
	if (Added > 0)
	{
		Wanted->SetArrayField(TEXT("parts"), Parts);
		FString Text;
		FJsonSerializer::Serialize(Wanted.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
		FFileHelper::SaveStringToFile(Text, *Path);
	}
	return Added;
}
