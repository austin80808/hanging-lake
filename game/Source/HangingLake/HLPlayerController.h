// Input: Enhanced Input with a fallback to legacy binds so the project runs
// without hand-authoring input assets first.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HLPlayerController.generated.h"

class AHLBow;

UCLASS()
class HANGINGLAKE_API AHLPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AHLPlayerController();

	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

protected:
	void OnDrawPressed();
	void OnDrawReleased();
	void OnQuit();
	void OnJump();
	void OnTurn(float Value);
	void OnLookUp(float Value);

	UPROPERTY()
	AHLBow* Bow = nullptr;
};
