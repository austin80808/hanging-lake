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
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
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
	// Standing spot: the south boardwalk, the postcard view north across the log
	// to the falls.
	static const FVector2D Spawn(58544.f, 59512.f);
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
		// Opened through a symlink (e.g. an old ~/Documents/Unreal Projects link),
		// the relative path misses: fall back to the repo's usual home.
		if (!IFileManager::Get().DirectoryExists(*FPaths::ConvertRelativePathToFull(DataDir)))
		{
			DataDir = FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT("dev/hanging-lake/pipeline/data"));
		}
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
	SpawnFalls(World);
	SpawnTrees(World);

	// --- player -----------------------------------------------------------
	const FVector Spawn = GetSpawnPoint();
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (ACharacter* Char = Cast<ACharacter>(PC->GetPawn()))
		{
			Char->SetActorLocation(Spawn);

			// Open on Hanging Lake's fallen log with the falls behind it: aim a
			// little past the log toward the falls, slightly down.
			const FVector Target = FMath::Lerp(LogCentre, FVector(HLLandmarks::Falls, LogCentre.Z), 0.45f)
				+ FVector(0.0f, 0.0f, 150.0f);
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

	BuildCoverPoints();
	if (!FParse::Param(FCommandLine::Get(), TEXT("hlnofish")))
	{
		SpawnFish();
	}

	// --- journal ----------------------------------------------------------
	// Saved/HangingLake/Journal.json, or -hljournal=<file> (a test can start clean).
	FString JournalPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HangingLake"), TEXT("Journal.json"));
	JournalPath = FPaths::ConvertRelativePathToFull(JournalPath);
	FString JournalArg;
	if (FParse::Value(FCommandLine::Get(), TEXT("hljournal="), JournalArg))
	{
		// Relative to where the game was launched from, not the engine's binaries.
		JournalPath = FPaths::ConvertRelativePathToFull(FPaths::LaunchDir(), JournalArg);
	}
	Journal.SetPath(JournalPath);
	Journal.Load();
	if (Bow)
	{
		Bow->OnFishHit.AddUObject(this, &AHLGameMode::OnFishLanded);
	}
	{
		FTimerHandle JournalTimer;
		TWeakObjectPtr<AHLGameMode> WeakThis(this);
		World->GetTimerManager().SetTimer(JournalTimer, [WeakThis]()
		{
			if (WeakThis.IsValid()) { WeakThis->UpdateJournal(0.25f); }
		}, 0.25f, true, 1.0f);
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

	// Test: start on the bank behind the log and walk out along it (-hlwalklog).
	if (FParse::Param(FCommandLine::Get(), TEXT("hlwalklog")) && LogPath.Num() > 2)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* P = PC->GetPawn())
			{
				const FVector Dir = (LogPath.Last() - LogPath[0]).GetSafeNormal2D();
				// Stand on the shelf 1 m behind the log's end, feet on the ground.
				const FVector Start = LogPath[0] - Dir * 100.0f;
				P->SetActorLocation(FVector(Start.X, Start.Y, FMath::Max(Terrain->ElevationAt(Start.X, Start.Y), LogPath[0].Z) + 100.0f),
					false, nullptr, ETeleportType::TeleportPhysics);
				PC->SetControlRotation(Dir.Rotation());
				PC->SetIgnoreLookInput(true);
				TWeakObjectPtr<APawn> WeakP(P);
				FTimerHandle Walk;
				World->GetTimerManager().SetTimer(Walk, [WeakP, Dir]()
				{
					if (WeakP.IsValid()) { WeakP->AddMovementInput(Dir, 1.0f); }
				}, 0.02f, true, 20.0f);
				FTimerHandle Report;
				TWeakObjectPtr<AHLGameMode> WeakGM(this);
				World->GetTimerManager().SetTimer(Report, [WeakP, WeakGM]()
				{
					if (!WeakP.IsValid() || !WeakGM.IsValid()) { return; }
					const FVector L = WeakP->GetActorLocation();
					const float Feet = L.Z - 90.0f;
					float Best = TNumericLimits<float>::Max(); int32 BestI = 0;
					for (int32 I = 0; I < WeakGM->LogPath.Num(); ++I)
					{
						const float D = FVector::Dist2D(L, WeakGM->LogPath[I]);
						if (D < Best) { Best = D; BestI = I; }
					}
					UE_LOG(LogTemp, Warning, TEXT("HLWALK feet %.0f cm over water, %.0f cm off the log line, along %d/%d, log top %.0f"),
						Feet - WeakGM->Terrain->GetWaterZ(), Best, BestI, WeakGM->LogPath.Num() - 1,
						WeakGM->LogPath[BestI].Z - WeakGM->Terrain->GetWaterZ());
				}, 1.0f, true, 20.0f);
			}
		}
	}

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

	// Test runs: quit by themselves after <s> seconds (-hlquit=<s>), so a script
	// can play a session and read the log (game/Tests/play_test.sh).
	float QuitAfter = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("hlquit="), QuitAfter) && QuitAfter > 0.0f)
	{
		FTimerHandle QuitTimer;
		World->GetTimerManager().SetTimer(QuitTimer, []()
		{
			UE_LOG(LogTemp, Warning, TEXT("HLQUIT time is up"));
			FPlatformMisc::RequestExit(false, TEXT("hlquit"));
		}, QuitAfter, false);
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
				T.Append({A, B, C, B, D, C}); // outward-facing
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
				if (End == 0) { T.Append({Centre, B, A}); } else { T.Append({Centre, A, B}); }
			}
		}
	}
}

