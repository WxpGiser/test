// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#include "SShpToTilesPanel.h"

#include "ShapeFileReader.h"
#include "ShpGeo.h"
#include "ShpSpatialReference.h"
#include "ShpBuildJob.h"
#include "ShpToTilesModule.h"

#include "CesiumGeoreference.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "JsonObjectConverter.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ShpToTilesPanel"

namespace
{
	const FString ConstHeightItem = TEXT("(常量高度)");
	const FString NoColorFieldItem = TEXT("(不指定)");

	FString SettingsFilePath()
	{
		return FPaths::ProjectSavedDir() / TEXT("ShpToTiles") / TEXT("panel_options.json");
	}

	FText ShpColorModeText(EShpColorMode Mode)
	{
		switch (Mode)
		{
		case EShpColorMode::Unique: return FText::FromString(TEXT("唯一值分类"));
		case EShpColorMode::Classify: return FText::FromString(TEXT("数值分级"));
		default: return FText::FromString(TEXT("单色白模"));
		}
	}

	FText ShpPaletteText(EShpPalette Palette)
	{
		switch (Palette)
		{
		case EShpPalette::Spectral: return FText::FromString(TEXT("Spectral"));
		case EShpPalette::Terrain: return FText::FromString(TEXT("Terrain"));
		case EShpPalette::Blues: return FText::FromString(TEXT("Blues"));
		case EShpPalette::Warm: return FText::FromString(TEXT("Warm"));
		case EShpPalette::Categorical: return FText::FromString(TEXT("Categorical"));
		case EShpPalette::Gray: return FText::FromString(TEXT("Gray"));
		default: return FText::FromString(TEXT("Viridis"));
		}
	}

	FText ShpEncodingText(EShpEncoding Encoding)
	{
		switch (Encoding)
		{
		case EShpEncoding::Gbk: return FText::FromString(TEXT("GBK / CP936"));
		case EShpEncoding::Utf8: return FText::FromString(TEXT("UTF-8"));
		default: return FText::FromString(TEXT("自动检测"));
		}
	}
}

