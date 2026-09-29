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
	// Standing inside the arriving train car, facing the open door onto the platform (+Y).
	// PlayerStart pivots sit at the centre of a 92 cm half-height capsule.
	const float TrainFloorCm = 48.0f * HU;
	const FVector Local(0.0f, -96.0f * HU, TrainFloorCm + 94.0f);
	return FTransform(FRotator(0.0f, 90.0f, 0.0f), Local) * GetActorTransform();
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

	BuildTrainPlatform();
	BuildCheckpoint();
	BuildStationHall();
	BuildPlaza();
	BuildStreetAndApartments();
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

// ---------------------------------------------------------------------------
// Layout. +X = east, +Y = north, Z up. Units: Hammer units.
// Doors are 64x112, standard stair steps are 8 rise / 12 run (18 max step).
// ---------------------------------------------------------------------------

void AHL2PointInsertionBlockout::BuildTrainPlatform()
{
	using M = EHL2BlockoutMaterial;

	// Catch-all ground under the whole map.
	Box(M::DarkConcrete, FVector(-4096.0f, -4096.0f, -64.0f), FVector(12288.0f, 12288.0f, -16.0f));

	// Track trench (z 0) south of the platform (z 48).
	Box(M::DarkConcrete, FVector(-2048.0f, -416.0f, -16.0f), FVector(1024.0f, -16.0f, 0.0f));
	Box(M::Concrete, FVector(-2048.0f, -432.0f, -16.0f), FVector(1024.0f, -416.0f, 384.0f));   // far wall
	Box(M::Concrete, FVector(-2064.0f, -432.0f, -16.0f), FVector(-2048.0f, 272.0f, 384.0f));  // west end wall
	Box(M::Concrete, FVector(1024.0f, -432.0f, -16.0f), FVector(1040.0f, 272.0f, 384.0f));    // east end wall

	for (const float TrackY : { -96.0f, -288.0f })
	{
		for (float X = -2032.0f; X < 1008.0f; X += 64.0f)
		{
			Box(M::Wood, FVector(X, TrackY - 48.0f, 0.0f), FVector(X + 16.0f, TrackY + 48.0f, 3.0f));
		}
		Box(M::Metal, FVector(-2048.0f, TrackY - 30.0f, 0.0f), FVector(1024.0f, TrackY - 26.0f, 7.0f));
		Box(M::Metal, FVector(-2048.0f, TrackY + 26.0f, 0.0f), FVector(1024.0f, TrackY + 30.0f, 7.0f));
	}

	// Platform.
	Box(M::Concrete, FVector(-2048.0f, -16.0f, -16.0f), FVector(1024.0f, 256.0f, 48.0f));
	Box(M::Trim, FVector(-2048.0f, -16.0f, 48.0f), FVector(1024.0f, 0.0f, 49.0f));

	// Maintenance steps from the tracks back up to the platform (west end).
	Stairs(M::Concrete, FVector(-1600.0f, -112.0f, 0.0f), FIntPoint(0, 1), 6, 8.0f, 16.0f, 64.0f);

	// Back wall of the platform with the door into the checkpoint corridor.
	WallWithOpenings(M::Brick, FVector(-2048.0f, 256.0f, 48.0f), FVector(1024.0f, 272.0f, 384.0f), 1,
		{ FBox2D(FVector2D(-32.0f, 48.0f), FVector2D(32.0f, 160.0f)) });

	// Canopy on pillars.
	Box(M::Metal, FVector(-2048.0f, -144.0f, 288.0f), FVector(1024.0f, 256.0f, 296.0f));
	for (float X = -1920.0f; X <= 896.0f; X += 384.0f)
	{
		Cylinder(M::Metal, FVector(X, 128.0f, 48.0f), 8.0f, 240.0f);
	}

	// Breencast screen and benches along the back wall.
	Box(M::Metal, FVector(-408.0f, 248.0f, 168.0f), FVector(-136.0f, 256.0f, 280.0f));
	Box(M::Screen, FVector(-400.0f, 244.0f, 176.0f), FVector(-144.0f, 248.0f, 272.0f));
	for (const float X : { -1200.0f, -800.0f, 400.0f, 700.0f })
	{
		Box(M::Wood, FVector(X, 184.0f, 48.0f), FVector(X + 96.0f, 216.0f, 66.0f));
	}

	// --- The arriving train (track 1, y = -96). The player starts in the middle car. ---
	const float CarY0 = -160.0f;
	const float CarY1 = -32.0f;
	const float CarFloor = 48.0f;
	const float CarRoof = 176.0f;

	Box(M::Train, FVector(-448.0f, CarY0, 16.0f), FVector(448.0f, CarY1, CarFloor));
	for (const float X : { -400.0f, 304.0f })
	{
		Box(M::DarkConcrete, FVector(X, CarY0 + 10.0f, 7.0f), FVector(X + 96.0f, CarY1 - 10.0f, 16.0f));
	}

	const TArray<FBox2D> SideWindows = {
		FBox2D(FVector2D(-320.0f, 96.0f), FVector2D(-128.0f, 144.0f)),
		FBox2D(FVector2D(128.0f, 96.0f), FVector2D(320.0f, 144.0f)),
	};
	TArray<FBox2D> PlatformSide = SideWindows;
	PlatformSide.Add(FBox2D(FVector2D(-40.0f, CarFloor), FVector2D(40.0f, 160.0f)));

	WallWithOpenings(M::Train, FVector(-448.0f, CarY1 - 6.0f, CarFloor), FVector(448.0f, CarY1, CarRoof), 1, PlatformSide);
	WallWithOpenings(M::Train, FVector(-448.0f, CarY0, CarFloor), FVector(448.0f, CarY0 + 6.0f, CarRoof), 1, SideWindows);
	Box(M::Train, FVector(-448.0f, CarY0, CarFloor), FVector(-442.0f, CarY1, CarRoof));
	Box(M::Train, FVector(442.0f, CarY0, CarFloor), FVector(448.0f, CarY1, CarRoof));
	Box(M::Train, FVector(-448.0f, CarY0, CarRoof), FVector(448.0f, CarY1, CarRoof + 8.0f));

	// Seats.
	for (const float X : { -400.0f, -256.0f, 160.0f, 304.0f })
	{
		Box(M::Wood, FVector(X, CarY0 + 6.0f, CarFloor), FVector(X + 96.0f, CarY0 + 38.0f, CarFloor + 18.0f));
		Box(M::Wood, FVector(X, CarY0 + 6.0f, CarFloor + 18.0f), FVector(X + 96.0f, CarY0 + 14.0f, CarFloor + 52.0f));
	}
	for (const float X : { -330.0f, 170.0f })
	{
		Box(M::Wood, FVector(X, CarY1 - 38.0f, CarFloor), FVector(X + 128.0f, CarY1 - 6.0f, CarFloor + 18.0f));
	}

	// Coupled cars either side and a parked train on track 2.
	Box(M::Train, FVector(-1360.0f, CarY0, 16.0f), FVector(-456.0f, CarY1, CarRoof + 8.0f));
	Box(M::Train, FVector(456.0f, CarY0, 16.0f), FVector(1016.0f, CarY1, CarRoof + 8.0f));
	Box(M::Train, FVector(-1800.0f, -352.0f, 16.0f), FVector(200.0f, -224.0f, CarRoof + 8.0f));
	Box(M::Screen, FVector(-1800.0f, -224.0f, 96.0f), FVector(200.0f, -222.0f, 144.0f));
}

