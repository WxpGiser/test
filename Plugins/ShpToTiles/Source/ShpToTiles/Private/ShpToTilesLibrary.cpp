// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#include "ShpToTilesLibrary.h"

#include "ShpBuildJob.h"
#include "ShpToTilesModule.h"

#include "JsonObjectConverter.h"

FString UShpToTilesLibrary::BuildFromJson(const FString& OptionsJson)
{
	FShpBuildOptions Options;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(OptionsJson, &Options, 0, 0))
	{
		return TEXT("error: 无法解析 options JSON");
	}

	FString Error;
	if (!FShpToTilesModule::Get().StartJob(Options, Error))
	{
		return FString::Printf(TEXT("error: %s"), *Error);
	}

	return TEXT("ok");
}

FString UShpToTilesLibrary::GetStatusJson()
{
	return FShpToTilesModule::Get().GetStatusJson();
}

void UShpToTilesLibrary::OpenPanel()
{
	FShpToTilesModule::Get().OpenPanel();
}

FString UShpToTilesLibrary::GetDefaultOptionsJson()
{
	const FShpBuildOptions Options;
	FString Json;
	FJsonObjectConverter::UStructToJsonObjectString(Options, Json, 0, 0);
	return Json;
}