void SShpToTilesPanel::Construct(const FArguments& InArgs)
{
	LoadSettings();

	ColorModeItems.Add(MakeShared<FString>(TEXT("单色白模")));
	ColorModeItems.Add(MakeShared<FString>(TEXT("唯一值分类")));
	ColorModeItems.Add(MakeShared<FString>(TEXT("数值分级")));

	PaletteItems.Add(MakeShared<FString>(TEXT("Viridis")));
	PaletteItems.Add(MakeShared<FString>(TEXT("Spectral")));
	PaletteItems.Add(MakeShared<FString>(TEXT("Terrain")));
	PaletteItems.Add(MakeShared<FString>(TEXT("Blues")));
	PaletteItems.Add(MakeShared<FString>(TEXT("Warm")));
	PaletteItems.Add(MakeShared<FString>(TEXT("Categorical")));
	PaletteItems.Add(MakeShared<FString>(TEXT("Gray")));

	EncodingItems.Add(MakeShared<FString>(TEXT("自动检测")));
	EncodingItems.Add(MakeShared<FString>(TEXT("GBK / CP936")));
	EncodingItems.Add(MakeShared<FString>(TEXT("UTF-8")));

	HeightFieldItems.Add(MakeShared<FString>(ConstHeightItem));
	ColorFieldItems.Add(MakeShared<FString>(NoColorFieldItem));

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(6.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f, 2.0f, 2.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "SHP → 3D Tiles 导入（Cesium for Unreal）"))
				.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
			]

			+ SVerticalBox::Slot()
			.FillHeight(0.62f)
			[
				SNew(SScrollBox)

				// ---------------------------------------------------------- input
				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("ShpFile", "SHP 文件"),
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SAssignNew(ShpPathBox, SEditableTextBox)
							.Text_Lambda([this]() { return FText::FromString(Options.ShpPath); })
							.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
							{
								Options.ShpPath = NewText.ToString();
								LoadFields();
							})
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(4.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("Browse", "浏览…"))
							.OnClicked(this, &SShpToTilesPanel::OnBrowseShp)
						])
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("OutDir", "输出目录"),
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SAssignNew(OutDirBox, SEditableTextBox)
							.Text_Lambda([this]() { return FText::FromString(Options.OutDir); })
							.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type) { Options.OutDir = NewText.ToString(); })
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(4.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("BrowseDir", "浏览…"))
							.OnClicked(this, &SShpToTilesPanel::OnBrowseOutDir)
						])
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("ReadFields", "字段"),
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(LOCTEXT("Read", "读取字段 / 坐标系"))
							.OnClicked(this, &SShpToTilesPanel::OnReadFields)
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						.Padding(8.0f, 0.0f, 0.0f, 0.0f)
						[
							SAssignNew(InfoText, STextBlock)
							.Text(LOCTEXT("NoInfo", "尚未读取"))
							.AutoWrapText(true)
						])
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CrsLabel", "坐标系"))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
					.AutoWrapText(true)
				]
				+ SScrollBox::Slot().Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					SAssignNew(CrsText, STextBlock)
					.Text(LOCTEXT("NoCrs", "-"))
					.AutoWrapText(true)
				]

				+ SScrollBox::Slot().Padding(0.0f, 6.0f, 0.0f, 2.0f)
				[
					MakeSectionHeader(LOCTEXT("GeoSection", "几何 / 拉伸"))
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("HeightField", "高度字段"),
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SAssignNew(HeightFieldCombo, SComboBox<TSharedPtr<FString>>)
								.OptionsSource(&HeightFieldItems)
								.Content()
								[
									 SNew(STextBlock)
									 .Text_Lambda([this]() { return FText::FromString(Options.HeightField.IsEmpty() ? ConstHeightItem : Options.HeightField); })
								]
								.InitiallySelectedItem(HeightFieldItems[0])
								.OnGenerateWidget_Lambda([](TSharedPtr<FString> In)
								{
									return SNew(STextBlock).Text(FText::FromString(In.IsValid() ? *In : FString()));
								})
								.MaxListHeight(360.0f)
								.OnComboBoxOpening_Lambda([this]() { EnsureFieldsLoaded(); })
								.OnSelectionChanged_Lambda([this](TSharedPtr<FString> In, ESelectInfo::Type)
								{
									if (!In.IsValid())
									{
										return;
									}
									Options.HeightField = (*In == ConstHeightItem) ? FString() : *In;
									AppendLog(FString::Printf(TEXT("高度字段 = %s"), *(*In)));
								})
							]

						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.66f, 0.72f)))
								.Text_Lambda([this]()
								{
									return FText::FromString(FString::Printf(
										TEXT("当前: %s   → 高度 = 字段值 × 单位高度"),
										Options.HeightField.IsEmpty() ? TEXT("(常量高度)") : *Options.HeightField));
								})
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(6.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SBox)
								.WidthOverride(150.0f)
								[
									SAssignNew(HeightFieldTextBox, SEditableTextBox)
									.HintText(LOCTEXT("HeightFieldHint", "手动输入字段名"))
									.ToolTipText(LOCTEXT("HeightFieldTip", "下拉框不好用时，直接输入字段名并回车，效果相同"))
									.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
									{
										const FString Name = NewText.ToString().TrimStartAndEnd();
										Options.HeightField = (Name == ConstHeightItem) ? FString() : Name;
										if (!ApplyHeightFieldSelection(Options.HeightField))
										{
											AppendLog(FString::Printf(TEXT("高度字段 = %s（字段名以输入为准）"), *Name));
										}
									})
								]
							]
						])
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("UnitHeight", "单位高度(m)"),
						SNew(SNumericEntryBox<double>)
						.Value_Lambda([this]() { return TOptional<double>(Options.FloorHeight); })
						.OnValueChanged_Lambda([this](double NewValue) { Options.FloorHeight = NewValue; })
						)
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("ConstHeight", "常量高度(m)"),
						SNew(SNumericEntryBox<double>)
						.Value_Lambda([this]() { return TOptional<double>(Options.ConstantHeight); })
						.OnValueChanged_Lambda([this](double NewValue) { Options.ConstantHeight = NewValue; })
						)
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("MinArea", "最小面积(m²)"),
						SNew(SNumericEntryBox<double>)
						.Value_Lambda([this]() { return TOptional<double>(Options.MinFootprintArea); })
						.OnValueChanged_Lambda([this](double NewValue) { Options.MinFootprintArea = NewValue; })
						)
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("Grid", "分块 N×N"),
						SNew(SNumericEntryBox<int32>)
						.Value_Lambda([this]() { return TOptional<int32>(Options.GridSize); })
						.OnValueChanged_Lambda([this](int32 NewValue) { Options.GridSize = NewValue; })
						)
				]

				+ SScrollBox::Slot().Padding(0.0f, 4.0f, 0.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return Options.bDoubleSided ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Options.bDoubleSided = (State == ECheckBoxState::Checked); })
						[
							SNew(STextBlock).Text(LOCTEXT("DoubleSided", "双面"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(12.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return Options.bCloseBottom ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Options.bCloseBottom = (State == ECheckBoxState::Checked); })
						[
							SNew(STextBlock).Text(LOCTEXT("CloseBottom", "封底"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(12.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]() { return Options.bUnlit ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Options.bUnlit = (State == ECheckBoxState::Checked); })
						[
							SNew(STextBlock).Text(LOCTEXT("Unlit", "无光照"))
						]
					]
				]

				// ------------------------------------------------------ ground clamp
				+ SScrollBox::Slot().Padding(0.0f, 8.0f, 0.0f, 2.0f)
				[
					MakeSectionHeader(LOCTEXT("ClampSection", "贴地"))
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return Options.bClampToGround ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Options.bClampToGround = (State == ECheckBoxState::Checked); })
					[
						SNew(STextBlock).Text(LOCTEXT("ClampEach", "每个要素贴合 Cesium 地形（逐要素采样高度）"))
					]
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("BaseHeight", "基准高程(m)"),
						SNew(SNumericEntryBox<double>)
						.Value_Lambda([this]() { return TOptional<double>(Options.BaseHeight); })
						.OnValueChanged_Lambda([this](double NewValue) { Options.BaseHeight = NewValue; })
						)
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("ClampOffset", "贴地偏移(m)"),
						SNew(SNumericEntryBox<double>)
						.Value_Lambda([this]() { return TOptional<double>(Options.ClampOffset); })
						.OnValueChanged_Lambda([this](double NewValue) { Options.ClampOffset = NewValue; })
						)
				]

				// ---------------------------------------------------------- colors
				+ SScrollBox::Slot().Padding(0.0f, 8.0f, 0.0f, 2.0f)
				[
					MakeSectionHeader(LOCTEXT("ColorSection", "按属性着色"))
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("ColorMode", "着色方式"),
						SAssignNew(ColorModeCombo, SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&ColorModeItems)
						.Content()
						[
							 SNew(STextBlock)
							 .Text_Lambda([this]() { return FText::FromString(ShpColorModeText(Options.ColorMode).ToString()); })
						]
						.InitiallySelectedItem(ColorModeItems[0])
						.OnGenerateWidget_Lambda([](TSharedPtr<FString> In)
						{
							return SNew(STextBlock).Text(FText::FromString(In.IsValid() ? *In : FString()));
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<FString> In, ESelectInfo::Type)
						{
							if (!In.IsValid())
							{
								return;
							}
							if (*In == TEXT("单色白模"))
							{
								Options.ColorMode = EShpColorMode::None;
							}
							else if (*In == TEXT("唯一值分类"))
							{
								Options.ColorMode = EShpColorMode::Unique;
							}
							else
							{
								Options.ColorMode = EShpColorMode::Classify;
							}
						}))
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("ColorField", "着色字段"),
						SAssignNew(ColorFieldCombo, SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&ColorFieldItems)
						.Content()
						[
							 SNew(STextBlock)
							 .Text_Lambda([this]() { return FText::FromString(Options.ColorField.IsEmpty() ? NoColorFieldItem : Options.ColorField); })
						]
						.InitiallySelectedItem(ColorFieldItems[0])
						.OnGenerateWidget_Lambda([](TSharedPtr<FString> In)
						{
							return SNew(STextBlock).Text(FText::FromString(In.IsValid() ? *In : FString()));
						})
						.MaxListHeight(360.0f)
						.OnComboBoxOpening_Lambda([this]() { EnsureFieldsLoaded(); })
						.OnSelectionChanged_Lambda([this](TSharedPtr<FString> In, ESelectInfo::Type)
						{
							if (!In.IsValid())
							{
								return;
							}
							Options.ColorField = (*In == NoColorFieldItem) ? FString() : *In;
							AppendLog(FString::Printf(TEXT("着色字段 = %s"), *(*In)));
						}))
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("Palette", "调色板"),
						SAssignNew(PaletteCombo, SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&PaletteItems)
						.Content()
						[
							 SNew(STextBlock)
							 .Text_Lambda([this]() { return FText::FromString(ShpPaletteText(Options.Palette).ToString()); })
						]
						.InitiallySelectedItem(PaletteItems[0])
						.OnGenerateWidget_Lambda([](TSharedPtr<FString> In)
						{
							return SNew(STextBlock).Text(FText::FromString(In.IsValid() ? *In : FString()));
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<FString> In, ESelectInfo::Type)
						{
							if (!In.IsValid())
							{
								return;
							}
							const FString& Name = *In;
							if (Name == TEXT("Spectral")) { Options.Palette = EShpPalette::Spectral; }
							else if (Name == TEXT("Terrain")) { Options.Palette = EShpPalette::Terrain; }
							else if (Name == TEXT("Blues")) { Options.Palette = EShpPalette::Blues; }
							else if (Name == TEXT("Warm")) { Options.Palette = EShpPalette::Warm; }
							else if (Name == TEXT("Categorical")) { Options.Palette = EShpPalette::Categorical; }
							else if (Name == TEXT("Gray")) { Options.Palette = EShpPalette::Gray; }
							else { Options.Palette = EShpPalette::Viridis; }
						}))
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("NumClasses", "分级数"),
						SNew(SNumericEntryBox<int32>)
						.Value_Lambda([this]() { return TOptional<int32>(Options.NumClasses); })
						.OnValueChanged_Lambda([this](int32 NewValue) { Options.NumClasses = NewValue; })
						)
				]

				// ------------------------------------------------------------- CRS
				+ SScrollBox::Slot().Padding(0.0f, 8.0f, 0.0f, 2.0f)
				[
					MakeSectionHeader(LOCTEXT("CrsSection", "高级：坐标系 / 编码"))
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return Options.bManualCrs ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Options.bManualCrs = (State == ECheckBoxState::Checked); })
					[
						SNew(STextBlock).Text(LOCTEXT("ManualCrs", "手动坐标参数（忽略 .prj，按横轴墨卡托/Gauss-Kruger）"))
					]
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("CM", "中央经线(°)"),
						SNew(SNumericEntryBox<double>)
						.Value_Lambda([this]() { return TOptional<double>(Options.ManualCentralMeridian); })
						.OnValueChanged_Lambda([this](double NewValue) { Options.ManualCentralMeridian = NewValue; })
						)
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("FE", "假东(m)"),
						SNew(SNumericEntryBox<double>)
						.Value_Lambda([this]() { return TOptional<double>(Options.ManualFalseEasting); })
						.OnValueChanged_Lambda([this](double NewValue) { Options.ManualFalseEasting = NewValue; })
						)
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("K0", "比例因子 k0"),
						SNew(SNumericEntryBox<double>)
						.Value_Lambda([this]() { return TOptional<double>(Options.ManualScaleFactor); })
						.OnValueChanged_Lambda([this](double NewValue) { Options.ManualScaleFactor = NewValue; })
						)
				]

				+ SScrollBox::Slot().Padding(0.0f, 2.0f)
				[
					MakeRow(
						LOCTEXT("Enc", "DBF 编码"),
						SAssignNew(EncodingCombo, SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&EncodingItems)
						.Content()
						[
							 SNew(STextBlock)
							 .Text_Lambda([this]() { return FText::FromString(ShpEncodingText(Options.Encoding).ToString()); })
						]
						.InitiallySelectedItem(EncodingItems[0])
						.OnGenerateWidget_Lambda([](TSharedPtr<FString> In)
						{
							return SNew(STextBlock).Text(FText::FromString(In.IsValid() ? *In : FString()));
						})
						.OnSelectionChanged_Lambda([this](TSharedPtr<FString> In, ESelectInfo::Type)
						{
							if (!In.IsValid())
							{
								return;
							}
							if (*In == TEXT("GBK / CP936")) { Options.Encoding = EShpEncoding::Gbk; }
							else if (*In == TEXT("UTF-8")) { Options.Encoding = EShpEncoding::Utf8; }
							else { Options.Encoding = EShpEncoding::Auto; }
						}))
				]
			]

			// ------------------------------------------------------- status / buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f, 6.0f, 2.0f, 2.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(StatusText, STextBlock)
					.Text(LOCTEXT("Idle", "就绪"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
				[
					SAssignNew(ProgressBarWidget, SProgressBar)
					.Percent_Lambda([this]() -> TOptional<float>
					{
						const TSharedPtr<FShpBuildJob> Job = FShpToTilesModule::Get().GetActiveJob();
						return Job.IsValid() ? TOptional<float>(Job->GetProgress()) : TOptional<float>(0.0f);
					})
					.FillColorAndOpacity(FSlateColor(FLinearColor(0.2f, 0.55f, 0.95f, 1.0f)))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Generate", "生成并加入场景"))
					.IsEnabled_Lambda([this]() { return !IsJobRunning(); })
					.OnClicked(this, &SShpToTilesPanel::OnGenerate)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("CancelJob", "取消"))
					.IsEnabled_Lambda([this]() { return IsJobRunning(); })
					.OnClicked(this, &SShpToTilesPanel::OnCancelJob)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("OpenOut", "打开输出目录"))
					.OnClicked(this, &SShpToTilesPanel::OnOpenOutputDir)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Focus", "定位到数据"))
					.OnClicked(this, &SShpToTilesPanel::OnFocusData)
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(0.38f)
			.Padding(2.0f, 4.0f, 2.0f, 2.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(4.0f)
				[
					SAssignNew(LogBox, SMultiLineEditableTextBox)
					.IsReadOnly(true)
					.AutoWrapText(true)
					.Margin(4.0f)
					.Text(LOCTEXT("LogEmpty", ""))
				]
			]
		]
	];

	// Restore selection widgets to the persisted options.
	if (Options.ColorMode == EShpColorMode::Unique)
	{
		ColorModeCombo->SetSelectedItem(ColorModeItems[1]);
	}
	else if (Options.ColorMode == EShpColorMode::Classify)
	{
		ColorModeCombo->SetSelectedItem(ColorModeItems[2]);
	}

	if (Options.Encoding == EShpEncoding::Gbk)
	{
		EncodingCombo->SetSelectedItem(EncodingItems[1]);
	}
	else if (Options.Encoding == EShpEncoding::Utf8)
	{
		EncodingCombo->SetSelectedItem(EncodingItems[2]);
	}


	if (!Options.ShpPath.IsEmpty())
	{
		LoadFields();
	}
}

