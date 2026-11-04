// The bow: draw, release, and a physical arrow that survives the water entry.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HLBow.generated.h"

class ACharacter;
class UHLTerrainData;
class UStaticMeshComponent;
class AHLWaterSurface;

UCLASS()
class HANGINGLAKE_API AHLBow : public AActor
{
	GENERATED_BODY()

public:
	AHLBow();

	virtual void Tick(float DeltaSeconds) override;

	void Setup(ACharacter* InOwner, UHLTerrainData* InTerrain, AHLWaterSurface* InWater);

	/** Begin / end a draw. Release fires. */
	void StartDraw();
	void ReleaseDraw();

	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Bow")
	float GetDrawAlpha() const { return DrawAlpha; }

	/** True while an arrow is in flight, so the player cannot fire again. */
	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Bow")
	bool IsArrowInFlight() const { return bArrowInFlight; }

	/** Where the aim ray enters the water, for the HUD's surface marker. */
	bool GetAimWaterEntry(FVector& OutEntry, FVector& OutAimDir) const;

	/** Exposed for the teaching overlay: the last shot's bent path. */
	const TArray<FVector>& GetLastShotPath() const { return LastShotPath; }

	/** Fired when an arrow hits a fish. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnFishHit, class AHLFish*);
	FOnFishHit OnFishHit;

	/** Fired when an arrow misses everything and settles. */
	DECLARE_MULTICAST_DELEGATE(FOnShotMissed);
	FOnShotMissed OnShotMissed;

protected:
	virtual void BeginPlay() override;

	UPROPERTY()
	ACharacter* OwnerChar = nullptr;

	UPROPERTY()
	UHLTerrainData* Terrain = nullptr;

	UPROPERTY()
	AHLWaterSurface* Water = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Hanging Lake|Bow")
	UStaticMeshComponent* BowMesh = nullptr;

	/** The arrow you can see: follows the simulated flight, then stays put briefly. */
	UPROPERTY(VisibleAnywhere, Category = "Hanging Lake|Bow")
	UStaticMeshComponent* ArrowMesh = nullptr;

	float ArrowLinger = 0.0f;
	void PlaceArrowMesh();

	float DrawAlpha = 0.0f;
	bool bDrawing = false;
	bool bArrowInFlight = false;

	// Arrow state, integrated in Tick rather than by a projectile component so the
	// water transition is explicit.
	FVector ArrowPos = FVector::ZeroVector;
	FVector ArrowVel = FVector::ZeroVector;
	bool bArrowUnderwater = false;
	float ArrowTimer = 0.0f;

	TArray<FVector> LastShotPath;

	FVector GetEyeLocation() const;
	FVector GetAimDirection() const;
	void EndShot(bool bHit, class AHLFish* HitFish);

	/** One fixed step of flight; true when the shot has ended. */
	bool StepArrow(float Dt);
};
