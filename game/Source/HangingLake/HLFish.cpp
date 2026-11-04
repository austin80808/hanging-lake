#include "HLFish.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "HLSnellSolver.h"
#include "HLTerrainData.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
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

	Image = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Image"));
	Image->SetupAttachment(Body);
	Image->SetUsingAbsoluteLocation(true);
	Image->SetUsingAbsoluteRotation(true);
	Image->SetUsingAbsoluteScale(true);
	Image->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Image->SetCastShadow(false); // the shadow belongs to the real fish, on the bed below it

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Body->SetStaticMesh(SphereMesh.Object);
		Image->SetStaticMesh(SphereMesh.Object);
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
	Heading = FMath::FRandRange(-180.0f, 180.0f);

	// -hlnoimage draws the fish where it really is, to compare with the refracted view.
	bDrawImage = !FParse::Param(FCommandLine::Get(), TEXT("hlnoimage"));

	// The stand-in body is a stretched sphere roughly the proportions of a trout.
	const FVector Scale(BodyLengthCm / 100.0f, BodyLengthCm / 320.0f, BodyLengthCm / 320.0f);
	if (Body)
	{
		Body->SetWorldScale3D(Scale);
		Body->SetWorldLocation(HomePoint);
		// Hidden, but still blocking arrows and casting the shadow on the bed.
		Body->SetHiddenInGame(bDrawImage);
		Body->SetCastHiddenShadow(bDrawImage);
	}
	if (Image)
	{
		Image->SetWorldScale3D(Scale);
		Image->SetHiddenInGame(!bDrawImage);
	}
	SetActorRotation(FRotator(0.0f, Heading, 0.0f));

	PickNextTarget();
	UpdateImage();
}

void AHLFish::SetColour(const FLinearColor& Colour)
{
	for (UStaticMeshComponent* C : {Body, Image})
	{
		if (C)
		{
			if (UMaterialInstanceDynamic* Mat = C->CreateAndSetMaterialInstanceDynamic(0))
			{
				Mat->SetVectorParameterValue(TEXT("Color"), Colour);
			}
		}
	}
}

float AHLFish::GetDepthCm() const
{
	if (!Terrain)
	{
		return 0.0f;
	}
	return Terrain->GetWaterZ() - GetActorLocation().Z;
}

FVector AHLFish::GetImageLocation() const
{
	return Image ? Image->GetComponentLocation() : GetActorLocation();
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
	// It remembers: each scare keeps it away longer and makes it jumpier.
	StateTimer = FMath::FRandRange(8.0f, 20.0f) * (1.0f + 0.35f * FMath::Min(Wariness, 4.0f));
	Wariness += 1.0f;

	// Bolt away from the current position toward the deepest water nearby.
	if (Terrain)
	{
		// Compare water depths (to the bed), not the fish's own depth, or a fish with
		// no deeper water nearby "bolts" upward.
		FVector Best = GetActorLocation();
		float BestDepth = Terrain->DepthAt(Best.X, Best.Y);
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
		// Hug the bottom of that deep water.
		Best.Z = Terrain->GetWaterZ() - FMath::Max(BestDepth - 20.0f, 10.0f);
		TargetPoint = Best;
	}
}

void AHLFish::OnLanded()
{
	bLanded = true;
	SpookDelay = 0.0f;
	if (Body)
	{
		Body->SetCastHiddenShadow(false); // or the hidden fish would still cast one
	}
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);
}

bool AHLFish::PickRestTarget()
{
	if (!Terrain || CoverPoints.Num() == 0)
	{
		return false;
	}

	// The nearest cover within reach of home: the log's shade or the falls' pool.
	const FVector* Best = nullptr;
	float BestDist = 1800.0f;
	for (const FVector& C : CoverPoints)
	{
		const float D = FVector::Dist2D(C, HomePoint);
		if (D < BestDist)
		{
			BestDist = D;
			Best = &C;
		}
	}
	if (!Best)
	{
		return false;
	}

	FVector Spot = *Best + FVector(FMath::FRandRange(-80.0f, 80.0f), FMath::FRandRange(-80.0f, 80.0f), 0.0f);
	const float Depth = Terrain->DepthAt(Spot.X, Spot.Y);
	if (Depth < 40.0f)
	{
		return false;
	}

	State = EHLFishState::Rest;
	StateTimer = FMath::FRandRange(10.0f, 25.0f);
	// Tucked up in the shade, off the bed.
	Spot.Z = Terrain->GetWaterZ() - FMath::Clamp(Depth * 0.5f, 25.0f, 90.0f);
	TargetPoint = Spot;
	return true;
}

