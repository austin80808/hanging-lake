#include "HLGameMode.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "Engine/GameViewportClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerStart.h"
#include "HLBow.h"
#include "HLFish.h"
#include "HLHUD.h"
#include "HLSnellSolver.h"
#include "HLPlayerCharacter.h"
#include "HLPlayerController.h"
#include "HLTerrainActor.h"
#include "HLTerrainData.h"
#include "HLWaterSurface.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/ExponentialHeightFog.h"
#include "Misc/Paths.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace HLLandmarks
{
	// From OpenStreetMap (data/cache/osm_*.json in hanging-lake-ue), converted with
	// the exporter's mapping: X = (north - lat) / lat span * extent X (X runs
	// south), Y = (lon - west) / lon span * extent Y (Y runs east). Centimetres.
	// Way 159737338, "Hanging Lake Trail", bridge=yes: the boardwalk on the
	// south-east shore where the trail arrives.
	static const FVector2D Boardwalk[] = {
		{59260.f, 57395.f}, {59477.f, 58439.f}, {59213.f, 58996.f}, {59003.f, 59245.f},
		{58544.f, 59512.f}, {57854.f, 59925.f}, {57119.f, 60230.f},
		// Way 159737337 continues the walk north along the east shore.
		{56127.f, 59928.f}, {55480.f, 59890.f}};
	// Where East Fork Dead Horse Creek pours in over the north rim: the falls.
	static const FVector2D Falls(55600.f, 58200.f);
	// The fallen log, measured off the USGS NAIP aerial photo (it shows as a pale
	// diagonal in the lake): the shore end rests on the north-east rim at
	// 39.601460 N, -107.191661 W and the trunk runs 14.2 m south-west to
	// 39.601373 N, -107.191781 W.
	static const FVector2D LogShoreEnd(56100.f, 58659.f);
	static const FVector2D LogLakeEnd(57069.f, 57627.f);
	static const FVector2D LogCentre(56584.f, 58142.f);
	// Standing spot: the east-side boardwalk, the classic view west across the log.
	static const FVector2D Spawn(56127.f, 59928.f);
}

AHLGameMode::AHLGameMode()
{
	DefaultPawnClass = AHLPlayerCharacter::StaticClass();
	PlayerControllerClass = AHLPlayerController::StaticClass();
	HUDClass = AHLHUD::StaticClass();

	if (DataDir.IsEmpty())
	{
		// Repo layout: game/ (this project) beside pipeline/ (the terrain data).
		// -hldata=<dir> overrides.
		DataDir = FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("pipeline"), TEXT("data"));
		FParse::Value(FCommandLine::Get(), TEXT("hldata="), DataDir);
	}
}

