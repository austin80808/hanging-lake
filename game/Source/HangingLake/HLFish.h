// A fish. Small state machine driven by light and cover; spooks and remembers.
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

UCLASS()
class HANGINGLAKE_API AHLFish : public AActor
{
	GENERATED_BODY()

public:
	AHLFish();

	virtual void Tick(float DeltaSeconds) override;

	void Init(class UHLTerrainData* InTerrain, const FVector& HomePoint, float InBodyLengthCm);

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

	/** A short code for the journal, e.g. "brook". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hanging Lake|Fish")
	FName SpeciesId = TEXT("brook");

	/** Mark this fish as landed: it leaves the population for good. */
	UFUNCTION(BlueprintCallable, Category = "Hanging Lake|Fish")
	void OnLanded();

	UPROPERTY(BlueprintReadOnly, Category = "Hanging Lake|Fish")
	bool bLanded = false;

protected:
	virtual void BeginPlay() override;

	UPROPERTY()
	UHLTerrainData* Terrain = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Hanging Lake|Fish")
	class UStaticMeshComponent* Body = nullptr;

	EHLFishState State = EHLFishState::Hold;
	FVector HomePoint = FVector::ZeroVector;
	FVector TargetPoint = FVector::ZeroVector;
	float BodyLengthCm = 28.0f;
	float StateTimer = 0.0f;
	/** Seconds until a pending spook turns into a bolt; 0 = none pending. */
	float SpookDelay = 0.0f;
	float SwimPhase = 0.0f;

	void PickNextTarget();
	void KeepUnderwater();
};
