#pragma once

#include "CoreMinimal.h"

class UMeshComponent;
struct FSwcItemMasks;

/**
 * The run-time values of the look materials (docs/MATERIALS.md "The skinned bodies"; research weapon-materials.md section 1).
 * The body and weapon materials of the surface parents have the overlay masks of the source (MaskMode 1: %OVERLAY_MASK); the
 * values of the appearance of a character and of an item go on dynamic instances of the materials of one component.
 */
namespace SwcLookMaterials
{
/** The hair color of the appearance (the hair color database): the color of the red mask group of each material with
 *  overlay masks (the full row of the hair color, its amounts and overrides, goes on after it with ApplyItemMasks: research
 *  hair-color.md); a material with no masks (the glTF
 *  parents) multiplies its base color (BaseColorFactor). @return The number of the changed materials. */
int32 ApplyHairColor(UMeshComponent* Component, const FLinearColor& Color);

/** The mask groups of the appearance row of an item. bAllGroups (the gear path of the retail client at 0x5C3EEE0 [R]: it
 *  copies the material data of the appearance row, zeros too): each group of a known row replaces the values of the material.
 *  Else (the writer at 0x5BF39B0 [R]) only a group with a positive amount replaces them, a zero keeps them. @return The number
 *  of the changed materials. */
int32 ApplyItemMasks(UMeshComponent* Component, const FSwcItemMasks& Masks, bool bAllGroups);
}
