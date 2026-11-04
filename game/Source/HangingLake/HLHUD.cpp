#include "HLHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "HLBow.h"
#include "HLFish.h"
#include "HLGameMode.h"
#include "HLTerrainData.h"
#include "HLWaterSurface.h"
#include "Kismet/GameplayStatics.h"

AHLHUD::AHLHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AHLHUD::HandleFishHit(AHLFish* Fish)
{
	SetLastCatch(Fish);
}

void AHLHUD::SetLastCatch(AHLFish* Fish)
{
	LastCatch = Fish;
	// The first catch explains the mechanic; later ones just acknowledge it.
	CatchOverlayTimer = bTaughtRefraction ? 3.0f : 12.0f;
	if (!bTaughtRefraction)
	{
		bTaughtRefraction = true;
	}
}

void AHLHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}
	if (StartTime <= 0.0f)
	{
		StartTime = GetWorld()->GetTimeSeconds();
	}

	AHLGameMode* GM = Cast<AHLGameMode>(UGameplayStatics::GetGameMode(this));
	AHLBow* Bow = GM ? GM->GetBow() : nullptr;

	// Bind once, when the bow first exists.
	if (Bow && !BoundBow)
	{
		BoundBow = Bow;
		Bow->OnFishHit.AddUObject(this, &AHLHUD::HandleFishHit);
	}

	DrawReticle();
	if (Bow)
	{
		DrawDrawMeter(Bow);
		DrawBentPath(Bow);
	}
	if (GM)
	{
		DrawStatus(GM);
	}

	if (CatchOverlayTimer > 0.0f)
	{
		CatchOverlayTimer -= GetWorld()->GetDeltaSeconds();
		if (CatchOverlayTimer <= 0.0f)
		{
			LastCatch = nullptr;
		}
	}
}

void AHLHUD::DrawReticle()
{
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float CX = W * 0.5f;
	const float CY = H * 0.5f;

	// A small crosshair: four ticks with a gap, so it reads at a glance without
	// covering the fish.
	FLinearColor Ink(0.92f, 0.94f, 0.96f, 0.85f);
	const float Gap = 6.0f;
	const float Len = 10.0f;

	DrawLine(CX - Gap - Len, CY, CX - Gap, CY, Ink, 1.4f);
	DrawLine(CX + Gap, CY, CX + Gap + Len, CY, Ink, 1.4f);
	DrawLine(CX, CY - Gap - Len, CX, CY - Gap, Ink, 1.4f);
	DrawLine(CX, CY + Gap, CX, CY + Gap + Len, Ink, 1.4f);

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (Font)
	{
		DrawText(TEXT("the reticle is the truth: aim here, the arrow bends"),
			Ink * 0.55f, CX - 190.0f, CY + 26.0f, Font, 1.0f);
	}
}

void AHLHUD::DrawDrawMeter(AHLBow* Bow)
{
	const float Alpha = Bow->GetDrawAlpha();
	if (Alpha <= 0.01f)
	{
		return;
	}

	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float BarW = 160.0f;
	const float BarH = 5.0f;
	const float X = W * 0.5f - BarW * 0.5f;
	const float Y = H * 0.5f + 70.0f;

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), X, Y, BarW, BarH);
	DrawRect(FLinearColor(0.75f, 0.85f, 0.95f, 0.9f), X, Y, BarW * Alpha, BarH);
}

void AHLHUD::DrawBentPath(AHLBow* Bow)
{
	const TArray<FVector>& Path = Bow->GetLastShotPath();
	const bool bTeaching = CatchOverlayTimer > 0.0f && !Path.IsEmpty();
	const bool bAiming = Bow->GetDrawAlpha() > 0.01f;
	if (!bTeaching && !bAiming)
	{
		return;
	}

	// Project the stored world path to screen and draw it as a polyline. This is
	// the shot path as the arrow actually travelled, showing the kink at the
	// surface: the visual explanation of why the fish was not where it looked.
	TArray<FVector2D> ScreenPts;
	for (const FVector& P : Path)
	{
		const FVector Proj = Project(P);
		if (Proj.Z > 0.0f)
		{
			ScreenPts.Add(FVector2D(Proj.X, Proj.Y));
		}
	}

	if (ScreenPts.Num() < 2)
	{
		return;
	}

	const FLinearColor Line = bTeaching
		? FLinearColor(1.0f, 0.78f, 0.35f, 0.95f)
		: FLinearColor(1.0f, 1.0f, 1.0f, 0.35f);

	for (int32 I = 0; I < ScreenPts.Num() - 1; ++I)
	{
		DrawLine(ScreenPts[I].X, ScreenPts[I].Y, ScreenPts[I + 1].X, ScreenPts[I + 1].Y, Line, 1.6f);
	}

	// Mark the surface crossing, which is where the bend happens.
	if (bTeaching && ScreenPts.Num() >= 3)
	{
		const FVector2D Entry = ScreenPts[1];
		DrawRect(FLinearColor(1.0f, 0.78f, 0.35f, 0.9f), Entry.X - 2.5f, Entry.Y - 2.5f, 5.0f, 5.0f);

		UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
		if (Font)
		{
			DrawText(TEXT("the arrow bends here"), Line, Entry.X + 12.0f, Entry.Y - 6.0f, Font, 1.0f);
		}
	}
}

void AHLHUD::DrawStatus(AHLGameMode* GM)
{
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font || !GM)
	{
		return;
	}

	const FLinearColor Ink(0.92f, 0.94f, 0.96f, 0.85f);
	const FLinearColor Dim(0.92f, 0.94f, 0.96f, 0.45f);

	DrawText(
		FString::Printf(TEXT("Hanging Lake   %d fish"), GM->CountAlive()),
		Ink, 28.0f, 26.0f, Font, 1.15f);

	DrawText(TEXT("hold LEFT MOUSE to draw, release to shoot"), Dim, 28.0f, 46.0f, Font, 1.0f);

	if (LastCatch)
	{
		const FString Msg = FString::Printf(
			TEXT("Landed  %s   %.0f cm long   %.0f cm down"),
			*LastCatch->SpeciesId.ToString(),
			LastCatch->GetBodyLengthCm(),
			LastCatch->GetDepthCm());

		DrawText(Msg, FLinearColor(1.0f, 0.86f, 0.5f, 1.0f), 28.0f, Canvas->ClipY - 60.0f, Font, 1.2f);

		// Explain, once, what just happened.
		if (CatchOverlayTimer > 6.0f)
		{
			DrawText(
				TEXT("it was never where it looked. light bends at the surface, so aim lower."),
				Dim, 28.0f, Canvas->ClipY - 40.0f, Font, 1.0f);
		}
	}
}
