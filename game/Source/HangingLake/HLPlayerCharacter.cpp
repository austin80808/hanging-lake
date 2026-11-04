#include "HLPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

AHLPlayerCharacter::AHLPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(40.0f, 90.0f);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(10.0f, 0.0f, 70.0f)); // eye height
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(84.0f);

	// Movement: a hiker on rock, not a sprinter. The canyon is the point, so the
	// pace is deliberately slow enough to look around.
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = 200.0f;
		Move->BrakingDecelerationWalking = 1400.0f;
		Move->AirControl = 0.15f;
	}

	// First person: the body does not need to block the camera's view of itself.
	GetMesh()->SetOwnerNoSee(true);
	bUseControllerRotationYaw = true;

	// The spawn sits on a shore ledge well above the water. Look down the bowl
	// toward the lake so the first frame shows water, not a wall of rock.
	// AHLGameMode overrides this at spawn once it knows the terrain heights.
	// Pitch stays on the camera only. Pitching the whole character tilted the
	// capsule when aiming down at the lake and it slid off the spawn ledge.
	bUseControllerRotationPitch = false;
}
