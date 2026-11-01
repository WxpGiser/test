// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#pragma once

#include "CoreMinimal.h"
#include "ShpTypes.h"

/** One column of the .dbf attribute table. */
struct FShpFieldInfo
{
	FString Name;
	TCHAR Type = TEXT('C');
	int32 Length = 0;
	int32 Decimals = 0;
	int32 Offset = 0; // byte offset inside the record (after the 1-byte deletion flag)

	bool IsNumeric() const
	{
		return Type == TEXT('N') || Type == TEXT('F') || Type == TEXT('I') || Type == TEXT('O') ||
			Type == TEXT('B') || Type == TEXT('D');
	}
	bool IsInteger() const { return Type == TEXT('I') || Type == TEXT('N') || Type == TEXT('F'); }
};

/** One shape record (.shp) with raw file coordinates. */
struct FShpRecord
{
	int32 Number = 0;
	int32 ShapeType = 0;
	TArray<int32> PartStarts;
	TArray<FVector2D> Points;

	bool IsNull() const { return ShapeType == 0 || Points.Num() == 0; }
	bool IsPolygon() const { return ShapeType == 5 || ShapeType == 15 || ShapeType == 25; }
	bool IsPolyline() const { return ShapeType == 3 || ShapeType == 13 || ShapeType == 23; }
	bool IsPoint() const { return ShapeType == 1 || ShapeType == 11 || ShapeType == 21 || ShapeType == 8 || ShapeType == 18 || ShapeType == 28; }

	int32 NumParts() const { return PartStarts.Num(); }

	void GetPartRange(int32 Part, int32& OutStart, int32& OutEnd) const
	{
		OutStart = PartStarts.IsValidIndex(Part) ? PartStarts[Part] : 0;
		OutEnd = PartStarts.IsValidIndex(Part + 1) ? PartStarts[Part + 1] : Points.Num();
	}
};

/**
 * Minimal, dependency-free Shapefile reader (.shp geometry + .dbf attributes + .prj WKT).
 * Null shapes and unknown record types are skipped instead of failing the whole file.
 */
class FShapeFileReader
{
public:
	/** Reads the .shp header, the .dbf field descriptors and the .prj (fast, no geometry). */
	bool Open(const FString& InShpPath, EShpEncoding InEncoding, FString& OutError);

	/** Parses every geometry record. Required before iterating GetRecords(). */
	bool LoadAllRecords(FString& OutError);

	void Close();

	bool IsOpen() const { return bOpened; }

	const FString& GetShpPath() const { return ShpPath; }
	const TArray<FShpFieldInfo>& GetFields() const { return Fields; }
	const FString& GetPrjWkt() const { return PrjWkt; }
	const FString& GetResolvedEncodingName() const { return ResolvedEncodingName; }

	int32 GetDeclaredRecordCount() const { return DeclaredRecords; }
	int32 GetFileShapeType() const { return FileShapeType; }
	FShpBounds GetHeaderBounds() const { return HeaderBounds; }
	FShpBounds GetPointBounds() const { return PointBounds; }
	int32 GetNullRecordCount() const { return NullRecords; }

	const TArray<FShpRecord>& GetRecords() const { return Records; }

	/** Attribute value as text (already decoded and trimmed). */
	FString ReadValue(int32 RecordIndex, int32 FieldIndex) const;

	/** Attribute value as a number (0 when empty / non numeric). */
	double ReadNumeric(int32 RecordIndex, int32 FieldIndex) const;

	int32 FindField(const FString& Name) const;

	const TCHAR* GetShapeTypeName() const;

private:
	bool LoadDbf(FString& OutError);
	bool LoadPrj();
	void ResolveEncoding();

	FString ShpPath;
	FString PrjWkt;
	EShpEncoding Encoding = EShpEncoding::Auto;
	FString ResolvedEncodingName = TEXT("unknown");
	bool bResolvedAsUtf8 = true;

	bool bOpened = false;
	int32 FileShapeType = 0;
	int32 DeclaredRecords = 0;
	int32 NullRecords = 0;
	FShpBounds HeaderBounds;
	FShpBounds PointBounds;

	TArray<FShpRecord> Records;

	TArray<uint8> DbfData;
	TArray<FShpFieldInfo> Fields;
	int32 DbfHeaderLength = 0;
	int32 DbfRecordLength = 0;
	int32 DbfRecordCount = 0;
	int32 DbfTotalFieldBytes = 0;
};
