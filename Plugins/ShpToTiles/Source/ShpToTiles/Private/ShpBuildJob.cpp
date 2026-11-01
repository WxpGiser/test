// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#include "ShpBuildJob.h"

#include "ShapeFileReader.h"
#include "ShpColorizer.h"
#include "ShpGeo.h"
#include "ShpMeshBuilder.h"
#include "ShpSpatialReference.h"

#include "Cesium3DTileset.h"
#include "CesiumGeoreference.h"
#include "CesiumSampleHeightResult.h"

#include "CollisionQueryParams.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"

namespace
{
	constexpr double MetresPerDegreeLat = 111132.0;
	constexpr int32 MaxReportedLogLines = 400;
}

/** Worker thread trampoline. */
class FShpJobRunnable : public FRunnable
{
public:
	explicit FShpJobRunnable(FShpBuildJob* InJob)
		: Job(InJob)
	{
	}

	virtual uint32 Run() override
	{
		if (Job)
		{
			Job->RunWorker();
		}
		return 0;
	}

	virtual void Stop() override {}

private:
	FShpBuildJob* Job = nullptr;
};

FShpBuildJob::FShpBuildJob(const FShpBuildOptions& InOptions)
	: Options(InOptions)
{
	SamplingDoneEvent = FPlatformProcess::GetSynchEventFromPool(false);
	StartSeconds = FPlatformTime::Seconds();
	GridSize = FMath::Clamp(Options.GridSize, 1, 64);
}

FShpBuildJob::~FShpBuildJob()
{
	Cancel();

	if (WorkerThread)
	{
		WorkerThread->WaitForCompletion();
		delete WorkerThread;
		WorkerThread = nullptr;
	}
	delete Runnable;
	Runnable = nullptr;

	if (SamplingDoneEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(SamplingDoneEvent);
		SamplingDoneEvent = nullptr;
	}
}

void FShpBuildJob::Start()
{
	if (WorkerThread)
	{
		return;
	}

	Runnable = new FShpJobRunnable(this);
	WorkerThread = FRunnableThread::Create(Runnable, TEXT("ShpToTilesWorker"), 0, TPri_BelowNormal);
	if (!WorkerThread)
	{
		Phase = EShpJobPhase::Failed;
		Log(TEXT("无法创建后台线程"));
	}
}

void FShpBuildJob::Cancel()
{
	bCancelRequested = true;
	if (SamplingDoneEvent)
	{
		SamplingDoneEvent->Trigger();
	}
}

bool FShpBuildJob::IsRunning() const
{
	const EShpJobPhase P = Phase.load();
	return P != EShpJobPhase::Done && P != EShpJobPhase::Failed && P != EShpJobPhase::Cancelled;
}

bool FShpBuildJob::IsFinished() const
{
	return !IsRunning();
}

EShpJobPhase FShpBuildJob::GetPhase() const
{
	return Phase.load();
}

float FShpBuildJob::GetProgress() const
{
	return Progress.load();
}

double FShpBuildJob::GetElapsedSeconds() const
{
	return FPlatformTime::Seconds() - StartSeconds;
}

FString FShpBuildJob::GetPhaseText() const
{
	switch (Phase.load())
	{
	case EShpJobPhase::Idle:
		return TEXT("准备中");
	case EShpJobPhase::Parsing:
		return TEXT("解析 SHP / 属性 / 坐标系");
	case EShpJobPhase::SamplingGround:
		return TEXT("采样地形高度（每要素贴地）");
	case EShpJobPhase::Building:
		return TEXT("拉伸建模 + 分块");
	case EShpJobPhase::Writing:
		return TEXT("写出 tileset.json / b3dm");
	case EShpJobPhase::Written:
		return TEXT("加入场景");
	case EShpJobPhase::Done:
		return TEXT("完成");
	case EShpJobPhase::Failed:
		return TEXT("失败");
	case EShpJobPhase::Cancelled:
		return TEXT("已取消");
	default:
		return FString();
	}
}

void FShpBuildJob::Log(const FString& Line)
{
	FScopeLock Lock(&LogLock);
	LogLines.Add(Line);
	if (LogLines.Num() > MaxReportedLogLines)
	{
		LogLines.RemoveAt(0, LogLines.Num() - MaxReportedLogLines);
	}
	UE_LOG(LogTemp, Display, TEXT("[ShpToTiles] %s"), *Line);
}

TArray<FString> FShpBuildJob::GetLog() const
{
	FScopeLock Lock(&LogLock);
	return LogLines;
}

void FShpBuildJob::RunWorker()
{
	ParseStage();

	if (Phase.load() == EShpJobPhase::Parsing)
	{
		// Parsing finished successfully (phase was advanced inside ParseStage).
	}

	if (bCancelRequested || Phase.load() == EShpJobPhase::Failed)
	{
		Phase = bCancelRequested ? EShpJobPhase::Cancelled : EShpJobPhase::Failed;
		bWorkerFinished = true;
		return;
	}

	BuildStage();

	if (bCancelRequested)
	{
		Phase = EShpJobPhase::Cancelled;
		bWorkerFinished = true;
		return;
	}
	if (Phase.load() == EShpJobPhase::Failed)
	{
		bWorkerFinished = true;
		return;
	}

	WriteStage();
	bWorkerFinished = true;
}

bool FShpBuildJob::SetupSpatialReference(FString& OutError)
{
	SpatialRef = MakeShared<FShpSpatialReference>();

	if (Options.bManualCrs)
	{
		*SpatialRef = FShpSpatialReference::MakeManualGaussKruger(
			Options.ManualCentralMeridian,
			Options.ManualFalseEasting,
			Options.ManualScaleFactor,
			Options.ManualLatitudeOfOrigin);
		Log(TEXT("使用手动坐标参数（横轴墨卡托）"));
		return true;
	}

	const FString& Wkt = Reader->GetPrjWkt();
	if (!Wkt.IsEmpty())
	{
		if (FShpSpatialReference::FromWkt(Wkt, *SpatialRef, OutError))
		{
			return true;
		}
		OutError = FString::Printf(TEXT("%s（可在面板里改用「手动坐标参数」）"), *OutError);
		return false;
	}

	// No .prj: guess from the coordinate range.
	const FShpBounds Bounds = Reader->GetPointBounds();
	const double AbsMax = FMath::Max3(FMath::Abs(Bounds.MinX), FMath::Abs(Bounds.MaxX), FMath::Max(FMath::Abs(Bounds.MinY), FMath::Abs(Bounds.MaxY)));
	if (AbsMax > 360.0)
	{
		OutError = TEXT("数据看起来是投影坐标，但缺少 .prj。请在面板中勾选「手动坐标参数」并填写中央经线/假东。");
		return false;
	}

	*SpatialRef = FShpSpatialReference::MakeGeographicWgs84();
	Log(TEXT("没有 .prj，按 WGS84 经纬度处理"));
	return true;
}

