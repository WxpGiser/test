// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#include "ShpGeo.h"

namespace ShpGeo
{
	void LonLatHeightToEcef(double LonDeg, double LatDeg, double Height, FVector& OutEcef)
	{
		const double F = 1.0 / Wgs84InverseFlattening;
		const double E2 = F * (2.0 - F);

		const double Lon = FMath::DegreesToRadians(LonDeg);
		const double Lat = FMath::DegreesToRadians(LatDeg);
		const double SinLat = FMath::Sin(Lat);
		const double CosLat = FMath::Cos(Lat);

		const double N = Wgs84SemiMajor / FMath::Sqrt(1.0 - E2 * SinLat * SinLat);

		OutEcef.X = (N + Height) * CosLat * FMath::Cos(Lon);
		OutEcef.Y = (N + Height) * CosLat * FMath::Sin(Lon);
		OutEcef.Z = (N * (1.0 - E2) + Height) * SinLat;
	}

	void FEnuFrame::Init(double LonDeg, double LatDeg, double HeightMetres)
	{
		LonLatHeightToEcef(LonDeg, LatDeg, HeightMetres, Origin);

		const double Lon = FMath::DegreesToRadians(LonDeg);
		const double Lat = FMath::DegreesToRadians(LatDeg);
		const double SinLon = FMath::Sin(Lon);
		const double CosLon = FMath::Cos(Lon);
		const double SinLat = FMath::Sin(Lat);
		const double CosLat = FMath::Cos(Lat);

		Up = FVector(CosLat * CosLon, CosLat * SinLon, SinLat);
		East = FVector(-SinLon, CosLon, 0.0);
		North = FVector::CrossProduct(Up, East);
	}

	double MetresPerDegreeLon(double LatDeg)
	{
		return FMath::DegreesToRadians(1.0) * Wgs84SemiMajor * FMath::Cos(FMath::DegreesToRadians(LatDeg));
	}

	double MetresPerDegreeLat(double LatDeg)
	{
		const double F = 1.0 / Wgs84InverseFlattening;
		const double E2 = F * (2.0 - F);
		const double Lat = FMath::DegreesToRadians(LatDeg);
		const double SinLat = FMath::Sin(Lat);
		const double Denom = 1.0 - E2 * SinLat * SinLat;
		const double MeridionalRadius = Wgs84SemiMajor * (1.0 - E2) / FMath::Pow(Denom, 1.5);
		return FMath::DegreesToRadians(1.0) * MeridionalRadius;
	}
}