void AHLGameMode::SpawnFalls(UWorld* World)
{
	// Bridal Veil-style curtains: thin white ribbons along the north rim, from the
	// travertine ledge down into the lake, where East Fork Dead Horse Creek comes in.
	UMaterialInterface* FallsMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Falls.M_Falls"));
	const float WaterZ = Terrain->GetWaterZ();
	AActor* Falls = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Falls, TEXT("Falls"));
	Falls->SetRootComponent(Mesh);
	Mesh->RegisterComponent();
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);

	TArray<FVector> V, N; TArray<int32> T; TArray<FColor> C;
	int32 Count = 0;
	for (float Y = 57300.0f; Y <= 59200.0f; Y += 55.0f)
	{
		// Walk north (towards smaller X) from the lake centre to the shore.
		float ShoreX = -1.0f;
		for (float X = Terrain->GetLakeCentre().X; X > Terrain->GetLakeCentre().X - 3000.0f; X -= 20.0f)
		{
			if (Terrain->DepthAt(X, Y) <= 0.0f) { ShoreX = X; break; }
		}
		if (ShoreX < 0.0f) { continue; }
		// The ledge top: the highest ground within 6 m behind the shore.
		float TopX = ShoreX, TopZ = WaterZ;
		for (float X = ShoreX; X > ShoreX - 600.0f; X -= 25.0f)
		{
			const float Zh = Terrain->ElevationAt(X, Y);
			if (Zh > TopZ) { TopZ = Zh; TopX = X; }
		}
		if (TopZ - WaterZ < 250.0f) { continue; } // no ledge here, no fall
		const float Jitter = FMath::Abs(FMath::Sin(Y * 0.37f));
		if (Jitter < 0.15f) { continue; }          // gaps between curtains
		const float Width = 20.0f + 70.0f * Jitter;
		// Ribbon: 14 rows from the lip to the water, bulging out over the ledge.
		const int32 Base = V.Num();
		for (int32 Rw = 0; Rw <= 14; ++Rw)
		{
			const float U = Rw / 14.0f;
			const float Xr = FMath::Lerp(TopX + 40.0f, ShoreX + 60.0f, U) + 60.0f * FMath::Sin(PI * U);
			const float Zr = FMath::Lerp(TopZ + 10.0f, WaterZ, U);
			for (int32 Cl = 0; Cl < 3; ++Cl)
			{
				const float Off = (Cl - 1) * Width * 0.5f * (1.0f + 0.4f * U); // spreads as it falls
				V.Add(FVector(Xr, Y + Off, Zr));
				N.Add(FVector(1, 0, 0));
				const uint8 A = Cl == 1 ? (uint8)(255 * (1.0f - 0.5f * U)) : 0;
				C.Add(FColor(255, 255, 255, A));
			}
		}
		for (int32 Rw = 0; Rw < 14; ++Rw)
		{
			for (int32 Cl = 0; Cl < 2; ++Cl)
			{
				const int32 A = Base + Rw * 3 + Cl, B = A + 1, Cc = A + 3, D = Cc + 1;
				T.Append({A, Cc, B, B, Cc, D});
			}
		}
		++Count;
	}
	TArray<FVector2D> UV; UV.SetNumZeroed(V.Num());
	TArray<FProcMeshTangent> Tan;
	Mesh->CreateMeshSection(0, V, T, N, UV, C, Tan, false);
	if (FallsMat) { Mesh->SetMaterial(0, FallsMat); }
	UE_LOG(LogTemp, Display, TEXT("HL falls: %d curtains on the north rim"), Count);
}

