// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

using UnrealBuildTool;

public class ShpToTiles : ModuleRules
{
	public ShpToTiles(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"CesiumRuntime"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"UnrealEd",
			"ToolMenus",
			"EditorFramework",
			"DesktopPlatform",
			"JsonUtilities",
			"LevelEditor",
			"ApplicationCore",
			"RenderCore",
			"Projects"
		});
	}
}