void FShpBuildJob::ParseStage()
{
	Phase = EShpJobPhase::Parsing;
	Progress = 0.02f;

	const double T0 = FPlatformTime::Seconds();

	Options.ShpPath = FPaths::ConvertRelativePathToFull(Options.ShpPath);
	Summary.DatasetName = FPaths::GetBaseFilename(Options.ShpPath);

	Reader = MakeShared<FShapeFileReader>();
	FString Error;
	if (!Reader->Open(Options.ShpPath, Options.Encoding, Error))
	{
		Log(FString::Printf(TEXT("错误: %s"), *Error));
		Phase = EShpJobPhase::Failed;
		return;
	}

	Log(FString::Printf(TEXT("SHP: %s"), *Options.ShpPath));
	Log(FString::Printf(TEXT("几何类型=%s  索引记录=%s  属性字段=%d  编码=%s"),
		Reader->GetShapeTypeName(),
		(Reader->GetDeclaredRecordCount() >= 0) ? *FString::FromInt(Reader->GetDeclaredRecordCount()) : TEXT("?"),
		Reader->GetFields().Num(),
		*Reader->GetResolvedEncodingName()));

	if (!Reader->LoadAllRecords(Error))
	{
		Log(FString::Printf(TEXT("错误: %s"), *Error));
		Phase = EShpJobPhase::Failed;
		return;
	}

	Log(FString::Printf(TEXT("有效几何记录=%d  空记录=%d  解析用时=%.1fs"),
		Reader->GetRecords().Num(), Reader->GetNullRecordCount(), FPlatformTime::Seconds() - T0));

	if (!SetupSpatialReference(Error))
	{
		Log(FString::Printf(TEXT("错误: %s"), *Error));
		Phase = EShpJobPhase::Failed;
		return;
	}

	Summary.CrsText = SpatialRef->Describe();
	Log(TEXT("坐标系: ") + Summary.CrsText);

	// Chinese projected data is often stored with a 3/6-degree zone prefix
	// (e.g. 40500000 instead of 500000). Detect and strip it.
	if (SpatialRef->Method == EShpProjectionMethod::TransverseMercator)
	{
		const FShpBounds Bounds = Reader->GetPointBounds();
		const double MeanX = 0.5 * (Bounds.MinX + Bounds.MaxX);
		if (FMath::Abs(MeanX) > 1.0e6)
		{
			const double Zone = FMath::FloorToDouble(FMath::Abs(MeanX) / 1.0e6);
			SpatialRef->CoordinateShiftX = Zone * 1.0e6;
			Log(FString::Printf(TEXT("检测到带号坐标前缀，已自动剔除 %.0f m（带号 %.0f）"), SpatialRef->CoordinateShiftX, Zone));
		}
	}

	Progress = 0.10f;

	// ------------------------------------------------------------ attribute setup
	int32 HeightFieldIndex = INDEX_NONE;
	if (!Options.HeightField.IsEmpty())
	{
		HeightFieldIndex = Reader->FindField(Options.HeightField);
		if (HeightFieldIndex == INDEX_NONE)
		{
			Log(FString::Printf(TEXT("警告: 找不到高度字段 \"%s\"，改用常量高度"), *Options.HeightField));
		}
	}
	Summary.HeightField = (HeightFieldIndex != INDEX_NONE) ? Options.HeightField : FString();

	int32 ColorFieldIndex = INDEX_NONE;
	if (Options.ColorMode != EShpColorMode::None)
	{
		ColorFieldIndex = Reader->FindField(Options.ColorField);
		if (ColorFieldIndex == INDEX_NONE)
		{
			Log(FString::Printf(TEXT("警告: 找不到着色字段 \"%s\"，本次按单色生成"), *Options.ColorField));
			Options.ColorMode = EShpColorMode::None;
		}
	}
	Summary.ColorField = (ColorFieldIndex != INDEX_NONE) ? Options.ColorField : FString();

	// ------------------------------------------------------------------- geometry
	const TArray<FShpRecord>& Records = Reader->GetRecords();
	Features.Reset();
	Features.Reserve(Records.Num());

	WestDeg = TNumericLimits<double>::Max();
	SouthDeg = TNumericLimits<double>::Max();
	EastDeg = TNumericLimits<double>::Lowest();
	NorthDeg = TNumericLimits<double>::Lowest();

	const int32 Total = Records.Num();
	int32 SkippedRecords = 0;

	for (int32 r = 0; r < Total; ++r)
	{
		if ((r % 512) == 0)
		{
			if (bCancelRequested)
			{
				return;
			}
			Progress = 0.10f + 0.28f * (static_cast<float>(r) / static_cast<float>(FMath::Max(1, Total)));
		}

		const FShpRecord& Rec = Records[r];
		if (Rec.Points.Num() < 3 || Rec.PartStarts.Num() == 0)
		{
			++SkippedRecords;
			continue;
		}

		FFeature Feature;
		Feature.PointsLonLat.Reserve(Rec.Points.Num());
		for (const FVector2D& Pt : Rec.Points)
		{
			Feature.PointsLonLat.Add(SpatialRef->ToLonLat(Pt));
		}

		Feature.PartStarts.Reserve(Rec.PartStarts.Num());
		Feature.PartCounts.Reserve(Rec.PartStarts.Num());
		for (int32 p = 0; p < Rec.PartStarts.Num(); ++p)
		{
			int32 Start = Rec.PartStarts[p];
			const int32 End = (p + 1 < Rec.PartStarts.Num()) ? Rec.PartStarts[p + 1] : Rec.Points.Num();
			Start = FMath::Clamp(Start, 0, Rec.Points.Num());
			const int32 Count = FMath::Max(0, End - Start);
			if (Count < 3)
			{
				continue;
			}
			Feature.PartStarts.Add(Start);
			Feature.PartCounts.Add(Count);
		}

		if (Feature.PartStarts.Num() == 0)
		{
			++SkippedRecords;
			continue;
		}

		// Centroid (mean of all points) used for tile assignment and ground sampling.
		FVector2D Sum = FVector2D::ZeroVector;
		int32 Count = 0;
		for (int32 p = 0; p < Feature.PartStarts.Num(); ++p)
		{
			const int32 Start = Feature.PartStarts[p];
			const int32 Num = Feature.PartCounts[p];
			for (int32 k = 0; k < Num; ++k)
			{
				const FVector2D& Pt = Feature.PointsLonLat[Start + k];
				Sum += Pt;
				++Count;

				WestDeg = FMath::Min(WestDeg, Pt.X);
				SouthDeg = FMath::Min(SouthDeg, Pt.Y);
				EastDeg = FMath::Max(EastDeg, Pt.X);
				NorthDeg = FMath::Max(NorthDeg, Pt.Y);
			}
		}
		Feature.CentroidLonLat = (Count > 0) ? (Sum / static_cast<double>(Count)) : FVector2D::ZeroVector;

		// Attributes
		const int32 DbfIndex = Rec.Number - 1;
		if (HeightFieldIndex != INDEX_NONE)
		{
			Feature.HeightValue = Reader->ReadNumeric(DbfIndex, HeightFieldIndex);
			Feature.ComputedHeight = FMath::Max(0.2, Feature.HeightValue) * Options.FloorHeight;
			if (Options.FloorHeight <= 0.0)
			{
				Feature.ComputedHeight = FMath::Max(0.2, Feature.HeightValue);
			}
		}
		else
		{
			Feature.ComputedHeight = FMath::Max(0.2, Options.ConstantHeight);
		}

		if (ColorFieldIndex != INDEX_NONE)
		{
			Feature.ColorValue = Reader->ReadValue(DbfIndex, ColorFieldIndex);
			Feature.ColorNumber = Reader->ReadNumeric(DbfIndex, ColorFieldIndex);
		}

		Feature.BaseHeight = Options.BaseHeight;

		Features.Add(MoveTemp(Feature));
	}

	if (Features.Num() == 0)
	{
		Log(TEXT("错误: 没有解析出任何可用要素"));
		Phase = EShpJobPhase::Failed;
		return;
	}

	Summary.FeatureCount = Features.Num();
	Summary.SkippedCount = SkippedRecords;
	Summary.WestDeg = WestDeg;
	Summary.SouthDeg = SouthDeg;
	Summary.EastDeg = EastDeg;
	Summary.NorthDeg = NorthDeg;

	Log(FString::Printf(TEXT("要素=%d  跳过=%d  经度=%.6f~%.6f  纬度=%.6f~%.6f"),
		Features.Num(), SkippedRecords, WestDeg, EastDeg, SouthDeg, NorthDeg));

	if (EastDeg <= WestDeg || NorthDeg <= SouthDeg)
	{
		Log(TEXT("错误: 转换后的经纬度范围无效，请检查坐标系设置"));
		Phase = EShpJobPhase::Failed;
		return;
	}

	Progress = 0.40f;

	AssignColors();

	Summary.ParseSeconds = FPlatformTime::Seconds() - T0;
	Log(FString::Printf(TEXT("解析完成，用时 %.1fs"), Summary.ParseSeconds));

	if (Options.bClampToGround)
	{
		Phase = EShpJobPhase::SamplingGround;
		Progress = 0.42f;
		Log(TEXT("开始地形高度采样（每个要素一个采样点）…"));
		SamplingDoneEvent->Wait();
		if (bCancelRequested)
		{
			return;
		}
	}
}