void AHLGameMode::SpawnTrees(UWorld* World)
{
	// Conifers on every slope gentle enough to hold them, around the bowl: the
	// photos show the lake ringed by spruce and fir.
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Cone || !Cyl || !Base) { return; }
	AActor* Forest = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	USceneComponent* Root = NewObject<USceneComponent>(Forest, TEXT("Root"));
	Forest->SetRootComponent(Root);
	Root->RegisterComponent();
	auto MakeISM = [&](UStaticMesh* M, const FLinearColor& Col) {
		UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(Forest);
		I->SetupAttachment(Root);
		I->RegisterComponent();
		I->SetStaticMesh(M);
		I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(Base, Forest);
		Mat->SetVectorParameterValue(TEXT("Color"), Col);
		I->SetMaterial(0, Mat);
		return I;
	};
	UInstancedStaticMeshComponent* Crowns = MakeISM(Cone, FLinearColor(0.02f, 0.07f, 0.03f));
	UInstancedStaticMeshComponent* Crowns2 = MakeISM(Cone, FLinearColor(0.04f, 0.10f, 0.03f));
	UInstancedStaticMeshComponent* Trunks = MakeISM(Cyl, FLinearColor(0.10f, 0.07f, 0.05f));

	const float WaterZ = Terrain->GetWaterZ();
	const FVector Centre = Terrain->GetLakeCentre();
	FRandomStream Rng(20261104);
	int32 Planted = 0;
	for (int32 Try = 0; Try < 6000 && Planted < 900; ++Try)
	{
		const float Ang = Rng.FRandRange(0.0f, 2.0f * PI);
		const float Rad = FMath::Sqrt(Rng.FRand()) * 9000.0f;
		const float X = Centre.X + FMath::Cos(Ang) * Rad, Y = Centre.Y + FMath::Sin(Ang) * Rad;
		if (Terrain->DepthAt(X, Y) > 0.0f) { continue; }
		const float Zg = Terrain->ElevationAt(X, Y);
		if (Zg - WaterZ < 80.0f) { continue; }                     // not on the shore shelf
		const float Gx = Terrain->ElevationAt(X + 150.0f, Y) - Terrain->ElevationAt(X - 150.0f, Y);
		const float Gy = Terrain->ElevationAt(X, Y + 150.0f) - Terrain->ElevationAt(X, Y - 150.0f);
		if (FMath::Sqrt(Gx * Gx + Gy * Gy) / 300.0f > 1.1f) { continue; } // too steep: bare rock
		// Nothing grows on the travertine ledge in front of the falls.
		if (Zg - WaterZ < 700.0f && FVector2D::Distance(FVector2D(X, Y), FVector2D(Centre)) < 3000.0f) { continue; }
		// Keep the boardwalk view clear: no trees between the walk and the lake.
		if (FVector2D::Distance(FVector2D(X, Y), HLLandmarks::Spawn) < 700.0f) { continue; }
		const float H = Rng.FRandRange(900.0f, 2200.0f);
		const float R = H * Rng.FRandRange(0.14f, 0.2f);
		const float Crown = H * 0.8f;
		// Engine Cone and Cylinder are 100 cm tall with the pivot at the centre.
		(Rng.FRand() < 0.5f ? Crowns : Crowns2)->AddInstance(FTransform(FRotator(0, Rng.FRandRange(0, 360), 0),
			FVector(X, Y, Zg + H - Crown * 0.5f), FVector(R / 50.0f, R / 50.0f, Crown / 100.0f)));
		Trunks->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X, Y, Zg + H * 0.25f),
			FVector(0.25f + R / 900.0f, 0.25f + R / 900.0f, H * 0.5f / 100.0f)));
		++Planted;
	}
	UE_LOG(LogTemp, Display, TEXT("HL forest: %d trees"), Planted);
}

