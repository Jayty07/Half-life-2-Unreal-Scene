#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "HL2PointInsertionBlockout.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UStaticMesh;
class AStaticMeshActor;

UENUM(BlueprintType)
enum class EHL2BlockoutMaterial : uint8
{
	Concrete,
	DarkConcrete,
	Tile,
	Brick,
	Metal,
	Combine,
	Wood,
	Train,
	Screen,
	Trim,
	Count UMETA(Hidden)
};

/**
 * Grey-box layout of Half-Life 2's first chapter, "Point Insertion", following
 * its overview map: rail yard -> arrival platform under the long canopy (next
 * to the arched train shed) -> security queue and interrogation room -> station
 * hall -> City 17 plaza -> street -> courtyard -> Resistance apartments ->
 * rooftops -> attic -> end room.
 *
 * The whole layout is authored in Hammer units (1 HU = 1.905 cm, the same
 * scale PBCharacterMovement uses) and rebuilt from code in OnConstruction, so
 * dropping this actor into an empty level produces the full blockout.
 */
UCLASS()
class HL2BLOCKOUT_API AHL2PointInsertionBlockout : public AActor
{
	GENERATED_BODY()

public:
	AHL2PointInsertionBlockout();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** World transform the player should spawn at (inside the arriving train car). */
	UFUNCTION(BlueprintPure, Category = "HL2 Blockout")
	FTransform GetPlayerStartTransform() const;

	/** Clears and regenerates every blockout instance. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "HL2 Blockout")
	void RebuildBlockout();

	/** Colour for each EHL2BlockoutMaterial entry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, EditFixedSize, Category = "HL2 Blockout")
	TArray<FLinearColor> Palette;

	/** Include a sun, sky light, sky atmosphere and height fog. Disable if the level has its own lighting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HL2 Blockout")
	bool bIncludeSkyAndLighting = true;

	/** Spawn simulated crates and cans (the "pick up that can" moment) on BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HL2 Blockout")
	bool bSpawnPhysicsProps = true;

protected:
	UPROPERTY(VisibleAnywhere, Category = "HL2 Blockout")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "HL2 Blockout")
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> CubeInstances;

	UPROPERTY(VisibleAnywhere, Category = "HL2 Blockout")
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> CylinderInstances;

	UPROPERTY(VisibleAnywhere, Category = "HL2 Blockout|Lighting")
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere, Category = "HL2 Blockout|Lighting")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "HL2 Blockout|Lighting")
	TObjectPtr<USkyAtmosphereComponent> SkyAtmosphere;

	UPROPERTY(VisibleAnywhere, Category = "HL2 Blockout|Lighting")
	TObjectPtr<UExponentialHeightFogComponent> HeightFog;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PaletteMaterials;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedProps;

private:
	struct FPhysicsProp
	{
		bool bCylinder;
		EHL2BlockoutMaterial Material;
		FVector CenterHU;
		FVector SizeHU;
	};

	static void GetPhysicsProps(TArray<FPhysicsProp>& OutProps);

	void ClearInstances();
	void ApplyPalette();
	void ApplyLightingVisibility();
	void SpawnPhysicsProps();

	void BuildWorldBounds();
	void BuildRailYard();
	void BuildArrivalPlatform();
	void BuildTrainShed();
	void BuildSecurity();
	void BuildStationHall();
	void BuildPlaza();
	void BuildStreet();
	void BuildApartments();
	void BuildRooftops();
	void BuildAtticAndEnd();
	void BuildSkyline();

	/** Rails and sleepers along X, centred on CenterY. */
	void Track(float X0, float X1, float CenterY);

	/** Barrel vault along X spanning [Y0, Y1], springing from BaseZ. The middle OpenTopSegments are left open. */
	void Vault(EHL2BlockoutMaterial Mat, float X0, float X1, float Y0, float Y1, float BaseZ, float RiseHU, int32 Segments, float ThicknessHU, int32 OpenTopSegments);

	/** Stepped (walkable) gable roof filling Min/Max, ridge along RidgeAxis (0 = X, 1 = Y). */
	void GableRoof(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, int32 RidgeAxis, int32 Steps);

	/** Grid of window panels inside a thin facade box (ThinAxis 0 = X, 1 = Y), one row per floor. */
	void FacadeWindows(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, int32 ThinAxis, float SpacingHU, float FloorHeightHU, float WidthHU, float HeightHU);

	/** Axis-aligned box from min/max corners in Hammer units. */
	void Box(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU);

	/** Vertical cylinder: base centre, radius and height in Hammer units. */
	void Cylinder(EHL2BlockoutMaterial Mat, const FVector& BaseCenterHU, float RadiusHU, float HeightHU);

	/** Box with rectangular openings. Openings are given in the two axes other than ThinAxis (0 = X, 1 = Y). */
	void WallWithOpenings(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, int32 ThinAxis, const TArray<FBox2D>& Openings);

	/** Horizontal slab with a rectangular hole (XY in Hammer units). */
	void SlabWithHole(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, const FBox2D& HoleXY);

	/**
	 * Solid staircase. Start is the min corner of the first step; the stairs climb along Direction (+X, -X, +Y or -Y).
	 * Width is measured perpendicular to Direction, extending in the positive axis from Start.
	 */
	void Stairs(EHL2BlockoutMaterial Mat, const FVector& StartHU, const FIntPoint& Direction, int32 NumSteps, float RiseHU, float RunHU, float WidthHU);
};