void AHLGameMode::StartPlay()
{
	Super::StartPlay();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FixLighting(World);
	// The "lighting needs to be rebuilt" banner is meaningless here (everything is
	// dynamic and built at runtime) and covers the HUD.
	if (GEngine) { GEngine->Exec(World, TEXT("DisableAllScreenMessages")); }

	// --- terrain ----------------------------------------------------------
	Terrain = NewObject<UHLTerrainData>(this);
	if (!Terrain->LoadFromDirectory(FPaths::ConvertRelativePathToFull(DataDir)))
	{
		UE_LOG(LogTemp, Error, TEXT("HL: terrain failed to load from %s"), *DataDir);
		return;
	}

	AHLTerrainActor* TerrainActor = World->SpawnActor<AHLTerrainActor>(
		AHLTerrainActor::StaticClass(), FTransform::Identity);
	// Debug switches for bisecting render problems: -hlnoterrain -hlnowater -hlnofish
	if (TerrainActor && !FParse::Param(FCommandLine::Get(), TEXT("hlnoterrain")))
	{
		Terrain->BuildMesh(TerrainActor);
	}

	// --- water ------------------------------------------------------------
	Water = World->SpawnActor<AHLWaterSurface>(AHLWaterSurface::StaticClass(), FTransform::Identity);
	if (Water && !FParse::Param(FCommandLine::Get(), TEXT("hlnowater")))
	{
		Water->Build(Terrain);
	}

	// --- the boardwalk and the log -------------------------------------------
	SpawnBoardwalk(World);
	const FVector LogCentre = SpawnLog(World);

	// --- player -----------------------------------------------------------
	const FVector Spawn = GetSpawnPoint();
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (ACharacter* Char = Cast<ACharacter>(PC->GetPawn()))
		{
			Char->SetActorLocation(Spawn);

			// Open on Hanging Lake's fallen log with the falls behind it: aim a
			// little past the log toward the falls, slightly down.
			const FVector Target = LogCentre + FVector(0.0f, 0.0f, -150.0f);
			const FVector Eye = Char->GetActorLocation();
			const FRotator Look = (Target - Eye).Rotation();
			// SetControlRotation alone is overwritten when the pawn finishes
			// possessing, so force the client rotation as well and turn the body
			// to match (the character uses controller yaw/pitch for its camera).
			Char->SetActorRotation(FRotator(0.0f, Look.Yaw, 0.0f));
			PC->SetControlRotation(Look);
			PC->ClientSetRotation(Look, true);

			// And once more next frame, which is when possession has settled.
			FTimerHandle AimTimer;
			TWeakObjectPtr<AHLGameMode> WeakThis(this);
			World->GetTimerManager().SetTimer(AimTimer, [WeakThis, Look]()
			{
				if (!WeakThis.IsValid()) { return; }
				if (APlayerController* C = WeakThis->GetWorld()->GetFirstPlayerController())
				{
					C->SetControlRotation(Look);
					C->ClientSetRotation(Look, true);
					if (APawn* P = C->GetPawn())
					{
						P->SetActorRotation(FRotator(0.0f, Look.Yaw, 0.0f));
					}
				}
			}, 0.35f, false);
			UE_LOG(LogTemp, Warning, TEXT("HLDBG aim eye=(%.0f,%.0f,%.0f) target=(%.0f,%.0f,%.0f) pitch=%.1f"),
				Eye.X, Eye.Y, Eye.Z, Target.X, Target.Y, Target.Z, Look.Pitch);
		}
	}

	// --- bow --------------------------------------------------------------
	Bow = World->SpawnActor<AHLBow>(AHLBow::StaticClass(), FTransform::Identity);
	if (Bow)
	{
		Bow->Setup(Cast<ACharacter>(World->GetFirstPlayerController()
										? World->GetFirstPlayerController()->GetPawn()
										: nullptr),
			Terrain, Water);
	}

	if (!FParse::Param(FCommandLine::Get(), TEXT("hlnofish")))
	{
		SpawnFish();
	}

	// --- diagnostics ------------------------------------------------------
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		FVector EyeLoc; FRotator EyeRot;
		PC->GetPlayerViewPoint(EyeLoc, EyeRot);
		UE_LOG(LogTemp, Warning, TEXT("HLDBG camera=(%.0f,%.0f,%.0f) rot=(%.1f,%.1f) terrainHere=%.0f waterZ=%.0f"),
			EyeLoc.X, EyeLoc.Y, EyeLoc.Z, EyeRot.Pitch, EyeRot.Yaw,
			Terrain->ElevationAt(EyeLoc.X, EyeLoc.Y), Terrain->GetWaterZ());
	}

	UE_LOG(LogTemp, Display, TEXT("HL: bowl ready with %d fish"), Fish.Num());

	// Docs: record frames for a GIF (-hlrec=<start s> -hlrecdur=<s> -hlrecfps=<n>),
	// written as Saved/Screenshots/<Platform>/rec_0000.png and on.
	float RecStart = -1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("hlrec="), RecStart))
	{
		float RecDur = 5.0f, RecFps = 12.0f;
		FParse::Value(FCommandLine::Get(), TEXT("hlrecdur="), RecDur);
		FParse::Value(FCommandLine::Get(), TEXT("hlrecfps="), RecFps);
		const int32 Frames = FMath::CeilToInt(RecDur * RecFps);
		for (int32 I = 0; I < Frames; ++I)
		{
			FTimerHandle H;
			World->GetTimerManager().SetTimer(H, [I]()
			{
				FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("rec_%04d"), I), true, false);
			}, RecStart + I / RecFps, false);
		}
	}

	// Docs: slow pan for the opening GIF (-hlpan=<deg/s>, with -hlpitch).
	float PanRate = 0.0f;
	FParse::Value(FCommandLine::Get(), TEXT("hlpan="), PanRate);

	// Test: shoot at the nearest fish a few times and log the result (-hlautoshoot).
	if (FParse::Param(FCommandLine::Get(), TEXT("hlautoshoot")) && Bow)
	{
		Bow->OnFishHit.AddLambda([](AHLFish* F)
		{
			UE_LOG(LogTemp, Warning, TEXT("HLSHOOT HIT %s"), F ? *F->GetName() : TEXT("?"));
		});
		Bow->OnShotMissed.AddLambda([]()
		{
			UE_LOG(LogTemp, Warning, TEXT("HLSHOOT MISS"));
		});
		float First = 25.0f;
		FParse::Value(FCommandLine::Get(), TEXT("hlshootat="), First);
		for (int32 I = 0; I < 4; ++I)
		{
			FTimerHandle H;
			World->GetTimerManager().SetTimer(H, this, &AHLGameMode::AutoShoot, First + I * 4.0f, false);
		}
	}

	// Debug: lock the view so captures are repeatable (-hlpitch=<deg> [-hlyaw=<deg>]).
	float LockPitch = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("hlpitch="), LockPitch))
	{
		float LockYaw = -10.4f;
		FParse::Value(FCommandLine::Get(), TEXT("hlyaw="), LockYaw);
		const float PanStart = World->GetTimeSeconds();
		FTimerHandle LockTimer;
		TWeakObjectPtr<UWorld> WeakWorld(World);
		World->GetTimerManager().SetTimer(LockTimer, [WeakWorld, LockPitch, LockYaw, PanRate, PanStart]()
		{
			if (!WeakWorld.IsValid()) { return; }
			if (APlayerController* C = WeakWorld->GetFirstPlayerController())
			{
				C->SetIgnoreLookInput(true);
				C->SetIgnoreMoveInput(true);
				const float Yaw = LockYaw + PanRate * (WeakWorld->GetTimeSeconds() - PanStart);
				C->SetControlRotation(FRotator(LockPitch, Yaw, 0.0f));
			}
		}, 0.1f, true);
	}

	// Debug: where is the camera over time? (-hltrace)
	if (FParse::Param(FCommandLine::Get(), TEXT("hltrace")))
	{
		FTimerHandle TraceTimer;
		TWeakObjectPtr<UWorld> WeakWorld(World);
		World->GetTimerManager().SetTimer(TraceTimer, [WeakWorld]()
		{
			if (!WeakWorld.IsValid()) { return; }
			if (APlayerController* C = WeakWorld->GetFirstPlayerController())
			{
				FVector L; FRotator R; C->GetPlayerViewPoint(L, R);
				const APawn* P = C->GetPawn();
				UE_LOG(LogTemp, Warning, TEXT("HLTRACE t=%.0f cam=(%.0f,%.0f,%.0f) pitch=%.1f vel=%s"),
					WeakWorld->GetTimeSeconds(), L.X, L.Y, L.Z, R.Pitch,
					P ? *P->GetVelocity().ToCompactString() : TEXT("-"));
			}
		}, 5.0f, true);
	}

	// Capture late enough for on-demand shader compilation to finish. Capturing at
	// launch produced black frames while the HUD (Canvas, no shaders) drew fine.
	if (FParse::Param(FCommandLine::Get(), TEXT("hlshot")))
	{
		FTimerHandle ShotTimer;
		World->GetTimerManager().SetTimer(ShotTimer, []()
		{
			if (GEngine && GEngine->GameViewport)
			{
				FScreenshotRequest::RequestScreenshot(TEXT("hlbowl"), true, false);
			}
		}, 45.0f, false);
	}
}



