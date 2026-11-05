#include "HLBow.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "HLFish.h"
#include "HLSnellSolver.h"
#include "HLTerrainData.h"
#include "HLWaterSurface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

AHLBow::AHLBow()
{
	PrimaryActorTick.bCanEverTick = true;

	BowMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BowMesh"));
	SetRootComponent(BowMesh);
	BowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BowMesh->SetCastShadow(false);
	BowMesh->SetVisibility(false); // first-person: the HUD carries the bow

	ArrowMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArrowMesh"));
	ArrowMesh->SetupAttachment(BowMesh);
	ArrowMesh->SetUsingAbsoluteLocation(true);
	ArrowMesh->SetUsingAbsoluteRotation(true);
	ArrowMesh->SetUsingAbsoluteScale(true);
	ArrowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ArrowMesh->SetVisibility(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ShaftMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (ShaftMesh.Succeeded())
	{
		ArrowMesh->SetStaticMesh(ShaftMesh.Object);
	}
	// Cylinder is 100 cm tall along Z: a 75 cm shaft, 1.6 cm thick.
	ArrowMesh->SetWorldScale3D(FVector(0.016f, 0.016f, 0.75f));
}

void AHLBow::PlaceArrowMesh()
{
	if (!ArrowMesh || ArrowVel.IsNearlyZero())
	{
		return;
	}
	const FVector Dir = ArrowVel.GetSafeNormal();
	// Cylinder's long axis is Z; its pivot is the centre, so sit the tip at ArrowPos.
	ArrowMesh->SetWorldLocationAndRotation(ArrowPos - Dir * 37.5f, FRotationMatrix::MakeFromZ(Dir).ToQuat());
}

void AHLBow::BeginPlay()
{
	Super::BeginPlay();
	// Real physics by default; -hlbendarrow makes the arrow refract like light.
	// (-hlstraightarrow, the old way to ask for real physics, still works.)
	if (FParse::Param(FCommandLine::Get(), TEXT("hlbendarrow")))
	{
		bArrowBendsLikeLight = true;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("hlstraightarrow")))
	{
		bArrowBendsLikeLight = false;
	}
}

void AHLBow::Setup(ACharacter* InOwner, UHLTerrainData* InTerrain, AHLWaterSurface* InWater)
{
	OwnerChar = InOwner;
	Terrain = InTerrain;
	Water = InWater;
}

FVector AHLBow::GetEyeLocation() const
{
	if (OwnerChar && OwnerChar->GetController())
	{
		FVector EyeLoc;
		FRotator EyeRot;
		OwnerChar->GetController()->GetPlayerViewPoint(EyeLoc, EyeRot);
		return EyeLoc;
	}
	return GetActorLocation();
}

FVector AHLBow::GetAimDirection() const
{
	if (OwnerChar && OwnerChar->GetController())
	{
		FVector EyeLoc;
		FRotator EyeRot;
		OwnerChar->GetController()->GetPlayerViewPoint(EyeLoc, EyeRot);
		return EyeRot.Vector();
	}
	return FVector::ForwardVector;
}

bool AHLBow::GetAimWaterEntry(FVector& OutEntry, FVector& OutAimDir) const
{
	if (!Water)
	{
		return false;
	}
	OutAimDir = GetAimDirection();
	return Water->RayWaterEntry(GetEyeLocation(), OutAimDir, OutEntry);
}

void AHLBow::StartDraw()
{
	if (bArrowInFlight)
	{
		return;
	}
	bDrawing = true;
}

void AHLBow::ReleaseDraw()
{
	if (!bDrawing || bArrowInFlight || !Water || !Terrain)
	{
		bDrawing = false;
		DrawAlpha = 0.0f;
		return;
	}

	bDrawing = false;
	const float Power = FMath::Lerp(0.35f, 1.0f, DrawAlpha);
	DrawAlpha = 0.0f;

	ArrowPos = GetEyeLocation();
	UE_LOG(LogTemp, Display, TEXT("HL release eye=%s rot=%s power=%.2f"),
		*GetEyeLocation().ToCompactString(), *GetAimDirection().Rotation().ToCompactString(), Power);
	// A real bow loses energy to the water; speed here is the dry-land value and
	// the drag below does the rest.
	ArrowVel = GetAimDirection() * (4200.0f * Power);
	bArrowInFlight = true;
	bArrowUnderwater = false;
	ArrowTimer = 0.0f;
	ArrowLinger = 0.0f;
	// Start the visible arrow a little ahead of the eye so it is not inside the camera.
	ArrowPos += ArrowVel.GetSafeNormal() * 60.0f;
	// Shooting from in the water (wading deep): nothing to cross, nothing bends.
	bArrowUnderwater = ArrowPos.Z < Water->GetWaterZ();
	FlightAccumulator = 0.0f;
	PlaceArrowMesh();
	if (ArrowMesh) { ArrowMesh->SetVisibility(true); }
	// Record the flight as it happens (see StepArrow) for the teaching overlay.
	LastShotPath.Reset();
	LastShotPath.Add(ArrowPos);
	LastShotEntryIndex = INDEX_NONE;
	StepsSinceSample = 0;
}

