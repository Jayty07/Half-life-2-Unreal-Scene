#include "HL2PointInsertionBlockout.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HL2PointInsertionBlockout)

namespace HL2Blockout
{
	/** Hammer unit -> Unreal centimetre (matches PBCharacterMovement's conversions). */
	constexpr float HU = 1.905f;

	constexpr int32 NumMaterials = static_cast<int32>(EHL2BlockoutMaterial::Count);

	/** Arrival track centre line and the train car the player starts in. */
	constexpr float ArrivalTrackY = -1850.0f;
	constexpr float StartCarX0 = 3272.0f;
	constexpr float StartCarX1 = 3912.0f;
	constexpr float TrainFloorZ = 48.0f;

	const TCHAR* MaterialNames[NumMaterials] = {
		TEXT("Concrete"), TEXT("DarkConcrete"), TEXT("Tile"), TEXT("Brick"), TEXT("Metal"),
		TEXT("Combine"), TEXT("Wood"), TEXT("Train"), TEXT("Screen"), TEXT("Trim"),
	};

	FORCEINLINE FVector ToCm(const FVector& V) { return V * HU; }
}

using namespace HL2Blockout;

AHL2PointInsertionBlockout::AHL2PointInsertionBlockout()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = CubeFinder.Object;
	CylinderMesh = CylinderFinder.Object;
	BaseMaterial = MaterialFinder.Object;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;

	auto MakeISM = [this](const FString& Name, UStaticMesh* Mesh)
	{
		UInstancedStaticMeshComponent* ISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(*Name);
		ISM->SetupAttachment(Root);
		ISM->SetMobility(EComponentMobility::Static);
		ISM->SetStaticMesh(Mesh);
		ISM->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		return ISM;
	};

	for (int32 Index = 0; Index < NumMaterials; ++Index)
	{
		CubeInstances.Add(MakeISM(FString::Printf(TEXT("Boxes_%s"), MaterialNames[Index]), CubeMesh));
		CylinderInstances.Add(MakeISM(FString::Printf(TEXT("Cylinders_%s"), MaterialNames[Index]), CylinderMesh));
	}

	// City 17 palette: desaturated concrete, rust brick, Combine blue-black.
	Palette = {
		FLinearColor(0.32f, 0.32f, 0.30f), // Concrete
		FLinearColor(0.10f, 0.10f, 0.10f), // DarkConcrete
		FLinearColor(0.42f, 0.45f, 0.40f), // Tile
		FLinearColor(0.28f, 0.14f, 0.09f), // Brick
		FLinearColor(0.18f, 0.20f, 0.22f), // Metal
		FLinearColor(0.04f, 0.06f, 0.09f), // Combine
		FLinearColor(0.33f, 0.20f, 0.09f), // Wood
		FLinearColor(0.12f, 0.22f, 0.19f), // Train
		FLinearColor(0.08f, 0.30f, 0.45f), // Screen
		FLinearColor(0.80f, 0.60f, 0.05f), // Trim
	};

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetRelativeRotation(FRotator(-38.0f, 40.0f, 0.0f));
	Sun->Intensity = 5.0f;
	Sun->LightColor = FColor(235, 240, 255);
	Sun->SetAtmosphereSunLight(true);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;

	SkyAtmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
	SkyAtmosphere->SetupAttachment(Root);

	HeightFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	HeightFog->SetupAttachment(Root);
	HeightFog->FogDensity = 0.03f;
	HeightFog->FogHeightFalloff = 0.1f;
}

void AHL2PointInsertionBlockout::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildBlockout();
}

void AHL2PointInsertionBlockout::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	ApplyPalette();
	ApplyLightingVisibility();
}

void AHL2PointInsertionBlockout::BeginPlay()
{
	Super::BeginPlay();

	if (bSpawnPhysicsProps && HasAuthority())
	{
		SpawnPhysicsProps();
	}
}

void AHL2PointInsertionBlockout::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (AActor* Prop : SpawnedProps)
	{
		if (IsValid(Prop))
		{
			Prop->Destroy();
		}
	}
	SpawnedProps.Reset();

	Super::EndPlay(EndPlayReason);
}

FTransform AHL2PointInsertionBlockout::GetPlayerStartTransform() const
{
	// Standing in the middle car of the arriving train, facing its door onto the platform (-Y).
	// PlayerStart pivots sit at the centre of a 92 cm half-height capsule.
	const FVector Local((StartCarX0 + StartCarX1) * 0.5f * HU, ArrivalTrackY * HU, TrainFloorZ * HU + 94.0f);
	return FTransform(FRotator(0.0f, -90.0f, 0.0f), Local) * GetActorTransform();
}

void AHL2PointInsertionBlockout::ClearInstances()
{
	for (UInstancedStaticMeshComponent* ISM : CubeInstances)
	{
		if (ISM) { ISM->ClearInstances(); }
	}
	for (UInstancedStaticMeshComponent* ISM : CylinderInstances)
	{
		if (ISM) { ISM->ClearInstances(); }
	}
}

void AHL2PointInsertionBlockout::RebuildBlockout()
{
	ClearInstances();

	BuildWorldBounds();
	BuildRailYard();
	BuildArrivalPlatform();
	BuildTrainShed();
	BuildSecurity();
	BuildStationHall();
	BuildPlaza();
	BuildStreet();
	BuildApartments();
	BuildRooftops();
	BuildAtticAndEnd();
	BuildSkyline();

	ApplyPalette();
	ApplyLightingVisibility();
}

void AHL2PointInsertionBlockout::ApplyPalette()
{
	if (HasAnyFlags(RF_ClassDefaultObject) || !BaseMaterial)
	{
		return;
	}

	Palette.SetNum(NumMaterials);
	PaletteMaterials.SetNum(NumMaterials);

	for (int32 Index = 0; Index < NumMaterials; ++Index)
	{
		UMaterialInstanceDynamic* MID = PaletteMaterials[Index];
		if (!MID)
		{
			MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
			PaletteMaterials[Index] = MID;
		}
		MID->SetVectorParameterValue(TEXT("Color"), Palette[Index]);
		MID->SetVectorParameterValue(TEXT("BaseColor"), Palette[Index]);

		if (CubeInstances.IsValidIndex(Index) && CubeInstances[Index])
		{
			CubeInstances[Index]->SetMaterial(0, MID);
		}
		if (CylinderInstances.IsValidIndex(Index) && CylinderInstances[Index])
		{
			CylinderInstances[Index]->SetMaterial(0, MID);
		}
	}
}

void AHL2PointInsertionBlockout::ApplyLightingVisibility()
{
	for (USceneComponent* Component : TArray<USceneComponent*>{ Sun, SkyLight, SkyAtmosphere, HeightFog })
	{
		if (Component)
		{
			Component->SetVisibility(bIncludeSkyAndLighting);
		}
	}
}

// ---------------------------------------------------------------------------
// Geometry helpers (all inputs in Hammer units, local to the actor)
// ---------------------------------------------------------------------------

