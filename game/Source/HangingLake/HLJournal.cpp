#include "HLJournal.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr int32 JournalVersion = 1;

	EHLJournalStage ParseStage(const FString& Name)
	{
		for (int32 S = 1; S <= 3; ++S)
		{
			if (Name.Equals(FHLJournal::StageName(static_cast<EHLJournalStage>(S)), ESearchCase::IgnoreCase))
			{
				return static_cast<EHLJournalStage>(S);
			}
		}
		return EHLJournalStage::None;
	}

	TSharedRef<FJsonObject> WriteMoment(const FHLJournalMoment& M)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("when"), M.When.ToIso8601());
		O->SetNumberField(TEXT("sun_elevation_deg"), M.SunElevationDeg);
		O->SetNumberField(TEXT("depth_cm"), FMath::RoundToFloat(M.DepthCm));
		O->SetNumberField(TEXT("distance_cm"), FMath::RoundToFloat(M.DistanceCm));
		O->SetStringField(TEXT("place"), M.Place);
		return O;
	}

	void ReadMoment(const FJsonObject& O, FHLJournalMoment& M)
	{
		FString When;
		if (O.TryGetStringField(TEXT("when"), When))
		{
			FDateTime::ParseIso8601(*When, M.When);
		}
		double Number = 0.0;
		if (O.TryGetNumberField(TEXT("sun_elevation_deg"), Number)) { M.SunElevationDeg = static_cast<float>(Number); }
		if (O.TryGetNumberField(TEXT("depth_cm"), Number)) { M.DepthCm = static_cast<float>(Number); }
		if (O.TryGetNumberField(TEXT("distance_cm"), Number)) { M.DistanceCm = static_cast<float>(Number); }
		O.TryGetStringField(TEXT("place"), M.Place);
	}
}

FString FHLJournal::StageName(EHLJournalStage Stage)
{
	switch (Stage)
	{
	case EHLJournalStage::Observed: return TEXT("observed");
	case EHLJournalStage::Stalked: return TEXT("stalked");
	case EHLJournalStage::Landed: return TEXT("landed");
	default: return TEXT("unseen");
	}
}

bool FHLJournal::Load()
{
	Entries.Reset();

	FString Text;
	if (Path.IsEmpty() || !FFileHelper::LoadFileToString(Text, *Path))
	{
		return false; // no journal yet: a first visit
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	const TArray<TSharedPtr<FJsonValue>>* Fish = nullptr;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()
		|| !Root->TryGetArrayField(TEXT("fish"), Fish) || !Fish)
	{
		UE_LOG(LogTemp, Warning, TEXT("HL journal: %s is not a journal; starting a new one"), *Path);
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Fish)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Obj) || !Obj || !Obj->IsValid())
		{
			continue;
		}
		const FJsonObject& O = **Obj;

		FHLJournalEntry E;
		double Number = 0.0;
		if (O.TryGetNumberField(TEXT("id"), Number)) { E.FishId = static_cast<int32>(Number); }
		if (E.FishId <= 0)
		{
			continue;
		}
		FString Text2;
		if (O.TryGetStringField(TEXT("species"), Text2)) { E.Species = FName(*Text2); }
		if (O.TryGetNumberField(TEXT("length_cm"), Number)) { E.LengthCm = static_cast<float>(Number); }
		if (O.TryGetStringField(TEXT("stage"), Text2)) { E.Stage = ParseStage(Text2); }
		if (O.TryGetNumberField(TEXT("times_landed"), Number)) { E.TimesLanded = static_cast<int32>(Number); }
		for (int32 S = 1; S <= static_cast<int32>(E.Stage); ++S)
		{
			const TSharedPtr<FJsonObject>* Moment = nullptr;
			if (O.TryGetObjectField(StageName(static_cast<EHLJournalStage>(S)), Moment) && Moment && Moment->IsValid())
			{
				ReadMoment(**Moment, E.Moments[S - 1]);
			}
		}
		FindOrAdd(E.FishId) = E;
	}

	UE_LOG(LogTemp, Display, TEXT("HL journal: %d fish from %s"), Entries.Num(), *Path);
	return true;
}

bool FHLJournal::Save() const
{
	if (Path.IsEmpty())
	{
		return false;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), JournalVersion);

	TArray<TSharedPtr<FJsonValue>> Fish;
	for (const FHLJournalEntry& E : Entries)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("id"), E.FishId);
		O->SetStringField(TEXT("species"), E.Species.ToString());
		O->SetNumberField(TEXT("length_cm"), FMath::RoundToFloat(E.LengthCm * 10.0f) / 10.0f);
		O->SetStringField(TEXT("stage"), StageName(E.Stage));
		O->SetNumberField(TEXT("times_landed"), E.TimesLanded);
		for (int32 S = 1; S <= static_cast<int32>(E.Stage); ++S)
		{
			O->SetObjectField(StageName(static_cast<EHLJournalStage>(S)), WriteMoment(E.Moments[S - 1]));
		}
		Fish.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("fish"), Fish);

	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return FFileHelper::SaveStringToFile(Out, *Path);
}

bool FHLJournal::Record(
	int32 FishId, FName Species, float LengthCm, EHLJournalStage Stage, const FHLJournalMoment& Moment)
{
	if (FishId <= 0 || Stage == EHLJournalStage::None)
	{
		return false;
	}

	FHLJournalEntry& E = FindOrAdd(FishId);
	E.Species = Species;
	E.LengthCm = LengthCm;

	const bool bNewStage = Stage > E.Stage;
	if (bNewStage)
	{
		// Reached in order: anything skipped is filled in with this moment.
		for (int32 S = static_cast<int32>(E.Stage) + 1; S <= static_cast<int32>(Stage); ++S)
		{
			E.Moments[S - 1] = Moment;
		}
		E.Stage = Stage;
	}
	if (Stage == EHLJournalStage::Landed)
	{
		++E.TimesLanded;
	}
	return bNewStage;
}

EHLJournalStage FHLJournal::GetStage(int32 FishId) const
{
	const FHLJournalEntry* E = Find(FishId);
	return E ? E->Stage : EHLJournalStage::None;
}

const FHLJournalEntry* FHLJournal::Find(int32 FishId) const
{
	return Entries.FindByPredicate([FishId](const FHLJournalEntry& E) { return E.FishId == FishId; });
}

int32 FHLJournal::CountAtLeast(EHLJournalStage Stage) const
{
	int32 N = 0;
	for (const FHLJournalEntry& E : Entries)
	{
		if (E.Stage >= Stage)
		{
			++N;
		}
	}
	return N;
}

FHLJournalEntry& FHLJournal::FindOrAdd(int32 FishId)
{
	for (FHLJournalEntry& E : Entries)
	{
		if (E.FishId == FishId)
		{
			return E;
		}
	}
	FHLJournalEntry& Added = Entries.AddDefaulted_GetRef();
	Added.FishId = FishId;
	Entries.Sort([](const FHLJournalEntry& A, const FHLJournalEntry& B) { return A.FishId < B.FishId; });
	// Sorting moved it: find it again.
	return *Entries.FindByPredicate([FishId](const FHLJournalEntry& E) { return E.FishId == FishId; });
}