void FShpBuildJob::AssignColors()
{
	CategoryIndexByValue.Reset();
	CategoryColors.Reset();
	ClassThresholds.Reset();

	if (Options.ColorMode == EShpColorMode::None || Features.Num() == 0)
	{
		const FColor Single = ShpColor::ToColor(Options.SingleColor);
		for (FFeature& F : Features)
		{
			F.Color = Single;
		}
		Summary.ColorModeText = TEXT("单色");
		return;
	}

	if (Options.ColorMode == EShpColorMode::Unique)
	{
		TMap<FString, int32> Counts;
		for (const FFeature& F : Features)
		{
			const FString Key = F.ColorValue.IsEmpty() ? TEXT("(空)") : F.ColorValue;
			Counts.FindOrAdd(Key) += 1;
		}

		TArray<TPair<FString, int32>> Pairs;
		Pairs.Reserve(Counts.Num());
		for (const TPair<FString, int32>& Pair : Counts)
		{
			Pairs.Add(Pair);
		}
		Pairs.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
		{
			return A.Value > B.Value;
		});

		const int32 MaxCats = FMath::Clamp(Options.MaxCategories, 1, 64);
		const int32 Used = FMath::Min(Pairs.Num(), MaxCats);
		const TArray<FLinearColor> UniqueRamp =
			ShpColor::CategoryColors(Options.Palette, Used + ((Pairs.Num() > MaxCats) ? 1 : 0));
		CategoryColors.Reserve(UniqueRamp.Num());
		for (const FLinearColor& RampColor : UniqueRamp)
		{
			CategoryColors.Add(ShpColor::ToColor(RampColor));
		}

		for (int32 i = 0; i < Used; ++i)
		{
			CategoryIndexByValue.Add(Pairs[i].Key, i);
		}
		const int32 OtherIndex = (Pairs.Num() > MaxCats) ? Used : INDEX_NONE;

		for (FFeature& F : Features)
		{
			const FString Key = F.ColorValue.IsEmpty() ? TEXT("(空)") : F.ColorValue;
			const int32* Found = CategoryIndexByValue.Find(Key);
			if (Found)
			{
				F.Color = CategoryColors[*Found];
			}
			else if (OtherIndex != INDEX_NONE)
			{
				F.Color = CategoryColors[OtherIndex];
			}
			else
			{
				F.Color = ShpColor::ToColor(Options.SingleColor);
			}
		}

		Summary.ColorModeText = FString::Printf(TEXT("唯一值分类 (%d 类 / 共 %d 个取值)"), Used, Pairs.Num());
		Log(FString::Printf(TEXT("着色: %s"), *Summary.ColorModeText));
		return;
	}

	// ------------------------------------------------------------- numeric classes
	double MinValue = TNumericLimits<double>::Max();
	double MaxValue = TNumericLimits<double>::Lowest();
	for (const FFeature& F : Features)
	{
		MinValue = FMath::Min(MinValue, F.ColorNumber);
		MaxValue = FMath::Max(MaxValue, F.ColorNumber);
	}

	const int32 NumClasses = FMath::Clamp(Options.NumClasses, 2, 32);
	const TArray<FLinearColor> Ramp = ShpColor::CategoryColors(Options.Palette, NumClasses);

	TArray<FColor> Colors;
	Colors.Reserve(Ramp.Num());
	for (const FLinearColor& C : Ramp)
	{
		Colors.Add(ShpColor::ToColor(C));
	}

	const double Span = FMath::Max(1e-9, MaxValue - MinValue);
	for (FFeature& F : Features)
	{
		const double T = FMath::Clamp((F.ColorNumber - MinValue) / Span, 0.0, 1.0);
		const int32 Index = FMath::Clamp(static_cast<int32>(T * (NumClasses - 1) + 0.5), 0, NumClasses - 1);
		F.Color = Colors[Index];
	}

	Summary.ColorModeText = FString::Printf(TEXT("数值分级 (%d 级, %.3f ~ %.3f)"), NumClasses, MinValue, MaxValue);
	Log(FString::Printf(TEXT("着色: %s"), *Summary.ColorModeText));
}