void AHL2PointInsertionBlockout::BuildCheckpoint()
{
	using M = EHL2BlockoutMaterial;

	// Corridor north from the platform door: x [-128, 128], y [272, 1024].
	Box(M::Tile, FVector(-144.0f, 272.0f, 32.0f), FVector(144.0f, 1024.0f, 48.0f));
	Box(M::Concrete, FVector(-144.0f, 272.0f, 192.0f), FVector(144.0f, 1024.0f, 208.0f));
	Box(M::Concrete, FVector(-144.0f, 272.0f, 48.0f), FVector(-128.0f, 1024.0f, 192.0f));
	WallWithOpenings(M::Concrete, FVector(128.0f, 272.0f, 48.0f), FVector(144.0f, 1024.0f, 192.0f), 0,
		{ FBox2D(FVector2D(416.0f, 48.0f), FVector2D(480.0f, 160.0f)) });

	// Queue railings and the Combine barrier with a single scanner gate.
	Box(M::Metal, FVector(-48.0f, 480.0f, 48.0f), FVector(-44.0f, 640.0f, 88.0f));
	Box(M::Metal, FVector(44.0f, 480.0f, 48.0f), FVector(48.0f, 640.0f, 88.0f));
	WallWithOpenings(M::Combine, FVector(-128.0f, 640.0f, 48.0f), FVector(128.0f, 656.0f, 192.0f), 1,
		{ FBox2D(FVector2D(-24.0f, 48.0f), FVector2D(24.0f, 160.0f)) });
	Box(M::Combine, FVector(-40.0f, 632.0f, 160.0f), FVector(40.0f, 664.0f, 176.0f));
	Cylinder(M::Metal, FVector(96.0f, 600.0f, 48.0f), 12.0f, 36.0f); // bin next to the can

	// Interrogation room: x [144, 528], y [352, 736].
	Box(M::Tile, FVector(144.0f, 336.0f, 32.0f), FVector(544.0f, 752.0f, 48.0f));
	Box(M::Concrete, FVector(144.0f, 336.0f, 192.0f), FVector(544.0f, 752.0f, 208.0f));
	Box(M::Concrete, FVector(144.0f, 336.0f, 48.0f), FVector(544.0f, 352.0f, 192.0f));
	Box(M::Concrete, FVector(528.0f, 352.0f, 48.0f), FVector(544.0f, 736.0f, 192.0f));
	WallWithOpenings(M::Concrete, FVector(144.0f, 736.0f, 48.0f), FVector(544.0f, 752.0f, 192.0f), 1,
		{ FBox2D(FVector2D(400.0f, 48.0f), FVector2D(464.0f, 160.0f)) });
	Box(M::Metal, FVector(288.0f, 512.0f, 48.0f), FVector(384.0f, 576.0f, 78.0f));
	Box(M::Metal, FVector(304.0f, 470.0f, 48.0f), FVector(336.0f, 502.0f, 66.0f));
	Cylinder(M::Metal, FVector(336.0f, 544.0f, 188.0f), 2.0f, 4.0f);

	// Back hallway from the interrogation room to the station hall.
	Box(M::Tile, FVector(368.0f, 752.0f, 32.0f), FVector(496.0f, 1024.0f, 48.0f));
	Box(M::Concrete, FVector(368.0f, 752.0f, 192.0f), FVector(496.0f, 1024.0f, 208.0f));
	Box(M::Concrete, FVector(368.0f, 752.0f, 48.0f), FVector(384.0f, 1024.0f, 192.0f));
	Box(M::Concrete, FVector(480.0f, 752.0f, 48.0f), FVector(496.0f, 1024.0f, 192.0f));
}

