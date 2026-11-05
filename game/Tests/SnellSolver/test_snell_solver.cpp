// Tests for FHLSnellSolver, run without Unreal: the solver is compiled against
// the small Core shim in shim/. Run ./run.sh from this folder.
//
// Units are the game's: centimetres, +Z up, water plane at WATER_Z.
#include "HLSnellSolver.h"

#include <cstdio>
#include <string>

namespace
{
	constexpr float WATER_Z = 1000.0f;
	constexpr double N = FHLSnellSolver::WaterIOR;
	int Failures = 0;
	int Checks = 0;

	void Check(bool bOk, const std::string& What)
	{
		++Checks;
		if (!bOk)
		{
			++Failures;
			std::printf("FAIL  %s\n", What.c_str());
		}
	}

	void Near(double A, double B, double Tol, const std::string& What)
	{
		char Buf[160];
		std::snprintf(Buf, sizeof Buf, " (got %.6f, want %.6f +- %g)", A, B, Tol);
		Check(std::abs(A - B) <= Tol, What + Buf);
	}

	// sin of the angle between a direction and the vertical.
	double SinFromVertical(const FVector& Dir)
	{
		const FVector U = Dir.GetSafeNormal();
		return std::sqrt(U.X * U.X + U.Y * U.Y);
	}

	// Closest distance from point P to the ray Origin + t * Dir (t >= 0).
	double DistToRay(const FVector& P, const FVector& Origin, const FVector& Dir)
	{
		const FVector U = Dir.GetSafeNormal();
		const double T = std::max(0.0, FVector::DotProduct(P - Origin, U));
		return FVector::Dist(P, Origin + U * T);
	}

	// A spread of shots like the game's: boardwalk eye ~1.7 m above the water,
	// fish 0.2 to 2.2 m deep, 0 to 30 m out, in several compass directions.
	struct FCase { FVector Eye; FVector Fish; };
	std::vector<FCase> Cases()
	{
		std::vector<FCase> Out;
		const FVector Eye(0.0, 0.0, WATER_Z + 170.0);
		for (double Dist : {0.0, 1.0, 50.0, 300.0, 800.0, 1500.0, 3000.0})
		{
			for (double Depth : {20.0, 90.0, 220.0})
			{
				for (double Bearing : {0.0, 1.1, 2.5, 4.0})
				{
					Out.push_back({Eye, FVector(std::cos(Bearing) * Dist, std::sin(Bearing) * Dist, WATER_Z - Depth)});
				}
			}
		}
		return Out;
	}

	void TestRefract()
	{
		// 45 degrees in air, heading down and north.
		const FVector In = FVector(1.0, 0.0, -1.0).GetSafeNormal();
		bool bTIR = true;
		const FVector Bent = FHLSnellSolver::Refract(In, FVector::UpVector, 1.0f, FHLSnellSolver::WaterIOR, bTIR);
		Check(!bTIR, "air to water never reflects totally");
		Near(SinFromVertical(In), N * SinFromVertical(Bent), 1e-6, "Snell's law air to water");
		Check(Bent.Z < 0.0 && Bent.X > 0.0, "refracted ray keeps going down and forward");
		Check(SinFromVertical(Bent) < SinFromVertical(In), "bends toward the vertical going into water");

		// The normal's sign must not matter.
		bool bTIR2 = true;
		const FVector BentDown = FHLSnellSolver::Refract(In, -FVector::UpVector, 1.0f, FHLSnellSolver::WaterIOR, bTIR2);
		Near(FVector::Dist(Bent, BentDown), 0.0, 1e-9, "same result with the normal pointing down");

		// Reversibility: out of the water along -Bent comes back along -In.
		bool bTIR3 = true;
		const FVector Out = FHLSnellSolver::Refract(-Bent, FVector::UpVector, FHLSnellSolver::WaterIOR, 1.0f, bTIR3);
		Check(!bTIR3, "a ray that got in can get out");
		Near(FVector::Dist(Out, -In), 0.0, 1e-6, "water to air retraces air to water");

		// Straight down passes straight through.
		bool bTIR4 = true;
		const FVector Down = FHLSnellSolver::Refract(FVector(0, 0, -1), FVector::UpVector, 1.0f, FHLSnellSolver::WaterIOR, bTIR4);
		Near(FVector::Dist(Down, FVector(0, 0, -1)), 0.0, 1e-9, "vertical ray is not bent");

		// Total internal reflection past the critical angle (asin(1/1.333) = 48.6 deg).
		const double Shallow = 60.0 * 3.14159265358979 / 180.0;
		bool bTIR5 = false;
		FHLSnellSolver::Refract(FVector(std::sin(Shallow), 0.0, std::cos(Shallow)), FVector::UpVector,
			FHLSnellSolver::WaterIOR, 1.0f, bTIR5);
		Check(bTIR5, "60 degrees from inside the water reflects totally");
		const double Steep = 40.0 * 3.14159265358979 / 180.0;
		bool bTIR6 = true;
		FHLSnellSolver::Refract(FVector(std::sin(Steep), 0.0, std::cos(Steep)), FVector::UpVector,
			FHLSnellSolver::WaterIOR, 1.0f, bTIR6);
		Check(!bTIR6, "40 degrees from inside the water gets out");
	}