void FShpBuildJob::BuildStage()
{
	Phase = EShpJobPhase::Building;
	Progress = 0.45f;

	GridSize = FMath::Clamp(Options.GridSize, 1, 64);

	// ------------------------------------------------------------- tile frames
	TArray<ShpGeo::FEnuFrame> TileFrames;
	TileFrames.SetNum(GridSize * GridSize);
	Tiles.Reset();
	Tiles.SetNum(GridSize * GridSize);

	for (int32 iy = 0; iy < GridSize; ++iy)
	{
		for (int32 ix = 0; ix < GridSize; ++ix)
		{
			const double Lon = WestDeg + (EastDeg - WestDeg) * ((ix + 0.5) / GridSize);
			const double Lat = SouthDeg + (NorthDeg - SouthDeg) * ((iy + 0.5) / GridSize);

			ShpGeo::FEnuFrame& Frame = TileFrames[iy * GridSize + ix];
			Frame.Init(Lon, Lat, 0.0);

			FShpTileContent& Tile = Tiles[iy * GridSize + ix];
			Tile.Name = FString::Printf(TEXT("tile_%02d_%02d"), ix, iy);
			Tile.FrameEast = Frame.East;
			Tile.FrameUp = Frame.Up;
			Tile.FrameNegNorth = -Frame.North;
			Tile.FrameOrigin = Frame.Origin;
		}
	}

	TArray<double> LonEdges;
	TArray<double> LatEdges;
	LonEdges.SetNum(GridSize + 1);
	LatEdges.SetNum(GridSize + 1);
	for (int32 i = 0; i <= GridSize; ++i)
	{
		LonEdges[i] = WestDeg + (EastDeg - WestDeg) * (static_cast<double>(i) / GridSize);
		LatEdges[i] = SouthDeg + (NorthDeg - SouthDeg) * (static_cast<double>(i) / GridSize);
	}

	// ---------------------------------------------------------------- meshing
	const int32 Total = Features.Num();
	const double MinArea = FMath::Max(0.0, Options.MinFootprintArea);

	int32 Built = 0;
	int32 Skipped = 0;
	int32 Tris = 0;
	double GlobalMinBase = TNumericLimits<double>::Max();
	double GlobalMaxTop = TNumericLimits<double>::Lowest();

	FShpMeshData Mesh;

	for (int32 fi = 0; fi < Total; ++fi)
	{
		if ((fi % 128) == 0)
		{
			if (bCancelRequested)
			{
				return;
			}
			Progress = 0.45f + 0.40f * (static_cast<float>(fi) / static_cast<float>(FMath::Max(1, Total)));
		}

		FFeature& Feature = Features[fi];

		const double LonC = Feature.CentroidLonLat.X;
		const double LatC = Feature.CentroidLonLat.Y;

		// Tile assignment by centroid.
		int32 ix = 0;
		while (ix + 1 < GridSize && LonC > LonEdges[ix + 1])
		{
			++ix;
		}
		int32 iy = 0;
		while (iy + 1 < GridSize && LatC > LatEdges[iy + 1])
		{
			++iy;
		}
		ix = FMath::Clamp(ix, 0, GridSize - 1);
		iy = FMath::Clamp(iy, 0, GridSize - 1);
		const int32 TileIndex = iy * GridSize + ix;
		Feature.TileIndex = TileIndex;

		FShpTileContent& Tile = Tiles[TileIndex];
		const ShpGeo::FEnuFrame& TileFrame = TileFrames[TileIndex];

		// Feature-local ENU frame; origin already lifted to the base height.
		ShpGeo::FEnuFrame Anchor;
		Anchor.Init(LonC, LatC, Feature.BaseHeight);

		const double Kx = ShpGeo::MetresPerDegreeLon(LatC);
		const double Ky = MetresPerDegreeLat;

		// Rings in local metres.
		TArray<TArray<FVector2D>> Rings;
		Rings.Reserve(Feature.PartStarts.Num());
		for (int32 p = 0; p < Feature.PartStarts.Num(); ++p)
		{
			const int32 Start = Feature.PartStarts[p];
			const int32 Num = Feature.PartCounts[p];

			TArray<FVector2D> Ring;
			Ring.Reserve(Num);
			for (int32 k = 0; k < Num; ++k)
			{
				const FVector2D& Pt = Feature.PointsLonLat[Start + k];
				Ring.Add(FVector2D((Pt.X - LonC) * Kx, (Pt.Y - LatC) * Ky));
			}

			Ring = ShpMesh::CleanRing(Ring, 1e-3);
			if (Ring.Num() >= 3)
			{
				Rings.Add(MoveTemp(Ring));
			}
		}

		if (Rings.Num() == 0)
		{
			++Skipped;
			continue;
		}

		TArray<TArray<FVector2D>> Outers;
		TArray<TArray<TArray<FVector2D>>> Holes;
		ShpMesh::ClassifyRings(Rings, Outers, Holes);

		bool bAny = false;
		for (int32 o = 0; o < Outers.Num(); ++o)
		{
			if (FMath::Abs(ShpMesh::SignedArea(Outers[o])) < MinArea)
			{
				continue;
			}

			Mesh.Reset();
			const double Top = FMath::Max(0.2, Feature.ComputedHeight);
			ShpMesh::BuildExtrusion(Outers[o], Holes[o], 0.0, Top, Feature.Color, Options.bCloseBottom, Mesh);
			if (Mesh.Num() == 0 || Mesh.NumTriangles() == 0)
			{
				continue;
			}

			// Transform: local ENU -> ECEF -> tile-local glTF frame.
			const uint32 Base = static_cast<uint32>(Tile.Positions.Num());
			const int32 Verts = Mesh.Num();
			Tile.Positions.Reserve(Tile.Positions.Num() + Verts);
			Tile.Normals.Reserve(Tile.Normals.Num() + Verts);
			Tile.Colors.Reserve(Tile.Colors.Num() + Verts);

			for (int32 v = 0; v < Verts; ++v)
			{
				const FVector& LP = Mesh.Positions[v];
				const FVector Ecef = Anchor.Origin + Anchor.East * LP.X + Anchor.North * LP.Y + Anchor.Up * LP.Z;
				const FVector D = Ecef - Tile.FrameOrigin;

				const FVector3f G(
					static_cast<float>(FVector::DotProduct(D, Tile.FrameEast)),
					static_cast<float>(FVector::DotProduct(D, Tile.FrameUp)),
					static_cast<float>(FVector::DotProduct(D, Tile.FrameNegNorth)));

				Tile.Positions.Add(G);
				Tile.AccumulateBounds(G);
				Tile.Colors.Add(Mesh.Colors[v]);

				const FVector& LN = Mesh.Normals[v];
				const FVector EcefN = Anchor.East * LN.X + Anchor.North * LN.Y + Anchor.Up * LN.Z;
				Tile.Normals.Add(FVector3f(
					static_cast<float>(FVector::DotProduct(EcefN, Tile.FrameEast)),
					static_cast<float>(FVector::DotProduct(EcefN, Tile.FrameUp)),
					static_cast<float>(FVector::DotProduct(EcefN, Tile.FrameNegNorth))));
			}

			Tile.Indices.Reserve(Tile.Indices.Num() + Mesh.Indices.Num());
			for (uint32 Index : Mesh.Indices)
			{
				Tile.Indices.Add(Base + Index);
			}

			Tris += Mesh.NumTriangles();
			GlobalMinBase = FMath::Min(GlobalMinBase, Feature.BaseHeight);
			GlobalMaxTop = FMath::Max(GlobalMaxTop, Feature.BaseHeight + Top);
			bAny = true;
		}

		if (bAny)
		{
			++Built;
		}
		else
		{
			++Skipped;
		}
	}

	Summary.TriangleCount = Tris;
	Summary.SkippedCount += Skipped;
	Summary.MinBaseHeight = (GlobalMinBase == TNumericLimits<double>::Max()) ? Options.BaseHeight : GlobalMinBase;
	Summary.MaxTopHeight = (GlobalMaxTop == TNumericLimits<double>::Lowest()) ? Options.BaseHeight + Options.ConstantHeight : GlobalMaxTop;

	int32 NonEmpty = 0;
	for (const FShpTileContent& Tile : Tiles)
	{
		if (!Tile.IsEmpty())
		{
			++NonEmpty;
		}
	}
	Summary.TileCount = NonEmpty;

	Log(FString::Printf(TEXT("建模完成: 要素=%d  跳过=%d  瓦片=%d  三角形=%d"),
		Built, Skipped, NonEmpty, Tris));

	if (Built == 0)
	{
		Log(TEXT("错误: 没有生成任何几何（检查最小面积过滤 / 坐标系 / 高度字段）"));
		Phase = EShpJobPhase::Failed;
	}
}