TSharedRef<SWidget> SShpToTilesPanel::MakeRow(const FText& Label, const TSharedRef<SWidget>& Control)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(96.0f)
			[
				SNew(STextBlock)
				.Text(Label)
				.AutoWrapText(true)
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			Control
		];
}

TSharedRef<SWidget> SShpToTilesPanel::MakeSectionHeader(const FText& Text)
{
	return SNew(STextBlock)
		.Text(Text)
		.Font(FAppStyle::GetFontStyle("NormalFontBold"))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.72f, 1.0f)));
}

void SShpToTilesPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const TSharedPtr<FShpBuildJob> Job = FShpToTilesModule::Get().GetActiveJob();
	if (!Job.IsValid())
	{
		return;
	}

	const TArray<FString> Lines = Job->GetLog();
	if (Lines.Num() != LastLogCount && LogBox.IsValid())
	{
		FString Text;
		for (const FString& Line : Lines)
		{
			Text += Line;
			Text += TEXT("\n");
		}
		LogBox->SetText(FText::FromString(Text));
		LastLogCount = Lines.Num();
	}

	if (StatusText.IsValid())
	{
		FString Status = FString::Printf(TEXT("%s · %d%% · %.1fs"),
			*Job->GetPhaseText(),
			FMath::RoundToInt(Job->GetProgress() * 100.0f),
			Job->GetElapsedSeconds());

		if (Job->IsFinished())
		{
			const FShpJobSummary& Summary = Job->GetSummary();
			if (Job->GetPhase() == EShpJobPhase::Done)
			{
				Status = FString::Printf(
					TEXT("完成 · 要素 %d · 瓦片 %d · 三角形 %d · %.1f MB · %.1fs%s"),
					Summary.FeatureCount,
					Summary.TileCount,
					Summary.TriangleCount,
					Summary.ByteCount / (1024.0 * 1024.0),
					Summary.TotalSeconds,
					Summary.bUsedTerrainSampling ? TEXT(" · 已贴地") : TEXT(""));
			}
			else if (Job->GetPhase() == EShpJobPhase::Failed)
			{
				Status = TEXT("失败（详见下方日志）");
			}
			else if (Job->GetPhase() == EShpJobPhase::Cancelled)
			{
				Status = TEXT("已取消");
			}
		}

		StatusText->SetText(FText::FromString(Status));
	}
}