void AHL2PointInsertionBlockout::BuildStationHall()
{
	using M = EHL2BlockoutMaterial;

	// Hall interior: x [-512, 1024], y [1040, 1800], floor z 48, ceiling z 512.
	Box(M::Tile, FVector(-528.0f, 1024.0f, 32.0f), FVector(1040.0f, 1816.0f, 48.0f));
	Box(M::Concrete, FVector(-528.0f, 1024.0f, 512.0f), FVector(1040.0f, 1816.0f, 528.0f));

	WallWithOpenings(M::Concrete, FVector(-528.0f, 1024.0f, 48.0f), FVector(1040.0f, 1040.0f, 512.0f), 1,
		{
			FBox2D(FVector2D(-64.0f, 48.0f), FVector2D(64.0f, 160.0f)),
			FBox2D(FVector2D(400.0f, 48.0f), FVector2D(464.0f, 160.0f)),
		});
	Box(M::Concrete, FVector(-528.0f, 1040.0f, 48.0f), FVector(-512.0f, 1800.0f, 512.0f));
	Box(M::Concrete, FVector(-528.0f, 1800.0f, 48.0f), FVector(1040.0f, 1816.0f, 512.0f));

	// Facade onto the plaza.
	WallWithOpenings(M::Brick, FVector(1024.0f, 1040.0f, 48.0f), FVector(1040.0f, 1800.0f, 512.0f), 0,
		{ FBox2D(FVector2D(1344.0f, 48.0f), FVector2D(1504.0f, 208.0f)) });
	Box(M::Brick, FVector(1024.0f, 1024.0f, 512.0f), FVector(1056.0f, 1816.0f, 704.0f));

	// Pillars and benches.
	for (const float X : { 0.0f, 384.0f, 768.0f })
	{
		for (const float Y : { 1280.0f, 1600.0f })
		{
			Cylinder(M::Concrete, FVector(X, Y, 48.0f), 24.0f, 464.0f);
		}
		Box(M::Wood, FVector(X - 64.0f, 1424.0f, 48.0f), FVector(X + 64.0f, 1456.0f, 66.0f));
	}

	// Ticket booth on the west wall.
	Box(M::Wood, FVector(-512.0f, 1200.0f, 48.0f), FVector(-448.0f, 1600.0f, 90.0f));
	WallWithOpenings(M::Metal, FVector(-456.0f, 1200.0f, 90.0f), FVector(-448.0f, 1600.0f, 176.0f), 0,
		{
			FBox2D(FVector2D(1232.0f, 100.0f), FVector2D(1296.0f, 160.0f)),
			FBox2D(FVector2D(1368.0f, 100.0f), FVector2D(1432.0f, 160.0f)),
			FBox2D(FVector2D(1504.0f, 100.0f), FVector2D(1568.0f, 160.0f)),
		});
	Box(M::Metal, FVector(-512.0f, 1200.0f, 176.0f), FVector(-448.0f, 1600.0f, 184.0f));

	// Big Breencast screen above the booth.
	Box(M::Screen, FVector(-512.0f, 1120.0f, 272.0f), FVector(-504.0f, 1600.0f, 448.0f));

	// Mezzanine along the north wall with stairs up from the hall.
	Box(M::Concrete, FVector(-512.0f, 1640.0f, 240.0f), FVector(1024.0f, 1800.0f, 256.0f));
	Box(M::Metal, FVector(-512.0f, 1636.0f, 256.0f), FVector(-320.0f, 1640.0f, 296.0f));
	Box(M::Metal, FVector(-192.0f, 1636.0f, 256.0f), FVector(1024.0f, 1640.0f, 296.0f));
	Stairs(M::Concrete, FVector(-320.0f, 1328.0f, 48.0f), FIntPoint(0, 1), 26, 8.0f, 12.0f, 128.0f);

	// Crate stack on the mezzanine (crouch-jump practice).
	Box(M::Wood, FVector(600.0f, 1680.0f, 256.0f), FVector(648.0f, 1728.0f, 304.0f));
	Box(M::Wood, FVector(648.0f, 1680.0f, 256.0f), FVector(696.0f, 1728.0f, 280.0f));
	Box(M::Wood, FVector(608.0f, 1688.0f, 304.0f), FVector(640.0f, 1720.0f, 336.0f));
}

