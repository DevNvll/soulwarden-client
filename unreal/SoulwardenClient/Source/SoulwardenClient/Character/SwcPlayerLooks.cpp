#include "Character/SwcPlayerLooks.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "zlib.h"

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

/** @return The mask values of the "appearance" object of a weapon item (weapons.json): "#RRGGBB" texts are bytes over 255. */
FSwcItemMasks MasksOf(const TSharedPtr<FJsonObject>& Appearance)
{
	FSwcItemMasks Masks;
	if (!Appearance.IsValid())
	{
		return Masks;
	}
	Masks.bKnown = true;
	const auto Number = [&Appearance](const TCHAR* Name)
	{
		double Value = 0.0;
		return Appearance->TryGetNumberField(Name, Value) ? static_cast<float>(Value) : 0.0f;
	};
	const auto Color = [&Appearance](const TCHAR* Name)
	{
		FString Text;
		if (!Appearance->TryGetStringField(Name, Text) || !Text.StartsWith(TEXT("#")) || Text.Len() < 7)
		{
			return FLinearColor(0, 0, 0, 0);
		}
		const FColor Bytes = FColor::FromHex(Text.Mid(1, 6));
		return FLinearColor(Bytes.R / 255.0f, Bytes.G / 255.0f, Bytes.B / 255.0f, 0.0f);
	};
	static const TCHAR* const Groups[3] = {TEXT("R"), TEXT("G"), TEXT("B")};
	for (int32 Group = 0; Group < 3; ++Group)
	{
		Masks.Amount[Group] = Number(*FString::Printf(TEXT("Mask%s"), Groups[Group]));
		Masks.Override[Group] = Number(*FString::Printf(TEXT("Mask%sOverride"), Groups[Group]));
		Masks.Color[Group] = Color(*FString::Printf(TEXT("Mask%sColor"), Groups[Group]));
	}
	Masks.SpecAmount = Number(TEXT("MaskASpec"));
	Masks.SpecColor = Color(TEXT("MaskASpecColor"));
	Masks.GlossAmount = Number(TEXT("MaskAGloss"));
	Masks.GlossShift = Number(TEXT("MaskAGlossShift"));
	return Masks;
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

/** The mask values of a hair color row (HairColorData, research hair-color.md): the three colors, amounts, and overrides; the A
 *  specular and gloss amounts 0. The retail writer (0x695C2B0) puts all of them on the hair material in place of its XML values,
 *  zeros too (haircolor1: Mask_R 1, Mask_R_Override 0.04). bKnown is false for a row with no amounts (an older looks.json). */
FSwcItemMasks HairMasksOf(const TSharedPtr<FJsonObject>& Entry)
{
	FSwcItemMasks Masks;
	const TArray<TSharedPtr<FJsonValue>>* Amount = ArrayField(Entry, TEXT("amount"));
	const TArray<TSharedPtr<FJsonValue>>* Override = ArrayField(Entry, TEXT("override"));
	if (Amount == nullptr || Override == nullptr || Amount->Num() < 3 || Override->Num() < 3)
	{
		return Masks;
	}
	static const TCHAR* const Colors[3] = {TEXT("color"), TEXT("green"), TEXT("blue")};
	for (int32 Group = 0; Group < 3; ++Group)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = ArrayField(Entry, Colors[Group]);
		if (Values != nullptr && Values->Num() >= 3)
		{
			Masks.Color[Group] = FLinearColor((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber(), 0.0f);
		}
		Masks.Amount[Group] = (*Amount)[Group]->AsNumber();
		Masks.Override[Group] = (*Override)[Group]->AsNumber();
	}
	Masks.bKnown = true;
	return Masks;
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
	Weapons = ReadObject(FPaths::Combine(Content, TEXT("player/weapons.json")));
	const FString Catalog = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("SWC_GAMEDATA_DIR")), TEXT("item-catalog.json"));
	const TSharedPtr<FJsonObject> ItemCatalog = ReadObject(Catalog);
	Items.Reset();
	Weights.Reset();
	if (const TArray<TSharedPtr<FJsonValue>>* Rows = ArrayField(ItemCatalog, TEXT("items")))
	{
		// Columns of format 5: id ... weight (5, tenths), armorAppearanceM (15), armorAppearanceF (16).
		Items.Reserve(Rows->Num());
		Weights.Reserve(Rows->Num());
		for (const TSharedPtr<FJsonValue>& Row : *Rows)
		{
			const TArray<TSharedPtr<FJsonValue>>& Cells = Row->AsArray();
			TArray<FString>& Item = Items.AddDefaulted_GetRef();
			Item.Add(Cells.Num() > 0 ? Cells[0]->AsString() : FString());
			Item.Add(Cells.Num() > 16 ? Cells[15]->AsString() : FString());
			Item.Add(Cells.Num() > 16 ? Cells[16]->AsString() : FString());
			Weights.Add(Cells.Num() > 5 && Cells[5]->Type == EJson::Number ? static_cast<int32>(Cells[5]->AsNumber()) : 0);
		}
	}
	const TSharedPtr<FJsonObject> ImportedAssets = ObjectField(Imported, TEXT("assets"));
	const TSharedPtr<FJsonObject> WeaponItems = ObjectField(Weapons, TEXT("items"));
	OutReport = FString::Printf(TEXT("player looks: %d armor appearances, %d weapon items, %d imported assets, %d items of the catalog %s"),
		ObjectField(Looks, TEXT("armor")).IsValid() ? ObjectField(Looks, TEXT("armor"))->Values.Num() : 0,
		WeaponItems.IsValid() ? WeaponItems->Values.Num() : 0, ImportedAssets.IsValid() ? ImportedAssets->Values.Num() : 0, Items.Num(), *Catalog);
	return true;
}