void AHL2PointInsertionBlockout::Box(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU)
{
	const FVector Size = MaxHU - MinHU;
	if (Size.X <= UE_KINDA_SMALL_NUMBER || Size.Y <= UE_KINDA_SMALL_NUMBER || Size.Z <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	const int32 Index = static_cast<int32>(Mat);
	if (!CubeInstances.IsValidIndex(Index) || !CubeInstances[Index])
	{
		return;
	}

	// Engine cube is 100 cm with a centred pivot.
	CubeInstances[Index]->AddInstance(FTransform(FQuat::Identity, ToCm((MinHU + MaxHU) * 0.5f), ToCm(Size) / 100.0f));
}

void AHL2PointInsertionBlockout::Cylinder(EHL2BlockoutMaterial Mat, const FVector& BaseCenterHU, float RadiusHU, float HeightHU)
{
	const int32 Index = static_cast<int32>(Mat);
	if (RadiusHU <= 0.0f || HeightHU <= 0.0f || !CylinderInstances.IsValidIndex(Index) || !CylinderInstances[Index])
	{
		return;
	}

	// Engine cylinder is 100 cm wide/tall with a centred pivot.
	const FVector Center = BaseCenterHU + FVector(0.0f, 0.0f, HeightHU * 0.5f);
	const FVector Scale = ToCm(FVector(RadiusHU * 2.0f, RadiusHU * 2.0f, HeightHU)) / 100.0f;
	CylinderInstances[Index]->AddInstance(FTransform(FQuat::Identity, ToCm(Center), Scale));
}

void AHL2PointInsertionBlockout::WallWithOpenings(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, int32 ThinAxis, const TArray<FBox2D>& Openings)
{
	// U runs along the wall horizontally, V is height.
	const int32 UAxis = ThinAxis == 0 ? 1 : 0;
	const double U0 = MinHU[UAxis];
	const double U1 = MaxHU[UAxis];
	const double V0 = MinHU.Z;
	const double V1 = MaxHU.Z;

	TArray<double> Cuts = { U0, U1 };
	for (const FBox2D& Opening : Openings)
	{
		Cuts.Add(FMath::Clamp(Opening.Min.X, U0, U1));
		Cuts.Add(FMath::Clamp(Opening.Max.X, U0, U1));
	}
	Cuts.Sort();

	for (int32 CutIndex = 0; CutIndex + 1 < Cuts.Num(); ++CutIndex)
	{
		const double A = Cuts[CutIndex];
		const double B = Cuts[CutIndex + 1];
		if (B - A <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const double Mid = (A + B) * 0.5f;

		// Height ranges removed from this strip.
		TArray<FVector2D> Holes;
		for (const FBox2D& Opening : Openings)
		{
			if (Mid > Opening.Min.X && Mid < Opening.Max.X)
			{
				Holes.Add(FVector2D(FMath::Max(Opening.Min.Y, V0), FMath::Min(Opening.Max.Y, V1)));
			}
		}
		Holes.Sort([](const FVector2D& L, const FVector2D& R) { return L.X < R.X; });

		double Cursor = V0;
		auto Emit = [&](double Bottom, double Top)
		{
			if (Top - Bottom <= UE_KINDA_SMALL_NUMBER)
			{
				return;
			}
			FVector Min = MinHU;
			FVector Max = MaxHU;
			Min[UAxis] = A;
			Max[UAxis] = B;
			Min.Z = Bottom;
			Max.Z = Top;
			Box(Mat, Min, Max);
		};

		for (const FVector2D& Hole : Holes)
		{
			Emit(Cursor, Hole.X);
			Cursor = FMath::Max(Cursor, Hole.Y);
		}
		Emit(Cursor, V1);
	}
}

void AHL2PointInsertionBlockout::SlabWithHole(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, const FBox2D& HoleXY)
{
	const double HX0 = FMath::Clamp(HoleXY.Min.X, MinHU.X, MaxHU.X);
	const double HX1 = FMath::Clamp(HoleXY.Max.X, MinHU.X, MaxHU.X);
	const double HY0 = FMath::Clamp(HoleXY.Min.Y, MinHU.Y, MaxHU.Y);
	const double HY1 = FMath::Clamp(HoleXY.Max.Y, MinHU.Y, MaxHU.Y);

	Box(Mat, MinHU, FVector(HX0, MaxHU.Y, MaxHU.Z));
	Box(Mat, FVector(HX1, MinHU.Y, MinHU.Z), MaxHU);
	Box(Mat, FVector(HX0, MinHU.Y, MinHU.Z), FVector(HX1, HY0, MaxHU.Z));
	Box(Mat, FVector(HX0, HY1, MinHU.Z), FVector(HX1, MaxHU.Y, MaxHU.Z));
}

void AHL2PointInsertionBlockout::Stairs(EHL2BlockoutMaterial Mat, const FVector& StartHU, const FIntPoint& Direction, int32 NumSteps, float RiseHU, float RunHU, float WidthHU)
{
	for (int32 Step = 0; Step < NumSteps; ++Step)
	{
		const float Near = Step * RunHU;
		const float Far = (Step + 1) * RunHU;
		const float Top = StartHU.Z + (Step + 1) * RiseHU;

		FVector Min = StartHU;
		FVector Max = StartHU;
		Max.Z = Top;

		if (Direction.X != 0)
		{
			const float A = StartHU.X + Direction.X * Near;
			const float B = StartHU.X + Direction.X * Far;
			Min.X = FMath::Min(A, B);
			Max.X = FMath::Max(A, B);
			Max.Y = StartHU.Y + WidthHU;
		}
		else
		{
			const float A = StartHU.Y + Direction.Y * Near;
			const float B = StartHU.Y + Direction.Y * Far;
			Min.Y = FMath::Min(A, B);
			Max.Y = FMath::Max(A, B);
			Max.X = StartHU.X + WidthHU;
		}

		Box(Mat, Min, Max);
	}
}

void AHL2PointInsertionBlockout::Track(float X0, float X1, float CenterY)
{
	using M = EHL2BlockoutMaterial;

	for (float X = X0 + 8.0f; X < X1 - 8.0f; X += 64.0f)
	{
		Box(M::Wood, FVector(X, CenterY - 48.0f, 0.0f), FVector(X + 16.0f, CenterY + 48.0f, 3.0f));
	}
	Box(M::Metal, FVector(X0, CenterY - 30.0f, 0.0f), FVector(X1, CenterY - 26.0f, 7.0f));
	Box(M::Metal, FVector(X0, CenterY + 26.0f, 0.0f), FVector(X1, CenterY + 30.0f, 7.0f));
}

void AHL2PointInsertionBlockout::Vault(EHL2BlockoutMaterial Mat, float X0, float X1, float Y0, float Y1, float BaseZ, float RiseHU, int32 Segments, float ThicknessHU, int32 OpenTopSegments)
{
	const double CenterY = (Y0 + Y1) * 0.5;
	const double Radius = (Y1 - Y0) * 0.5;
	const int32 OpenBegin = (Segments - OpenTopSegments) / 2;
	const int32 OpenEnd = OpenBegin + OpenTopSegments;

	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		if (OpenTopSegments > 0 && Segment >= OpenBegin && Segment < OpenEnd)
		{
			continue;
		}

		const double T0 = UE_DOUBLE_PI * Segment / Segments;
		const double T1 = UE_DOUBLE_PI * (Segment + 1) / Segments;
		const double YA = CenterY - Radius * FMath::Cos(T0);
		const double YB = CenterY - Radius * FMath::Cos(T1);
		const double ZA = BaseZ + RiseHU * FMath::Sin(T0);
		const double ZB = BaseZ + RiseHU * FMath::Sin(T1);

		Box(Mat,
			FVector(X0, FMath::Min(YA, YB), FMath::Min(ZA, ZB)),
			FVector(X1, FMath::Max(YA, YB), FMath::Max(ZA, ZB) + ThicknessHU));
	}
}

void AHL2PointInsertionBlockout::GableRoof(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, int32 RidgeAxis, int32 Steps)
{
	const int32 Across = RidgeAxis == 0 ? 1 : 0;
	const double HalfSpan = (MaxHU[Across] - MinHU[Across]) * 0.5;
	const double StepRise = (MaxHU.Z - MinHU.Z) / Steps;

	for (int32 Step = 0; Step < Steps; ++Step)
	{
		const double Inset = HalfSpan * Step / Steps;
		FVector Min = MinHU;
		FVector Max = MaxHU;
		Min[Across] += Inset;
		Max[Across] -= Inset;
		Min.Z = MinHU.Z + StepRise * Step;
		Max.Z = Min.Z + StepRise;
		Box(Mat, Min, Max);
	}
}

void AHL2PointInsertionBlockout::FacadeWindows(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, int32 ThinAxis, float SpacingHU, float FloorHeightHU, float WidthHU, float HeightHU)
{
	const int32 UAxis = ThinAxis == 0 ? 1 : 0;
	const double Length = MaxHU[UAxis] - MinHU[UAxis];
	const int32 Columns = FMath::Max(1, FMath::FloorToInt32(Length / SpacingHU));
	const double Margin = (Length - Columns * SpacingHU) * 0.5 + (SpacingHU - WidthHU) * 0.5;

	for (double FloorZ = MinHU.Z + FloorHeightHU * 0.35; FloorZ + HeightHU <= MaxHU.Z; FloorZ += FloorHeightHU)
	{
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			FVector Min = MinHU;
			FVector Max = MaxHU;
			Min[UAxis] = MinHU[UAxis] + Margin + Column * SpacingHU;
			Max[UAxis] = Min[UAxis] + WidthHU;
			Min.Z = FloorZ;
			Max.Z = FloorZ + HeightHU;
			Box(Mat, Min, Max);
		}
	}
}

// ---------------------------------------------------------------------------
// Layout, following the Point Insertion overview map.
// +X runs from the rail yard towards the plaza, +Y from the tracks towards
// the apartments and rooftops. Units: Hammer units. Doors are 64-80 wide and
// 112 tall; stairs are 8 rise / 12 run (PB max step height is 18).
//
//  Start (train yard) -> arrival platform under the long canopy -> security
//  queue + interrogation room -> station hall -> City 17 plaza -> street ->
//  alley -> courtyard -> Resistance apartments -> rooftops -> attic -> End.
// ---------------------------------------------------------------------------

