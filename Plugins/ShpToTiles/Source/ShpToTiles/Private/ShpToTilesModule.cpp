// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#include "ShpToTilesModule.h"

#include "SShpToTilesPanel.h"
#include "ShpBuildJob.h"

#include "Cesium3DTileset.h"
#include "CesiumSampleHeightResult.h"
#include "EngineUtils.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/ConsoleManager.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "ShpToTilesModule"

namespace
{
	FShpToTilesModule* GShpToTilesModule = nullptr;

	FString EscapeJson(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Out.ReplaceInline(TEXT("\r"), TEXT(""));
		Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		return Out;
	}

	const TCHAR* PhaseToString(EShpJobPhase Phase)
	{
		switch (Phase)
		{
		case EShpJobPhase::Idle: return TEXT("idle");
		case EShpJobPhase::Parsing: return TEXT("parsing");
		case EShpJobPhase::SamplingGround: return TEXT("sampling");
		case EShpJobPhase::Building: return TEXT("building");
		case EShpJobPhase::Writing: return TEXT("writing");
		case EShpJobPhase::Written: return TEXT("adding");
		case EShpJobPhase::Done: return TEXT("done");
		case EShpJobPhase::Failed: return TEXT("failed");
		case EShpJobPhase::Cancelled: return TEXT("cancelled");
		default: return TEXT("unknown");
		}
	}
}

FShpToTilesModule& FShpToTilesModule::Get()
{
	checkf(GShpToTilesModule, TEXT("ShpToTiles module is not loaded"));
	return *GShpToTilesModule;
}

void FShpToTilesModule::StartupModule()
{
	GShpToTilesModule = this;

	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(GetTabName(), FOnSpawnTab::CreateRaw(this, &FShpToTilesModule::OnSpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "SHP 导入"))
		.SetTooltipText(LOCTEXT("TabTip", "SHP → 3D Tiles 导入面板"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::Get()->RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FShpToTilesModule::RegisterMenus));
	}

	RegisterConsoleCommands();

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FShpToTilesModule::Tick), 0.0f);
}

void FShpToTilesModule::ShutdownModule()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);

	UnregisterConsoleCommands();

	if (ActiveJob.IsValid())
	{
		ActiveJob->Cancel();
		ActiveJob.Reset();
	}

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GetTabName());
	}

	GShpToTilesModule = nullptr;
}

TSharedRef<SDockTab> FShpToTilesModule::OnSpawnTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SShpToTilesPanel> Panel = SNew(SShpToTilesPanel);
	PanelWidget = Panel;
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("TabLabel", "SHP 导入"))
		[
			Panel
		];
}

void FShpToTilesModule::OpenPanel()
{
	const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(FTabId(GetTabName()));
	if (Tab.IsValid())
	{

	}
}

void FShpToTilesModule::RegisterMenus()
{
	UToolMenus* Menus = UToolMenus::Get();
	if (!Menus)
	{
		return;
	}

	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolsMenu = Menus->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (!ToolsMenu)
	{
		return;
	}

	FToolMenuSection& Section = ToolsMenu->FindOrAddSection(
		TEXT("ShpToTiles"), LOCTEXT("ShpToTilesSection", "SHP / 3D Tiles"));

	Section.AddMenuEntry(
		"ShpToTiles.OpenPanel",
		LOCTEXT("OpenPanelEntry", "SHP → 3D Tiles 导入…"),
		LOCTEXT("OpenPanelEntryTip", "打开 SHP 导入面板，生成 3D Tiles 并加入场景"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FShpToTilesModule::OpenPanel)));
}

bool FShpToTilesModule::StartJob(const FShpBuildOptions& Options, FString& OutError)
{
	if (ActiveJob.IsValid() && ActiveJob->IsRunning())
	{
		OutError = TEXT("已有导入任务在运行，请等待完成或取消");
		return false;
	}

	ActiveJob = MakeShared<FShpBuildJob>(Options);
	ActiveJob->Start();
	return true;
}

TArray<FString> FShpToTilesModule::GetActiveLog() const
{
	return ActiveJob.IsValid() ? ActiveJob->GetLog() : TArray<FString>();
}