FVector AHLGameMode::SpawnLog(UWorld* World)
{
	// Hanging Lake's famous fallen log, at its surveyed spot (see HLLandmarks): a
	// bleached, barkless trunk with its thick end up on the north-east rim and
	// the thin end dipping under the water to the south-west.
	const float WaterZ = Terrain->GetWaterZ();
	// The shore end must rest on dry land so it can be walked onto, as people do
	// at the real lake. The authored lake bed does not match the aerial
	// shoreline exactly, so extend the trunk north-east until it meets the bank.
	const FVector2D OutDir = (HLLandmarks::LogShoreEnd - HLLandmarks::LogLakeEnd).GetSafeNormal();
	FVector2D Edge = HLLandmarks::LogShoreEnd, Bank = HLLandmarks::LogShoreEnd;
	for (float D = 0.0f; D < 2500.0f; D += 20.0f)
	{
		const FVector2D Q = HLLandmarks::LogShoreEnd + OutDir * D;
		// First ground at or above the waterline: the travertine shelf at the foot
		// of the falls ledge, about 3.5 m past the photo-measured end.
		if (Terrain->ElevationAt(Q.X, Q.Y) >= WaterZ) { Edge = Q; Bank = Q + OutDir * 50.0f; break; }
	}
	const float BankZ = FMath::Clamp(Terrain->ElevationAt(Bank.X, Bank.Y) + 4.0f, WaterZ + 3.0f, WaterZ + 60.0f);
	const FVector Shore(Bank, BankZ);
	const FVector Tip(HLLandmarks::LogLakeEnd, WaterZ - 1.0f);
	// Fraction of the length (from the shore end) where the trunk reaches open water.
	const float EdgeU = FMath::Clamp(FVector2D::Distance(Bank, Edge) / FVector2D::Distance(Bank, HLLandmarks::LogLakeEnd) + 0.04f, 0.02f, 0.5f);
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
		P += Across * 8.0f * FMath::Sin(PI * U);    // nearly straight
		// Floats flat on the water, rising only where it climbs onto the bank.
		P.Z = U < EdgeU ? FMath::Lerp(BankZ, WaterZ + 3.0f, FMath::SmoothStep(0.0f, 1.0f, U / EdgeU))
		                : FMath::Lerp(WaterZ + 3.0f, WaterZ - 1.0f, (U - EdgeU) / (1.0f - EdgeU));
		Spine.Add(P);
		Radii.Add(FMath::Lerp(13.0f, 7.0f, FMath::Pow(U, 0.8f))); // a slim, barkless trunk
	}
	AddTube(Spine, Radii, 16, V, T, N);
	// Broken branch stubs, short and angled the way a fallen conifer keeps them.
	// The photos show a row of short broken branch spikes standing up along its
	// whole length, every half metre or so.
	for (int32 I = 3; I < Rings - 1; I += 2)
	{
		const float Side = (I / 2) % 2 == 0 ? 1.0f : -1.0f;
		const float Tilt = FMath::DegreesToRadians(15.0f + 20.0f * FMath::Abs(FMath::Sin(I * 1.3f)));
		const FVector Out = (FVector::UpVector * FMath::Cos(Tilt) + Across * Side * FMath::Sin(Tilt)).GetSafeNormal();
		const float Len = 8.0f + 14.0f * FMath::Abs(FMath::Sin(I * 2.1f));
		TArray<FVector> StubSpine = {Spine[I], Spine[I] + Out * (Radii[I] + Len)};
		TArray<float> StubR = {2.2f, 0.8f};
		AddTube(StubSpine, StubR, 6, V, T, N);
	}
	TArray<FVector2D> UV; UV.SetNumZeroed(V.Num());
	// Bleached wood, via the bowl's rock material (lit, vertex colour as base colour,
	// a little self-glow) so it stays bright at a distance like the photos.
	TArray<FColor> Col; Col.Init(FColor(214, 207, 190), V.Num());
	TArray<FProcMeshTangent> Tan;
	Mesh->CreateMeshSection(0, V, T, N, UV, Col, Tan, true);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionObjectType(ECC_WorldStatic);
	Mesh->SetCollisionResponseToAllChannels(ECR_Block);
	if (UMaterialInterface* Wood = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_BowlRock.M_BowlRock")))
	{
		Mesh->SetMaterial(0, Wood);
	}
	// Moss and grass tufts growing on the log.
	{
		TArray<FVector> MV, MN; TArray<int32> MT;
		const float Tufts[] = {0.12f, 0.27f, 0.46f, 0.63f, 0.81f};
		for (const float U : Tufts)
		{
			const int32 I = FMath::RoundToInt(U * Rings);
			TArray<FVector> TuftSpine = {Spine[I] + FVector(0, 0, Radii[I] * 0.6f), Spine[I] + FVector(0, 0, Radii[I] + 14.0f)};
			TArray<float> TuftR = {11.0f, 3.0f};
			AddTube(TuftSpine, TuftR, 10, MV, MT, MN);
		}
		TArray<FVector2D> MUV; MUV.SetNumZeroed(MV.Num());
		TArray<FColor> MC; MC.Init(FColor::White, MV.Num());
		TArray<FProcMeshTangent> MTan;
		Mesh->CreateMeshSection(1, MV, MT, MN, MUV, MC, MTan, false);
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			UMaterialInstanceDynamic* MossMat = UMaterialInstanceDynamic::Create(Base, Log);
			MossMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.12f, 0.30f, 0.04f));
			Mesh->SetMaterial(1, MossMat);
		}
	}
	// A walkable strip along the top, pawns only (arrows still hit the trunk
	// itself): a 13 cm round trunk would otherwise roll a player straight off.
	for (int32 I = 0; I < Rings; I += 2)
	{
		const FVector A = Spine[I] + FVector(0, 0, Radii[I]);
		const FVector B = Spine[I + 2] + FVector(0, 0, Radii[I + 2]);
		UBoxComponent* Walk = NewObject<UBoxComponent>(Log);
		Walk->SetupAttachment(Mesh);
		Walk->RegisterComponent();
		Walk->SetWorldLocationAndRotation((A + B) * 0.5f - FVector(0, 0, 4.0f), (B - A).Rotation());
		Walk->SetBoxExtent(FVector(FVector::Dist(A, B) * 0.5f + 6.0f, 20.0f, 4.0f));
		Walk->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Walk->SetCollisionObjectType(ECC_WorldStatic);
		Walk->SetCollisionResponseToAllChannels(ECR_Ignore);
		Walk->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Walk->SetHiddenInGame(true);
	}
	LogPath.Reset();
	for (int32 I = 0; I <= Rings; ++I) { LogPath.Add(Spine[I] + FVector(0, 0, Radii[I])); }

	UE_LOG(LogTemp, Display, TEXT("HL log: %.0f cm from %s to %s"), FVector::Dist(Shore, Tip),
		*Shore.ToCompactString(), *Tip.ToCompactString());
	return FVector(HLLandmarks::LogCentre, WaterZ);
}