void AHL2PointInsertionBlockout::BuildWorldBounds()
{
	using M = EHL2BlockoutMaterial;

	const float X0 = -1600.0f, X1 = 11200.0f, Y0 = -2800.0f, Y1 = 4400.0f, Height = 1536.0f;

	Box(M::DarkConcrete, FVector(X0, Y0, -64.0f), FVector(X1, Y1, 0.0f));
	Box(M::DarkConcrete, FVector(X0 - 16.0f, Y0 - 16.0f, 0.0f), FVector(X0, Y1 + 16.0f, Height));
	Box(M::DarkConcrete, FVector(X1, Y0 - 16.0f, 0.0f), FVector(X1 + 16.0f, Y1 + 16.0f, Height));
	Box(M::DarkConcrete, FVector(X0, Y0 - 16.0f, 0.0f), FVector(X1, Y0, Height));
	Box(M::DarkConcrete, FVector(X0, Y1, 0.0f), FVector(X1, Y1 + 16.0f, Height));
}

void AHL2PointInsertionBlockout::BuildRailYard()
{
	using M = EHL2BlockoutMaterial;

	// Tracks: the arrival line runs past the long canopy, the rest run into the arched train shed.
	const float YardTracks[] = { -640.0f, -320.0f, 0.0f, 320.0f, 640.0f };
	Track(-1400.0f, 5200.0f, ArrivalTrackY);
	for (const float Y : YardTracks)
	{
		Track(-1400.0f, FMath::IsNearlyZero(Y) ? 3000.0f : 5400.0f, Y);
	}
	Box(M::Metal, FVector(5160.0f, ArrivalTrackY - 50.0f, 0.0f), FVector(5200.0f, ArrivalTrackY + 50.0f, 64.0f));
	Box(M::Metal, FVector(2960.0f, -50.0f, 0.0f), FVector(3000.0f, 50.0f, 64.0f));

	// Tunnel portal at the far end where the train comes in ("Start").
	TArray<FBox2D> Portals = { FBox2D(FVector2D(ArrivalTrackY - 80.0f, 0.0f), FVector2D(ArrivalTrackY + 80.0f, 256.0f)) };
	for (const float Y : YardTracks)
	{
		Portals.Add(FBox2D(FVector2D(Y - 80.0f, 0.0f), FVector2D(Y + 80.0f, 256.0f)));
	}
	WallWithOpenings(M::Concrete, FVector(-1416.0f, -2272.0f, 0.0f), FVector(-1400.0f, 1016.0f, 640.0f), 0, Portals);

	// Yard boundaries: retaining wall behind the arrival line, brick wall on the far side.
	Box(M::Concrete, FVector(-1400.0f, -2272.0f, 0.0f), FVector(2400.0f, -2256.0f, 320.0f));
	Box(M::Brick, FVector(-1400.0f, 1000.0f, 0.0f), FVector(3200.0f, 1016.0f, 320.0f));
	Box(M::Metal, FVector(-1400.0f, -1652.0f, 0.0f), FVector(3440.0f, -1648.0f, 96.0f)); // fence

	// Tall tenement and the arched warehouse between the arrival line and the yard.
	Box(M::Brick, FVector(400.0f, -1560.0f, 0.0f), FVector(1000.0f, -900.0f, 1400.0f));
	FacadeWindows(M::DarkConcrete, FVector(400.0f, -1564.0f, 0.0f), FVector(1000.0f, -1560.0f, 1400.0f), 1, 100.0f, 128.0f, 48.0f, 72.0f);
	FacadeWindows(M::DarkConcrete, FVector(1000.0f, -1560.0f, 0.0f), FVector(1004.0f, -900.0f, 1400.0f), 0, 100.0f, 128.0f, 48.0f, 72.0f);
	Box(M::Brick, FVector(1700.0f, -1560.0f, 0.0f), FVector(2700.0f, -900.0f, 256.0f));
	Vault(M::Metal, 1700.0f, 2700.0f, -1560.0f, -900.0f, 256.0f, 192.0f, 10, 12.0f, 0);
	FacadeWindows(M::Tile, FVector(1700.0f, -1564.0f, 0.0f), FVector(2700.0f, -1560.0f, 256.0f), 1, 125.0f, 256.0f, 64.0f, 112.0f);

	// Signal gantry over the yard and the signal box.
	Box(M::Metal, FVector(1200.0f, -2240.0f, 0.0f), FVector(1224.0f, -2216.0f, 336.0f));
	Box(M::Metal, FVector(1200.0f, 960.0f, 0.0f), FVector(1224.0f, 984.0f, 336.0f));
	Box(M::Metal, FVector(1200.0f, -2240.0f, 320.0f), FVector(1224.0f, 984.0f, 336.0f));
	for (const float Y : { -1850.0f, -640.0f, -320.0f, 0.0f, 320.0f, 640.0f })
	{
		Box(M::Screen, FVector(1194.0f, Y - 12.0f, 280.0f), FVector(1200.0f, Y + 12.0f, 316.0f));
	}
	Box(M::Brick, FVector(2000.0f, 780.0f, 0.0f), FVector(2160.0f, 940.0f, 256.0f));
	Box(M::Tile, FVector(1984.0f, 764.0f, 256.0f), FVector(2176.0f, 956.0f, 352.0f));
	Box(M::Concrete, FVector(1976.0f, 756.0f, 352.0f), FVector(2184.0f, 964.0f, 368.0f));

	// Parked freight on a yard track.
	Box(M::Train, FVector(-600.0f, -690.0f, 16.0f), FVector(1400.0f, -590.0f, 176.0f));
	Box(M::Metal, FVector(200.0f, 270.0f, 16.0f), FVector(1100.0f, 370.0f, 150.0f));
}

void AHL2PointInsertionBlockout::BuildArrivalPlatform()
{
	using M = EHL2BlockoutMaterial;

	// Platform (z 48) with the retaining wall behind it and a fence at the yard end.
	Box(M::Concrete, FVector(2400.0f, -2256.0f, 0.0f), FVector(5216.0f, -1920.0f, 48.0f));
	Box(M::Trim, FVector(2400.0f, -1936.0f, 48.0f), FVector(5216.0f, -1920.0f, 49.0f));
	Box(M::Concrete, FVector(2400.0f, -2272.0f, 0.0f), FVector(5216.0f, -2256.0f, 320.0f));
	Box(M::Metal, FVector(2400.0f, -2256.0f, 48.0f), FVector(2404.0f, -1936.0f, 96.0f));

	// The long pitched canopy from the overview map.
	for (float X = 2560.0f; X < 5216.0f; X += 320.0f)
	{
		Cylinder(M::Metal, FVector(X, -2112.0f, 48.0f), 8.0f, 272.0f);
	}
	Box(M::Concrete, FVector(2384.0f, -2288.0f, 320.0f), FVector(5216.0f, -1744.0f, 336.0f));
	GableRoof(M::Wood, FVector(2384.0f, -2288.0f, 336.0f), FVector(5216.0f, -1744.0f, 432.0f), 0, 6);

	// Benches, bins and Breencast screens along the back wall.
	for (float X = 2600.0f; X < 5000.0f; X += 480.0f)
	{
		Box(M::Wood, FVector(X, -2240.0f, 48.0f), FVector(X + 96.0f, -2208.0f, 66.0f));
		Cylinder(M::Metal, FVector(X + 140.0f, -2236.0f, 48.0f), 12.0f, 36.0f);
	}
	for (const float X : { 3000.0f, 4200.0f })
	{
		Box(M::Metal, FVector(X - 8.0f, -2256.0f, 168.0f), FVector(X + 264.0f, -2248.0f, 280.0f));
		Box(M::Screen, FVector(X, -2248.0f, 176.0f), FVector(X + 256.0f, -2244.0f, 272.0f));
	}

	// --- Arriving train. The player starts in the middle car; its door faces the platform (-Y). ---
	const float Y0 = ArrivalTrackY - 64.0f;
	const float Y1 = ArrivalTrackY + 64.0f;
	const float Roof = 176.0f;

	// Lead car, locomotive and trailing car.
	Box(M::Train, FVector(2600.0f, Y0, 16.0f), FVector(3240.0f, Y1, Roof + 8.0f));
	Box(M::Train, FVector(3944.0f, Y0, 16.0f), FVector(4584.0f, Y1, Roof + 8.0f));
	Box(M::Train, FVector(4616.0f, Y0, 16.0f), FVector(5100.0f, Y1, Roof + 40.0f));
	Box(M::Screen, FVector(5100.0f, Y0 + 16.0f, 120.0f), FVector(5104.0f, Y1 - 16.0f, 180.0f));
	for (const float X : { 2600.0f, 3944.0f })
	{
		Box(M::Screen, FVector(X + 40.0f, Y0 - 2.0f, 96.0f), FVector(X + 600.0f, Y0, 144.0f));
		Box(M::Screen, FVector(X + 40.0f, Y1, 96.0f), FVector(X + 600.0f, Y1 + 2.0f, 144.0f));
	}

	// Start car, fully modelled.
	const float X0 = StartCarX0;
	const float X1 = StartCarX1;
	const float DoorX = (X0 + X1) * 0.5f;
	Box(M::Train, FVector(X0, Y0, 16.0f), FVector(X1, Y1, TrainFloorZ));
	for (const float X : { X0 + 48.0f, X1 - 144.0f })
	{
		Box(M::DarkConcrete, FVector(X, Y0 + 10.0f, 7.0f), FVector(X + 96.0f, Y1 - 10.0f, 16.0f));
	}

	const TArray<FBox2D> Windows = {
		FBox2D(FVector2D(X0 + 64.0f, 96.0f), FVector2D(DoorX - 96.0f, 144.0f)),
		FBox2D(FVector2D(DoorX + 96.0f, 96.0f), FVector2D(X1 - 64.0f, 144.0f)),
	};
	TArray<FBox2D> PlatformSide = Windows;
	PlatformSide.Add(FBox2D(FVector2D(DoorX - 40.0f, TrainFloorZ), FVector2D(DoorX + 40.0f, 160.0f)));

	WallWithOpenings(M::Train, FVector(X0, Y0, TrainFloorZ), FVector(X1, Y0 + 6.0f, Roof), 1, PlatformSide);
	WallWithOpenings(M::Train, FVector(X0, Y1 - 6.0f, TrainFloorZ), FVector(X1, Y1, Roof), 1, Windows);
	Box(M::Train, FVector(X0, Y0, TrainFloorZ), FVector(X0 + 6.0f, Y1, Roof));
	Box(M::Train, FVector(X1 - 6.0f, Y0, TrainFloorZ), FVector(X1, Y1, Roof));
	Box(M::Train, FVector(X0, Y0, Roof), FVector(X1, Y1, Roof + 8.0f));

	for (const float X : { X0 + 24.0f, X0 + 168.0f, X1 - 264.0f, X1 - 120.0f })
	{
		Box(M::Wood, FVector(X, Y1 - 38.0f, TrainFloorZ), FVector(X + 96.0f, Y1 - 6.0f, TrainFloorZ + 18.0f));
		Box(M::Wood, FVector(X, Y1 - 14.0f, TrainFloorZ + 18.0f), FVector(X + 96.0f, Y1 - 6.0f, TrainFloorZ + 52.0f));
	}
	for (const float X : { X0 + 40.0f, X1 - 168.0f })
	{
		Box(M::Wood, FVector(X, Y0 + 6.0f, TrainFloorZ), FVector(X + 128.0f, Y0 + 38.0f, TrainFloorZ + 18.0f));
	}
}

