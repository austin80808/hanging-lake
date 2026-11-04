// The player: a walking body with a first-person camera. A bare ACharacter has
// no camera, which is why the first run rendered black.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "HLPlayerCharacter.generated.h"

class UCameraComponent;

UCLASS()
class HANGINGLAKE_API AHLPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AHLPlayerCharacter();

	UFUNCTION(BlueprintPure, Category = "Hanging Lake")
	UCameraComponent* GetFirstPersonCamera() const { return Camera; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Hanging Lake")
	UCameraComponent* Camera = nullptr;
};