FVector AHLGameMode::GetSpawnPoint() const
{
	// The OSM trail point sits up the slope. Walk from it toward the log and stand
	// at the first dry spot within 1.2 m of the water: the lake's edge, where the
	// boardwalk is.
	const float WaterZ = Terrain->GetWaterZ();
	const FVector2D From = HLLandmarks::Spawn;
	const FVector2D To = HLLandmarks::LogCentre;
	FVector2D Best = From;
	for (float U = 0.0f; U <= 1.0f; U += 0.01f)
	{
		const FVector2D P = FMath::Lerp(From, To, U);
		if (Terrain->DepthAt(P.X, P.Y) > 0.0f) { break; }        // reached the water
		Best = P;
		if (Terrain->ElevationAt(P.X, P.Y) - WaterZ < 120.0f) { break; }
	}
	const float Ground = FMath::Max(Terrain->ElevationAt(Best.X, Best.Y), BoardwalkDeckZ);
	UE_LOG(LogTemp, Display, TEXT("HL spawn (%.0f, %.0f) ground %.0f (%.0f above water)"),
		Best.X, Best.Y, Ground, Ground - WaterZ);
	return FVector(Best, Ground + 110.0f);
}

void AHLGameMode::SpawnBoardwalk(UWorld* World)
{
	const float WaterZ = Terrain->GetWaterZ();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	// One deck height for the whole walk: 50 cm over the water, or just over the
	// highest ground it crosses.
	float DeckZ = WaterZ + 50.0f;
	for (const FVector2D& P : HLLandmarks::Boardwalk)
	{
		DeckZ = FMath::Max(DeckZ, Terrain->ElevationAt(P.X, P.Y) + 20.0f);
	}
	DeckZ = FMath::Min(DeckZ, WaterZ + 150.0f);
	BoardwalkDeckZ = DeckZ;

	AActor* Walk = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(HLLandmarks::Spawn, DeckZ)));
	USceneComponent* Root = NewObject<USceneComponent>(Walk, TEXT("Root"));
	Walk->SetRootComponent(Root);
	Root->RegisterComponent();
	UMaterialInstanceDynamic* Wood = Base ? UMaterialInstanceDynamic::Create(Base, Walk) : nullptr;
	if (Wood) { Wood->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.32f, 0.22f, 0.13f)); }

	for (int32 I = 0; I + 1 < UE_ARRAY_COUNT(HLLandmarks::Boardwalk); ++I)
	{
		const FVector A(HLLandmarks::Boardwalk[I], DeckZ);
		const FVector B(HLLandmarks::Boardwalk[I + 1], DeckZ);
		UStaticMeshComponent* Plank = NewObject<UStaticMeshComponent>(Walk);
		Plank->SetupAttachment(Root);
		Plank->RegisterComponent();
		Plank->SetStaticMesh(Cube);
		// Cube is 100 cm; overlap segments by 2 m so the bends have no gaps.
		const float Len = FVector::Dist(A, B) + 200.0f;
		Plank->SetWorldLocationAndRotation((A + B) * 0.5f - FVector(0, 0, 8.0f), (B - A).Rotation());
		Plank->SetWorldScale3D(FVector(Len / 100.0f, 1.8f, 0.16f));
		Plank->SetCollisionProfileName(TEXT("BlockAll"));
		if (Wood) { Plank->SetMaterial(0, Wood); }
	}
	UE_LOG(LogTemp, Display, TEXT("HL boardwalk deck at Z %.0f (water %.0f)"), DeckZ, WaterZ);
}