FReply SShpToTilesPanel::OnBrowseShp()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	const void* ParentWindow = nullptr;

	TArray<FString> Files;
	const FString StartDir = Options.ShpPath.IsEmpty() ? FPaths::ProjectContentDir() : FPaths::GetPath(Options.ShpPath);

	if (DesktopPlatform->OpenFileDialog(
		ParentWindow,
		TEXT("选择 SHP 文件"),
		StartDir,
		TEXT(""),
		TEXT("Shapefile (*.shp)|*.shp"),
		EFileDialogFlags::None,
		Files) &&
		Files.Num() > 0)
	{
		Options.ShpPath = FPaths::ConvertRelativePathToFull(Files[0]);
		Options.OutDir = MakeDefaultOutDir();
		LoadFields();
		SaveSettings();
	}

	return FReply::Handled();
}

FReply SShpToTilesPanel::OnBrowseOutDir()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	const void* ParentWindow = nullptr;

	FString Folder = Options.OutDir.IsEmpty() ? FPaths::ProjectSavedDir() : Options.OutDir;
	if (DesktopPlatform->OpenDirectoryDialog(ParentWindow, TEXT("选择输出目录"), Folder, Folder))
	{
		Options.OutDir = FPaths::ConvertRelativePathToFull(Folder);
		SaveSettings();
	}

	return FReply::Handled();
}

