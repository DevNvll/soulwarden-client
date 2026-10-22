#include "Character/SwcLookMaterials.h"

#include "Character/SwcPlayerLooks.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
/** @return True for a material of the surface parents with the overlay masks of %OVERLAY_MASK (MaskMode 1). */
bool HasOverlayMasks(const UMaterialInterface* Material)
{
	float Mode = 0.0f;
	return Material != nullptr && Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("MaskMode")), Mode) &&
		Mode > 0.5f && Mode < 1.5f;
}

FLinearColor VectorOf(const UMaterialInterface* Material, const TCHAR* Name)
{
	FLinearColor Value(0, 0, 0, 0);
	Material->GetVectorParameterValue(FHashedMaterialParameterInfo(Name), Value);
	return Value;
}

float& Channel(FLinearColor& Color, int32 Index)
{
	return Index == 0 ? Color.R : Index == 1 ? Color.G : Index == 2 ? Color.B : Color.A;
}
} // namespace

int32 SwcLookMaterials::ApplyHairColor(UMeshComponent* Component, const FLinearColor& Color)
{
	int32 Changed = 0;
	for (int32 Index = 0; Component != nullptr && Index < Component->GetNumMaterials(); ++Index)
	{
		UMaterialInterface* Material = Component->GetMaterial(Index);
		const bool bMasks = HasOverlayMasks(Material);
		if (UMaterialInstanceDynamic* Dynamic = Component->CreateAndSetMaterialInstanceDynamic(Index))
		{
			// The mask colors are gamma values: the source shader squares them (charactermasking.cfib, C * C).
			Dynamic->SetVectorParameterValue(bMasks ? TEXT("MaskColorR") : TEXT("BaseColorFactor"),
				bMasks ? FLinearColor(Color.R, Color.G, Color.B, 0.0f) : Color);
			++Changed;
		}
	}
	return Changed;
}

int32 SwcLookMaterials::ApplyItemMasks(UMeshComponent* Component, const FSwcItemMasks& Masks, bool bAllGroups)
{
	if (Component == nullptr || (bAllGroups ? !Masks.bKnown : Masks.IsEmpty()))
	{
		return 0;
	}
	static const TCHAR* const ColorNames[3] = {TEXT("MaskColorR"), TEXT("MaskColorG"), TEXT("MaskColorB")};
	int32 Changed = 0;
	for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
	{
		UMaterialInterface* Material = Component->GetMaterial(Index);
		if (!HasOverlayMasks(Material))
		{
			continue;
		}
		// MaskAmount: R, G, B amounts and the A gloss amount; MaskOverride: R, G, B overrides and the A specular amount;
		// MaskExtra.R: the A gloss shift (swc_materials.MASK_HLSL).
		FLinearColor Amount = VectorOf(Material, TEXT("MaskAmount"));
		FLinearColor Override = VectorOf(Material, TEXT("MaskOverride"));
		FLinearColor Extra = VectorOf(Material, TEXT("MaskExtra"));
		UMaterialInstanceDynamic* Dynamic = Component->CreateAndSetMaterialInstanceDynamic(Index);
		if (Dynamic == nullptr)
		{
			continue;
		}
		for (int32 Group = 0; Group < 3; ++Group)
		{
			if (bAllGroups || Masks.Amount[Group] > 0.0f)
			{
				Dynamic->SetVectorParameterValue(ColorNames[Group], Masks.Color[Group]);
				Channel(Amount, Group) = Masks.Amount[Group];
				Channel(Override, Group) = Masks.Override[Group];
			}
		}
		if (bAllGroups || Masks.SpecAmount > 0.0f)
		{
			Dynamic->SetVectorParameterValue(TEXT("MaskSpecColor"), Masks.SpecColor);
			Override.A = Masks.SpecAmount;
		}
		if (bAllGroups || Masks.GlossAmount > 0.0f)
		{
			Amount.A = Masks.GlossAmount;
			Extra.R = Masks.GlossShift;
		}
		Dynamic->SetVectorParameterValue(TEXT("MaskAmount"), Amount);
		Dynamic->SetVectorParameterValue(TEXT("MaskOverride"), Override);
		Dynamic->SetVectorParameterValue(TEXT("MaskExtra"), Extra);
		++Changed;
	}
	return Changed;
}