void FShpBuildJob::WriteStage()
{
	Phase = EShpJobPhase::Writing;
	Progress = 0.88f;

	FShpTilesetSettings Settings;
	Settings.OutDir = Options.OutDir;
	Settings.DatasetName = Summary.DatasetName;
	Settings.WestDeg = WestDeg;
	Settings.SouthDeg = SouthDeg;
	Settings.EastDeg = EastDeg;
	Settings.NorthDeg = NorthDeg;
	Settings.MinHeight = Summary.MinBaseHeight;
	Settings.MaxHeight = Summary.MaxTopHeight;
	Settings.bHasColors = (Options.ColorMode != EShpColorMode::None);
	Settings.bUnlit = Options.bUnlit;
	Settings.bDoubleSided = Options.bDoubleSided;
	Settings.SingleColor = Options.SingleColor;

	int32 TriangleCount = 0;
	int64 ByteCount = 0;
	FString TilesetPath;
	FString Error;

	if (!ShpTiles::WriteTileset(Settings, Tiles, TriangleCount, ByteCount, TilesetPath, Error))
	{
		Log(FString::Printf(TEXT("错误: %s"), *Error));
		Phase = EShpJobPhase::Failed;
		return;
	}

	Summary.TriangleCount = TriangleCount;
	Summary.ByteCount = ByteCount;
	Summary.TilesetPath = TilesetPath;
	Summary.OutDir = FPaths::ConvertRelativePathToFull(Options.OutDir);
	Summary.TotalSeconds = FPlatformTime::Seconds() - StartSeconds;

	Log(FString::Printf(TEXT("写出完成: %s"), *TilesetPath));
	Log(FString::Printf(TEXT("瓦片=%d  三角形=%d  体积=%.1f MB  总用时=%.1fs"),
		Summary.TileCount, Summary.TriangleCount, ByteCount / (1024.0 * 1024.0), Summary.TotalSeconds));

	// Sidecar report, handy for scripting / verification.
	FString Meta;
	Meta += TEXT("{\n");
	Meta += FString::Printf(TEXT(" \"source\": \"%s\",\n"), *Options.ShpPath.Replace(TEXT("\\"), TEXT("\\\\")));
	Meta += FString::Printf(TEXT(" \"crs\": \"%s\",\n"), *Summary.CrsText.Replace(TEXT("\""), TEXT("'")));
	Meta += FString::Printf(TEXT(" \"features\": %d,\n"), Summary.FeatureCount);
	Meta += FString::Printf(TEXT(" \"skipped\": %d,\n"), Summary.SkippedCount);
	Meta += FString::Printf(TEXT(" \"tiles\": %d,\n"), Summary.TileCount);
	Meta += FString::Printf(TEXT(" \"triangles\": %d,\n"), Summary.TriangleCount);
	Meta += FString::Printf(TEXT(" \"bytes\": %lld,\n"), Summary.ByteCount);
	Meta += FString::Printf(TEXT(" \"bbox\": [%.9f, %.9f, %.9f, %.9f],\n"), WestDeg, SouthDeg, EastDeg, NorthDeg);
	Meta += FString::Printf(TEXT(" \"heightField\": \"%s\",\n"), *Summary.HeightField);
	Meta += FString::Printf(TEXT(" \"floorHeight\": %.4f,\n"), Options.FloorHeight);
	Meta += FString::Printf(TEXT(" \"constantHeight\": %.4f,\n"), Options.ConstantHeight);
	Meta += FString::Printf(TEXT(" \"colorField\": \"%s\",\n"), *Summary.ColorField);
	Meta += FString::Printf(TEXT(" \"colorMode\": \"%s\",\n"), *Summary.ColorModeText.Replace(TEXT("\""), TEXT("'")));
	Meta += FString::Printf(TEXT(" \"clampToGround\": %s,\n"), Options.bClampToGround ? TEXT("true") : TEXT("false"));
	Meta += FString::Printf(TEXT(" \"clampedFeatures\": %d,\n"), Summary.ClampedFeatures);
	Meta += FString::Printf(TEXT(" \"terrainSampling\": %s,\n"), Summary.bUsedTerrainSampling ? TEXT("true") : TEXT("false"));
	Meta += FString::Printf(TEXT(" \"baseHeight\": %.4f,\n"), Options.BaseHeight);
	Meta += FString::Printf(TEXT(" \"minBaseHeight\": %.4f,\n"), Summary.MinBaseHeight);
	Meta += FString::Printf(TEXT(" \"maxTopHeight\": %.4f,\n"), Summary.MaxTopHeight);
	Meta += FString::Printf(TEXT(" \"grid\": %d\n"), GridSize);
	Meta += TEXT("}\n");

	FFileHelper::SaveStringToFile(Meta, *(FPaths::ConvertRelativePathToFull(Options.OutDir) / TEXT("meta.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	Progress = 0.98f;
	Phase = EShpJobPhase::Written;
}

// ------------------------------------------------------------------------- sampling

ACesium3DTileset* FShpBuildJob::FindHeightSourceTileset() const
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	ACesium3DTileset* FirstValid = nullptr;
	for (TActorIterator<ACesium3DTileset> It(World); It; ++It)
	{
		ACesium3DTileset* Tileset = *It;
		if (!Tileset)
		{
			continue;
		}

		if (!FirstValid)
		{
			FirstValid = Tileset;
		}

		const FString Url = Tileset->GetUrl();
		if (Url.Contains(TEXT("terrain"), ESearchCase::IgnoreCase) ||
			Tileset->GetIonAssetID() == 1)
		{
			return Tileset;
		}
	}
	return FirstValid;
}

void FShpBuildJob::BeginGroundSampling()
{
	SamplingStartSeconds = FPlatformTime::Seconds();
	SampleSource = FindHeightSourceTileset();

	if (!SampleSource)
	{
		Log(TEXT("[贴地] 场景里没有 Cesium3DTileset 可用于采样，改用基准高程"));
		ApplyFallbackHeights(0);
		CompleteGroundSampling(false);
		return;
	}

	Summary.SamplingSource = SampleSource->GetActorLabel();
	BuildGroundGrid();

	// Fast path: cast rays onto the terrain that is already rendered. It needs no tile
	// streaming and matches exactly what the user sees, so it is tried before the
	// (potentially very slow) max-detail height sampler.
	FillMissingHeightsByTrace();
	if (GroundValidCount == GroundPoints.Num())
	{
		Log(FString::Printf(
			TEXT("[贴地] 采样源: %s  高程栅格=%d x %d，射线已命中全部 %d 个点"),
			*Summary.SamplingSource,
			GroundGridX,
			GroundGridY,
			GroundValidCount));
		ApplyGroundHeights();
		CompleteGroundSampling(true);
		return;
	}

	Log(FString::Printf(
		TEXT("[贴地] 采样源: %s  要素=%d  高程栅格=%d x %d (%d 个采样点，射线先命中 %d 个)"),
		*Summary.SamplingSource,
		Features.Num(),
		GroundGridX,
		GroundGridY,
		GroundPoints.Num(),
		GroundValidCount));
	SampleNodes.Reset();
	for (int32 Node = 0; Node < GroundPoints.Num(); ++Node)
	{
		if (!GroundValid[Node])
		{
			SampleNodes.Add(Node);
		}
	}
	if (SampleNodes.IsEmpty())
	{
		ApplyGroundHeights();
		CompleteGroundSampling(true);
		return;
	}
	Log(FString::Printf(TEXT("[贴地] 射线命中 %d 个栅格点，剩余 %d 个交给地形采样"), GroundPoints.Num() - SampleNodes.Num(), SampleNodes.Num()));

	GroundCursor = 0;
	IssueNextSampleBatch();
}

void FShpBuildJob::BuildGroundGrid()
{
	GroundPoints.Reset();
	GroundHeights.Reset();
	GroundValid.Reset();
	GroundValidCount = 0;

	// Rays against the already-rendered terrain are cheap, so the raster can be a bit
	// finer; only the nodes they miss go to the (much slower) height sampler.
	int32 Cells = FMath::Clamp(Options.GridSize / 2, 3, 10);
	while ((int64)(Cells + 1) * (int64)(Cells + 1) > 4096 && Cells > 8)
	{
		--Cells;
	}
	GroundGridX = Cells + 1;
	GroundGridY = Cells + 1;

	const int32 Total = GroundGridX * GroundGridY;
	GroundPoints.Reserve(Total);
	GroundHeights.Init(static_cast<float>(Options.BaseHeight), Total);
	GroundValid.Init(0, Total);

	for (int32 j = 0; j < GroundGridY; ++j)
	{
		const double T = static_cast<double>(j) / static_cast<double>(GroundGridY - 1);
		const double Lat = SouthDeg + (NorthDeg - SouthDeg) * T;
		for (int32 i = 0; i < GroundGridX; ++i)
		{
			const double S = static_cast<double>(i) / static_cast<double>(GroundGridX - 1);
			const double Lon = WestDeg + (EastDeg - WestDeg) * S;
			GroundPoints.Add(FVector(Lon, Lat, Options.BaseHeight));
		}
	}
}

void FShpBuildJob::IssueNextSampleBatch()
{
	const int32 Total = SampleNodes.Num();
	if (GroundCursor >= Total)
	{
		FillMissingHeightsByTrace();
		ApplyGroundHeights();
		Log(FString::Printf(TEXT("[贴地] 采样结束: 有效采样点 %d / %d"), GroundValidCount, Total));
		CompleteGroundSampling(GroundValidCount > 0);
		return;
	}

	const int32 Count = FMath::Min(GroundBatchSize, Total - GroundCursor);
	const int32 GridStart = GroundCursor;

	TArray<FVector> Points;
	Points.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		Points.Add(GroundPoints[SampleNodes[GridStart + i]]);
	}
	GroundCursor += Count;

	if (!SampleSource)
	{
		ApplyFallbackHeights(0);
		CompleteGroundSampling(false);
		return;
	}

	bSamplingInFlight = true;

	TSharedPtr<FShpBuildJob> Self = SharedThis(this);
	SampleSource->SampleHeightMostDetailed(
		Points,
		FCesiumSampleHeightMostDetailedCallback::CreateLambda(
			[Self, GridStart](ACesium3DTileset* Tileset, const TArray<FCesiumSampleHeightResult>& Results, const TArray<FString>& Warnings)
			{
				if (Self.IsValid())
				{
					Self->OnBatchSampled(GridStart, Results, Warnings);
				}
			}));

	Progress = 0.42f + 0.03f * (static_cast<float>(GroundCursor) / static_cast<float>(FMath::Max(1, Total)));
}

void FShpBuildJob::OnBatchSampled(int32 GridStart, const TArray<FCesiumSampleHeightResult>& Results, const TArray<FString>& Warnings)
{
	if (bSamplingAborted)
	{
		return;
	}
	bSamplingInFlight = false;

	int32 Failed = 0;
	const int32 Count = FMath::Min(Results.Num(), SampleNodes.Num() - GridStart);
	for (int32 i = 0; i < Count; ++i)
	{
		const int32 Node = SampleNodes[GridStart + i];
		if (Results[i].SampleSuccess)
		{
			GroundHeights[Node] = static_cast<float>(Results[i].LongitudeLatitudeHeight.Z);
			GroundValid[Node] = 1;
			++GroundValidCount;
		}
		else
		{
			++Failed;
		}
	}

	if (Failed > 0 && Failed == Count && Warnings.Num() > 0)
	{
		Log(FString::Printf(TEXT("[贴地] 采样警告: %s"), *Warnings[0]));
	}

	if (GroundCursor < SampleNodes.Num())
	{
		IssueNextSampleBatch();
		return;
	}

	FillMissingHeightsByTrace();
	ApplyGroundHeights();
	Log(FString::Printf(TEXT("[贴地] 采样结束: 有效采样点 %d / %d"), GroundValidCount, GroundPoints.Num()));
	CompleteGroundSampling(GroundValidCount > 0);
}

void FShpBuildJob::FillMissingHeightsByTrace()
{
	// Second chance for nodes the tileset height sampler did not resolve: cast a ray
	// straight down onto whatever terrain geometry is actually rendered right now.
	// This uses the streamed (visible) LOD instead of waiting for max-LOD tiles, which
	// is what makes clamping work even when SampleHeightMostDetailed never answers.
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return;
	}

	ACesiumGeoreference* Georef = ACesiumGeoreference::GetDefaultGeoreference(World);
	if (!Georef)
	{
		return;
	}

	int32 Traced = 0;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShpToTilesGroundTrace), true);
	// Never ignore SampleSource: that IS the terrain we want to hit. Only skip the
	// tilesets this plugin generated (they carry no collision anyway).
	for (TActorIterator<ACesium3DTileset> It(World); It; ++It)
	{
		if (It->GetActorLabel().StartsWith(TEXT("Shp3DTiles")))
		{
			Params.AddIgnoredActor(*It);
		}
	}

	for (int32 Node = 0; Node < GroundPoints.Num(); ++Node)
	{
		if (GroundValid[Node])
		{
			continue;
		}

		const FVector& LL = GroundPoints[Node];
		const FVector Top = Georef->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(LL.X, LL.Y, 6000.0));
		const FVector Bottom = Georef->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(LL.X, LL.Y, -1500.0));

		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Top, Bottom, ECC_Visibility, Params) && Hit.bBlockingHit)
		{
			const FVector LLH = Georef->TransformUnrealPositionToLongitudeLatitudeHeight(Hit.ImpactPoint);
			GroundHeights[Node] = static_cast<float>(LLH.Z);
			GroundValid[Node] = 1;
			++GroundValidCount;
			++Traced;
		}
	}

	if (Traced > 0)
	{
		Log(FString::Printf(TEXT("[贴地] 射线补齐 %d 个栅格点（基于当前已渲染的地形网格）"), Traced));
	}
}

