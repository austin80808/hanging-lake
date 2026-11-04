#include "HLSnellSolver.h"

bool FHLSnellSolver::RayPlaneEntry(
	const FVector& RayOrigin, const FVector& RayDir, float WaterZ, FVector& OutEntry)
{
	if (FMath::IsNearlyZero(RayDir.Z))
	{
		return false;
	}

	const float T = (WaterZ - RayOrigin.Z) / RayDir.Z;
	if (T <= 0.0f)
	{
		return false; // plane is behind the eye
	}

	OutEntry = RayOrigin + RayDir * T;
	return true;
}

FVector FHLSnellSolver::Refract(
	const FVector& IncidentDir,
	const FVector& SurfaceNormal,
	float EtaIn,
	float EtaOut,
	bool& bOutTIR)
{
	bOutTIR = false;

	const FVector I = IncidentDir.GetSafeNormal();
	const FVector N = SurfaceNormal.GetSafeNormal();
	const float Eta = EtaIn / EtaOut;

	const float CosI = FMath::Clamp(FVector::DotProduct(-I, N), -1.0f, 1.0f);
	const float K = 1.0f - Eta * Eta * (1.0f - CosI * CosI);

	if (K < 0.0f)
	{
		bOutTIR = true;
		// Mirror about the surface; the caller decides whether to use it.
		return I - 2.0f * FVector::DotProduct(I, N) * N;
	}

	return (Eta * I + (Eta * CosI - FMath::Sqrt(K)) * N).GetSafeNormal();
}

bool FHLSnellSolver::ApparentPosition(
	const FVector& EyePos, const FVector& RealPos, float WaterZ, FVector& OutApparent)
{
	// Eye must be above the water and the target below it for refraction to matter.
	if (EyePos.Z <= WaterZ || RealPos.Z >= WaterZ)
	{
		OutApparent = RealPos;
		return false;
	}

	// Direction from the real point up to the eye, then refract on the way out.
	const FVector ToEye = (EyePos - RealPos).GetSafeNormal();
	bool bTIR = false;
	const FVector AirDir = Refract(ToEye, FVector::UpVector, WaterIOR, 1.0f, bTIR);

	if (bTIR)
	{
		OutApparent = RealPos;
		return false;
	}

	// Intersect that emergent ray with the eye's viewing geometry: walk back from
	// the surface entry point along the refracted ray to the eye's depth.
	FVector Entry;
	if (!RayPlaneEntry(RealPos, ToEye, WaterZ, Entry))
	{
		OutApparent = RealPos;
		return false;
	}

	// The observer sees the point along the refracted direction, so the apparent
	// position is where that line passes closest to the eye at the target's depth.
	const FVector ViewLine = (EyePos - Entry).GetSafeNormal();
	const float Denom = ViewLine.Z;
	if (FMath::IsNearlyZero(Denom))
	{
		OutApparent = RealPos;
		return false;
	}

	const float T = (RealPos.Z - EyePos.Z) / Denom;
	OutApparent = EyePos + ViewLine * T;
	return true;
}

bool FHLSnellSolver::SolveAim(
	const FVector& EyePos, const FVector& TargetPos, float WaterZ, FVector& OutAimPoint)
{
	if (EyePos.Z <= WaterZ || TargetPos.Z >= WaterZ)
	{
		OutAimPoint = TargetPos;
		return false;
	}

	// The real path from eye to target: find the surface entry point such that
	// the refracted path bends exactly toward the target. Fermat's principle, so
	// we search the entry point along the surface instead of inverting analytically.
	FVector Entry;
	if (!RayPlaneEntry(EyePos, (TargetPos - EyePos).GetSafeNormal(), WaterZ, Entry))
	{
		OutAimPoint = TargetPos;
		return false;
	}

	FVector Best = Entry;
	float BestErr = TNumericLimits<float>::Max();

	const FVector SurfaceDir = (Entry - EyePos);
	const float Span = SurfaceDir.Size2D();
	const FVector SurfaceAxis = FVector(SurfaceDir.X, SurfaceDir.Y, 0.0f).GetSafeNormal();

	// Coarse-to-fine scan for the entry point minimising lateral miss.
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		const float Range = (Pass == 0) ? Span * 0.5f : Span * 0.05f;
		const int32 Steps = (Pass == 0) ? 32 : 16;
		const FVector Centre = Best;

		for (int32 i = -Steps; i <= Steps; ++i)
		{
			const float Offset = Range * static_cast<float>(i) / static_cast<float>(Steps);
			const FVector Candidate(
				Centre.X + SurfaceAxis.X * Offset,
				Centre.Y + SurfaceAxis.Y * Offset,
				WaterZ);

			const FVector IntoWater = (TargetPos - Candidate);
			if (IntoWater.Z >= 0.0f)
			{
				continue;
			}

			bool bTIR = false;
			const FVector Bent = Refract(IntoWater, FVector::UpVector, 1.0f, WaterIOR, bTIR);
			if (bTIR)
			{
				continue;
			}

			// The bent direction should be parallel to the real geometric line.
			const FVector Actual = (TargetPos - Candidate).GetSafeNormal();
			const float Err = (Bent - Actual).Size();

			if (Err < BestErr)
			{
				BestErr = Err;
				Best = Candidate;
			}
		}
	}

	// OutAimPoint is where the arrow must be pointed: the surface entry that
	// produces a bent path landing on the target.
	OutAimPoint = Best;
	return true;
}

float FHLSnellSolver::AimOffset(
	const FVector& EyePos, const FVector& RealPos, float WaterZ)
{
	FVector Apparent;
	if (!ApparentPosition(EyePos, RealPos, WaterZ, Apparent))
	{
		return 0.0f;
	}

	// The player aims at the image, so the miss is the depth difference between
	// the image and the fish along the view line.
	return FMath::Abs(RealPos.Z - Apparent.Z);
}

bool FHLSnellSolver::TraceBentPath(
	const FVector& EyePos,
	const FVector& AimDir,
	float WaterZ,
	float MaxDistanceCm,
	TArray<FVector>& OutPath)
{
	OutPath.Reset();

	FVector Entry;
	if (!RayPlaneEntry(EyePos, AimDir, WaterZ, Entry))
	{
		OutPath.Add(EyePos);
		OutPath.Add(EyePos + AimDir.GetSafeNormal() * MaxDistanceCm);
		return false;
	}

	if (FVector::Dist(EyePos, Entry) > MaxDistanceCm)
	{
		OutPath.Add(EyePos);
		OutPath.Add(EyePos + AimDir.GetSafeNormal() * MaxDistanceCm);
		return true;
	}

	bool bTIR = false;
	const FVector Bent = Refract(AimDir, FVector::UpVector, 1.0f, WaterIOR, bTIR);

	const float Remaining = MaxDistanceCm - FVector::Dist(EyePos, Entry);
	OutPath.Add(EyePos);
	OutPath.Add(Entry);
	OutPath.Add(Entry + Bent * Remaining);
	return true;
}
