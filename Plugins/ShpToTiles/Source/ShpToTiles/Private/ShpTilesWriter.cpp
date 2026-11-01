// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#include "ShpTilesWriter.h"

#include "ShpGeo.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
	FString FormatDouble(double Value)
	{
		return FString::Printf(TEXT("%.10f"), Value);
	}

	void AppendBytesPadded(TArray<uint8>& Blob, const void* Data, int32 Size)
	{
		const int32 Start = Blob.Num();
		Blob.AddUninitialized(Size);
		if (Size > 0)
		{
			FMemory::Memcpy(Blob.GetData() + Start, Data, Size);
		}
		while (Blob.Num() % 4 != 0)
		{
			Blob.Add(0);
		}
	}

	/** Builds a binary glTF 2.0 payload for one tile. */
	bool BuildGlb(const FShpTileContent& Tile, bool bUnlit, bool bDoubleSided, const FLinearColor& BaseColor, TArray<uint8>& OutGlb, FString& OutError)
	{
		OutGlb.Reset();

		const int32 VertexCount = Tile.Positions.Num();
		const int32 IndexCount = Tile.Indices.Num();
		if (VertexCount == 0 || IndexCount == 0)
		{
			OutError = TEXT("空 tile");
			return false;
		}

		const bool bHasColors = Tile.Colors.Num() == VertexCount;

		// ---- vertex buffer data ------------------------------------------------
		TArray<uint8> Bin;
		Bin.Reserve(VertexCount * 28 + IndexCount * 4 + 64);

		const int32 PosOffset = Bin.Num();
		AppendBytesPadded(Bin, Tile.Positions.GetData(), VertexCount * static_cast<int32>(sizeof(FVector3f)));
		const int32 PosBytes = Bin.Num() - PosOffset;

		const int32 NrmOffset = Bin.Num();
		AppendBytesPadded(Bin, Tile.Normals.GetData(), VertexCount * static_cast<int32>(sizeof(FVector3f)));
		const int32 NrmBytes = Bin.Num() - NrmOffset;

		int32 ColOffset = INDEX_NONE;
		int32 ColBytes = 0;
		if (bHasColors)
		{
			// glTF wants RGBA byte order; FColor is BGRA in memory on little endian,
			// so pack explicitly.
			TArray<uint8> Rgba;
			Rgba.SetNumUninitialized(VertexCount * 4);
			for (int32 i = 0; i < VertexCount; ++i)
			{
				const FColor& C = Tile.Colors[i];
				Rgba[i * 4 + 0] = C.R;
				Rgba[i * 4 + 1] = C.G;
				Rgba[i * 4 + 2] = C.B;
				Rgba[i * 4 + 3] = 255;
			}

			ColOffset = Bin.Num();
			AppendBytesPadded(Bin, Rgba.GetData(), Rgba.Num());
			ColBytes = Bin.Num() - ColOffset;
		}

		const int32 IdxOffset = Bin.Num();
		AppendBytesPadded(Bin, Tile.Indices.GetData(), IndexCount * static_cast<int32>(sizeof(uint32)));
		const int32 IdxBytes = Bin.Num() - IdxOffset;

		// ---- JSON --------------------------------------------------------------
		FString BufferViews = FString::Printf(
			TEXT("{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":%d,\"target\":34962},"
				 "{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":%d,\"target\":34962}"),
			PosOffset, PosBytes, NrmOffset, NrmBytes);

		int32 ColorViewIndex = INDEX_NONE;
		if (bHasColors)
		{
			BufferViews += FString::Printf(
				TEXT(",{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":%d,\"target\":34962}"),
				ColOffset, ColBytes);
			ColorViewIndex = 2;
		}

		const int32 IndexViewIndex = bHasColors ? 3 : 2;
		BufferViews += FString::Printf(
			TEXT(",{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":%d,\"target\":34963}"),
			IdxOffset, IdxBytes);

		FString Accessors = FString::Printf(
			TEXT("{\"bufferView\":0,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\",\"min\":[%s,%s,%s],\"max\":[%s,%s,%s]},"),
			VertexCount,
			*FormatDouble(Tile.Min.X), *FormatDouble(Tile.Min.Y), *FormatDouble(Tile.Min.Z),
			*FormatDouble(Tile.Max.X), *FormatDouble(Tile.Max.Y), *FormatDouble(Tile.Max.Z));
		Accessors += FString::Printf(
			TEXT("{\"bufferView\":1,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\"}"), VertexCount);

		int32 NextAccessor = 2;
		int32 ColorAccessor = INDEX_NONE;
		if (bHasColors)
		{
			Accessors += FString::Printf(
				TEXT(",{\"bufferView\":%d,\"componentType\":5121,\"normalized\":true,\"count\":%d,\"type\":\"VEC4\"}"),
				ColorViewIndex, VertexCount);
			ColorAccessor = NextAccessor++;
		}

		const int32 IndexAccessor = NextAccessor;
		Accessors += FString::Printf(
			TEXT(",{\"bufferView\":%d,\"componentType\":5125,\"count\":%d,\"type\":\"SCALAR\"}"),
			IndexViewIndex, IndexCount);

		FString Attributes = TEXT("\"POSITION\":0,\"NORMAL\":1");
		if (bHasColors)
		{
			Attributes += FString::Printf(TEXT(",\"COLOR_0\":%d"), ColorAccessor);
		}

		const FString Material = FString::Printf(
			TEXT("\"materials\":[{\"name\":\"ShpExtrusion\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[%.6f,%.6f,%.6f,1.0],\"metallicFactor\":0.0,\"roughnessFactor\":0.85},\"doubleSided\":%s%s}]"),
			bHasColors ? 1.0 : BaseColor.R,
			bHasColors ? 1.0 : BaseColor.G,
			bHasColors ? 1.0 : BaseColor.B,
			bDoubleSided ? TEXT("true") : TEXT("false"),
			bUnlit ? TEXT(",\"extensions\":{\"KHR_materials_unlit\":{}}") : TEXT(""));

		FString Json;
		Json.Reserve(2048);
		Json += TEXT("{\"asset\":{\"version\":\"2.0\",\"generator\":\"ShpToTiles\"},");
		if (bUnlit)
		{
			Json += TEXT("\"extensionsUsed\":[\"KHR_materials_unlit\"],");
		}
		Json += TEXT("\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],");
		Json += Material;
		Json += TEXT(",\"meshes\":[{\"primitives\":[{\"attributes\":{");
		Json += Attributes;
		Json += FString::Printf(TEXT("},\"indices\":%d,\"material\":0,\"mode\":4}]}],"), IndexAccessor);
		Json += FString::Printf(TEXT("\"buffers\":[{\"byteLength\":%d}],"), Bin.Num());
		Json += TEXT("\"bufferViews\":[") + BufferViews + TEXT("],");
		Json += TEXT("\"accessors\":[") + Accessors + TEXT("]}");

		// ---- GLB container ----------------------------------------------------
		TArray<uint8> JsonBytes;
		FTCHARToUTF8 Converter(*Json);
		JsonBytes.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
		while (JsonBytes.Num() % 4 != 0)
		{
			JsonBytes.Add(' ');
		}

		const int32 TotalLength = 12 + 8 + JsonBytes.Num() + 8 + Bin.Num();
		OutGlb.Reserve(TotalLength);

		const uint32 Magic = 0x46546C67; // 'glTF'
		const uint32 Version = 2;
		const uint32 JsonChunkType = 0x4E4F534A;
		const uint32 BinChunkType = 0x004E4942;

		OutGlb.Append(reinterpret_cast<const uint8*>(&Magic), 4);
		OutGlb.Append(reinterpret_cast<const uint8*>(&Version), 4);
		OutGlb.Append(reinterpret_cast<const uint8*>(&TotalLength), 4);

		uint32 JsonLength = static_cast<uint32>(JsonBytes.Num());
		OutGlb.Append(reinterpret_cast<const uint8*>(&JsonLength), 4);
		OutGlb.Append(reinterpret_cast<const uint8*>(&JsonChunkType), 4);
		OutGlb.Append(JsonBytes);

		uint32 BinLength = static_cast<uint32>(Bin.Num());
		OutGlb.Append(reinterpret_cast<const uint8*>(&BinLength), 4);
		OutGlb.Append(reinterpret_cast<const uint8*>(&BinChunkType), 4);
		OutGlb.Append(Bin);

		// The last write must not exceed the header length we announced.
		check(OutGlb.Num() == TotalLength);
		return true;
	}

	/** Wraps a GLB into a b3dm feature table (no batch table). */
	void WrapB3dm(const TArray<uint8>& Glb, TArray<uint8>& OutB3dm)
	{
		OutB3dm.Reset();

		FString FeatureTableJson = TEXT("{\"BATCH_LENGTH\":0}");
		FTCHARToUTF8 Conv(*FeatureTableJson);
		TArray<uint8> FtBytes;
		FtBytes.Append(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length());

		// The embedded glb must start at an 8-byte boundary.
		while ((28 + FtBytes.Num()) % 8 != 0)
		{
			FtBytes.Add(' ');
		}

		const uint32 TotalLength = static_cast<uint32>(28 + FtBytes.Num() + Glb.Num());
		const uint32 Version = 1;
		const uint32 FtJsonLength = static_cast<uint32>(FtBytes.Num());
		const uint32 FtBinLength = 0;
		const uint32 BtJsonLength = 0;
		const uint32 BtBinLength = 0;

		OutB3dm.Reserve(TotalLength);
		OutB3dm.Append(reinterpret_cast<const uint8*>("b3dm"), 4);
		OutB3dm.Append(reinterpret_cast<const uint8*>(&Version), 4);
		OutB3dm.Append(reinterpret_cast<const uint8*>(&TotalLength), 4);
		OutB3dm.Append(reinterpret_cast<const uint8*>(&FtJsonLength), 4);
		OutB3dm.Append(reinterpret_cast<const uint8*>(&FtBinLength), 4);
		OutB3dm.Append(reinterpret_cast<const uint8*>(&BtJsonLength), 4);
		OutB3dm.Append(reinterpret_cast<const uint8*>(&BtBinLength), 4);
		OutB3dm.Append(FtBytes);
		OutB3dm.Append(Glb);
	}
}

