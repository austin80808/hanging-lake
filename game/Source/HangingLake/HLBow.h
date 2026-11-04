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

	/** The last shot as the arrow actually flew (gravity arc and the bend), for
	 *  the teaching overlay. */
	const TArray<FVector>& GetLastShotPath() const { return LastShotPath; }

	/** Index in GetLastShotPath() of the surface crossing, or INDEX_NONE. */
	int32 GetLastShotEntryIndex() const { return LastShotEntryIndex; }

	/**
	 * True: the arrow refracts at the surface the way light does, so a shot at the
	 * fish's image lands on the fish (gravity aside). False (-hlstraightarrow): the
	 * arrow keeps its line through the surface like a real one, so the player has
	 * to aim below the image, as a spearfisher does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hanging Lake|Bow")
	bool bArrowBendsLikeLight = true;

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
	int32 LastShotEntryIndex = INDEX_NONE;
	int32 StepsSinceSample = 0;

	FVector GetEyeLocation() const;
	FVector GetAimDirection() const;
	void EndShot(bool bHit, class AHLFish* HitFish);

	/** One fixed step of flight; true when the shot has ended. */
	bool StepArrow(float Dt);
};
