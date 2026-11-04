#include "HLHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HLBow.h"
#include "HLFish.h"
#include "HLGameMode.h"
#include "HLJournal.h"
#include "HLTerrainData.h"
#include "HLWaterSurface.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const FLinearColor Ink(0.92f, 0.94f, 0.96f, 0.85f);
	const FLinearColor Dim(0.92f, 0.94f, 0.96f, 0.45f);
	const FLinearColor Amber(1.0f, 0.78f, 0.35f, 0.95f);
	const FLinearColor Gold(1.0f, 0.86f, 0.5f, 1.0f);
}

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
	if (Fish)
	{
		LastCatchReal = Fish->GetActorLocation();
		LastCatchImage = Fish->GetImageLocation();
	}
	// The first catch explains the mechanic; later ones just acknowledge it.
	CatchOverlayTimer = bTaughtRefraction ? 3.0f : 12.0f;
	bTaughtRefraction = true;
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

	DrawReticle(Bow);
	if (Bow)
	{
		DrawDrawMeter(Bow);
		DrawAimEntry(Bow);
		DrawBentPath(Bow);
	}
	if (CatchOverlayTimer > 0.0f)
	{
		DrawCatchMarkers();
	}
	if (GM)
	{
		DrawStatus(GM, Bow);
		DrawJournalNote(GM);
		if (bShowJournal)
		{
			DrawJournal(GM);
		}
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

void AHLHUD::DrawRing(const FVector2D& Centre, float Radius, const FLinearColor& Colour, float Thickness)
{
	constexpr int32 Segments = 12;
	for (int32 I = 0; I < Segments; ++I)
	{
		const float A0 = 2.0f * PI * I / Segments;
		const float A1 = 2.0f * PI * (I + 1) / Segments;
		DrawLine(Centre.X + FMath::Cos(A0) * Radius, Centre.Y + FMath::Sin(A0) * Radius,
			Centre.X + FMath::Cos(A1) * Radius, Centre.Y + FMath::Sin(A1) * Radius, Colour, Thickness);
	}
}

void AHLHUD::DrawReticle(AHLBow* Bow)
{
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float CX = W * 0.5f;
	const float CY = H * 0.5f;

	// A small crosshair: four ticks with a gap, so it reads at a glance without
	// covering the fish.
	const float Gap = 6.0f;
	const float Len = 10.0f;

	DrawLine(CX - Gap - Len, CY, CX - Gap, CY, Ink, 1.4f);
	DrawLine(CX + Gap, CY, CX + Gap + Len, CY, Ink, 1.4f);
	DrawLine(CX, CY - Gap - Len, CX, CY - Gap, Ink, 1.4f);
	DrawLine(CX, CY + Gap, CX, CY + Gap + Len, Ink, 1.4f);

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (Font)
	{
		// The fish on screen is its refracted image. Whether to aim at it or under
		// it depends on whether the arrow bends like the light did.
		const bool bBends = !Bow || Bow->bArrowBendsLikeLight;
		const TCHAR* Hint = bBends
			? TEXT("aim where the fish looks: the arrow bends like the light")
			: TEXT("the fish is deeper than it looks: aim under it");
		DrawText(Hint, Ink * 0.55f, CX - 190.0f, CY + 26.0f, Font, 1.0f);
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

void AHLHUD::DrawAimEntry(AHLBow* Bow)
{
	// While drawing, a faint ring where the aim line meets the water: where the
	// bend will happen. It shows that the path bends, never where to aim.
	if (Bow->GetDrawAlpha() <= 0.01f)
	{
		return;
	}
	FVector Entry, Dir;
	if (!Bow->GetAimWaterEntry(Entry, Dir))
	{
		return;
	}
	const FVector Proj = Project(Entry);
	if (Proj.Z <= 0.0f)
	{
		return;
	}
	DrawRing(FVector2D(Proj.X, Proj.Y), 5.0f, FLinearColor(0.75f, 0.95f, 1.0f, 0.45f), 1.0f);
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

	// Project the recorded flight to screen and draw it as a polyline: the arc,
	// the kink at the surface (if the arrow bends) and the slowdown in the water.
	// Points behind the camera are skipped without breaking the indices we need.
	TArray<FVector2D> ScreenPts;
	int32 EntryScreen = INDEX_NONE;
	for (int32 I = 0; I < Path.Num(); ++I)
	{
		const FVector Proj = Project(Path[I]);
		if (Proj.Z > 0.0f)
		{
			if (I == Bow->GetLastShotEntryIndex())
			{
				EntryScreen = ScreenPts.Num();
			}
			ScreenPts.Add(FVector2D(Proj.X, Proj.Y));
		}
	}

	if (ScreenPts.Num() < 2)
	{
		return;
	}

	const FLinearColor Line = bTeaching ? Amber : FLinearColor(1.0f, 1.0f, 1.0f, 0.35f);

	for (int32 I = 0; I < ScreenPts.Num() - 1; ++I)
	{
		DrawLine(ScreenPts[I].X, ScreenPts[I].Y, ScreenPts[I + 1].X, ScreenPts[I + 1].Y, Line, 1.6f);
	}

	// Mark the surface crossing, which is where the bend happens.
	if (bTeaching && EntryScreen != INDEX_NONE)
	{
		const FVector2D Entry = ScreenPts[EntryScreen];
		DrawRect(Amber, Entry.X - 2.5f, Entry.Y - 2.5f, 5.0f, 5.0f);

		UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
		if (Font)
		{
			const TCHAR* Label = Bow->bArrowBendsLikeLight
				? TEXT("the arrow bends here, like the light")
				: TEXT("the light bends here; the arrow does not");
			DrawText(Label, Line, Entry.X + 12.0f, Entry.Y - 6.0f, Font, 1.0f);
		}
	}
}

void AHLHUD::DrawCatchMarkers()
{
	// On the catch: where the fish looked to be and where it really was. The gap is
	// the whole mechanic.
	if (FVector::Dist(LastCatchReal, LastCatchImage) < 1.0f)
	{
		return;
	}
	const FVector Img = Project(LastCatchImage);
	const FVector Real = Project(LastCatchReal);
	if (Img.Z <= 0.0f || Real.Z <= 0.0f)
	{
		return;
	}
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;

	const FLinearColor Seen(0.75f, 0.95f, 1.0f, 0.85f);
	DrawRing(FVector2D(Img.X, Img.Y), 7.0f, Seen, 1.2f);
	DrawRing(FVector2D(Real.X, Real.Y), 7.0f, Amber, 1.6f);

	// Dashed line from image down to the fish.
	const FVector2D A(Img.X, Img.Y);
	const FVector2D B(Real.X, Real.Y);
	const float Len = FVector2D::Distance(A, B);
	for (float T = 0.0f; T < Len; T += 8.0f)
	{
		const FVector2D P0 = A + (B - A) * (T / Len);
		const FVector2D P1 = A + (B - A) * (FMath::Min(T + 4.0f, Len) / Len);
		DrawLine(P0.X, P0.Y, P1.X, P1.Y, Dim, 1.0f);
	}

	if (Font)
	{
		DrawText(TEXT("it looked here"), Seen, Img.X + 12.0f, Img.Y - 6.0f, Font, 1.0f);
		DrawText(TEXT("it was here"), Amber, Real.X + 12.0f, Real.Y - 6.0f, Font, 1.0f);
	}
}

void AHLHUD::DrawStatus(AHLGameMode* GM, AHLBow* Bow)
{
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font || !GM)
	{
		return;
	}

	DrawText(
		FString::Printf(TEXT("Hanging Lake   %d fish"), GM->CountAlive()),
		Ink, 28.0f, 26.0f, Font, 1.15f);

	DrawText(TEXT("hold LEFT MOUSE to draw, release to shoot    SHIFT creep    J journal"),
		Dim, 28.0f, 46.0f, Font, 1.0f);

	if (LastCatch)
	{
		const FString Msg = FString::Printf(
			TEXT("Landed  No. %d  %s   %.0f cm long   %.0f cm down"),
			LastCatch->FishId,
			*LastCatch->SpeciesId.ToString(),
			LastCatch->GetBodyLengthCm(),
			LastCatch->GetDepthCm());

		DrawText(Msg, Gold, 28.0f, Canvas->ClipY - 60.0f, Font, 1.2f);

		// Explain, once, what just happened.
		if (CatchOverlayTimer > 6.0f)
		{
			const bool bBends = !Bow || Bow->bArrowBendsLikeLight;
			DrawText(bBends
					? TEXT("it was deeper than it looked. the light bent at the surface, and so did your arrow.")
					: TEXT("it was deeper than it looked. light bends at the surface, arrows don't: aim under it."),
				Dim, 28.0f, Canvas->ClipY - 40.0f, Font, 1.0f);
		}
	}
}

void AHLHUD::DrawJournalNote(AHLGameMode* GM)
{
	FString Note;
	float Age = 0.0f;
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font || !GM->GetJournalNote(Note, Age) || Age > 5.0f)
	{
		return;
	}
	const float Fade = FMath::Clamp(5.0f - Age, 0.0f, 1.0f);
	DrawText(TEXT("journal  ") + Note, Gold * FLinearColor(1.0f, 1.0f, 1.0f, Fade), 28.0f, 70.0f, Font, 1.05f);
}

void AHLHUD::DrawJournal(AHLGameMode* GM)
{
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}
	const FHLJournal& J = GM->GetJournal();
	const TArray<AHLFish*>& Fish = GM->GetFish();

	const float RowH = 17.0f;
	const float PanelW = 470.0f;
	const float PanelH = 70.0f + RowH * Fish.Num();
	const float X = Canvas->ClipX - PanelW - 28.0f;
	const float Y = 26.0f;
	DrawRect(FLinearColor(0.02f, 0.04f, 0.05f, 0.72f), X, Y, PanelW, PanelH);

	DrawText(TEXT("Journal: the cutthroat of Hanging Lake"), Ink, X + 14.0f, Y + 10.0f, Font, 1.15f);
	DrawText(FString::Printf(TEXT("observed %d    stalked %d    landed %d    of %d"),
			J.CountAtLeast(EHLJournalStage::Observed), J.CountAtLeast(EHLJournalStage::Stalked),
			J.CountAtLeast(EHLJournalStage::Landed), Fish.Num()),
		Dim, X + 14.0f, Y + 30.0f, Font, 1.0f);

	float RowY = Y + 54.0f;
	for (const AHLFish* F : Fish)
	{
		if (!F)
		{
			continue;
		}
		const FHLJournalEntry* E = J.Find(F->FishId);
		const EHLJournalStage Stage = E ? E->Stage : EHLJournalStage::None;
		const FLinearColor RowInk = Stage == EHLJournalStage::None ? Dim : Ink;

		DrawText(FString::Printf(TEXT("No. %d"), F->FishId), RowInk, X + 14.0f, RowY, Font, 1.0f);

		// Three pips: observed, stalked, landed.
		for (int32 S = 1; S <= 3; ++S)
		{
			const bool bHave = static_cast<int32>(Stage) >= S;
			DrawRect(bHave ? Gold : FLinearColor(1.0f, 1.0f, 1.0f, 0.15f), X + 70.0f + (S - 1) * 11.0f, RowY + 4.0f, 7.0f, 7.0f);
		}

		if (E && Stage != EHLJournalStage::None)
		{
			const FHLJournalMoment& M = E->Moments[static_cast<int32>(Stage) - 1];
			FString Line = FString::Printf(TEXT("%.0f cm   %s %s, %.1f m down"),
				E->LengthCm, *FHLJournal::StageName(Stage), *M.Place, M.DepthCm / 100.0f);
			if (E->TimesLanded > 1)
			{
				Line += FString::Printf(TEXT("   x%d"), E->TimesLanded);
			}
			if (F->bLanded)
			{
				Line += TEXT("   (today)");
			}
			DrawText(Line, RowInk, X + 110.0f, RowY, Font, 1.0f);
		}
		else
		{
			DrawText(TEXT("not yet seen"), Dim, X + 110.0f, RowY, Font, 1.0f);
		}
		RowY += RowH;
	}
}
