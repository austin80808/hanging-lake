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

	/** Refract a direction crossing the water plane, given a surface normal. */
	static FVector Refract(
		const FVector& IncidentDir,
		const FVector& SurfaceNormal,
		float EtaIn,
		float EtaOut,
		bool& bOutTotalInternalReflection);

	/**
	 * Where the player must actually aim to hit Target.
	 *
	 * Returns the submerged aim point: the point on the target's depth plane
	 * that, when refracted up through the surface, reaches the eye along the
	 * same line as the target would. In other words, aim here and the arrow
	 * lands on Target.
	 *
	 * Note this is the inverse problem, and it is solved by finding the actual
	 * bent path from eye to target rather than by applying a fudge factor.
	 */
	static bool SolveAim(
		const FVector& EyePos,
		const FVector& TargetPos,
		float WaterZ,
		FVector& OutAimPoint);

	/**
	 * The apparent (image) position of a submerged point as seen from EyePos.
	 * This is where the fish *looks* like it is; the gap between this and the
	 * real position is the difficulty of the game.
	 */
	static bool ApparentPosition(
		const FVector& EyePos,
		const FVector& RealPos,
		float WaterZ,
		FVector& OutApparent);

	/**
	 * How far below the fish's apparent position the real fish sits, in cm.
	 * Zero when the eye is directly above the fish.
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
