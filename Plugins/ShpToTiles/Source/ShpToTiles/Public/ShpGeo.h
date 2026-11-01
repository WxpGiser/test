// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#pragma once

#include "CoreMinimal.h"

namespace ShpGeo
{
	/** WGS84 constants. */
	constexpr double Wgs84SemiMajor = 6378137.0;
	constexpr double Wgs84InverseFlattening = 298.257223563;

	/** Geodetic (lon/lat degrees, height in metres above the ellipsoid) -> ECEF metres. */
	void LonLatHeightToEcef(double LonDeg, double LatDeg, double Height, FVector& OutEcef);

	/** East / North / Up basis attached to a geodetic position. */
	struct FEnuFrame
	{
		FVector Origin = FVector::ZeroVector;
		FVector East = FVector(1.0, 0.0, 0.0);
		FVector North = FVector(0.0, 1.0, 0.0);
		FVector Up = FVector(0.0, 0.0, 1.0);

		void Init(double LonDeg, double LatDeg, double HeightMetres);
	};

	/** Metres per degree of longitude at the given latitude. */
	double MetresPerDegreeLon(double LatDeg);

	/** Metres per degree of latitude (meridional). */
	double MetresPerDegreeLat(double LatDeg);
}