FString FShpToTilesModule::GetStatusJson() const
{
	if (!ActiveJob.IsValid())
	{
		return TEXT("{\"phase\":\"none\"}");
	}

	const FShpJobSummary& Summary = ActiveJob->GetSummary();
	const TArray<FString> Lines = ActiveJob->GetLog();

	FString Log;
	for (const FString& Line : Lines)
	{
		if (!Log.IsEmpty())
		{
			Log += TEXT("\n");
		}
		Log += Line;
	}

	return FString::Printf(
		TEXT("{\"phase\":\"%s\",\"phaseText\":\"%s\",\"running\":%s,\"progress\":%.4f,\"elapsed\":%.2f,"
			 "\"features\":%d,\"skipped\":%d,\"tiles\":%d,\"triangles\":%d,\"bytes\":%lld,"
			 "\"tileset\":\"%s\",\"outDir\":\"%s\",\"clamped\":%s,\"usedTerrain\":%s,\"crs\":\"%s\",\"log\":\"%s\"}"),
		PhaseToString(ActiveJob->GetPhase()),
		*EscapeJson(ActiveJob->GetPhaseText()),
		ActiveJob->IsRunning() ? TEXT("true") : TEXT("false"),
		ActiveJob->GetProgress(),
		ActiveJob->GetElapsedSeconds(),
		Summary.FeatureCount,
		Summary.SkippedCount,
		Summary.TileCount,
		Summary.TriangleCount,
		Summary.ByteCount,
		*EscapeJson(Summary.TilesetPath),
		*EscapeJson(Summary.OutDir),
		Summary.bClamped ? TEXT("true") : TEXT("false"),
		Summary.bUsedTerrainSampling ? TEXT("true") : TEXT("false"),
		*EscapeJson(Summary.CrsText),
		*EscapeJson(Log));
}

bool FShpToTilesModule::Tick(float DeltaTime)
{
	if (ActiveJob.IsValid())
	{
		ActiveJob->TickGameThread();
	}
	return true;
}

void FShpToTilesModule::RegisterConsoleCommands()
{
	IConsoleManager& Manager = IConsoleManager::Get();

	ConsoleCommands.Add(Manager.RegisterConsoleCommand(
		TEXT("ShpToTiles.Open"),
		TEXT("打开 SHP → 3D Tiles 面板"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FShpToTilesModule::ConsoleOpen)));

	ConsoleCommands.Add(Manager.RegisterConsoleCommand(
		TEXT("ShpToTiles.Build"),
		TEXT("ShpToTiles.Build <shp> [-out DIR] [-height FIELD] [-floor M] [-color FIELD] [-mode unique|classify|none] [-palette NAME] [-grid N] [-base M] [-clamp] [-offset M] [-noadd] [-unlit]"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FShpToTilesModule::ConsoleBuild)));

	ConsoleCommands.Add(Manager.RegisterConsoleCommand(
		TEXT("ShpToTiles.Status"),
		TEXT("打印当前 SHP 导入任务状态"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FShpToTilesModule::ConsoleStatus)));

	ConsoleCommands.Add(Manager.RegisterConsoleCommand(
		TEXT("ShpToTiles.Panel"),
		TEXT("ShpToTiles.Panel [setheight <字段>|setcolor <字段>]：打印/设置面板当前状态（字段下拉诊断）"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FShpToTilesModule::ConsolePanel)));

	ConsoleCommands.Add(Manager.RegisterConsoleCommand(
		TEXT("ShpToTiles.SampleTest"),
		TEXT("ShpToTiles.SampleTest <lon> <lat>：诊断地形高度采样（对单个点调用 SampleHeightMostDetailed 并打印结果/耗时）"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FShpToTilesModule::ConsoleSampleTest)));
}

void FShpToTilesModule::UnregisterConsoleCommands()
{
	for (IConsoleCommand* Command : ConsoleCommands)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Command, false);
	}
	ConsoleCommands.Reset();
}

void FShpToTilesModule::ConsoleOpen(const TArray<FString>& Args)
{
	OpenPanel();
}

void FShpToTilesModule::ConsoleStatus(const TArray<FString>& Args)
{
	UE_LOG(LogTemp, Display, TEXT("[ShpToTiles] %s"), *GetStatusJson());
}

