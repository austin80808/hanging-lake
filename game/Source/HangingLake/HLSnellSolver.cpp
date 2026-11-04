#include "HLSnellSolver.h"

namespace
{
	/**
	 * The one geometric question behind every function here.
	 *
	 * Light (or a light-like arrow) travels from a point Above the water to a point
	 * Below it. Fermat: it crosses the surface where Snell's law holds,
	 * sin(air angle) = n * sin(water angle). In the vertical plane through both
	 * points, with S the horizontal distance from Above's foot to the crossing:
	 *
	 *   f(S) = S / sqrt(S^2 + H^2) - n * (D - S) / sqrt((D - S)^2 + Depth^2)
	 *
	 * f rises monotonically from <= 0 at S = 0 to >= 0 at S = D, so bisection always
	 * converges on the single root. Returns S, in cm. Doubles throughout: 60 halvings
	 * of a 100 m span is well under a micron.
	 */
	double CrossingDistance(double D, double H, double Depth, double N)
	{
		if (D <= 1e-9)
		{
			return 0.0;
		}
		double Lo = 0.0;
		double Hi = D;
		for (int32 I = 0; I < 60; ++I)
		{
			const double S = 0.5 * (Lo + Hi);
			const double F = S / FMath::Sqrt(S * S + H * H)
				- N * (D - S) / FMath::Sqrt((D - S) * (D - S) + Depth * Depth);
			if (F < 0.0)
			{
				Lo = S;
			}
			else
			{
				Hi = S;
			}
		}
		return 0.5 * (Lo + Hi);
	}
}

bool FHLSnellSolver::RayPlaneEntry(
	const FVector& RayOrigin, const FVector& RayDir, float WaterZ, FVector& OutEntry)
{
	if (FMath::IsNearlyZero(RayDir.Z))
	{
		return false;
	}

	const double T = (WaterZ - RayOrigin.Z) / RayDir.Z;
	if (T <= 0.0)
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
	FVector N = SurfaceNormal.GetSafeNormal();
	// The vector form below needs the normal on the incident side. Callers pass "up"
	// for both directions (air to water and water to air), so turn it to face I.
	if (FVector::DotProduct(I, N) > 0.0)
	{
		N = -N;
	}
	const double Eta = static_cast<double>(EtaIn) / static_cast<double>(EtaOut);

	const double CosI = FMath::Clamp(FVector::DotProduct(-I, N), 0.0, 1.0);
	const double K = 1.0 - Eta * Eta * (1.0 - CosI * CosI);

	if (K < 0.0)
	{
		bOutTIR = true;
		// Mirror about the surface; the caller decides whether to use it.
		return I - 2.0 * FVector::DotProduct(I, N) * N;
	}

	return (Eta * I + (Eta * CosI - FMath::Sqrt(K)) * N).GetSafeNormal();
}

bool FHLSnellSolver::FindSurfaceCrossing(
	const FVector& AbovePos, const FVector& BelowPos, float WaterZ, FVector& OutCrossing)
{
	const double H = AbovePos.Z - WaterZ;
	const double Depth = WaterZ - BelowPos.Z;
	if (H <= 0.0 || Depth <= 0.0)
	{
		OutCrossing = FVector(BelowPos.X, BelowPos.Y, WaterZ);
		return false;
	}

	const FVector Flat(BelowPos.X - AbovePos.X, BelowPos.Y - AbovePos.Y, 0.0);
	const double D = Flat.Size2D();
	const double S = CrossingDistance(D, H, Depth, WaterIOR);
	const FVector Axis = D > 1e-9 ? Flat / D : FVector::ZeroVector;
	OutCrossing = FVector(AbovePos.X + Axis.X * S, AbovePos.Y + Axis.Y * S, WaterZ);
	return true;
}

bool FHLSnellSolver::ApparentPosition(
	const FVector& EyePos, const FVector& RealPos, float WaterZ, FVector& OutApparent)
{
	FVector Crossing;
	if (!FindSurfaceCrossing(EyePos, RealPos, WaterZ, Crossing))
	{
		OutApparent = RealPos; // not looking through the surface: nothing bends
		return false;
	}

	const double H = EyePos.Z - WaterZ;
	const double Depth = WaterZ - RealPos.Z;
	const double D = FVector::Dist2D(EyePos, RealPos);
	const double S = FVector::Dist2D(EyePos, Crossing);

	// The eye sees the fish along the straight line eye -> crossing. Carry that line
	// on below the surface until it is over the real fish: that is the image, the
	// classic "apparent depth" (the fish looks raised, never moved sideways). The
	// line drops H over S, so over the remaining D - S it drops (D - S) * H / S.
	// Straight down (S -> 0) that ratio tends to Depth / n.
	const double ApparentDepth = (S > 1e-3)
		? (D - S) * H / S
		: Depth / WaterIOR;

	OutApparent = FVector(RealPos.X, RealPos.Y, WaterZ - ApparentDepth);
	return true;
}

bool FHLSnellSolver::SolveAim(
	const FVector& EyePos, const FVector& TargetPos, float WaterZ, FVector& OutAimPoint)
{
	// The bent path from the eye to the target crosses the surface at one point.
	// Aim through it and an arrow that refracts like light lands on the target.
	if (!FindSurfaceCrossing(EyePos, TargetPos, WaterZ, OutAimPoint))
	{
		OutAimPoint = TargetPos;
		return false;
	}
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

	// The image sits straight above the fish, so the error is purely vertical.
	return static_cast<float>(FMath::Abs(Apparent.Z - RealPos.Z));
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

	const double Remaining = MaxDistanceCm - FVector::Dist(EyePos, Entry);
	OutPath.Add(EyePos);
	OutPath.Add(Entry);
	OutPath.Add(Entry + Bent * Remaining);
	return true;
}
