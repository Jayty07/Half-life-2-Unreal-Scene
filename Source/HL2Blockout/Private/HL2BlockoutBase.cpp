#include "HL2BlockoutBase.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HL2BlockoutBase)

namespace HL2BlockoutBaseImpl
{
	/** Hammer unit -> Unreal centimetre (matches PBCharacterMovement's conversions). */
	constexpr float HU = 1.905f;

	constexpr int32 NumMaterials = static_cast<int32>(EHL2BlockoutMaterial::Count);

	const TCHAR* MaterialNames[NumMaterials] = {
		TEXT("Concrete"), TEXT("DarkConcrete"), TEXT("Tile"), TEXT("Brick"), TEXT("Metal"),
		TEXT("Combine"), TEXT("Wood"), TEXT("Train"), TEXT("Screen"), TEXT("Trim"),
		TEXT("Plaster"), TEXT("PlasterWarm"), TEXT("RoofRed"), TEXT("RoofTeal"), TEXT("Foliage"),
		TEXT("FoliageDark"), TEXT("Grass"), TEXT("Rubble"), TEXT("Water"), TEXT("Dirt"),
		TEXT("Vines"), TEXT("Marker"),
	};

	FORCEINLINE FVector ToCm(const FVector& V) { return V * HU; }
}


AHL2BlockoutBase::AHL2BlockoutBase()
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

	for (int32 Index = 0; Index < HL2BlockoutBaseImpl::NumMaterials; ++Index)
	{
		CubeInstances.Add(MakeISM(FString::Printf(TEXT("Boxes_%s"), HL2BlockoutBaseImpl::MaterialNames[Index]), CubeMesh));
		CylinderInstances.Add(MakeISM(FString::Printf(TEXT("Cylinders_%s"), HL2BlockoutBaseImpl::MaterialNames[Index]), CylinderMesh));
	}

	for (const EHL2BlockoutMaterial WalkThrough : { EHL2BlockoutMaterial::Vines, EHL2BlockoutMaterial::Water })
	{
		CubeInstances[static_cast<int32>(WalkThrough)]->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		CylinderInstances[static_cast<int32>(WalkThrough)]->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	}

	Palette.Init(FLinearColor(0.4f, 0.4f, 0.4f), HL2BlockoutBaseImpl::NumMaterials);

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
void AHL2BlockoutBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildBlockout();
}

void AHL2BlockoutBase::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	ApplyPalette();
	ApplyLightingVisibility();
}

FTransform AHL2BlockoutBase::GetPlayerStartTransform() const
{
	return FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, 100.0f)) * GetActorTransform();
}

void AHL2BlockoutBase::ClearInstances()
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


void AHL2BlockoutBase::RebuildBlockout()
{
	ClearInstances();
	BuildLayout();
	ApplyPalette();
	ApplyLightingVisibility();
}

void AHL2BlockoutBase::ApplyPalette()
{
	if (HasAnyFlags(RF_ClassDefaultObject) || !BaseMaterial)
	{
		return;
	}

	// Levels saved before the palette grew keep their colours; new entries come from the class defaults.
	if (Palette.Num() < HL2BlockoutBaseImpl::NumMaterials)
	{
		const TArray<FLinearColor>& Defaults = GetClass()->GetDefaultObject<AHL2BlockoutBase>()->Palette;
		for (int32 Index = Palette.Num(); Index < HL2BlockoutBaseImpl::NumMaterials; ++Index)
		{
			Palette.Add(Defaults.IsValidIndex(Index) ? Defaults[Index] : FLinearColor::Gray);
		}
	}
	Palette.SetNum(HL2BlockoutBaseImpl::NumMaterials);
	PaletteMaterials.SetNum(HL2BlockoutBaseImpl::NumMaterials);

	for (int32 Index = 0; Index < HL2BlockoutBaseImpl::NumMaterials; ++Index)
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

void AHL2BlockoutBase::ApplyLightingVisibility()
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

void AHL2BlockoutBase::Box(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU)
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
	CubeInstances[Index]->AddInstance(FTransform(FQuat::Identity, HL2BlockoutBaseImpl::ToCm((MinHU + MaxHU) * 0.5f), HL2BlockoutBaseImpl::ToCm(Size) / 100.0f));
}

void AHL2BlockoutBase::OrientedBox(EHL2BlockoutMaterial Mat, const FVector& CenterHU, const FVector& SizeHU, const FRotator& Rotation)
{
	const int32 Index = static_cast<int32>(Mat);
	if (SizeHU.X <= UE_KINDA_SMALL_NUMBER || SizeHU.Y <= UE_KINDA_SMALL_NUMBER || SizeHU.Z <= UE_KINDA_SMALL_NUMBER
		|| !CubeInstances.IsValidIndex(Index) || !CubeInstances[Index])
	{
		return;
	}

	CubeInstances[Index]->AddInstance(FTransform(Rotation.Quaternion(), HL2BlockoutBaseImpl::ToCm(CenterHU), HL2BlockoutBaseImpl::ToCm(SizeHU) / 100.0f));
}

void AHL2BlockoutBase::Cylinder(EHL2BlockoutMaterial Mat, const FVector& BaseCenterHU, float RadiusHU, float HeightHU)
{
	const int32 Index = static_cast<int32>(Mat);
	if (RadiusHU <= 0.0f || HeightHU <= 0.0f || !CylinderInstances.IsValidIndex(Index) || !CylinderInstances[Index])
	{
		return;
	}

	// Engine cylinder is 100 cm wide/tall with a centred pivot.
	const FVector Center = BaseCenterHU + FVector(0.0f, 0.0f, HeightHU * 0.5f);
	const FVector Scale = HL2BlockoutBaseImpl::ToCm(FVector(RadiusHU * 2.0f, RadiusHU * 2.0f, HeightHU)) / 100.0f;
	CylinderInstances[Index]->AddInstance(FTransform(FQuat::Identity, HL2BlockoutBaseImpl::ToCm(Center), Scale));
}

void AHL2BlockoutBase::WallWithOpenings(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, int32 ThinAxis, const TArray<FBox2D>& Openings)
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

void AHL2BlockoutBase::SlabWithHole(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, const FBox2D& HoleXY)
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

void AHL2BlockoutBase::Stairs(EHL2BlockoutMaterial Mat, const FVector& StartHU, const FIntPoint& Direction, int32 NumSteps, float RiseHU, float RunHU, float WidthHU)
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

void AHL2BlockoutBase::Vault(EHL2BlockoutMaterial Mat, float X0, float X1, float Y0, float Y1, float BaseZ, float RiseHU, int32 Segments, float ThicknessHU, int32 OpenTopSegments)
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

void AHL2BlockoutBase::GableRoof(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, int32 RidgeAxis, int32 Steps)
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

void AHL2BlockoutBase::FacadeWindows(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, int32 ThinAxis, float SpacingHU, float FloorHeightHU, float WidthHU, float HeightHU)
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