namespace
{
	// Appends a closed tube along a polyline, radius per point, to a mesh section.
	void AddTube(const TArray<FVector>& Spine, const TArray<float>& Radii, int32 Sides,
		TArray<FVector>& V, TArray<int32>& T, TArray<FVector>& N)
	{
		const int32 Base = V.Num();
		for (int32 I = 0; I < Spine.Num(); ++I)
		{
			const FVector Fwd = (Spine[FMath::Min(I + 1, Spine.Num() - 1)] - Spine[FMath::Max(I - 1, 0)]).GetSafeNormal();
			const FVector Side = FVector::CrossProduct(Fwd, FVector::UpVector).GetSafeNormal();
			const FVector Up = FVector::CrossProduct(Side, Fwd);
			for (int32 K = 0; K < Sides; ++K)
			{
				const float A = 2.0f * PI * K / Sides;
				// Weathered, not round: a little lumpiness around and along.
				const float Lump = 1.0f + 0.07f * FMath::Sin(A * 3.0f + I * 0.9f) + 0.04f * FMath::Sin(A * 5.0f - I * 1.7f);
				const FVector Dir = Side * FMath::Cos(A) + Up * FMath::Sin(A);
				V.Add(Spine[I] + Dir * Radii[I] * Lump);
				N.Add(Dir);
			}
		}
		for (int32 I = 0; I + 1 < Spine.Num(); ++I)
		{
			for (int32 K = 0; K < Sides; ++K)
			{
				const int32 A = Base + I * Sides + K, B = Base + I * Sides + (K + 1) % Sides;
				const int32 C = A + Sides, D = B + Sides;
				T.Append({A, C, B, B, C, D});
			}
		}
		// End caps (fans).
		for (int32 End = 0; End < 2; ++End)
		{
			const int32 Ring = End == 0 ? 0 : Spine.Num() - 1;
			const int32 Centre = V.Num();
			V.Add(Spine[Ring]);
			N.Add((End == 0 ? -1.0f : 1.0f) * (Spine.Last() - Spine[0]).GetSafeNormal());
			for (int32 K = 0; K < Sides; ++K)
			{
				const int32 A = Base + Ring * Sides + K, B = Base + Ring * Sides + (K + 1) % Sides;
				if (End == 0) { T.Append({Centre, A, B}); } else { T.Append({Centre, B, A}); }
			}
		}
	}
}