void AHL2PointInsertionBlockout::BuildTrainShed()
{
	using M = EHL2BlockoutMaterial;

	const float X0 = 3200.0f, X1 = 5440.0f, Y0 = -992.0f, Y1 = 992.0f, WallTop = 384.0f;

	Box(M::Brick, FVector(X0, Y0, 0.0f), FVector(X1, Y0 + 16.0f, WallTop));
	Box(M::Brick, FVector(X0, Y1 - 16.0f, 0.0f), FVector(X1, Y1, WallTop));
	Box(M::Concrete, FVector(X1, Y0, 0.0f), FVector(X1 + 16.0f, Y1, WallTop + 320.0f));

	// Glazed barrel roof (open ridge as a skylight) with ribs every 320.
	Vault(M::Metal, X0, X1, Y0, Y1, WallTop, 320.0f, 16, 6.0f, 4);
	for (float X = X0; X <= X1 - 16.0f; X += 320.0f)
	{
		Vault(M::DarkConcrete, X, X + 16.0f, Y0, Y1, WallTop, 320.0f, 16, 16.0f, 0);
	}

	// Side platforms and the island platform with its lamp columns.
	Box(M::Concrete, FVector(3300.0f, Y0 + 16.0f, 0.0f), FVector(X1, -688.0f, 48.0f));
	Box(M::Concrete, FVector(3300.0f, -272.0f, 0.0f), FVector(X1, 272.0f, 48.0f));
	Box(M::Concrete, FVector(3300.0f, 688.0f, 0.0f), FVector(X1, Y1 - 16.0f, 48.0f));
	for (const float Y : { -704.0f, -272.0f, 256.0f, 688.0f })
	{
		Box(M::Trim, FVector(3300.0f, Y, 48.0f), FVector(X1, Y + 16.0f, 49.0f));
	}
	for (float X = 3500.0f; X < X1; X += 448.0f)
	{
		Cylinder(M::Metal, FVector(X, 0.0f, 48.0f), 10.0f, 336.0f);
		Box(M::Wood, FVector(X + 120.0f, -120.0f, 48.0f), FVector(X + 216.0f, -88.0f, 66.0f));
	}

	// Parked trains.
	Box(M::Train, FVector(3400.0f, -384.0f, 16.0f), FVector(5000.0f, -256.0f, 184.0f));
	Box(M::Train, FVector(3700.0f, 576.0f, 16.0f), FVector(5300.0f, 704.0f, 184.0f));
	Box(M::Screen, FVector(3700.0f, 574.0f, 96.0f), FVector(5300.0f, 576.0f, 144.0f));

	// Low building with the rows of skylights between the shed and the arrival canopy.
	Box(M::Concrete, FVector(3440.0f, -1600.0f, 0.0f), FVector(5200.0f, Y0, 256.0f));
	for (int32 Column = 0; Column < 4; ++Column)
	{
		for (int32 Row = 0; Row < 2; ++Row)
		{
			const float SX = 3520.0f + Column * 420.0f;
			const float SY = -1560.0f + Row * 290.0f;
			GableRoof(M::Tile, FVector(SX, SY, 256.0f), FVector(SX + 320.0f, SY + 240.0f, 320.0f), 1, 4);
		}
	}
}

void AHL2PointInsertionBlockout::BuildSecurity()
{
	using M = EHL2BlockoutMaterial;

	const float X0 = 5216.0f, X1 = 5840.0f, Y0 = -2272.0f, Y1 = -1344.0f;
	const float Floor = 48.0f, Ceiling = 304.0f;

	Box(M::Tile, FVector(X0, Y0, 0.0f), FVector(X1, Y1, Floor));
	Box(M::Concrete, FVector(X0, Y0, Ceiling), FVector(X1, Y1, Ceiling + 16.0f));
	Box(M::Concrete, FVector(X0, Y0, 0.0f), FVector(X1, Y0 + 16.0f, Ceiling));
	Box(M::Concrete, FVector(X1 - 16.0f, Y0 + 16.0f, Floor), FVector(X1, Y1, Ceiling));

	// West wall: entrance from the platform.
	WallWithOpenings(M::Concrete, FVector(X0, Y0 + 16.0f, Floor), FVector(X0 + 16.0f, Y1, Ceiling), 0,
		{ FBox2D(FVector2D(-2160.0f, Floor), FVector2D(-2064.0f, 160.0f)) });

	// North wall: exit to the corridor that leads to the station hall.
	WallWithOpenings(M::Concrete, FVector(X0 + 16.0f, Y1 - 16.0f, Floor), FVector(X1 - 16.0f, Y1, Ceiling), 1,
		{ FBox2D(FVector2D(5700.0f, Floor), FVector2D(5780.0f, 160.0f)) });

	// Divider between the queue hall and the back rooms.
	WallWithOpenings(M::Concrete, FVector(X0 + 16.0f, -1936.0f, Floor), FVector(X1 - 16.0f, -1920.0f, Ceiling), 1,
		{ FBox2D(FVector2D(5712.0f, Floor), FVector2D(5792.0f, 160.0f)) });

	// Queue railings.
	for (const float Y : { -2176.0f, -2048.0f })
	{
		Box(M::Metal, FVector(5260.0f, Y, Floor), FVector(5560.0f, Y + 4.0f, Floor + 40.0f));
	}

	// Combine barrier with the scanner gate.
	WallWithOpenings(M::Combine, FVector(5600.0f, Y0 + 16.0f, Floor), FVector(5616.0f, -1936.0f, Ceiling), 0,
		{ FBox2D(FVector2D(-2144.0f, Floor), FVector2D(-2064.0f, 160.0f)) });
	Box(M::Trim, FVector(5596.0f, -2152.0f, Floor), FVector(5620.0f, -2144.0f, 168.0f));
	Box(M::Trim, FVector(5596.0f, -2064.0f, Floor), FVector(5620.0f, -2056.0f, 168.0f));
	Box(M::Trim, FVector(5596.0f, -2152.0f, 160.0f), FVector(5620.0f, -2056.0f, 168.0f));
	Box(M::Screen, FVector(5300.0f, Y0 + 16.0f, 176.0f), FVector(5500.0f, Y0 + 20.0f, 272.0f));
	Box(M::Metal, FVector(5660.0f, -2240.0f, Floor), FVector(5760.0f, -2200.0f, Floor + 40.0f)); // CP desk

	// Interrogation room (west of the back corridor).
	WallWithOpenings(M::Concrete, FVector(5504.0f, -1920.0f, Floor), FVector(5520.0f, Y1 - 16.0f, Ceiling), 0,
		{ FBox2D(FVector2D(-1700.0f, Floor), FVector2D(-1620.0f, 160.0f)) });
	Box(M::Metal, FVector(5320.0f, -1700.0f, Floor), FVector(5420.0f, -1640.0f, Floor + 32.0f));
	Box(M::Wood, FVector(5340.0f, -1620.0f, Floor), FVector(5364.0f, -1596.0f, Floor + 18.0f));
	Box(M::Screen, FVector(5232.0f, -1760.0f, 120.0f), FVector(5236.0f, -1560.0f, 200.0f));

	// Corridor north to the station hall.
	Box(M::Tile, FVector(5664.0f, Y1, 0.0f), FVector(5816.0f, -716.0f, Floor));
	Box(M::Concrete, FVector(5664.0f, Y1, 208.0f), FVector(5816.0f, -716.0f, 224.0f));
	Box(M::Concrete, FVector(5664.0f, Y1, Floor), FVector(5680.0f, -716.0f, 208.0f));
	Box(M::Concrete, FVector(5800.0f, Y1, Floor), FVector(5816.0f, -716.0f, 208.0f));
}