FString FSwcPlayerLooks::ImportedObject(const FString& Key, const TCHAR* Class) const
{
	const TArray<TSharedPtr<FJsonValue>>* Objects = ArrayField(ObjectField(ObjectField(Imported, TEXT("assets")), Key), Class);
	return Objects != nullptr && Objects->Num() > 0 ? (*Objects)[0]->AsString() : FString();
}

TArray<FString> FSwcPlayerLooks::ImportedObjects(const FString& Key, const TCHAR* Class) const
{
	TArray<FString> Out;
	if (const TArray<TSharedPtr<FJsonValue>>* Objects = ArrayField(ObjectField(ObjectField(Imported, TEXT("assets")), Key), Class))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Objects)
		{
			Out.Add(Value->AsString());
		}
	}
	return Out;
}

FString FSwcPlayerLooks::ItemId(uint32 ItemIndex) const
{
	return ItemIndex > 0 && Items.IsValidIndex(static_cast<int32>(ItemIndex) - 1) && !Items[ItemIndex - 1].IsEmpty() ? Items[ItemIndex - 1][0] : FString();
}

int32 FSwcPlayerLooks::ItemWeight(uint32 ItemIndex) const
{
	return ItemIndex > 0 && Weights.IsValidIndex(static_cast<int32>(ItemIndex) - 1) ? Weights[ItemIndex - 1] : 0;
}

FString FSwcPlayerLooks::BaseMesh(bool bFemale) const
{
	return ImportedObject(bFemale ? TEXT("base|female") : TEXT("base|male"), TEXT("SkeletalMesh"));
}

FString FSwcPlayerLooks::IdleAnimation(bool bFemale) const
{
	// The base has each locomotion clip (Character/SwcLocomotion): the idle clip is "<gender>unarmed_idle".
	const FString Name = (bFemale ? TEXT("female") : TEXT("male")) + FString(TEXT("unarmed_idle"));
	const TArray<TSharedPtr<FJsonValue>>* Objects =
		ArrayField(ObjectField(ObjectField(Imported, TEXT("assets")), bFemale ? TEXT("base|female") : TEXT("base|male")), TEXT("AnimSequence"));
	if (Objects != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Objects)
		{
			if (FPaths::GetBaseFilename(Value->AsString()).Equals(Name, ESearchCase::IgnoreCase))
			{
				return Value->AsString();
			}
		}
	}
	return FString();
}

FSwcPlayerLook FSwcPlayerLooks::Resolve(const TArray<uint8>& Appearance, const TMap<uint32, uint32>& Equipment) const
{
	return ResolveRows(Appearance, Equipment, nullptr);
}