namespace
{
	// Mirrors AHLBow::Tick's flight model at full draw. Returns the closest the
	// path comes to Target before it hits the ground, and the time it gets there.
	float SimulateMiss(const UHLTerrainData* Terrain, const FVector& Eye, const FVector& Dir,
		float WaterZ, const FVector& Target, bool bBendsLikeLight, float& OutTime)
	{
		// Step for step what AHLBow::StepArrow does at full draw: 1/120 s steps,
		// split at the surface so the bend happens exactly where the arrow enters.
		FVector Pos = Eye + Dir * 60.0f; // the bow starts the arrow 60 cm out
		FVector Vel = Dir * 4200.0f;
		bool bUnder = Pos.Z < WaterZ;
		float Best = TNumericLimits<float>::Max();
		OutTime = 0.0f;
		const float Dt = 1.0f / 120.0f;
		for (float T = 0.0f; T < 4.0f; T += Dt)
		{
			auto Consider = [&](const FVector& A, const FVector& B)
			{
				const float D = FMath::PointDistToSegment(Target, A, B);
				if (D < Best) { Best = D; OutTime = T; }
			};
			Vel.Z -= 980.0f * Dt;
			if (bUnder)
			{
				Vel *= FMath::Pow(0.12f, Dt);
			}
			FVector Next = Pos + Vel * Dt;
			if (!bUnder && Next.Z < WaterZ)
			{
				const float U = FMath::Clamp((Pos.Z - WaterZ) / (Pos.Z - Next.Z), 0.0f, 1.0f);
				const FVector Cross = Pos + (Next - Pos) * U;
				Consider(Pos, Cross);
				Pos = Cross;
				bUnder = true;
				bool bTIR = false;
				Vel = bBendsLikeLight
					? FHLSnellSolver::Refract(Vel.GetSafeNormal(), FVector::UpVector, 1.0f,
						FHLSnellSolver::WaterIOR, bTIR) * Vel.Size() * 0.9f
					: Vel * 0.9f;
				Next = Pos + Vel * (Dt * (1.0f - U));
			}
			Consider(Pos, Next);
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
						GM->Water->GetWaterZ(), Aim, GM->Bow->bArrowBendsLikeLight, Time);
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
		if (FParse::Param(FCommandLine::Get(), TEXT("hlaimimage")))
		{
			// Test the README's promise instead of the solver: point the reticle at the
			// nearest fish's image, as a player would, and let go. Only still fish
			// (holding or resting) within 4.5 m qualify: past that, gravity alone
			// drops a full-draw arrow more than a trout is tall, and a cruising fish
			// moves out of the way during the flight. With the arrow bending like
			// light (-hlbendarrow) these shots land; with a real, straight arrow (the
			// default) they go high.
			BestFish = nullptr;
			float BestDist = 450.0f;
			for (const auto& Pair : Before)
			{
				AHLFish* F = Pair.Key.Get();
				if (!F || F->bLanded
					|| (F->GetState() != EHLFishState::Hold && F->GetState() != EHLFishState::Rest))
				{
					continue;
				}
				const float D = FVector::Dist(F->GetImageLocation(), Eye);
				if (D < BestDist) { BestDist = D; BestFish = F; }
			}
			if (!BestFish)
			{
				UE_LOG(LogTemp, Warning, TEXT("HLSHOOT skipped: no still fish within 4.5 m to aim at"));
				GM->Bow->CancelDraw();
				return;
			}
			if (BestFish)
			{
				const FRotator R = (BestFish->GetImageLocation() - Eye).Rotation();
				BestPitch = R.Pitch; BestYaw = R.Yaw; BestMiss = -1.0f;
				UE_LOG(LogTemp, Warning, TEXT("HLSHOOT aiming at the image of %s: %.0f cm above the fish, %.0f cm away"),
					*BestFish->GetName(), BestFish->GetImageLocation().Z - BestFish->GetActorLocation().Z, BestDist);
			}
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
			It->SetActorRotation(FRotator(-SunElevationDeg, 148.0f, 0.0f));
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

FVector AHLGameMode::FindDeepWater(float MinDepthCm, FRandomStream& Stream) const
{
	const FVector2D Extent = Terrain->GetExtentCm();
	for (int32 Attempt = 0; Attempt < 400; ++Attempt)
	{
		const float X = Stream.FRandRange(0.0f, Extent.X);
		const float Y = Stream.FRandRange(0.0f, Extent.Y);
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

	// The same fish every visit: homes, sizes and markings come from a fixed seed.
	FRandomStream Pop(PopulationSeed);
	for (int32 I = 0; I < FishCount; ++I)
	{
		// Hold stations prefer shallower shelves (where they can be seen), but a
		// third of the population sits deep, which is what makes a landed deep
		// fish feel earned.
		const float MinDepth = (I % 3 == 0) ? 150.0f : 40.0f;
		const FVector Home = FindDeepWater(MinDepth, Pop);
		const float Length = Pop.FRandRange(22.0f, 48.0f);
		const float Shade = Pop.FRandRange(0.8f, 1.2f);

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AHLFish* F = World->SpawnActor<AHLFish>(
			AHLFish::StaticClass(), Home, FRotator::ZeroRotator, Params);
		if (!F)
		{
			continue;
		}

		F->FishId = I + 1;
		// Hanging Lake's trout are cutthroat, an isolated native population. From
		// the boardwalk they read as dark olive backs over the pale travertine.
		F->SpeciesId = TEXT("cutthroat");
		F->SetCoverPoints(CoverPoints);
		F->Init(Terrain, Home, Length);
		F->SetColour(FLinearColor(0.13f, 0.12f, 0.06f) * Shade);
		Fish.Add(F);
	}
}

void AHLGameMode::BuildCoverPoints()
{
	CoverPoints.Reset();
	if (!Terrain)
	{
		return;
	}
	const float WaterZ = Terrain->GetWaterZ();

	// The log's shade, along the floating part of the trunk.
	for (int32 I = 0; I < LogPath.Num(); I += 2)
	{
		if (Terrain->DepthAt(LogPath[I].X, LogPath[I].Y) > 40.0f)
		{
			CoverPoints.Add(FVector(LogPath[I].X, LogPath[I].Y, WaterZ));
		}
	}

	// The churned, deeper water below the falls.
	for (const float Radius : {300.0f, 600.0f, 900.0f})
	{
		for (int32 A = 0; A < 16; ++A)
		{
			const float Angle = 2.0f * PI * A / 16.0f;
			const FVector P(HLLandmarks::Falls.X + FMath::Cos(Angle) * Radius,
				HLLandmarks::Falls.Y + FMath::Sin(Angle) * Radius, WaterZ);
			if (Terrain->DepthAt(P.X, P.Y) > 60.0f)
			{
				CoverPoints.Add(P);
			}
		}
	}
	UE_LOG(LogTemp, Display, TEXT("HL: %d cover points (log and falls)"), CoverPoints.Num());
}

FString AHLGameMode::PlaceName(const FVector& Where) const
{
	const FVector Flat(Where.X, Where.Y, 0.0f);
	auto DistToLine = [&Flat](const TArray<FVector>& Pts)
	{
		float Best = TNumericLimits<float>::Max();
		for (int32 I = 0; I + 1 < Pts.Num(); ++I)
		{
			Best = FMath::Min(Best, FMath::PointDistToSegment(
				Flat, FVector(Pts[I].X, Pts[I].Y, 0.0f), FVector(Pts[I + 1].X, Pts[I + 1].Y, 0.0f)));
		}
		return Best;
	};

	if (DistToLine(LogPath) < 300.0f)
	{
		return TEXT("under the log");
	}
	if (FVector::Dist2D(Flat, FVector(HLLandmarks::Falls, 0.0f)) < 900.0f)
	{
		return TEXT("below the falls");
	}
	TArray<FVector> Walk;
	for (const FVector2D& P : HLLandmarks::Boardwalk)
	{
		Walk.Add(FVector(P, 0.0f));
	}
	if (DistToLine(Walk) < 400.0f)
	{
		return TEXT("off the boardwalk");
	}

	const float Depth = Terrain ? Terrain->DepthAt(Where.X, Where.Y) : 0.0f;
	if (Depth > 160.0f)
	{
		return TEXT("in the deep middle");
	}
	if (Depth < 70.0f)
	{
		return TEXT("on the shallow shelf");
	}
	return TEXT("in open water");
}

void AHLGameMode::UpdateJournal(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || !Terrain)
	{
		return;
	}

	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);
	const FVector Forward = View.Vector();
	const APawn* Pawn = PC->GetPawn();
	const FVector Feet = Pawn ? Pawn->GetActorLocation() : Eye;

	// "In view" means near the middle of the screen, in range and not behind rock
	// or the log: what a person would call watching it.
	const float ViewCos = FMath::Cos(FMath::DegreesToRadians(18.0f));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HLJournalSight), false, Pawn);

