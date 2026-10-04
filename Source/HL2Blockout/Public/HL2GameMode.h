#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "HL2GameMode.generated.h"

class AHL2BlockoutBase;

/**
 * Uses AHL2Character as the default pawn. If the current level has no
 * PlayerStart, the level's blockout actor is used (a Point Insertion blockout
 * is spawned when there is none and bAutoSpawnBlockout is set) and the player
 * starts at the blockout's start transform.
 */
UCLASS(Config = Game)
class HL2BLOCKOUT_API AHL2GameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHL2GameMode();

	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** Spawn the blockout at the world origin when a level has no PlayerStart and no blockout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "HL2")
	bool bAutoSpawnBlockout = true;

protected:
	AHL2BlockoutBase* FindOrSpawnBlockout();

	UPROPERTY(Transient)
	TObjectPtr<AActor> FallbackStart;
};