void AHL2PointInsertionBlockout::BuildStationHall()
{
	using M = EHL2BlockoutMaterial;

	const float X0 = 5584.0f, X1 = 6800.0f, Y0 = -716.0f, Y1 = 900.0f;
	const float Floor = 48.0f, WallTop = 640.0f;

	Box(M::Tile, FVector(X0, Y0, 0.0f), FVector(X1, Y1, Floor));
	WallWithOpenings(M::Concrete, FVector(X0, Y0, Floor), FVector(X1, Y0 + 16.0f, WallTop), 1,
		{ FBox2D(FVector2D(5700.0f, Floor), FVector2D(5780.0f, 160.0f)) });
	Box(M::Concrete, FVector(X0, Y1 - 16.0f, Floor), FVector(X1, Y1, WallTop));
	Box(M::Concrete, FVector(X0, Y0 + 16.0f, Floor), FVector(X0 + 16.0f, Y1 - 16.0f, WallTop));

	// East facade facing the plaza: doors under the great arched window.
	const float WinY0 = -400.0f, WinY1 = 560.0f, WinZ0 = 240.0f, WinZ1 = 600.0f;
	WallWithOpenings(M::Concrete, FVector(X1 - 16.0f, Y0 + 16.0f, Floor), FVector(X1, Y1 - 16.0f, WallTop), 0,
		{
			FBox2D(FVector2D(60.0f, Floor), FVector2D(260.0f, 176.0f)),
			FBox2D(FVector2D(WinY0, WinZ0), FVector2D(WinY1, WinZ1)),
		});
	for (float Y = WinY0 + 120.0f; Y < WinY1; Y += 120.0f)
	{
		Box(M::Trim, FVector(X1 - 12.0f, Y - 4.0f, WinZ0), FVector(X1 - 4.0f, Y + 4.0f, WinZ1));
	}
	Box(M::Trim, FVector(X1 - 12.0f, WinY0, 416.0f), FVector(X1 - 4.0f, WinY1, 424.0f));

	// Arched roof with a skylight strip.
	Vault(M::Metal, X0, X1, Y0, Y1, WallTop, 240.0f, 14, 8.0f, 4);
	for (float X = X0; X <= X1 - 16.0f; X += 304.0f)
	{
		Vault(M::DarkConcrete, X, X + 16.0f, Y0, Y1, WallTop, 240.0f, 14, 16.0f, 0);
	}

	// Pillars, benches, ticket booths and the big Breencast screen.
	for (const float X : { 5900.0f, 6200.0f, 6500.0f })
	{
		for (const float Y : { -350.0f, 550.0f })
		{
			Cylinder(M::Concrete, FVector(X, Y, Floor), 24.0f, WallTop - Floor);
		}
		Box(M::Wood, FVector(X - 48.0f, 80.0f, Floor), FVector(X + 48.0f, 112.0f, Floor + 18.0f));
	}
	for (const float Y : { -200.0f, 40.0f, 280.0f })
	{
		WallWithOpenings(M::Wood, FVector(5680.0f, Y, Floor), FVector(5696.0f, Y + 120.0f, Floor + 160.0f), 0,
			{ FBox2D(FVector2D(Y + 30.0f, Floor + 56.0f), FVector2D(Y + 90.0f, Floor + 112.0f)) });
		Box(M::Wood, FVector(5600.0f, Y, Floor), FVector(5696.0f, Y + 8.0f, Floor + 160.0f));
		Box(M::Wood, FVector(5600.0f, Y, Floor + 160.0f), FVector(5696.0f, Y + 120.0f, Floor + 168.0f));
	}
	Box(M::Wood, FVector(5600.0f, 392.0f, Floor), FVector(5696.0f, 400.0f, Floor + 160.0f));
	Box(M::Metal, FVector(5600.0f, -168.0f, 352.0f), FVector(5608.0f, 368.0f, 568.0f));
	Box(M::Screen, FVector(5608.0f, -160.0f, 360.0f), FVector(5612.0f, 360.0f, 560.0f));

	// Mezzanine along the north wall with stairs up from the concourse.
	Box(M::Concrete, FVector(X0 + 16.0f, 700.0f, 256.0f), FVector(X1 - 16.0f, Y1 - 16.0f, 272.0f));
	Box(M::Metal, FVector(X0 + 16.0f, 696.0f, 272.0f), FVector(6600.0f, 700.0f, 312.0f));
	Stairs(M::Concrete, FVector(6600.0f, 364.0f, Floor), FIntPoint(0, 1), 28, 8.0f, 12.0f, 96.0f);

	// Steps down from the doors into the plaza.
	Stairs(M::Concrete, FVector(6896.0f, 60.0f, 0.0f), FIntPoint(-1, 0), 6, 8.0f, 16.0f, 200.0f);
}

