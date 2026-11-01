// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#pragma once

#include "CoreMinimal.h"

/**
 * One tile of the dataset: geometry already expressed in the tile-local glTF frame
 * (x = East, y = Up, z = -North) plus the ENU basis / origin that maps that frame
 * into ECEF.
 */
struct FShpTileContent
{
	FString Name;

	FVector FrameEast = FVector(1.0, 0.0, 0.0);
	FVector FrameUp = FVector(0.0, 0.0, 1.0);
	FVector FrameNegNorth = FVector(0.0, -1.0, 0.0);
	FVector FrameOrigin = FVector::ZeroVector;

	TArray<FVector3f> Positions;
	TArray<FVector3f> Normals;
	TArray<FColor> Colors;
	TArray<uint32> Indices;

	FVector3f Min = FVector3f(TNumericLimits<float>::Max());
	FVector3f Max = FVector3f(TNumericLimits<float>::Lowest());

	void AccumulateBounds(const FVector3f& P)
	{
		Min.X = FMath::Min(Min.X, P.X);
		Min.Y = FMath::Min(Min.Y, P.Y);
		Min.Z = FMath::Min(Min.Z, P.Z);
		Max.X = FMath::Max(Max.X, P.X);
		Max.Y = FMath::Max(Max.Y, P.Y);
		Max.Z = FMath::Max(Max.Z, P.Z);
	}

	bool IsEmpty() const { return Positions.Num() == 0 || Indices.Num() < 3; }
	int32 NumTriangles() const { return Indices.Num() / 3; }
};

/** Global settings for tileset.json. */
struct FShpTilesetSettings
{
	FString OutDir;
	FString GeneratorName = TEXT("ShpToTiles");
	FString DatasetName = TEXT("ShpDataset");

	/** Geographic extent of the dataset in degrees. */
	double WestDeg = 0.0;
	double SouthDeg = 0.0;
	double EastDeg = 0.0;
	double NorthDeg = 0.0;

	/** Height range above the ellipsoid, used for the root bounding region. */
	double MinHeight = 0.0;
	double MaxHeight = 100.0;

	bool bHasColors = false;
	bool bUnlit = false;
	bool bDoubleSided = false;
	FLinearColor SingleColor = FLinearColor(0.82f, 0.82f, 0.83f, 1.0f);
};

namespace ShpTiles
{
	/** Writes tileset.json plus tiles/*.b3dm. Returns false with OutError on failure. */
	bool WriteTileset(
		const FShpTilesetSettings& Settings,
		TArray<FShpTileContent>& Tiles,
		int32& OutTriangleCount,
		int64& OutByteCount,
		FString& OutTilesetPath,
		FString& OutError);
}
