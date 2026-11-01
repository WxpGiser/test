// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleInterface.h"
#include "ShpTypes.h"

class FShpBuildJob;
class SShpToTilesPanel;
class SDockTab;
class FSpawnTabArgs;
struct IConsoleCommand;

/** Editor module: dockable panel, Tools menu entry, console commands, job ticking. */
class FShpToTilesModule : public IModuleInterface
{
public:
	static FShpToTilesModule& Get();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void OpenPanel();
	bool StartJob(const FShpBuildOptions& Options, FString& OutError);

	TSharedPtr<FShpBuildJob> GetActiveJob() const { return ActiveJob; }
	TArray<FString> GetActiveLog() const;
	FString GetStatusJson() const;

	static FName GetTabName() { return FName(TEXT("ShpToTilesPanel")); }

private:
	bool Tick(float DeltaTime);
	void RegisterMenus();
	TSharedRef<SDockTab> OnSpawnTab(const FSpawnTabArgs& Args);

	void RegisterConsoleCommands();
	void UnregisterConsoleCommands();
	void ConsoleOpen(const TArray<FString>& Args);
	void ConsoleBuild(const TArray<FString>& Args);
	void ConsoleStatus(const TArray<FString>& Args);
	void ConsoleSampleTest(const TArray<FString>& Args);
	void ConsolePanel(const TArray<FString>& Args);

	TSharedPtr<FShpBuildJob> ActiveJob;
	TWeakPtr<SShpToTilesPanel> PanelWidget;
	FTSTicker::FDelegateHandle TickerHandle;
	TArray<IConsoleCommand*> ConsoleCommands;
};
