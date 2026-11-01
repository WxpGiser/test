// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/Event.h"
#include "ShpTypes.h"
#include "ShpTilesWriter.h"

#include <atomic>

class FShapeFileReader;
class FShpSpatialReference;
class FShpJobRunnable;
class ACesium3DTileset;
class AActor;
struct FCesiumSampleHeightResult;

enum class EShpJobPhase : uint8
{
	Idle,
	Parsing,
	SamplingGround,
	Building,
	Writing,
	Written,
	Done,
	Failed,
	Cancelled
};

/** Everything the panel / console wants to report after a run. */
struct FShpJobSummary
{
	int32 FeatureCount = 0;
	int32 SkippedCount = 0;
	int32 TileCount = 0;
	int32 TriangleCount = 0;
	int64 ByteCount = 0;

	double ParseSeconds = 0.0;
	double TotalSeconds = 0.0;

	FString TilesetPath;
	FString OutDir;
	FString DatasetName;

	double WestDeg = 0.0;
	double SouthDeg = 0.0;
	double EastDeg = 0.0;
	double NorthDeg = 0.0;

	double MinBaseHeight = 0.0;
	double MaxBaseHeight = 0.0;
	double MaxTopHeight = 0.0;

	bool bClamped = false;
	int32 ClampedFeatures = 0;
	bool bUsedTerrainSampling = false;
	FString SamplingSource;

	FString HeightField;
	FString ColorField;
	FString ColorModeText;
	FString CrsText;
};

/**
 * Runs one SHP -> 3D Tiles conversion.
 *
 * Heavy work (parse / mesh build / file writing) happens on a worker thread.
 * Anything that touches UObjects (Cesium height sampling, actor spawning) is done
 * from TickGameThread(), which the module calls on the game thread every frame.
 */
class FShpBuildJob : public TSharedFromThis<FShpBuildJob>
{
public:
	explicit FShpBuildJob(const FShpBuildOptions& InOptions);
	~FShpBuildJob();

	void Start();
	void TickGameThread();
	void Cancel();

	bool IsRunning() const;
	bool IsFinished() const;
	EShpJobPhase GetPhase() const;
	FString GetPhaseText() const;
	float GetProgress() const;
	double GetElapsedSeconds() const;

	TArray<FString> GetLog() const;
	void Log(const FString& Line);

	const FShpJobSummary& GetSummary() const { return Summary; }
	const FShpBuildOptions& GetOptions() const { return Options; }

	AActor* GetSpawnedActor() const { return SpawnedActor.Get(); }
	TWeakObjectPtr<AActor> SpawnedActor;

private:
	void RunWorker();

	void ParseStage();
	void BuildStage();
	void WriteStage();

	bool SetupSpatialReference(FString& OutError);
	void AssignColors();
	void AssignTileIndices();

	// game thread side
	void BeginGroundSampling();
	void BuildGroundGrid();
	void IssueNextSampleBatch();
	void OnBatchSampled(int32 GridStart, const TArray<FCesiumSampleHeightResult>& Results, const TArray<FString>& Warnings);
	void CompleteGroundSampling(bool bUsedSampling);
	void FillMissingHeightsByTrace();
	void ApplyGroundHeights();
	void ApplyFallbackHeights(int32 FromIndex);
	ACesium3DTileset* FindHeightSourceTileset() const;
	void SpawnTilesetActor();

	struct FFeature
	{
		TArray<FVector2D> PointsLonLat;
		TArray<int32> PartStarts;
		TArray<int32> PartCounts;

		FVector2D CentroidLonLat = FVector2D::ZeroVector;
		double HeightValue = 0.0;
		double ComputedHeight = 0.0;
		double BaseHeight = 0.0;

		FString ColorValue;
		double ColorNumber = 0.0;
		FColor Color = FColor::White;

		int32 TileIndex = 0;
	};

	FShpBuildOptions Options;
	FShpJobSummary Summary;

	TSharedPtr<FShapeFileReader> Reader;
	TSharedPtr<FShpSpatialReference> SpatialRef;

	TArray<FFeature> Features;
	TArray<FShpTileContent> Tiles;

	// grid geometry (lon/lat degrees)
	double WestDeg = 0.0;
	double SouthDeg = 0.0;
	double EastDeg = 0.0;
	double NorthDeg = 0.0;
	int32 GridSize = 16;

	// color classification
	TMap<FString, int32> CategoryIndexByValue;
	TArray<FColor> CategoryColors;
	TArray<double> ClassThresholds;

	// threading
	std::atomic<EShpJobPhase> Phase{EShpJobPhase::Idle};
	std::atomic<float> Progress{0.0f};
	std::atomic<bool> bCancelRequested{false};
	std::atomic<bool> bWorkerFinished{false};
	FEvent* SamplingDoneEvent = nullptr;
	FRunnableThread* WorkerThread = nullptr;
	double StartSeconds = 0.0;

	mutable FCriticalSection LogLock;
	TArray<FString> LogLines;

	// sampling state (game thread)
	ACesium3DTileset* SampleSource = nullptr;
	bool bSamplingInFlight = false;
	bool bSamplingStarted = false;
	bool bSamplingAborted = false;
	double SamplingStartSeconds = 0.0;
	constexpr static double SamplingTimeoutSeconds = 25.0;

	// Ground height raster: a lon/lat node grid sampled from the terrain tileset.
	// Sampling a coarse grid instead of every feature keeps the request count small
	// (a few thousand nodes regardless of feature count) and interpolates per feature.
	TArray<FVector> GroundPoints;
	TArray<float> GroundHeights;
	TArray<uint8> GroundValid;
	int32 GroundGridX = 0;
	int32 GroundGridY = 0;
	TArray<int32> SampleNodes;    // grid nodes the trace pass could not resolve
	int32 GroundCursor = 0;
	int32 GroundBatchSize = 1024;
	int32 GroundValidCount = 0;

	class FShpJobRunnable* Runnable = nullptr;

	friend class FShpJobRunnable;
};
