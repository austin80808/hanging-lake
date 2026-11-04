#include "HLFish.h"

#include "Components/StaticMeshComponent.h"
#include "HLTerrainData.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AHLFish::AHLFish()
{
	PrimaryActorTick.bCanEverTick = true;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionObjectType(ECC_WorldDynamic);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetGenerateOverlapEvents(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Body->SetStaticMesh(SphereMesh.Object);
	}
}

void AHLFish::BeginPlay()
{
	Super::BeginPlay();
	PickNextTarget();
}

void AHLFish::Init(UHLTerrainData* InTerrain, const FVector& InHomePoint, float InBodyLengthCm)
{
	Terrain = InTerrain;
	HomePoint = InHomePoint;
	BodyLengthCm = InBodyLengthCm;

	// The stand-in body is a stretched sphere roughly the proportions of a trout.
	if (Body)
	{
		Body->SetWorldScale3D(FVector(BodyLengthCm / 100.0f, BodyLengthCm / 320.0f, BodyLengthCm / 320.0f));
		Body->SetWorldLocation(HomePoint);
	}

	PickNextTarget();
}

float AHLFish::GetDepthCm() const
{
	if (!Terrain)
	{
		return 0.0f;
	}
	return Terrain->GetWaterZ() - GetActorLocation().Z;
}

void AHLFish::Spook()
{
	// A fish needs a moment to react to a splash. Bolting the instant the arrow
	// touched the water made a well-aimed shot impossible to land.
	if (State != EHLFishState::Startle && SpookDelay <= 0.0f)
	{
		SpookDelay = 0.2f;
	}
}

void AHLFish::StartleNow()
{
	SpookDelay = 0.0f;
	State = EHLFishState::Startle;
	StateTimer = FMath::FRandRange(8.0f, 20.0f);

	// Bolt away from the current position toward the deepest water nearby.
	if (Terrain)
	{
		FVector Best = GetActorLocation();
		float BestDepth = GetDepthCm();
		for (int32 I = 0; I < 12; ++I)
		{
			const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
			const float Radius = FMath::FRandRange(300.0f, 1200.0f);
			const FVector Candidate = GetActorLocation() +
				FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
			const float Depth = Terrain->DepthAt(Candidate.X, Candidate.Y);
			if (Depth > BestDepth)
			{
				BestDepth = Depth;
				Best = Candidate;
			}
		}
		TargetPoint = Best;
	}
}

void AHLFish::OnLanded()
{
	bLanded = true;
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);
}

void AHLFish::PickNextTarget()
{
	if (!Terrain)
	{
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	switch (State)
	{
	case EHLFishState::Startle:
		StateTimer = FMath::FRandRange(8.0f, 20.0f);
		break;
	case EHLFishState::Rest:
		StateTimer = FMath::FRandRange(10.0f, 25.0f);
		break;
	default:
		State = (FMath::FRand() < 0.45f) ? EHLFishState::Hold : EHLFishState::Cruise;
		StateTimer = FMath::FRandRange(5.0f, 18.0f);
		break;
	}

	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const float Radius = (State == EHLFishState::Cruise) ? FMath::FRandRange(400.0f, 1600.0f)
														: FMath::FRandRange(50.0f, 350.0f);

	const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
	TargetPoint = HomePoint + Offset;

	// Keep it in water: if the target is on land, come back toward the deepest
	// nearby cell instead.
	if (Terrain->DepthAt(TargetPoint.X, TargetPoint.Y) <= 5.0f)
	{
		TargetPoint = GetActorLocation();
	}

	// Height: hold near the bed, cruise a little above it.
	const float BedZ = Terrain->GetWaterZ() - Terrain->DepthAt(TargetPoint.X, TargetPoint.Y);
	const float Clearance = (State == EHLFishState::Cruise)
		? FMath::FRandRange(20.0f, 60.0f)
		: FMath::FRandRange(8.0f, 25.0f);
	TargetPoint.Z = FMath::Max(BedZ + Clearance, Terrain->GetWaterZ() - 1200.0f);

	(void)Now;
}

void AHLFish::KeepUnderwater()
{
	if (!Terrain)
	{
		return;
	}
	const float WaterZ = Terrain->GetWaterZ();
	FVector Loc = GetActorLocation();
	if (Loc.Z > WaterZ - 8.0f)
	{
		Loc.Z = WaterZ - 8.0f;
		SetActorLocation(Loc);
	}
}

void AHLFish::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (SpookDelay > 0.0f)
	{
		SpookDelay -= DeltaSeconds;
		if (SpookDelay <= 0.0f)
		{
			StartleNow();
		}
	}

	if (bLanded || !Terrain)
	{
		return;
	}

	StateTimer -= DeltaSeconds;
	if (StateTimer <= 0.0f)
	{
		PickNextTarget();
	}

	const float Speed = (State == EHLFishState::Startle) ? 260.0f
					  : (State == EHLFishState::Cruise) ? 55.0f
					  : 16.0f;

	FVector Loc = GetActorLocation();
	const FVector ToTarget = TargetPoint - Loc;
	if (ToTarget.SizeSquared() > 25.0f)
	{
		Loc += ToTarget.GetSafeNormal() * FMath::Min(Speed * DeltaSeconds, ToTarget.Size());
		SetActorLocation(Loc);
	}

	// Idle bob so a holding fish is not perfectly static.
	SwimPhase += DeltaSeconds * (State == EHLFishState::Startle ? 6.0f : 1.6f);
	FVector Bob = GetActorLocation();
	Bob.Z += FMath::Sin(SwimPhase) * 0.6f;
	SetActorLocation(Bob);

	KeepUnderwater();
}
