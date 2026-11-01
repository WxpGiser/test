// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShpToTilesLibrary.generated.h"

/**
 * Scripting entry point (Python / Blueprint) for the SHP -> 3D Tiles pipeline.
 * The panel uses the same job class, so behaviour is identical.
 */
UCLASS()
class UShpToTilesLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Starts a conversion from a JSON object matching FShpBuildOptions. Returns a status string. */
	UFUNCTION(BlueprintCallable, Category = "ShpToTiles")
	static FString BuildFromJson(const FString& OptionsJson);

	/** Current job phase / progress / summary as JSON. */
	UFUNCTION(BlueprintCallable, Category = "ShpToTiles")
	static FString GetStatusJson();

	/** Opens the dockable panel. */
	UFUNCTION(BlueprintCallable, Category = "ShpToTiles")
	static void OpenPanel();

	/** Default FShpBuildOptions as JSON, handy from Python. */
	UFUNCTION(BlueprintCallable, Category = "ShpToTiles")
	static FString GetDefaultOptionsJson();
};