FReply SShpToTilesPanel::OnReadFields()
{
	LoadFields();
	return FReply::Handled();
}

FReply SShpToTilesPanel::OnGenerate()
{
	if (IsJobRunning())
	{
		return FReply::Handled();
	}

	if (Options.ShpPath.IsEmpty() || !FPaths::FileExists(Options.ShpPath))
	{
		AppendLog(TEXT("请先选择一个存在的 .shp 文件"));
		return FReply::Handled();
	}

	if (Options.OutDir.IsEmpty())
	{
		Options.OutDir = MakeDefaultOutDir();
	}

	SaveSettings();
	LastLogCount = 0;
	if (LogBox.IsValid())
	{
		LogBox->SetText(FText::GetEmpty());
	}

	FString Error;
	if (!FShpToTilesModule::Get().StartJob(Options, Error))
	{
		AppendLog(Error);
	}

	return FReply::Handled();
}

FReply SShpToTilesPanel::OnCancelJob()
{
	if (const TSharedPtr<FShpBuildJob> Job = FShpToTilesModule::Get().GetActiveJob())
	{
		Job->Cancel();
	}
	return FReply::Handled();
}

FReply SShpToTilesPanel::OnOpenOutputDir()
{
	FString Dir = Options.OutDir;
	if (Dir.IsEmpty())
	{
		Dir = MakeDefaultOutDir();
	}
	if (!Dir.IsEmpty())
	{
		FPlatformProcess::ExploreFolder(*FPaths::ConvertRelativePathToFull(Dir));
	}
	return FReply::Handled();
}

