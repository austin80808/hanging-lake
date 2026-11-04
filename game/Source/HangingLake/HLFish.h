// A fish. Small state machine driven by cover and the player; spooks and remembers.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HLFish.generated.h"

UENUM(BlueprintType)
enum class EHLFishState : uint8
{
	Hold,     // hovering over structure, least visible
	Cruise,   // slow patrol along the shelf
	Startle,  // bolted for deep water
	Rest      // under the falls or an undercut ledge
};

/**
 * One trout. Two bodies: the real one (collision, shadow, what an arrow hits) and
 * the image the player sees, drawn where refraction puts it: straight above the
 * real fish and shallower, on the player's line of sight. The renderer's water is
 * a flat tint and does not bend light, so the bend is done here, per fish.
 */
UCLASS()
class HANGINGLAKE_API AHLFish : public AActor
{
	GENERATED_BODY()

public:
	AHLFish();

	virtual void Tick(float DeltaSeconds) override;

	void Init(class UHLTerrainData* InTerrain, const FVector& HomePoint, float InBodyLengthCm);

	/** Places to rest out of the light: under the log, below the falls. */
	void SetCoverPoints(const TArray<FVector>& InCover) { CoverPoints = InCover; }

	/** Body colour of the visible fish. */
	void SetColour(const FLinearColor& Colour);

	/** A miss, a shadow or a near hit. Sends it into deep water for a while. */
	UFUNCTION(BlueprintCallable, Category = "Hanging Lake|Fish")
	void Spook();

	/** The bolt itself, after the reaction delay Spook() starts. */
	void StartleNow();

	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Fish")
	EHLFishState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Fish")
	float GetBodyLengthCm() const { return BodyLengthCm; }

	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Fish")
	float GetDepthCm() const;

	/** Where the player sees this fish (its refracted image). Equals the real
	 *  location when the eye is under water or -hlnoimage is set. */
	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Fish")
	FVector GetImageLocation() const;

	/** How many times it has been spooked, fading slowly. Wary fish spook sooner. */
	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Fish")
	float GetWariness() const { return Wariness; }

	/** Which of the lake's fish this is (1-based), stable between sessions. */
	UPROPERTY(BlueprintReadOnly, Category = "Hanging Lake|Fish")
	int32 FishId = 0;

	/** A short code for the journal. Hanging Lake's own trout are cutthroat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hanging Lake|Fish")
	FName SpeciesId = TEXT("cutthroat");

	/** Mark this fish as landed: it leaves the population for good. */
	UFUNCTION(BlueprintCallable, Category = "Hanging Lake|Fish")
	void OnLanded();

	UPROPERTY(BlueprintReadOnly, Category = "Hanging Lake|Fish")
	bool bLanded = false;

protected:
	virtual void BeginPlay() override;

	UPROPERTY()
	class UHLTerrainData* Terrain = nullptr;

	/** The real fish: collision and shadow. Hidden unless -hlnoimage. */
	UPROPERTY(VisibleAnywhere, Category = "Hanging Lake|Fish")
	class UStaticMeshComponent* Body = nullptr;

	/** The fish as seen through the surface. No collision, no shadow. */
	UPROPERTY(VisibleAnywhere, Category = "Hanging Lake|Fish")
	class UStaticMeshComponent* Image = nullptr;

	EHLFishState State = EHLFishState::Hold;
	FVector HomePoint = FVector::ZeroVector;
	FVector TargetPoint = FVector::ZeroVector;
	TArray<FVector> CoverPoints;
	float BodyLengthCm = 28.0f;
	float StateTimer = 0.0f;
	/** Seconds until a pending spook turns into a bolt; 0 = none pending. */
	float SpookDelay = 0.0f;
	float SwimPhase = 0.0f;
	float Wariness = 0.0f;
	float Heading = 0.0f;
	bool bDrawImage = true;

	void PickNextTarget();
	bool PickRestTarget();
	void KeepUnderwater();
	void WatchForPlayer();
	void UpdateImage();
};
