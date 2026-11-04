#include "HLTerrainActor.h"

#include "ProceduralMeshComponent.h"

AHLTerrainActor::AHLTerrainActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
	SetRootComponent(Mesh);
	// Movable: a Static component silently refuses runtime CreateMeshSection.
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCastShadow(true);
	Mesh->bUseAsyncCooking = true;
}