void FShpToTilesModule::ConsolePanel(const TArray<FString>& Args)
{
	TSharedPtr<SShpToTilesPanel> Panel = PanelWidget.Pin();
	if (!Panel.IsValid())
	{
		OpenPanel();
		Panel = PanelWidget.Pin();
	}
	if (!Panel.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShpToTiles] 面板没打开，先执行 ShpToTiles.Open"));
		return;
	}

	if (Args.Num() >= 2 && Args[0].Equals(TEXT("setheight"), ESearchCase::IgnoreCase))
	{
		const bool bOk = Panel->ApplyHeightFieldSelection(Args[1]);
		UE_LOG(LogTemp, Display, TEXT("[ShpToTiles] setheight %s -> %s"), *Args[1], bOk ? TEXT("ok") : TEXT("字段不存在"));
	}
	else if (Args.Num() >= 2 && Args[0].Equals(TEXT("setcolor"), ESearchCase::IgnoreCase))
	{
		const bool bOk = Panel->ApplyColorFieldSelection(Args[1]);
		UE_LOG(LogTemp, Display, TEXT("[ShpToTiles] setcolor %s -> %s"), *Args[1], bOk ? TEXT("ok") : TEXT("字段不存在"));
	}

	UE_LOG(LogTemp, Display, TEXT("[ShpToTiles] 面板状态:\n%s"), *Panel->DescribeState());
}

void FShpToTilesModule::ConsoleSampleTest(const TArray<FString>& Args)
{
	if (Args.Num() < 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShpToTiles] 用法: ShpToTiles.SampleTest <lon> <lat>"));
		return;
	}

	const double Lon = FCString::Atod(*Args[0]);
	const double Lat = FCString::Atod(*Args[1]);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShpToTiles] 没有可用的编辑器世界"));
		return;
	}

	ACesium3DTileset* Source = nullptr;
	ACesium3DTileset* First = nullptr;
	for (TActorIterator<ACesium3DTileset> It(World); It; ++It)
	{
		if (!First)
		{
			First = *It;
		}
		const FString Url = It->GetUrl();
		if (Url.Contains(TEXT("terrain"), ESearchCase::IgnoreCase) || It->GetIonAssetID() == 1)
		{
			Source = *It;
			break;
		}
	}
	if (!Source)
	{
		Source = First;
	}
	if (!Source)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShpToTiles] 关卡里没有 Cesium3DTileset 可用于采样"));
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("[ShpToTiles] 采样测试: %s  目标 (%.6f, %.6f)"),
		*Source->GetActorLabel(), Lon, Lat);

	const double StartSeconds = FPlatformTime::Seconds();
	TWeakObjectPtr<ACesium3DTileset> WeakSource = Source;

	TArray<FVector> Points;
	Points.Add(FVector(Lon, Lat, 0.0));

	Source->SampleHeightMostDetailed(
		Points,
		FCesiumSampleHeightMostDetailedCallback::CreateLambda(
			[WeakSource, StartSeconds](ACesium3DTileset* Tileset, const TArray<FCesiumSampleHeightResult>& Results, const TArray<FString>& Warnings)
			{
				const double Elapsed = FPlatformTime::Seconds() - StartSeconds;
				const int32 Num = Results.Num();
				const bool bOk = Num > 0 && Results[0].SampleSuccess;
				UE_LOG(LogTemp, Display, TEXT("[ShpToTiles] 采样回调 %.2fs  结果=%d  高度=%.3f"),
					Elapsed, Num, Num > 0 ? Results[0].LongitudeLatitudeHeight.Z : 0.0);
				if (!bOk)
				{
					UE_LOG(LogTemp, Warning, TEXT("[ShpToTiles] 采样失败/未命中（%s）"),
						Warnings.Num() > 0 ? *Warnings[0] : TEXT("无警告"));
				}
			}));
}

