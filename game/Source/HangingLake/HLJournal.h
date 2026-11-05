// The journal: what the player has learned about each of the lake's fish, kept
// between sessions in Saved/HangingLake/Journal.json. Progress in this game is
// knowledge, not score (design spec section 2).
#pragma once

#include "CoreMinimal.h"

/** How far the player has got with one fish. Stages are reached in order. */
enum class EHLJournalStage : uint8
{
	None = 0,
	Observed = 1, // watched it hold, from a distance
	Stalked = 2,  // got within bow range without spooking it
	Landed = 3    // hit and retrieved it
};

/** The conditions at the moment a stage was earned. */
struct FHLJournalMoment
{
	FDateTime When;
	float SunElevationDeg = 0.0f;
	float DepthCm = 0.0f;
	float DistanceCm = 0.0f;
	FString Place;
};

/** Everything known about one fish. */
struct FHLJournalEntry
{
	int32 FishId = 0;
	FName Species;
	float LengthCm = 0.0f;
	EHLJournalStage Stage = EHLJournalStage::None;
	/** Indexed by stage - 1: Observed, Stalked, Landed. */
	FHLJournalMoment Moments[3];
	int32 TimesLanded = 0;
};

/**
 * The journal itself. Plain C++ (no reflection) so it can be read and tested
 * outside the engine; the game mode owns one and the HUD draws it.
 */
class HANGINGLAKE_API FHLJournal
{
public:
	/** Where the journal lives on disk. Load() and Save() use it. */
	void SetPath(const FString& InPath) { Path = InPath; }
	const FString& GetPath() const { return Path; }

	/** Read the file. False (and an empty journal) when it is missing or unreadable. */
	bool Load();

	/** Write the file, creating its folder. */
	bool Save() const;

	/**
	 * Note that the player reached Stage with this fish. Stages it skipped (landing
	 * a fish never formally observed) are filled in with the same moment. Returns
	 * true when the fish reached a stage it had not reached before; landing it again
	 * only counts another catch.
	 */
	bool Record(int32 FishId, FName Species, float LengthCm, EHLJournalStage Stage, const FHLJournalMoment& Moment);

	EHLJournalStage GetStage(int32 FishId) const;
	const FHLJournalEntry* Find(int32 FishId) const;
	const TArray<FHLJournalEntry>& GetEntries() const { return Entries; }

	/** How many fish have reached at least this stage. */
	int32 CountAtLeast(EHLJournalStage Stage) const;

	/** "observed", "stalked", "landed" (or "unseen"). */
	static FString StageName(EHLJournalStage Stage);

private:
	FString Path;
	TArray<FHLJournalEntry> Entries; // kept sorted by FishId

	FHLJournalEntry& FindOrAdd(int32 FishId);
};
