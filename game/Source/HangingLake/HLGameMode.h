// Spawns the bowl, populates the lake, wires the bow. No level asset needed:
// the whole playable scene is built at BeginPlay from the exported geodata.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HLJournal.h"
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
	void SpawnFalls(UWorld* World);
	void SpawnTrees(UWorld* World);
	FVector GetSpawnPoint() const;
	float BoardwalkDeckZ = 0.0f;
	/** Top of the log, shore end first (for the walk test). */
	TArray<FVector> LogPath;

	/** Absolute path to the exported data directory. */
	UPROPERTY(EditAnywhere, Category = "Hanging Lake")
	FString DataDir;

	/** How many fish to place in the bowl. Small and countable, on purpose. */
	UPROPERTY(EditAnywhere, Category = "Hanging Lake")
	int32 FishCount = 18;

	/** Seed for the population, so the same fish live here every visit (and the
	 *  journal's "No. 7" is the same fish tomorrow). 7177 ft: the lake's elevation. */
	UPROPERTY(EditAnywhere, Category = "Hanging Lake")
	int32 PopulationSeed = 7177;

	/** Bow range for the journal's "stalked": this close without spooking it. */
	UPROPERTY(EditAnywhere, Category = "Hanging Lake")
	float StalkRangeCm = 800.0f;

	/** What the player has learned, kept between sessions. */
	const FHLJournal& GetJournal() const { return Journal; }

	/** The newest journal line and how long ago it was written, for the HUD. */
	bool GetJournalNote(FString& OutNote, float& OutAgeSeconds) const;

	/** A name for where a point in the lake is: "under the log", "below the falls"... */
	FString PlaceName(const FVector& Where) const;

	/** Sun height above the horizon, degrees (fixed lighting, see FixLighting). */
	float GetSunElevationDeg() const { return SunElevationDeg; }

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
	FVector FindDeepWater(float MinDepthCm, FRandomStream& Stream) const;

	/** Shade to rest in: under the log, below the falls. Built after the log. */
	TArray<FVector> CoverPoints;
	void BuildCoverPoints();

	FHLJournal Journal;
	TMap<int32, float> SeenSeconds;  // per fish: time in view this session
	TMap<int32, float> CloseSeconds; // per fish: time within stalking range, unspooked
	FString JournalNote;
	float JournalNoteTime = -1000.0f;
	float SunElevationDeg = 38.0f;

	/** Every quarter second: has the player watched or crept up on a fish? */
	void UpdateJournal(float DeltaSeconds);
	void RecordJournal(class AHLFish* Fish, EHLJournalStage Stage, float DistanceCm);
	void OnFishLanded(class AHLFish* Fish);
};
