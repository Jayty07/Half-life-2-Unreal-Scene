#include "HL2NeighborhoodsBlockout.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HL2NeighborhoodsBlockout)

AHL2NeighborhoodsBlockout::AHL2NeighborhoodsBlockout()
{
	// Warm, sun-bleached storybook palette.
	Palette = {
		FLinearColor(0.56f, 0.54f, 0.49f), // Concrete (weathered stone)
		FLinearColor(0.17f, 0.17f, 0.18f), // DarkConcrete (asphalt)
		FLinearColor(0.52f, 0.46f, 0.38f), // Tile (cobbles)
		FLinearColor(0.58f, 0.32f, 0.22f), // Brick
		FLinearColor(0.30f, 0.34f, 0.36f), // Metal
		FLinearColor(0.20f, 0.22f, 0.26f), // Combine
		FLinearColor(0.42f, 0.26f, 0.14f), // Wood
		FLinearColor(0.20f, 0.36f, 0.33f), // Train (old tram green)
		FLinearColor(0.95f, 0.76f, 0.38f), // Screen (warm lit windows)
		FLinearColor(0.88f, 0.85f, 0.76f), // Trim
		FLinearColor(0.88f, 0.81f, 0.65f), // Plaster
		FLinearColor(0.88f, 0.64f, 0.44f), // PlasterWarm
		FLinearColor(0.60f, 0.18f, 0.11f), // RoofRed
		FLinearColor(0.16f, 0.42f, 0.44f), // RoofTeal
		FLinearColor(0.30f, 0.52f, 0.17f), // Foliage
		FLinearColor(0.11f, 0.29f, 0.11f), // FoliageDark
		FLinearColor(0.36f, 0.55f, 0.22f), // Grass
		FLinearColor(0.47f, 0.44f, 0.40f), // Rubble
		FLinearColor(0.13f, 0.36f, 0.45f), // Water
		FLinearColor(0.46f, 0.35f, 0.22f), // Dirt
		FLinearColor(0.22f, 0.44f, 0.13f), // Vines
		FLinearColor(0.95f, 0.10f, 0.75f), // Marker
		FLinearColor(0.55f, 0.68f, 0.82f), // PlasterBlue
		FLinearColor(0.86f, 0.60f, 0.62f), // PlasterRose
		FLinearColor(0.62f, 0.72f, 0.55f), // PlasterSage
		FLinearColor(0.85f, 0.70f, 0.30f), // PlasterOchre
		FLinearColor(0.28f, 0.20f, 0.15f), // TimberDark
		FLinearColor(0.26f, 0.28f, 0.36f), // RoofSlate
		FLinearColor(0.76f, 0.48f, 0.16f), // RoofOchre
		FLinearColor(0.30f, 0.40f, 0.18f), // RoofMoss
		FLinearColor(0.78f, 0.12f, 0.10f), // PaintRed
		FLinearColor(0.12f, 0.30f, 0.72f), // PaintBlue
		FLinearColor(0.95f, 0.80f, 0.12f), // PaintYellow
		FLinearColor(0.10f, 0.52f, 0.28f), // PaintGreen
	};

	Sun->SetRelativeRotation(FRotator(-32.0f, 135.0f, 0.0f));
	Sun->Intensity = 7.0f;
	Sun->LightColor = FColor(255, 238, 205);
	HeightFog->FogDensity = 0.012f;
	HeightFog->FogHeightFalloff = 0.05f;
}

FTransform AHL2NeighborhoodsBlockout::GetPlayerStartTransform() const
{
	// South side of the hub plaza (top at 4 HU), facing the great tree (+Y).
	// PlayerStart pivots sit at the centre of a 92 cm half-height capsule.
	constexpr float HammerUnit = 1.905f;
	const FVector Local(0.0f, -560.0f * HammerUnit, 4.0f * HammerUnit + 94.0f);
	return FTransform(FRotator(0.0f, 90.0f, 0.0f), Local) * GetActorTransform();
}

// ---------------------------------------------------------------------------
// Layout (pure geometry; Hammer units, actor-local)
//
//                     Shrine (H1)
//                         |
//   C1 ---- Maple Lane ---T1                    Sparrow Close ---- C2
//   | (ivy secret path)   |  Lantern Road  T2 ---------------------  |
//   v                     |                                         | stairs
//  Grove heart  [B2]  Grove Path ---- HUB ---- Tram Steps -> Clocktower Hill (H2)
//  (H5)  ^ ledge L1       |          / | \                          |
//        |          Windmill Path   /  |  Rubble Row ---T3---[B1]--> Rubble Terraces (H7, L2)
//  Windmill Fields (H6) <----------    |                |
//                              Canal Walk          Quarry Lane      Fallen Block (H3)
//                                      |                |
//  Canal promenade (H4) ---[B4 drawbridge]---  far bank  C3 -> Quarry -[B3 tunnel]-> canal
//
// Heights: player 72 standing / 36 crouched, step 18, jump ~21 (crouch-jump
// ~56). Boundaries are >= 192 tall; upgrade ledges are 72 tall; the broken
// footbridge gap (128) needs a sprint jump.
// ---------------------------------------------------------------------------

namespace HL2Neighborhoods
{
	struct FRand
	{
		uint32 State;

		explicit FRand(uint32 Seed) : State(Seed * 2654435761u + 0x9E3779B9u) { Next(); }

		float Next()
		{
			State = State * 1664525u + 1013904223u;
			return static_cast<float>(State >> 8) / 16777216.0f;
		}

		float Range(float A, float B) { return A + (B - A) * Next(); }
		bool Chance(float P) { return Next() < P; }
	};

	inline uint32 HashXY(float X, float Y)
	{
		return static_cast<uint32>(static_cast<int32>(X) * 73856093) ^ static_cast<uint32>(static_cast<int32>(Y) * 19349663);
	}

	inline bool InsideAny(const FVector2D& P, const TArray<FBox2D>& Boxes)
	{
		for (const FBox2D& B : Boxes)
		{
			if (P.X >= B.Min.X && P.X <= B.Max.X && P.Y >= B.Min.Y && P.Y <= B.Max.Y)
			{
				return true;
			}
		}
		return false;
	}

	constexpr float RoadHalf = 160.0f;
	constexpr float WalkWidth = 64.0f;
	constexpr float SetBack = 272.0f;
	constexpr float StoreyHeight = 112.0f;
	constexpr float PlinthHeight = 16.0f;
	constexpr float MapHalf = 6400.0f;

	/** Canal water strip (Y range) and depth of its bed. */
	constexpr float CanalY0 = -5000.0f;
	constexpr float CanalY1 = -4600.0f;
	constexpr float CanalBedZ = -160.0f;
	constexpr float GroundBottomZ = -192.0f;

	/** Terrain grid over the walkable square inside the perimeter. */
	constexpr float TerrainCell = 64.0f;
	constexpr int32 TerrainCells = static_cast<int32>(2.0f * MapHalf / TerrainCell);
	constexpr float TerrainPeak = 176.0f;
	constexpr float TerrainTerrace = 8.0f;
	/** Largest height change between neighbouring cells; below the 18 HU step height. */
	constexpr float TerrainMaxRise = 16.0f;
	/** Cells of level ground kept around anything built on the ground. */
	constexpr int32 TerrainFlatMargin = 1;
	/** Primitives starting above this are not on the ground and do not flatten it. */
	constexpr float TerrainFlatMaxZ = 40.0f;

	inline float LatticeValue(int32 X, int32 Y, uint32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 73856093u ^ static_cast<uint32>(Y) * 19349663u ^ Seed * 83492791u;
		H ^= H >> 13;
		H *= 0x5bd1e995u;
		H ^= H >> 15;
		return static_cast<float>(H & 0xFFFFFFu) / 16777216.0f;
	}

	/** Smooth value noise in [0, 1]. */
	inline float ValueNoise(float X, float Y, float Wavelength, uint32 Seed)
	{
		const float FX = X / Wavelength;
		const float FY = Y / Wavelength;
		const int32 IX = FMath::FloorToInt32(FX);
		const int32 IY = FMath::FloorToInt32(FY);
		auto Smooth = [](float T) { return T * T * (3.0f - 2.0f * T); };
		const float TX = Smooth(FX - IX);
		const float TY = Smooth(FY - IY);
		const float A = LatticeValue(IX, IY, Seed) + (LatticeValue(IX + 1, IY, Seed) - LatticeValue(IX, IY, Seed)) * TX;
		const float B = LatticeValue(IX, IY + 1, Seed) + (LatticeValue(IX + 1, IY + 1, Seed) - LatticeValue(IX, IY + 1, Seed)) * TX;
		return A + (B - A) * TY;
	}

	inline FBox2D Expand(const FBox2D& B, float By)
	{
		return FBox2D(B.Min - FVector2D(By, By), B.Max + FVector2D(By, By));
	}

	inline bool OverlapsAny(const FBox2D& A, const TArray<FBox2D>& Boxes, float Margin)
	{
		for (const FBox2D& B : Boxes)
		{
			if (A.Min.X < B.Max.X + Margin && A.Max.X > B.Min.X - Margin && A.Min.Y < B.Max.Y + Margin && A.Max.Y > B.Min.Y - Margin)
			{
				return true;
			}
		}
		return false;
	}
}

using namespace HL2Neighborhoods;

void AHL2NeighborhoodsBlockout::BuildLayout()
{
	HouseCount = 0;
	HiddenRoomCount = 0;
	BlockerCount = 0;
	HouseFootprints.Reset();
	PavedAreas.Reset();
	ScatterJobs.Reset();
	TerrainHeights.Reset();
	FlatCells.Init(0, TerrainCells * TerrainCells);
	FlattenSuppression = 0;

	BuildGround();
	BuildPerimeter();
	BuildHub();
	BuildNorthSuburbs();
	BuildClocktowerHill();
	BuildRubbleTerraces();
	BuildFallenBlockAndQuarry();
	BuildCanalQuarter();
	BuildGrove();
	BuildWindmillFields();
	BuildSkyline();

	BuildTerrain();
	for (const FScatterJob& Job : ScatterJobs)
	{
		RunScatter(Job);
	}
	ScatterJobs.Reset();
}

// ---------------------------------------------------------------------------
// Local-frame helpers
// ---------------------------------------------------------------------------

FVector AHL2NeighborhoodsBlockout::FFrame::ToWorld(const FVector& L) const
{
	switch (Facing & 3)
	{
	case 1:  return FVector(Origin.X - L.Y, Origin.Y + L.X, BaseZ + L.Z);
	case 2:  return FVector(Origin.X - L.X, Origin.Y - L.Y, BaseZ + L.Z);
	case 3:  return FVector(Origin.X + L.Y, Origin.Y - L.X, BaseZ + L.Z);
	default: return FVector(Origin.X + L.X, Origin.Y + L.Y, BaseZ + L.Z);
	}
}

void AHL2NeighborhoodsBlockout::LBox(const FFrame& F, EHL2BlockoutMaterial Mat, const FVector& LocalMin, const FVector& LocalMax)
{
	const FVector A = F.ToWorld(LocalMin);
	const FVector B = F.ToWorld(LocalMax);
	Box(Mat, FVector(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z)),
		FVector(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z)));
}

void AHL2NeighborhoodsBlockout::LOriented(const FFrame& F, EHL2BlockoutMaterial Mat, const FVector& LocalCenter, const FVector& Size, const FRotator& LocalRotation)
{
	// The frame is a pure yaw of 90 * Facing degrees, which composes by adding yaw.
	OrientedBox(Mat, F.ToWorld(LocalCenter), Size,
		FRotator(LocalRotation.Pitch, LocalRotation.Yaw + 90.0f * (F.Facing & 3), LocalRotation.Roll));
}

void AHL2NeighborhoodsBlockout::LCylinder(const FFrame& F, EHL2BlockoutMaterial Mat, const FVector& LocalBase, float Radius, float Height)
{
	Cylinder(Mat, F.ToWorld(LocalBase), Radius, Height);
}

void AHL2NeighborhoodsBlockout::LRoof(const FFrame& F, EHL2BlockoutMaterial Mat, const FVector& LocalMin, const FVector& LocalMax, bool bRidgeAlongX, int32 Steps)
{
	const FVector A = F.ToWorld(LocalMin);
	const FVector B = F.ToWorld(LocalMax);
	const bool bFrameEven = (F.Facing & 1) == 0;
	const int32 RidgeAxis = (bRidgeAlongX == bFrameEven) ? 0 : 1;
	GableRoof(Mat, FVector(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z)),
		FVector(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z)), RidgeAxis, Steps);
}

// ---------------------------------------------------------------------------
// Ground-aware primitives and terrain
// ---------------------------------------------------------------------------

void AHL2NeighborhoodsBlockout::Box(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU)
{
	MarkFlat(MinHU, MaxHU);
	AHL2BlockoutBase::Box(Mat, MinHU, MaxHU);
}

void AHL2NeighborhoodsBlockout::OrientedBox(EHL2BlockoutMaterial Mat, const FVector& CenterHU, const FVector& SizeHU, const FRotator& Rotation)
{
	// Bounding sphere: conservative for any rotation.
	const float Radius = 0.5f * FMath::Sqrt(SizeHU.X * SizeHU.X + SizeHU.Y * SizeHU.Y + SizeHU.Z * SizeHU.Z);
	MarkFlat(CenterHU - FVector(Radius, Radius, Radius), CenterHU + FVector(Radius, Radius, Radius));
	AHL2BlockoutBase::OrientedBox(Mat, CenterHU, SizeHU, Rotation);
}

void AHL2NeighborhoodsBlockout::Cylinder(EHL2BlockoutMaterial Mat, const FVector& BaseCenterHU, float RadiusHU, float HeightHU)
{
	MarkFlat(BaseCenterHU - FVector(RadiusHU, RadiusHU, 0.0f), BaseCenterHU + FVector(RadiusHU, RadiusHU, HeightHU));
	AHL2BlockoutBase::Cylinder(Mat, BaseCenterHU, RadiusHU, HeightHU);
}

void AHL2NeighborhoodsBlockout::WallWithOpenings(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, int32 ThinAxis, const TArray<FBox2D>& Openings)
{
	MarkFlat(MinHU, MaxHU);
	AHL2BlockoutBase::WallWithOpenings(Mat, MinHU, MaxHU, ThinAxis, Openings);
}

void AHL2NeighborhoodsBlockout::Stairs(EHL2BlockoutMaterial Mat, const FVector& StartHU, const FIntPoint& Direction, int32 NumSteps, float RiseHU, float RunHU, float WidthHU)
{
	const float Length = NumSteps * RunHU;
	const FVector End = StartHU + FVector(Direction.X * Length + (Direction.X != 0 ? 0.0f : WidthHU), Direction.Y * Length + (Direction.X != 0 ? WidthHU : 0.0f), NumSteps * RiseHU);
	MarkFlat(FVector(FMath::Min(StartHU.X, End.X), FMath::Min(StartHU.Y, End.Y), StartHU.Z), FVector(FMath::Max(StartHU.X, End.X), FMath::Max(StartHU.Y, End.Y), End.Z));
	AHL2BlockoutBase::Stairs(Mat, StartHU, Direction, NumSteps, RiseHU, RunHU, WidthHU);
}

void AHL2NeighborhoodsBlockout::MarkFlat(const FVector& Min, const FVector& Max)
{
	if (FlattenSuppression > 0 || Min.Z > TerrainFlatMaxZ || Max.Z <= 0.25f)
	{
		return;
	}
	MarkFlatXY(FVector2D(Min.X, Min.Y), FVector2D(Max.X, Max.Y));
}

void AHL2NeighborhoodsBlockout::MarkFlatXY(const FVector2D& Min, const FVector2D& Max)
{
	if (FlatCells.Num() != TerrainCells * TerrainCells || Max.X < -MapHalf || Max.Y < -MapHalf || Min.X > MapHalf || Min.Y > MapHalf)
	{
		return;
	}
	const int32 X0 = FMath::Clamp(FMath::FloorToInt32((Min.X + MapHalf) / TerrainCell) - TerrainFlatMargin, 0, TerrainCells - 1);
	const int32 X1 = FMath::Clamp(FMath::FloorToInt32((Max.X + MapHalf) / TerrainCell) + TerrainFlatMargin, 0, TerrainCells - 1);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt32((Min.Y + MapHalf) / TerrainCell) - TerrainFlatMargin, 0, TerrainCells - 1);
	const int32 Y1 = FMath::Clamp(FMath::FloorToInt32((Max.Y + MapHalf) / TerrainCell) + TerrainFlatMargin, 0, TerrainCells - 1);
	for (int32 Y = Y0; Y <= Y1; ++Y)
	{
		for (int32 X = X0; X <= X1; ++X)
		{
			FlatCells[Y * TerrainCells + X] = 1;
		}
	}
}

void AHL2NeighborhoodsBlockout::BuildTerrain()
{
	using M = EHL2BlockoutMaterial;
	constexpr int32 N = TerrainCells;
	TerrainHeights.Init(0.0f, N * N);

	// Two octaves of value noise; only the upper part of the range rises, giving separate hills.
	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			if (FlatCells[Y * N + X])
			{
				continue;
			}
			const float WX = -MapHalf + (X + 0.5f) * TerrainCell;
			const float WY = -MapHalf + (Y + 0.5f) * TerrainCell;
			const float Noise = 0.62f * ValueNoise(WX, WY, 1500.0f, 7u) + 0.38f * ValueNoise(WX, WY, 640.0f, 19u);
			const float Height = TerrainPeak * FMath::Clamp((Noise - 0.34f) / 0.45f, 0.0f, 1.0f);
			TerrainHeights[Y * N + X] = FMath::FloorToInt32(Height / TerrainTerrace) * TerrainTerrace;
		}
	}

	// Limit the rise between neighbouring cells (two-pass chamfer), so hills slope down to the flat ground.
	auto Relax = [&](int32 X, int32 Y, int32 NX, int32 NY)
	{
		if (NX >= 0 && NY >= 0 && NX < N && NY < N)
		{
			float& H = TerrainHeights[Y * N + X];
			H = FMath::Min(H, TerrainHeights[NY * N + NX] + TerrainMaxRise);
		}
	};
	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			Relax(X, Y, X - 1, Y);
			Relax(X, Y, X, Y - 1);
		}
	}
	for (int32 Y = N - 1; Y >= 0; --Y)
	{
		for (int32 X = N - 1; X >= 0; --X)
		{
			Relax(X, Y, X + 1, Y);
			Relax(X, Y, X, Y + 1);
		}
	}

	// One box per run of equal height along each row.
	for (int32 Y = 0; Y < N; ++Y)
	{
		int32 X = 0;
		while (X < N)
		{
			const float H = TerrainHeights[Y * N + X];
			int32 End = X + 1;
			while (End < N && TerrainHeights[Y * N + End] == H)
			{
				++End;
			}
			if (H > 0.0f)
			{
				AHL2BlockoutBase::Box(M::Grass, FVector(-MapHalf + X * TerrainCell, -MapHalf + Y * TerrainCell, -8.0f),
					FVector(-MapHalf + End * TerrainCell, -MapHalf + (Y + 1) * TerrainCell, H));
			}
			X = End;
		}
	}
}

