// The lake surface: a translucent plane at the water line, with the depth read
// from the real basin underneath. Refraction is physical (see HLSnellSolver);
// this actor only draws the surface and answers "where is the water".
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HLWaterSurface.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UHLTerrainData;

UCLASS()
class HANGINGLAKE_API AHLWaterSurface : public AActor
{
	GENERATED_BODY()

public:
	AHLWaterSurface();

	/** Build the surface from the terrain bounds and water height. */
	void Build(UHLTerrainData* InTerrain);

	/** Where a world-space ray first crosses the water, if it does. */
	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Water")
	bool RayWaterEntry(const FVector& Origin, const FVector& Dir, FVector& OutEntry) const;

	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Water")
	float GetWaterZ() const { return WaterZ; }

protected:
	UPROPERTY()
	UProceduralMeshComponent* Surface = nullptr;

	UPROPERTY()
	UHLTerrainData* Terrain = nullptr;

	float WaterZ = 0.0f;
};