FSwcPlayerLook FSwcPlayerLooks::ResolveRows(const TArray<uint8>& Appearance, const TMap<uint32, uint32>& Equipment,
	const TMap<uint32, FString>* ArmorRows) const
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
	const TSharedPtr<FJsonObject> HairRow = Entry(ArrayField(HairSet, TEXT("colors")), Byte(5));
	const FLinearColor HairColor = ColorOf(HairRow);
	const TArray<TSharedPtr<FJsonValue>>* FacialColors = ArrayField(FacialSet, TEXT("colors"));
	const TSharedPtr<FJsonObject> FacialRow = Entry(FacialColors != nullptr && FacialColors->Num() > 0 ? FacialColors : ArrayField(HairSet, TEXT("colors")), Byte(6));
	const FLinearColor FacialColor = ColorOf(FacialRow);

	// The default appearance data of the gender: the naked body parts with the body material of the skin tone.
	TMap<FName, FSwcLookPart> Slots;
	const auto Put = [&Slots](const FString& Slot, const FString& Skin, const FString& Material, FLinearColor Tint = FLinearColor(0, 0, 0, 0),
		const FSwcItemMasks& Masks = FSwcItemMasks())
	{
		FSwcLookPart Part;
		Part.Slot = FName(*Slot);
		Part.Skin = Skin;
		Part.Material = Material;
		Part.Tint = Tint;
		Part.Masks = Masks;
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
	const auto PutHair = [&Put](const TCHAR* Slot, const TSharedPtr<FJsonObject>& Style, const FString& Variant, FLinearColor Tint,
		const FSwcItemMasks& Masks)
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
		Put(Slot, Skin, StringField(Style, TEXT("material")), Tint, Masks);
	};
	const FSwcItemMasks HairMasks = HairMasksOf(HairRow);
	const FSwcItemMasks FacialMasks = HairMasksOf(FacialRow);
	PutHair(TEXT("hair"), Hair, TEXT("0"), HairColor, HairMasks);
	PutHair(TEXT("facial_hair"), Facial, TEXT("0"), FacialColor, FacialMasks);

	// The armor of the paperdoll slots 0 to 4 (research menu-equipment.md, equipment.md 6.1).
	const TSharedPtr<FJsonObject> Armor = ObjectField(Looks, TEXT("armor"));
	bool bHideLegs = false;
	// The name of the armor appearance row of a slot: from the item of the wire and the item catalog (the world), or from the rows
	// of the menu document (ResolveMenu, AST-196 F3). @return False for a slot with no armor.
	const auto ArmorRowName = [this, &Equipment, ArmorRows, &Look](uint32 PaperdollSlot, FString& OutName, FString& OutItem)
	{
		if (ArmorRows != nullptr)
		{
			const FString* Name = ArmorRows->Find(PaperdollSlot);
			OutName = Name != nullptr ? *Name : FString();
			OutItem = TEXT("(menu document)");
			return Name != nullptr;
		}
		const uint32* ItemIndex = Equipment.Find(PaperdollSlot);
		if (ItemIndex == nullptr || *ItemIndex == 0)
		{
			return false;
		}
		if (!Items.IsValidIndex(static_cast<int32>(*ItemIndex) - 1))
		{
			Look.Notes.Add(FString::Printf(TEXT("paperdoll slot %u: item index %u is not in the item catalog"), PaperdollSlot, *ItemIndex));
			return false;
		}
		const TArray<FString>& Item = Items[*ItemIndex - 1];
		OutName = Item[Look.bFemale ? 2 : 1];
		OutItem = Item[0];
		return true;
	};
	for (uint32 PaperdollSlot = 0; PaperdollSlot < UE_ARRAY_COUNT(VisualSlots); ++PaperdollSlot)
	{
		FString AppearanceName;
		FString ItemName;
		if (!ArmorRowName(PaperdollSlot, AppearanceName, ItemName))
		{
			continue;
		}
		const TSharedPtr<FJsonObject> Row = ObjectField(Armor, AppearanceName);
		if (!Row.IsValid())
		{
			Look.Notes.Add(FString::Printf(TEXT("paperdoll slot %u: item %s has no armor appearance (%s)"), PaperdollSlot, *ItemName,
				AppearanceName.IsEmpty() ? TEXT("no name") : *AppearanceName));
			continue;
		}
		// The cloth attachments of the character file of the appearance (AppearanceCDF): a cape or a skirt, drawn as a
		// skinned part with its render skin (no cloth simulation yet).
		if (const TArray<TSharedPtr<FJsonValue>>* Cloth = ArrayField(Row, TEXT("cloth")))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Cloth)
			{
				const TArray<TSharedPtr<FJsonValue>>& Fields = Value->AsArray();
				if (Fields.Num() == 3)
				{
					Put(Fields[0]->AsString(), Fields[1]->AsString(), Fields[2]->AsString());
				}
			}
		}
		const FString Visual = VisualSlots[PaperdollSlot];
		const FString Skin1 = StringField(Row, TEXT("skin1"));
		const FString Skin2 = StringField(Row, TEXT("skin2"));
		// Skin1 and Skin2 replace the parts of the visual slot; a skin of the body (IsSkin1, IsSkin2) has the body material.
		Slots.Remove(FName(*(Visual + TEXT("1"))));
		Slots.Remove(FName(*(Visual + TEXT("2"))));
		// The mask values of the row go on the armor skins (not on a skin of the body with the body material).
		const FSwcItemMasks RowMasks = MasksOf(ObjectField(Row, TEXT("appearance")));
		if (!Skin1.IsEmpty())
		{
			const bool bBody = FlagField(Row, TEXT("isSkin1"));
			Put(Visual + TEXT("1"), Skin1, bBody ? BodyMaterial : StringField(Row, TEXT("material1")));
			Slots[FName(*(Visual + TEXT("1")))].Masks = bBody ? FSwcItemMasks() : RowMasks;
		}
		if (!Skin2.IsEmpty())
		{
			const bool bBody = FlagField(Row, TEXT("isSkin2"));
			const FString Material2 = StringField(Row, TEXT("material2"));
			Put(Visual + TEXT("2"), Skin2, bBody ? BodyMaterial : (Material2.IsEmpty() ? StringField(Row, TEXT("material1")) : Material2));
			Slots[FName(*(Visual + TEXT("2")))].Masks = bBody ? FSwcItemMasks() : RowMasks;
		}
		if (PaperdollSlot == 0)
		{
			if (FlagField(Row, TEXT("hideHair")))
			{
				Slots.Remove(TEXT("hair"));
			}
			else if (const FString* Variant = HairChopVariants.Find(StringField(Row, TEXT("hairChop"))))
			{
				PutHair(TEXT("hair"), Hair, *Variant, HairColor, HairMasks);
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
		// HideLegs counts only on the chest (a pants row with the flag does not hide itself).
		bHideLegs = bHideLegs || (PaperdollSlot == 1 && FlagField(Row, TEXT("hideLegs")));
	}
	if (bHideLegs)
	{
		// HideLegs of the chest hides both pants parts (gear.md "Hide rules": 0x6851BA0, 0x68F9660).
		Slots.Remove(TEXT("pants1"));
		Slots.Remove(TEXT("pants2"));
	}
	ResolveGear(Equipment, Look, Slots);
	for (TPair<FName, FSwcLookPart>& Slot : Slots)
	{
		Slot.Value.MeshPath = ImportedObject(Slot.Value.Skin + TEXT("|") + Slot.Value.Material, TEXT("SkeletalMesh"));
		Look.Parts.Add(Slot.Value);
	}
	return Look;
}