void AHLBow::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bDrawing)
	{
		DrawAlpha = FMath::Min(1.0f, DrawAlpha + DeltaSeconds * 1.8f);
	}

	if (!bArrowInFlight)
	{
		if (ArrowLinger > 0.0f)
		{
			ArrowLinger -= DeltaSeconds;
			if (ArrowLinger <= 0.0f && ArrowMesh) { ArrowMesh->SetVisibility(false); }
		}
		return;
	}

	// Fly in exact 1/120 s steps whatever the frame rate, carrying the leftover
	// time to the next frame, so the arrow lands where it should on a slow machine
	// (at ~6 fps the single-step path drifted) and matches SimulateMiss exactly.
	constexpr float Step = 1.0f / 120.0f;
	FlightAccumulator += DeltaSeconds;
	int32 Steps = 0;
	while (FlightAccumulator >= Step && bArrowInFlight)
	{
		FlightAccumulator -= Step;
		if (StepArrow(Step) || ++Steps >= 240) { break; } // 2 s of flight per frame at most
	}
}

bool AHLBow::StepArrow(float Dt)
{
	ArrowTimer += Dt;
	const float WaterZ = Water ? Water->GetWaterZ() : -1e9f;

	// Gravity, then water drag. Below the surface the arrow loses energy fast, which
	// is why deep shots are harder and why the retrieval distance stays short.
	ArrowVel.Z -= 980.0f * Dt;
	if (bArrowUnderwater)
	{
		ArrowVel *= FMath::Pow(0.12f, Dt); // strong drag
	}

	FVector NextPos = ArrowPos + ArrowVel * Dt;

	if (!bArrowUnderwater && NextPos.Z < WaterZ)
	{
		// The surface is somewhere in this step. Fly to it on the old heading, enter
		// the water exactly there, and spend the rest of the step on the new heading.
		// (Bending at the next step instead, once already under, put the kink up to
		// 35 cm late: enough to miss a fish aimed at dead centre.)
		const float T = FMath::Clamp((ArrowPos.Z - WaterZ) / (ArrowPos.Z - NextPos.Z), 0.0f, 1.0f);
		if (SweepTo(ArrowPos + (NextPos - ArrowPos) * T))
		{
			return true;
		}
		EnterWater();
		NextPos = ArrowPos + ArrowVel * (Dt * (1.0f - T));
	}

	if (SweepTo(NextPos))
	{
		return true;
	}
	PlaceArrowMesh();
	if (++StepsSinceSample >= 4) // every 1/30 s is plenty for a smooth line
	{
		LastShotPath.Add(ArrowPos);
		StepsSinceSample = 0;
	}

	// Stop at the bed.
	if (Terrain && ArrowPos.Z < Terrain->ElevationAt(ArrowPos.X, ArrowPos.Y))
	{
		UE_LOG(LogTemp, Display, TEXT("HL arrow reached the bed at %s"), *ArrowPos.ToCompactString());
		EndShot(false, nullptr);
		return true;
	}

	if (ArrowTimer > 6.0f)
	{
		EndShot(false, nullptr);
	}
	return !bArrowInFlight;
}

bool AHLBow::SweepTo(const FVector& To)
{
	if (FVector::DistSquared(ArrowPos, To) < 1e-4)
	{
		return false; // nothing to sweep (a zero-length sweep is an overlap test)
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HLAArrow), false, OwnerChar);
	if (!GetWorld()->SweepSingleByChannel(
			Hit, ArrowPos, To, FQuat::Identity, ECC_WorldDynamic,
			FCollisionShape::MakeSphere(4.0f), Params))
	{
		ArrowPos = To;
		return false;
	}

	AHLFish* Fish = Cast<AHLFish>(Hit.GetActor());
	UE_LOG(LogTemp, Display, TEXT("HL arrow hit %s at %s (underwater=%d)"),
		Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("nothing"),
		*Hit.ImpactPoint.ToCompactString(), bArrowUnderwater ? 1 : 0);
	// Leave the arrow (and the overlay's line) where it struck, not a step short.
	ArrowPos = Hit.Location;
	EndShot(Fish != nullptr, Fish);
	return true;
}

void AHLBow::EnterWater()
{
	// Bend like light, or carry straight on like a real arrow. Either way the
	// water takes a tenth of the speed at once.
	bArrowUnderwater = true;
	if (bArrowBendsLikeLight)
	{
		bool bTIR = false;
		ArrowVel = FHLSnellSolver::Refract(
			ArrowVel.GetSafeNormal(), FVector::UpVector, 1.0f, FHLSnellSolver::WaterIOR, bTIR)
			* ArrowVel.Size() * 0.9f;
	}
	else
	{
		ArrowVel *= 0.9f;
	}

	// The kink the overlay points at.
	LastShotPath.Add(ArrowPos);
	LastShotEntryIndex = LastShotPath.Num() - 1;
	StepsSinceSample = 0;

	// A splash: disturb nearby fish.
	for (TActorIterator<AHLFish> It(GetWorld()); It; ++It)
	{
		if (!It->bLanded && FVector::Dist(It->GetActorLocation(), ArrowPos) < 600.0f)
		{
			It->Spook();
		}
	}
}

void AHLBow::EndShot(bool bHit, AHLFish* HitFish)
{
	LastShotPath.Add(ArrowPos);
	bArrowInFlight = false;
	bArrowUnderwater = false;
	PlaceArrowMesh();
	ArrowVel = FVector::ZeroVector;
	ArrowLinger = 2.0f; // leave it visible where it ended

	if (bHit && HitFish)
	{
		HitFish->OnLanded();
		OnFishHit.Broadcast(HitFish);
	}
	else
	{
		OnShotMissed.Broadcast();
	}
}