FReply SShpToTilesPanel::OnFocusData()
{
	const TSharedPtr<FShpBuildJob> Job = FShpToTilesModule::Get().GetActiveJob();
	if (!Job.IsValid())
	{
		return FReply::Handled();
	}

	const FShpJobSummary& Summary = Job->GetSummary();
	if (Summary.EastDeg <= Summary.WestDeg || Summary.NorthDeg <= Summary.SouthDeg)
	{
		return FReply::Handled();
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	ACesiumGeoreference* Georeference = ACesiumGeoreference::GetDefaultGeoreference(World);
	if (!Georeference)
	{
		return FReply::Handled();
	}

	const double Lon = 0.5 * (Summary.WestDeg + Summary.EastDeg);
	const double Lat = 0.5 * (Summary.SouthDeg + Summary.NorthDeg);
	const double Height = 0.5 * (Summary.MinBaseHeight + Summary.MaxTopHeight);

	const FVector Center = Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(Lon, Lat, Height));

	const double ExtentX = FMath::Abs(Summary.EastDeg - Summary.WestDeg) * ShpGeo::MetresPerDegreeLon(Lat);
	const double ExtentY = FMath::Abs(Summary.NorthDeg - Summary.SouthDeg) * 111132.0;
	const double Half = FMath::Max(500.0, 0.6 * FMath::Max(ExtentX, ExtentY));

	const FBox Box(Center - FVector(Half, Half, Half), Center + FVector(Half, Half, Half));

	if (GEditor)
	{
		if (FViewport* Viewport = GEditor->GetActiveViewport())
		{
			if (FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(Viewport->GetClient()))
			{
				ViewportClient->FocusViewportOnBox(Box, true);
				ViewportClient->Invalidate();
			}
		}
	}

	return FReply::Handled();
}