FVector AHLGameMode::SpawnLog(UWorld* World)
{
	// Hanging Lake's famous fallen log, at its surveyed spot (see HLLandmarks): a
	// bleached, barkless trunk with its thick end up on the north-east rim and
	// the thin end dipping under the water to the south-west.
	const float WaterZ = Terrain->GetWaterZ();
	const FVector Shore(HLLandmarks::LogShoreEnd, WaterZ + 20.0f);
	const FVector Tip(HLLandmarks::LogLakeEnd, WaterZ - 18.0f); // top stays near the surface
	const FVector Along = (Tip - Shore).GetSafeNormal();
	const FVector Across = FVector::CrossProduct(Along, FVector::UpVector).GetSafeNormal();

	AActor* Log = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Shore));
	if (!Log)
	{
		return FVector(HLLandmarks::LogCentre, WaterZ);
	}
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Log, TEXT("Trunk"));
	Log->SetRootComponent(Mesh);
	Mesh->RegisterComponent();
	Mesh->SetWorldLocation(FVector::ZeroVector); // vertices are in world space

	TArray<FVector> V, N; TArray<int32> T;
	// Trunk: 28 cm radius at the shore end tapering to 15 cm, with a gentle bow
	// and a slight sag in the middle.
	TArray<FVector> Spine; TArray<float> Radii;
	const int32 Rings = 48;
	for (int32 I = 0; I <= Rings; ++I)
	{
		const float U = static_cast<float>(I) / Rings;
		FVector P = FMath::Lerp(Shore, Tip, U);
		P += Across * 22.0f * FMath::Sin(PI * U);   // bow
		P.Z -= 6.0f * FMath::Sin(PI * U);           // sag
		Spine.Add(P);
		Radii.Add(FMath::Lerp(28.0f, 15.0f, FMath::Pow(U, 0.8f)));
	}
	AddTube(Spine, Radii, 16, V, T, N);
	// Broken branch stubs, short and angled the way a fallen conifer keeps them.
	const struct { float U; float Angle; float Len; float R; } Stubs[] = {
		{0.30f, 15.0f, 30.0f, 5.0f}, {0.55f, 170.0f, 38.0f, 4.5f}, {0.78f, -20.0f, 25.0f, 3.5f}};
	for (const auto& S : Stubs)
	{
		const int32 I = FMath::RoundToInt(S.U * Rings);
		const float Rad = FMath::DegreesToRadians(S.Angle);
		const FVector Out = (Across * FMath::Cos(Rad) + FVector::UpVector * FMath::Sin(Rad) + Along * 0.35f).GetSafeNormal();
		TArray<FVector> StubSpine = {Spine[I], Spine[I] + Out * (Radii[I] + S.Len)};
		TArray<float> StubR = {S.R, S.R * 0.6f};
		AddTube(StubSpine, StubR, 8, V, T, N);
	}
	TArray<FVector2D> UV; UV.SetNumZeroed(V.Num());
	TArray<FColor> Col; Col.Init(FColor::White, V.Num());
	TArray<FProcMeshTangent> Tan;
	Mesh->CreateMeshSection(0, V, T, N, UV, Col, Tan, true);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionObjectType(ECC_WorldStatic);
	Mesh->SetCollisionResponseToAllChannels(ECR_Block);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		UMaterialInstanceDynamic* Wood = UMaterialInstanceDynamic::Create(Base, Log);
		Wood->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.66f, 0.62f, 0.54f)); // sun-bleached grey-tan
		Mesh->SetMaterial(0, Wood);
	}
	UE_LOG(LogTemp, Display, TEXT("HL log: %.0f cm from %s to %s"), FVector::Dist(Shore, Tip),
		*Shore.ToCompactString(), *Tip.ToCompactString());
	return FVector(HLLandmarks::LogCentre, WaterZ);
}

