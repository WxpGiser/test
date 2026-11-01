// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#include "ShpSpatialReference.h"

namespace
{
	/** WKT may use [] (OGC) or () (ESRI); normalise to []. */
	FString NormalizeWkt(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("("), TEXT("["));
		Out.ReplaceInline(TEXT(")"), TEXT("]"));
		return Out;
	}

	/** Finds the bracketed argument list that follows Keyword, returns the inner text. */
	bool FindKeywordArgs(const FString& Wkt, const TCHAR* Keyword, int32 SearchFrom, FString& OutInner, int32& OutNext)
	{
		int32 Pos = Wkt.Find(Keyword, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
		if (Pos == INDEX_NONE)
		{
			return false;
		}

		int32 Open = Wkt.Find(TEXT("["), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
		if (Open == INDEX_NONE)
		{
			return false;
		}

		int32 Depth = 0;
		for (int32 i = Open; i < Wkt.Len(); ++i)
		{
			if (Wkt[i] == TEXT('['))
			{
				++Depth;
			}
			else if (Wkt[i] == TEXT(']'))
			{
				--Depth;
				if (Depth == 0)
				{
					OutInner = Wkt.Mid(Open + 1, i - Open - 1);
					OutNext = i + 1;
					return true;
				}
			}
		}
		return false;
	}

	/** Splits a WKT argument list on top-level commas. */
	void SplitArgs(const FString& Inner, TArray<FString>& OutArgs)
	{
		OutArgs.Reset();
		int32 Depth = 0;
		int32 Start = 0;
		for (int32 i = 0; i < Inner.Len(); ++i)
		{
			const TCHAR C = Inner[i];
			if (C == TEXT('['))
			{
				++Depth;
			}
			else if (C == TEXT(']'))
			{
				--Depth;
			}
			else if (C == TEXT(',') && Depth == 0)
			{
				FString Arg = Inner.Mid(Start, i - Start);
				Arg.TrimStartAndEndInline();
				OutArgs.Add(Arg);
				Start = i + 1;
			}
		}
		FString Last = Inner.Mid(Start);
		Last.TrimStartAndEndInline();
		if (!Last.IsEmpty())
		{
			OutArgs.Add(Last);
		}
	}

	FString Unquote(const FString& In)
	{
		FString Out = In;
		Out.TrimStartAndEndInline();
		Out.RemoveFromStart(TEXT("\""));
		Out.RemoveFromEnd(TEXT("\""));
		return Out;
	}
}

FShpSpatialReference FShpSpatialReference::MakeGeographicWgs84()
{
	FShpSpatialReference Out;
	Out.Method = EShpProjectionMethod::Geographic;
	Out.SemiMajorAxis = 6378137.0;
	Out.InverseFlattening = 298.257223563;
	Out.AngularRadPerUnit = 0.017453292519943295;
	Out.Name = TEXT("WGS84 geographic (assumed)");
	Out.Source = TEXT("assumed");
	return Out;
}

FShpSpatialReference FShpSpatialReference::MakeManualGaussKruger(
	double InCentralMeridianDeg,
	double InFalseEasting,
	double InScaleFactor,
	double InLatitudeOfOriginDeg)
{
	FShpSpatialReference Out;
	Out.Method = EShpProjectionMethod::TransverseMercator;
	Out.SemiMajorAxis = 6378137.0;
	Out.InverseFlattening = 298.257222101; // CGCS2000
	Out.CentralMeridianDeg = InCentralMeridianDeg;
	Out.FalseEasting = InFalseEasting;
	Out.FalseNorthing = 0.0;
	Out.ScaleFactor = InScaleFactor;
	Out.LatitudeOfOriginDeg = InLatitudeOfOriginDeg;
	Out.Name = FString::Printf(TEXT("Manual Gauss-Kruger CM=%.4f"), InCentralMeridianDeg);
	Out.Source = TEXT("manual");
	return Out;
}

bool FShpSpatialReference::FromWkt(const FString& WktIn, FShpSpatialReference& Out, FString& OutError)
{
	OutError.Reset();

	const FString Wkt = NormalizeWkt(WktIn);
	if (Wkt.IsEmpty())
	{
		OutError = TEXT("空的 .prj 内容");
		return false;
	}

	const bool bProjected = Wkt.Contains(TEXT("PROJCS"), ESearchCase::IgnoreCase);

	// --- ellipsoid -----------------------------------------------------------
	{
		FString Inner;
		int32 Next = 0;
		if (FindKeywordArgs(Wkt, TEXT("SPHEROID"), 0, Inner, Next))
		{
			TArray<FString> Args;
			SplitArgs(Inner, Args);
			if (Args.Num() >= 3)
			{
				Out.SemiMajorAxis = FCString::Atod(*Unquote(Args[1]));
				Out.InverseFlattening = FCString::Atod(*Unquote(Args[2]));
			}
		}
	}

	if (Out.SemiMajorAxis <= 0.0)
	{
		Out.SemiMajorAxis = 6378137.0;
	}
	if (Out.InverseFlattening <= 0.0)
	{
		Out.InverseFlattening = 298.257223563;
	}

	if (!bProjected)
	{
		// --- geographic: only need the angular unit ---------------------------
		Out.Method = EShpProjectionMethod::Geographic;
		FString Inner;
		int32 Next = 0;
		if (FindKeywordArgs(Wkt, TEXT("UNIT"), 0, Inner, Next))
		{
			TArray<FString> Args;
			SplitArgs(Inner, Args);
			if (Args.Num() >= 2)
			{
				const double RadPerUnit = FCString::Atod(*Unquote(Args[1]));
				if (RadPerUnit > 0.0)
				{
					Out.AngularRadPerUnit = RadPerUnit;
				}
			}
		}

		FString NameInner;
		int32 NameNext = 0;
		if (FindKeywordArgs(Wkt, TEXT("GEOGCS"), 0, NameInner, NameNext))
		{
			TArray<FString> Args;
			SplitArgs(NameInner, Args);
			if (Args.Num() > 0)
			{
				Out.Name = Unquote(Args[0]);
			}
		}
		if (Out.Name.IsEmpty())
		{
			Out.Name = TEXT("Geographic");
		}
		Out.Source = TEXT("prj");
		return true;
	}

	// --- projected -----------------------------------------------------------
	Out.Method = EShpProjectionMethod::TransverseMercator;

	FString ProjInner;
	int32 ProjNext = 0;
	if (FindKeywordArgs(Wkt, TEXT("PROJECTION"), 0, ProjInner, ProjNext))
	{
		TArray<FString> Args;
		SplitArgs(ProjInner, Args);
		const FString ProjName = Args.Num() > 0 ? Unquote(Args[0]) : FString();
		Out.Name = ProjName;

		const bool bIsTM =
			ProjName.Contains(TEXT("Transverse_Mercator"), ESearchCase::IgnoreCase) ||
			ProjName.Contains(TEXT("TransverseMercator"), ESearchCase::IgnoreCase) ||
			ProjName.Contains(TEXT("Gauss_Kruger"), ESearchCase::IgnoreCase) ||
			ProjName.Contains(TEXT("GaussKruger"), ESearchCase::IgnoreCase) ||
			ProjName.Contains(TEXT("Gauss Kruger"), ESearchCase::IgnoreCase);

		if (!bIsTM)
		{
			Out.Method = EShpProjectionMethod::Unsupported;
			OutError = FString::Printf(TEXT("暂不支持的投影: %s（目前支持 经纬度 / 横轴墨卡托 Gauss-Kruger）"), *ProjName);
			return false;
		}
	}
	else
	{
		OutError = TEXT(".prj 中没有 PROJECTION 定义");
		Out.Method = EShpProjectionMethod::Unsupported;
		return false;
	}

	// --- parameters ----------------------------------------------------------
	int32 SearchFrom = 0;
	for (;;)
	{
		FString Inner;
		int32 Next = 0;
		if (!FindKeywordArgs(Wkt, TEXT("PARAMETER"), SearchFrom, Inner, Next))
		{
			break;
		}
		SearchFrom = Next;

		TArray<FString> Args;
		SplitArgs(Inner, Args);
		if (Args.Num() < 2)
		{
			continue;
		}
		const FString Key = Unquote(Args[0]).ToLower();
		const double Value = FCString::Atod(*Unquote(Args[1]));

		if (Key == TEXT("central_meridian") || Key == TEXT("longitude_of_center") || Key == TEXT("longitude_of_origin"))
		{
			Out.CentralMeridianDeg = Value;
		}
		else if (Key == TEXT("latitude_of_origin") || Key == TEXT("latitude_of_center"))
		{
			Out.LatitudeOfOriginDeg = Value;
		}
		else if (Key == TEXT("scale_factor"))
		{
			Out.ScaleFactor = (Value != 0.0) ? Value : 1.0;
		}
		else if (Key == TEXT("false_easting"))
		{
			Out.FalseEasting = Value;
		}
		else if (Key == TEXT("false_northing"))
		{
			Out.FalseNorthing = Value;
		}
	}

	// --- linear unit (last UNIT[] in the WKT) --------------------------------
	{
		int32 LastUnit = INDEX_NONE;
		int32 Cursor = 0;
		for (;;)
		{
			const int32 Pos = Wkt.Find(TEXT("UNIT"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Cursor);
			if (Pos == INDEX_NONE)
			{
				break;
			}
			LastUnit = Pos;
			Cursor = Pos + 4;
		}

		if (LastUnit != INDEX_NONE)
		{
			FString Inner;
			int32 Next = 0;
			if (FindKeywordArgs(Wkt, TEXT("UNIT"), LastUnit, Inner, Next))
			{
				TArray<FString> Args;
				SplitArgs(Inner, Args);
				if (Args.Num() >= 2)
				{
					const double MetresPerUnit = FCString::Atod(*Unquote(Args[1]));
					if (MetresPerUnit > 0.0 && MetresPerUnit < 1000.0)
					{
						Out.UnitToMetres = MetresPerUnit;
					}
				}
			}
		}
	}

	if (Out.ScaleFactor == 0.0)
	{
		Out.ScaleFactor = 1.0;
	}
	Out.Source = TEXT("prj");
	return true;
}

double FShpSpatialReference::MeridianArc(double LatRad) const
{
	const double A = SemiMajorAxis;
	const double E2 = GetEccentricitySquared();
	const double E4 = E2 * E2;
	const double E6 = E4 * E2;

	return A * ((1.0 - E2 / 4.0 - 3.0 * E4 / 64.0 - 5.0 * E6 / 256.0) * LatRad
		- (3.0 * E2 / 8.0 + 3.0 * E4 / 32.0 + 45.0 * E6 / 1024.0) * FMath::Sin(2.0 * LatRad)
		+ (15.0 * E4 / 256.0 + 45.0 * E6 / 1024.0) * FMath::Sin(4.0 * LatRad)
		- (35.0 * E6 / 3072.0) * FMath::Sin(6.0 * LatRad));
}

FVector2D FShpSpatialReference::ToLonLat(const FVector2D& XYIn) const
{
	const double RadToDeg = 180.0 / PI;
	const FVector2D XY(XYIn.X - CoordinateShiftX, XYIn.Y);

	if (Method == EShpProjectionMethod::Geographic)
	{
		return FVector2D(XY.X * AngularRadPerUnit * RadToDeg, XY.Y * AngularRadPerUnit * RadToDeg);
	}

	if (Method != EShpProjectionMethod::TransverseMercator)
	{
		return XY;
	}

	const double A = SemiMajorAxis;
	const double E2 = GetEccentricitySquared();
	const double EP2 = E2 / (1.0 - E2);
	const double OneMinusE2 = 1.0 - E2;

	const double X = XY.X * UnitToMetres - FalseEasting;
	const double Y = XY.Y * UnitToMetres - FalseNorthing;

	const double M0 = MeridianArc(LatitudeOfOriginDeg * PI / 180.0);
	const double M = M0 + Y / ScaleFactor;

	const double Mu = M / (A * (1.0 - E2 / 4.0 - 3.0 * E2 * E2 / 64.0 - 5.0 * E2 * E2 * E2 / 256.0));
	const double SqrtOneMinus = FMath::Sqrt(OneMinusE2);
	const double E1 = (1.0 - SqrtOneMinus) / (1.0 + SqrtOneMinus);
	const double E1_2 = E1 * E1;
	const double E1_3 = E1_2 * E1;
	const double E1_4 = E1_3 * E1;

	const double Phi1 = Mu
		+ (3.0 * E1 / 2.0 - 27.0 * E1_3 / 32.0) * FMath::Sin(2.0 * Mu)
		+ (21.0 * E1_2 / 16.0 - 55.0 * E1_4 / 32.0) * FMath::Sin(4.0 * Mu)
		+ (151.0 * E1_3 / 96.0) * FMath::Sin(6.0 * Mu)
		+ (1097.0 * E1_4 / 512.0) * FMath::Sin(8.0 * Mu);

	const double SinPhi1 = FMath::Sin(Phi1);
	const double CosPhi1 = FMath::Cos(Phi1);
	const double TanPhi1 = FMath::Tan(Phi1);

	const double C1 = EP2 * CosPhi1 * CosPhi1;
	const double T1 = TanPhi1 * TanPhi1;
	const double N1 = A / FMath::Sqrt(1.0 - E2 * SinPhi1 * SinPhi1);
	const double R1 = A * OneMinusE2 / FMath::Pow(1.0 - E2 * SinPhi1 * SinPhi1, 1.5);

	const double D = X / (N1 * ScaleFactor);
	const double D2 = D * D;
	const double D3 = D2 * D;
	const double D4 = D3 * D;
	const double D5 = D4 * D;
	const double D6 = D5 * D;

	const double Lat = Phi1 - (N1 * TanPhi1 / R1) *
		(D2 / 2.0
			- (5.0 + 3.0 * T1 + 10.0 * C1 - 4.0 * C1 * C1 - 9.0 * EP2) * D4 / 24.0
			+ (61.0 + 90.0 * T1 + 298.0 * C1 + 45.0 * T1 * T1 - 252.0 * EP2 - 3.0 * C1 * C1) * D6 / 720.0);

	const double Lon = (CentralMeridianDeg * PI / 180.0) +
		(D - (1.0 + 2.0 * T1 + C1) * D3 / 6.0
			+ (5.0 - 2.0 * C1 + 28.0 * T1 - 3.0 * C1 * C1 + 8.0 * EP2 + 24.0 * T1 * T1) * D5 / 120.0) / CosPhi1;

	return FVector2D(Lon * RadToDeg, Lat * RadToDeg);
}

FString FShpSpatialReference::Describe() const
{
	FString EllipsoidDesc = FString::Printf(TEXT("a=%.3f, 1/f=%.9f"), SemiMajorAxis, InverseFlattening);

	if (Method == EShpProjectionMethod::Geographic)
	{
		return FString::Printf(TEXT("经纬度 (GEOGCS \"%s\", %s)"), *Name, *EllipsoidDesc);
	}
	if (Method == EShpProjectionMethod::Unsupported)
	{
		return TEXT("不支持的投影");
	}
	return FString::Printf(
		TEXT("横轴墨卡托 TM \"%s\" 中央经线=%.5f°, k0=%.9f, 假东=%.3f, 假北=%.3f, %s"),
		*Name,
		CentralMeridianDeg,
		ScaleFactor,
		FalseEasting,
		FalseNorthing,
		*EllipsoidDesc);
}
