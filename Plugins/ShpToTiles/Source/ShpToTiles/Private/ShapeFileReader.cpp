// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#include "ShapeFileReader.h"

#include "ShpText.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	FORCEINLINE int32 ReadI32LE(const uint8* P)
	{
		return static_cast<int32>(
			static_cast<uint32>(P[0]) | (static_cast<uint32>(P[1]) << 8) |
			(static_cast<uint32>(P[2]) << 16) | (static_cast<uint32>(P[3]) << 24));
	}

	FORCEINLINE int32 ReadI32BE(const uint8* P)
	{
		return static_cast<int32>(
			(static_cast<uint32>(P[0]) << 24) | (static_cast<uint32>(P[1]) << 16) |
			(static_cast<uint32>(P[2]) << 8) | static_cast<uint32>(P[3]));
	}

	FORCEINLINE int32 ReadU16LE(const uint8* P)
	{
		return static_cast<int32>(static_cast<uint16>(P[0]) | (static_cast<uint16>(P[1]) << 8));
	}

	FORCEINLINE double ReadF64LE(const uint8* P)
	{
		double V = 0.0;
		FMemory::Memcpy(&V, P, sizeof(double));
		return V;
	}
}

void FShapeFileReader::Close()
{
	bOpened = false;
	Records.Empty();
	Fields.Empty();
	DbfData.Empty();
	PrjWkt.Empty();
	HeaderBounds = FShpBounds();
	PointBounds = FShpBounds();
	FileShapeType = 0;
	DeclaredRecords = 0;
	NullRecords = 0;
}

bool FShapeFileReader::Open(const FString& InShpPath, EShpEncoding InEncoding, FString& OutError)
{
	Close();

	ShpPath = FPaths::ConvertRelativePathToFull(InShpPath);
	Encoding = InEncoding;

	if (!FPaths::FileExists(ShpPath))
	{
		OutError = FString::Printf(TEXT("找不到 SHP 文件: %s"), *ShpPath);
		return false;
	}

	TArray<uint8> Header;
	if (!FFileHelper::LoadFileToArray(Header, *ShpPath))
	{
		OutError = FString::Printf(TEXT("无法读取 SHP 文件: %s"), *ShpPath);
		return false;
	}

	if (Header.Num() < 100)
	{
		OutError = TEXT("SHP 文件头不完整 (小于 100 字节)");
		return false;
	}

	FileShapeType = ReadI32LE(&Header[32]);
	HeaderBounds.MinX = ReadF64LE(&Header[36]);
	HeaderBounds.MinY = ReadF64LE(&Header[44]);
	HeaderBounds.MaxX = ReadF64LE(&Header[52]);
	HeaderBounds.MaxY = ReadF64LE(&Header[60]);
	HeaderBounds.bValid = HeaderBounds.Width() > 0.0 || HeaderBounds.Height() > 0.0;

	// The .shp header stores the file length in 16-bit words, not a record count.
	// The .shx index gives the exact number of records: 100 + 8 * N bytes.
	DeclaredRecords = INDEX_NONE;
	const FString ShxPath = FPaths::ChangeExtension(ShpPath, TEXT("shx"));
	const int64 ShxSize = IFileManager::Get().FileSize(*ShxPath);
	if (ShxSize > 100)
	{
		DeclaredRecords = static_cast<int32>((ShxSize - 100) / 8);
	}

	bOpened = true;

	LoadPrj();
	LoadDbf(OutError);
	ResolveEncoding();

	return true;
}

bool FShapeFileReader::LoadPrj()
{
	const FString PrjPath = FPaths::ChangeExtension(ShpPath, TEXT("prj"));
	if (!FPaths::FileExists(PrjPath))
	{
		return false;
	}

	FFileHelper::LoadFileToString(PrjWkt, *PrjPath);
	PrjWkt.TrimStartAndEndInline();
	return PrjWkt.Len() > 0;
}