namespace
{
	// Mirrors AHLBow::Tick's flight model at full draw. Returns the closest the
	// path comes to Target before it hits the ground, and the time it gets there.
	float SimulateMiss(const UHLTerrainData* Terrain, const FVector& Eye, const FVector& Dir,
		float WaterZ, const FVector& Target, float& OutTime)
	{
		FVector Pos = Eye + Dir * 60.0f; // the bow starts the arrow 60 cm out
		FVector Vel = Dir * 4200.0f;
		bool bUnder = false;
		float Best = TNumericLimits<float>::Max();
		OutTime = 0.0f;
		const float Dt = 1.0f / 120.0f;
		for (float T = 0.0f; T < 4.0f; T += Dt)
		{
			Vel.Z -= 980.0f * Dt;
			if (Pos.Z < WaterZ && !bUnder)
			{
				bUnder = true;
				bool bTIR = false;
				Vel = FHLSnellSolver::Refract(Vel.GetSafeNormal(), FVector::UpVector, 1.0f,
					FHLSnellSolver::WaterIOR, bTIR) * Vel.Size() * 0.9f;
			}
			if (bUnder)
			{
				Vel *= FMath::Pow(0.12f, Dt);
			}
			const FVector Next = Pos + Vel * Dt;
			const float D = FMath::PointDistToSegment(Target, Pos, Next);
			if (D < Best) { Best = D; OutTime = T; }
			Pos = Next;
			if (Pos.Z < Terrain->ElevationAt(Pos.X, Pos.Y)) { break; } // blocked by rock or bed
		}
		return Best;
	}
}