namespace
{
/** @return The index of the entry with the name in a list of looks.json, or -1. */
int32 IndexOfName(const TArray<TSharedPtr<FJsonValue>>* List, const FString& Name)
{
	if (List != nullptr)
	{
		for (int32 Index = 0; Index < List->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> Item = (*List)[Index]->AsObject();
			if (Item.IsValid() && StringField(Item, TEXT("name")).Equals(Name, ESearchCase::IgnoreCase))
			{
				return Index;
			}
		}
	}
	return -1;
}
} // namespace

FSwcPlayerLook FSwcPlayerLooks::ResolveMenu(const TArray<FString>& AppearanceNames, const TMap<uint32, uint32>& ArmorCrcs) const
{
	TArray<FString> Notes;
	const auto Name = [&AppearanceNames](int32 Field) { return AppearanceNames.IsValidIndex(Field) ? AppearanceNames[Field] : FString(); };
	// The names of the creation databases (gender, race, skinTone, hairstyle, facialHair, hairColor, facialHairColor) to the
	// indexes of the appearance state: the lists of looks.json. A name that is not there gives the first entry (the rule of
	// Entry) and a note. The eye color, the face mark, the scar, and the tattoo are not in looks.json (no part of the look).
	TArray<uint8> Appearance;
	Appearance.SetNumZeroed(12);
	const bool bFemale = Name(0).Equals(TEXT("Female"), ESearchCase::IgnoreCase);
	Appearance[0] = bFemale ? 1 : 0;
	const TSharedPtr<FJsonObject> Gender = ObjectField(ObjectField(Looks, TEXT("genders")), bFemale ? TEXT("Female") : TEXT("Male"));
	const auto Index = [&Notes, &Appearance](int32 Field, const TArray<TSharedPtr<FJsonValue>>* List, const FString& Value, const TCHAR* What)
	{
		const int32 Found = IndexOfName(List, Value);
		if (Found < 0 && !Value.IsEmpty() && !Value.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			Notes.Add(FString::Printf(TEXT("menu document: the %s %s is not in looks.json (the first entry)"), What, *Value));
		}
		Appearance[Field] = static_cast<uint8>(FMath::Clamp(Found, 0, 255));
		return Found;
	};
	const TArray<TSharedPtr<FJsonValue>>* Races = ArrayField(Gender, TEXT("races"));
	const TSharedPtr<FJsonObject> Race = Entry(Races, FMath::Max(0, Index(1, Races, Name(1), TEXT("race"))));
	const TSharedPtr<FJsonObject> HairSet = ObjectField(ObjectField(Looks, TEXT("hairSets")), StringField(Race, TEXT("hair")));
	const TSharedPtr<FJsonObject> FacialSet = ObjectField(ObjectField(Looks, TEXT("hairSets")), StringField(Race, TEXT("facial")));
	Index(2, ArrayField(ObjectField(Looks, TEXT("toneSets")), StringField(Race, TEXT("tones"))), Name(2), TEXT("skin tone"));
	Index(3, ArrayField(HairSet, TEXT("styles")), Name(3), TEXT("hairstyle"));
	Index(4, ArrayField(FacialSet, TEXT("styles")), Name(4), TEXT("facial hair"));
	Index(5, ArrayField(HairSet, TEXT("colors")), Name(5), TEXT("hair color"));
	const TArray<TSharedPtr<FJsonValue>>* FacialColors = ArrayField(FacialSet, TEXT("colors"));
	Index(6, FacialColors != nullptr && FacialColors->Num() > 0 ? FacialColors : ArrayField(HairSet, TEXT("colors")), Name(6), TEXT("facial hair color"));

	// The armor rows by the CRC-32 of the lower-case row name (the appearanceId of the map `equipment`, research
	// frontend-native.md FE5). An unknown or zero CRC: no armor in that slot (the naked part of the slot) and a note; no item.
	if (ArmorByCrc.Num() == 0)
	{
		if (const TSharedPtr<FJsonObject> Armor = ObjectField(Looks, TEXT("armor")))
		{
			for (const auto& Row : Armor->Values)
			{
				const FTCHARToUTF8 Lower(*FString(Row.Key.ToView()).ToLower());
				ArmorByCrc.Add(static_cast<uint32>(crc32(0, reinterpret_cast<const Bytef*>(Lower.Get()), Lower.Length())), FString(Row.Key.ToView()));
			}
		}
	}
	TMap<uint32, FString> Rows;
	for (const TPair<uint32, uint32>& Slot : ArmorCrcs)
	{
		const FString* Row = ArmorByCrc.Find(Slot.Value);
		if (Slot.Value == 0 || Row == nullptr)
		{
			Notes.Add(FString::Printf(TEXT("menu document: paperdoll slot %u: %s appearance id %08x: no armor"), Slot.Key,
				Slot.Value == 0 ? TEXT("the zero") : TEXT("an unknown"), Slot.Value));
			continue;
		}
		Rows.Add(Slot.Key, *Row);
	}
	FSwcPlayerLook Look = ResolveRows(Appearance, TMap<uint32, uint32>(), &Rows);
	Notes.Append(Look.Notes);
	Look.Notes = MoveTemp(Notes);
	return Look;
}