void AHLFish::PickNextTarget()
{
	if (!Terrain)
	{
		return;
	}

	// A bolt or a rest runs for its timer once; after that the fish goes back to
	// holding and cruising. (Before, a startled fish stayed startled for good,
	// darting about at full speed and never spooking again.)
	if (FMath::FRand() < 0.18f && PickRestTarget())
	{
		return;
	}

	State = (FMath::FRand() < 0.45f) ? EHLFishState::Hold : EHLFishState::Cruise;
	StateTimer = FMath::FRandRange(5.0f, 18.0f);

	const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
	const float Radius = (State == EHLFishState::Cruise) ? FMath::FRandRange(400.0f, 1600.0f)
														: FMath::FRandRange(50.0f, 350.0f);

	const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
	TargetPoint = HomePoint + Offset;

	// Keep it in water: if the target is on land, stay put this round.
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

void AHLFish::WatchForPlayer()
{
	if (State == EHLFishState::Startle || SpookDelay > 0.0f)
	{
		return;
	}
	const APawn* P = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!P)
	{
		return;
	}

	// Trout read movement on the bank. Close enough to touch, a fish always bolts;
	// within a few metres it bolts from a hurried walker but tolerates a slow,
	// careful one. That slow approach is what the journal calls stalking.
	const float Flat = FVector::Dist2D(P->GetActorLocation(), GetActorLocation());
	const float Speed = P->GetVelocity().Size2D();
	const float Alert = 260.0f + 90.0f * FMath::Min(Wariness, 4.0f);
	if (Flat < 120.0f || (Flat < Alert && Speed > 110.0f))
	{
		Spook();
	}
}

void AHLFish::UpdateImage()
{
	if (!Image)
	{
		return;
	}

	FVector Where = GetActorLocation();
	if (bDrawImage && Terrain)
	{
		if (const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
		{
			FVector Apparent;
			if (FHLSnellSolver::ApparentPosition(Cam->GetCameraLocation(), Where, Terrain->GetWaterZ(), Apparent))
			{
				// Seen at a grazing angle the image rises almost to the surface; keep
				// the whole body under it so the water still tints it.
				const float Clear = BodyLengthCm * 0.16f + 2.0f;
				Apparent.Z = FMath::Min(Apparent.Z, Terrain->GetWaterZ() - Clear);
				Where = Apparent;
			}
		}
	}
	// A slight idle bob on what the player sees, so a holding fish is not frozen.
	Where.Z += FMath::Sin(SwimPhase) * 1.2f;
	Image->SetWorldLocationAndRotation(Where, GetActorRotation());
}

void AHLFish::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bLanded || !Terrain)
	{
		return;
	}

	if (SpookDelay > 0.0f)
	{
		SpookDelay -= DeltaSeconds;
		if (SpookDelay <= 0.0f)
		{
			StartleNow();
		}
	}

	// Calms down over a few minutes.
	Wariness = FMath::Max(0.0f, Wariness - DeltaSeconds / 90.0f);
	WatchForPlayer();

	StateTimer -= DeltaSeconds;
	if (StateTimer <= 0.0f)
	{
		PickNextTarget();
	}

	const float Speed = (State == EHLFishState::Startle) ? 260.0f
					  : (State == EHLFishState::Cruise) ? 55.0f
					  : (State == EHLFishState::Rest) ? 45.0f
					  : 16.0f;

	FVector Loc = GetActorLocation();
	const FVector ToTarget = TargetPoint - Loc;
	if (ToTarget.SizeSquared() > 25.0f)
	{
		Loc += ToTarget.GetSafeNormal() * FMath::Min(Speed * DeltaSeconds, ToTarget.Size());
		SetActorLocation(Loc);

		// Face the way it swims, turning at a fish's pace rather than snapping.
		if (ToTarget.SizeSquared2D() > 100.0f)
		{
			const float Want = ToTarget.Rotation().Yaw;
			const float TurnRate = (State == EHLFishState::Startle) ? 720.0f : 120.0f;
			Heading = FMath::FixedTurn(Heading, Want, TurnRate * DeltaSeconds);
			SetActorRotation(FRotator(0.0f, Heading, 0.0f));
		}
	}

	SwimPhase += DeltaSeconds * (State == EHLFishState::Startle ? 6.0f : 1.6f);

	KeepUnderwater();
	UpdateImage();
}