void AHL2PointInsertionBlockout::BuildPlaza()
{
	using M = EHL2BlockoutMaterial;

	const float X0 = 6800.0f, X1 = 10200.0f, Y0 = -1216.0f, Y1 = 2000.0f;
	Box(M::Tile, FVector(X0, Y0, 0.0f), FVector(X1, Y1, 1.0f));

	// Boundaries: Combine wall (south), civic buildings east, blocks north with the street exit.
	Box(M::Combine, FVector(X0, Y0 - 16.0f, 0.0f), FVector(X1, Y0, 768.0f));
	for (float X = X0 + 128.0f; X < X1; X += 256.0f)
	{
		Box(M::Combine, FVector(X, Y0 - 32.0f, 768.0f), FVector(X + 64.0f, Y0, 896.0f));
	}
	Box(M::Brick, FVector(6600.0f, Y0, 0.0f), FVector(X0, -716.0f, 640.0f));
	Box(M::Brick, FVector(6600.0f, 900.0f, 0.0f), FVector(X0, Y1, 640.0f));
	Box(M::Brick, FVector(X1, Y0, 0.0f), FVector(10600.0f, 900.0f, 896.0f));
	FacadeWindows(M::DarkConcrete, FVector(X1 - 4.0f, Y0, 0.0f), FVector(X1, 900.0f, 896.0f), 0, 128.0f, 160.0f, 56.0f, 88.0f);
	Box(M::Brick, FVector(8400.0f, Y1, 0.0f), FVector(9400.0f, 2400.0f, 768.0f));
	FacadeWindows(M::DarkConcrete, FVector(8400.0f, Y1 - 4.0f, 0.0f), FVector(9400.0f, Y1, 768.0f), 1, 128.0f, 160.0f, 56.0f, 88.0f);

	// The domed civic building with its clock tower ("City 17" on the map).
	Box(M::Concrete, FVector(9400.0f, 900.0f, 0.0f), FVector(10600.0f, Y1, 768.0f));
	FacadeWindows(M::DarkConcrete, FVector(9396.0f, 900.0f, 0.0f), FVector(9400.0f, Y1, 768.0f), 0, 128.0f, 160.0f, 56.0f, 88.0f);
	Cylinder(M::Concrete, FVector(10000.0f, 1450.0f, 768.0f), 320.0f, 96.0f);
	Cylinder(M::Metal, FVector(10000.0f, 1450.0f, 864.0f), 256.0f, 128.0f);
	Cylinder(M::Metal, FVector(10000.0f, 1450.0f, 992.0f), 160.0f, 96.0f);
	Box(M::Concrete, FVector(9480.0f, 1800.0f, 768.0f), FVector(9640.0f, 1960.0f, 1280.0f));
	Box(M::Screen, FVector(9476.0f, 1840.0f, 1120.0f), FVector(9480.0f, 1920.0f, 1200.0f));
	GableRoof(M::Metal, FVector(9480.0f, 1800.0f, 1280.0f), FVector(9640.0f, 1960.0f, 1376.0f), 0, 4);

	// Breen monument on its plinth with the Breencast screen facing the station.
	Box(M::Concrete, FVector(8060.0f, 184.0f, 0.0f), FVector(8460.0f, 584.0f, 16.0f));
	Box(M::Concrete, FVector(8100.0f, 224.0f, 16.0f), FVector(8420.0f, 544.0f, 32.0f));
	Cylinder(M::Combine, FVector(8260.0f, 384.0f, 32.0f), 48.0f, 720.0f);
	Box(M::Metal, FVector(8190.0f, 292.0f, 352.0f), FVector(8212.0f, 476.0f, 488.0f));
	Box(M::Screen, FVector(8186.0f, 300.0f, 360.0f), FVector(8190.0f, 468.0f, 480.0f));

	// Trees and lamps around the monument.
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const float Angle = UE_TWO_PI * Index / 8.0f;
		const float TX = 8260.0f + 640.0f * FMath::Cos(Angle);
		const float TY = 384.0f + 640.0f * FMath::Sin(Angle);
		Cylinder(M::Wood, FVector(TX, TY, 0.0f), 8.0f, 176.0f);
		Cylinder(M::Train, FVector(TX, TY, 144.0f), 64.0f, 112.0f);
	}
	for (const FVector2D& P : { FVector2D(7200.0f, -900.0f), FVector2D(7200.0f, 1700.0f), FVector2D(9200.0f, -900.0f), FVector2D(9200.0f, 700.0f) })
	{
		Cylinder(M::Metal, FVector(P.X, P.Y, 0.0f), 3.0f, 192.0f);
		Box(M::Metal, FVector(P.X - 12.0f, P.Y - 6.0f, 192.0f), FVector(P.X + 12.0f, P.Y + 6.0f, 200.0f));
	}

	// Elevated railway crossing behind the station (the dark viaduct on the map).
	const float ViaductY0 = 1300.0f, ViaductY1 = 1500.0f, ViaductZ = 448.0f;
	Box(M::Concrete, FVector(5000.0f, ViaductY0, ViaductZ), FVector(9400.0f, ViaductY1, ViaductZ + 48.0f));
	Box(M::Metal, FVector(5000.0f, ViaductY0, ViaductZ + 48.0f), FVector(9400.0f, ViaductY0 + 8.0f, ViaductZ + 96.0f));
	Box(M::Metal, FVector(5000.0f, ViaductY1 - 8.0f, ViaductZ + 48.0f), FVector(9400.0f, ViaductY1, ViaductZ + 96.0f));
	for (float X = 7000.0f; X < 9400.0f; X += 512.0f)
	{
		Cylinder(M::Concrete, FVector(X, (ViaductY0 + ViaductY1) * 0.5f, 0.0f), 32.0f, ViaductZ);
	}
	Box(M::Train, FVector(5400.0f, ViaductY0 + 36.0f, ViaductZ + 48.0f), FVector(7000.0f, ViaductY1 - 36.0f, ViaductZ + 208.0f));

	// Civil Protection barricades, checkpoint booth, vehicles and crates.
	Box(M::Combine, FVector(7700.0f, 1640.0f, 0.0f), FVector(7716.0f, 1980.0f, 96.0f));
	Box(M::Combine, FVector(8484.0f, 1640.0f, 0.0f), FVector(8500.0f, 1980.0f, 96.0f));
	Box(M::Metal, FVector(9000.0f, -900.0f, 0.0f), FVector(9160.0f, -740.0f, 128.0f));
	Box(M::Screen, FVector(8996.0f, -860.0f, 64.0f), FVector(9000.0f, -780.0f, 112.0f));
	Box(M::Metal, FVector(9500.0f, -300.0f, 0.0f), FVector(9800.0f, -150.0f, 128.0f)); // APC
	Box(M::Metal, FVector(9560.0f, -280.0f, 128.0f), FVector(9720.0f, -170.0f, 176.0f));
	Box(M::Metal, FVector(7300.0f, -1180.0f, 0.0f), FVector(7400.0f, -1120.0f, 56.0f)); // dumpster
	Box(M::Wood, FVector(7500.0f, -1180.0f, 0.0f), FVector(7548.0f, -1132.0f, 48.0f));
	Box(M::Wood, FVector(7548.0f, -1180.0f, 0.0f), FVector(7596.0f, -1132.0f, 48.0f));
	Box(M::Wood, FVector(7524.0f, -1180.0f, 48.0f), FVector(7572.0f, -1132.0f, 96.0f));
}

void AHL2PointInsertionBlockout::BuildStreet()
{
	using M = EHL2BlockoutMaterial;

	const float X0 = 7800.0f, X1 = 8400.0f, Y0 = 2000.0f, Y1 = 3900.0f;

	Box(M::Concrete, FVector(X0, Y0, 0.0f), FVector(X1, Y1, 1.0f));
	Box(M::Concrete, FVector(X0, 2400.0f, 0.0f), FVector(X0 + 80.0f, Y1, 8.0f));
	Box(M::Concrete, FVector(X1 - 80.0f, Y0, 0.0f), FVector(X1, Y1, 8.0f));

	// Terraces either side and a building closing the far end.
	Box(M::Brick, FVector(7000.0f, 2400.0f, 0.0f), FVector(X0, Y1, 640.0f));
	FacadeWindows(M::DarkConcrete, FVector(X0, 2400.0f, 0.0f), FVector(X0 + 4.0f, Y1, 640.0f), 0, 128.0f, 128.0f, 48.0f, 72.0f);
	Box(M::Brick, FVector(X1, 2400.0f, 0.0f), FVector(8800.0f, Y1, 704.0f));
	FacadeWindows(M::DarkConcrete, FVector(X1 - 4.0f, 2400.0f, 0.0f), FVector(X1, Y1, 704.0f), 0, 128.0f, 128.0f, 48.0f, 72.0f);
	Box(M::Brick, FVector(X0, Y1, 0.0f), FVector(X1, Y1 + 100.0f, 640.0f));

	// Civil Protection barricade blocks the street, forcing the turn into the alley.
	Box(M::Combine, FVector(X0, 3200.0f, 0.0f), FVector(X1, 3216.0f, 160.0f));
	Box(M::Metal, FVector(8200.0f, 2600.0f, 0.0f), FVector(8300.0f, 2820.0f, 40.0f)); // abandoned car
	Box(M::Metal, FVector(8210.0f, 2660.0f, 40.0f), FVector(8290.0f, 2780.0f, 72.0f));
	for (const float Y : { 2500.0f, 2900.0f, 3500.0f })
	{
		Cylinder(M::Metal, FVector(X0 + 32.0f, Y, 8.0f), 3.0f, 176.0f);
		Cylinder(M::Metal, FVector(X1 - 32.0f, Y, 8.0f), 3.0f, 176.0f);
	}
}

