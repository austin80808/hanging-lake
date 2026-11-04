// HUD: reticle at the true aim point, draw meter, and the first-catch overlay
// that traces the bent arrow path so the refraction is learnable.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HLHUD.generated.h"

class AHLBow;
class AHLFish;
class AHLGameMode;

UCLASS()
class HANGINGLAKE_API AHLHUD : public AHUD
{
	GENERATED_BODY()

public:
	AHLHUD();

	virtual void DrawHUD() override;

	/** Called when a fish is landed; starts the teaching overlay. */
	void SetLastCatch(AHLFish* Fish);

	void HandleFishHit(AHLFish* Fish);

protected:
	void DrawReticle();
	void DrawDrawMeter(AHLBow* Bow);
	void DrawBentPath(AHLBow* Bow);
	void DrawStatus(AHLGameMode* GM);

	UPROPERTY()
	AHLFish* LastCatch = nullptr;

	UPROPERTY()
	AHLBow* BoundBow = nullptr;

	float CatchOverlayTimer = 0.0f;
	float StartTime = 0.0f;

	// The first landed fish gets a much longer, louder overlay: this is the moment
	// the player is supposed to understand the mechanic.
	bool bTaughtRefraction = false;
};