namespace
{
/** A rotation of the game (a quaternion w, x, y, z) in Unreal: the space map of stage 1.1, (x, y, z, w) -> (-x, y, -z, w). */
FQuat UnrealRotation(const FQuat& Game)
{
	return FQuat(-Game.X, Game.Y, -Game.Z, Game.W).GetNormalized();
}

FVector UnrealOffset(double X, double Y, double Z)
{
	return FVector(X * 100.0, -Y * 100.0, Z * 100.0);
}

double NumberAt(const TArray<TSharedPtr<FJsonValue>>* Values, int32 Index)
{
	return Values != nullptr && Values->IsValidIndex(Index) ? (*Values)[Index]->AsNumber() : 0.0;
}
} // namespace

FSwcAttachPlace FSwcPlayerLooks::Place(bool bFemale, const FString& Attachment, const TSharedPtr<FJsonObject>& Side, const TCHAR* Kind) const
{
	// The socket of the base character file of the gender (position m; rotation w, x, y, z), then the offset of the equip
	// database (position m; rotation in degrees about x, y, z, the x rotation first: CryEngine CreateRotationXYZ). Both go
	// through the space map of stage 1.1 (research gear.md, "Attachments and transforms").
	FSwcAttachPlace Out;
	const TSharedPtr<FJsonObject> Data = ObjectField(ObjectField(ObjectField(Weapons, TEXT("attachments")), bFemale ? TEXT("Female") : TEXT("Male")), Attachment);
	const TArray<TSharedPtr<FJsonValue>>* Position = ArrayField(Data, TEXT("position"));
	const TArray<TSharedPtr<FJsonValue>>* Rotation = ArrayField(Data, TEXT("rotation"));
	if (Attachment.IsEmpty() || !Data.IsValid() || Position == nullptr || Rotation == nullptr)
	{
		return Out;
	}
	const FTransform Socket(UnrealRotation(FQuat(NumberAt(Rotation, 1), NumberAt(Rotation, 2), NumberAt(Rotation, 3), NumberAt(Rotation, 0))),
		UnrealOffset(NumberAt(Position, 0), NumberAt(Position, 1), NumberAt(Position, 2)));
	const TArray<TSharedPtr<FJsonValue>>* OffsetPosition = ArrayField(Side, FString(Kind) + TEXT("Position"));
	const TArray<TSharedPtr<FJsonValue>>* OffsetDegrees = ArrayField(Side, FString(Kind) + TEXT("Rotation"));
	const FQuat GameOffset = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(NumberAt(OffsetDegrees, 2)))
		* FQuat(FVector::YAxisVector, FMath::DegreesToRadians(NumberAt(OffsetDegrees, 1)))
		* FQuat(FVector::XAxisVector, FMath::DegreesToRadians(NumberAt(OffsetDegrees, 0)));
	const FTransform Offset(UnrealRotation(GameOffset), UnrealOffset(NumberAt(OffsetPosition, 0), NumberAt(OffsetPosition, 1), NumberAt(OffsetPosition, 2)));
	Out.Name = Attachment;
	Out.Bone = FName(*StringField(Data, TEXT("bone")));
	// The equip offset is in the frame of the socket: socket x offset (Unreal order: the offset first).
	Out.Offset = Offset * Socket;
	return Out;
}

