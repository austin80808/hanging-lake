#include "HLTerrainData.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HLTerrainActor.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ReadJsonObject(const FString& Path, TSharedPtr<FJsonObject>& Out)
	{
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *Path))
		{
			return false;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		return FJsonSerializer::Deserialize(Reader, Out) && Out.IsValid();
	}

	bool ReadFloat32Array(const FString& Path, TArray<float>& Out, int32& OutCount)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			return false;
		}
		if (Bytes.Num() % sizeof(float) != 0)
		{
			UE_LOG(LogTemp, Error, TEXT("HL: %s is not a whole number of float32s"), *Path);
			return false;
		}
		const int32 N = Bytes.Num() / sizeof(float);
		Out.SetNumUninitialized(N);
		FMemory::Memcpy(Out.GetData(), Bytes.GetData(), Bytes.Num());
		OutCount = N;
		return true;
	}
}

bool UHLTerrainData::LoadFromDirectory(const FString& DataDir)
{
	const FString BowlPath = FPaths::Combine(DataDir, TEXT("bowl.json"));
	const FString HeightsPath = FPaths::Combine(DataDir, TEXT("raw"), TEXT("heights_m.f32"));
	const FString DepthPath = FPaths::Combine(DataDir, TEXT("raw"), TEXT("depth_m.f32"));

	TSharedPtr<FJsonObject> Bowl;
	if (!ReadJsonObject(BowlPath, Bowl))
	{
		UE_LOG(LogTemp, Error, TEXT("HL: cannot read %s"), *BowlPath);
		return false;
	}

	int32 HeightCount = 0;
	if (!ReadFloat32Array(HeightsPath, Heights, HeightCount))
	{
		UE_LOG(LogTemp, Error, TEXT("HL: cannot read %s"), *HeightsPath);
		return false;
	}

	// Depth is optional: without it the lake still renders, it just has no bed.
	TArray<float> RawDepth;
	int32 DepthCount = 0;
	if (ReadFloat32Array(DepthPath, RawDepth, DepthCount) && DepthCount == HeightCount)
	{
		Depths = MoveTemp(RawDepth);

		// The bed is authored, not surveyed (3DEP has no bathymetry here), so its
		// depth is a tunable: -hlmaxdepth=<m> rescales the whole basin at load.
		float WantMaxM = 0.0f;
		double BuiltMaxM = 0.0;
		const TSharedPtr<FJsonObject>* LakeObj = nullptr;
		if (FParse::Value(FCommandLine::Get(), TEXT("hlmaxdepth="), WantMaxM) && WantMaxM > 0.0f
			&& Bowl->TryGetObjectField(TEXT("lake"), LakeObj) && LakeObj && LakeObj->IsValid()
			&& (*LakeObj)->TryGetNumberField(TEXT("max_depth_m"), BuiltMaxM) && BuiltMaxM > 0.0)
		{
			const float Scale = WantMaxM / static_cast<float>(BuiltMaxM);
			for (float& D : Depths)
			{
				if (D > 0.0f) { D *= Scale; }
			}
			UE_LOG(LogTemp, Display, TEXT("HL: lake bed rescaled from %.2f m to %.2f m deep"), BuiltMaxM, WantMaxM);
		}
	}

	const TSharedPtr<FJsonObject>* ExtentObj = nullptr;
	if (Bowl->TryGetObjectField(TEXT("heightmap_extent_cm"), ExtentObj))
	{
		const double EX = (*ExtentObj)->GetNumberField(TEXT("x"));
		const double EY = (*ExtentObj)->GetNumberField(TEXT("y"));
		ExtentCm = FVector2D(EX, EY);
	}

	double SurfaceElevM = 0.0;
	Bowl->TryGetNumberField(TEXT("surface_elevation_m"), SurfaceElevM);

	double DatumM = 0.0;
	Bowl->TryGetNumberField(TEXT("vertical_datum_m"), DatumM);

	// Grid shape comes from the exporter, not from factorising the sample count.
	const TSharedPtr<FJsonObject>* GridObj = nullptr;
	if (!Bowl->TryGetObjectField(TEXT("grid"), GridObj))
	{
		UE_LOG(LogTemp, Error, TEXT("HL: bowl.json has no grid block"));
		return false;
	}
	Width = static_cast<int32>((*GridObj)->GetIntegerField(TEXT("width")));
	Height = static_cast<int32>((*GridObj)->GetIntegerField(TEXT("height")));

	if (Width * Height != Heights.Num())
	{
		UE_LOG(LogTemp, Error,
			TEXT("HL: grid %dx%d = %d does not match %d samples"),
			Width, Height, Width * Height, Heights.Num());
		return false;
	}

	UE_LOG(LogTemp, Display, TEXT("HL grid %dx%d over %.0fx%.0f cm"),
		Width, Height, ExtentCm.X, ExtentCm.Y);

	// Vertical origin: the lowest terrain point becomes Z=0 so the player spawns
	// near the origin rather than 300 m up.
	float Lo = TNumericLimits<float>::Max();
	float Hi = -TNumericLimits<float>::Max();
	for (const float H2 : Heights)
	{
		Lo = FMath::Min(Lo, H2);
		Hi = FMath::Max(Hi, H2);
	}
	MinZ = Lo * 100.0f;
	MaxZ = Hi * 100.0f;

	WaterZ = static_cast<float>(SurfaceElevM) * 100.0f - MinZ;

	// Lake centre: mean of the underwater cells.
	ShoreSpawn = FVector(ExtentCm.X * 0.5f, ExtentCm.Y * 0.5f, 0.0f);
	if (Depths.Num() == Heights.Num())
	{
		double SX = 0.0, SY = 0.0; int32 N = 0;
		for (int32 I = 0; I < Depths.Num(); ++I)
		{
			if (Depths[I] > 0.0f)
			{
				SX += (I / Width + 0.5) / Height * ExtentCm.X;
				SY += (I % Width + 0.5) / Width * ExtentCm.Y;
				++N;
			}
		}
		if (N > 0)
		{
			LakeCentre = FVector(SX / N, SY / N, WaterZ);
		}

		// Spawn on the shore: a land cell beside the water, low enough to stand at
		// the water's edge (0.2-1.5 m above it), on the south (boardwalk) side of
		// the lake (rows run north to south, so larger X), 6-16 m from the centre.
		float BestScore = TNumericLimits<float>::Max();
		for (int32 R = 1; R < Height - 1; ++R)
		{
			for (int32 C = 1; C < Width - 1; ++C)
			{
				const int32 I = R * Width + C;
				if (Depths[I] > 0.0f) { continue; }
				const bool bBesideWater = Depths[I - 1] > 0.0f || Depths[I + 1] > 0.0f
					|| Depths[I - Width] > 0.0f || Depths[I + Width] > 0.0f;
				if (!bBesideWater) { continue; }
				const float X = (R + 0.5f) / Height * ExtentCm.X;
				const float Y = (C + 0.5f) / Width * ExtentCm.Y;
				const float Above = ElevationAt(X, Y) - WaterZ;
				const float Dist = FVector2D::Distance(FVector2D(X, Y), FVector2D(LakeCentre));
				if (Above < 20.0f || Above > 150.0f || Dist < 600.0f || Dist > 1600.0f) { continue; }
				// Prefer the south side, then low ground.
				const float Score = (X > LakeCentre.X ? 0.0f : 10000.0f) + Above;
				if (Score < BestScore)
				{
					BestScore = Score;
					ShoreSpawn = FVector(X, Y, ElevationAt(X, Y) + 100.0f);
				}
			}
		}
		UE_LOG(LogTemp, Display, TEXT("HL lake centre (%.0f, %.0f); shore spawn (%.0f, %.0f, %.0f) score %.0f"),
			LakeCentre.X, LakeCentre.Y, ShoreSpawn.X, ShoreSpawn.Y, ShoreSpawn.Z, BestScore);
	}

	// Deepest water, for the spawn's look direction and for fish placement.
	if (Depths.Num() == Heights.Num())
	{
		int32 BestIdx = 0;
		for (int32 I = 1; I < Depths.Num(); ++I)
		{
			if (Depths[I] > Depths[BestIdx]) { BestIdx = I; }
		}
		const int32 BR = BestIdx / Width;
		const int32 BC = BestIdx % Width;
		DeepestPoint = FVector(
			static_cast<float>(BR) / (Height - 1) * ExtentCm.X,
			static_cast<float>(BC) / (Width - 1) * ExtentCm.Y,
			WaterZ - Depths[BestIdx] * 100.0f);
		UE_LOG(LogTemp, Display, TEXT("HL deepest water at (%.0f, %.0f) depth %.0f cm"),
			DeepestPoint.X, DeepestPoint.Y, Depths[BestIdx] * 100.0f);
	}

	UE_LOG(LogTemp, Display,
		TEXT("HL terrain loaded: %dx%d, extent %.0fx%.0f cm, water Z %.0f cm, spawn (%.0f, %.0f)"),
		Width, Height, ExtentCm.X, ExtentCm.Y, WaterZ, ShoreSpawn.X, ShoreSpawn.Y);
	return true;
}

