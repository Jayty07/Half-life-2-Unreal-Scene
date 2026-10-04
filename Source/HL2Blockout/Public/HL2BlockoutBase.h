#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "HL2BlockoutBase.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UStaticMesh;

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
	Plaster,
	PlasterWarm,
	RoofRed,
	RoofTeal,
	Foliage,
	FoliageDark,
	Grass,
	Rubble,
	/** Water surfaces have no collision (there is no swimming; the canal bed is below). */
	Water,
	Dirt,
	/** Hanging ivy / curtains the player can walk through (no collision). */
	Vines,
	/** Bright placeholder for gameplay spots (blockers, hidden caches, robot). */
	Marker,
	Count UMETA(Hidden)
};

/**
 * Shared base for code-built blockouts: one instanced static mesh component
 * per palette entry for engine cubes and cylinders, optional sky/lighting,
 * and Hammer-unit geometry helpers. Subclasses author their level in
 * BuildLayout(), which runs from OnConstruction.
 */
UCLASS(Abstract)
class HL2BLOCKOUT_API AHL2BlockoutBase : public AActor
{
	GENERATED_BODY()

public:
	AHL2BlockoutBase();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;

	/** World transform the player should spawn at. */
	UFUNCTION(BlueprintPure, Category = "HL2 Blockout")
	virtual FTransform GetPlayerStartTransform() const;

	/** Clears and regenerates every blockout instance. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "HL2 Blockout")
	void RebuildBlockout();

	/** Colour for each EHL2BlockoutMaterial entry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, EditFixedSize, Category = "HL2 Blockout")
	TArray<FLinearColor> Palette;

	/** Include a sun, sky light, sky atmosphere and height fog. Disable if the level has its own lighting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HL2 Blockout")
	bool bIncludeSkyAndLighting = true;

protected:
	/** Emit all geometry for this blockout (Hammer units, actor-local). */
	virtual void BuildLayout() {}

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

	void ClearInstances();
	void ApplyPalette();
	void ApplyLightingVisibility();

	/** Axis-aligned box from min/max corners in Hammer units. */
	void Box(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU);

	/** Rotated box: centre, full size and rotation (degrees) in Hammer units. */
	void OrientedBox(EHL2BlockoutMaterial Mat, const FVector& CenterHU, const FVector& SizeHU, const FRotator& Rotation);

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

	/** Barrel vault along X spanning [Y0, Y1], springing from BaseZ. The middle OpenTopSegments are left open. */
	void Vault(EHL2BlockoutMaterial Mat, float X0, float X1, float Y0, float Y1, float BaseZ, float RiseHU, int32 Segments, float ThicknessHU, int32 OpenTopSegments);

	/** Stepped (walkable) gable roof filling Min/Max, ridge along RidgeAxis (0 = X, 1 = Y). */
	void GableRoof(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, int32 RidgeAxis, int32 Steps);

	/** Grid of window panels inside a thin facade box (ThinAxis 0 = X, 1 = Y), one row per floor. */
	void FacadeWindows(EHL2BlockoutMaterial Mat, const FVector& MinHU, const FVector& MaxHU, int32 ThinAxis, float SpacingHU, float FloorHeightHU, float WidthHU, float HeightHU);
};