void FShpBuildJob::ApplyGroundHeights()
{
	if (GroundGridX < 2 || GroundGridY < 2 || GroundValidCount == 0)
	{
		ApplyFallbackHeights(0);
		return;
	}

	const double LonSpan = EastDeg - WestDeg;
	const double LatSpan = NorthDeg - SouthDeg;
	const int32 XMax = GroundGridX - 1;
	const int32 YMax = GroundGridY - 1;

	// Mean of the valid nodes: last resort when nothing else is available.
	double ValidSum = 0.0;
	for (int32 Node = 0; Node < GroundValid.Num(); ++Node)
	{
		if (GroundValid[Node])
		{
			ValidSum += static_cast<double>(GroundHeights[Node]);
		}
	}
	const double MeanValid = GroundValidCount > 0 ? ValidSum / GroundValidCount : Options.BaseHeight;

	// Nearest sampled node: used for cells whose four corners were never sampled, so a
	// feature far from any sample still follows the terrain trend of its closest node.
	TArray<float> NearestHeight;
	NearestHeight.Init(static_cast<float>(MeanValid), GroundValid.Num());
	for (int32 J = 0; J < GroundGridY; ++J)
	{
		for (int32 I = 0; I < GroundGridX; ++I)
		{
			double BestDist = TNumericLimits<double>::Max();
			float BestHeight = static_cast<float>(MeanValid);
			for (int32 Jj = 0; Jj < GroundGridY; ++Jj)
			{
				for (int32 Ii = 0; Ii < GroundGridX; ++Ii)
				{
					const int32 Node = Jj * GroundGridX + Ii;
					if (!GroundValid[Node])
					{
						continue;
					}
					const double D = FMath::Square(static_cast<double>(Ii - I)) + FMath::Square(static_cast<double>(Jj - J));
					if (D < BestDist)
					{
						BestDist = D;
						BestHeight = GroundHeights[Node];
					}
				}
			}
			NearestHeight[J * GroundGridX + I] = BestHeight;
		}
	}
	auto NearestValidHeight = [&NearestHeight, this](int32 I, int32 J) -> double
	{
		return static_cast<double>(NearestHeight[J * GroundGridX + I]) + Options.ClampOffset;
	};

	for (FFeature& Feature : Features)
	{
		double U = LonSpan > KINDA_SMALL_NUMBER
			? (Feature.CentroidLonLat.X - WestDeg) / LonSpan * XMax
			: 0.0;
		double V = LatSpan > KINDA_SMALL_NUMBER
			? (Feature.CentroidLonLat.Y - SouthDeg) / LatSpan * YMax
			: 0.0;
		U = FMath::Clamp(U, 0.0, static_cast<double>(XMax));
		V = FMath::Clamp(V, 0.0, static_cast<double>(YMax));

		const int32 I0 = FMath::Clamp(static_cast<int32>(U), 0, XMax);
		const int32 J0 = FMath::Clamp(static_cast<int32>(V), 0, YMax);
		const int32 I1 = FMath::Min(I0 + 1, XMax);
		const int32 J1 = FMath::Min(J0 + 1, YMax);
		const double FU = U - I0;
		const double FV = V - J0;

		// bilinear over the valid nodes only; falls back to the base height when the
		// whole neighbourhood failed to sample.
		double Sum = 0.0;
		double WeightSum = 0.0;
		auto Accumulate = [&](int32 I, int32 J, double W)
		{
			const int32 Node = J * GroundGridX + I;
			if (GroundValid[Node])
			{
				Sum += static_cast<double>(GroundHeights[Node]) * W;
				WeightSum += W;
			}
		};
		Accumulate(I0, J0, (1.0 - FU) * (1.0 - FV));
		Accumulate(I1, J0, FU * (1.0 - FV));
		Accumulate(I0, J1, (1.0 - FU) * FV);
		Accumulate(I1, J1, FU * FV);

		Feature.BaseHeight = WeightSum > KINDA_SMALL_NUMBER
			? Sum / WeightSum + Options.ClampOffset
			: NearestValidHeight(I0, J0);
		if (WeightSum > KINDA_SMALL_NUMBER)
		{
			++Summary.ClampedFeatures;
		}
	}

	Summary.MinBaseHeight = TNumericLimits<double>::Max();
	Summary.MaxBaseHeight = TNumericLimits<double>::Lowest();
	for (const FFeature& Feature : Features)
	{
		Summary.MinBaseHeight = FMath::Min(Summary.MinBaseHeight, Feature.BaseHeight);
		Summary.MaxBaseHeight = FMath::Max(Summary.MaxBaseHeight, Feature.BaseHeight);
	}
}