bool UHLTerrainData::SampleGrid(
	const TArray<float>& Grid, float X, float Y, float& OutValue) const
{
	if (Grid.Num() != Width * Height || Width < 2 || Height < 2)
	{
		return false;
	}

	const float FU = X / ExtentCm.X * (Height - 1);
	const float FV = Y / ExtentCm.Y * (Width - 1);

	if (FU < 0.0f || FV < 0.0f || FU > Height - 1 || FV > Width - 1)
	{
		return false;
	}

	const int32 U0 = FMath::FloorToInt(FU);
	const int32 V0 = FMath::FloorToInt(FV);
	const int32 U1 = FMath::Min(U0 + 1, Height - 1);
	const int32 V1 = FMath::Min(V0 + 1, Width - 1);
	const float TU = FU - U0;
	const float TV = FV - V0;

	const float A = Grid[U0 * Width + V0];
	const float B = Grid[U0 * Width + V1];
	const float C = Grid[U1 * Width + V0];
	const float D = Grid[U1 * Width + V1];

	OutValue = FMath::Lerp(FMath::Lerp(A, B, TV), FMath::Lerp(C, D, TV), TU);
	return true;
}

float UHLTerrainData::ElevationAt(float X, float Y) const
{
	float M = 0.0f;
	if (!SampleGrid(Heights, X, Y, M))
	{
		return 0.0f;
	}
	// The DEM is the water's surface inside the lake; the bed is that minus depth.
	return M * 100.0f - MinZ - DepthAt(X, Y);
}