void AHL2PointInsertionBlockout::BuildApartments()
{
	using M = EHL2BlockoutMaterial;

	// Courtyard reached through the alley off the street.
	Box(M::Brick, FVector(3400.0f, 2000.0f, 0.0f), FVector(7800.0f, 2032.0f, 384.0f));
	Box(M::Concrete, FVector(6000.0f, 2032.0f, 0.0f), FVector(7800.0f, 2600.0f, 1.0f));
	Cylinder(M::Wood, FVector(6600.0f, 2300.0f, 0.0f), 10.0f, 224.0f);
	Cylinder(M::Train, FVector(6600.0f, 2300.0f, 176.0f), 96.0f, 128.0f);
	Box(M::Wood, FVector(6300.0f, 2080.0f, 0.0f), FVector(6396.0f, 2112.0f, 18.0f));
	Box(M::Metal, FVector(7100.0f, 2060.0f, 0.0f), FVector(7196.0f, 2120.0f, 56.0f));

	// Resistance apartment block: four storeys, roof at 512.
	const float X0 = 6000.0f, X1 = 7000.0f, Y0 = 2600.0f, Y1 = 3200.0f;
	const float Wall = 16.0f, Storey = 128.0f, RoofZ = 512.0f;

	TArray<FBox2D> Front = { FBox2D(FVector2D(6480.0f, 0.0f), FVector2D(6560.0f, 112.0f)) };
	for (float Z = 160.0f; Z < RoofZ; Z += Storey)
	{
		for (const float WX : { 6100.0f, 6300.0f, 6660.0f, 6860.0f })
		{
			Front.Add(FBox2D(FVector2D(WX, Z), FVector2D(WX + 80.0f, Z + 56.0f)));
		}
	}
	WallWithOpenings(M::Brick, FVector(X0, Y0, 0.0f), FVector(X1, Y0 + Wall, RoofZ), 1, Front);
	Box(M::Brick, FVector(X0, Y1 - Wall, 0.0f), FVector(X1, Y1, RoofZ));
	Box(M::Brick, FVector(X0, Y0 + Wall, 0.0f), FVector(X0 + Wall, Y1 - Wall, RoofZ));
	Box(M::Brick, FVector(X1 - Wall, Y0 + Wall, 0.0f), FVector(X1, Y1 - Wall, RoofZ));
	Box(M::Wood, FVector(X0, Y0, 0.0f), FVector(X1, Y1, 1.0f));

	// Switchback stairwell in the north-east corner, one flight per storey.
	const FBox2D LaneA(FVector2D(6728.0f, 3056.0f), FVector2D(6920.0f, 3120.0f));
	const FBox2D LaneB(FVector2D(6728.0f, 3120.0f), FVector2D(6920.0f, Y1 - Wall));
	const FVector InnerMin(X0 + Wall, Y0 + Wall, 0.0f);
	const FVector InnerMax(X1 - Wall, Y1 - Wall, 0.0f);

	for (int32 Level = 1; Level <= 4; ++Level)
	{
		const float Top = Level * Storey;
		const bool bRoof = Level == 4;
		SlabWithHole(bRoof ? M::Concrete : M::Wood, FVector(InnerMin.X, InnerMin.Y, Top - 8.0f), FVector(InnerMax.X, InnerMax.Y, Top),
			Level % 2 == 1 ? LaneA : LaneB);

		const bool bEastbound = Level % 2 == 1;
		const FVector Start = bEastbound
			? FVector(LaneA.Min.X, LaneA.Min.Y, Top - Storey)
			: FVector(LaneB.Max.X, LaneB.Min.Y, Top - Storey);
		Stairs(M::Wood, Start, FIntPoint(bEastbound ? 1 : -1, 0), 16, 8.0f, 12.0f, 64.0f);

		// Partition with a doorway on every storey, plus some furniture.
		const float Base = Top - Storey;
		WallWithOpenings(M::Concrete, FVector(6400.0f, Y0 + Wall, Base), FVector(6416.0f, 3040.0f, Top - 8.0f), 0,
			{ FBox2D(FVector2D(2700.0f, Base), FVector2D(2780.0f, Base + 112.0f)) });
		Box(M::Wood, FVector(6100.0f, 3000.0f, Base), FVector(6228.0f, 3180.0f, Base + 24.0f));
		Box(M::Wood, FVector(6500.0f, 2700.0f, Base), FVector(6580.0f, 2760.0f, Base + 30.0f));
	}

	// Roof parapet; the west edge is open for the drop onto the neighbouring roof.
	Box(M::Concrete, FVector(X0, Y0, RoofZ), FVector(X1, Y0 + Wall, RoofZ + 32.0f));
	Box(M::Concrete, FVector(X0, Y1 - Wall, RoofZ), FVector(X1, Y1, RoofZ + 32.0f));
	Box(M::Concrete, FVector(X1 - Wall, Y0 + Wall, RoofZ), FVector(X1, Y1 - Wall, RoofZ + 32.0f));
	Box(M::Metal, FVector(6200.0f, 2700.0f, RoofZ), FVector(6280.0f, 2780.0f, RoofZ + 48.0f));

	// Blocks behind the apartments that wall in the rooftop run.
	Box(M::Brick, FVector(6000.0f, Y1, 0.0f), FVector(7000.0f, 3700.0f, 704.0f));
	Box(M::Brick, FVector(1200.0f, 3300.0f, 0.0f), FVector(6000.0f, 3700.0f, 704.0f));
	Box(M::Brick, FVector(3400.0f, 3100.0f, 0.0f), FVector(3600.0f, 3300.0f, 704.0f));
}

void AHL2PointInsertionBlockout::BuildRooftops()
{
	using M = EHL2BlockoutMaterial;

	// Neighbour west of the courtyard: flat roof 32 below the apartment roof.
	Box(M::Brick, FVector(5600.0f, 2032.0f, 0.0f), FVector(6000.0f, 3300.0f, 480.0f));
	Box(M::Concrete, FVector(5600.0f, 2032.0f, 480.0f), FVector(6000.0f, 2048.0f, 512.0f));
	Box(M::Brick, FVector(5700.0f, 2900.0f, 480.0f), FVector(5760.0f, 2960.0f, 576.0f)); // chimney

	// Alley 1 (96 wide) with a fire escape back up if you fall.
	Box(M::Concrete, FVector(5504.0f, 2880.0f, 480.0f), FVector(5600.0f, 2960.0f, 488.0f)); // plank
	Stairs(M::Metal, FVector(5504.0f, 2100.0f, 0.0f), FIntPoint(0, 1), 30, 16.0f, 16.0f, 96.0f);

	// Pitched roof (stepped so it stays walkable).
	Box(M::Brick, FVector(4400.0f, 2500.0f, 0.0f), FVector(5504.0f, 3300.0f, 400.0f));
	FacadeWindows(M::DarkConcrete, FVector(4400.0f, 2496.0f, 0.0f), FVector(5504.0f, 2500.0f, 400.0f), 1, 128.0f, 128.0f, 48.0f, 72.0f);
	GableRoof(M::Brick, FVector(4400.0f, 2500.0f, 400.0f), FVector(5504.0f, 3300.0f, 496.0f), 0, 8);
	for (const float X : { 4600.0f, 5100.0f })
	{
		Box(M::Brick, FVector(X, 2860.0f, 496.0f), FVector(X + 48.0f, 2940.0f, 560.0f));
	}

	// Alley 2 and the flat roof with the water tower.
	Stairs(M::Metal, FVector(4304.0f, 2144.0f, 0.0f), FIntPoint(0, 1), 26, 16.0f, 16.0f, 96.0f);
	Box(M::Brick, FVector(3600.0f, 2500.0f, 0.0f), FVector(4304.0f, 3300.0f, 416.0f));
	FacadeWindows(M::DarkConcrete, FVector(3600.0f, 2496.0f, 0.0f), FVector(4304.0f, 2500.0f, 416.0f), 1, 128.0f, 128.0f, 48.0f, 72.0f);
	Box(M::Concrete, FVector(3600.0f, 2500.0f, 416.0f), FVector(4304.0f, 2516.0f, 448.0f));
	Box(M::Concrete, FVector(3600.0f, 3284.0f, 416.0f), FVector(4304.0f, 3300.0f, 448.0f));
	for (const FVector2D& Leg : { FVector2D(3900.0f, 3040.0f), FVector2D(4020.0f, 3040.0f), FVector2D(3900.0f, 3160.0f), FVector2D(4020.0f, 3160.0f) })
	{
		Box(M::Metal, FVector(Leg.X, Leg.Y, 416.0f), FVector(Leg.X + 8.0f, Leg.Y + 8.0f, 544.0f));
	}
	Cylinder(M::Wood, FVector(3964.0f, 3104.0f, 544.0f), 88.0f, 128.0f);
	Box(M::Metal, FVector(3700.0f, 2620.0f, 416.0f), FVector(3800.0f, 2700.0f, 464.0f));
	Box(M::Tile, FVector(4100.0f, 2700.0f, 416.0f), FVector(4200.0f, 2860.0f, 440.0f));

	// Drop onto the plank across alley 3; it leads in through the attic window.
	Box(M::Wood, FVector(3400.0f, 2640.0f, 312.0f), FVector(3600.0f, 2760.0f, 320.0f));
}