void SShpToTilesPanel::LoadFields()
{
	HeightFieldItems.Reset();
	ColorFieldItems.Reset();
	HeightFieldItems.Add(MakeShared<FString>(ConstHeightItem));
	ColorFieldItems.Add(MakeShared<FString>(NoColorFieldItem));

	if (Options.ShpPath.IsEmpty())
	{
		return;
	}

	FShapeFileReader Reader;
	FString Error;
	if (!Reader.Open(Options.ShpPath, Options.Encoding, Error))
	{
		AppendLog(FString::Printf(TEXT("读取失败: %s"), *Error));
		if (InfoText.IsValid())
		{
			InfoText->SetText(FText::FromString(Error));
		}
		return;
	}

	const TArray<FShpFieldInfo>& Fields = Reader.GetFields();
	for (const FShpFieldInfo& Field : Fields)
	{
		HeightFieldItems.Add(MakeShared<FString>(Field.Name));
		ColorFieldItems.Add(MakeShared<FString>(Field.Name));
	}

	if (HeightFieldCombo.IsValid())
	{
		HeightFieldCombo->RefreshOptions();
	}
	if (ColorFieldCombo.IsValid())
	{
		ColorFieldCombo->RefreshOptions();
	}

	if (InfoText.IsValid())
	{
		const FShpBounds Bounds = Reader.GetHeaderBounds().bValid ? Reader.GetHeaderBounds() : FShpBounds();
		const FString CountText = (Reader.GetDeclaredRecordCount() >= 0)
			? FString::FromInt(Reader.GetDeclaredRecordCount())
			: FString(TEXT("?"));
		InfoText->SetText(FText::FromString(FString::Printf(
			TEXT("%s · %s 条记录 · %d 个字段 · 编码 %s · 范围 [%.3f, %.3f] ~ [%.3f, %.3f]"),
			Reader.GetShapeTypeName(),
			*CountText,
			Fields.Num(),
			*Reader.GetResolvedEncodingName(),
			Bounds.MinX, Bounds.MinY, Bounds.MaxX, Bounds.MaxY)));
	}

	if (CrsText.IsValid())
	{
		const FString& Wkt = Reader.GetPrjWkt();
		if (Wkt.IsEmpty())
		{
			CrsText->SetText(FText::FromString(TEXT("没有 .prj —— 若为投影坐标请在下方勾选「手动坐标参数」")));
		}
		else
		{
			FShpSpatialReference Ref;
			FString CrsError;
			if (FShpSpatialReference::FromWkt(Wkt, Ref, CrsError))
			{
				CrsText->SetText(FText::FromString(Ref.Describe()));
			}
			else
			{
				CrsText->SetText(FText::FromString(CrsError));
			}
		}
	}

	// Pick a sensible default height field the first time round.
	if (Options.HeightField.IsEmpty())
	{
		for (const FShpFieldInfo& Field : Fields)
		{
			if (!Field.IsNumeric())
			{
				continue;
			}
			if (Field.Name.Contains(TEXT("floor"), ESearchCase::IgnoreCase) ||
				Field.Name.Contains(TEXT("height"), ESearchCase::IgnoreCase) ||
				Field.Name.Contains(TEXT("层"), ESearchCase::IgnoreCase) ||
				Field.Name.Contains(TEXT("高"), ESearchCase::IgnoreCase))
			{
				Options.HeightField = Field.Name;
				break;
			}
		}
	}

	// Reflect the persisted selection in the combos.
	if (HeightFieldTextBox.IsValid())
	{
		HeightFieldTextBox->SetText(FText::FromString(Options.HeightField));
	}
	if (HeightFieldCombo.IsValid())
	{
		for (const TSharedPtr<FString>& Item : HeightFieldItems)
		{
			const bool bConst = Options.HeightField.IsEmpty();
			if ((bConst && *Item == ConstHeightItem) || (!bConst && *Item == Options.HeightField))
			{
				HeightFieldCombo->SetSelectedItem(Item);
				break;
			}
		}
	}
	if (ColorFieldCombo.IsValid())
	{
		for (const TSharedPtr<FString>& Item : ColorFieldItems)
		{
			const bool bNone = Options.ColorField.IsEmpty();
			if ((bNone && *Item == NoColorFieldItem) || (!bNone && *Item == Options.ColorField))
			{
				ColorFieldCombo->SetSelectedItem(Item);
				break;
			}
		}
	}

	FString FieldList;
	for (const FShpFieldInfo& Field : Fields)
	{
		if (!FieldList.IsEmpty())
		{
			FieldList += TEXT(", ");
		}
		FieldList += Field.Name;
	}
	AppendLog(FString::Printf(TEXT("已读取: %s（%d 个字段）: %s"), *FPaths::GetCleanFilename(Options.ShpPath), Fields.Num(), *FieldList));
	AppendLog(FString::Printf(TEXT("当前高度字段 = %s"), Options.HeightField.IsEmpty() ? TEXT("(常量高度)") : *Options.HeightField));
}

void SShpToTilesPanel::EnsureFieldsLoaded()
{
	// Opening a field combo must never show a stale/empty list: re-read whenever we
	// only have the placeholder entry (e.g. the path was typed instead of browsed).
	if (HeightFieldItems.Num() <= 1 && !Options.ShpPath.IsEmpty())
	{
		LoadFields();
	}
}