	void TestCrossingObeysSnell()
	{
		for (const FCase& C : Cases())
		{
			FVector X;
			Check(FHLSnellSolver::FindSurfaceCrossing(C.Eye, C.Fish, WATER_Z, X), "crossing found");
			Near(X.Z, WATER_Z, 1e-6, "crossing is on the water");
			const double D = FVector::Dist2D(C.Eye, C.Fish);
			if (D < 1e-6)
			{
				Near(FVector::Dist2D(X, C.Eye), 0.0, 1e-6, "straight down crosses straight below the eye");
				continue;
			}
			Near(SinFromVertical(X - C.Eye), N * SinFromVertical(C.Fish - X), 1e-7, "Snell's law holds at the crossing");

			// Between the straight-line entry and the point above the fish: the
			// water leg is steeper, so the crossing is farther out than a straight line.
			const double Straight = D * (C.Eye.Z - WATER_Z) / (C.Eye.Z - C.Fish.Z);
			const double S = FVector::Dist2D(C.Eye, X);
			Check(S >= Straight - 1e-6 && S <= D + 1e-6, "crossing lies past the straight-line entry");
		}

		FVector X;
		Check(!FHLSnellSolver::FindSurfaceCrossing(FVector(0, 0, WATER_Z - 10), FVector(100, 0, WATER_Z - 50), WATER_Z, X),
			"no crossing with the eye under water");
		Check(!FHLSnellSolver::FindSurfaceCrossing(FVector(0, 0, WATER_Z + 10), FVector(100, 0, WATER_Z + 5), WATER_Z, X),
			"no crossing with the target above water");
	}

	void TestAimHitsTarget()
	{
		for (const FCase& C : Cases())
		{
			FVector Aim;
			Check(FHLSnellSolver::SolveAim(C.Eye, C.Fish, WATER_Z, Aim), "aim solved");
			TArray<FVector> Path;
			FHLSnellSolver::TraceBentPath(C.Eye, (Aim - C.Eye).GetSafeNormal(), WATER_Z, 1.0e5f, Path);
			Check(Path.Num() == 3, "bent path has eye, entry and an underwater leg");
			if (Path.Num() == 3)
			{
				Near(DistToRay(C.Fish, Path[1], Path[2] - Path[1]), 0.0, 0.01,
					"a light-like shot through the aim point lands on the fish");
			}
		}
	}

