// Copyright (c) 2026. SHP -> 3D Tiles importer plugin.

#include "ShpMeshBuilder.h"

#include "Algo/Reverse.h"

namespace
{
	constexpr double AreaEpsilon = 1e-9;
	constexpr double WeldTolerance = 1e-6;

	bool PointInTriangle(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C, double Eps)
	{
		const double D1 = (P.X - B.X) * (A.Y - B.Y) - (A.X - B.X) * (P.Y - B.Y);
		const double D2 = (P.X - C.X) * (B.Y - C.Y) - (B.X - C.X) * (P.Y - C.Y);
		const double D3 = (P.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (P.Y - A.Y);

		const bool bNeg = (D1 < -Eps) || (D2 < -Eps) || (D3 < -Eps);
		const bool bPos = (D1 > Eps) || (D2 > Eps) || (D3 > Eps);
		return !(bNeg && bPos);
	}

	void EarClip(const TArray<FVector2D>& Pts, TArray<FIntVector>& OutTris)
	{
		const int32 N = Pts.Num();
		if (N < 3)
		{
			return;
		}
		if (N == 3)
		{
			OutTris.Add(FIntVector(0, 1, 2));
			return;
		}

		TArray<int32> Remaining;
		Remaining.Reserve(N);
		for (int32 i = 0; i < N; ++i)
		{
			Remaining.Add(i);
		}

		const int32 MaxIterations = N * N * 2 + 16;
		int32 Cursor = 0;
		int32 Iterations = 0;

		while (Remaining.Num() > 3 && Iterations++ < MaxIterations)
		{
			const int32 Count = Remaining.Num();
			if (Cursor >= Count)
			{
				Cursor = 0;
			}

			const int32 I0 = Remaining[(Cursor + Count - 1) % Count];
			const int32 I1 = Remaining[Cursor];
			const int32 I2 = Remaining[(Cursor + 1) % Count];

			const FVector2D& A = Pts[I0];
			const FVector2D& B = Pts[I1];
			const FVector2D& C = Pts[I2];

			const double Cross = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
			if (Cross <= AreaEpsilon)
			{
				++Cursor;
				continue;
			}

			bool bBlocked = false;
			for (int32 k = 0; k < Count && !bBlocked; ++k)
			{
				const int32 J = Remaining[k];
				if (J == I0 || J == I1 || J == I2)
				{
					continue;
				}
				const FVector2D& P = Pts[J];
				if (P.Equals(A, WeldTolerance) || P.Equals(B, WeldTolerance) || P.Equals(C, WeldTolerance))
				{
					continue; // bridge duplicates must not block an ear
				}
				if (PointInTriangle(P, A, B, C, 1e-12))
				{
					bBlocked = true;
				}
			}

			if (bBlocked)
			{
				++Cursor;
				continue;
			}

			OutTris.Add(FIntVector(I0, I1, I2));
			Remaining.RemoveAt(Cursor);
		}

		if (Remaining.Num() == 3)
		{
			OutTris.Add(FIntVector(Remaining[0], Remaining[1], Remaining[2]));
		}
		else if (Remaining.Num() > 3)
		{
			// Degenerate polygon: fall back to a fan so we still produce a surface.
			for (int32 k = 1; k + 1 < Remaining.Num(); ++k)
			{
				OutTris.Add(FIntVector(Remaining[0], Remaining[k], Remaining[k + 1]));
			}
		}
	}

	double RingMaxX(const TArray<FVector2D>& Ring)
	{
		double MaxX = TNumericLimits<double>::Lowest();
		for (const FVector2D& P : Ring)
		{
			MaxX = FMath::Max(MaxX, P.X);
		}
		return MaxX;
	}

	/** Connects every hole to the outer polygon with a degenerate bridge. */
	void EliminateHoles(
		const TArray<FVector2D>& Outer,
		const TArray<TArray<FVector2D>>& Holes,
		TArray<FVector2D>& OutPoly)
	{
		OutPoly = Outer;

		TArray<int32> Order;
		Order.Reserve(Holes.Num());
		for (int32 i = 0; i < Holes.Num(); ++i)
		{
			Order.Add(i);
		}
		Order.Sort([&Holes](int32 A, int32 B)
		{
			return RingMaxX(Holes[A]) > RingMaxX(Holes[B]);
		});

		for (int32 HoleIndex : Order)
		{
			const TArray<FVector2D>& Hole = Holes[HoleIndex];
			if (Hole.Num() < 3)
			{
				continue;
			}

			int32 Hi = 0;
			for (int32 i = 1; i < Hole.Num(); ++i)
			{
				if (Hole[i].X > Hole[Hi].X || (Hole[i].X == Hole[Hi].X && Hole[i].Y > Hole[Hi].Y))
				{
					Hi = i;
				}
			}
			const FVector2D H = Hole[Hi];

			const int32 N = OutPoly.Num();
			if (N < 3)
			{
				return;
			}

			int32 BestEdge = INDEX_NONE;
			double BestDistance = TNumericLimits<double>::Max();
			for (int32 i = 0; i < N; ++i)
			{
				const FVector2D& P = OutPoly[i];
				const FVector2D& Q = OutPoly[(i + 1) % N];
				if ((P.Y > H.Y) == (Q.Y > H.Y))
				{
					continue;
				}
				const double T = (H.Y - P.Y) / (Q.Y - P.Y);
				const double X = P.X + T * (Q.X - P.X);
				const double Distance = X - H.X;
				if (Distance < -1e-9)
				{
					continue;
				}
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					BestEdge = i;
				}
			}

			if (BestEdge == INDEX_NONE)
			{
				continue;
			}

			const int32 I0 = BestEdge;
			const int32 I1 = (BestEdge + 1) % N;
			const int32 BridgeIndex = (OutPoly[I0].X > OutPoly[I1].X) ? I0 : I1;

			TArray<FVector2D> NewPoly;
			NewPoly.Reserve(N + Hole.Num() + 2);
			for (int32 i = 0; i <= BridgeIndex; ++i)
			{
				NewPoly.Add(OutPoly[i]);
			}
			for (int32 k = 0; k < Hole.Num(); ++k)
			{
				NewPoly.Add(Hole[(Hi + k) % Hole.Num()]);
			}
			NewPoly.Add(Hole[Hi]);
			NewPoly.Add(OutPoly[BridgeIndex]);
			for (int32 i = BridgeIndex + 1; i < N; ++i)
			{
				NewPoly.Add(OutPoly[i]);
			}

			OutPoly = MoveTemp(NewPoly);
		}
	}
}