namespace ShpTiles
{
	bool WriteTileset(
		const FShpTilesetSettings& Settings,
		TArray<FShpTileContent>& Tiles,
		int32& OutTriangleCount,
		int64& OutByteCount,
		FString& OutTilesetPath,
		FString& OutError)
	{
		OutTriangleCount = 0;
		OutByteCount = 0;
		OutTilesetPath.Reset();
		OutError.Reset();

		const FString OutDir = FPaths::ConvertRelativePathToFull(Settings.OutDir);
		const FString TileDir = OutDir / TEXT("tiles");
		if (!IFileManager::Get().MakeDirectory(*TileDir, true))
		{
			OutError = FString::Printf(TEXT("无法创建输出目录: %s"), *TileDir);
			return false;
		}

		FString Children;
		Children.Reserve(Tiles.Num() * 512);

		for (FShpTileContent& Tile : Tiles)
		{
			if (Tile.IsEmpty())
			{
				continue;
			}

			TArray<uint8> Glb;
			if (!BuildGlb(Tile, Settings.bUnlit, Settings.bDoubleSided, Settings.SingleColor, Glb, OutError))
			{
				return false;
			}

			TArray<uint8> B3dm;
			WrapB3dm(Glb, B3dm);

			const FString FileName = Tile.Name + TEXT(".b3dm");
			const FString FullPath = TileDir / FileName;
			if (!FFileHelper::SaveArrayToFile(B3dm, *FullPath))
			{
				OutError = FString::Printf(TEXT("无法写入瓦片文件: %s"), *FullPath);
				return false;
			}

			OutByteCount += B3dm.Num();
			OutTriangleCount += Tile.NumTriangles();

			// Bounding volume is expressed in the tile's own (Z-up) frame, while the
			// glTF content is Y-up: content (x, y, z) = (east, up, -north) becomes
			// tile (east, north, up) once the runtime applies Y_UP_TO_Z_UP.
			const FVector3f Center(
				0.5f * (Tile.Min.X + Tile.Max.X),
				-0.5f * (Tile.Min.Z + Tile.Max.Z),
				0.5f * (Tile.Min.Y + Tile.Max.Y));
			const FVector3f Half(
				0.5f * (Tile.Max.X - Tile.Min.X),
				0.5f * (Tile.Max.Z - Tile.Min.Z),
				0.5f * (Tile.Max.Y - Tile.Min.Y));

			FString Box = FString::Printf(
				TEXT("[%s,%s,%s,%s,0.0000000000,0.0000000000,0.0000000000,%s,0.0000000000,0.0000000000,0.0000000000,%s]"),
				*FormatDouble(Center.X), *FormatDouble(Center.Y), *FormatDouble(Center.Z),
				*FormatDouble(Half.X),
				*FormatDouble(Half.Y),
				*FormatDouble(Half.Z));

			// Column order must be (east, north, up): the glTF content is Y-up, so the
			// runtime rotates it by Y_UP_TO_Z_UP before this transform is applied. Using
			// (east, up, -north) here rotated the whole model 90° about the east axis,
			// i.e. buildings lying on their side.
			const FVector& North = -Tile.FrameNegNorth;
			FString Transform = FString::Printf(
				TEXT("[%s,%s,%s,0.0000000000,%s,%s,%s,0.0000000000,%s,%s,%s,0.0000000000,%s,%s,%s,1.0000000000]"),
				*FormatDouble(Tile.FrameEast.X), *FormatDouble(Tile.FrameEast.Y), *FormatDouble(Tile.FrameEast.Z),
				*FormatDouble(North.X), *FormatDouble(North.Y), *FormatDouble(North.Z),
				*FormatDouble(Tile.FrameUp.X), *FormatDouble(Tile.FrameUp.Y), *FormatDouble(Tile.FrameUp.Z),
				*FormatDouble(Tile.FrameOrigin.X), *FormatDouble(Tile.FrameOrigin.Y), *FormatDouble(Tile.FrameOrigin.Z));

			if (Children.Len() > 0)
			{
				Children += TEXT(",");
			}
			Children += FString::Printf(
				TEXT("\n    {\"boundingVolume\":{\"box\":%s},\"geometricError\":0.0,\"transform\":%s,\"content\":{\"uri\":\"tiles/%s\"}}"),
				*Box, *Transform, *FileName);

			// Free per-tile buffers as we go; these can be very large.
			Tile.Positions.Empty();
			Tile.Normals.Empty();
			Tile.Colors.Empty();
			Tile.Indices.Empty();
		}

		if (Children.IsEmpty())
		{
			OutError = TEXT("没有任何非空瓦片（数据可能全部被过滤掉了）");
			return false;
		}

		const double MidLat = 0.5 * (Settings.SouthDeg + Settings.NorthDeg);
		const double ExtentX = FMath::Abs(Settings.EastDeg - Settings.WestDeg) * ShpGeo::MetresPerDegreeLon(MidLat);
		const double ExtentY = FMath::Abs(Settings.NorthDeg - Settings.SouthDeg) * ShpGeo::MetresPerDegreeLat(MidLat);
		const double RootError = FMath::Max(100.0, FMath::Max(ExtentX, ExtentY) * 0.5);

		const double MinHeight = FMath::Min(Settings.MinHeight, Settings.MaxHeight) - 50.0;
		const double MaxHeight = FMath::Max(Settings.MinHeight, Settings.MaxHeight) + 50.0;

		const FString Region = FString::Printf(
			TEXT("[%s,%s,%s,%s,%s,%s]"),
			*FormatDouble(FMath::DegreesToRadians(Settings.WestDeg)),
			*FormatDouble(FMath::DegreesToRadians(Settings.SouthDeg)),
			*FormatDouble(FMath::DegreesToRadians(Settings.EastDeg)),
			*FormatDouble(FMath::DegreesToRadians(Settings.NorthDeg)),
			*FormatDouble(MinHeight),
			*FormatDouble(MaxHeight));

		FString TilesetJson;
		TilesetJson.Reserve(Children.Len() + 1024);
		TilesetJson += TEXT("{\n \"asset\":{\"version\":\"1.0\",\"generator\":\"");
		TilesetJson += Settings.GeneratorName;
		TilesetJson += TEXT("\"},\n");
		TilesetJson += FString::Printf(TEXT(" \"geometricError\":%s,\n"), *FormatDouble(RootError));
		TilesetJson += TEXT(" \"root\":{\"boundingVolume\":{\"region\":");
		TilesetJson += Region;
		TilesetJson += FString::Printf(TEXT("},\"geometricError\":%s,\"refine\":\"ADD\",\n"), *FormatDouble(RootError));
		TilesetJson += TEXT("  \"children\":[");
		TilesetJson += Children;
		TilesetJson += TEXT("\n  ]\n }\n}\n");

		OutTilesetPath = OutDir / TEXT("tileset.json");
		if (!FFileHelper::SaveStringToFile(TilesetJson, *OutTilesetPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("无法写入 tileset.json: %s"), *OutTilesetPath);
			return false;
		}

		OutByteCount += TilesetJson.Len();
		return true;
	}
}