bool FShapeFileReader::LoadDbf(FString& OutError)
{
	const FString DbfPath = FPaths::ChangeExtension(ShpPath, TEXT("dbf"));
	if (!FPaths::FileExists(DbfPath))
	{
		OutError = TEXT("没有 .dbf 属性表（将只能按常量高度生成）");
		return false;
	}

	if (!FFileHelper::LoadFileToArray(DbfData, *DbfPath) || DbfData.Num() < 32)
	{
		OutError = FString::Printf(TEXT("无法读取属性表: %s"), *DbfPath);
		DbfData.Empty();
		return false;
	}

	DbfRecordCount = ReadI32LE(&DbfData[4]);
	DbfHeaderLength = ReadU16LE(&DbfData[8]);
	DbfRecordLength = ReadU16LE(&DbfData[10]);

	int32 Offset = 0;
	int32 Cursor = 32;
	while (Cursor + 32 <= DbfData.Num() && DbfData[Cursor] != 0x0D)
	{
		int32 NameLen = 0;
		while (NameLen < 11 && DbfData[Cursor + NameLen] != 0)
		{
			++NameLen;
		}

		FShpFieldInfo Field;
		Field.Name = ShpText::DecodeAuto(DbfData.GetData() + Cursor, NameLen, Encoding);
		Field.Type = static_cast<TCHAR>(DbfData[Cursor + 11]);
		Field.Length = DbfData[Cursor + 16];
		Field.Decimals = DbfData[Cursor + 17];
		Field.Offset = Offset;
		Offset += Field.Length;
		Fields.Add(Field);

		Cursor += 32;
	}
	DbfTotalFieldBytes = Offset;

	if (DbfRecordLength <= 0)
	{
		DbfRecordLength = DbfTotalFieldBytes + 1;
	}

	return Fields.Num() > 0;
}

void FShapeFileReader::ResolveEncoding()
{
	if (Encoding == EShpEncoding::Utf8)
	{
		bResolvedAsUtf8 = true;
		ResolvedEncodingName = TEXT("UTF-8");
		return;
	}
	if (Encoding == EShpEncoding::Gbk)
	{
		bResolvedAsUtf8 = false;
		ResolvedEncodingName = TEXT("GBK/CP936");
		return;
	}

	// Sniff a sample of the attribute table.
	TArray<uint8> Sample;
	if (Fields.Num() > 0)
	{
		const int32 FieldNameProbe = FMath::Min(DbfData.Num() - 32, 32 * Fields.Num());
		if (FieldNameProbe > 0)
		{
			Sample.Append(DbfData.GetData() + 32, FieldNameProbe);
		}
	}

	const int32 MaxProbeRecords = FMath::Min(DbfRecordCount, 64);
	for (int32 i = 0; i < MaxProbeRecords; ++i)
	{
		const int64 RecStart = static_cast<int64>(DbfHeaderLength) + static_cast<int64>(i) * DbfRecordLength + 1;
		if (RecStart + DbfTotalFieldBytes > DbfData.Num())
		{
			break;
		}
		Sample.Append(DbfData.GetData() + RecStart, DbfTotalFieldBytes);
	}

	bResolvedAsUtf8 = ShpText::LooksLikeUtf8(Sample.GetData(), Sample.Num());
	ResolvedEncodingName = bResolvedAsUtf8 ? TEXT("UTF-8") : TEXT("GBK/CP936");
}

