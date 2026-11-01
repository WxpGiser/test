// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#pragma once

#include "CoreMinimal.h"

/** Mesh arrays expressed in a local East/North/Up metre frame. */
struct FShpMeshData
{
	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FColor> Colors;
	TArray<uint32> Indices;

	void Reset()
	{
		Positions.Reset();
		Normals.Reset();
		Colors.Reset();
		Indices.Reset();
	}

	int32 Num() const { return Positions.Num(); }
	int32 NumTriangles() const { return Indices.Num() / 3; }
};

namespace ShpMesh
{
	double SignedArea(const TArray<FVector2D>& Ring);
	bool IsPointInRing(const FVector2D& P, const TArray<FVector2D>& Ring);

	/** Drops duplicate / closing points and points closer than Tolerance. */
	TArray<FVector2D> CleanRing(const TArray<FVector2D>& In, double Tolerance = 1e-4);

	/** Re-orients the ring in place. */
	void NormalizeRing(TArray<FVector2D>& Ring, bool bCounterClockwise);

	/**
	 * Splits the rings of one shape record into outer rings (CCW) and the holes
	 * that belong to them (CW), using point-in-polygon containment.
	 */
	void ClassifyRings(
		const TArray<TArray<FVector2D>>& Rings,
		TArray<TArray<FVector2D>>& OutOuters,
		TArray<TArray<TArray<FVector2D>>>& OutHoles);

	/** Ear clipping with hole bridging; expects CCW outer and CW holes. */
	void TriangulateWithHoles(
		const TArray<FVector2D>& Outer,
		const TArray<TArray<FVector2D>>& Holes,
		TArray<FIntVector>& OutTriangles);

	/**
	 * Extrudes one footprint (outer ring + holes) from BaseZ to TopZ, adding walls,
	 * a roof and optionally a floor. All arrays of Out are appended to.
	 */
	void BuildExtrusion(
		const TArray<FVector2D>& Outer,
		const TArray<TArray<FVector2D>>& Holes,
		double BaseZ,
		double TopZ,
		const FColor& Color,
		bool bCloseBottom,
		FShpMeshData& Out);
}