void FShpBuildJob::ApplyFallbackHeights(int32 FromIndex)
{
	for (int32 i = FMath::Max(0, FromIndex); i < Features.Num(); ++i)
	{
		Features[i].BaseHeight = Options.BaseHeight + Options.ClampOffset;
	}
}

void FShpBuildJob::CompleteGroundSampling(bool bUsedSampling)
{
	bSamplingInFlight = false;
	Summary.bUsedTerrainSampling = bUsedSampling;
	if (!bUsedSampling)
	{
		ApplyFallbackHeights(0);
	}

	Phase = EShpJobPhase::Building;
	if (SamplingDoneEvent)
	{
		SamplingDoneEvent->Trigger();
	}
}

void FShpBuildJob::SpawnTilesetActor()
{
	if (!Options.bAddToScene)
	{
		return;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		Log(TEXT("警告: 没有可用的编辑器世界，未加入场景"));
		return;
	}

	FString Url = Summary.TilesetPath;
	Url.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (!Url.StartsWith(TEXT("file://")))
	{
		Url = TEXT("file:///") + Url;
	}

	FString Label = FString::Printf(TEXT("Shp3DTiles_%s"), *Summary.DatasetName);

	// Keep the label unique so repeated imports stay distinguishable.
	TSet<FString> ExistingLabels;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->IsA<ACesium3DTileset>())
		{
			ExistingLabels.Add(It->GetActorLabel());
		}
	}
	if (ExistingLabels.Contains(Label))
	{
		for (int32 Suffix = 2; Suffix < 1000; ++Suffix)
		{
			const FString Candidate = FString::Printf(TEXT("%s_%d"), *Label, Suffix);
			if (!ExistingLabels.Contains(Candidate))
			{
				Label = Candidate;
				break;
			}
		}
	}

	const FScopedTransaction Transaction(NSLOCTEXT("ShpToTiles", "AddTileset", "SHP 3D Tiles 加入场景"));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Do NOT force Params.Name here: actor object names live in the ULevel outer, so a
	// name made unique against the UWorld will collide and abort the editor with
	// "Cannot generate unique name for ... in level". Let the engine pick the object
	// name and only drive the (non-unique-constrained) actor label below.
	Params.Name = NAME_None;

	ACesium3DTileset* Actor = World->SpawnActor<ACesium3DTileset>(ACesium3DTileset::StaticClass(), FTransform::Identity, Params);
	if (!Actor)
	{
		Log(TEXT("警告: 创建 Cesium3DTileset 失败"));
		return;
	}

	Actor->SetTilesetSource(ETilesetSource::FromUrl);
	Actor->SetUrl(Url);
	Actor->SetCreatePhysicsMeshes(false);
	Actor->SetActorLabel(Label);
	Actor->SetFolderPath(FName(TEXT("ShpToTiles")));

	SpawnedActor = Actor;
	Log(FString::Printf(TEXT("已加入场景: %s  ->  %s"), *Label, *Url));
}