	for (AHLFish* F : Fish)
	{
		if (!F || F->bLanded)
		{
			continue;
		}
		const int32 Id = F->FishId;
		const bool bCalm = F->GetState() != EHLFishState::Startle;
		if (!bCalm)
		{
			CloseSeconds.Remove(Id); // spooked: the stalk starts over
		}

		// Both stages need an unbroken stretch: look away, lose sight of it or let
		// it spook, and the count starts again.
		const FVector Img = F->GetImageLocation();
		const FVector To = Img - Eye;
		const float Dist = To.Size();
		bool bInView = Dist <= 3500.0f && Dist >= 1.0f && FVector::DotProduct(To / Dist, Forward) >= ViewCos;
		if (bInView)
		{
			FHitResult Hit;
			bInView = !World->LineTraceSingleByChannel(Hit, Eye, Img, ECC_Visibility, Params) || Hit.GetActor() == F;
		}
		if (!bInView)
		{
			SeenSeconds.Remove(Id);
			CloseSeconds.Remove(Id);
			continue;
		}

		float& Seen = SeenSeconds.FindOrAdd(Id);
		Seen += DeltaSeconds;
		if (Seen >= 1.5f && Journal.GetStage(Id) < EHLJournalStage::Observed)
		{
			RecordJournal(F, EHLJournalStage::Observed, Dist);
		}

		// Stalked: watched from within bow range while it carried on undisturbed.
		const float Flat = FVector::Dist2D(Feet, F->GetActorLocation());
		if (!bCalm || Flat >= StalkRangeCm)
		{
			CloseSeconds.Remove(Id);
			continue;
		}
		float& Close = CloseSeconds.FindOrAdd(Id);
		Close += DeltaSeconds;
		if (Close >= 2.0f && Journal.GetStage(Id) < EHLJournalStage::Stalked)
		{
			RecordJournal(F, EHLJournalStage::Stalked, Flat);
		}
	}
}

