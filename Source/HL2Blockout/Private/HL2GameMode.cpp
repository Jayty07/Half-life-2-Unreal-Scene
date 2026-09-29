#include "HL2GameMode.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "HL2Character.h"
#include "HL2PointInsertionBlockout.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HL2GameMode)

AHL2GameMode::AHL2GameMode()
{
	DefaultPawnClass = AHL2Character::StaticClass();
}

AActor* AHL2GameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	UWorld* World = GetWorld();
	if (!World || TActorIterator<APlayerStart>(World))
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	if (IsValid(FallbackStart))
	{
		return FallbackStart;
	}

	if (AHL2PointInsertionBlockout* Blockout = FindOrSpawnBlockout())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		FallbackStart = World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), Blockout->GetPlayerStartTransform(), Params);
		if (FallbackStart)
		{
			return FallbackStart;
		}
	}

	return Super::ChoosePlayerStart_Implementation(Player);
}

AHL2PointInsertionBlockout* AHL2GameMode::FindOrSpawnBlockout()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (TActorIterator<AHL2PointInsertionBlockout> It(World); It)
	{
		return *It;
	}

	if (!bAutoSpawnBlockout)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<AHL2PointInsertionBlockout>(AHL2PointInsertionBlockout::StaticClass(), FTransform::Identity, Params);
}