float AHL2NeighborhoodsBlockout::TerrainHeightAt(const FVector2D& P) const
{
	const int32 X = FMath::FloorToInt32((P.X + MapHalf) / TerrainCell);
	const int32 Y = FMath::FloorToInt32((P.Y + MapHalf) / TerrainCell);
	if (TerrainHeights.Num() != TerrainCells * TerrainCells || X < 0 || Y < 0 || X >= TerrainCells || Y >= TerrainCells)
	{
		return 0.0f;
	}
	return TerrainHeights[Y * TerrainCells + X];
}

// ---------------------------------------------------------------------------
// Props and dressing
// ---------------------------------------------------------------------------

void AHL2NeighborhoodsBlockout::Tree(const FVector& Base, float Scale, uint32 Seed)
{
	using M = EHL2BlockoutMaterial;
	FRand R(Seed);
	++FlattenSuppression;

	const float TrunkH = 190.0f * Scale;
	Cylinder(M::Wood, Base, 9.0f * Scale, TrunkH);

	// A couple of crooked limbs.
	for (int32 Limb = 0; Limb < 2; ++Limb)
	{
		const float Yaw = R.Range(0.0f, 360.0f);
		const FVector Dir(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0.0f);
		OrientedBox(M::Wood, Base + Dir * 22.0f * Scale + FVector(0.0f, 0.0f, TrunkH * 0.75f),
			FVector(60.0f * Scale, 8.0f * Scale, 8.0f * Scale), FRotator(35.0f, Yaw, 0.0f));
	}

	// Billowy layered canopy.
	const int32 Puffs = 4 + static_cast<int32>(R.Range(0.0f, 3.0f));
	for (int32 Puff = 0; Puff < Puffs; ++Puff)
	{
		const float A = R.Range(0.0f, 2.0f * UE_PI);
		const float Dist = (Puff == 0) ? 0.0f : R.Range(30.0f, 70.0f) * Scale;
		const float Radius = R.Range(55.0f, 85.0f) * Scale;
		const float Z = TrunkH * R.Range(0.70f, 0.95f) + (Puff == 0 ? 25.0f * Scale : 0.0f);
		const FVector C = Base + FVector(FMath::Cos(A) * Dist, FMath::Sin(A) * Dist, Z);
		Cylinder(R.Chance(0.6f) ? M::Foliage : M::FoliageDark, C, Radius, R.Range(45.0f, 75.0f) * Scale);
	}
	Cylinder(M::Foliage, Base + FVector(0.0f, 0.0f, TrunkH + 40.0f * Scale), 40.0f * Scale, 35.0f * Scale);
	--FlattenSuppression;
}

void AHL2NeighborhoodsBlockout::Shrub(const FVector& Base, float Size, uint32 Seed)
{
	using M = EHL2BlockoutMaterial;
	FRand R(Seed);
	++FlattenSuppression;
	Cylinder(R.Chance(0.5f) ? M::Foliage : M::FoliageDark, Base, Size * 0.5f, Size * R.Range(0.45f, 0.8f));
	if (R.Chance(0.6f))
	{
		Cylinder(M::Foliage, Base + FVector(R.Range(-0.3f, 0.3f) * Size, R.Range(-0.3f, 0.3f) * Size, 0.0f), Size * 0.32f, Size * R.Range(0.6f, 1.0f));
	}
	--FlattenSuppression;
}

void AHL2NeighborhoodsBlockout::RubblePile(const FVector& Center, float Radius, float Height, uint32 Seed)
{
	using M = EHL2BlockoutMaterial;
	FRand R(Seed);

	// Solid core so the pile reads as one mound, then tumbled chunks.
	Cylinder(M::Rubble, Center, Radius * 0.7f, Height * 0.6f);
	const int32 Chunks = 6 + static_cast<int32>(Radius / 24.0f);
	for (int32 Chunk = 0; Chunk < Chunks; ++Chunk)
	{
		const float A = R.Range(0.0f, 2.0f * UE_PI);
		const float D = R.Range(0.0f, Radius * 0.85f);
		const float T = 1.0f - D / Radius;
		const float S = R.Range(0.25f, 0.55f) * Radius;
		const FVector C = Center + FVector(FMath::Cos(A) * D, FMath::Sin(A) * D, Height * T * 0.55f);
		OrientedBox(R.Chance(0.7f) ? M::Rubble : M::Concrete, C, FVector(S, S * R.Range(0.5f, 1.0f), S * R.Range(0.3f, 0.7f)),
			FRotator(R.Range(-25.0f, 25.0f), R.Range(0.0f, 360.0f), R.Range(-25.0f, 25.0f)));
	}
	// Rebar and a little moss.
	OrientedBox(M::Metal, Center + FVector(0.0f, 0.0f, Height * 0.7f), FVector(2.0f, 2.0f, Height * 0.8f), FRotator(R.Range(10.0f, 30.0f), R.Range(0.0f, 360.0f), 0.0f));
	Cylinder(M::Vines, Center + FVector(R.Range(-0.3f, 0.3f) * Radius, R.Range(-0.3f, 0.3f) * Radius, Height * 0.45f), Radius * 0.35f, Height * 0.2f);
}

void AHL2NeighborhoodsBlockout::Lantern(const FVector& Base)
{
	using M = EHL2BlockoutMaterial;
	Box(M::Wood, Base + FVector(-3.0f, -3.0f, 0.0f), Base + FVector(3.0f, 3.0f, 96.0f));
	Box(M::Wood, Base + FVector(-3.0f, -3.0f, 92.0f), Base + FVector(18.0f, 3.0f, 96.0f));
	Box(M::Screen, Base + FVector(10.0f, -5.0f, 76.0f), Base + FVector(20.0f, 5.0f, 90.0f));
	Box(M::RoofRed, Base + FVector(8.0f, -7.0f, 90.0f), Base + FVector(22.0f, 7.0f, 93.0f));
}

void AHL2NeighborhoodsBlockout::HiddenRoom(EHL2BlockoutMaterial Mat, const FVector& Min, const FVector& Max, int32 EntranceSide, float EntranceCenter)
{
	using M = EHL2BlockoutMaterial;
	constexpr float T = 16.0f;
	constexpr float DoorHalf = 32.0f;
	constexpr float DoorHeight = 44.0f;

	Box(Mat, FVector(Min.X, Min.Y, Max.Z - T), Max); // roof

	const float Floor = Min.Z;
	const float Top = Max.Z - T;
	auto Opening = [&](int32 Side) -> TArray<FBox2D>
	{
		if (Side != EntranceSide)
		{
			return {};
		}
		return { FBox2D(FVector2D(EntranceCenter - DoorHalf, Floor), FVector2D(EntranceCenter + DoorHalf, Floor + DoorHeight)) };
	};

	WallWithOpenings(Mat, FVector(Min.X, Min.Y, Floor), FVector(Min.X + T, Max.Y, Top), 0, Opening(0));
	WallWithOpenings(Mat, FVector(Max.X - T, Min.Y, Floor), FVector(Max.X, Max.Y, Top), 0, Opening(1));
	WallWithOpenings(Mat, FVector(Min.X + T, Min.Y, Floor), FVector(Max.X - T, Min.Y + T, Top), 1, Opening(2));
	WallWithOpenings(Mat, FVector(Min.X + T, Max.Y - T, Floor), FVector(Max.X - T, Max.Y, Top), 1, Opening(3));

	// Placeholder scrap cache.
	const FVector C((Min.X + Max.X) * 0.5f, (Min.Y + Max.Y) * 0.5f, Floor);
	Box(M::Marker, C + FVector(-16.0f, -16.0f, 0.0f), C + FVector(16.0f, 16.0f, 24.0f));
	Box(M::Wood, C + FVector(-20.0f, 20.0f, 0.0f), C + FVector(4.0f, 36.0f, 16.0f));
	Box(M::Screen, C + FVector(-4.0f, -4.0f, Top - Floor - 10.0f), C + FVector(4.0f, 4.0f, Top - Floor - 2.0f));

	++HiddenRoomCount;
}

void AHL2NeighborhoodsBlockout::BlockerSign(const FVector& Base)
{
	using M = EHL2BlockoutMaterial;
	Box(M::Wood, Base + FVector(-3.0f, -3.0f, 0.0f), Base + FVector(3.0f, 3.0f, 80.0f));
	Box(M::Marker, Base + FVector(-28.0f, -4.0f, 60.0f), Base + FVector(28.0f, 4.0f, 96.0f));
	++BlockerCount;
}

void AHL2NeighborhoodsBlockout::MaintenanceRobot(const FVector& Base, float Yaw)
{
	using M = EHL2BlockoutMaterial;
	const FVector Fwd(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0.0f);
	const FVector Side(-Fwd.Y, Fwd.X, 0.0f);

	// Squat, rounded caretaker: treads, barrel body, dome head, lamp eye, arms.
	OrientedBox(M::Metal, Base + Side * 22.0f + FVector(0.0f, 0.0f, 8.0f), FVector(56.0f, 12.0f, 16.0f), FRotator(0.0f, Yaw, 0.0f));
	OrientedBox(M::Metal, Base - Side * 22.0f + FVector(0.0f, 0.0f, 8.0f), FVector(56.0f, 12.0f, 16.0f), FRotator(0.0f, Yaw, 0.0f));
	Cylinder(M::Marker, Base + FVector(0.0f, 0.0f, 16.0f), 22.0f, 40.0f);
	Cylinder(M::Trim, Base + FVector(0.0f, 0.0f, 56.0f), 16.0f, 14.0f);
	Cylinder(M::Screen, Base + Fwd * 13.0f + FVector(0.0f, 0.0f, 58.0f), 5.0f, 8.0f);
	OrientedBox(M::Metal, Base + Side * 26.0f + Fwd * 14.0f + FVector(0.0f, 0.0f, 36.0f), FVector(34.0f, 4.0f, 4.0f), FRotator(-20.0f, Yaw, 0.0f));
	OrientedBox(M::Metal, Base - Side * 26.0f + Fwd * 14.0f + FVector(0.0f, 0.0f, 36.0f), FVector(34.0f, 4.0f, 4.0f), FRotator(-20.0f, Yaw, 0.0f));
	Box(M::Metal, Base + FVector(-1.0f, -1.0f, 70.0f), Base + FVector(1.0f, 1.0f, 92.0f));
	Cylinder(M::Screen, Base + FVector(0.0f, 0.0f, 92.0f), 3.0f, 4.0f);
}

void AHL2NeighborhoodsBlockout::LedgeStripe(const FVector& Min, const FVector& Max)
{
	Box(EHL2BlockoutMaterial::Marker, Min, Max);
}

// ---------------------------------------------------------------------------
// Roads, paths, boundaries
// ---------------------------------------------------------------------------

void AHL2NeighborhoodsBlockout::Road(bool bAlongX, float Center, float From, float To, EHL2BlockoutMaterial Surface,
	const TArray<FVector2D>& GapsNegSide, const TArray<FVector2D>& GapsPosSide)
{
	using M = EHL2BlockoutMaterial;

	auto Emit = [&](EHL2BlockoutMaterial Mat, float A0, float A1, float C0, float C1, float Z0, float Z1)
	{
		if (bAlongX)
		{
			Box(Mat, FVector(A0, C0, Z0), FVector(A1, C1, Z1));
		}
		else
		{
			Box(Mat, FVector(C0, A0, Z0), FVector(C1, A1, Z1));
		}
	};

	Emit(Surface, From, To, Center - RoadHalf, Center + RoadHalf, 0.0f, 2.0f);
	const float Outer = RoadHalf + WalkWidth;
	PavedAreas.Add(bAlongX ? FBox2D(FVector2D(From, Center - Outer), FVector2D(To, Center + Outer))
		: FBox2D(FVector2D(Center - Outer, From), FVector2D(Center + Outer, To)));

	if (Surface == M::DarkConcrete)
	{
		for (float A = From + 40.0f; A + 48.0f < To; A += 128.0f)
		{
			Emit(M::Trim, A, A + 48.0f, Center - 3.0f, Center + 3.0f, 2.0f, 2.25f);
		}
	}

	auto Sidewalk = [&](float C0, float C1, const TArray<FVector2D>& Gaps)
	{
		TArray<float> Cuts = { From, To };
		for (const FVector2D& Gap : Gaps)
		{
			Cuts.Add(FMath::Clamp(static_cast<float>(Gap.X), From, To));
			Cuts.Add(FMath::Clamp(static_cast<float>(Gap.Y), From, To));
		}
		Cuts.Sort();
		for (int32 Index = 0; Index + 1 < Cuts.Num(); ++Index)
		{
			const float Mid = (Cuts[Index] + Cuts[Index + 1]) * 0.5f;
			bool bInGap = false;
			for (const FVector2D& Gap : Gaps)
			{
				bInGap |= Mid > Gap.X && Mid < Gap.Y;
			}
			if (!bInGap)
			{
				Emit(M::Concrete, Cuts[Index], Cuts[Index + 1], C0, C1, 0.0f, 6.0f);
			}
		}
	};
	Sidewalk(Center - RoadHalf - WalkWidth, Center - RoadHalf, GapsNegSide);
	Sidewalk(Center + RoadHalf, Center + RoadHalf + WalkWidth, GapsPosSide);
}

void AHL2NeighborhoodsBlockout::CulDeSac(const FVector2D& Center, float Radius, float EntryYawDeg)
{
	using M = EHL2BlockoutMaterial;

	Cylinder(M::DarkConcrete, FVector(Center.X, Center.Y, 0.0f), Radius, 2.5f);
	PavedAreas.Add(FBox2D(Center - FVector2D(Radius + WalkWidth, Radius + WalkWidth), Center + FVector2D(Radius + WalkWidth, Radius + WalkWidth)));

	// Kerb ring, open where the road comes in.
	constexpr int32 Segments = 32;
	const float RingR = Radius + WalkWidth * 0.5f;
	const float SegLen = 2.0f * UE_PI * RingR / Segments + 6.0f;
	const float EntryHalfDeg = FMath::RadiansToDegrees(FMath::Asin((RoadHalf + WalkWidth) / RingR)) + 4.0f;
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const float Deg = 360.0f * (Segment + 0.5f) / Segments;
		const float Delta = FMath::Abs(FRotator::NormalizeAxis(Deg - EntryYawDeg));
		if (Delta < EntryHalfDeg)
		{
			continue;
		}
		const float Rad = FMath::DegreesToRadians(Deg);
		OrientedBox(M::Concrete, FVector(Center.X + FMath::Cos(Rad) * RingR, Center.Y + FMath::Sin(Rad) * RingR, 3.0f),
			FVector(SegLen, WalkWidth, 6.0f), FRotator(0.0f, Deg + 90.0f, 0.0f));
	}

	// Overgrown island with a tree.
	Cylinder(M::Concrete, FVector(Center.X, Center.Y, 0.0f), 118.0f, 8.0f);
	Cylinder(M::Grass, FVector(Center.X, Center.Y, 0.0f), 110.0f, 12.0f);
	Tree(FVector(Center.X, Center.Y, 12.0f), 1.15f, HashXY(Center.X, Center.Y));
	Shrub(FVector(Center.X + 60.0f, Center.Y - 40.0f, 12.0f), 48.0f, HashXY(Center.X + 1.0f, Center.Y));
}

void AHL2NeighborhoodsBlockout::DirtPath(const FVector2D& Min, const FVector2D& Max)
{
	Box(EHL2BlockoutMaterial::Dirt, FVector(Min.X, Min.Y, 0.0f), FVector(Max.X, Max.Y, 1.5f));
	PavedAreas.Add(FBox2D(Min, Max));
}