void FSwcPlayerLooks::ResolveGear(const TMap<uint32, uint32>& Equipment, FSwcPlayerLook& Look, TMap<FName, FSwcLookPart>& Slots) const
{
	// The weapons of the paperdoll slots 24 to 29 (weapons 1 to 3 and their off hand; research gear.md "From a paperdoll
	// entry to a weapon"). The attachments come from the equip database: the main-hand fields for the slots 24 to 26, the
	// off-hand fields for 27 to 29 (the shield has only off-hand fields).
	const TSharedPtr<FJsonObject> WeaponItems = ObjectField(Weapons, TEXT("items"));
	const TSharedPtr<FJsonObject> EquipTypes = ObjectField(Weapons, TEXT("equipTypes"));
	if (!WeaponItems.IsValid())
	{
		return;
	}
	// A marker time of a fragment (the names compare without case), or a default.
	const auto Marker = [](const TSharedPtr<FJsonObject>& Fragment, const TCHAR* Name, float Default = 0.0f)
	{
		if (const TSharedPtr<FJsonObject> Markers = ObjectField(Fragment, TEXT("markers")))
		{
			for (const auto& Pair : Markers->Values)
			{
				if (FString(Pair.Key.ToView()).Equals(Name, ESearchCase::IgnoreCase))
				{
					return static_cast<float>(Pair.Value->AsNumber());
				}
			}
		}
		return Default;
	};
	TMap<uint32, float> OffhandDrawTimes;
	const auto MakePart = [&](uint32 Slot, const FString& Item, const TSharedPtr<FJsonObject>& Data, bool bAccessory)
	{
		FSwcGearPart Part;
		Part.PaperdollSlot = Slot;
		Part.Item = Item;
		Part.bAccessory = bAccessory;
		Part.EquipType = StringField(Data, TEXT("equipType"));
		Part.Tags = StringField(Data, TEXT("tag"));
		Part.Model = StringField(Data, TEXT("model"));
		Part.MeshKey = StringField(Data, TEXT("key"));
		Part.Masks = MasksOf(ObjectField(Data, TEXT("appearance")));
		// A weapon with weapon clips is a skeletal mesh (AST-202), else a static mesh.
		Part.MeshPath = ImportedObject(Part.MeshKey, TEXT("SkeletalMesh"));
		Part.bSkeletal = !Part.MeshPath.IsEmpty();
		if (Part.bSkeletal)
		{
			Part.ClipPaths = ImportedObjects(Part.MeshKey, TEXT("AnimSequence"));
		}
		else
		{
			Part.MeshPath = ImportedObject(Part.MeshKey, TEXT("StaticMesh"));
		}
		Part.bHideSheathed = FlagField(Data, TEXT("hideSheathed"));
		Part.bAlwaysVisible = StringField(Data, TEXT("sheathMode")) == TEXT("ALWAYS_VISIBLE");
		if (FlagField(Data, TEXT("playerModel")))
		{
			// A model on the player skeleton (a gauntlet): its skins are parts of the body (gear.md, step 5).
			if (const TArray<TSharedPtr<FJsonValue>>* Skins = ArrayField(Data, TEXT("skins")))
			{
				for (int32 Index = 0; Index < Skins->Num(); ++Index)
				{
					const TArray<TSharedPtr<FJsonValue>>& Pair = (*Skins)[Index]->AsArray();
					FSwcLookPart Body;
					Body.Slot = FName(*FString::Printf(TEXT("weapon%u_skin%d"), Slot, Index + 1));
					Body.Skin = Pair.Num() > 0 ? Pair[0]->AsString() : FString();
					Body.Material = Pair.Num() > 1 ? Pair[1]->AsString() : FString();
					Slots.Add(Body.Slot, Body);
				}
			}
			return;
		}
		// The swap times of the tags of the weapon (weapons.json "timings": the draw and the sheathe fragments).
		const TSharedPtr<FJsonObject> Timing = ObjectField(ObjectField(Weapons, TEXT("timings")), StringField(Data, TEXT("tag")));
		Part.DrawTime = Marker(ObjectField(Timing, TEXT("draw")), TEXT("unsheathe_weapon"));
		Part.SheatheTime = Marker(ObjectField(Timing, TEXT("sheathe")), TEXT("sheathe_weapon"));
		OffhandDrawTimes.Add(Slot, Marker(ObjectField(Timing, TEXT("draw")), TEXT("unsheathe_offhand"), Part.DrawTime));
		const TSharedPtr<FJsonObject> Side = ObjectField(ObjectField(EquipTypes, Part.EquipType), Slot >= 27 && Slot <= 29 && !bAccessory ? TEXT("offHand") : TEXT("main"));
		Part.Sheathed = Place(Look.bFemale, StringField(Side, TEXT("sheathed")), Side, TEXT("sheath"));
		Part.Hand = Place(Look.bFemale, StringField(Side, TEXT("hand")), Side, TEXT("hand"));
		if (!Part.Sheathed.IsValid() || !Part.Hand.IsValid())
		{
			Look.Notes.Add(FString::Printf(TEXT("paperdoll slot %u: %s: no attachment of the equip type %s"), Slot, *Item, *Part.EquipType));
		}
		Look.Gear.Add(Part);
	};
	// The weapon slots 24 to 29, and the tool slots 33 to 36 (stage P5.2: 33 the logging axe, 34 the sickle, 35 the skinning knife,
	// 36 the pick; Soulwarden protocol notes 4.44). A tool is in the hand only in a gather (ASwcPlayerCharacter::SetToolInHand);
	// else it is hidden (an inference: the retail character shows no tool when it does not gather).
	const uint32 GearSlots[] = {24, 25, 26, 27, 28, 29, 33, 34, 35, 36};
	for (const uint32 Slot : GearSlots)
	{
		const uint32* ItemIndex = Equipment.Find(Slot);
		if (ItemIndex == nullptr || *ItemIndex == 0 || !Items.IsValidIndex(static_cast<int32>(*ItemIndex) - 1))
		{
			continue;
		}
		const FString& Item = Items[*ItemIndex - 1][0];
		const TSharedPtr<FJsonObject> Data = ObjectField(WeaponItems, Item);
		if (!Data.IsValid())
		{
			Look.Notes.Add(FString::Printf(TEXT("paperdoll slot %u: item %s has no weapon look"), Slot, *Item));
			continue;
		}
		const int32 Before = Look.Gear.Num();
		MakePart(Slot, Item, Data, false);
		if (Slot >= 33)
		{
			if (Look.Gear.Num() > Before)
			{
				Look.Gear.Last().bHideSheathed = true;
			}
			continue;
		}
		if (const TSharedPtr<FJsonObject> Accessory = ObjectField(Data, TEXT("accessory")))
		{
			MakePart(Slot, Item, Accessory, true);
		}
	}
	// An off-hand part moves with the action of its main weapon: Unsheathe_Offhand of its draw fragment (else its
	// Unsheathe_Weapon), and its sheathe marker. An accessory does not move.
	for (FSwcGearPart& Part : Look.Gear)
	{
		const FSwcGearPart* Main = Look.Gear.FindByPredicate([&Part](const FSwcGearPart& Other)
			{ return !Other.bAccessory && Other.PaperdollSlot + 3 == Part.PaperdollSlot; });
		if (Part.PaperdollSlot >= 27 && Part.PaperdollSlot <= 29 && Main != nullptr)
		{
			Part.DrawTime = OffhandDrawTimes.FindRef(Main->PaperdollSlot);
			Part.SheatheTime = Main->SheatheTime;
		}
	}
}

