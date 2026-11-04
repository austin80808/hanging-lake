#include "HLWaterSurface.h"

#include "Components/StaticMeshComponent.h"
#include "HLTerrainData.h"
#include "HLSnellSolver.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AHLWaterSurface::AHLWaterSurface()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Surface = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Surface"));
	Surface->SetupAttachment(Root);
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Surface->SetCastShadow(false);
}

void AHLWaterSurface::Build(UHLTerrainData* InTerrain)
{
	Terrain = InTerrain;
	if (!Terrain || !Terrain->IsLoaded() || !Surface)
	{
		return;
	}

	WaterZ = Terrain->GetWaterZ();
	const FVector2D Extent = Terrain->GetExtentCm();
	constexpr float CellSize = 400.0f; // 4 m cells: the surface is flat, so coarse is fine

	const int32 NX = FMath::Max(2, FMath::CeilToInt(Extent.X / CellSize));
	const int32 NY = FMath::Max(2, FMath::CeilToInt(Extent.Y / CellSize));

	TArray<FVector> Verts;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Tris;

	for (int32 I = 0; I <= NX; ++I)
	{
		for (int32 J = 0; J <= NY; ++J)
		{
			const float X = static_cast<float>(I) / NX * Extent.X;
			const float Y = static_cast<float>(J) / NY * Extent.Y;
			// Only emit surface where there is actually water underneath, so the
			// plane follows the shoreline instead of roofing the whole bowl.
			const bool bWater = Terrain->DepthAt(X, Y) > 0.0f;
			Verts.Add(FVector(X, Y, WaterZ));
			Normals.Add(FVector::UpVector);
			UVs.Add(FVector2D(static_cast<float>(I) / NX, static_cast<float>(J) / NY));
			// Alpha is the water mask: the material multiplies opacity by it, so
			// land (alpha 0) is invisible and only cells over the basin draw.
			Colors.Add(bWater ? FColor(255, 255, 255, 255) : FColor(255, 255, 255, 0));
		}
	}

	for (int32 I = 0; I < NX; ++I)
	{
		for (int32 J = 0; J < NY; ++J)
		{
			const int32 A = I * (NY + 1) + J;
			const int32 B = A + 1;
			const int32 C = A + (NY + 1);
			const int32 D = C + 1;
			Tris.Append({A, C, B, B, C, D});
		}
	}

	Surface->CreateMeshSection(0, Verts, Tris, Normals, UVs, Colors, Tangents, false);

	if (UMaterialInterface* WaterMat = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/Materials/M_BowlWater.M_BowlWater")))
	{
		Surface->SetMaterial(0, WaterMat);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("HL: M_BowlWater not found; lake will be invisible"));
	}

	UE_LOG(LogTemp, Display, TEXT("HL: water surface at Z %.0f, %dx%d cells"), WaterZ, NX, NY);
}

bool AHLWaterSurface::RayWaterEntry(
	const FVector& Origin, const FVector& Dir, FVector& OutEntry) const
{
	return FHLSnellSolver::RayPlaneEntry(Origin, Dir, WaterZ, OutEntry);
}