void AHL2PointInsertionBlockout::BuildPlaza()
{
	using M = EHL2BlockoutMaterial;

	// Steps down from the station facade (z 48) to the plaza (z 0).
	Stairs(M::Concrete, FVector(1136.0f, 1344.0f, 0.0f), FIntPoint(-1, 0), 6, 8.0f, 16.0f, 160.0f);

	// Plaza ground: x [1040, 3440], y [640, 2240].
	Box(M::Concrete, FVector(880.0f, 624.0f, -16.0f), FVector(3456.0f, 2240.0f, 0.0f));

	// Station building flanks either side of the facade.
	Box(M::Brick, FVector(880.0f, 624.0f, 0.0f), FVector(1040.0f, 1024.0f, 640.0f));
	Box(M::Brick, FVector(880.0f, 1816.0f, 0.0f), FVector(1040.0f, 2240.0f, 640.0f));

	// Boundaries: Combine wall (south), apartment blocks (north / east) with the street exit.
	Box(M::Combine, FVector(1040.0f, 608.0f, 0.0f), FVector(3440.0f, 640.0f, 768.0f));
	Box(M::Brick, FVector(880.0f, 2240.0f, 0.0f), FVector(2240.0f, 2560.0f, 896.0f));
	Box(M::Brick, FVector(2560.0f, 2240.0f, 0.0f), FVector(3440.0f, 2560.0f, 768.0f));
	Box(M::Brick, FVector(3440.0f, 608.0f, 0.0f), FVector(3760.0f, 2560.0f, 1024.0f));

	// Monument / Combine pylon with a Breencast screen facing the station.
	Box(M::Concrete, FVector(2048.0f, 1248.0f, 0.0f), FVector(2432.0f, 1632.0f, 16.0f));
	Box(M::Concrete, FVector(2080.0f, 1280.0f, 16.0f), FVector(2400.0f, 1600.0f, 32.0f));
	Cylinder(M::Combine, FVector(2240.0f, 1440.0f, 32.0f), 64.0f, 640.0f);
	Box(M::Screen, FVector(2164.0f, 1360.0f, 320.0f), FVector(2178.0f, 1520.0f, 440.0f));

	// Lamp posts.
	for (const FVector2D& P : { FVector2D(1400.0f, 900.0f), FVector2D(1400.0f, 1980.0f), FVector2D(2900.0f, 900.0f), FVector2D(2900.0f, 1980.0f) })
	{
		Cylinder(M::Metal, FVector(P.X, P.Y, 0.0f), 3.0f, 160.0f);
		Box(M::Metal, FVector(P.X - 12.0f, P.Y - 6.0f, 160.0f), FVector(P.X + 12.0f, P.Y + 6.0f, 168.0f));
	}

	// Barricades, dumpster, a parked truck, crates.
	Box(M::Combine, FVector(1700.0f, 700.0f, 0.0f), FVector(1716.0f, 1100.0f, 96.0f));
	Box(M::Combine, FVector(2800.0f, 1800.0f, 0.0f), FVector(2816.0f, 2200.0f, 96.0f));
	Box(M::Metal, FVector(3200.0f, 700.0f, 0.0f), FVector(3296.0f, 760.0f, 56.0f));
	Box(M::Metal, FVector(2900.0f, 1100.0f, 0.0f), FVector(3180.0f, 1220.0f, 120.0f));
	Box(M::Metal, FVector(3180.0f, 1110.0f, 0.0f), FVector(3260.0f, 1210.0f, 96.0f));
	Box(M::Wood, FVector(1300.0f, 660.0f, 0.0f), FVector(1348.0f, 708.0f, 48.0f));
	Box(M::Wood, FVector(1348.0f, 660.0f, 0.0f), FVector(1396.0f, 708.0f, 48.0f));
	Box(M::Wood, FVector(1324.0f, 660.0f, 48.0f), FVector(1372.0f, 708.0f, 96.0f));
}

