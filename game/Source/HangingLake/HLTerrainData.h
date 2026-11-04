// Runtime terrain data: raw lidar heights and the water depth field, loaded from
// the CSV/raw exports the Python package writes. One object, no landscape actor.
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HLTerrainData.generated.h"

/**
 * The bowl, at runtime.
 *
 * The Python package exports raw float32 heights rather than a landscape
 * heightmap image, so the game does not depend on an image plugin and the same
 * numbers feed both the rendered mesh and the physics/aim queries. Row 0 is
 * north, column 0 is west; terrain spans [0, ExtentX] x [0, ExtentY] in world XY.
 */
UCLASS(BlueprintType)
class HANGINGLAKE_API UHLTerrainData : public UObject
{
	GENERATED_BODY()

public:
	/** Load from the data directory produced by `hanginglake --out data`. */
	UFUNCTION(BlueprintCallable, Category = "Hanging Lake|Terrain")
	bool LoadFromDirectory(const FString& DataDir);

	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Terrain")
	float ElevationAt(float X, float Y) const;

	/** Water depth in cm at a world XY. Zero on land. */
	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Terrain")
	float DepthAt(float X, float Y) const;

	/** Water surface height in world Z, cm. */
	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Terrain")
	float GetWaterZ() const { return WaterZ; }

	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Terrain")
	FVector2D GetExtentCm() const { return ExtentCm; }

	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Terrain")
	bool IsLoaded() const { return Heights.Num() > 0; }

	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Terrain")
	int32 GetWidth() const { return Width; }

	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Terrain")
	int32 GetHeight() const { return Height; }

	/** Deepest water in the bowl, in world space. Where the lake actually is. */
	FVector GetDeepestPoint() const { return DeepestPoint; }

	/** A point on the shoreline to spawn the player, picked at load time. */
	UFUNCTION(BlueprintPure, Category = "Hanging Lake|Terrain")
	FVector GetShoreSpawn() const { return ShoreSpawn; }

	/** Centre of the lake's surface (mean of the underwater cells), at water level. */
	FVector GetLakeCentre() const { return LakeCentre; }

	/** Build the bowl's render mesh (procedural), added to the given owner. */
	void BuildMesh(class AActor* Owner);

private:
	TArray<float> Heights;   // metres, row-major, north-up
	TArray<float> Depths;    // metres below surface, <0 on land
	int32 Width = 0;
	int32 Height = 0;

	FVector2D ExtentCm = FVector2D::ZeroVector;
	float WaterZ = 0.0f;     // world Z of the water plane, cm
	float MinZ = 0.0f;       // world Z of the lowest terrain, cm (we offset so this is 0)
	float MaxZ = 0.0f;
	FVector ShoreSpawn = FVector::ZeroVector;
	FVector DeepestPoint = FVector::ZeroVector;
	FVector LakeCentre = FVector::ZeroVector;

	bool SampleGrid(const TArray<float>& Grid, float X, float Y, float& OutValue) const;
};
