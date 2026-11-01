// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#include "ShpColorizer.h"

namespace
{
	struct FStop
	{
		double T;
		FColor Color;
	};

	void Interpolate(const TArray<FStop>& Stops, double T, FLinearColor& Out)
	{
		if (Stops.Num() == 0)
		{
			Out = FLinearColor::White;
			return;
		}
		if (T <= Stops[0].T)
		{
			Out = FLinearColor(Stops[0].Color);
			return;
		}
		if (T >= Stops.Last().T)
		{
			Out = FLinearColor(Stops.Last().Color);
			return;
		}

		for (int32 i = 1; i < Stops.Num(); ++i)
		{
			if (T <= Stops[i].T)
			{
				const double Span = FMath::Max(1e-9, Stops[i].T - Stops[i - 1].T);
				const double A = (T - Stops[i - 1].T) / Span;
				const FLinearColor C0(Stops[i - 1].Color);
				const FLinearColor C1(Stops[i].Color);
				Out = FLinearColor(
					FMath::Lerp(C0.R, C1.R, A),
					FMath::Lerp(C0.G, C1.G, A),
					FMath::Lerp(C0.B, C1.B, A),
					1.0f);
				return;
			}
		}
		Out = FLinearColor(Stops.Last().Color);
	}

	const TArray<FStop>& GetStops(EShpPalette Palette)
	{
		static const TArray<FStop> Viridis = {
			{0.00, FColor(68, 1, 84)},
			{0.25, FColor(59, 82, 139)},
			{0.50, FColor(33, 145, 140)},
			{0.75, FColor(94, 201, 98)},
			{1.00, FColor(253, 231, 37)}};

		static const TArray<FStop> Spectral = {
			{0.00, FColor(94, 79, 162)},
			{0.20, FColor(50, 136, 189)},
			{0.40, FColor(102, 194, 165)},
			{0.60, FColor(255, 255, 191)},
			{0.80, FColor(244, 109, 67)},
			{1.00, FColor(158, 1, 66)}};

		static const TArray<FStop> Terrain = {
			{0.00, FColor(38, 115, 66)},
			{0.30, FColor(140, 176, 92)},
			{0.55, FColor(214, 199, 140)},
			{0.75, FColor(168, 122, 82)},
			{1.00, FColor(240, 240, 240)}};

		static const TArray<FStop> Blues = {
			{0.00, FColor(222, 235, 247)},
			{0.50, FColor(107, 174, 214)},
			{1.00, FColor(8, 48, 107)}};

		static const TArray<FStop> Warm = {
			{0.00, FColor(255, 255, 178)},
			{0.35, FColor(253, 174, 97)},
			{0.65, FColor(244, 109, 67)},
			{0.85, FColor(178, 24, 43)},
			{1.00, FColor(103, 0, 31)}};

		static const TArray<FStop> Gray = {
			{0.00, FColor(60, 60, 62)},
			{1.00, FColor(230, 230, 232)}};

		switch (Palette)
		{
		case EShpPalette::Spectral:
			return Spectral;
		case EShpPalette::Terrain:
			return Terrain;
		case EShpPalette::Blues:
			return Blues;
		case EShpPalette::Warm:
			return Warm;
		case EShpPalette::Gray:
			return Gray;
		case EShpPalette::Categorical:
		case EShpPalette::Viridis:
		default:
			return Viridis;
		}
	}

	const FColor CategoricalPalette[] = {
		FColor(31, 119, 180),
		FColor(255, 127, 14),
		FColor(44, 160, 44),
		FColor(214, 39, 40),
		FColor(148, 103, 189),
		FColor(140, 86, 75),
		FColor(227, 119, 194),
		FColor(127, 127, 127),
		FColor(188, 189, 34),
		FColor(23, 190, 207),
		FColor(174, 199, 232),
		FColor(255, 187, 120),
		FColor(152, 223, 138),
		FColor(255, 152, 150),
		FColor(197, 176, 213),
		FColor(196, 156, 148),
		FColor(247, 182, 210),
		FColor(199, 199, 199),
		FColor(219, 219, 141),
		FColor(158, 218, 229),
		FColor(122, 33, 12),
		FColor(51, 76, 130),
		FColor(26, 105, 40),
		FColor(153, 153, 51)};
}

namespace ShpColor
{
	FLinearColor CategoricalColor(int32 Index)
	{
		const int32 Count = UE_ARRAY_COUNT(CategoricalPalette);
		return FLinearColor(CategoricalPalette[((Index % Count) + Count) % Count]);
	}

	FLinearColor RampColor(EShpPalette Palette, double T)
	{
		if (Palette == EShpPalette::Categorical)
		{
			const int32 Index = FMath::Clamp(FMath::RoundToInt(T * 23.0), 0, 23);
			return CategoricalColor(Index);
		}

		FLinearColor Out;
		Interpolate(GetStops(Palette), FMath::Clamp(T, 0.0, 1.0), Out);
		return Out;
	}

	TArray<FLinearColor> CategoryColors(EShpPalette Palette, int32 Count)
	{
		TArray<FLinearColor> Out;
		Count = FMath::Max(1, Count);
		Out.Reserve(Count);

		for (int32 i = 0; i < Count; ++i)
		{
			if (Palette == EShpPalette::Categorical)
			{
				Out.Add(CategoricalColor(i));
			}
			else
			{
				const double T = (Count == 1) ? 0.5 : static_cast<double>(i) / static_cast<double>(Count - 1);
				Out.Add(RampColor(Palette, T));
			}
		}
		return Out;
	}

	FColor ToColor(const FLinearColor& In)
	{
		// glTF COLOR_0 is linear, so keep the values linear (no sRGB encode).
		const FColor C = In.ToFColor(false);
		return FColor(C.R, C.G, C.B, 255);
	}
}