void AHL2PointInsertionBlockout::BuildStreetAndApartments()
{
	using M = EHL2BlockoutMaterial;

	// Street north of the plaza: x [2240, 2560], y [2560, 3840].
	Box(M::DarkConcrete, FVector(2240.0f, 2240.0f, -16.0f), FVector(2560.0f, 3840.0f, 0.0f));
	Box(M::Concrete, FVector(2240.0f, 2560.0f, 0.0f), FVector(2304.0f, 3840.0f, 8.0f));
	Box(M::Concrete, FVector(2496.0f, 2560.0f, 0.0f), FVector(2560.0f, 3840.0f, 8.0f));
	Box(M::Brick, FVector(2080.0f, 2560.0f, 0.0f), FVector(2240.0f, 3840.0f, 768.0f));
	Box(M::Brick, FVector(2560.0f, 2560.0f, 0.0f), FVector(2720.0f, 3840.0f, 640.0f));

	// Civil Protection barricade across the street, an abandoned car and street lamps.
	WallWithOpenings(M::Combine, FVector(2240.0f, 3200.0f, 0.0f), FVector(2560.0f, 3216.0f, 128.0f), 1,
		{ FBox2D(FVector2D(2368.0f, 0.0f), FVector2D(2432.0f, 112.0f)) });
	Box(M::Metal, FVector(2320.0f, 2900.0f, 0.0f), FVector(2400.0f, 3100.0f, 56.0f));
	for (const float Y : { 2800.0f, 3500.0f })
	{
		Cylinder(M::Metal, FVector(2272.0f, Y, 8.0f), 3.0f, 160.0f);
		Cylinder(M::Metal, FVector(2528.0f, Y, 8.0f), 3.0f, 160.0f);
	}

	// --- Apartment block at the end of the street: x [2176, 2624], y [3840, 4288]. ---
	const float X0 = 2176.0f, X1 = 2624.0f, Y0 = 3840.0f, Y1 = 4288.0f;
	const float Wall = 16.0f;
	const float RoofZ = 384.0f;

	Box(M::Wood, FVector(X0, Y0, -16.0f), FVector(X1, Y1, 0.0f));

	TArray<FBox2D> FrontOpenings = { FBox2D(FVector2D(2368.0f, 0.0f), FVector2D(2432.0f, 112.0f)) };
	for (const float Z : { 176.0f, 304.0f })
	{
		FrontOpenings.Add(FBox2D(FVector2D(2224.0f, Z), FVector2D(2320.0f, Z + 56.0f)));
		FrontOpenings.Add(FBox2D(FVector2D(2480.0f, Z), FVector2D(2576.0f, Z + 56.0f)));
	}
	WallWithOpenings(M::Brick, FVector(X0, Y0, 0.0f), FVector(X1, Y0 + Wall, RoofZ), 1, FrontOpenings);
	Box(M::Brick, FVector(X0, Y1 - Wall, 0.0f), FVector(X1, Y1, RoofZ));
	Box(M::Brick, FVector(X0, Y0 + Wall, 0.0f), FVector(X0 + Wall, Y1 - Wall, RoofZ));
	WallWithOpenings(M::Brick, FVector(X1 - Wall, Y0 + Wall, 0.0f), FVector(X1, Y1 - Wall, RoofZ), 0,
		{ FBox2D(FVector2D(3920.0f, 0.0f), FVector2D(3984.0f, 112.0f)) }); // side door to the alley

	// Switchback stairwell in the north-east corner. Lane A climbs east, lane B climbs west.
	const FBox2D LaneA(FVector2D(2352.0f, 4144.0f), FVector2D(2544.0f, 4208.0f));
	const FBox2D LaneB(FVector2D(2352.0f, 4208.0f), FVector2D(2544.0f, 4272.0f));
	const FVector InnerMin(X0 + Wall, Y0 + Wall, 0.0f);
	const FVector InnerMax(X1 - Wall, Y1 - Wall, 0.0f);

	SlabWithHole(M::Wood, FVector(InnerMin.X, InnerMin.Y, 120.0f), FVector(InnerMax.X, InnerMax.Y, 128.0f), LaneA);
	SlabWithHole(M::Wood, FVector(InnerMin.X, InnerMin.Y, 248.0f), FVector(InnerMax.X, InnerMax.Y, 256.0f), LaneB);
	SlabWithHole(M::Concrete, FVector(InnerMin.X, InnerMin.Y, 376.0f), FVector(InnerMax.X, InnerMax.Y, 384.0f), LaneA);

	Stairs(M::Wood, FVector(2352.0f, 4144.0f, 0.0f), FIntPoint(1, 0), 16, 8.0f, 12.0f, 64.0f);
	Stairs(M::Wood, FVector(2544.0f, 4208.0f, 128.0f), FIntPoint(-1, 0), 16, 8.0f, 12.0f, 64.0f);
	Stairs(M::Wood, FVector(2352.0f, 4144.0f, 256.0f), FIntPoint(1, 0), 16, 8.0f, 12.0f, 64.0f);

	// Roof parapet; the east edge is open for the jump across the alley.
	Box(M::Concrete, FVector(X0, Y1 - Wall, RoofZ), FVector(X1, Y1, RoofZ + 32.0f));
	Box(M::Concrete, FVector(X0, Y0, RoofZ), FVector(X1, Y0 + Wall, RoofZ + 32.0f));
	Box(M::Concrete, FVector(X0, Y0 + Wall, RoofZ), FVector(X0 + Wall, Y1 - Wall, RoofZ + 32.0f));

	// Alley (96 HU gap) and the neighbouring, slightly lower roof.
	Box(M::DarkConcrete, FVector(X1, Y0, -16.0f), FVector(2720.0f, Y1, 0.0f));
	Box(M::Brick, FVector(X1, Y1, 0.0f), FVector(2720.0f, Y1 + 16.0f, 352.0f));
	Box(M::Brick, FVector(2720.0f, Y0, 0.0f), FVector(3200.0f, Y1, 352.0f));
	Box(M::Concrete, FVector(2720.0f, Y1 - 16.0f, 352.0f), FVector(3200.0f, Y1, 384.0f));
	Box(M::Concrete, FVector(3184.0f, Y0, 352.0f), FVector(3200.0f, Y1, 384.0f));
	Box(M::Concrete, FVector(2720.0f, Y0, 352.0f), FVector(3200.0f, Y0 + 16.0f, 384.0f));
	Box(M::Metal, FVector(2900.0f, 4000.0f, 352.0f), FVector(2980.0f, 4080.0f, 400.0f));
	Box(M::Metal, FVector(3040.0f, 3920.0f, 352.0f), FVector(3120.0f, 3968.0f, 384.0f));

	// Buildings boxing in the apartment block.
	Box(M::Brick, FVector(1600.0f, 3840.0f, 0.0f), FVector(2176.0f, 4608.0f, 704.0f));
	Box(M::Brick, FVector(2176.0f, 4304.0f, 0.0f), FVector(3200.0f, 4800.0f, 560.0f));
}