bool FSwcPlayerLooks::Preflight(const FSwcPlayerLook& Look, FString& OutText) const
{
	TArray<FString> Missing;
	int32 Parts = 0;
	int32 Gear = 0;
	for (const FSwcLookPart& Part : Look.Parts)
	{
		if (Part.MeshPath.IsEmpty())
		{
			Missing.Add(FString::Printf(TEXT("part %s %s|%s"), *Part.Slot.ToString(), *Part.Skin, *Part.Material));
		}
		else
		{
			++Parts;
		}
	}
	for (const FSwcGearPart& Part : Look.Gear)
	{
		if (Part.MeshPath.IsEmpty())
		{
			Missing.Add(FString::Printf(TEXT("weapon slot %u %s%s"), Part.PaperdollSlot, *Part.Item, Part.bAccessory ? TEXT(" (accessory)") : TEXT("")));
		}
		else
		{
			++Gear;
		}
	}
	const bool bBase = !BaseMesh(Look.bFemale).IsEmpty();
	if (!bBase)
	{
		Missing.Add(FString::Printf(TEXT("the base of the %s body"), Look.bFemale ? TEXT("female") : TEXT("male")));
	}
	OutText = FString::Printf(TEXT("%d of %d part(s), %d of %d weapon(s), the base %s"), Parts, Look.Parts.Num(), Gear, Look.Gear.Num(),
		bBase ? TEXT("imported") : TEXT("missing"));
	if (!Missing.IsEmpty())
	{
		OutText += FString::Printf(TEXT("; %d missing: %s (tools/prepare-looks.sh, or tools/convert-player.sh wanted and tools/import-player.sh)"),
			Missing.Num(), *FString::Join(Missing, TEXT(", ")));
	}
	return Missing.IsEmpty();
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
	TArray<TSharedPtr<FJsonValue>> WantedWeapons;
	if (const TArray<TSharedPtr<FJsonValue>>* Old = ArrayField(Wanted, TEXT("weapons")))
	{
		WantedWeapons = *Old;
	}
	TSet<FString> KnownWeapons;
	for (const TSharedPtr<FJsonValue>& Item : WantedWeapons)
	{
		KnownWeapons.Add(Item->AsString());
	}
	for (const FSwcGearPart& Part : Look.Gear)
	{
		if (Part.MeshPath.IsEmpty() && !KnownWeapons.Contains(Part.Item))
		{
			KnownWeapons.Add(Part.Item);
			WantedWeapons.Add(MakeShared<FJsonValueString>(Part.Item));
			++Added;
		}
	}
	Wanted->SetArrayField(TEXT("weapons"), WantedWeapons);
	// The clip packs of the weapon tags (stage P2.7): "clips|<gender>|<tags in lower case, joined by _>" in imported.json.
	TArray<TSharedPtr<FJsonValue>> WantedClips;
	if (const TArray<TSharedPtr<FJsonValue>>* Old = ArrayField(Wanted, TEXT("clips")))
	{
		WantedClips = *Old;
	}
	TSet<FString> KnownClips;
	for (const TSharedPtr<FJsonValue>& Pack : WantedClips)
	{
		const TArray<TSharedPtr<FJsonValue>>& Values = Pack->AsArray();
		if (Values.Num() == 2)
		{
			KnownClips.Add(Values[0]->AsString() + TEXT("|") + Values[1]->AsString());
		}
	}
	const FString Gender = Look.bFemale ? TEXT("Female") : TEXT("Male");
	// The unarmed pack (the dodge of player.adb) and the pack of each weapon.
	TArray<FString> PackTags = {TEXT("unarmed")};
	for (const FSwcGearPart& Part : Look.Gear)
	{
		if (!Part.bAccessory && !Part.Tags.IsEmpty() && Part.PaperdollSlot < 27)
		{
			PackTags.AddUnique(Part.Tags);
		}
	}
	for (const FString& PartTags : PackTags)
	{
		TArray<FString> Tags;
		PartTags.ParseIntoArray(Tags, TEXT(","));
		for (FString& Tag : Tags)
		{
			Tag = Tag.TrimStartAndEnd().ToLower();
		}
		const FString Key = TEXT("clips|") + Gender.ToLower() + TEXT("|") + FString::Join(Tags, TEXT("_"));
		if (!ImportedObject(Key, TEXT("AnimSequence")).IsEmpty() || KnownClips.Contains(Gender + TEXT("|") + PartTags))
		{
			continue;
		}
		KnownClips.Add(Gender + TEXT("|") + PartTags);
		WantedClips.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueString>(Gender), MakeShared<FJsonValueString>(PartTags)}));
		++Added;
	}
	Wanted->SetArrayField(TEXT("clips"), WantedClips);
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
