// Spawns the bowl, populates the lake, wires the bow. No level asset needed:
// the whole playable scene is built at BeginPlay from the exported geodata.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HLGameMode.generated.h"

class AHLBow;
class AHLFish;
class AHLWaterSurface;
class UHLTerrainData;

UCLASS()
class HANGINGLAKE_API AHLGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHLGameMode();

	virtual void StartPlay() override;

	UFUNCTION(BlueprintPure, Category = "Hanging Lake")
	UHLTerrainData* GetTerrain() const { return Terrain; }

	UFUNCTION(BlueprintPure, Category = "Hanging Lake")
	AHLWaterSurface* GetWater() const { return Water; }

	UFUNCTION(BlueprintPure, Category = "Hanging Lake")
	AHLBow* GetBow() const { return Bow; }

	UFUNCTION(BlueprintPure, Category = "Hanging Lake")
	const TArray<AHLFish*>& GetFish() const { return Fish; }

	UFUNCTION(BlueprintCallable, Category = "Hanging Lake")
	int32 CountAlive() const;

	void FixLighting(UWorld* World);

	void AutoShoot();

	FVector SpawnLog(UWorld* World);
	void SpawnBoardwalk(UWorld* World);
	FVector GetSpawnPoint() const;
	float BoardwalkDeckZ = 0.0f;

	/** Absolute path to the exported data directory. */
	UPROPERTY(EditAnywhere, Category = "Hanging Lake")
	FString DataDir;

	/** How many fish to place in the bowl. Small and countable, on purpose. */
	UPROPERTY(EditAnywhere, Category = "Hanging Lake")
	int32 FishCount = 18;

protected:
	UPROPERTY()
	UHLTerrainData* Terrain = nullptr;

	UPROPERTY()
	AHLWaterSurface* Water = nullptr;

	UPROPERTY()
	AHLBow* Bow = nullptr;

	UPROPERTY()
	TArray<AHLFish*> Fish;

	void SpawnFish();
	FVector FindDeepWater(float MinDepthCm) const;
};