void AHL2PointInsertionBlockout::BuildSkyline()
{
	using M = EHL2BlockoutMaterial;

	// The Citadel, far to the north-east, as a stack of tapering drums.
	const FVector2D CitadelXY(7200.0f, 7600.0f);
	float Z = 0.0f;
	const float Tiers[][2] = {
		{ 640.0f, 1200.0f }, { 560.0f, 1400.0f }, { 480.0f, 1600.0f },
		{ 400.0f, 1800.0f }, { 300.0f, 1600.0f }, { 180.0f, 1200.0f },
	};
	for (const auto& Tier : Tiers)
	{
		Cylinder(M::Combine, FVector(CitadelXY.X, CitadelXY.Y, Z), Tier[0], Tier[1]);
		Z += Tier[1];
	}

	// Distant City 17 blocks.
	const float Blocks[][6] = {
		{ 3760.0f, 400.0f, 0.0f, 4400.0f, 1400.0f, 1400.0f },
		{ 3760.0f, 1600.0f, 0.0f, 4200.0f, 2800.0f, 1100.0f },
		{ 4600.0f, 800.0f, 0.0f, 5200.0f, 1800.0f, 1800.0f },
		{ 3300.0f, 2800.0f, 0.0f, 4000.0f, 3600.0f, 1300.0f },
		{ 4400.0f, 3000.0f, 0.0f, 5000.0f, 4000.0f, 1600.0f },
		{ 3300.0f, 4400.0f, 0.0f, 4200.0f, 5200.0f, 1200.0f },
		{ 1000.0f, 2700.0f, 0.0f, 1800.0f, 3600.0f, 1000.0f },
		{ 1200.0f, -1400.0f, 0.0f, 2400.0f, 400.0f, 900.0f },
		{ 2600.0f, -1600.0f, 0.0f, 3600.0f, 200.0f, 1200.0f },
		{ -1200.0f, 2000.0f, 0.0f, 400.0f, 3200.0f, 1100.0f },
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

	// The can by the checkpoint bin.
	OutProps.Add({ true, M::Metal, FVector(64.0f, 610.0f, 54.0f), FVector(7.0f, 7.0f, 10.0f) });
	OutProps.Add({ true, M::Metal, FVector(1500.0f, 1800.0f, 6.0f), FVector(7.0f, 7.0f, 10.0f) });

	// Loose crates in the plaza and on the platform.
	for (int32 Index = 0; Index < 3; ++Index)
	{
		OutProps.Add({ false, M::Wood, FVector(1600.0f + Index * 40.0f, 2000.0f, 17.0f), FVector(32.0f, 32.0f, 32.0f) });
	}
	OutProps.Add({ false, M::Wood, FVector(-1400.0f, 120.0f, 65.0f), FVector(32.0f, 32.0f, 32.0f) });
	OutProps.Add({ true, M::Metal, FVector(3000.0f, 800.0f, 21.0f), FVector(24.0f, 24.0f, 40.0f) }); // oil drum
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