bool FShapeFileReader::LoadAllRecords(FString& OutError)
{
	Records.Empty();
	PointBounds = FShpBounds();
	NullRecords = 0;

	if (!bOpened)
	{
		OutError = TEXT("SHP 文件尚未打开");
		return false;
	}

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *ShpPath))
	{
		OutError = FString::Printf(TEXT("无法读取 SHP 文件: %s"), *ShpPath);
		return false;
	}

	if (Bytes.Num() < 100)
	{
		OutError = TEXT("SHP 文件头不完整");
		return false;
	}

	int64 Offset = 100;
	while (Offset + 8 <= Bytes.Num())
	{
		const int32 RecordNumber = ReadI32BE(Bytes.GetData() + Offset);
		const int32 ContentWords = ReadI32BE(Bytes.GetData() + Offset + 4);
		Offset += 8;

		if (ContentWords <= 0)
		{
			break;
		}

		const int64 ContentLen = static_cast<int64>(ContentWords) * 2;
		if (Offset + ContentLen > Bytes.Num())
		{
			break; // truncated tail
		}

		const uint8* Content = Bytes.GetData() + Offset;

		FShpRecord Rec;
		Rec.Number = RecordNumber;
		Rec.ShapeType = ReadI32LE(Content);

		switch (Rec.ShapeType)
		{
		case 1:  // Point
		case 11: // PointZ
		case 21: // PointM
		{
			if (ContentLen >= 20)
			{
				Rec.PartStarts.Add(0);
				Rec.Points.Add(FVector2D(ReadF64LE(Content + 4), ReadF64LE(Content + 12)));
			}
			break;
		}
		case 8:  // MultiPoint
		case 18: // MultiPointZ
		case 28: // MultiPointM
		{
			if (ContentLen >= 40)
			{
				const int32 NumPoints = ReadI32LE(Content + 36);
				if (NumPoints > 0 && 40 + static_cast<int64>(NumPoints) * 16 <= ContentLen)
				{
					Rec.PartStarts.Add(0);
					Rec.Points.Reserve(NumPoints);
					for (int32 j = 0; j < NumPoints; ++j)
					{
						const uint8* P = Content + 40 + static_cast<int64>(j) * 16;
						Rec.Points.Add(FVector2D(ReadF64LE(P), ReadF64LE(P + 8)));
					}
				}
			}
			break;
		}
		case 3:  // PolyLine
		case 13: // PolyLineZ
		case 23: // PolyLineM
		case 5:  // Polygon
		case 15: // PolygonZ
		case 25: // PolygonM
		{
			if (ContentLen >= 44)
			{
				const int32 NumParts = ReadI32LE(Content + 36);
				const int32 NumPoints = ReadI32LE(Content + 40);
				const int64 PartsBytes = static_cast<int64>(NumParts) * 4;
				const int64 PointsBytes = static_cast<int64>(NumPoints) * 16;
				if (NumParts > 0 && NumPoints > 0 && 44 + PartsBytes + PointsBytes <= ContentLen)
				{
					Rec.PartStarts.Reserve(NumParts);
					Rec.Points.Reserve(NumPoints);
					for (int32 p = 0; p < NumParts; ++p)
					{
						Rec.PartStarts.Add(ReadI32LE(Content + 44 + static_cast<int64>(p) * 4));
					}
					const uint8* PtsBase = Content + 44 + PartsBytes;
					for (int32 j = 0; j < NumPoints; ++j)
					{
						const uint8* P = PtsBase + static_cast<int64>(j) * 16;
						Rec.Points.Add(FVector2D(ReadF64LE(P), ReadF64LE(P + 8)));
					}
				}
			}
			break;
		}
		default:
			break; // Null shape (0) or something we do not support
		}

		if (Rec.Points.Num() > 0)
		{
			for (const FVector2D& Pt : Rec.Points)
			{
				PointBounds.Add(Pt.X, Pt.Y);
			}
			Records.Add(MoveTemp(Rec));
		}
		else
		{
			++NullRecords;
		}

		Offset += ContentLen;
	}

	if (!PointBounds.bValid && HeaderBounds.bValid)
	{
		PointBounds = HeaderBounds;
	}

	if (Records.Num() == 0)
	{
		OutError = TEXT("SHP 中没有可用的几何记录");
		return false;
	}

	return true;
}

FString FShapeFileReader::ReadValue(int32 RecordIndex, int32 FieldIndex) const
{
	if (!Fields.IsValidIndex(FieldIndex) || DbfData.Num() == 0)
	{
		return FString();
	}
	if (RecordIndex < 0 || RecordIndex >= DbfRecordCount)
	{
		return FString();
	}

	const FShpFieldInfo& Field = Fields[FieldIndex];
	const int64 RecStart = static_cast<int64>(DbfHeaderLength) + static_cast<int64>(RecordIndex) * DbfRecordLength + 1;
	const int64 ValueStart = RecStart + Field.Offset;
	const int64 ValueEnd = ValueStart + Field.Length;
	if (ValueStart < 0 || ValueEnd > DbfData.Num())
	{
		return FString();
	}

	const FString Raw = ShpText::Decode(DbfData.GetData() + ValueStart, Field.Length, bResolvedAsUtf8);
	return Field.IsNumeric() ? ShpText::TrimDbText(Raw) : ShpText::TrimDbText(Raw);
}

double FShapeFileReader::ReadNumeric(int32 RecordIndex, int32 FieldIndex) const
{
	const FString Value = ReadValue(RecordIndex, FieldIndex);
	if (Value.IsEmpty())
	{
		return 0.0;
	}
	return FCString::Atod(*Value);
}

int32 FShapeFileReader::FindField(const FString& Name) const
{
	for (int32 i = 0; i < Fields.Num(); ++i)
	{
		if (Fields[i].Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

const TCHAR* FShapeFileReader::GetShapeTypeName() const
{
	switch (FileShapeType)
	{
	case 1:
	case 11:
	case 21:
		return TEXT("Point");
	case 3:
	case 13:
	case 23:
		return TEXT("PolyLine");
	case 5:
	case 15:
	case 25:
		return TEXT("Polygon");
	case 8:
	case 18:
	case 28:
		return TEXT("MultiPoint");
	default:
		return TEXT("Unknown");
	}
}
