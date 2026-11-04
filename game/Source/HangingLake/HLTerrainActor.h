// The bowl mesh lives on a real actor with a real root component.
//
// The first version spawned a bare AActor and attached a scene root that was
// never properly registered. The mesh built (the log proved it) but the actor had
// no valid bounds, so it was culled and the world rendered black.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HLTerrainActor.generated.h"

class UProceduralMeshComponent;

UCLASS()
class HANGINGLAKE_API AHLTerrainActor : public AActor
{
	GENERATED_BODY()

public:
	AHLTerrainActor();

	UProceduralMeshComponent* GetMesh() const { return Mesh; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Hanging Lake")
	UProceduralMeshComponent* Mesh = nullptr;
};