FString SShpToTilesPanel::DescribeState()
{
	auto JoinItems = [](const TArray<TSharedPtr<FString>>& Items)
	{
		FString Out;
		for (const TSharedPtr<FString>& Item : Items)
		{
			if (!Out.IsEmpty())
			{
				Out += TEXT(", ");
			}
			Out += Item.IsValid() ? *Item : FString(TEXT("<null>"));
		}
		return Out;
	};

	auto Selected = [](TSharedPtr<SComboBox<TSharedPtr<FString>>> Combo)
	{
		if (!Combo.IsValid())
		{
			return FString(TEXT("<no widget>"));
		}
		const TSharedPtr<FString> Item = Combo->GetSelectedItem();
		return Item.IsValid() ? *Item : FString(TEXT("<none>"));
	};

	return FString::Printf(
		TEXT("shp=%s\nout=%s\nheightField=[%s]\ncolorField=[%s]\ngrid=%d minArea=%.1f floor=%.2f const=%.2f base=%.2f clamp=%d\n"
			 "combo.height selected=[%s] items(%d): %s\ncombo.color selected=[%s] items(%d): %s"),
		*Options.ShpPath,
		*Options.OutDir,
		*Options.HeightField,
		*Options.ColorField,
		Options.GridSize,
		Options.MinFootprintArea,
		Options.FloorHeight,
		Options.ConstantHeight,
		Options.BaseHeight,
		Options.bClampToGround ? 1 : 0,
		*Selected(HeightFieldCombo),
		HeightFieldItems.Num(),
		*JoinItems(HeightFieldItems),
		*Selected(ColorFieldCombo),
		ColorFieldItems.Num(),
		*JoinItems(ColorFieldItems));
}

bool SShpToTilesPanel::ApplyHeightFieldSelection(const FString& FieldName)
{
	if (!HeightFieldCombo.IsValid())
	{
		return false;
	}

	const FString Target = FieldName.IsEmpty() ? ConstHeightItem : FieldName;
	for (const TSharedPtr<FString>& Item : HeightFieldItems)
	{
		if (Item.IsValid() && *Item == Target)
		{
			HeightFieldCombo->SetSelectedItem(Item);
			Options.HeightField = (Target == ConstHeightItem) ? FString() : Target;
			return true;
		}
	}
	return false;
}

bool SShpToTilesPanel::ApplyColorFieldSelection(const FString& FieldName)
{
	if (!ColorFieldCombo.IsValid())
	{
		return false;
	}

	const FString Target = FieldName.IsEmpty() ? NoColorFieldItem : FieldName;
	for (const TSharedPtr<FString>& Item : ColorFieldItems)
	{
		if (Item.IsValid() && *Item == Target)
		{
			ColorFieldCombo->SetSelectedItem(Item);
			Options.ColorField = (Target == NoColorFieldItem) ? FString() : Target;
			return true;
		}
	}
	return false;
}

void SShpToTilesPanel::AppendLog(const FString& Line)
{
	if (!LogBox.IsValid())
	{
		return;
	}

	FString Text = LogBox->GetText().ToString();
	if (!Text.IsEmpty())
	{
		Text += TEXT("\n");
	}
	Text += Line;
	LogBox->SetText(FText::FromString(Text));
	++LastLogCount;
}

FString SShpToTilesPanel::MakeDefaultOutDir() const
{
	FString Base = FPaths::ProjectSavedDir() / TEXT("ShpToTiles");
	const FString Dataset = Options.ShpPath.IsEmpty() ? FString(TEXT("dataset")) : FPaths::GetBaseFilename(Options.ShpPath);
	return FPaths::ConvertRelativePathToFull(Base / Dataset);
}

bool SShpToTilesPanel::IsJobRunning() const
{
	const TSharedPtr<FShpBuildJob> Job = FShpToTilesModule::Get().GetActiveJob();
	return Job.IsValid() && Job->IsRunning();
}

void SShpToTilesPanel::LoadSettings()
{
	const FString Path = SettingsFilePath();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		return;
	}

	FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Options, 0, 0);
}

void SShpToTilesPanel::SaveSettings() const
{
	const FString Path = SettingsFilePath();
	const FString Dir = FPaths::GetPath(Path);
	IFileManager::Get().MakeDirectory(*Dir, true);

	FString Json;
	if (FJsonObjectConverter::UStructToJsonObjectString(Options, Json, 0, 0))
	{
		FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

#undef LOCTEXT_NAMESPACE
