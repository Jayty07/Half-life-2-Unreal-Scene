#pragma once

#include "CoreMinimal.h"
#include "HL2BlockoutBase.h"

#include "HL2PointInsertionBlockout.generated.h"

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
class HL2BLOCKOUT_API AHL2PointInsertionBlockout : public AHL2BlockoutBase
{
	GENERATED_BODY()

public:
	AHL2PointInsertionBlockout();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Inside the arriving train car. */
	virtual FTransform GetPlayerStartTransform() const override;

	/** Spawn simulated crates and cans (the "pick up that can" moment) on BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HL2 Blockout")
	bool bSpawnPhysicsProps = true;

protected:
	virtual void BuildLayout() override;

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
};
