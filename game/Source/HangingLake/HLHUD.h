// HUD: reticle at the true aim point, draw meter, the first-catch overlay that
// traces the bent arrow path so the refraction is learnable, and the journal.
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

	/** Show or hide the journal page (J / top face button). */
	void ToggleJournal() { bShowJournal = !bShowJournal; }

protected:
	void DrawReticle(AHLBow* Bow);
	void DrawDrawMeter(AHLBow* Bow);
	void DrawAimEntry(AHLBow* Bow);
	void DrawBentPath(AHLBow* Bow);
	void DrawCatchMarkers();
	void DrawStatus(AHLGameMode* GM, AHLBow* Bow);
	void DrawJournalNote(AHLGameMode* GM);
	void DrawJournal(AHLGameMode* GM);
	void DrawRing(const FVector2D& Centre, float Radius, const FLinearColor& Colour, float Thickness);

	UPROPERTY()
	AHLFish* LastCatch = nullptr;

	UPROPERTY()
	AHLBow* BoundBow = nullptr;

	float CatchOverlayTimer = 0.0f;
	float StartTime = 0.0f;

	// The first landed fish gets a much longer, louder overlay: this is the moment
	// the player is supposed to understand the mechanic.
	bool bTaughtRefraction = false;

	// Where the last fish really was and where it looked to be, at the hit.
	FVector LastCatchReal = FVector::ZeroVector;
	FVector LastCatchImage = FVector::ZeroVector;

	bool bShowJournal = false;
};