void AHL2PointInsertionBlockout::BuildAtticAndEnd()
{
	using M = EHL2BlockoutMaterial;

	// Attic building: ground floor, first floor at 160, attic at 320, eaves at 448.
	const float X0 = 2400.0f, X1 = 3400.0f, Y0 = 2300.0f, Y1 = 3100.0f, Wall = 16.0f, Eaves = 448.0f;

	WallWithOpenings(M::Brick, FVector(X1 - Wall, Y0, 0.0f), FVector(X1, Y1, Eaves), 0,
		{
			FBox2D(FVector2D(2640.0f, 320.0f), FVector2D(2760.0f, 432.0f)), // attic window
			FBox2D(FVector2D(2800.0f, 0.0f), FVector2D(2880.0f, 112.0f)),   // alley door
		});
	WallWithOpenings(M::Brick, FVector(X0, Y0 + Wall, 0.0f), FVector(X0 + Wall, Y1 - Wall, Eaves), 0,
		{ FBox2D(FVector2D(2600.0f, 0.0f), FVector2D(2680.0f, 112.0f)) }); // into the End building
	Box(M::Brick, FVector(X0, Y0, 0.0f), FVector(X1 - Wall, Y0 + Wall, Eaves));
	Box(M::Brick, FVector(X0, Y1 - Wall, 0.0f), FVector(X1 - Wall, Y1, Eaves));
	GableRoof(M::Brick, FVector(X0 - 16.0f, Y0 - 16.0f, Eaves), FVector(X1 + 16.0f, Y1 + 16.0f, 576.0f), 0, 8);
	Box(M::Wood, FVector(X0, Y0, 0.0f), FVector(X1, Y1, 1.0f));

	// Two storeys of switchback stairs: ground -> 160 -> attic 320.
	const FBox2D LaneA(FVector2D(2496.0f, Y0 + Wall), FVector2D(2736.0f, 2380.0f));
	const FBox2D LaneB(FVector2D(2496.0f, 2380.0f), FVector2D(2736.0f, 2444.0f));
	const FVector InnerMin(X0 + Wall, Y0 + Wall, 0.0f);
	const FVector InnerMax(X1 - Wall, Y1 - Wall, 0.0f);
	SlabWithHole(M::Wood, FVector(InnerMin.X, InnerMin.Y, 152.0f), FVector(InnerMax.X, InnerMax.Y, 160.0f), LaneA);
	SlabWithHole(M::Wood, FVector(InnerMin.X, InnerMin.Y, 312.0f), FVector(InnerMax.X, InnerMax.Y, 320.0f), LaneB);
	Stairs(M::Wood, FVector(LaneA.Min.X, LaneA.Min.Y, 0.0f), FIntPoint(1, 0), 20, 8.0f, 12.0f, 64.0f);
	Stairs(M::Wood, FVector(LaneB.Max.X, LaneB.Min.Y, 160.0f), FIntPoint(-1, 0), 20, 8.0f, 12.0f, 64.0f);

	// Attic clutter and roof beams.
	for (float X = X0 + 120.0f; X < X1 - 60.0f; X += 200.0f)
	{
		Box(M::Wood, FVector(X, Y0 + Wall, 424.0f), FVector(X + 16.0f, Y1 - Wall, 440.0f));
	}
	Box(M::Wood, FVector(3000.0f, 2900.0f, 320.0f), FVector(3064.0f, 2964.0f, 384.0f));
	Box(M::Wood, FVector(3100.0f, 2500.0f, 320.0f), FVector(3228.0f, 2540.0f, 352.0f));

	// End building west of the attic: where Civil Protection corner you.
	const float EX0 = 1200.0f, EY0 = 2200.0f, EHeight = 384.0f;
	Box(M::Concrete, FVector(EX0, EY0, 0.0f), FVector(EX0 + 16.0f, Y1, EHeight));
	Box(M::Concrete, FVector(EX0, EY0, 0.0f), FVector(X0, EY0 + 16.0f, EHeight));
	Box(M::Concrete, FVector(EX0, Y1 - 16.0f, 0.0f), FVector(X0, Y1, EHeight));
	Box(M::Concrete, FVector(X0 - 16.0f, EY0 + 16.0f, 0.0f), FVector(X0, Y0, EHeight));
	Box(M::Concrete, FVector(EX0, EY0, EHeight), FVector(X0, Y1, EHeight + 16.0f));
	Box(M::Tile, FVector(EX0 + 16.0f, EY0 + 16.0f, 0.0f), FVector(X0 - 16.0f, Y1 - 16.0f, 1.0f));
	Box(M::Trim, FVector(1500.0f, 2500.0f, 1.0f), FVector(1760.0f, 2760.0f, 2.0f)); // end marker
	Box(M::Screen, FVector(EX0 + 16.0f, 2500.0f, 160.0f), FVector(EX0 + 20.0f, 2760.0f, 288.0f));
	for (float X = EX0; X <= X0; X += 200.0f)
	{
		Box(M::Metal, FVector(X, EY0 - 80.0f, 0.0f), FVector(X + 8.0f, EY0 - 72.0f, 448.0f)); // scaffolding
	}
	Box(M::Wood, FVector(EX0, EY0 - 96.0f, 192.0f), FVector(X0, EY0 - 16.0f, 200.0f));

	// Factory with the striped smokestacks.
	Box(M::Concrete, FVector(2600.0f, 1500.0f, 0.0f), FVector(3200.0f, 2000.0f, 256.0f));
	for (const float SX : { 2750.0f, 3000.0f })
	{
		for (int32 Band = 0; Band < 12; ++Band)
		{
			Cylinder(Band % 2 == 0 ? M::Brick : M::Concrete, FVector(SX, 1750.0f, 256.0f + Band * 128.0f), 48.0f - Band * 1.5f, 128.0f);
		}
	}
}

void AHL2PointInsertionBlockout::BuildSkyline()
{
	using M = EHL2BlockoutMaterial;

	// The Citadel, looming beyond the plaza.
	const FVector2D CitadelXY(16000.0f, 4200.0f);
	float Z = 0.0f;
	const float Tiers[][2] = {
		{ 900.0f, 1600.0f }, { 780.0f, 1800.0f }, { 660.0f, 2000.0f },
		{ 540.0f, 2200.0f }, { 400.0f, 2000.0f }, { 240.0f, 1400.0f },
	};
	for (const auto& Tier : Tiers)
	{
		Cylinder(M::Combine, FVector(CitadelXY.X, CitadelXY.Y, Z), Tier[0], Tier[1]);
		Z += Tier[1];
	}

	// Distant City 17 blocks outside the playable bounds.
	const float Blocks[][6] = {
		{ 11400.0f, -2400.0f, 0.0f, 12400.0f, -800.0f, 1400.0f },
		{ 11400.0f, -400.0f, 0.0f, 12200.0f, 1400.0f, 1700.0f },
		{ 11600.0f, 1800.0f, 0.0f, 12800.0f, 3200.0f, 1300.0f },
		{ 9000.0f, 4600.0f, 0.0f, 10400.0f, 5600.0f, 1800.0f },
		{ 6000.0f, 4600.0f, 0.0f, 8000.0f, 5400.0f, 1200.0f },
		{ 2400.0f, 4600.0f, 0.0f, 4800.0f, 5600.0f, 1500.0f },
		{ -1000.0f, 4600.0f, 0.0f, 1600.0f, 5400.0f, 1000.0f },
		{ -3200.0f, -1800.0f, 0.0f, -2000.0f, 1600.0f, 1100.0f },
		{ 0.0f, -4400.0f, 0.0f, 2400.0f, -3200.0f, 1300.0f },
		{ 3600.0f, -4400.0f, 0.0f, 6400.0f, -3200.0f, 1000.0f },
		{ 7200.0f, -4600.0f, 0.0f, 9600.0f, -3200.0f, 1600.0f },
	};
	for (const auto& B : Blocks)
	{
		Box(M::Brick, FVector(B[0], B[1], B[2]), FVector(B[3], B[4], B[5]));
	}
}

// ---------------------------------------------------------------------------
// Physics props
// ---------------------------------------------------------------------------

void AHL2PointInsertionBlockout::GetPhysicsProps(TArray<FPhysicsProp>& OutProps)
{
	using M = EHL2BlockoutMaterial;

	// The can by the station hall entrance, and one in the plaza.
	OutProps.Add({ true, M::Metal, FVector(5760.0f, -600.0f, 54.0f), FVector(7.0f, 7.0f, 10.0f) });
	OutProps.Add({ true, M::Metal, FVector(7400.0f, -600.0f, 6.0f), FVector(7.0f, 7.0f, 10.0f) });

	// Loose crates in the courtyard and the attic.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		OutProps.Add({ false, M::Wood, FVector(6800.0f + Index * 40.0f, 2200.0f, 17.0f), FVector(32.0f, 32.0f, 32.0f) });
	}
	OutProps.Add({ false, M::Wood, FVector(3150.0f, 2700.0f, 337.0f), FVector(32.0f, 32.0f, 32.0f) });

	// Oil drums in the rail yard.
	OutProps.Add({ true, M::Metal, FVector(1500.0f, -2100.0f, 21.0f), FVector(24.0f, 24.0f, 40.0f) });
	OutProps.Add({ true, M::Metal, FVector(-800.0f, 800.0f, 21.0f), FVector(24.0f, 24.0f, 40.0f) });
}

void AHL2PointInsertionBlockout::SpawnPhysicsProps()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<FPhysicsProp> Props;
	GetPhysicsProps(Props);

	for (const FPhysicsProp& Prop : Props)
	{
		const FTransform Local(FQuat::Identity, ToCm(Prop.CenterHU), ToCm(Prop.SizeHU) / 100.0f);
		const FTransform WorldTransform = Local * GetActorTransform();

		AStaticMeshActor* Actor = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), WorldTransform, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Actor)
		{
			continue;
		}

		UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Prop.bCylinder ? CylinderMesh : CubeMesh);
		const int32 MatIndex = static_cast<int32>(Prop.Material);
		if (PaletteMaterials.IsValidIndex(MatIndex))
		{
			Mesh->SetMaterial(0, PaletteMaterials[MatIndex]);
		}
		Mesh->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
		Mesh->SetSimulatePhysics(true);

		Actor->FinishSpawning(WorldTransform);
		SpawnedProps.Add(Actor);
	}
}