	void TestApparentPosition()
	{
		// Straight down: the textbook apparent depth, depth / n.
		{
			const FVector Eye(0, 0, WATER_Z + 170.0);
			const FVector Fish(0, 0, WATER_Z - 133.3);
			FVector Img;
			Check(FHLSnellSolver::ApparentPosition(Eye, Fish, WATER_Z, Img), "image exists looking straight down");
			Near(WATER_Z - Img.Z, 133.3 / N, 1e-6, "straight down the fish looks depth / n deep");
			Near(FHLSnellSolver::AimOffset(Eye, Fish, WATER_Z), 133.3 - 133.3 / N, 1e-3, "aim offset straight down");
		}

		double LastOffset = -1.0;
		for (double Dist : {1.0, 100.0, 300.0, 600.0, 1200.0, 2400.0})
		{
			const FVector Eye(0, 0, WATER_Z + 170.0);
			const FVector Fish(Dist * 0.6, Dist * 0.8, WATER_Z - 150.0);
			FVector Img;
			Check(FHLSnellSolver::ApparentPosition(Eye, Fish, WATER_Z, Img), "image exists");
			Near(FVector::Dist2D(Img, Fish), 0.0, 1e-6, "image is straight above the fish");
			Check(Img.Z > Fish.Z && Img.Z < WATER_Z, "image is raised but still under the surface");

			// The image is on the line of sight: eye, crossing and image are collinear.
			FVector X;
			FHLSnellSolver::FindSurfaceCrossing(Eye, Fish, WATER_Z, X);
			Near(DistToRay(Img, Eye, X - Eye), 0.0, 1e-6, "image is on the eye's line of sight");

			// Aim at the image with a light-like arrow and it reaches the fish.
			TArray<FVector> Path;
			FHLSnellSolver::TraceBentPath(Eye, (Img - Eye).GetSafeNormal(), WATER_Z, 1.0e5f, Path);
			Near(DistToRay(Fish, Path[1], Path[2] - Path[1]), 0.0, 0.01, "shot at the image reaches the real fish");

			// The flatter the view, the bigger the gap between image and fish.
			const double Offset = FHLSnellSolver::AimOffset(Eye, Fish, WATER_Z);
			Check(Offset > LastOffset, "aim offset grows as the view flattens");
			LastOffset = Offset;
		}

		// A straight (non-bending) shot at the image passes over the fish: aim low.
		{
			const FVector Eye(0, 0, WATER_Z + 170.0);
			const FVector Fish(900.0, 0.0, WATER_Z - 120.0);
			FVector Img;
			FHLSnellSolver::ApparentPosition(Eye, Fish, WATER_Z, Img);
			const FVector Dir = (Img - Eye).GetSafeNormal();
			const double T = (Fish.X - Eye.X) / Dir.X;
			const double ZAtFish = Eye.Z + Dir.Z * T;
			Check(ZAtFish > Fish.Z + 30.0, "a straight shot at the image goes high: the real fish is lower");
		}

		FVector Img;
		Check(!FHLSnellSolver::ApparentPosition(FVector(0, 0, WATER_Z + 100), FVector(50, 0, WATER_Z + 10), WATER_Z, Img),
			"a point above the water has no refracted image");
		Near(FVector::Dist(Img, FVector(50, 0, WATER_Z + 10)), 0.0, 1e-9, "and is returned unchanged");
	}

	void TestRayPlaneEntry()
	{
		FVector E;
		Check(FHLSnellSolver::RayPlaneEntry(FVector(0, 0, WATER_Z + 100), FVector(1, 0, -1), WATER_Z, E), "down ray meets water");
		Near(E.X, 100.0, 1e-9, "entry 100 cm out at 45 degrees");
		Check(!FHLSnellSolver::RayPlaneEntry(FVector(0, 0, WATER_Z + 100), FVector(1, 0, 0.2), WATER_Z, E), "up ray never meets water");
		Check(!FHLSnellSolver::RayPlaneEntry(FVector(0, 0, WATER_Z + 100), FVector(1, 0, 0), WATER_Z, E), "level ray never meets water");
	}
}

int main()
{
	TestRefract();
	TestRayPlaneEntry();
	TestCrossingObeysSnell();
	TestAimHitsTarget();
	TestApparentPosition();
	std::printf("%d checks, %d failed\n", Checks, Failures);
	return Failures == 0 ? 0 : 1;
}