void AHL2NeighborhoodsBlockout::Ridge(const FVector2D& Min, const FVector2D& Max, float Height, int32 Style)
{
	using M = EHL2BlockoutMaterial;
	FRand R(HashXY(Min.X + Max.X, Min.Y * 3.0f + Max.Y));

	const M Core = Style == 0 ? M::FoliageDark : (Style == 1 ? M::Rubble : M::Concrete);
	Box(Core, FVector(Min.X, Min.Y, GroundBottomZ), FVector(Max.X, Max.Y, Height));

	const bool bAlongX = (Max.X - Min.X) >= (Max.Y - Min.Y);
	const float Length = bAlongX ? (Max.X - Min.X) : (Max.Y - Min.Y);
	const float Thick = bAlongX ? (Max.Y - Min.Y) : (Max.X - Min.X);
	const float Step = Style == 0 ? 72.0f : 110.0f;

	for (float A = 0.0f; A < Length; A += Step * R.Range(0.7f, 1.3f))
	{
		const FVector2D P = bAlongX ? FVector2D(Min.X + A, (Min.Y + Max.Y) * 0.5f) : FVector2D((Min.X + Max.X) * 0.5f, Min.Y + A);
		const float S = R.Range(0.6f, 1.2f);
		if (Style == 0)
		{
			// Lumpy hedge top with flowering bits.
			Cylinder(R.Chance(0.5f) ? M::Foliage : M::FoliageDark, FVector(P.X, P.Y, Height - 24.0f), Thick * 0.6f * S, R.Range(30.0f, 60.0f));
		}
		else
		{
			// Rubble chunks spilling off both faces, moss on top.
			for (int32 Face = -1; Face <= 1; Face += 2)
			{
				const FVector2D Off = bAlongX ? FVector2D(0.0f, Face * Thick * 0.5f) : FVector2D(Face * Thick * 0.5f, 0.0f);
				const float Sz = R.Range(40.0f, 90.0f) * S;
				OrientedBox(R.Chance(0.6f) ? M::Rubble : M::Concrete, FVector(P.X + Off.X, P.Y + Off.Y, Sz * 0.3f),
					FVector(Sz, Sz * 0.8f, Sz * 0.7f), FRotator(R.Range(-20.0f, 20.0f), R.Range(0.0f, 90.0f), R.Range(-20.0f, 20.0f)));
			}
			if (R.Chance(0.55f))
			{
				Cylinder(R.Chance(0.5f) ? M::Foliage : M::FoliageDark, FVector(P.X, P.Y, Height - 8.0f), Thick * 0.55f * S, R.Range(16.0f, 40.0f));
			}
			if (R.Chance(0.35f))
			{
				// Ivy hanging down a face (walk-through).
				const FVector2D Off = bAlongX ? FVector2D(0.0f, (R.Chance(0.5f) ? -1.0f : 1.0f) * (Thick * 0.5f + 1.0f)) : FVector2D((R.Chance(0.5f) ? -1.0f : 1.0f) * (Thick * 0.5f + 1.0f), 0.0f);
				const FVector Sz = bAlongX ? FVector(R.Range(50.0f, 110.0f), 3.0f, Height * R.Range(0.4f, 0.8f)) : FVector(3.0f, R.Range(50.0f, 110.0f), Height * R.Range(0.4f, 0.8f));
				Box(M::Vines, FVector(P.X + Off.X, P.Y + Off.Y, Height - Sz.Z) - FVector(Sz.X, Sz.Y, 0.0f) * 0.5f,
					FVector(P.X + Off.X, P.Y + Off.Y, Height) + FVector(Sz.X, Sz.Y, 0.0f) * 0.5f);
			}
			if (Style == 2 && R.Chance(0.3f))
			{
				Tree(FVector(P.X, P.Y, Height), R.Range(0.8f, 1.2f), R.State);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Buildings
// ---------------------------------------------------------------------------

void AHL2NeighborhoodsBlockout::House(const FHouse& H)
{
	using M = EHL2BlockoutMaterial;
	FRand R(H.Seed);
	const FFrame F{ H.FrontCenter, H.Facing, H.BaseZ };
	const float W = H.Width;
	const float D = H.Depth;
	const float HalfW = W * 0.5f;

	// Address identity, cycled by build order so neighbours never share it.
	const int32 Id = HouseCount;
	const M Paints[] = { M::PaintRed, M::PaintBlue, M::PaintYellow, M::PaintGreen };
	const M DoorPaint = Paints[Id % 4];
	const M ShutterPaint = Paints[(Id * 3 + 1) % 4];
	const M FlagPaint = Paints[(Id + 2) % 4];
	const int32 Ornament = (Id * 5) % 8;
	const int32 YardFeature = (Id * 7 + 2) % 6;

	HouseFootprints.Add(HouseFootprint(H, Id, false));
	++HouseCount;

	// Plinth (stone, or a rubble mound for houses built into the ruins).
	if (H.Features & HF_Rubble)
	{
		LBox(F, M::Rubble, FVector(-HalfW - 10.0f, -10.0f, -8.0f), FVector(HalfW + 10.0f, D + 10.0f, PlinthHeight));
	}
	else
	{
		LBox(F, M::Concrete, FVector(-HalfW - 6.0f, -6.0f, 0.0f), FVector(HalfW + 6.0f, D + 6.0f, PlinthHeight));
	}

	// Storeys: stone or plaster ground floor, jettied timber-banded upper floors.
	const bool bStoneGround = H.Storeys > 1 && R.Chance(0.5f);
	const float DoorX = R.Range(-HalfW * 0.4f, HalfW * 0.4f);
	float Z = PlinthHeight;
	float Jetty = 0.0f;
	for (int32 Storey = 0; Storey < H.Storeys; ++Storey)
	{
		Jetty = Storey == 0 ? 0.0f : 8.0f * Storey;
		const M WallMat = (Storey == 0 && bStoneGround) ? M::Concrete : H.Wall;
		LBox(F, WallMat, FVector(-HalfW, -Jetty, Z), FVector(HalfW, D + Jetty, Z + StoreyHeight));

		// Front windows with shutters and the odd flower box.
		const int32 Windows = FMath::Max(1, static_cast<int32>(W / 110.0f));
		for (int32 Window = 0; Window < Windows; ++Window)
		{
			const float X = -HalfW + W * (Window + 0.5f) / Windows;
			if (Storey == 0 && FMath::Abs(X - DoorX) < 50.0f)
			{
				continue;
			}
			const float Y = -Jetty;
			LBox(F, M::Screen, FVector(X - 16.0f, Y - 2.0f, Z + 40.0f), FVector(X + 16.0f, Y, Z + 86.0f));
			LBox(F, M::Trim, FVector(X - 19.0f, Y - 3.0f, Z + 37.0f), FVector(X + 19.0f, Y, Z + 40.0f));
			LBox(F, ShutterPaint, FVector(X - 28.0f, Y - 3.0f, Z + 40.0f), FVector(X - 18.0f, Y, Z + 86.0f));
			LBox(F, ShutterPaint, FVector(X + 18.0f, Y - 3.0f, Z + 40.0f), FVector(X + 28.0f, Y, Z + 86.0f));
			if (R.Chance(0.4f))
			{
				LBox(F, M::Wood, FVector(X - 20.0f, Y - 10.0f, Z + 30.0f), FVector(X + 20.0f, Y, Z + 37.0f));
				LBox(F, M::Foliage, FVector(X - 19.0f, Y - 9.0f, Z + 37.0f), FVector(X + 19.0f, Y - 1.0f, Z + 44.0f));
			}
		}

		// One window on each side and at the back.
		LBox(F, M::Screen, FVector(-HalfW - 2.0f, D * 0.5f - 14.0f, Z + 44.0f), FVector(-HalfW, D * 0.5f + 14.0f, Z + 84.0f));
		LBox(F, M::Screen, FVector(HalfW, D * 0.5f - 14.0f, Z + 44.0f), FVector(HalfW + 2.0f, D * 0.5f + 14.0f, Z + 84.0f));
		LBox(F, M::Screen, FVector(-14.0f, D + Jetty, Z + 44.0f), FVector(14.0f, D + Jetty + 2.0f, Z + 84.0f));

		if (Storey + 1 < H.Storeys)
		{
			LBox(F, M::Wood, FVector(-HalfW - 2.0f, -Jetty - 10.0f, Z + StoreyHeight - 6.0f), FVector(HalfW + 2.0f, D + Jetty + 10.0f, Z + StoreyHeight));
		}
		if (Storey > 0)
		{
			// Corner posts and a diagonal brace: half-timbering.
			LBox(F, M::Wood, FVector(-HalfW - 2.0f, -Jetty - 2.0f, Z), FVector(-HalfW + 6.0f, -Jetty, Z + StoreyHeight));
			LBox(F, M::Wood, FVector(HalfW - 6.0f, -Jetty - 2.0f, Z), FVector(HalfW + 2.0f, -Jetty, Z + StoreyHeight));
			LOriented(F, M::Wood, FVector(-HalfW + 30.0f, -Jetty - 1.0f, Z + StoreyHeight * 0.5f), FVector(4.0f, 2.0f, StoreyHeight * 1.05f), FRotator(25.0f, 0.0f, 0.0f));
		}
		Z += StoreyHeight;
	}
	const float EaveZ = Z;
	const float TopJetty = Jetty;

	// Door (solid, decorative), step and a little tilted awning.
	LBox(F, DoorPaint, FVector(DoorX - 20.0f, -3.0f, PlinthHeight), FVector(DoorX + 20.0f, 0.0f, PlinthHeight + 84.0f));
	LBox(F, M::Trim, FVector(DoorX + 12.0f, -5.0f, PlinthHeight + 40.0f), FVector(DoorX + 16.0f, -3.0f, PlinthHeight + 44.0f));
	LBox(F, M::Concrete, FVector(DoorX - 28.0f, -18.0f, 0.0f), FVector(DoorX + 28.0f, -6.0f, 8.0f));
	LOriented(F, H.Roof, FVector(DoorX, -14.0f, PlinthHeight + 98.0f), FVector(70.0f, 30.0f, 4.0f), FRotator(0.0f, 0.0f, -18.0f));

	// Steep storybook roof.
	const float Overhang = 14.0f;
	const bool bRidgeX = (H.Features & HF_GableFront) == 0;
	const float RoofH = bRidgeX ? (D + 2.0f * TopJetty) * 0.62f : W * 0.85f;
	LRoof(F, H.Roof, FVector(-HalfW - Overhang, -TopJetty - Overhang, EaveZ), FVector(HalfW + Overhang, D + TopJetty + Overhang, EaveZ + RoofH), bRidgeX, 10);
	if (!bRidgeX)
	{
		// Gable end facing the street: plaster triangle stand-in and a round window.
		LRoof(F, H.Wall, FVector(-HalfW, -TopJetty, EaveZ), FVector(HalfW, -TopJetty + 4.0f, EaveZ + RoofH - 10.0f), false, 8);
		LBox(F, M::Screen, FVector(-12.0f, -TopJetty - 2.0f, EaveZ + RoofH * 0.3f), FVector(12.0f, -TopJetty, EaveZ + RoofH * 0.3f + 24.0f));
	}

	if ((H.Features & HF_Dormer) && bRidgeX)
	{
		const float DX = R.Chance(0.5f) ? -HalfW * 0.45f : HalfW * 0.45f;
		const float Y0 = -TopJetty + D * 0.08f;
		const float Y1 = Y0 + D * 0.3f;
		LBox(F, H.Wall, FVector(DX - 26.0f, Y0, EaveZ), FVector(DX + 26.0f, Y1, EaveZ + RoofH * 0.5f));
		LBox(F, M::Screen, FVector(DX - 14.0f, Y0 - 2.0f, EaveZ + 12.0f), FVector(DX + 14.0f, Y0, EaveZ + RoofH * 0.5f - 8.0f));
		LRoof(F, H.Roof, FVector(DX - 34.0f, Y0 - 8.0f, EaveZ + RoofH * 0.5f), FVector(DX + 34.0f, Y1 + 20.0f, EaveZ + RoofH * 0.5f + 34.0f), false, 5);
	}

	if (H.Features & HF_Chimney)
	{
		const float CX = R.Chance(0.5f) ? -HalfW * 0.6f : HalfW * 0.6f;
		const float CH = RoofH * 0.75f + 56.0f;
		LOriented(F, M::Brick, FVector(CX, D * 0.68f, EaveZ + RoofH * 0.3f + CH * 0.5f), FVector(26.0f, 26.0f, CH), FRotator(0.0f, 0.0f, R.Range(-6.0f, 6.0f)));
		LOriented(F, M::Concrete, FVector(CX, D * 0.68f, EaveZ + RoofH * 0.3f + CH + 3.0f), FVector(34.0f, 34.0f, 6.0f), FRotator(0.0f, 0.0f, R.Range(-6.0f, 6.0f)));
	}

	if (H.Features & HF_Tower)
	{
		// Round corner turret with a witch's-hat roof.
		const float TX = (R.Chance(0.5f) ? -1.0f : 1.0f) * (HalfW - 6.0f);
		const FVector Base(TX, 18.0f, 0.0f);
		const float TowerTop = EaveZ + RoofH * 0.55f;
		LCylinder(F, H.Wall, Base, 44.0f, TowerTop);
		LBox(F, M::Screen, FVector(TX - 10.0f, 18.0f - 46.0f, TowerTop - 70.0f), FVector(TX + 10.0f, 18.0f - 42.0f, TowerTop - 30.0f));
		LCylinder(F, M::Wood, Base + FVector(0.0f, 0.0f, TowerTop), 52.0f, 6.0f);
		float ConeZ = TowerTop + 6.0f;
		for (int32 Ring = 0; Ring < 6; ++Ring)
		{
			const float RR = 52.0f - Ring * 9.0f;
			LCylinder(F, H.Roof, Base + FVector(0.0f, 0.0f, ConeZ), RR, 18.0f);
			ConeZ += 18.0f;
		}
		LCylinder(F, M::Metal, Base + FVector(0.0f, 0.0f, ConeZ), 2.0f, 30.0f);
	}

	if ((H.Features & HF_Balcony) && H.Storeys > 1)
	{
		const float BZ = PlinthHeight + StoreyHeight;
		const float Side = R.Chance(0.5f) ? 1.0f : -1.0f;
		const float X0 = Side > 0.0f ? HalfW : -HalfW - 52.0f;
		const float X1 = X0 + 52.0f;
		LBox(F, M::Wood, FVector(X0, D * 0.25f, BZ), FVector(X1, D * 0.75f, BZ + 6.0f));
		const float RailX0 = Side > 0.0f ? X1 - 4.0f : X0;
		LBox(F, M::Wood, FVector(RailX0, D * 0.25f, BZ + 6.0f), FVector(RailX0 + 4.0f, D * 0.75f, BZ + 40.0f));
		LOriented(F, M::Wood, FVector((X0 + X1) * 0.5f, D * 0.3f, BZ - 24.0f), FVector(4.0f, 4.0f, 64.0f), FRotator(Side * -40.0f, 0.0f, 0.0f));
		LBox(F, M::Foliage, FVector(RailX0 - 6.0f * Side, D * 0.3f, BZ + 6.0f), FVector(RailX0 + 4.0f, D * 0.42f, BZ + 22.0f));
	}

	if (H.Features & HF_Stacked)
	{
		// Scrap-built shack perched over the roof on stilts, with a ladder and a laundry line.
		const float SX = -HalfW * 0.25f;
		const float SY = D * 0.3f;
		const float SZ = EaveZ + RoofH * 0.55f;
		const float Yaw = R.Range(-9.0f, 9.0f);
		for (int32 Leg = 0; Leg < 4; ++Leg)
		{
			const float LX = SX + ((Leg & 1) ? 60.0f : -60.0f);
			const float LY = SY + ((Leg & 2) ? 110.0f : 10.0f);
			LBox(F, M::Wood, FVector(LX - 3.0f, LY - 3.0f, EaveZ), FVector(LX + 3.0f, LY + 3.0f, SZ));
		}
		LOriented(F, M::Wood, FVector(SX, SY + 60.0f, SZ + 3.0f), FVector(150.0f, 130.0f, 6.0f), FRotator(0.0f, Yaw, 0.0f));
		LOriented(F, R.Chance(0.5f) ? M::PlasterWarm : M::Metal, FVector(SX, SY + 60.0f, SZ + 50.0f), FVector(124.0f, 100.0f, 88.0f), FRotator(0.0f, Yaw, 0.0f));
		LOriented(F, M::Screen, FVector(SX, SY + 9.0f, SZ + 54.0f), FVector(30.0f, 2.0f, 30.0f), FRotator(0.0f, Yaw, 0.0f));
		LOriented(F, M::RoofTeal, FVector(SX, SY + 60.0f, SZ + 104.0f), FVector(150.0f, 132.0f, 6.0f), FRotator(0.0f, Yaw, 10.0f));
		LOriented(F, M::Metal, FVector(SX + 40.0f, SY + 90.0f, SZ + 124.0f), FVector(8.0f, 8.0f, 40.0f), FRotator(0.0f, Yaw, 0.0f));
		LOriented(F, M::Wood, FVector(HalfW + 10.0f, D * 0.5f, EaveZ * 0.5f + SZ * 0.5f), FVector(4.0f, 24.0f, SZ + 10.0f), FRotator(0.0f, 0.0f, 8.0f));
		LBox(F, M::Wood, FVector(SX + 60.0f, SY + 110.0f, SZ + 60.0f), FVector(HalfW * 0.6f, SY + 112.0f, SZ + 62.0f));
		LBox(F, M::Trim, FVector(SX + 80.0f, SY + 109.0f, SZ + 36.0f), FVector(SX + 100.0f, SY + 113.0f, SZ + 60.0f));
		LBox(F, M::RoofRed, FVector(SX + 110.0f, SY + 109.0f, SZ + 42.0f), FVector(SX + 126.0f, SY + 113.0f, SZ + 60.0f));
	}

	if (H.Features & HF_Rubble)
	{
		// Built into a collapsed wall: mound on one flank, a fallen slab leaning on the other.
		const float Side = R.Chance(0.5f) ? 1.0f : -1.0f;
		const FVector Mound = F.ToWorld(FVector(Side * (HalfW + 50.0f), D * 0.55f, 0.0f));
		RubblePile(Mound, 110.0f, 150.0f, H.Seed + 7u);
		LOriented(F, M::Concrete, FVector(-Side * (HalfW + 36.0f), D * 0.5f, 92.0f), FVector(24.0f, D * 0.8f, 200.0f), FRotator(-Side * 16.0f, 0.0f, 0.0f));
		LBox(F, M::Brick, FVector(-HalfW - 4.0f, D - 40.0f, 0.0f), FVector(-HalfW + W * 0.6f, D + 30.0f, EaveZ + 40.0f));
		LBox(F, M::Vines, FVector(-HalfW - 6.0f, D - 42.0f, EaveZ - 60.0f), FVector(-HalfW + W * 0.4f, D + 32.0f, EaveZ + 40.0f));
	}

	if (H.Features & HF_Ivy)
	{
		const float IX = R.Range(-HalfW, HalfW * 0.2f);
		LBox(F, M::Vines, FVector(IX, -2.5f, PlinthHeight), FVector(IX + W * 0.4f, -1.0f, EaveZ - 12.0f));
		LBox(F, M::Vines, FVector(HalfW + 1.0f, D * 0.1f, PlinthHeight + 30.0f), FVector(HalfW + 3.0f, D * 0.8f, EaveZ));
		LBox(F, M::Vines, FVector(-HalfW - 3.0f, D * 0.4f, PlinthHeight), FVector(-HalfW - 1.0f, D, EaveZ * 0.7f));
	}

	// Front garden: picket fence with a gate gap, shrubs, and a tree out back.
	if (H.Features & HF_Fence)
	{
		for (float X = -HalfW - 30.0f; X < HalfW + 30.0f; X += 12.0f)
		{
			if (FMath::Abs(X - DoorX) < 30.0f)
			{
				continue;
			}
			LBox(F, M::Trim, FVector(X, -34.0f, 0.0f), FVector(X + 4.0f, -31.0f, 24.0f));
		}
		LBox(F, M::Trim, FVector(-HalfW - 30.0f, -35.0f, 14.0f), FVector(DoorX - 30.0f, -30.0f, 18.0f));
		LBox(F, M::Trim, FVector(DoorX + 30.0f, -35.0f, 14.0f), FVector(HalfW + 30.0f, -30.0f, 18.0f));
	}
	Shrub(F.ToWorld(FVector(-HalfW - 16.0f, -12.0f, 0.0f)), R.Range(36.0f, 56.0f), H.Seed + 11u);
	Shrub(F.ToWorld(FVector(HalfW + 16.0f, -12.0f, 0.0f)), R.Range(30.0f, 50.0f), H.Seed + 13u);
	const float TreeX = R.Range(-HalfW, HalfW);
	const float TreeY = D + (YardFeature == 3 ? 90.0f : 0.0f) + R.Range(80.0f, 140.0f);
	const float TreeScale = R.Range(0.9f, 1.4f);
	const FVector TreeBase = F.ToWorld(FVector(TreeX, TreeY, 0.0f));
	if (!InsideAny(FVector2D(TreeBase.X, TreeBase.Y), PavedAreas))
	{
		Tree(TreeBase, TreeScale, H.Seed + 17u);
	}

	// Mailbox by the gate, painted to match the door, with a flag.
	{
		const float MX = DoorX >= 0.0f ? DoorX + 52.0f : DoorX - 52.0f;
		LBox(F, M::Wood, FVector(MX - 2.0f, -28.0f, 0.0f), FVector(MX + 2.0f, -24.0f, 44.0f));
		LBox(F, DoorPaint, FVector(MX - 8.0f, -32.0f, 44.0f), FVector(MX + 8.0f, -18.0f, 58.0f));
		LBox(F, DoorPaint == M::PaintRed ? M::PaintYellow : M::PaintRed, FVector(MX + 8.0f, -27.0f, 50.0f), FVector(MX + 10.0f, -25.0f, 68.0f));
	}

	// Yard feature.
	const bool bTower = (H.Features & HF_Tower) != 0;
	switch (YardFeature)
	{
	case 1:
		if (!bTower)
		{
			// Bay window on the side away from the mailbox.
			const float BX = DoorX >= 0.0f ? -HalfW * 0.5f : HalfW * 0.5f;
			LBox(F, H.Wall, FVector(BX - 40.0f, -18.0f, PlinthHeight), FVector(BX + 40.0f, 0.0f, PlinthHeight + 96.0f));
			LBox(F, M::Screen, FVector(BX - 30.0f, -20.0f, PlinthHeight + 36.0f), FVector(BX + 30.0f, -18.0f, PlinthHeight + 84.0f));
			LBox(F, H.Roof, FVector(BX - 46.0f, -24.0f, PlinthHeight + 96.0f), FVector(BX + 46.0f, 2.0f, PlinthHeight + 104.0f));
		}
		break;
	case 2:
		if (!bTower)
		{
			// Front porch with posts, a roof and a bench.
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				LBox(F, M::Wood, FVector(DoorX + Side * 40.0f - 2.0f, -28.0f, 0.0f), FVector(DoorX + Side * 40.0f + 2.0f, -24.0f, PlinthHeight + 106.0f));
			}
			LOriented(F, H.Roof, FVector(DoorX, -14.0f, PlinthHeight + 110.0f), FVector(100.0f, 36.0f, 4.0f), FRotator(0.0f, 0.0f, -12.0f));
			LBox(F, M::Wood, FVector(DoorX - 38.0f, -24.0f, 0.0f), FVector(DoorX - 24.0f, -12.0f, 16.0f));
		}
		break;
	case 3:
	{
		// Lean-to shed against the back wall.
		const float X0 = -HalfW + 20.0f;
		const float X1 = X0 + W * 0.45f;
		LBox(F, M::TimberDark, FVector(X0, D, 0.0f), FVector(X1, D + 70.0f, 96.0f));
		LBox(F, DoorPaint, FVector(X0 + 12.0f, D + 70.0f, 0.0f), FVector(X0 + 44.0f, D + 72.0f, 72.0f));
		LOriented(F, H.Roof, FVector((X0 + X1) * 0.5f, D + 36.0f, 112.0f), FVector(X1 - X0 + 16.0f, 86.0f, 4.0f), FRotator(-20.0f, 90.0f, 0.0f));
		break;
	}
	case 4:
		if ((H.Features & HF_Rubble) == 0)
		{
			// Rain barrels and a woodpile down one side.
			const float SX = DoorX >= 0.0f ? -HalfW - 18.0f : HalfW + 18.0f;
			LCylinder(F, M::Wood, FVector(SX, D * 0.55f, 0.0f), 10.0f, 30.0f);
			LCylinder(F, ShutterPaint, FVector(SX, D * 0.55f + 24.0f, 0.0f), 10.0f, 26.0f);
			LBox(F, M::TimberDark, FVector(SX - 9.0f, D * 0.75f, 0.0f), FVector(SX + 9.0f, D * 0.95f, 24.0f));
		}
		break;
	case 5:
		// Rose arch over the gate.
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			LBox(F, M::Trim, FVector(DoorX + Side * 33.0f - 3.0f, -38.0f, 0.0f), FVector(DoorX + Side * 33.0f + 3.0f, -30.0f, 92.0f));
		}
		LBox(F, M::Trim, FVector(DoorX - 38.0f, -38.0f, 92.0f), FVector(DoorX + 38.0f, -30.0f, 98.0f));
		LBox(F, M::Foliage, FVector(DoorX - 40.0f, -40.0f, 96.0f), FVector(DoorX + 40.0f, -28.0f, 108.0f));
		LBox(F, FlagPaint, FVector(DoorX - 30.0f, -41.0f, 100.0f), FVector(DoorX - 22.0f, -39.0f, 106.0f));
		LBox(F, FlagPaint, FVector(DoorX + 14.0f, -41.0f, 98.0f), FVector(DoorX + 22.0f, -39.0f, 104.0f));
		break;
	default:
		break;
	}

	// Rooftop ornament on the ridge, readable from a distance.
	{
		const float OX = bRidgeX ? ((H.Features & HF_Stacked) ? HalfW * 0.4f : -HalfW * 0.35f) : 0.0f;
		const float OY = bRidgeX ? D * 0.5f : D * 0.6f;
		const FVector O(OX, OY, EaveZ + RoofH - 4.0f);
		switch (Ornament)
		{
		case 0: // Weathervane cockerel.
			LBox(F, M::Metal, O + FVector(-1.5f, -1.5f, 0.0f), O + FVector(1.5f, 1.5f, 70.0f));
			LBox(F, M::Metal, O + FVector(-22.0f, -1.0f, 50.0f), O + FVector(22.0f, 1.0f, 52.0f));
			LBox(F, M::PaintRed, O + FVector(-8.0f, -2.0f, 70.0f), O + FVector(8.0f, 2.0f, 84.0f));
			break;
		case 1: // Pennant.
			LBox(F, M::Wood, O + FVector(-1.5f, -1.5f, 0.0f), O + FVector(1.5f, 1.5f, 110.0f));
			LBox(F, FlagPaint, O + FVector(1.5f, -1.0f, 80.0f), O + FVector(50.0f, 1.0f, 108.0f));
			break;
		case 2: // Bell cupola.
			for (int32 Post = 0; Post < 4; ++Post)
			{
				const FVector P = O + FVector((Post & 1) ? 16.0f : -16.0f, (Post & 2) ? 16.0f : -16.0f, 0.0f);
				LBox(F, M::Wood, P - FVector(2.0f, 2.0f, 0.0f), P + FVector(2.0f, 2.0f, 44.0f));
			}
			LCylinder(F, M::Metal, O + FVector(0.0f, 0.0f, 18.0f), 9.0f, 18.0f);
			LCylinder(F, H.Roof, O + FVector(0.0f, 0.0f, 44.0f), 26.0f, 8.0f);
			LCylinder(F, H.Roof, O + FVector(0.0f, 0.0f, 52.0f), 16.0f, 10.0f);
			LCylinder(F, H.Roof, O + FVector(0.0f, 0.0f, 62.0f), 6.0f, 12.0f);
			break;
		case 3: // Pinwheel.
			LBox(F, M::Wood, O + FVector(-1.5f, -1.5f, 0.0f), O + FVector(1.5f, 1.5f, 80.0f));
			for (int32 Blade = 0; Blade < 4; ++Blade)
			{
				LOriented(F, Blade & 1 ? FlagPaint : ShutterPaint, O + FVector(0.0f, -4.0f, 80.0f), FVector(60.0f, 2.0f, 10.0f), FRotator(Blade * 45.0f, 0.0f, 0.0f));
			}
			break;
		case 4: // Lantern finial.
			LBox(F, M::Wood, O + FVector(-2.0f, -2.0f, 0.0f), O + FVector(2.0f, 2.0f, 40.0f));
			LBox(F, M::Screen, O + FVector(-9.0f, -9.0f, 40.0f), O + FVector(9.0f, 9.0f, 64.0f));
			LBox(F, M::RoofRed, O + FVector(-13.0f, -13.0f, 64.0f), O + FVector(13.0f, 13.0f, 70.0f));
			break;
		case 5: // Birdhouse on a tall pole.
			LBox(F, M::Wood, O + FVector(-1.5f, -1.5f, 0.0f), O + FVector(1.5f, 1.5f, 120.0f));
			LBox(F, FlagPaint, O + FVector(-10.0f, -10.0f, 120.0f), O + FVector(10.0f, 10.0f, 140.0f));
			LRoof(F, H.Roof, O + FVector(-14.0f, -14.0f, 140.0f), O + FVector(14.0f, 14.0f, 154.0f), true, 3);
			break;
		case 6: // Little observatory dome.
			LCylinder(F, M::Metal, O, 30.0f, 22.0f);
			LCylinder(F, M::Trim, O + FVector(0.0f, 0.0f, 22.0f), 24.0f, 10.0f);
			LCylinder(F, M::Trim, O + FVector(0.0f, 0.0f, 32.0f), 14.0f, 8.0f);
			LOriented(F, M::Metal, O + FVector(0.0f, -18.0f, 34.0f), FVector(6.0f, 40.0f, 6.0f), FRotator(0.0f, 0.0f, 30.0f));
			break;
		default: // Radio mast.
			LBox(F, M::Metal, O + FVector(-1.5f, -1.5f, 0.0f), O + FVector(1.5f, 1.5f, 160.0f));
			for (int32 Bar = 0; Bar < 3; ++Bar)
			{
				const float BZ = 90.0f + Bar * 24.0f;
				const float BW = 30.0f - Bar * 8.0f;
				LBox(F, M::Metal, O + FVector(-BW, -1.0f, BZ), O + FVector(BW, 1.0f, BZ + 2.0f));
			}
			LBox(F, M::PaintRed, O + FVector(-3.0f, -3.0f, 160.0f), O + FVector(3.0f, 3.0f, 166.0f));
			break;
		}
	}
}

FBox2D AHL2NeighborhoodsBlockout::HouseFootprint(const FHouse& H, int32 Id, bool bWithYard) const
{
	// Walls and jetties; the yard version adds the back lean-to when this address gets one.
	const FFrame F{ H.FrontCenter, H.Facing, H.BaseZ };
	const float HalfW = H.Width * 0.5f;
	const float Back = H.Depth + (bWithYard && (Id * 7 + 2) % 6 == 3 ? 78.0f : 8.0f);
	const FVector A = F.ToWorld(FVector(-HalfW, -8.0f, 0.0f));
	const FVector B = F.ToWorld(FVector(HalfW, Back, 0.0f));
	return FBox2D(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
}

void AHL2NeighborhoodsBlockout::HouseRow(const FVector2D& FirstFront, const FVector2D& Step, int32 Count, int32 Facing, float MaxDepth,
	uint32 ExtraFeatures, uint32 Seed, const TArray<FBox2D>& Avoid)
{
	using M = EHL2BlockoutMaterial;
	const M Walls[] = { M::Plaster, M::PlasterBlue, M::PlasterWarm, M::PlasterSage, M::Brick, M::PlasterRose, M::TimberDark, M::PlasterOchre };
	const M Roofs[] = { M::RoofRed, M::RoofSlate, M::RoofTeal, M::RoofOchre, M::RoofMoss };
	const uint32 Shapes[] = { HF_Dormer, HF_Tower, HF_GableFront, HF_Dormer | HF_Balcony, HF_Stacked, HF_GableFront | HF_Balcony, HF_Tower | HF_Dormer, HF_None };
	const float Spacing = FMath::Max(FMath::Abs(Step.X), FMath::Abs(Step.Y));
	FRand R(Seed);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 Id = HouseCount;
		FHouse H;
		H.FrontCenter = FirstFront + FVector2D(Step.X * Index, Step.Y * Index);
		H.Facing = Facing;
		H.Width = FMath::Min(R.Range(240.0f, 300.0f), Spacing - 60.0f);
		H.Depth = R.Range(MaxDepth - 60.0f, MaxDepth);
		H.Storeys = 1 + static_cast<int32>(R.Range(0.0f, 2.99f));
		H.Wall = Walls[(Id * 3) % 8];
		H.Roof = Roofs[(Id * 2 + 1) % 5];
		H.Features = Shapes[(Id * 5 + Index) % 8] | HF_Fence | ExtraFeatures | (R.Chance(0.5f) ? HF_Chimney : HF_None) | (R.Chance(0.3f) ? HF_Ivy : HF_None);
		H.Seed = Seed * 7919u + static_cast<uint32>(Index) * 104729u;

		const FBox2D Plot = HouseFootprint(H, Id, true);
		if (OverlapsAny(Plot, HouseFootprints, 30.0f) || OverlapsAny(Plot, PavedAreas, 0.0f) || OverlapsAny(Plot, Avoid, 0.0f))
		{
			continue;
		}
		House(H);
	}
}

void AHL2NeighborhoodsBlockout::TownBlock(const FFrame& F, float X0, float X1, uint32 Seed, bool bWorkshop)
{
	using M = EHL2BlockoutMaterial;
	FRand R(Seed);
	constexpr float Depth = 160.0f;
	const float Height = bWorkshop ? 320.0f : R.Range(176.0f, 300.0f);
	const M Walls[] = { M::Plaster, M::PlasterWarm, M::Brick, M::Plaster };
	const M Wall = bWorkshop ? M::Brick : Walls[static_cast<int32>(R.Range(0.0f, 3.99f))];
	const M Roof = R.Chance(0.5f) ? M::RoofRed : M::RoofTeal;

	LBox(F, Wall, FVector(X0, 0.0f, 0.0f), FVector(X1, Depth, Height));
	LRoof(F, Roof, FVector(X0 - 4.0f, -10.0f, Height), FVector(X1 + 4.0f, Depth + 10.0f, Height + 76.0f), true, 7);

	// Shopfront: timber frame, glowing glass, striped awning.
	const float Mid = (X0 + X1) * 0.5f;
	const float Half = (X1 - X0) * 0.5f - 18.0f;
	LBox(F, M::Wood, FVector(Mid - Half - 4.0f, -4.0f, 0.0f), FVector(Mid + Half + 4.0f, 0.0f, 96.0f));
	LBox(F, M::Screen, FVector(Mid - Half + 6.0f, -6.0f, 24.0f), FVector(Mid + Half - 6.0f, -4.0f, 86.0f));
	LOriented(F, R.Chance(0.5f) ? M::RoofRed : M::Trim, FVector(Mid, -26.0f, 108.0f), FVector(Half * 2.0f + 12.0f, 48.0f, 4.0f), FRotator(0.0f, 0.0f, -16.0f));

	// Upper windows.
	for (float WZ = 128.0f; WZ + 50.0f < Height; WZ += 96.0f)
	{
		LBox(F, M::Screen, FVector(Mid - 16.0f, -2.0f, WZ), FVector(Mid + 16.0f, 0.0f, WZ + 46.0f));
		LBox(F, M::Wood, FVector(Mid - 20.0f, -8.0f, WZ - 6.0f), FVector(Mid + 20.0f, 0.0f, WZ));
	}

	if (bWorkshop)
	{
		// Tall workshop chimney and a big cog sign above the door.
		LBox(F, M::Brick, FVector(X1 - 50.0f, Depth - 60.0f, Height), FVector(X1 - 14.0f, Depth - 24.0f, Height + 200.0f));
		LBox(F, M::Metal, FVector(X1 - 54.0f, Depth - 64.0f, Height + 200.0f), FVector(X1 - 10.0f, Depth - 20.0f, Height + 210.0f));
		for (int32 Tooth = 0; Tooth < 6; ++Tooth)
		{
			LOriented(F, M::Metal, FVector(Mid, -8.0f, 150.0f), FVector(70.0f, 4.0f, 14.0f), FRotator(Tooth * 30.0f, 0.0f, 0.0f));
		}
	}
	else if (R.Chance(0.4f))
	{
		LBox(F, M::Brick, FVector(X0 + 20.0f, Depth * 0.6f, Height), FVector(X0 + 44.0f, Depth * 0.6f + 24.0f, Height + 100.0f));
	}
	if (R.Chance(0.5f))
	{
		LBox(F, M::Vines, FVector(X0 + 4.0f, -2.0f, Height * 0.4f), FVector(X0 + 40.0f, -1.0f, Height));
	}
	if (R.Chance(0.6f))
	{
		Shrub(F.ToWorld(FVector(X0 + 20.0f, -20.0f, 0.0f)), 30.0f, Seed + 3u);
	}
}

void AHL2NeighborhoodsBlockout::TownRow(const FFrame& F, float X0, float X1, const TArray<FVector2D>& Openings, uint32 Seed)
{
	TArray<float> Cuts = { X0, X1 };
	for (const FVector2D& Opening : Openings)
	{
		Cuts.Add(FMath::Clamp(static_cast<float>(Opening.X), X0, X1));
		Cuts.Add(FMath::Clamp(static_cast<float>(Opening.Y), X0, X1));
	}
	Cuts.Sort();

	FRand R(Seed);
	for (int32 Index = 0; Index + 1 < Cuts.Num(); ++Index)
	{
		const float A = Cuts[Index];
		const float B = Cuts[Index + 1];
		const float Mid = (A + B) * 0.5f;
		bool bOpen = B - A < 1.0f;
		for (const FVector2D& Opening : Openings)
		{
			bOpen |= Mid > Opening.X && Mid < Opening.Y;
		}
		if (bOpen)
		{
			continue;
		}
		const int32 Modules = FMath::Max(1, static_cast<int32>((B - A) / 170.0f));
		for (int32 Module = 0; Module < Modules; ++Module)
		{
			const float M0 = A + (B - A) * Module / Modules;
			const float M1 = A + (B - A) * (Module + 1) / Modules;
			TownBlock(F, M0, M1, static_cast<uint32>(R.Range(0.0f, 1.0e6f)), (Seed == 1u) && Index == 0 && Module == 0);
		}
	}
}

void AHL2NeighborhoodsBlockout::Scatter(const FVector2D& Min, const FVector2D& Max, float Spacing, float TreeChance, const TArray<FBox2D>& Avoid, uint32 Seed, float BaseZ)
{
	ScatterJobs.Add(FScatterJob{ Min, Max, Spacing, TreeChance, Avoid, Seed, BaseZ });
}

void AHL2NeighborhoodsBlockout::RunScatter(const FScatterJob& Job)
{
	using M = EHL2BlockoutMaterial;
	FRand R(Job.Seed);
	const float Spacing = Job.Spacing;

	TArray<FBox2D> Keep;
	for (const FBox2D& Footprint : HouseFootprints)
	{
		Keep.Add(Expand(Footprint, 50.0f));
	}
	for (const FBox2D& Paved : PavedAreas)
	{
		Keep.Add(Expand(Paved, 30.0f));
	}

	for (float Y = Job.Min.Y + Spacing * 0.5f; Y < Job.Max.Y; Y += Spacing)
	{
		for (float X = Job.Min.X + Spacing * 0.5f; X < Job.Max.X; X += Spacing)
		{
			const FVector2D P(X + R.Range(-0.4f, 0.4f) * Spacing, Y + R.Range(-0.4f, 0.4f) * Spacing);
			const float Roll = R.Next();
			if (P.X < Job.Min.X || P.X > Job.Max.X || P.Y < Job.Min.Y || P.Y > Job.Max.Y || InsideAny(P, Job.Avoid) || InsideAny(P, Keep))
			{
				continue;
			}
			const FVector Base(P.X, P.Y, Job.BaseZ + TerrainHeightAt(P));
			if (Roll < Job.TreeChance)
			{
				Tree(Base, R.Range(0.8f, 1.5f), R.State);
			}
			else if (Roll < Job.TreeChance + (1.0f - Job.TreeChance) * 0.55f)
			{
				Shrub(Base, R.Range(30.0f, 70.0f), R.State);
			}
			else
			{
				Box(Roll < 0.5f ? M::Grass : M::Foliage, Base - FVector(14.0f, 10.0f, 0.0f), Base + FVector(14.0f, 10.0f, R.Range(6.0f, 14.0f)));
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Districts
// ---------------------------------------------------------------------------

void AHL2NeighborhoodsBlockout::BuildGround()
{
	using M = EHL2BlockoutMaterial;
	constexpr float Edge = MapHalf + 200.0f;

	// The canal cuts through the ground; a crawl nook (H4) hides in its north quay wall.
	SlabWithHole(M::Grass, FVector(-Edge, CanalY1, GroundBottomZ), FVector(Edge, Edge, 0.0f), FBox2D(FVector2D(-480.0f, -4600.0f), FVector2D(-240.0f, -4400.0f)));
	Box(M::Grass, FVector(-Edge, -Edge, GroundBottomZ), FVector(Edge, CanalY0, 0.0f));
	Box(M::Dirt, FVector(-Edge, CanalY0, GroundBottomZ), FVector(Edge, CanalY1, CanalBedZ));
	Box(M::Water, FVector(-Edge, CanalY0, -72.0f), FVector(Edge, CanalY1, -64.0f));
	MarkFlatXY(FVector2D(-Edge, CanalY0), FVector2D(Edge, CanalY1));
}

void AHL2NeighborhoodsBlockout::BuildPerimeter()
{
	constexpr float Edge = MapHalf + 200.0f;
	Ridge(FVector2D(-Edge, MapHalf), FVector2D(Edge, Edge), 640.0f, 2);
	Ridge(FVector2D(-Edge, -Edge), FVector2D(Edge, -MapHalf), 640.0f, 2);
	Ridge(FVector2D(MapHalf, -MapHalf), FVector2D(Edge, MapHalf), 640.0f, 2);
	Ridge(FVector2D(-Edge, -MapHalf), FVector2D(-MapHalf, MapHalf), 640.0f, 2);
}

void AHL2NeighborhoodsBlockout::BuildHub()
{
	using M = EHL2BlockoutMaterial;

	// Market square. Openings: Lantern Road (N), Tram Steps (NE), Rubble Row (E),
	// Canal Walk (S), Windmill Path (SW), Grove Path (W).
	Box(M::Tile, FVector(-800.0f, -800.0f, 0.0f), FVector(800.0f, 800.0f, 4.0f));
	TownRow(FFrame{ FVector2D(0.0f, 800.0f), 0, 0.0f }, -960.0f, 960.0f, { FVector2D(-224.0f, 224.0f) }, 1u);
	TownRow(FFrame{ FVector2D(0.0f, -800.0f), 2, 0.0f }, -960.0f, 960.0f, { FVector2D(-224.0f, 224.0f), FVector2D(464.0f, 656.0f) }, 2u);
	TownRow(FFrame{ FVector2D(800.0f, 0.0f), 3, 0.0f }, -800.0f, 800.0f, { FVector2D(-224.0f, 224.0f), FVector2D(-656.0f, -464.0f) }, 3u);
	TownRow(FFrame{ FVector2D(-800.0f, 0.0f), 1, 0.0f }, -800.0f, 800.0f, { FVector2D(-112.0f, 112.0f) }, 4u);

	// The great camphor tree on its raised bed.
	Cylinder(M::Concrete, FVector(0.0f, 0.0f, 4.0f), 232.0f, 12.0f);
	Cylinder(M::Grass, FVector(0.0f, 0.0f, 4.0f), 216.0f, 16.0f);
	Cylinder(M::Wood, FVector(0.0f, 0.0f, 20.0f), 58.0f, 500.0f);
	FRand R(77u);
	for (int32 RootIndex = 0; RootIndex < 7; ++RootIndex)
	{
		const float Yaw = RootIndex * 360.0f / 7.0f + R.Range(-10.0f, 10.0f);
		const FVector Dir(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0.0f);
		OrientedBox(M::Wood, Dir * 90.0f + FVector(0.0f, 0.0f, 34.0f), FVector(120.0f, 26.0f, 30.0f), FRotator(-22.0f, Yaw, 0.0f));
	}
	for (int32 Branch = 0; Branch < 6; ++Branch)
	{
		const float Yaw = Branch * 60.0f + R.Range(-15.0f, 15.0f);
		const FVector Dir(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0.0f);
		const float Z = R.Range(330.0f, 450.0f);
		OrientedBox(M::Wood, Dir * 120.0f + FVector(0.0f, 0.0f, Z), FVector(260.0f, 22.0f, 22.0f), FRotator(28.0f, Yaw, 0.0f));
		// Lanterns hanging from the boughs.
		Box(M::Wood, Dir * 170.0f + FVector(-1.0f, -1.0f, Z - 60.0f), Dir * 170.0f + FVector(1.0f, 1.0f, Z + 40.0f));
		Box(M::Screen, Dir * 170.0f + FVector(-7.0f, -7.0f, Z - 76.0f), Dir * 170.0f + FVector(7.0f, 7.0f, Z - 58.0f));
	}
	for (int32 Puff = 0; Puff < 14; ++Puff)
	{
		const float A = R.Range(0.0f, 2.0f * UE_PI);
		const float D = Puff == 0 ? 0.0f : R.Range(80.0f, 300.0f);
		Cylinder(R.Chance(0.6f) ? M::Foliage : M::FoliageDark, FVector(FMath::Cos(A) * D, FMath::Sin(A) * D, R.Range(470.0f, 600.0f)), R.Range(110.0f, 190.0f), R.Range(70.0f, 120.0f));
	}

	// Lantern ring and benches around the tree.
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const float A = (Index + 0.5f) * UE_PI / 4.0f;
		Lantern(FVector(FMath::Cos(A) * 320.0f, FMath::Sin(A) * 320.0f, 4.0f));
		const float BA = Index * UE_PI / 4.0f;
		OrientedBox(M::Wood, FVector(FMath::Cos(BA) * 262.0f, FMath::Sin(BA) * 262.0f, 14.0f), FVector(18.0f, 72.0f, 4.0f), FRotator(0.0f, FMath::RadiansToDegrees(BA), 0.0f));
	}

	// Market stalls.
	const FVector2D Stalls[] = { FVector2D(-520.0f, -420.0f), FVector2D(520.0f, -360.0f), FVector2D(-520.0f, 420.0f), FVector2D(470.0f, 380.0f) };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector C(Stalls[Index].X, Stalls[Index].Y, 4.0f);
		for (int32 Post = 0; Post < 4; ++Post)
		{
			const FVector P = C + FVector((Post & 1) ? 70.0f : -70.0f, (Post & 2) ? 44.0f : -44.0f, 0.0f);
			Box(M::Wood, P - FVector(3.0f, 3.0f, 0.0f), P + FVector(3.0f, 3.0f, 96.0f));
		}
		Box(M::Wood, C + FVector(-66.0f, -40.0f, 0.0f), C + FVector(66.0f, -10.0f, 40.0f));
		Box(M::Screen, C + FVector(-60.0f, -36.0f, 40.0f), C + FVector(-20.0f, -14.0f, 52.0f));
		Box(M::Foliage, C + FVector(10.0f, -36.0f, 40.0f), C + FVector(50.0f, -14.0f, 50.0f));
		OrientedBox((Index & 1) ? M::RoofTeal : M::RoofRed, C + FVector(0.0f, 0.0f, 104.0f), FVector(170.0f, 120.0f, 4.0f), FRotator(0.0f, 0.0f, 12.0f));
		Box(M::Wood, C + FVector(30.0f, 12.0f, 0.0f), C + FVector(62.0f, 40.0f, 28.0f));
	}

	// Old well.
	Cylinder(M::Concrete, FVector(-300.0f, 560.0f, 4.0f), 40.0f, 32.0f);
	Cylinder(M::Water, FVector(-300.0f, 560.0f, 30.0f), 30.0f, 4.0f);
	Box(M::Wood, FVector(-344.0f, 556.0f, 4.0f), FVector(-336.0f, 564.0f, 100.0f));
	Box(M::Wood, FVector(-264.0f, 556.0f, 4.0f), FVector(-256.0f, 564.0f, 100.0f));
	GableRoof(M::RoofRed, FVector(-356.0f, 524.0f, 100.0f), FVector(-244.0f, 596.0f, 130.0f), 0, 4);

	// Placeholder spots (geometry only): notice board, workbench, robot dock pad.
	Box(M::Wood, FVector(-380.0f, -720.0f, 4.0f), FVector(-372.0f, -712.0f, 100.0f));
	Box(M::Wood, FVector(-268.0f, -720.0f, 4.0f), FVector(-260.0f, -712.0f, 100.0f));
	Box(M::Marker, FVector(-376.0f, -722.0f, 50.0f), FVector(-264.0f, -716.0f, 96.0f));
	Box(M::Marker, FVector(-900.0f + 160.0f, 700.0f, 4.0f), FVector(-900.0f + 260.0f, 750.0f, 36.0f));
	Cylinder(M::Marker, FVector(620.0f, -640.0f, 4.0f), 56.0f, 3.0f);
	Cylinder(M::Metal, FVector(620.0f, -640.0f, 7.0f), 46.0f, 3.0f);

	// Planters at the openings.
	const FVector2D Planters[] = { FVector2D(-260.0f, 760.0f), FVector2D(260.0f, 760.0f), FVector2D(760.0f, 260.0f), FVector2D(760.0f, -260.0f),
		FVector2D(-260.0f, -760.0f), FVector2D(260.0f, -760.0f), FVector2D(-760.0f, 150.0f), FVector2D(-760.0f, -150.0f) };
	for (const FVector2D& P : Planters)
	{
		Box(M::Concrete, FVector(P.X - 24.0f, P.Y - 24.0f, 4.0f), FVector(P.X + 24.0f, P.Y + 24.0f, 22.0f));
		Shrub(FVector(P.X, P.Y, 22.0f), 40.0f, HashXY(P.X, P.Y));
	}
}

void AHL2NeighborhoodsBlockout::BuildNorthSuburbs()
{
	using M = EHL2BlockoutMaterial;

	// Lantern Road (N-S) with T1 (Maple Lane, west) and T2 (Sparrow Close, east).
	Road(false, 0.0f, 800.0f, 5600.0f, M::DarkConcrete, { FVector2D(3000.0f - 224.0f, 3000.0f + 224.0f), FVector2D(5480.0f - 224.0f, 5480.0f + 224.0f) },
		{ FVector2D(4600.0f - 224.0f, 4600.0f + 224.0f), FVector2D(5480.0f - 224.0f, 5480.0f + 224.0f) });
	Road(true, 3000.0f, -2930.0f, -160.0f, M::DarkConcrete, { FVector2D(-224.0f, -160.0f) }, { FVector2D(-224.0f, -160.0f) });
	Road(true, 4600.0f, 160.0f, 2540.0f, M::DarkConcrete, { FVector2D(160.0f, 224.0f) }, { FVector2D(160.0f, 224.0f) });
	CulDeSac(FVector2D(-3400.0f, 3000.0f), 480.0f, 0.0f);
	CulDeSac(FVector2D(3000.0f, 4600.0f), 480.0f, 180.0f);
	// Bramble Way (west) and Kettle Row (east) cross Lantern Road below the shrine; Thistle Row tees off Bramble Way.
	Road(true, 5480.0f, -5440.0f, -160.0f, M::DarkConcrete, {}, { FVector2D(-560.0f, -160.0f) });
	Road(false, -5600.0f, 1700.0f, 5704.0f, M::DarkConcrete, {}, { FVector2D(5480.0f - 224.0f, 5704.0f) });
	Road(true, 5480.0f, 160.0f, 2560.0f, M::DarkConcrete, {}, {});
	// Kettle Row's dead end: a footpath on to cul-de-sac 2.
	DirtPath(FVector2D(2580.0f, 5100.0f), FVector2D(2700.0f, 5320.0f));
	for (float X = -5000.0f; X < -400.0f; X += 600.0f)
	{
		Lantern(FVector(X, 5480.0f - 200.0f, 6.0f));
	}
	for (float Y = 2000.0f; Y < 5300.0f; Y += 600.0f)
	{
		Lantern(FVector(-5600.0f + 200.0f, Y, 6.0f));
	}
	for (float Y = 1100.0f; Y < 5600.0f; Y += 400.0f)
	{
		Lantern(FVector(-200.0f, Y, 6.0f));
		Lantern(FVector(200.0f, Y + 200.0f, 6.0f));
	}

	const M Walls[] = { M::Plaster, M::PlasterWarm, M::PlasterBlue, M::Brick, M::PlasterRose };
	const M Roofs[] = { M::RoofRed, M::RoofTeal, M::RoofSlate };
	uint32 Seed = 100u;
	auto Add = [&](float X, float Y, int32 Facing, float W, float D, int32 Storeys, uint32 Features)
	{
		++Seed;
		FRand R(Seed);
		FHouse H;
		H.FrontCenter = FVector2D(X, Y);
		H.Facing = Facing;
		H.Width = W;
		H.Depth = D;
		H.Storeys = Storeys;
		H.Wall = Walls[static_cast<int32>(R.Range(0.0f, 4.99f))];
		H.Roof = Roofs[static_cast<int32>(R.Range(0.0f, 2.99f))];
		H.Features = Features | HF_Fence | (R.Chance(0.5f) ? HF_Chimney : HF_None) | (R.Chance(0.35f) ? HF_Ivy : HF_None);
		H.Seed = Seed * 7919u;
		House(H);
	};

	// Maple Lane: tight suburban rows (100 HU between houses).
	const float MapleX[] = { -2500.0f, -2100.0f, -1700.0f, -1300.0f };
	const uint32 MapleNorth[] = { HF_Dormer, HF_Tower | HF_Dormer, HF_GableFront, HF_Dormer | HF_Balcony };
	const uint32 MapleSouth[] = { HF_GableFront, HF_Dormer | HF_Stacked, HF_Dormer, HF_Tower };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Add(MapleX[Index], 3000.0f + RoadHalf + WalkWidth + 48.0f, 0, 300.0f, 320.0f, 2 + (Index & 1), MapleNorth[Index]);
		Add(MapleX[Index], 3000.0f - RoadHalf - WalkWidth - 48.0f, 2, 300.0f, 300.0f, 2 - (Index == 2 ? 1 : 0), MapleSouth[Index]);
	}

	// Lantern Road frontage.
	Add(-SetBack, 1500.0f, 1, 320.0f, 300.0f, 2, HF_Dormer | HF_Balcony);
	Add(-SetBack, 2250.0f, 1, 300.0f, 320.0f, 3, HF_Tower);
	Add(SetBack, 1500.0f, 3, 300.0f, 300.0f, 2, HF_GableFront | HF_Stacked);
	Add(SetBack, 2250.0f, 3, 340.0f, 320.0f, 2, HF_Dormer);

	// Sparrow Close.
	Add(800.0f, 4600.0f + SetBack, 0, 320.0f, 320.0f, 2, HF_Dormer | HF_Tower);
	Add(1300.0f, 4600.0f + SetBack, 0, 300.0f, 300.0f, 1, HF_GableFront);
	Add(800.0f, 4600.0f - SetBack, 2, 300.0f, 300.0f, 2, HF_Balcony | HF_Dormer);
	Add(1300.0f, 4600.0f - SetBack, 2, 320.0f, 320.0f, 3, HF_Stacked);

	// Cul-de-sac 1 (end of Maple Lane).
	Add(-3400.0f, 3592.0f, 0, 320.0f, 320.0f, 2, HF_Tower | HF_Dormer);
	Add(-3850.0f, 3592.0f, 0, 300.0f, 300.0f, 1, HF_GableFront | HF_Stacked);
	Add(-3400.0f, 2408.0f, 2, 320.0f, 300.0f, 2, HF_Dormer | HF_Balcony);
	Add(-3992.0f, 3000.0f, 1, 320.0f, 320.0f, 3, HF_Dormer);

	// Cul-de-sac 2 (end of Sparrow Close).
	Add(3000.0f, 5192.0f, 0, 340.0f, 320.0f, 2, HF_Tower | HF_Balcony);
	Add(3592.0f, 4600.0f, 3, 300.0f, 300.0f, 2, HF_GableFront | HF_Dormer);
	Add(3000.0f, 4008.0f, 2, 320.0f, 300.0f, 1, HF_Dormer | HF_Stacked);

	// Bramble Way, Thistle Row and Kettle Row.
	const float Front = RoadHalf + WalkWidth + 48.0f;
	HouseRow(FVector2D(-760.0f, 5480.0f + Front), FVector2D(-360.0f, 0.0f), 13, 0, 300.0f, HF_None, 2101u);
	HouseRow(FVector2D(-1300.0f, 5480.0f - Front), FVector2D(-360.0f, 0.0f), 11, 2, 280.0f, HF_None, 2102u);
	HouseRow(FVector2D(-5600.0f + Front, 2100.0f), FVector2D(0.0f, 360.0f), 8, 3, 280.0f, HF_None, 2103u);
	HouseRow(FVector2D(-5600.0f - Front, 2100.0f), FVector2D(0.0f, 360.0f), 10, 1, 260.0f, HF_None, 2104u);
	HouseRow(FVector2D(700.0f, 5480.0f + Front), FVector2D(360.0f, 0.0f), 6, 0, 300.0f, HF_None, 2105u);
	HouseRow(FVector2D(1660.0f, 5480.0f - Front), FVector2D(360.0f, 0.0f), 3, 2, 270.0f, HF_None, 2106u);

	// Driveways and garden paths.
	DirtPath(FVector2D(-3910.0f, 3200.0f), FVector2D(-3790.0f, 3592.0f));
	// Side path: cul-de-sac 2 down to the Clocktower Hill stairs.
	DirtPath(FVector2D(3400.0f, 3784.0f), FVector2D(3560.0f, 4440.0f));
	// Secret path: cul-de-sac 1 south between the gardens to the ivy curtain into the grove.
	DirtPath(FVector2D(-3100.0f, 1600.0f), FVector2D(-2940.0f, 2560.0f));

	// Shrine at the top of Lantern Road; hidden room H1 inside the mound, entered through ivy on its west side.
	Box(M::Tile, FVector(-480.0f, 5600.0f, 0.0f), FVector(480.0f, 5900.0f, 4.0f));
	Stairs(M::Concrete, FVector(-96.0f, 5644.0f, 0.0f), FIntPoint(0, 1), 16, 8.0f, 16.0f, 192.0f);
	Box(M::Concrete, FVector(-480.0f, 5900.0f, 0.0f), FVector(480.0f, 5980.0f, 128.0f));
	Box(M::Concrete, FVector(-480.0f, 6220.0f, 0.0f), FVector(480.0f, 6300.0f, 128.0f));
	Box(M::Concrete, FVector(-200.0f, 5980.0f, 0.0f), FVector(480.0f, 6220.0f, 128.0f));
	WallWithOpenings(M::Concrete, FVector(-480.0f, 5980.0f, 0.0f), FVector(-400.0f, 6220.0f, 128.0f), 0, { FBox2D(FVector2D(6068.0f, 0.0f), FVector2D(6132.0f, 44.0f)) });
	HiddenRoom(M::Concrete, FVector(-400.0f, 5980.0f, 0.0f), FVector(-200.0f, 6220.0f, 112.0f), 0, 6100.0f);
	Box(M::Concrete, FVector(-400.0f, 5980.0f, 112.0f), FVector(-200.0f, 6220.0f, 128.0f));
	Box(M::Vines, FVector(-486.0f, 6040.0f, 0.0f), FVector(-482.0f, 6160.0f, 110.0f));
	Shrub(FVector(-520.0f, 6010.0f, 0.0f), 60.0f, 901u);
	Shrub(FVector(-520.0f, 6190.0f, 0.0f), 54.0f, 902u);
	// Shrine on the mound.
	Box(M::Concrete, FVector(-80.0f, 6060.0f, 128.0f), FVector(80.0f, 6200.0f, 144.0f));
	Box(M::Wood, FVector(-64.0f, 6080.0f, 144.0f), FVector(64.0f, 6190.0f, 230.0f));
	Box(M::Screen, FVector(-18.0f, 6078.0f, 160.0f), FVector(18.0f, 6080.0f, 210.0f));
	GableRoof(M::RoofTeal, FVector(-96.0f, 6060.0f, 230.0f), FVector(96.0f, 6210.0f, 300.0f), 1, 6);
	Lantern(FVector(-140.0f, 6000.0f, 128.0f));
	Lantern(FVector(120.0f, 6000.0f, 128.0f));
	Tree(FVector(300.0f, 6120.0f, 128.0f), 1.6f, 903u);
	Tree(FVector(-300.0f, 6260.0f, 128.0f), 1.3f, 904u);
	// Old water tower beside the shrine plaza.
	for (int32 Leg = 0; Leg < 4; ++Leg)
	{
		Cylinder(M::Metal, FVector(380.0f + ((Leg & 1) ? 60.0f : -60.0f), 5750.0f + ((Leg & 2) ? 60.0f : -60.0f), 0.0f), 6.0f, 360.0f);
	}
	Cylinder(M::Wood, FVector(380.0f, 5750.0f, 360.0f), 100.0f, 130.0f);
	Cylinder(M::RoofTeal, FVector(380.0f, 5750.0f, 490.0f), 108.0f, 12.0f);
	Cylinder(M::RoofTeal, FVector(380.0f, 5750.0f, 502.0f), 70.0f, 16.0f);
	Cylinder(M::Vines, FVector(380.0f, 5750.0f, 380.0f), 102.0f, 50.0f);

	// Community allotments west of Lantern Road.
	for (float Y = 3500.0f; Y < 5300.0f; Y += 140.0f)
	{
		Box(M::Dirt, FVector(-900.0f, Y, 0.0f), FVector(-400.0f, Y + 80.0f, 8.0f));
		for (float X = -880.0f; X < -420.0f; X += 48.0f)
		{
			Box(M::Foliage, FVector(X, Y + 24.0f, 8.0f), FVector(X + 26.0f, Y + 56.0f, 24.0f));
		}
	}
	Box(M::Wood, FVector(-1000.0f, 4300.0f, 0.0f), FVector(-920.0f, 4420.0f, 96.0f));
	GableRoof(M::Metal, FVector(-1010.0f, 4290.0f, 96.0f), FVector(-910.0f, 4430.0f, 126.0f), 1, 3);

	// Overgrowth: orchards, backyards and the meadow beyond.
	const TArray<FBox2D> Avoid = {
		FBox2D(FVector2D(-240.0f, 760.0f), FVector2D(240.0f, 5650.0f)),
		FBox2D(FVector2D(-2950.0f, 2740.0f), FVector2D(-160.0f, 3260.0f)),
		FBox2D(FVector2D(160.0f, 4340.0f), FVector2D(2560.0f, 4860.0f)),
		FBox2D(FVector2D(-3950.0f, 2450.0f), FVector2D(-2850.0f, 3550.0f)),
		FBox2D(FVector2D(2450.0f, 4050.0f), FVector2D(3550.0f, 5150.0f)),
		FBox2D(FVector2D(-3140.0f, 1560.0f), FVector2D(-2900.0f, 2600.0f)),
		FBox2D(FVector2D(3360.0f, 3380.0f), FVector2D(3600.0f, 4460.0f)),
		FBox2D(FVector2D(-940.0f, 3450.0f), FVector2D(-380.0f, 5350.0f)),
		FBox2D(FVector2D(-560.0f, 5560.0f), FVector2D(560.0f, 6400.0f)),
		FBox2D(FVector2D(-1000.0f, 620.0f), FVector2D(1000.0f, 1000.0f)),
		FBox2D(FVector2D(940.0f, 420.0f), FVector2D(4600.0f, 3420.0f)),
	};
	Scatter(FVector2D(-6300.0f, 1700.0f), FVector2D(6300.0f, 6300.0f), 210.0f, 0.22f, Avoid, 1234u);
}

void AHL2NeighborhoodsBlockout::BuildClocktowerHill()
{
	using M = EHL2BlockoutMaterial;
	constexpr float Top = 192.0f;

	Box(M::Concrete, FVector(1384.0f, 480.0f, GroundBottomZ), FVector(4500.0f, 3400.0f, Top - 8.0f));
	Box(M::Grass, FVector(1384.0f, 480.0f, Top - 8.0f), FVector(4500.0f, 3400.0f, Top));
	for (float X = 1500.0f; X < 4400.0f; X += 320.0f)
	{
		Box(M::Brick, FVector(X, 464.0f, 0.0f), FVector(X + 40.0f, 480.0f, Top));
		if (static_cast<int32>(X) % 3 == 0)
		{
			Box(M::Vines, FVector(X + 60.0f, 476.0f, 40.0f), FVector(X + 200.0f, 479.0f, Top));
		}
	}

	// Tram Steps from the hub's north-east opening, with railings.
	Box(M::Tile, FVector(960.0f, 464.0f, 0.0f), FVector(1000.0f, 656.0f, 4.0f));
	Stairs(M::Tile, FVector(1000.0f, 480.0f, 0.0f), FIntPoint(1, 0), 24, 8.0f, 16.0f, 160.0f);
	const float RailPitch = FMath::RadiansToDegrees(FMath::Atan2(8.0f, 16.0f));
	OrientedBox(M::Metal, FVector(1192.0f, 476.0f, 132.0f), FVector(430.0f, 4.0f, 4.0f), FRotator(RailPitch, 0.0f, 0.0f));
	OrientedBox(M::Metal, FVector(1192.0f, 644.0f, 132.0f), FVector(430.0f, 4.0f, 4.0f), FRotator(RailPitch, 0.0f, 0.0f));
	// Old tram rails across the hilltop and a wrecked, overgrown tram.
	Box(M::Metal, FVector(1384.0f, 520.0f, Top), FVector(2600.0f, 526.0f, Top + 3.0f));
	Box(M::Metal, FVector(1384.0f, 594.0f, Top), FVector(2600.0f, 600.0f, Top + 3.0f));
	OrientedBox(M::Train, FVector(1900.0f, 760.0f, Top + 64.0f), FVector(360.0f, 110.0f, 120.0f), FRotator(0.0f, 14.0f, 4.0f));
	OrientedBox(M::Screen, FVector(1890.0f, 704.0f, Top + 90.0f), FVector(280.0f, 2.0f, 34.0f), FRotator(0.0f, 14.0f, 4.0f));
	OrientedBox(M::RoofTeal, FVector(1900.0f, 760.0f, Top + 128.0f), FVector(370.0f, 100.0f, 10.0f), FRotator(0.0f, 14.0f, 4.0f));
	OrientedBox(M::Vines, FVector(1960.0f, 770.0f, Top + 120.0f), FVector(200.0f, 116.0f, 40.0f), FRotator(0.0f, 14.0f, 4.0f));

	// Clocktower.
	Box(M::Brick, FVector(2840.0f, 2040.0f, Top), FVector(3160.0f, 2360.0f, 800.0f));
	Box(M::Trim, FVector(2832.0f, 2032.0f, 560.0f), FVector(3168.0f, 2368.0f, 576.0f));
	Box(M::Trim, FVector(2940.0f, 2032.0f, 630.0f), FVector(3060.0f, 2040.0f, 750.0f));
	Box(M::Trim, FVector(2940.0f, 2360.0f, 630.0f), FVector(3060.0f, 2368.0f, 750.0f));
	Box(M::Trim, FVector(2832.0f, 2140.0f, 630.0f), FVector(2840.0f, 2260.0f, 750.0f));
	Box(M::Trim, FVector(3160.0f, 2140.0f, 630.0f), FVector(3168.0f, 2260.0f, 750.0f));
	OrientedBox(M::Metal, FVector(3000.0f, 2028.0f, 700.0f), FVector(4.0f, 2.0f, 50.0f), FRotator(0.0f, 0.0f, 0.0f));
	OrientedBox(M::Metal, FVector(3010.0f, 2028.0f, 690.0f), FVector(36.0f, 2.0f, 4.0f), FRotator(-30.0f, 0.0f, 0.0f));
	Box(M::Screen, FVector(2970.0f, 2038.0f, 260.0f), FVector(3030.0f, 2040.0f, 360.0f));
	Box(M::Screen, FVector(2970.0f, 2038.0f, 420.0f), FVector(3030.0f, 2040.0f, 500.0f));
	Box(M::Brick, FVector(2840.0f, 2040.0f, 800.0f), FVector(3160.0f, 2360.0f, 808.0f));
	for (int32 Pillar = 0; Pillar < 4; ++Pillar)
	{
		const float PX = (Pillar & 1) ? 3120.0f : 2840.0f;
		const float PY = (Pillar & 2) ? 2320.0f : 2040.0f;
		Box(M::Brick, FVector(PX, PY, 808.0f), FVector(PX + 40.0f, PY + 40.0f, 900.0f));
	}
	Cylinder(M::Metal, FVector(3000.0f, 2200.0f, 830.0f), 44.0f, 56.0f);
	for (int32 Step = 0; Step < 9; ++Step)
	{
		const float Inset = Step * 18.0f - 12.0f;
		Box(M::RoofTeal, FVector(2840.0f + Inset, 2040.0f + Inset, 900.0f + Step * 24.0f), FVector(3160.0f - Inset, 2360.0f - Inset, 924.0f + Step * 24.0f));
	}
	Box(M::Metal, FVector(2998.0f, 2198.0f, 1116.0f), FVector(3002.0f, 2202.0f, 1180.0f));
	OrientedBox(M::Metal, FVector(3000.0f, 2200.0f, 1166.0f), FVector(50.0f, 3.0f, 12.0f), FRotator(0.0f, 30.0f, 0.0f));
	Box(M::Vines, FVector(2836.0f, 2100.0f, Top), FVector(2840.0f, 2300.0f, 520.0f));

	// Hidden room H2: annex at the tower's foot, crawl-in from the east behind shrubs.
	HiddenRoom(M::Brick, FVector(3160.0f, 2120.0f, Top), FVector(3400.0f, 2320.0f, Top + 112.0f), 1, 2220.0f);
	OrientedBox(M::RoofRed, FVector(3280.0f, 2220.0f, Top + 124.0f), FVector(260.0f, 220.0f, 8.0f), FRotator(10.0f, 0.0f, 0.0f));
	Shrub(FVector(3430.0f, 2150.0f, Top), 56.0f, 311u);
	Shrub(FVector(3430.0f, 2290.0f, Top), 60.0f, 312u);
	Box(M::Vines, FVector(3400.0f, 2180.0f, Top), FVector(3404.0f, 2260.0f, Top + 60.0f));

	// Stairs down to Sparrow Close cul-de-sac (side path).
	Stairs(M::Tile, FVector(3400.0f, 3784.0f, 0.0f), FIntPoint(0, -1), 24, 8.0f, 16.0f, 160.0f);

	// Lookout benches over the hub and hilltop overgrowth.
	Box(M::Wood, FVector(1500.0f, 1000.0f, Top), FVector(1580.0f, 1018.0f, Top + 16.0f));
	Box(M::Wood, FVector(1500.0f, 1300.0f, Top), FVector(1580.0f, 1318.0f, Top + 16.0f));
	const TArray<FBox2D> Avoid = {
		FBox2D(FVector2D(1384.0f, 480.0f), FVector2D(2700.0f, 900.0f)),
		FBox2D(FVector2D(2760.0f, 1960.0f), FVector2D(3520.0f, 2440.0f)),
		FBox2D(FVector2D(3360.0f, 3200.0f), FVector2D(3600.0f, 3400.0f)),
		FBox2D(FVector2D(1400.0f, 950.0f), FVector2D(1650.0f, 1350.0f)),
	};
	Scatter(FVector2D(1420.0f, 520.0f), FVector2D(4460.0f, 3360.0f), 240.0f, 0.35f, Avoid, 4321u, Top);
}

void AHL2NeighborhoodsBlockout::BuildRubbleTerraces()
{
	using M = EHL2BlockoutMaterial;

	// Rubble Row from the hub's east opening to the collapse (B1), with T3 (Quarry Lane, south).
	Road(true, 0.0f, 800.0f, 4400.0f, M::DarkConcrete, { FVector2D(2976.0f, 3424.0f) }, {});
	for (float X = 1100.0f; X < 4300.0f; X += 420.0f)
	{
		Lantern(FVector(X, 200.0f, 6.0f));
		RubblePile(FVector(X + 180.0f, 360.0f, 0.0f), 60.0f, 50.0f, HashXY(X, 360.0f));
	}

	// B1: collapsed overpass blocking the road. Maintenance robot placeholder beside it.
	Box(M::Rubble, FVector(4380.0f, -480.0f, 0.0f), FVector(4560.0f, 400.0f, 288.0f));
	RubblePile(FVector(4380.0f, -260.0f, 0.0f), 140.0f, 200.0f, 501u);
	RubblePile(FVector(4400.0f, 120.0f, 0.0f), 150.0f, 240.0f, 502u);
	OrientedBox(M::Concrete, FVector(4440.0f, -40.0f, 220.0f), FVector(140.0f, 700.0f, 30.0f), FRotator(0.0f, 0.0f, 14.0f));
	OrientedBox(M::Metal, FVector(4420.0f, 300.0f, 260.0f), FVector(8.0f, 8.0f, 200.0f), FRotator(30.0f, 0.0f, 0.0f));
	BlockerSign(FVector(4220.0f, -250.0f, 2.0f));
	MaintenanceRobot(FVector(4230.0f, 180.0f, 6.0f), 180.0f);

	// District walls (the hill side is taller so it can't be dropped into from the hilltop).
	Ridge(FVector2D(4500.0f, 400.0f), FVector2D(4600.0f, 3480.0f), 352.0f, 1);
	Ridge(FVector2D(4500.0f, -1680.0f), FVector2D(4600.0f, -480.0f), 256.0f, 1);
	Ridge(FVector2D(4600.0f, -1680.0f), FVector2D(MapHalf, -1600.0f), 288.0f, 1);
	Ridge(FVector2D(4600.0f, 3400.0f), FVector2D(MapHalf, 3480.0f), 320.0f, 2);

	// Inside: the road continues to a cracked plaza.
	Road(true, 0.0f, 4560.0f, 5400.0f, M::DarkConcrete, {}, {});
	Box(M::Tile, FVector(5400.0f, -400.0f, 0.0f), FVector(6200.0f, 400.0f, 4.0f));
	OrientedBox(M::Concrete, FVector(5800.0f, 0.0f, 50.0f), FVector(60.0f, 60.0f, 100.0f), FRotator(0.0f, 20.0f, 8.0f));
	OrientedBox(M::Concrete, FVector(5860.0f, 60.0f, 14.0f), FVector(90.0f, 40.0f, 28.0f), FRotator(0.0f, 50.0f, 0.0f));

	// Ruin R1: gutted apartment block with a low west wing (roof garden).
	Box(M::Concrete, FVector(5400.0f, 1200.0f, 0.0f), FVector(5640.0f, 2000.0f, 328.0f));
	Box(M::Grass, FVector(5400.0f, 1200.0f, 328.0f), FVector(5640.0f, 2000.0f, 330.0f));
	Box(M::Concrete, FVector(5400.0f, 2000.0f, 0.0f), FVector(5640.0f, 2600.0f, 600.0f));
	FRand R(600u);
	for (float X = 5640.0f; X < 6200.0f; X += 140.0f)
	{
		const float H = R.Range(520.0f, 780.0f);
		Box(M::Concrete, FVector(X, 1200.0f, 0.0f), FVector(X + 140.0f, 2600.0f, H));
		Box(M::Foliage, FVector(X + 10.0f, 1300.0f, H), FVector(X + 130.0f, 1300.0f + R.Range(200.0f, 900.0f), H + 12.0f));
	}
	for (float Z = 112.0f; Z < 600.0f; Z += 112.0f)
	{
		if (Z > 328.0f)
		{
			Box(M::DarkConcrete, FVector(5396.0f, 2000.0f, Z), FVector(5400.0f, 2600.0f, Z + 8.0f));
		}
		for (float Y = 2040.0f; Y < 2560.0f; Y += 120.0f)
		{
			Box(M::DarkConcrete, FVector(5396.0f, Y, Z - 80.0f), FVector(5400.0f, Y + 60.0f, Z - 20.0f));
		}
	}
	for (float X = 5680.0f; X < 6160.0f; X += 120.0f)
	{
		for (float Z = 60.0f; Z < 500.0f; Z += 112.0f)
		{
			Box(M::DarkConcrete, FVector(X, 1196.0f, Z), FVector(X + 60.0f, 1200.0f, Z + 60.0f));
		}
	}
	Box(M::Vines, FVector(5396.0f, 2100.0f, 200.0f), FVector(5398.0f, 2400.0f, 600.0f));
	Box(M::Vines, FVector(5800.0f, 1196.0f, 100.0f), FVector(6000.0f, 1198.0f, 520.0f));

	// Shelf on the ruin's west face, reached by stairs; house T2 is built onto it.
	Stairs(M::Concrete, FVector(4606.0f, 1300.0f, 0.0f), FIntPoint(1, 0), 32, 8.0f, 12.0f, 160.0f);
	Box(M::Concrete, FVector(4990.0f, 1300.0f, 240.0f), FVector(5400.0f, 2000.0f, 256.0f));
	for (float Y = 1320.0f; Y < 2000.0f; Y += 220.0f)
	{
		Box(M::Concrete, FVector(5000.0f, Y, 0.0f), FVector(5024.0f, Y + 24.0f, 240.0f));
	}
	// L2: upgrade ledge from the shelf (256) up to the roof garden (328).
	LedgeStripe(FVector(5397.0f, 1300.0f, 316.0f), FVector(5400.0f, 2000.0f, 328.0f));
	// Hidden room H7 on the roof garden.
	HiddenRoom(M::Concrete, FVector(5440.0f, 1240.0f, 330.0f), FVector(5640.0f, 1440.0f, 440.0f), 3, 5540.0f);
	Scatter(FVector2D(5410.0f, 1460.0f), FVector2D(5630.0f, 1990.0f), 110.0f, 0.3f, {}, 601u, 330.0f);

	// Platform of rubble for house T1, with stairs from the road.
	Box(M::Rubble, FVector(4800.0f, 520.0f, 0.0f), FVector(5300.0f, 1000.0f, 96.0f));
	RubblePile(FVector(4800.0f, 760.0f, 0.0f), 90.0f, 110.0f, 602u);
	RubblePile(FVector(5300.0f, 900.0f, 0.0f), 80.0f, 120.0f, 603u);
	Stairs(M::Rubble, FVector(4960.0f, 328.0f, 0.0f), FIntPoint(0, 1), 12, 8.0f, 16.0f, 160.0f);

	auto Add = [&](float X, float Y, int32 Facing, float W, float D, int32 Storeys, M Wall, M Roof, uint32 Features, float BaseZ, uint32 Seed)
	{
		FHouse H;
		H.FrontCenter = FVector2D(X, Y);
		H.Facing = Facing;
		H.Width = W;
		H.Depth = D;
		H.Storeys = Storeys;
		H.Wall = Wall;
		H.Roof = Roof;
		H.Features = Features;
		H.Seed = Seed;
		H.BaseZ = BaseZ;
		House(H);
	};
	Add(5050.0f, 600.0f, 0, 300.0f, 300.0f, 2, M::PlasterWarm, M::RoofTeal, HF_Stacked | HF_Chimney | HF_Ivy | HF_Fence, 96.0f, 7001u);
	Add(5140.0f, 1790.0f, 3, 300.0f, 240.0f, 2, M::Plaster, M::RoofRed, HF_Tower | HF_Ivy, 256.0f, 7002u);
	Add(5600.0f, -560.0f, 2, 280.0f, 280.0f, 1, M::Brick, M::RoofRed, HF_Rubble | HF_Dormer | HF_Chimney, 0.0f, 7003u);

	// Ruin R2: broken walls and fallen slabs.
	Box(M::Concrete, FVector(4800.0f, -1500.0f, 0.0f), FVector(5300.0f, -1460.0f, 380.0f));
	Box(M::Concrete, FVector(4800.0f, -1460.0f, 0.0f), FVector(4840.0f, -1100.0f, 300.0f));
	OrientedBox(M::Concrete, FVector(5100.0f, -1200.0f, 90.0f), FVector(400.0f, 30.0f, 260.0f), FRotator(0.0f, 10.0f, 30.0f));
	RubblePile(FVector(5150.0f, -1300.0f, 0.0f), 160.0f, 120.0f, 604u);
	Box(M::Vines, FVector(4840.0f, -1462.0f, 120.0f), FVector(5100.0f, -1460.0f, 380.0f));

	const TArray<FBox2D> Avoid = {
		FBox2D(FVector2D(4560.0f, -260.0f), FVector2D(6200.0f, 420.0f)),
		FBox2D(FVector2D(4600.0f, 300.0f), FVector2D(5320.0f, 1020.0f)),
		FBox2D(FVector2D(4600.0f, 1280.0f), FVector2D(5420.0f, 2020.0f)),
		FBox2D(FVector2D(5380.0f, 1180.0f), FVector2D(6400.0f, 2620.0f)),
		FBox2D(FVector2D(4780.0f, -1520.0f), FVector2D(5320.0f, -1080.0f)),
	};
	Scatter(FVector2D(4620.0f, -1580.0f), FVector2D(6380.0f, 3380.0f), 230.0f, 0.3f, Avoid, 605u);
	const TArray<FBox2D> RowAvoid = { FBox2D(FVector2D(2900.0f, -260.0f), FVector2D(3500.0f, 260.0f)) };
	Scatter(FVector2D(1000.0f, 250.0f), FVector2D(4360.0f, 470.0f), 140.0f, 0.0f, RowAvoid, 606u);
}

void AHL2NeighborhoodsBlockout::BuildFallenBlockAndQuarry()
{
	using M = EHL2BlockoutMaterial;

	// Quarry Lane (T3 off Rubble Row) down to cul-de-sac 3.
	Road(false, 3200.0f, -2560.0f, -160.0f, M::DarkConcrete, {}, {});
	CulDeSac(FVector2D(3200.0f, -3000.0f), 480.0f, 90.0f);
	FHouse East{ FVector2D(3792.0f, -3000.0f), 3, 300.0f, 300.0f, 2, M::PlasterWarm, M::RoofRed, HF_Rubble | HF_Dormer | HF_Chimney | HF_Fence, 8101u };
	FHouse West{ FVector2D(2608.0f, -3000.0f), 1, 320.0f, 300.0f, 2, M::Plaster, M::RoofTeal, HF_Tower | HF_Stacked | HF_Ivy | HF_Fence, 8102u };
	House(East);
	House(West);

	// Fallen Block: a toppled apartment tower lying on its side.
	Box(M::Concrete, FVector(1100.0f, -2200.0f, 0.0f), FVector(2300.0f, -1880.0f, 260.0f));
	for (float X = 1140.0f; X < 2260.0f; X += 112.0f)
	{
		for (float Y = -2160.0f; Y < -1920.0f; Y += 80.0f)
		{
			Box(M::DarkConcrete, FVector(X, Y, 260.0f), FVector(X + 60.0f, Y + 40.0f, 262.0f));
		}
	}
	OrientedBox(M::Concrete, FVector(2380.0f, -2040.0f, 120.0f), FVector(160.0f, 300.0f, 220.0f), FRotator(18.0f, 0.0f, 0.0f));
	RubblePile(FVector(1050.0f, -2050.0f, 0.0f), 160.0f, 160.0f, 801u);
	RubblePile(FVector(2450.0f, -1800.0f, 0.0f), 120.0f, 120.0f, 802u);
	Box(M::Vines, FVector(1300.0f, -1882.0f, 60.0f), FVector(1900.0f, -1880.0f, 260.0f));
	Tree(FVector(1700.0f, -2040.0f, 262.0f), 1.3f, 803u);

	// Hidden room H3 in the gap between the fallen tower and a leaning slab.
	HiddenRoom(M::Concrete, FVector(1300.0f, -1860.0f, 0.0f), FVector(1500.0f, -1680.0f, 96.0f), 3, 1400.0f);
	OrientedBox(M::Concrete, FVector(1400.0f, -1560.0f, 120.0f), FVector(320.0f, 20.0f, 260.0f), FRotator(0.0f, 0.0f, -30.0f));
	Shrub(FVector(1250.0f, -1560.0f, 0.0f), 60.0f, 804u);

	// Broken wall stubs with window holes.
	const TArray<FBox2D> Windows = { FBox2D(FVector2D(700.0f, 60.0f), FVector2D(780.0f, 150.0f)), FBox2D(FVector2D(900.0f, 60.0f), FVector2D(980.0f, 150.0f)) };
	WallWithOpenings(M::Brick, FVector(600.0f, -1000.0f, 0.0f), FVector(1100.0f, -980.0f, 220.0f), 1, Windows);
	WallWithOpenings(M::Brick, FVector(1700.0f, -3200.0f, 0.0f), FVector(1720.0f, -2700.0f, 260.0f), 0, { FBox2D(FVector2D(-3000.0f, 0.0f), FVector2D(-2900.0f, 110.0f)) });

	const TArray<FBox2D> FallenAvoid = {
		FBox2D(FVector2D(880.0f, -2400.0f), FVector2D(2620.0f, -1500.0f)),
		FBox2D(FVector2D(2950.0f, -3600.0f), FVector2D(3450.0f, -150.0f)),
		FBox2D(FVector2D(2680.0f, -3540.0f), FVector2D(3720.0f, -2460.0f)),
		FBox2D(FVector2D(580.0f, -1020.0f), FVector2D(1120.0f, -960.0f)),
	};
	Scatter(FVector2D(300.0f, -3560.0f), FVector2D(4460.0f, -500.0f), 220.0f, 0.3f, FallenAvoid, 805u);

	// Quarry: stepped rock faces, crane, conveyor, rock piles.
	for (int32 Tier = 0; Tier < 3; ++Tier)
	{
		const float Inset = Tier * 200.0f;
		Box(M::Rubble, FVector(5600.0f + Inset, -4600.0f, 0.0f), FVector(MapHalf, -1680.0f - Inset * 0.5f, 120.0f + Tier * 120.0f));
	}
	Box(M::Rubble, FVector(2560.0f, -MapHalf, 0.0f), FVector(MapHalf, -5000.0f, 300.0f));
	Box(M::Metal, FVector(4600.0f, -4400.0f, 0.0f), FVector(4640.0f, -4360.0f, 640.0f));
	OrientedBox(M::Metal, FVector(4500.0f, -4380.0f, 640.0f), FVector(520.0f, 24.0f, 24.0f), FRotator(6.0f, 160.0f, 0.0f));
	Box(M::Metal, FVector(4250.0f, -4300.0f, 400.0f), FVector(4252.0f, -4298.0f, 620.0f));
	OrientedBox(M::Wood, FVector(5200.0f, -3900.0f, 110.0f), FVector(600.0f, 50.0f, 8.0f), FRotator(18.0f, 0.0f, 0.0f));
	RubblePile(FVector(4000.0f, -3900.0f, 0.0f), 160.0f, 120.0f, 806u);
	RubblePile(FVector(5000.0f, -4400.0f, 0.0f), 200.0f, 150.0f, 807u);
	// Steps up out of the flooded cut so the canal bed here isn't a trap.
	Stairs(M::Concrete, FVector(3000.0f, -4920.0f, CanalBedZ), FIntPoint(0, 1), 20, 8.0f, 16.0f, 160.0f);

	// Mine-cart rails into the tunnel.
	Box(M::Metal, FVector(2560.0f, -4262.0f, 0.0f), FVector(3200.0f, -4258.0f, 3.0f));
	Box(M::Metal, FVector(2560.0f, -4222.0f, 0.0f), FVector(3200.0f, -4218.0f, 3.0f));
}

void AHL2NeighborhoodsBlockout::BuildCanalQuarter()
{
	using M = EHL2BlockoutMaterial;

	// Canal Walk (cobbled lane) from the hub to the promenade.
	Road(false, 0.0f, -4140.0f, -800.0f, M::Tile, {}, { FVector2D(-3250.0f - 224.0f, -3250.0f + 224.0f) });

	// Cinder Lane: east off Canal Walk, then a footpath on to cul-de-sac 3.
	Road(true, -3250.0f, 160.0f, 2240.0f, M::DarkConcrete, {}, {});
	DirtPath(FVector2D(2240.0f, -3400.0f), FVector2D(2780.0f, -3260.0f));
	HouseRow(FVector2D(520.0f, -3250.0f + RoadHalf + WalkWidth + 48.0f), FVector2D(360.0f, 0.0f), 5, 0, 280.0f, HF_None, 2201u);

	// Weir Street along the far bank, reached over the drawbridge or the broken footbridge.
	Road(true, -5700.0f, -2780.0f, 2300.0f, M::Tile, { }, { FVector2D(-224.0f, 224.0f) });
	Road(false, 0.0f, -5476.0f, -5000.0f, M::Tile, {}, {});
	DirtPath(FVector2D(-1680.0f, -5476.0f), FVector2D(-1520.0f, -5000.0f));
	Box(M::Tile, FVector(-2800.0f, -4600.0f, 0.0f), FVector(2320.0f, -4140.0f, 4.0f));
	for (float X = -2700.0f; X < 2300.0f; X += 160.0f)
	{
		Box(M::Metal, FVector(X, -4596.0f, 4.0f), FVector(X + 8.0f, -4588.0f, 30.0f));
	}
	for (float X = -2600.0f; X < 2300.0f; X += 480.0f)
	{
		Lantern(FVector(X, -4180.0f, 4.0f));
	}

	// District walls; the quarry wall has the caved-in tunnel (B3).
	Ridge(FVector2D(-2880.0f, -MapHalf), FVector2D(-2800.0f, -3600.0f), 384.0f, 2);
	Ridge(FVector2D(-2800.0f, -3680.0f), FVector2D(-224.0f, -3600.0f), 256.0f, 1);
	Ridge(FVector2D(224.0f, -3680.0f), FVector2D(2320.0f, -3600.0f), 256.0f, 1);
	Ridge(FVector2D(2320.0f, -MapHalf), FVector2D(2560.0f, -4320.0f), 384.0f, 2);
	Ridge(FVector2D(2320.0f, -4160.0f), FVector2D(2560.0f, -3600.0f), 384.0f, 2);
	Box(M::Concrete, FVector(2320.0f, -4320.0f, 128.0f), FVector(2560.0f, -4160.0f, 384.0f));
	Box(M::Brick, FVector(2316.0f, -4332.0f, 0.0f), FVector(2320.0f, -4148.0f, 140.0f));
	Box(M::Rubble, FVector(2400.0f, -4320.0f, 0.0f), FVector(2480.0f, -4160.0f, 128.0f));
	RubblePile(FVector(2300.0f, -4240.0f, 0.0f), 70.0f, 70.0f, 701u);
	RubblePile(FVector(2580.0f, -4240.0f, 0.0f), 70.0f, 80.0f, 702u);
	BlockerSign(FVector(2640.0f, -4100.0f, 0.0f));

	// Canal-front warehouses (solid).
	TownRow(FFrame{ FVector2D(0.0f, -4120.0f), 0, 0.0f }, -2700.0f, 2200.0f, { FVector2D(-224.0f, 224.0f), FVector2D(-2000.0f, -1800.0f) }, 5u);

	// Steps down into the canal bed on the near side only.
	Stairs(M::Concrete, FVector(600.0f, -4920.0f, CanalBedZ), FIntPoint(0, 1), 20, 8.0f, 16.0f, 160.0f);

	// Hidden room H4: crawl nook in the north quay wall, below the promenade.
	Box(M::Concrete, FVector(-480.0f, -4600.0f, GroundBottomZ), FVector(-240.0f, -4400.0f, CanalBedZ));
	HiddenRoom(M::Concrete, FVector(-480.0f, -4600.0f, CanalBedZ), FVector(-240.0f, -4400.0f, -8.0f), 2, -360.0f);
	Box(M::Concrete, FVector(-480.0f, -4600.0f, -8.0f), FVector(-240.0f, -4400.0f, 0.0f));
	Box(M::Vines, FVector(-420.0f, -4604.0f, -150.0f), FVector(-300.0f, -4600.0f, -40.0f));

	// B4: raised drawbridge on Canal Walk.
	OrientedBox(M::Wood, FVector(0.0f, -4626.0f, 97.0f), FVector(320.0f, 200.0f, 12.0f), FRotator(0.0f, 0.0f, 75.0f));
	OrientedBox(M::Wood, FVector(0.0f, -4974.0f, 97.0f), FVector(320.0f, 200.0f, 12.0f), FRotator(0.0f, 0.0f, -75.0f));
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		Box(M::Concrete, FVector(Side * 200.0f - 40.0f, -4680.0f, CanalBedZ), FVector(Side * 200.0f + 40.0f, -4600.0f, 240.0f));
		Box(M::Concrete, FVector(Side * 200.0f - 40.0f, -5000.0f, CanalBedZ), FVector(Side * 200.0f + 40.0f, -4920.0f, 240.0f));
		Box(M::RoofTeal, FVector(Side * 200.0f - 48.0f, -4688.0f, 240.0f), FVector(Side * 200.0f + 48.0f, -4592.0f, 260.0f));
		Box(M::RoofTeal, FVector(Side * 200.0f - 48.0f, -5008.0f, 240.0f), FVector(Side * 200.0f + 48.0f, -4912.0f, 260.0f));
	}
	Box(M::Metal, FVector(-200.0f, -4644.0f, 220.0f), FVector(200.0f, -4636.0f, 228.0f));
	BlockerSign(FVector(280.0f, -4560.0f, 4.0f));

	// J1: broken footbridge; the 128 HU gap needs a sprint jump.
	Box(M::Wood, FVector(-1680.0f, -4736.0f, -8.0f), FVector(-1520.0f, -4600.0f, 0.0f));
	Box(M::Wood, FVector(-1680.0f, -5000.0f, -8.0f), FVector(-1520.0f, -4864.0f, 0.0f));
	Box(M::Wood, FVector(-1680.0f, -4744.0f, CanalBedZ), FVector(-1664.0f, -4728.0f, -8.0f));
	Box(M::Wood, FVector(-1536.0f, -4872.0f, CanalBedZ), FVector(-1520.0f, -4856.0f, -8.0f));
	Box(M::Wood, FVector(-1680.0f, -4736.0f, 0.0f), FVector(-1676.0f, -4600.0f, 36.0f));
	Box(M::Wood, FVector(-1524.0f, -5000.0f, 0.0f), FVector(-1520.0f, -4880.0f, 36.0f));
	LedgeStripe(FVector(-1680.0f, -4740.0f, 0.0f), FVector(-1520.0f, -4736.0f, 0.5f));

	// Far bank: the waterwheel house and its wheel.
	FHouse Mill{ FVector2D(-1000.0f, -5064.0f), 2, 320.0f, 320.0f, 2, M::PlasterWarm, M::RoofRed, HF_Tower | HF_Chimney | HF_Ivy | HF_Balcony, 9001u };
	House(Mill);
	const FVector WheelC(-800.0f, -4960.0f, -30.0f);
	Box(M::Wood, WheelC + FVector(-24.0f, -6.0f, -6.0f), WheelC + FVector(24.0f, 6.0f, 6.0f));
	for (int32 Spoke = 0; Spoke < 6; ++Spoke)
	{
		OrientedBox(M::Wood, WheelC, FVector(8.0f, 240.0f, 8.0f), FRotator(0.0f, 0.0f, Spoke * 30.0f));
	}
	for (int32 Paddle = 0; Paddle < 12; ++Paddle)
	{
		const float A = FMath::DegreesToRadians(Paddle * 30.0f);
		OrientedBox(M::Wood, WheelC + FVector(0.0f, FMath::Cos(A) * 120.0f, FMath::Sin(A) * 120.0f), FVector(40.0f, 64.0f, 6.0f), FRotator(0.0f, 0.0f, Paddle * -30.0f));
	}

	// Far-bank garden and a ruined lock tower.
	Box(M::Brick, FVector(1200.0f, -5400.0f, 0.0f), FVector(1440.0f, -5160.0f, 520.0f));
	OrientedBox(M::Brick, FVector(1320.0f, -5280.0f, 560.0f), FVector(240.0f, 240.0f, 80.0f), FRotator(8.0f, 0.0f, 6.0f));
	Box(M::Vines, FVector(1196.0f, -5380.0f, 80.0f), FVector(1200.0f, -5200.0f, 480.0f));
	Box(M::Marker, FVector(1300.0f, -5164.0f, 40.0f), FVector(1340.0f, -5160.0f, 80.0f));

	// Weir Street houses; the lock tower plot is kept clear.
	const TArray<FBox2D> LockTower = { FBox2D(FVector2D(1150.0f, -5450.0f), FVector2D(1490.0f, -5110.0f)) };
	const float WeirFront = RoadHalf + WalkWidth + 48.0f;
	HouseRow(FVector2D(-2600.0f, -5700.0f + WeirFront), FVector2D(360.0f, 0.0f), 14, 0, 240.0f, HF_Ivy, 2202u, LockTower);
	HouseRow(FVector2D(-2600.0f, -5700.0f - WeirFront), FVector2D(360.0f, 0.0f), 14, 2, 250.0f, HF_None, 2203u);

	const TArray<FBox2D> NearAvoid = { FBox2D(FVector2D(-260.0f, -4160.0f), FVector2D(260.0f, -3580.0f)) };
	Scatter(FVector2D(-2780.0f, -3940.0f), FVector2D(2300.0f, -3700.0f), 200.0f, 0.25f, NearAvoid, 703u);
	const TArray<FBox2D> FarAvoid = {
		FBox2D(FVector2D(-1240.0f, -5500.0f), FVector2D(-740.0f, -5000.0f)),
		FBox2D(FVector2D(-260.0f, -5100.0f), FVector2D(260.0f, -5000.0f)),
		FBox2D(FVector2D(1150.0f, -5450.0f), FVector2D(1490.0f, -5110.0f)),
	};
	Scatter(FVector2D(-2780.0f, -6380.0f), FVector2D(2300.0f, -5040.0f), 220.0f, 0.35f, FarAvoid, 704u);
}

void AHL2NeighborhoodsBlockout::BuildGrove()
{
	using M = EHL2BlockoutMaterial;

	// Grove Path from the hub's west opening.
	DirtPath(FVector2D(-5700.0f, -96.0f), FVector2D(-800.0f, 96.0f));

	// Boundaries: north hedge (ivy curtain gap = secret path), east wall, bramble wall (B2), south wall (ledge L1).
	Ridge(FVector2D(-MapHalf, 1600.0f), FVector2D(-3100.0f, 1660.0f), 224.0f, 0);
	Ridge(FVector2D(-2940.0f, 1600.0f), FVector2D(-1200.0f, 1660.0f), 224.0f, 0);
	Box(M::FoliageDark, FVector(-3100.0f, 1600.0f, 120.0f), FVector(-2940.0f, 1660.0f, 224.0f));
	Box(M::Vines, FVector(-3100.0f, 1626.0f, 0.0f), FVector(-2940.0f, 1634.0f, 120.0f));
	Ridge(FVector2D(-1260.0f, -1840.0f), FVector2D(-1200.0f, -112.0f), 224.0f, 1);
	Ridge(FVector2D(-1260.0f, 112.0f), FVector2D(-1200.0f, 1600.0f), 224.0f, 1);
	Ridge(FVector2D(-2440.0f, -1760.0f), FVector2D(-2360.0f, -112.0f), 224.0f, 0);
	Ridge(FVector2D(-2440.0f, 112.0f), FVector2D(-2360.0f, 1600.0f), 224.0f, 0);
	Ridge(FVector2D(-MapHalf, -1840.0f), FVector2D(-4480.0f, -1760.0f), 256.0f, 1);
	Ridge(FVector2D(-4320.0f, -1840.0f), FVector2D(-1200.0f, -1760.0f), 256.0f, 1);

	// B2: thorny bramble across the path.
	Box(M::FoliageDark, FVector(-2440.0f, -112.0f, 0.0f), FVector(-2360.0f, 112.0f, 224.0f));
	FRand R(1501u);
	for (int32 Thorn = 0; Thorn < 14; ++Thorn)
	{
		OrientedBox(M::Wood, FVector(-2400.0f + R.Range(-60.0f, 60.0f), R.Range(-110.0f, 110.0f), R.Range(20.0f, 180.0f)),
			FVector(R.Range(80.0f, 160.0f), 4.0f, 4.0f), FRotator(R.Range(-40.0f, 40.0f), R.Range(0.0f, 180.0f), 0.0f));
		Cylinder(R.Chance(0.5f) ? M::Foliage : M::FoliageDark, FVector(-2400.0f + R.Range(-70.0f, 70.0f), R.Range(-110.0f, 110.0f), R.Range(0.0f, 150.0f)), R.Range(30.0f, 50.0f), R.Range(30.0f, 60.0f));
	}
	BlockerSign(FVector(-2300.0f, -150.0f, 0.0f));

	// Glade (public side of the bramble).
	Box(M::Concrete, FVector(-1900.0f, 300.0f, 0.0f), FVector(-1860.0f, 340.0f, 120.0f));
	Box(M::Concrete, FVector(-1700.0f, 300.0f, 0.0f), FVector(-1660.0f, 340.0f, 120.0f));
	Box(M::Concrete, FVector(-1910.0f, 290.0f, 120.0f), FVector(-1650.0f, 350.0f, 140.0f));
	OrientedBox(M::Wood, FVector(-1800.0f, -500.0f, 18.0f), FVector(360.0f, 36.0f, 36.0f), FRotator(0.0f, 30.0f, 0.0f));
	const TArray<FBox2D> GladeAvoid = {
		FBox2D(FVector2D(-2360.0f, -140.0f), FVector2D(-1200.0f, 140.0f)),
		FBox2D(FVector2D(-1960.0f, 260.0f), FVector2D(-1600.0f, 380.0f)),
	};
	Scatter(FVector2D(-2340.0f, -1740.0f), FVector2D(-1280.0f, 1580.0f), 200.0f, 0.45f, GladeAvoid, 1502u);

	// Grove heart: the house the camphor tree grew through.
	FHouse Swallowed{ FVector2D(-4300.0f, 260.0f), 0, 340.0f, 340.0f, 2, M::Plaster, M::RoofTeal, HF_Ivy | HF_Chimney | HF_Rubble | HF_Dormer, 1503u };
	House(Swallowed);
	Cylinder(M::Wood, FVector(-4300.0f, 440.0f, 0.0f), 54.0f, 640.0f);
	for (int32 Puff = 0; Puff < 10; ++Puff)
	{
		const float A = R.Range(0.0f, 2.0f * UE_PI);
		const float D = R.Range(0.0f, 220.0f);
		Cylinder(R.Chance(0.6f) ? M::Foliage : M::FoliageDark, FVector(-4300.0f + FMath::Cos(A) * D, 440.0f + FMath::Sin(A) * D, R.Range(560.0f, 700.0f)), R.Range(100.0f, 170.0f), R.Range(60.0f, 110.0f));
	}
	for (int32 RootIndex = 0; RootIndex < 5; ++RootIndex)
	{
		const float Yaw = RootIndex * 72.0f + 20.0f;
		const FVector Dir(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0.0f);
		OrientedBox(M::Wood, FVector(-4300.0f, 440.0f, 0.0f) + Dir * 200.0f + FVector(0.0f, 0.0f, 300.0f), FVector(240.0f, 18.0f, 18.0f), FRotator(25.0f, Yaw, 0.0f));
	}

	// Hidden room H5: hollow tree, crawl in between the roots on the east side.
	{
		const FVector2D C(-5200.0f, -900.0f);
		constexpr int32 Segments = 14;
		constexpr float Radius = 170.0f;
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const float Deg = 360.0f * Segment / Segments;
			const float Rad = FMath::DegreesToRadians(Deg);
			const FVector P(C.X + FMath::Cos(Rad) * Radius, C.Y + FMath::Sin(Rad) * Radius, 0.0f);
			const float SegLen = 2.0f * UE_PI * Radius / Segments + 10.0f;
			if (Segment == 0)
			{
				OrientedBox(M::Wood, P + FVector(0.0f, 0.0f, 44.0f + 188.0f), FVector(40.0f, SegLen, 376.0f), FRotator(0.0f, Deg, 0.0f));
			}
			else
			{
				OrientedBox(M::Wood, P + FVector(0.0f, 0.0f, 210.0f), FVector(40.0f, SegLen, 420.0f), FRotator(0.0f, Deg, 0.0f));
			}
		}
		Cylinder(M::Wood, FVector(C.X, C.Y, 400.0f), Radius + 20.0f, 20.0f);
		for (int32 Puff = 0; Puff < 8; ++Puff)
		{
			const float A = R.Range(0.0f, 2.0f * UE_PI);
			const float D = R.Range(0.0f, 200.0f);
			Cylinder(R.Chance(0.5f) ? M::Foliage : M::FoliageDark, FVector(C.X + FMath::Cos(A) * D, C.Y + FMath::Sin(A) * D, R.Range(420.0f, 560.0f)), R.Range(120.0f, 200.0f), R.Range(60.0f, 110.0f));
		}
		Box(M::Marker, FVector(C.X - 16.0f, C.Y - 16.0f, 0.0f), FVector(C.X + 16.0f, C.Y + 16.0f, 24.0f));
		Box(M::Vines, FVector(C.X + Radius + 18.0f, C.Y - 40.0f, 0.0f), FVector(C.X + Radius + 22.0f, C.Y + 40.0f, 60.0f));
		++HiddenRoomCount;
	}

	// Old shrine clearing at the end of the path.
	for (int32 Arch = 0; Arch < 3; ++Arch)
	{
		const float X = -5600.0f - Arch * 120.0f;
		Box(M::Concrete, FVector(X - 8.0f, -140.0f, 0.0f), FVector(X + 8.0f, -110.0f, 160.0f - Arch * 30.0f));
		Box(M::Concrete, FVector(X - 8.0f, 110.0f, 0.0f), FVector(X + 8.0f, 140.0f, 160.0f));
		if (Arch == 0)
		{
			Box(M::Concrete, FVector(X - 10.0f, -150.0f, 160.0f), FVector(X + 10.0f, 150.0f, 180.0f));
		}
	}
	Box(M::Concrete, FVector(-6000.0f, -60.0f, 0.0f), FVector(-5900.0f, 60.0f, 40.0f));
	Box(M::Marker, FVector(-5966.0f, -16.0f, 40.0f), FVector(-5934.0f, 16.0f, 64.0f));

	// Woodcutters' hamlet along the grove path.
	const TArray<FBox2D> HamletAvoid = {
		FBox2D(FVector2D(-5450.0f, -1120.0f), FVector2D(-4950.0f, -660.0f)),
		FBox2D(FVector2D(-4560.0f, 160.0f), FVector2D(-4040.0f, 700.0f)),
	};
	HouseRow(FVector2D(-5250.0f, 216.0f), FVector2D(380.0f, 0.0f), 7, 0, 280.0f, HF_Ivy, 2301u, HamletAvoid);
	HouseRow(FVector2D(-5250.0f, -216.0f), FVector2D(380.0f, 0.0f), 7, 2, 280.0f, HF_Ivy, 2302u, HamletAvoid);

	const TArray<FBox2D> HeartAvoid = {
		FBox2D(FVector2D(-6100.0f, -160.0f), FVector2D(-2440.0f, 160.0f)),
		FBox2D(FVector2D(-5420.0f, -1120.0f), FVector2D(-4980.0f, -680.0f)),
		FBox2D(FVector2D(-4560.0f, -1760.0f), FVector2D(-4240.0f, -1500.0f)),
		FBox2D(FVector2D(-3140.0f, 1300.0f), FVector2D(-2900.0f, 1600.0f)),
	};
	Scatter(FVector2D(-6380.0f, -1740.0f), FVector2D(-2460.0f, 1580.0f), 230.0f, 0.5f, HeartAvoid, 1504u);
}

void AHL2NeighborhoodsBlockout::BuildWindmillFields()
{
	using M = EHL2BlockoutMaterial;

	// Windmill Path from the hub's south-west opening.
	DirtPath(FVector2D(-640.0f, -2720.0f), FVector2D(-480.0f, -960.0f));
	DirtPath(FVector2D(-3600.0f, -2720.0f), FVector2D(-480.0f, -2560.0f));

	Ridge(FVector2D(-MapHalf, -3680.0f), FVector2D(-2800.0f, -3600.0f), 256.0f, 0);

	// L1: upgrade ledge in the grove's south wall (72 HU, one-way shortcut from inside).
	Box(M::Rubble, FVector(-4480.0f, -1840.0f, 0.0f), FVector(-4320.0f, -1760.0f, 72.0f));
	LedgeStripe(FVector(-4480.0f, -1842.0f, 66.0f), FVector(-4320.0f, -1840.0f, 72.0f));

	// Windmill on a stone platform; hidden room H6 is the crawl cellar inside the platform.
	HiddenRoom(M::Concrete, FVector(-4240.0f, -3040.0f, 0.0f), FVector(-3760.0f, -2560.0f, 96.0f), 2, -4000.0f);
	Stairs(M::Concrete, FVector(-3568.0f, -2880.0f, 0.0f), FIntPoint(-1, 0), 12, 8.0f, 16.0f, 160.0f);
	Box(M::Vines, FVector(-4060.0f, -3044.0f, 0.0f), FVector(-3940.0f, -3040.0f, 70.0f));
	const FVector Mill(-4000.0f, -2800.0f, 96.0f);
	Cylinder(M::Plaster, Mill, 120.0f, 240.0f);
	Cylinder(M::Plaster, Mill + FVector(0.0f, 0.0f, 240.0f), 105.0f, 200.0f);
	Cylinder(M::Plaster, Mill + FVector(0.0f, 0.0f, 440.0f), 90.0f, 160.0f);
	Cylinder(M::Wood, Mill + FVector(0.0f, 0.0f, 236.0f), 128.0f, 8.0f);
	const float CapR[] = { 100.0f, 80.0f, 58.0f, 34.0f, 12.0f };
	for (int32 Ring = 0; Ring < 5; ++Ring)
	{
		Cylinder(M::RoofRed, Mill + FVector(0.0f, 0.0f, 600.0f + Ring * 28.0f), CapR[Ring], 28.0f);
	}
	Box(M::Wood, Mill + FVector(-24.0f, -124.0f, 0.0f), Mill + FVector(24.0f, -118.0f, 90.0f));
	Box(M::Screen, Mill + FVector(-14.0f, -108.0f, 300.0f), Mill + FVector(14.0f, -104.0f, 340.0f));
	const FVector Hub = Mill + FVector(0.0f, -130.0f, 544.0f);
	Cylinder(M::Wood, Hub - FVector(0.0f, 0.0f, 10.0f), 14.0f, 20.0f);
	for (int32 Sail = 0; Sail < 4; ++Sail)
	{
		const float A = 20.0f + Sail * 90.0f;
		const float Rad = FMath::DegreesToRadians(A);
		OrientedBox(M::Wood, Hub + FVector(FMath::Cos(Rad) * 190.0f, -6.0f, FMath::Sin(Rad) * 190.0f), FVector(360.0f, 6.0f, 8.0f), FRotator(A, 0.0f, 0.0f));
		OrientedBox(M::Trim, Hub + FVector(FMath::Cos(Rad) * 220.0f, -10.0f, FMath::Sin(Rad) * 220.0f) + FVector(-FMath::Sin(Rad), 0.0f, FMath::Cos(Rad)) * 30.0f,
			FVector(280.0f, 2.0f, 56.0f), FRotator(A, 0.0f, 0.0f));
	}

	// Crop rows, haystacks, a scarecrow and fences.
	auto Field = [&](float X0, float X1, float Y0, float Y1)
	{
		for (float Y = Y0; Y + 60.0f < Y1; Y += 120.0f)
		{
			Box(M::Dirt, FVector(X0, Y, 0.0f), FVector(X1, Y + 60.0f, 6.0f));
			for (float X = X0 + 10.0f; X + 24.0f < X1; X += 44.0f)
			{
				Box(M::Foliage, FVector(X, Y + 18.0f, 6.0f), FVector(X + 24.0f, Y + 42.0f, 22.0f));
			}
		}
	};
	Field(-6200.0f, -4600.0f, -3500.0f, -2000.0f);
	Field(-3400.0f, -2000.0f, -3500.0f, -2800.0f);
	const FVector2D Hay[] = { FVector2D(-4400.0f, -2200.0f), FVector2D(-3300.0f, -2300.0f), FVector2D(-2200.0f, -2200.0f) };
	for (const FVector2D& P : Hay)
	{
		Cylinder(M::Dirt, FVector(P.X, P.Y, 0.0f), 50.0f, 60.0f);
		Cylinder(M::Dirt, FVector(P.X, P.Y, 60.0f), 34.0f, 24.0f);
	}
	Box(M::Wood, FVector(-5400.0f, -2800.0f, 6.0f), FVector(-5394.0f, -2794.0f, 130.0f));
	Box(M::Wood, FVector(-5440.0f, -2800.0f, 96.0f), FVector(-5354.0f, -2794.0f, 102.0f));
	Box(M::RoofRed, FVector(-5410.0f, -2806.0f, 130.0f), FVector(-5384.0f, -2788.0f, 150.0f));
	for (float X = -6200.0f; X < -2000.0f; X += 60.0f)
	{
		if (X > -4700.0f && X < -3400.0f)
		{
			continue;
		}
		Box(M::Wood, FVector(X, -1960.0f, 0.0f), FVector(X + 4.0f, -1956.0f, 30.0f));
	}

	const TArray<FBox2D> Avoid = {
		FBox2D(FVector2D(-6220.0f, -3520.0f), FVector2D(-4580.0f, -1980.0f)),
		FBox2D(FVector2D(-3420.0f, -3520.0f), FVector2D(-1980.0f, -2780.0f)),
		FBox2D(FVector2D(-4300.0f, -3100.0f), FVector2D(-3540.0f, -2500.0f)),
		FBox2D(FVector2D(-3640.0f, -2760.0f), FVector2D(-440.0f, -2520.0f)),
		FBox2D(FVector2D(-680.0f, -2760.0f), FVector2D(-440.0f, -940.0f)),
		FBox2D(FVector2D(-4560.0f, -1980.0f), FVector2D(-4240.0f, -1760.0f)),
		FBox2D(FVector2D(-260.0f, -3620.0f), FVector2D(260.0f, -760.0f)),
		FBox2D(FVector2D(-1000.0f, -1000.0f), FVector2D(1000.0f, -760.0f)),
	};
	Scatter(FVector2D(-6380.0f, -3580.0f), FVector2D(-260.0f, -1000.0f), 260.0f, 0.3f, Avoid, 1601u);
	// Open ground between the hub, Grove wall and Canal Walk.
	const TArray<FBox2D> HubAvoid = {
		FBox2D(FVector2D(-1000.0f, -1000.0f), FVector2D(1000.0f, 1000.0f)),
		FBox2D(FVector2D(-1300.0f, -140.0f), FVector2D(-760.0f, 140.0f)),
	};
	Scatter(FVector2D(-1180.0f, -980.0f), FVector2D(-980.0f, 1580.0f), 200.0f, 0.3f, HubAvoid, 1602u);
}

void AHL2NeighborhoodsBlockout::BuildSkyline()
{
	using M = EHL2BlockoutMaterial;

	// Far-off ruined towers swallowed by forest, visible above the boundary cliffs.
	struct FTower { float X, Y, W, H, Tilt; };
	const FTower Towers[] = {
		{ 8200.0f, 1800.0f, 800.0f, 2400.0f, 0.0f }, { 7800.0f, -2600.0f, 600.0f, 1700.0f, 6.0f },
		{ -8300.0f, -1400.0f, 900.0f, 2000.0f, -4.0f }, { -7900.0f, 3600.0f, 700.0f, 2800.0f, 0.0f },
		{ 1600.0f, 8400.0f, 1000.0f, 2200.0f, 3.0f }, { -3200.0f, 8100.0f, 700.0f, 1600.0f, 0.0f },
		{ 4200.0f, -8300.0f, 800.0f, 2600.0f, -5.0f }, { -5200.0f, -8000.0f, 600.0f, 1500.0f, 0.0f },
	};
	for (const FTower& T : Towers)
	{
		OrientedBox(M::Concrete, FVector(T.X, T.Y, T.H * 0.5f - 100.0f), FVector(T.W, T.W, T.H), FRotator(0.0f, 0.0f, T.Tilt));
		OrientedBox(M::Concrete, FVector(T.X + T.W * 0.2f, T.Y, T.H - 60.0f), FVector(T.W * 0.6f, T.W * 0.8f, 200.0f), FRotator(12.0f, 20.0f, T.Tilt));
		Cylinder(M::FoliageDark, FVector(T.X, T.Y, T.H - 140.0f), T.W * 0.6f, 120.0f);
		Box(M::Vines, FVector(T.X - T.W * 0.5f - 4.0f, T.Y - T.W * 0.3f, T.H * 0.3f), FVector(T.X - T.W * 0.5f, T.Y + T.W * 0.3f, T.H * 0.9f));
	}
	// A distant broken viaduct.
	for (int32 Pier = 0; Pier < 6; ++Pier)
	{
		const float X = -4000.0f + Pier * 1400.0f;
		Box(M::Concrete, FVector(X, 9000.0f, -100.0f), FVector(X + 200.0f, 9200.0f, 1000.0f));
		if (Pier != 3)
		{
			Box(M::Concrete, FVector(X, 9000.0f, 1000.0f), FVector(X + 1400.0f, 9200.0f, 1100.0f));
		}
	}
}
