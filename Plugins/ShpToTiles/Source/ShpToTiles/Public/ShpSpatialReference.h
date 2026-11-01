// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#pragma once

#include "CoreMinimal.h"

/** Supported source projections. */
enum class EShpProjectionMethod : uint8
{
	Geographic,
	TransverseMercator,
	Unsupported
};

/**
 * Just enough CRS support to bring a Shapefile into WGS84 lon/lat:
 *  - geographic (lon/lat degrees),
 *  - Transverse Mercator / Gauss-Kruger (the usual CGCS2000 / Xian80 / Beijing54
 *    projected grid used by Chinese footprint data).
 *
 * Parameters come from the .prj WKT when present, otherwise from the manual
 * override in the panel.
 */
class FShpSpatialReference
{
public:
	EShpProjectionMethod Method = EShpProjectionMethod::Geographic;

	double SemiMajorAxis = 6378137.0;
	double InverseFlattening = 298.257223563;

	double CentralMeridianDeg = 0.0;
	double LatitudeOfOriginDeg = 0.0;
	double ScaleFactor = 1.0;
	double FalseEasting = 0.0;
	double FalseNorthing = 0.0;

	/** Radians per angular unit for geographic CRS. */
	double AngularRadPerUnit = 0.017453292519943295;
	/** Metres per linear unit for projected CRS. */
	double UnitToMetres = 1.0;
	/** Subtracted from raw X before projection (drops a 3/6-degree zone prefix such as 40500000 -> 500000). */
	double CoordinateShiftX = 0.0;

	FString Name;
	FString Source;

	static bool FromWkt(const FString& Wkt, FShpSpatialReference& Out, FString& OutError);
	static FShpSpatialReference MakeManualGaussKruger(
		double InCentralMeridianDeg,
		double InFalseEasting,
		double InScaleFactor,
		double InLatitudeOfOriginDeg);
	static FShpSpatialReference MakeGeographicWgs84();

	/** File coordinates -> longitude/latitude in degrees. Returns (Lon, Lat). */
	FVector2D ToLonLat(const FVector2D& XY) const;

	bool IsGeographic() const { return Method == EShpProjectionMethod::Geographic; }
	bool IsValid() const { return Method != EShpProjectionMethod::Unsupported; }

	FString Describe() const;

	/** Eccentricity squared derived from the ellipsoid. */
	double GetEccentricitySquared() const
	{
		const double F = 1.0 / InverseFlattening;
		return F * (2.0 - F);
	}

private:
	double MeridianArc(double LatRad) const;
};