namespace ShpMesh
{
	double SignedArea(const TArray<FVector2D>& Ring)
	{
		const int32 N = Ring.Num();
		if (N < 3)
		{
			return 0.0;
		}

		double Sum = 0.0;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& A = Ring[i];
			const FVector2D& B = Ring[(i + 1) % N];
			Sum += A.X * B.Y - B.X * A.Y;
		}
		return 0.5 * Sum;
	}

	bool IsPointInRing(const FVector2D& P, const TArray<FVector2D>& Ring)
	{
		const int32 N = Ring.Num();
		if (N < 3)
		{
			return false;
		}

		bool bInside = false;
		for (int32 i = 0, j = N - 1; i < N; j = i++)
		{
			const FVector2D& A = Ring[i];
			const FVector2D& B = Ring[j];
			if (((A.Y > P.Y) != (B.Y > P.Y)) &&
				(P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X))
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	TArray<FVector2D> CleanRing(const TArray<FVector2D>& In, double Tolerance)
	{
		TArray<FVector2D> Out;
		Out.Reserve(In.Num());

		const double Tol2 = Tolerance * Tolerance;
		for (const FVector2D& P : In)
		{
			if (Out.Num() > 0)
			{
				if (FVector2D::DistSquared(Out.Last(), P) <= Tol2)
				{
					continue;
				}
			}
			Out.Add(P);
		}

		while (Out.Num() > 1 && FVector2D::DistSquared(Out[0], Out.Last()) <= Tol2)
		{
			Out.Pop();
		}

		if (Out.Num() < 3)
		{
			Out.Reset();
		}
		return Out;
	}

	void NormalizeRing(TArray<FVector2D>& Ring, bool bCounterClockwise)
	{
		const double Area = SignedArea(Ring);
		const bool bIsCCW = Area > 0.0;
		if (bIsCCW != bCounterClockwise)
		{
			Algo::Reverse(Ring);
		}
	}

	void ClassifyRings(
		const TArray<TArray<FVector2D>>& Rings,
		TArray<TArray<FVector2D>>& OutOuters,
		TArray<TArray<TArray<FVector2D>>>& OutHoles)
	{
		OutOuters.Reset();
		OutHoles.Reset();

		struct FRingInfo
		{
			int32 Index = 0;
			double AbsArea = 0.0;
		};

		TArray<FRingInfo> Sorted;
		Sorted.Reserve(Rings.Num());
		for (int32 i = 0; i < Rings.Num(); ++i)
		{
			if (Rings[i].Num() < 3)
			{
				continue;
			}
			const double Area = FMath::Abs(SignedArea(Rings[i]));
			if (Area <= 1e-6)
			{
				continue;
			}
			Sorted.Add({i, Area});
		}

		if (Sorted.Num() == 0)
		{
			return;
		}

		Sorted.Sort([](const FRingInfo& A, const FRingInfo& B)
		{
			return A.AbsArea > B.AbsArea;
		});

		// Parent = smallest ring that contains this one (INDEX_NONE when top level).
		TArray<int32> Parent;
		Parent.Init(INDEX_NONE, Rings.Num());

		for (int32 a = 0; a < Sorted.Num(); ++a)
		{
			const int32 RingIndex = Sorted[a].Index;
			const FVector2D Probe = Rings[RingIndex][0];
			for (int32 b = a - 1; b >= 0; --b)
			{
				const int32 Candidate = Sorted[b].Index;
				if (IsPointInRing(Probe, Rings[Candidate]))
				{
					Parent[RingIndex] = Candidate;
					break;
				}
			}
		}

		auto DepthOf = [&Parent](int32 RingIndex)
		{
			int32 Depth = 0;
			int32 Cursor = Parent[RingIndex];
			int32 Guard = 0;
			while (Cursor != INDEX_NONE && Guard++ < 64)
			{
				++Depth;
				Cursor = Parent[Cursor];
			}
			return Depth;
		};

		TArray<int32> OuterIndexOfRing;
		OuterIndexOfRing.Init(INDEX_NONE, Rings.Num());

		for (int32 a = 0; a < Sorted.Num(); ++a)
		{
			const int32 RingIndex = Sorted[a].Index;
			if (DepthOf(RingIndex) % 2 == 0)
			{
				TArray<FVector2D> Ring = Rings[RingIndex];
				NormalizeRing(Ring, true);
				OuterIndexOfRing[RingIndex] = OutOuters.Add(MoveTemp(Ring));
				OutHoles.AddDefaulted();
			}
		}

		for (int32 a = 0; a < Sorted.Num(); ++a)
		{
			const int32 RingIndex = Sorted[a].Index;
			if (DepthOf(RingIndex) % 2 == 0)
			{
				continue;
			}

			const int32 ParentIndex = Parent[RingIndex];
			const int32 OwnerOuter = (ParentIndex != INDEX_NONE) ? OuterIndexOfRing[ParentIndex] : INDEX_NONE;
			if (OwnerOuter == INDEX_NONE)
			{
				continue;
			}

			TArray<FVector2D> Hole = Rings[RingIndex];
			NormalizeRing(Hole, false);
			OutHoles[OwnerOuter].Add(MoveTemp(Hole));
		}
	}

	void TriangulateWithHoles(
		const TArray<FVector2D>& Outer,
		const TArray<TArray<FVector2D>>& Holes,
		TArray<FIntVector>& OutTriangles)
	{
		OutTriangles.Reset();

		if (Outer.Num() < 3)
		{
			return;
		}

		TArray<FVector2D> Poly;
		if (Holes.Num() > 0)
		{
			EliminateHoles(Outer, Holes, Poly);
		}
		else
		{
			Poly = Outer;
		}

		EarClip(Poly, OutTriangles);
	}

	void BuildExtrusion(
		const TArray<FVector2D>& Outer,
		const TArray<TArray<FVector2D>>& Holes,
		double BaseZ,
		double TopZ,
		const FColor& Color,
		bool bCloseBottom,
		FShpMeshData& Out)
	{
		if (Outer.Num() < 3 || TopZ <= BaseZ)
		{
			return;
		}

		// ---------------------------------------------------------------- walls
		auto AddWalls = [&Out, BaseZ, TopZ, &Color](const TArray<FVector2D>& Ring)
		{
			const int32 N = Ring.Num();
			if (N < 3)
			{
				return;
			}

			for (int32 i = 0; i < N; ++i)
			{
				const FVector2D& A = Ring[i];
				const FVector2D& B = Ring[(i + 1) % N];
				const double DX = B.X - A.X;
				const double DY = B.Y - A.Y;
				const double Len = FMath::Sqrt(DX * DX + DY * DY);
				if (Len < 1e-3)
				{
					continue;
				}

				// Right-hand side of the edge direction = outward for CCW rings.
				const FVector Normal(DY / Len, -DX / Len, 0.0);
				const uint32 Base = static_cast<uint32>(Out.Positions.Num());

				Out.Positions.Add(FVector(A.X, A.Y, BaseZ));
				Out.Positions.Add(FVector(B.X, B.Y, BaseZ));
				Out.Positions.Add(FVector(B.X, B.Y, TopZ));
				Out.Positions.Add(FVector(A.X, A.Y, TopZ));

				for (int32 k = 0; k < 4; ++k)
				{
					Out.Normals.Add(Normal);
					Out.Colors.Add(Color);
				}

				Out.Indices.Add(Base + 0);
				Out.Indices.Add(Base + 1);
				Out.Indices.Add(Base + 2);
				Out.Indices.Add(Base + 0);
				Out.Indices.Add(Base + 2);
				Out.Indices.Add(Base + 3);
			}
		};

		AddWalls(Outer);
		for (const TArray<FVector2D>& Hole : Holes)
		{
			AddWalls(Hole);
		}

		// ------------------------------------------------------------ roof/floor
		TArray<FIntVector> Tris;
		TriangulateWithHoles(Outer, Holes, Tris);
		if (Tris.Num() == 0)
		{
			return;
		}

		// Vertex table for the caps: outer ring followed by every hole ring.
		TArray<FVector2D> CapPoints;
		CapPoints.Reserve(Outer.Num());
		CapPoints.Append(Outer);
		for (const TArray<FVector2D>& Hole : Holes)
		{
			CapPoints.Append(Hole);
		}

		const uint32 CapBase = static_cast<uint32>(Out.Positions.Num());
		for (const FVector2D& P : CapPoints)
		{
			Out.Positions.Add(FVector(P.X, P.Y, TopZ));
			Out.Normals.Add(FVector(0.0, 0.0, 1.0));
			Out.Colors.Add(Color);
		}

		for (const FIntVector& T : Tris)
		{
			if (T.X == INDEX_NONE || T.Y == INDEX_NONE || T.Z == INDEX_NONE)
			{
				continue;
			}
			Out.Indices.Add(CapBase + static_cast<uint32>(T.X));
			Out.Indices.Add(CapBase + static_cast<uint32>(T.Y));
			Out.Indices.Add(CapBase + static_cast<uint32>(T.Z));
		}

		if (bCloseBottom)
		{
			const uint32 BottomBase = static_cast<uint32>(Out.Positions.Num());
			for (const FVector2D& P : CapPoints)
			{
				Out.Positions.Add(FVector(P.X, P.Y, BaseZ));
				Out.Normals.Add(FVector(0.0, 0.0, -1.0));
				Out.Colors.Add(Color);
			}
			for (const FIntVector& T : Tris)
			{
				if (T.X == INDEX_NONE || T.Y == INDEX_NONE || T.Z == INDEX_NONE)
				{
					continue;
				}
				Out.Indices.Add(BottomBase + static_cast<uint32>(T.X));
				Out.Indices.Add(BottomBase + static_cast<uint32>(T.Z));
				Out.Indices.Add(BottomBase + static_cast<uint32>(T.Y));
			}
		}
	}
}