void AHLGameMode::AutoShoot()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || !Bow || !Water || Bow->IsArrowInFlight())
	{
		return;
	}
	// Draw now; aim and release when the draw is full, leading the fish by how
	// far it moves while the arrow flies.
	TMap<TWeakObjectPtr<AHLFish>, FVector> Before;
	for (AHLFish* F : Fish)
	{
		if (F && !F->bLanded) { Before.Add(F, F->GetActorLocation()); }
	}
	PC->SetIgnoreLookInput(true); // the test owns the aim, not the desk mouse
	Bow->StartDraw();
	FTimerHandle ReleaseTimer;
	TWeakObjectPtr<AHLGameMode> WeakThis(this);
	const float DrawTime = 0.8f;
	World->GetTimerManager().SetTimer(ReleaseTimer, [WeakThis, Before, DrawTime]()
	{
		AHLGameMode* GM = WeakThis.Get();
		APlayerController* C = GM ? GM->GetWorld()->GetFirstPlayerController() : nullptr;
		if (!C || !GM->Bow) { return; }
		FVector Eye; FRotator Unused;
		C->GetPlayerViewPoint(Eye, Unused);

		float BestMiss = TNumericLimits<float>::Max(), BestPitch = 0.0f, BestYaw = 0.0f, BestScore = TNumericLimits<float>::Max();
		AHLFish* BestFish = nullptr;
		for (const auto& Pair : Before)
		{
			AHLFish* F = Pair.Key.Get();
			if (!F || F->bLanded) { continue; }
			const FVector Vel = (F->GetActorLocation() - Pair.Value) / DrawTime;
			FVector Aim = F->GetActorLocation();
			float Pitch = 0.0f, Yaw = 0.0f, Miss = TNumericLimits<float>::Max();
			// Two passes: find the flight time, then aim where the fish will be.
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				Yaw = (Aim - Eye).Rotation().Yaw;
				float PassBest = TNumericLimits<float>::Max(), PassPitch = 0.0f, PassTime = 0.0f;
				for (float P = -89.0f; P <= 10.0f; P += 0.05f)
				{
					float Time = 0.0f;
					const float M = SimulateMiss(GM->Terrain, Eye, FRotator(P, Yaw, 0.0f).Vector(),
						GM->Water->GetWaterZ(), Aim, Time);
					if (M < PassBest) { PassBest = M; PassPitch = P; PassTime = Time; }
				}
				Pitch = PassPitch; Miss = PassBest;
				Aim = F->GetActorLocation() + Vel * (PassTime + 0.1f); // + aim settle time
			}
			UE_LOG(LogTemp, Display, TEXT("HLSHOOT candidate %s dist=%.0f miss=%.1f"),
				*F->GetName(), FVector::Dist(F->GetActorLocation(), Eye), Miss);
			// Prefer close fish with a clean line: a long flight gives a cruising fish
			// time to leave the aim point.
			const float Score = Miss + 0.01f * FVector::Dist(F->GetActorLocation(), Eye);
			if (Miss < 5.0f && Score < BestScore) { BestScore = Score; BestMiss = Miss; BestPitch = Pitch; BestYaw = Yaw; BestFish = F; }
		}
		if (!BestFish)
		{
			UE_LOG(LogTemp, Warning, TEXT("HLSHOOT no fish left"));
			GM->Bow->ReleaseDraw();
			return;
		}
		UE_LOG(LogTemp, Warning, TEXT("HLSHOOT target %s dist=%.0f depth=%.0f pitch=%.2f predictedMiss=%.1f cm"),
			*BestFish->GetName(), FVector::Dist(BestFish->GetActorLocation(), Eye),
			GM->Water->GetWaterZ() - BestFish->GetActorLocation().Z, BestPitch, BestMiss);
		C->SetControlRotation(FRotator(BestPitch, BestYaw, 0.0f));
		// The bow reads the view this frame; the camera follows control rotation.
		if (APawn* P = C->GetPawn()) { P->FaceRotation(FRotator(BestPitch, BestYaw, 0.0f), 0.0f); }
		// The camera manager caches the view: give it a few frames to take the new
		// aim before releasing, or the arrow flies along the old view.
		FTimerHandle Settle;
		TWeakObjectPtr<AHLGameMode> WeakGM(GM);
		GM->GetWorld()->GetTimerManager().SetTimer(Settle, [WeakGM]()
		{
			if (WeakGM.IsValid() && WeakGM->Bow) { WeakGM->Bow->ReleaseDraw(); }
		}, 0.1f, false);
	}, DrawTime, false);
}

