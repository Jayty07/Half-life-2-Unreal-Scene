#include "HL2Character.h"

#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HL2Character)

namespace
{
	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name, RF_Transient);
		Action->ValueType = ValueType;
		return Action;
	}

	void AddModifier(FEnhancedActionKeyMapping& Mapping, UInputModifier* Modifier)
	{
		Mapping.Modifiers.Add(Modifier);
	}
}

AHL2Character::AHL2Character(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Flashlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Flashlight"));
	Flashlight->SetupAttachment(GetCapsuleComponent());
	Flashlight->SetMobility(EComponentMobility::Movable);
	Flashlight->SetRelativeLocation(FVector(0.0f, 15.0f, BaseEyeHeight - 10.0f));
	Flashlight->SetIntensityUnits(ELightUnits::Candelas);
	Flashlight->SetIntensity(400.0f);
	Flashlight->SetAttenuationRadius(2000.0f);
	Flashlight->SetInnerConeAngle(10.0f);
	Flashlight->SetOuterConeAngle(24.0f);
	Flashlight->SetCastShadows(true);
	Flashlight->SetVisibility(false);
}

void AHL2Character::EnsureDefaultInput()
{
	if (!MoveAction)       { MoveAction = MakeAction(this, TEXT("IA_Move"), EInputActionValueType::Axis2D); }
	if (!LookAction)       { LookAction = MakeAction(this, TEXT("IA_Look"), EInputActionValueType::Axis2D); }
	if (!JumpAction)       { JumpAction = MakeAction(this, TEXT("IA_Jump"), EInputActionValueType::Boolean); }
	if (!CrouchAction)     { CrouchAction = MakeAction(this, TEXT("IA_Crouch"), EInputActionValueType::Boolean); }
	if (!SprintAction)     { SprintAction = MakeAction(this, TEXT("IA_Sprint"), EInputActionValueType::Boolean); }
	if (!WalkAction)       { WalkAction = MakeAction(this, TEXT("IA_Walk"), EInputActionValueType::Boolean); }
	if (!FlashlightAction) { FlashlightAction = MakeAction(this, TEXT("IA_Flashlight"), EInputActionValueType::Boolean); }
	if (!NoClipAction)     { NoClipAction = MakeAction(this, TEXT("IA_NoClip"), EInputActionValueType::Boolean); }

	if (DefaultMappingContext)
	{
		return;
	}

	UInputMappingContext* Context = NewObject<UInputMappingContext>(this, TEXT("IMC_HL2Default"), RF_Transient);

	// Move: X = right, Y = forward.
	AddModifier(Context->MapKey(MoveAction, EKeys::W), NewObject<UInputModifierSwizzleAxis>(Context));
	{
		FEnhancedActionKeyMapping& Back = Context->MapKey(MoveAction, EKeys::S);
		AddModifier(Back, NewObject<UInputModifierSwizzleAxis>(Context));
		AddModifier(Back, NewObject<UInputModifierNegate>(Context));
	}
	Context->MapKey(MoveAction, EKeys::D);
	AddModifier(Context->MapKey(MoveAction, EKeys::A), NewObject<UInputModifierNegate>(Context));

	Context->MapKey(LookAction, EKeys::Mouse2D);

	Context->MapKey(JumpAction, EKeys::SpaceBar);
	Context->MapKey(CrouchAction, EKeys::LeftControl);
	Context->MapKey(SprintAction, EKeys::LeftShift);
	Context->MapKey(WalkAction, EKeys::LeftAlt);
	Context->MapKey(FlashlightAction, EKeys::F);
	Context->MapKey(NoClipAction, EKeys::V);

	DefaultMappingContext = Context;
}

void AHL2Character::PawnClientRestart()
{
	Super::PawnClientRestart();

	EnsureDefaultInput();

	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->RemoveMappingContext(DefaultMappingContext);
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

void AHL2Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	EnsureDefaultInput();

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogTemp, Error, TEXT("AHL2Character requires EnhancedInputComponent as the default input component class (see Config/DefaultInput.ini)."));
		return;
	}

	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AHL2Character::HandleMove);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AHL2Character::HandleLook);

	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &AHL2Character::HandleJumpPressed);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &AHL2Character::HandleJumpReleased);

	Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &AHL2Character::HandleCrouchPressed);
	Input->BindAction(CrouchAction, ETriggerEvent::Completed, this, &AHL2Character::HandleCrouchReleased);

	Input->BindAction(SprintAction, ETriggerEvent::Started, this, &AHL2Character::HandleSprintPressed);
	Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &AHL2Character::HandleSprintReleased);

	Input->BindAction(WalkAction, ETriggerEvent::Started, this, &AHL2Character::HandleWalkPressed);
	Input->BindAction(WalkAction, ETriggerEvent::Completed, this, &AHL2Character::HandleWalkReleased);

	Input->BindAction(FlashlightAction, ETriggerEvent::Started, this, &AHL2Character::HandleFlashlight);
	Input->BindAction(NoClipAction, ETriggerEvent::Started, this, &AHL2Character::HandleNoClip);
}

void AHL2Character::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (Flashlight && Flashlight->IsVisible())
	{
		Flashlight->SetRelativeLocation(FVector(0.0f, 15.0f, BaseEyeHeight - 10.0f));
		Flashlight->SetWorldRotation(GetControlRotation());
	}
}

void AHL2Character::HandleMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	MoveForward(Axis.Y);
	MoveRight(Axis.X);
}

void AHL2Character::HandleLook(const FInputActionValue& Value)
{
	// Rotate the control rotation directly so the result does not depend on
	// the project's legacy input scale settings.
	AController* PC = GetController();
	if (!PC)
	{
		return;
	}

	const FVector2D Delta = Value.Get<FVector2D>() * MouseSensitivity;
	FRotator Rotation = PC->GetControlRotation();
	Rotation.Yaw += Delta.X;
	Rotation.Pitch = FMath::ClampAngle(FRotator::NormalizeAxis(Rotation.Pitch) + (bInvertMouseY ? -Delta.Y : Delta.Y), -89.0f, 89.0f);
	PC->SetControlRotation(Rotation);
}

void AHL2Character::HandleJumpPressed()
{
	Jump();
}

void AHL2Character::HandleJumpReleased()
{
	StopJumping();
}

void AHL2Character::HandleCrouchPressed()
{
	if (bHoldToCrouch)
	{
		OnCrouch();
	}
	else
	{
		CrouchToggle();
	}
}

void AHL2Character::HandleCrouchReleased()
{
	if (bHoldToCrouch)
	{
		OnUnCrouch();
	}
}

void AHL2Character::HandleSprintPressed()
{
	SetSprinting(true);
}

void AHL2Character::HandleSprintReleased()
{
	SetSprinting(false);
}

void AHL2Character::HandleWalkPressed()
{
	SetWantsToWalk(true);
}

void AHL2Character::HandleWalkReleased()
{
	SetWantsToWalk(false);
}

void AHL2Character::HandleFlashlight()
{
	if (Flashlight)
	{
		Flashlight->ToggleVisibility();
		Flashlight->SetWorldRotation(GetControlRotation());
	}
}

void AHL2Character::HandleNoClip()
{
	ToggleNoClip();
}