void FShpBuildJob::TickGameThread()
{
	switch (Phase.load())
	{
	case EShpJobPhase::SamplingGround:
	{
		if (!bSamplingStarted)
		{
			bSamplingStarted = true;
			BeginGroundSampling();
		}
		else if ((FPlatformTime::Seconds() - SamplingStartSeconds) > SamplingTimeoutSeconds)
		{
			// Also fires while a batch is still in flight: SampleHeightMostDetailed can
			// never call back when the terrain tiles covering the requested points are
			// not streamed in, so waiting on it forever would hang the job.
			bSamplingAborted = true;
			bSamplingInFlight = false;
			Log(FString::Printf(
				TEXT("[贴地] 采样超时（%.0f 秒），已采样到 %d 个栅格点，改用射线补齐"),
				SamplingTimeoutSeconds,
				GroundValidCount));
			FillMissingHeightsByTrace();
			if (GroundValidCount > 0)
			{
				ApplyGroundHeights();
			}
			else
			{
				ApplyFallbackHeights(0);
			}
			CompleteGroundSampling(GroundValidCount > 0);
		}
		break;
	}
	case EShpJobPhase::Written:
	{
		SpawnTilesetActor();
		Progress = 1.0f;
		Phase = EShpJobPhase::Done;
		break;
	}
	default:
		break;
	}
}
