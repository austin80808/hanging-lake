#include "HLPlayerController.h"

#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HLBow.h"
#include "HLGameMode.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

AHLPlayerController::AHLPlayerController()
{
	bShowMouseCursor = false;
	PrimaryActorTick.bCanEverTick = true;
}

void AHLPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (AHLGameMode* GM = Cast<AHLGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		Bow = GM->GetBow();
	}

	// Walking speed suited to a hiker on rock, not a sprint.
	if (ACharacter* Char = Cast<ACharacter>(GetPawn()))
	{
		if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
		{
			Move->MaxWalkSpeed = 200.0f;
		}
	}
}

void AHLPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Legacy binds: work immediately with no asset authoring. Enhanced Input can
	// replace these once the project has input actions made.
	// Every control is a named action or axis from Config/DefaultInput.ini, so any
	// key, mouse button or controller input can be rebound there (or in the
	// player's Saved/Config/<Platform>/Input.ini) without touching code.
	InputComponent->BindAction(TEXT("Draw"), IE_Pressed, this, &AHLPlayerController::OnDrawPressed);
	InputComponent->BindAction(TEXT("Draw"), IE_Released, this, &AHLPlayerController::OnDrawReleased);
	InputComponent->BindAction(TEXT("Quit"), IE_Pressed, this, &AHLPlayerController::OnQuit);
	InputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &AHLPlayerController::OnJump);
	InputComponent->BindAxis(TEXT("TurnRate"));
	InputComponent->BindAxis(TEXT("LookUpRate"));

	// Movement goes to the possessed character; look goes to the controller.
	InputComponent->BindAxis(TEXT("MoveForward"));
	InputComponent->BindAxis(TEXT("MoveRight"));
	InputComponent->BindAxis(TEXT("Turn"), this, &AHLPlayerController::OnTurn);
	InputComponent->BindAxis(TEXT("LookUp"), this, &AHLPlayerController::OnLookUp);
}

void AHLPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// Walk: the axes are bound in SetupInputComponent but nothing consumed them,
	// so W/A/S/D did nothing. Move on the ground plane relative to the view yaw.
	if (APawn* P = GetPawn())
	{
		const FRotator YawOnly(0.0f, GetControlRotation().Yaw, 0.0f);
		const float Fwd = GetInputAxisValue(TEXT("MoveForward"));
		const float Right = GetInputAxisValue(TEXT("MoveRight"));
		if (!FMath::IsNearlyZero(Fwd)) { P->AddMovementInput(FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X), Fwd); }
		if (!FMath::IsNearlyZero(Right)) { P->AddMovementInput(FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y), Right); }

		// Right stick look: a rate, so scale by time (degrees per second at full tilt).
		// Slower when the bow is drawn, for fine aim.
		const bool bDrawing = Bow && Bow->GetDrawAlpha() > 0.0f;
		const float Rate = bDrawing ? 22.0f : 55.0f;
		const float TurnRate = GetInputAxisValue(TEXT("TurnRate"));
		const float LookRate = GetInputAxisValue(TEXT("LookUpRate"));
		if (!FMath::IsNearlyZero(TurnRate)) { AddYawInput(TurnRate * Rate * DeltaTime); }
		if (!FMath::IsNearlyZero(LookRate)) { AddPitchInput(LookRate * Rate * DeltaTime); }
	}

	if (!Bow)
	{
		if (AHLGameMode* GM = Cast<AHLGameMode>(UGameplayStatics::GetGameMode(this)))
		{
			Bow = GM->GetBow();
		}
	}
}

void AHLPlayerController::OnDrawPressed()
{
	if (Bow)
	{
		Bow->StartDraw();
	}
}

void AHLPlayerController::OnDrawReleased()
{
	if (Bow)
	{
		Bow->ReleaseDraw();
	}
}

void AHLPlayerController::OnTurn(float Value)
{
	if (!FMath::IsNearlyZero(Value))
	{
		AddYawInput(Value);
	}
}

void AHLPlayerController::OnLookUp(float Value)
{
	if (!FMath::IsNearlyZero(Value))
	{
		AddPitchInput(Value);
	}
}

void AHLPlayerController::OnJump()
{
	if (ACharacter* Char = Cast<ACharacter>(GetPawn()))
	{
		Char->Jump();
	}
}

void AHLPlayerController::OnQuit()
{
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}