void AHLGameMode::FixLighting(UWorld* World)
{
	// The level's sun must drive the SkyAtmosphere, or the sky renders black.
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		if (UDirectionalLightComponent* C = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
		{
			UE_LOG(LogTemp, Warning, TEXT("HLLIGHT sun %s atmo=%d intensity=%.2f rot=%s mob=%d"),
				*It->GetName(), C->IsUsedAsAtmosphereSunLight() ? 1 : 0, C->Intensity,
				*It->GetActorRotation().ToCompactString(), (int32)C->Mobility);

			// make_bowl.py passed unreal.Rotator(-14, 148, 0), but the Python
			// Rotator takes (roll, pitch, yaw): the sun ended up below the horizon
			// and the sky rendered as night. Aim it here, warm and fairly low.
			C->SetMobility(EComponentMobility::Movable);
			It->SetActorRotation(FRotator(-38.0f, 148.0f, 0.0f));
			C->SetAtmosphereSunLight(true);
			C->SetIntensity(10.0f);
		}
	}
	for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
	{
		UE_LOG(LogTemp, Warning, TEXT("HLLIGHT atmosphere %s at %s"), *It->GetName(), *It->GetActorLocation().ToCompactString());
	}
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		if (USkyLightComponent* C = It->GetLightComponent())
		{
			UE_LOG(LogTemp, Warning, TEXT("HLLIGHT skylight %s intensity=%.2f realtime=%d"), *It->GetName(), C->Intensity, C->bRealTimeCapture ? 1 : 0);
		}
	}
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		UE_LOG(LogTemp, Warning, TEXT("HLLIGHT ppv %s enabled=%d unbound=%d"), *It->GetName(), It->bEnabled ? 1 : 0, It->bUnbound ? 1 : 0);

		// Fixed manual exposure (the project turns auto exposure off so the view
		// through the water stays stable). -hlev=<bias> overrides for tuning.
		float Bias = -1.0f;
		FParse::Value(FCommandLine::Get(), TEXT("hlev="), Bias);
		It->bEnabled = true;
		It->bUnbound = true;
		FPostProcessSettings& S = It->Settings;
		S.bOverride_AutoExposureMethod = true;
		S.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		S.AutoExposureApplyPhysicalCameraExposure = false;
		S.bOverride_AutoExposureBias = true;
		S.AutoExposureBias = Bias;
		S.bOverride_AutoExposureMinBrightness = false;
		S.bOverride_AutoExposureMaxBrightness = false;
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		UE_LOG(LogTemp, Warning, TEXT("HLLIGHT fog %s at %s"), *It->GetName(), *It->GetActorLocation().ToCompactString());
	}
}

int32 AHLGameMode::CountAlive() const
{
	int32 N = 0;
	for (const AHLFish* F : Fish)
	{
		if (F && !F->bLanded)
		{
			++N;
		}
	}
	return N;
}

FVector AHLGameMode::FindDeepWater(float MinDepthCm) const
{
	const FVector2D Extent = Terrain->GetExtentCm();
	for (int32 Attempt = 0; Attempt < 400; ++Attempt)
	{
		const float X = FMath::FRandRange(0.0f, Extent.X);
		const float Y = FMath::FRandRange(0.0f, Extent.Y);
		const float Depth = Terrain->DepthAt(X, Y);
		if (Depth >= MinDepthCm)
		{
			return FVector(X, Y, Terrain->GetWaterZ() - Depth * 0.5f);
		}
	}
	// Fallback: the deepest point, never the middle of the terrain (that put fish
	// 140 m away, outside the lake).
	const FVector Deep = Terrain->GetDeepestPoint();
	return FVector(Deep.X, Deep.Y, Terrain->GetWaterZ() - Terrain->DepthAt(Deep.X, Deep.Y) * 0.5f);
}

void AHLGameMode::SpawnFish()
{
	UWorld* World = GetWorld();
	if (!World || !Terrain)
	{
		return;
	}

	for (int32 I = 0; I < FishCount; ++I)
	{
		// Hold stations prefer shallower shelves (where they can be seen), but a
		// third of the population sits deep, which is what makes a landed deep
		// fish feel earned.
		const float MinDepth = (I % 3 == 0) ? 150.0f : 40.0f;
		const FVector Home = FindDeepWater(MinDepth);

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AHLFish* F = World->SpawnActor<AHLFish>(
			AHLFish::StaticClass(), Home, FRotator::ZeroRotator, Params);
		if (!F)
		{
			continue;
		}

		const float Length = FMath::FRandRange(22.0f, 48.0f);
		F->Init(Terrain, Home, Length);
		F->SpeciesId = TEXT("brook");
		Fish.Add(F);
	}
}
