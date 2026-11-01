// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#pragma once

#include "CoreMinimal.h"
#include "ShpTypes.h"

namespace ShpColor
{
	/** Sample a built-in ramp at T in [0,1] (linear color, ready for vertex colors). */
	FLinearColor RampColor(EShpPalette Palette, double T);

	/** N evenly spread colors of a ramp (used for categorical fields). */
	TArray<FLinearColor> CategoryColors(EShpPalette Palette, int32 Count);

	/** Distinguishable default color for index i (used by the Categorical palette). */
	FLinearColor CategoricalColor(int32 Index);

	FColor ToColor(const FLinearColor& In);
}