float UHLTerrainData::DepthAt(float X, float Y) const
{
	float M = 0.0f;
	if (!SampleGrid(Depths, X, Y, M))
	{
		return 0.0f;
	}
	return FMath::Max(0.0f, M * 100.0f);
}

void UHLTerrainData::BuildMesh(AActor* Owner)
{
	if (!Owner || !IsLoaded())
	{
		return;
	}

	// Use the actor's own mesh component. Creating and attaching one at runtime
	// left the actor with no valid bounds, so the whole bowl was culled.
	AHLTerrainActor* TerrainActor = Cast<AHLTerrainActor>(Owner);
	UProceduralMeshComponent* Mesh = TerrainActor ? TerrainActor->GetMesh() : nullptr;
	if (!Mesh)
	{
		UE_LOG(LogTemp, Error, TEXT("HL: BuildMesh needs an AHLTerrainActor"));
		return;
	}

	// Decimate: 1 m lidar is finer than the render needs at this scale, and a
	// full 1.7M-vertex mesh costs minutes to build. Step 2 halves each axis for a
	// quarter of the triangles while keeping every cliff band readable.
	constexpr int32 Step = 2;
	const int32 VX = (Width - 1) / Step + 1;
	const int32 VY = (Height - 1) / Step + 1;

	TArray<FVector> Verts;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Tris;

	Verts.Reserve(VX * VY);
	Tris.Reserve((VX - 1) * (VY - 1) * 6);

	const FColor Rock(96, 92, 86);
	const FColor Travertine(206, 198, 176);
	const FColor BedAlgae(104, 122, 96);

	for (int32 R = 0; R < Height; R += Step)
	{
		for (int32 C = 0; C < Width; C += Step)
		{
			const int32 I = R * Width + C;
			const float X = static_cast<float>(R) / (Height - 1) * ExtentCm.X;
			const float Y = static_cast<float>(C) / (Width - 1) * ExtentCm.Y;
			// Carve the lake bed: inside the lake the DEM is the water surface, so
			// without this the terrain was a solid lid at water level over the fish.
			const float BedDepthCm = Depths.IsValidIndex(I) ? FMath::Max(0.0f, Depths[I] * 100.0f) : 0.0f;
			const float Z = Heights[I] * 100.0f - MinZ - BedDepthCm;

			// Central-difference normal from the 1 m grid. Without real normals the
			// procedural mesh renders unlit black.
			const int32 RU = FMath::Min(R + Step, Height - 1);
			const int32 RD = FMath::Max(R - Step, 0);
			const int32 CL = FMath::Min(C + Step, Width - 1);
			const int32 CR = FMath::Max(C - Step, 0);
			const float DZdU = (Heights[RU * Width + C] - Heights[RD * Width + C]) * 100.0f;
			const float DZdV = (Heights[R * Width + CL] - Heights[R * Width + CR]) * 100.0f;
			const float DUCm = static_cast<float>(RU - RD) / (Height - 1) * ExtentCm.X;
			const float DVCm = static_cast<float>(CL - CR) / (Width - 1) * ExtentCm.Y;
			FVector N(-DZdU / FMath::Max(DUCm, 1.0f), -DZdV / FMath::Max(DVCm, 1.0f), 1.0f);
			Normals.Add(N.GetSafeNormal());

			Verts.Add(FVector(X, Y, Z));
			UVs.Add(FVector2D(static_cast<float>(C) / Width, static_cast<float>(R) / Height));

			// Colours from photos of the lake. The material reads vertex colour as
			// linear, so these are darker than they look as sRGB numbers.
			//  - bed: pale travertine with patches of bright green algae
			//  - cliffs near the water: cream travertine streaked with rust
			//  - gentle ground above the water: forest floor, moss, conifer shade
			//  - steep rock higher up: grey-brown canyon limestone
			const float AboveWater = Z - WaterZ;
			const float Slope = 1.0f - Normals.Last().Z; // 0 flat .. 1 vertical
			// Cheap deterministic noise for patches and streaks.
			auto Hash = [](int32 A, int32 B) { uint32 H = uint32(A) * 73856093u ^ uint32(B) * 19349663u; H ^= H >> 13; H *= 0x5bd1e995u; return (H & 0xffff) / 65535.0f; };
			const float Patch = 0.5f + 0.5f * FMath::Sin(X * 0.0021f + FMath::Sin(Y * 0.0017f) * 2.0f) * FMath::Cos(Y * 0.0019f);
			const float Streak = 0.5f + 0.5f * FMath::Sin(Y * 0.012f + Hash(C / 6, 0) * 6.0f);
			FLinearColor Lin;
			if (Depths.IsValidIndex(I) && Depths[I] > 0.0f)
			{
				const FLinearColor Trav(0.55f, 0.53f, 0.42f), Algae(0.16f, 0.30f, 0.10f);
				Lin = FMath::Lerp(Trav, Algae, FMath::Clamp(Patch * 1.2f - 0.55f, 0.0f, 0.6f));
			}
			else if (Slope > 0.55f && AboveWater < 2500.0f)
			{
				const FLinearColor Cream(0.58f, 0.53f, 0.42f), Rust(0.36f, 0.16f, 0.06f), Moss(0.08f, 0.17f, 0.04f);
				Lin = FMath::Lerp(Cream, Rust, FMath::Clamp(Streak * 1.3f - 0.4f, 0.0f, 0.8f));
				Lin = FMath::Lerp(Lin, Moss, Hash(R / 4, C / 4) > 0.8f ? 0.6f : 0.0f);
			}
			else if (Slope > 0.55f)
			{
				Lin = FMath::Lerp(FLinearColor(0.19f, 0.16f, 0.13f), FLinearColor(0.30f, 0.26f, 0.20f), Patch);
			}
			else if (AboveWater < 40.0f)
			{
				Lin = FLinearColor(0.50f, 0.46f, 0.34f); // wet travertine shelf at the shore
			}
			else
			{
				Lin = FMath::Lerp(FLinearColor(0.05f, 0.10f, 0.03f), FLinearColor(0.13f, 0.14f, 0.06f), Patch);
			}
			FColor Col((uint8)FMath::Clamp(Lin.R * 255.f, 0.f, 255.f), (uint8)FMath::Clamp(Lin.G * 255.f, 0.f, 255.f),
				(uint8)FMath::Clamp(Lin.B * 255.f, 0.f, 255.f), 255);
			Colors.Add(Col);
		}
	}

	// Verts were filled row-major over (R = north, C = east), so the grid is
	// VY rows of VX columns; iterate R up to VY and C up to VX.
	for (int32 Row = 0; Row < VY - 1; ++Row)
	{
		for (int32 Col = 0; Col < VX - 1; ++Col)
		{
			const int32 A = Row * VX + Col;
			const int32 B = A + 1;
			const int32 Cc = A + VX;
			const int32 D = Cc + 1;
			// Rows run along +X and columns along +Y, so this order faces up (+Z).
			// The old {A, Cc, B} order faced down: one-sided materials were culled
			// from above and the camera saw straight through the ground.
			Tris.Append({A, B, Cc, B, D, Cc});
		}
	}

	Mesh->CreateMeshSection(0, Verts, Tris, Normals, UVs, Colors, Tangents, true);
	Mesh->SetRelativeLocation(FVector::ZeroVector);

	// The section carries vertex colours, so one material covers rock, travertine
	// bed and algae. Without this the mesh renders unlit-black.
	if (FParse::Param(FCommandLine::Get(), TEXT("hlnomat")))
	{
		UE_LOG(LogTemp, Warning, TEXT("HLDBG terrain using engine default material"));
	}
	else if (UMaterialInterface* RockMat = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/Materials/M_BowlRock.M_BowlRock")))
	{
		Mesh->SetMaterial(0, RockMat);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("HL: M_BowlRock not found; terrain will be black"));
	}

	UE_LOG(LogTemp, Display, TEXT("HL: terrain mesh %d verts, %d tris"),
		Verts.Num(), Tris.Num() / 3);
}