void AHLGameMode::RecordJournal(AHLFish* F, EHLJournalStage Stage, float DistanceCm)
{
	if (!F)
	{
		return;
	}
	FHLJournalMoment Moment;
	Moment.When = FDateTime::UtcNow(); // stored as ISO 8601 with a Z, so it must be UTC
	Moment.SunElevationDeg = SunElevationDeg;
	Moment.DepthCm = F->GetDepthCm();
	Moment.DistanceCm = DistanceCm;
	Moment.Place = PlaceName(F->GetActorLocation());

	const bool bNew = Journal.Record(F->FishId, F->SpeciesId, F->GetBodyLengthCm(), Stage, Moment);
	if (!bNew && Stage != EHLJournalStage::Landed)
	{
		return;
	}
	Journal.Save();

	const FHLJournalEntry* E = Journal.Find(F->FishId);
	JournalNote = (Stage == EHLJournalStage::Landed && E && E->TimesLanded > 1)
		? FString::Printf(TEXT("No. %d landed again (%d times)"), F->FishId, E->TimesLanded)
		: FString::Printf(TEXT("No. %d, %.0f cm %s: %s %s, %.1f m down"),
			F->FishId, F->GetBodyLengthCm(), *F->SpeciesId.ToString(),
			*FHLJournal::StageName(Stage), *Moment.Place, Moment.DepthCm / 100.0f);
	JournalNoteTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogTemp, Warning, TEXT("HLJOURNAL %s"), *JournalNote);
}

void AHLGameMode::OnFishLanded(AHLFish* F)
{
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const float Dist = (F && Pawn) ? FVector::Dist(Pawn->GetActorLocation(), F->GetActorLocation()) : 0.0f;
	RecordJournal(F, EHLJournalStage::Landed, Dist);
}

bool AHLGameMode::GetJournalNote(FString& OutNote, float& OutAgeSeconds) const
{
	if (JournalNote.IsEmpty() || !GetWorld())
	{
		return false;
	}
	OutNote = JournalNote;
	OutAgeSeconds = GetWorld()->GetTimeSeconds() - JournalNoteTime;
	return true;
}
