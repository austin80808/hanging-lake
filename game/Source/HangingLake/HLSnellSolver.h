// Snell's-law aiming. Pure math, no renderer dependency, so it can be unit
// tested and reused by the HUD, the bow and the fish director alike.
#pragma once

#include "CoreMinimal.h"

/**
 * The physical model behind every shot in the game.
 *
 * A ray leaves the eye, hits the water plane, and refracts. The fish the player
 * sees is the *refracted image*, which sits above and nearer than the real fish.
 * So a shot aimed at the image lands short. This class computes:
 *
 *   - where the aim ray actually enters the water
 *   - the bent path through the water
 *   - the apparent (image) position of a real point under the surface
 *
 * All positions are in world space, centimetres. Water is treated as a flat
 * plane at WaterZ, which is correct for a still lake and wrong the moment there
 * are ripples; the ripple error is handled at the surface shader, not here.
 */
class HANGINGLAKE_API FHLSnellSolver
{
public:
	/** Refractive index of water. 1.333 is fresh water at ~10 °C. */
	static constexpr float WaterIOR = 1.333f;

	/** Eye ray against the water plane. False if the ray never descends to it. */
	static bool RayPlaneEntry(
		const FVector& RayOrigin,
		const FVector& RayDir,
		float WaterZ,
		FVector& OutEntryPoint);

	/**
	 * Refract a direction crossing the water plane. Either orientation of the
	 * normal works (it is turned to face the incident ray), so pass "up" for both
	 * air to water (EtaIn 1, EtaOut WaterIOR) and water to air (the reverse).
	 */
	static FVector Refract(
		const FVector& IncidentDir,
		const FVector& SurfaceNormal,
		float EtaIn,
		float EtaOut,
		bool& bOutTotalInternalReflection);

	/**
	 * Where the real, bent path between a point above the water and a point below
	 * it crosses the surface (Fermat's principle, solved exactly by bisection).
	 * False, with the point straight above BelowPos, when the two are not on
	 * opposite sides of the water.
	 */
	static bool FindSurfaceCrossing(
		const FVector& AbovePos,
		const FVector& BelowPos,
		float WaterZ,
		FVector& OutCrossing);

	/**
	 * Where to point to hit Target with something that bends like light.
	 *
	 * Returns the surface point the bent eye-to-target path passes through: aim
	 * from the eye through it and the path refracts onto Target. This is the
	 * inverse problem, solved from the geometry rather than with a fudge factor.
	 */
	static bool SolveAim(
		const FVector& EyePos,
		const FVector& TargetPos,
		float WaterZ,
		FVector& OutAimPoint);

	/**
	 * The apparent (image) position of a submerged point as seen from EyePos:
	 * where the fish *looks* like it is. It lies on the eye's line of sight,
	 * straight above the real point and shallower than it (by a factor of 1/n
	 * looking straight down, much more at grazing angles).
	 */
	static bool ApparentPosition(
		const FVector& EyePos,
		const FVector& RealPos,
		float WaterZ,
		FVector& OutApparent);

	/**
	 * How far below its apparent position the real fish sits, in cm. Smallest
	 * looking straight down (a quarter of the depth), growing toward the depth
	 * itself as the view flattens.
	 */
	static float AimOffset(
		const FVector& EyePos,
		const FVector& RealPos,
		float WaterZ);

	/**
	 * Trace a bent shot path: eye -> surface entry -> underwater direction.
	 * Used both by the bow and by the teaching overlay on the first catch.
	 */
	static bool TraceBentPath(
		const FVector& EyePos,
		const FVector& AimDir,
		float WaterZ,
		float MaxDistanceCm,
		TArray<FVector>& OutPath);
};
