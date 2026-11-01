// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#pragma once

#include "CoreMinimal.h"
#include "ShpTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

class FShpBuildJob;
class SEditableTextBox;
class SMultiLineEditableTextBox;
class SProgressBar;
class STextBlock;

/** Dockable panel: SHP -> 3D Tiles. */
class SShpToTilesPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShpToTilesPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Human readable state of the panel's current options, for console diagnostics. */
	FString DescribeState();

	/** Same effect as picking an entry in the height combo (empty = 常量高度). */
	bool ApplyHeightFieldSelection(const FString& FieldName);

	/** Same effect as picking an entry in the colour-field combo (empty = 不指定). */
	bool ApplyColorFieldSelection(const FString& FieldName);

private:
	// --- actions
	FReply OnBrowseShp();
	FReply OnBrowseOutDir();
	FReply OnReadFields();
	FReply OnGenerate();
	FReply OnCancelJob();
	FReply OnOpenOutputDir();
	FReply OnFocusData();

	void LoadFields();
	void EnsureFieldsLoaded();
	void AppendLog(const FString& Line);
	void LoadSettings();
	void SaveSettings() const;
	FString MakeDefaultOutDir() const;
	bool IsJobRunning() const;

	TSharedRef<SWidget> MakeRow(const FText& Label, const TSharedRef<SWidget>& Control);
	TSharedRef<SWidget> MakeSectionHeader(const FText& Text);

	FShpBuildOptions Options;

	// widgets
	TSharedPtr<SEditableTextBox> ShpPathBox;
	TSharedPtr<SEditableTextBox> OutDirBox;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> HeightFieldCombo;
	TSharedPtr<SEditableTextBox> HeightFieldTextBox;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ColorFieldCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ColorModeCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> PaletteCombo;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> EncodingCombo;
	TSharedPtr<SMultiLineEditableTextBox> LogBox;
	TSharedPtr<SProgressBar> ProgressBarWidget;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> InfoText;
	TSharedPtr<STextBlock> CrsText;

	TArray<TSharedPtr<FString>> HeightFieldItems;
	TArray<TSharedPtr<FString>> ColorFieldItems;
	TArray<TSharedPtr<FString>> ColorModeItems;
	TArray<TSharedPtr<FString>> PaletteItems;
	TArray<TSharedPtr<FString>> EncodingItems;

	int32 LastLogCount = 0;
	double LastRefreshTime = 0.0;
};
