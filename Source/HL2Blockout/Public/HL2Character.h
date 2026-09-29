#pragma once

#include "CoreMinimal.h"
#include "Character/PBPlayerCharacter.h"
#include "InputActionValue.h"

#include "HL2Character.generated.h"

class UInputAction;
class UInputMappingContext;
class USpotLightComponent;

/**
 * Player pawn built on APBPlayerCharacter (HL2 movement) with Enhanced Input
 * bindings that match Half-Life 2's default controls.
 *
 * Input assets are optional: if none are assigned (e.g. in a Blueprint child),
 * equivalent actions and a mapping context are created at runtime so the
 * project works without any .uasset files.
 */
UCLASS(Config = Game)
class HL2BLOCKOUT_API AHL2Character : public APBPlayerCharacter
{
	GENERATED_BODY()

public:
	AHL2Character(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaTime) override;
	virtual void PawnClientRestart() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Degrees of rotation per unit of raw mouse delta (Source: sensitivity * m_yaw/m_pitch 0.022). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "HL2|Input")
	float MouseSensitivity = 0.066f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "HL2|Input")
	bool bInvertMouseY = false;

	/** Hold to crouch (HL2 default). When false, the crouch key toggles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "HL2|Input")
	bool bHoldToCrouch = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HL2|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HL2|Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HL2|Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HL2|Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HL2|Input")
	TObjectPtr<UInputAction> CrouchAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HL2|Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HL2|Input")
	TObjectPtr<UInputAction> WalkAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HL2|Input")
	TObjectPtr<UInputAction> FlashlightAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HL2|Input")
	TObjectPtr<UInputAction> NoClipAction;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HL2")
	TObjectPtr<USpotLightComponent> Flashlight;

	void EnsureDefaultInput();

	void HandleMove(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleJumpPressed();
	void HandleJumpReleased();
	void HandleCrouchPressed();
	void HandleCrouchReleased();
	void HandleSprintPressed();
	void HandleSprintReleased();
	void HandleWalkPressed();
	void HandleWalkReleased();
	void HandleFlashlight();
	void HandleNoClip();
};
