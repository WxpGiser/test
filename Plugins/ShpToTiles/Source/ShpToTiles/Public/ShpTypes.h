// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#pragma once

#include "CoreMinimal.h"
#include "ShpTypes.generated.h"

/** How per-feature colors are derived from the attribute table. */
UENUM(BlueprintType)
enum class EShpColorMode : uint8
{
	/** Single white-model color for every feature. */
	None UMETA(DisplayName = "单色白模"),
	/** One color per distinct attribute value (categorical). */
	Unique UMETA(DisplayName = "唯一值分类"),
	/** Numeric field split into classes (choropleth). */
	Classify UMETA(DisplayName = "数值分级")
};

/** Built-in color ramps. */
UENUM(BlueprintType)
enum class EShpPalette : uint8
{
	Viridis UMETA(DisplayName = "Viridis"),
	Spectral UMETA(DisplayName = "Spectral"),
	Terrain UMETA(DisplayName = "Terrain"),
	Blues UMETA(DisplayName = "Blues"),
	Warm UMETA(DisplayName = "Warm"),
	Categorical UMETA(DisplayName = "Categorical (10)"),
	Gray UMETA(DisplayName = "Gray")
};

/** Text encoding used by the .dbf attribute table. */
UENUM(BlueprintType)
enum class EShpEncoding : uint8
{
	Auto UMETA(DisplayName = "自动检测"),
	Gbk UMETA(DisplayName = "GBK / CP936"),
	Utf8 UMETA(DisplayName = "UTF-8")
};

/** Bounds of a shapefile in *file* coordinates (before CRS conversion). */
struct FShpBounds
{
	double MinX = 0.0;
	double MinY = 0.0;
	double MaxX = 0.0;
	double MaxY = 0.0;
	bool bValid = false;

	void Add(double X, double Y)
	{
		if (!bValid)
		{
			MinX = MaxX = X;
			MinY = MaxY = Y;
			bValid = true;
			return;
		}
		MinX = FMath::Min(MinX, X);
		MinY = FMath::Min(MinY, Y);
		MaxX = FMath::Max(MaxX, X);
		MaxY = FMath::Max(MaxY, Y);
	}

	double Width() const { return MaxX - MinX; }
	double Height() const { return MaxY - MinY; }
	FVector2D Center() const { return FVector2D((MinX + MaxX) * 0.5, (MinY + MaxY) * 0.5); }
};

/**
 * Everything the panel can configure. Exposed as a JSON-convertible USTRUCT so the
 * same options can be driven from Python / console commands.
 */
USTRUCT(BlueprintType)
struct FShpBuildOptions
{
	GENERATED_BODY()

	/** Absolute path to the .shp file. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	FString ShpPath;

	/** Directory the tileset is written to (tileset.json + tiles/*.b3dm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	FString OutDir;

	/** Numeric attribute used for the extrusion height (multiplied by FloorHeight). Empty = ConstantHeight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	FString HeightField;

	/** Attribute used for coloring (only when ColorMode != None). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color")
	FString ColorField;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color")
	EShpColorMode ColorMode = EShpColorMode::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color")
	EShpPalette Palette = EShpPalette::Viridis;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color")
	int32 NumClasses = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color")
	int32 MaxCategories = 24;

	/** Multiplier applied to HeightField: metres per storey (or per unit of the field). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	double FloorHeight = 3.0;

	/** Height used when HeightField is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	double ConstantHeight = 20.0;

	/** Ellipsoid height (m) of the building base when not clamping to terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	double BaseHeight = 25.0;

	/** Rings smaller than this (m^2) are discarded as slivers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	double MinFootprintArea = 8.0;

	/** N x N lon/lat grid used to split the dataset into tiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	int32 GridSize = 16;

	/** Sample the Cesium terrain height per feature and put the base on the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	bool bClampToGround = false;

	/** Vertical offset (m) applied on top of the ground/sample height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	double ClampOffset = 0.0;

	/** Emit back-facing geometry too (walls get a second, inward-facing copy). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	bool bDoubleSided = false;

	/** Use an unlit material (thematic colors stay exact). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	bool bUnlit = false;

	/** Also emit the bottom face of each extruded solid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	bool bCloseBottom = false;

	/** Override the .prj with manual Gauss-Kruger parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CRS")
	bool bManualCrs = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CRS")
	double ManualCentralMeridian = 114.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CRS")
	double ManualFalseEasting = 500000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CRS")
	double ManualScaleFactor = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CRS")
	double ManualLatitudeOfOrigin = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CRS")
	EShpEncoding Encoding = EShpEncoding::Auto;

	/** Base color used when ColorMode == None. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color")
	FLinearColor SingleColor = FLinearColor(0.82f, 0.82f, 0.83f, 1.0f);

	/** Spawn the tileset actor into the level after writing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output")
	bool bAddToScene = true;
};