void FShpToTilesModule::ConsoleBuild(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ShpToTiles] 用法: ShpToTiles.Build <shp> [-out DIR] [-height FIELD] ..."));
		return;
	}

	FShpBuildOptions Options;
	Options.ShpPath = Args[0];
	Options.OutDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ShpToTiles"), FPaths::GetBaseFilename(Args[0])));

	for (int32 i = 1; i < Args.Num(); ++i)
	{
		const FString Arg = Args[i];
		auto TakeValue = [&Args, &i](FString& OutValue)
		{
			if (i + 1 < Args.Num())
			{
				OutValue = Args[++i];
				return true;
			}
			return false;
		};

		FString Value;
		if (Arg.Equals(TEXT("-out"), ESearchCase::IgnoreCase))
		{
			if (TakeValue(Value))
			{
				Options.OutDir = Value;
			}
		}
		else if (Arg.Equals(TEXT("-height"), ESearchCase::IgnoreCase))
		{
			if (TakeValue(Value))
			{
				Options.HeightField = Value;
			}
		}
		else if (Arg.Equals(TEXT("-color"), ESearchCase::IgnoreCase))
		{
			if (TakeValue(Value))
			{
				Options.ColorField = Value;
				if (Options.ColorMode == EShpColorMode::None)
				{
					Options.ColorMode = EShpColorMode::Unique;
				}
			}
		}
		else if (Arg.Equals(TEXT("-mode"), ESearchCase::IgnoreCase))
		{
			if (TakeValue(Value))
			{
				if (Value.Equals(TEXT("unique"), ESearchCase::IgnoreCase))
				{
					Options.ColorMode = EShpColorMode::Unique;
				}
				else if (Value.Equals(TEXT("classify"), ESearchCase::IgnoreCase))
				{
					Options.ColorMode = EShpColorMode::Classify;
				}
				else
				{
					Options.ColorMode = EShpColorMode::None;
				}
			}
		}
		else if (Arg.Equals(TEXT("-palette"), ESearchCase::IgnoreCase))
		{
			if (TakeValue(Value))
			{
				if (Value.Equals(TEXT("Spectral"), ESearchCase::IgnoreCase)) { Options.Palette = EShpPalette::Spectral; }
				else if (Value.Equals(TEXT("Terrain"), ESearchCase::IgnoreCase)) { Options.Palette = EShpPalette::Terrain; }
				else if (Value.Equals(TEXT("Blues"), ESearchCase::IgnoreCase)) { Options.Palette = EShpPalette::Blues; }
				else if (Value.Equals(TEXT("Warm"), ESearchCase::IgnoreCase)) { Options.Palette = EShpPalette::Warm; }
				else if (Value.Equals(TEXT("Categorical"), ESearchCase::IgnoreCase)) { Options.Palette = EShpPalette::Categorical; }
				else if (Value.Equals(TEXT("Gray"), ESearchCase::IgnoreCase)) { Options.Palette = EShpPalette::Gray; }
				else { Options.Palette = EShpPalette::Viridis; }
			}
		}
		else if (Arg.Equals(TEXT("-grid"), ESearchCase::IgnoreCase))
		{
			if (TakeValue(Value))
			{
				Options.GridSize = FCString::Atoi(*Value);
			}
		}
		else if (Arg.Equals(TEXT("-floor"), ESearchCase::IgnoreCase))
		{
			if (TakeValue(Value))
			{
				Options.FloorHeight = FCString::Atod(*Value);
			}
		}
		else if (Arg.Equals(TEXT("-base"), ESearchCase::IgnoreCase))
		{
			if (TakeValue(Value))
			{
				Options.BaseHeight = FCString::Atod(*Value);
			}
		}
		else if (Arg.Equals(TEXT("-minarea"), ESearchCase::IgnoreCase))
		{
			if (TakeValue(Value))
			{
				Options.MinFootprintArea = FCString::Atod(*Value);
			}
		}
		else if (Arg.Equals(TEXT("-classes"), ESearchCase::IgnoreCase))
		{
			if (TakeValue(Value))
			{
				Options.NumClasses = FCString::Atoi(*Value);
			}
		}
		else if (Arg.Equals(TEXT("-offset"), ESearchCase::IgnoreCase))
		{
			if (TakeValue(Value))
			{
				Options.ClampOffset = FCString::Atod(*Value);
			}
		}
		else if (Arg.Equals(TEXT("-clamp"), ESearchCase::IgnoreCase))
		{
			Options.bClampToGround = true;
		}
		else if (Arg.Equals(TEXT("-noadd"), ESearchCase::IgnoreCase))
		{
			Options.bAddToScene = false;
		}
		else if (Arg.Equals(TEXT("-unlit"), ESearchCase::IgnoreCase))
		{
			Options.bUnlit = true;
		}
		else if (Arg.Equals(TEXT("-manualcm"), ESearchCase::IgnoreCase))
		{
			if (TakeValue(Value))
			{
				Options.bManualCrs = true;
				Options.ManualCentralMeridian = FCString::Atod(*Value);
			}
		}
		else if (Arg.Equals(TEXT("-fe"), ESearchCase::IgnoreCase))
		{
			if (TakeValue(Value))
			{
				Options.ManualFalseEasting = FCString::Atod(*Value);
			}
		}
	}

	FString Error;
	if (!StartJob(Options, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("[ShpToTiles] %s"), *Error);
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("[ShpToTiles] 任务已启动: %s -> %s"), *Options.ShpPath, *Options.OutDir);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FShpToTilesModule, ShpToTiles);
