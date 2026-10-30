#include "Audio/SWShipAudioSubsystem.h"
#include "Audio/SWWorldAudio.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Repair/ShipRepairPointComponent.h"
#include "Ship.h"
#include "Sound/SoundBase.h"

void USWShipAudioSubsystem::UpdateLeakAudio(const FVector& Listener, bool bEnabled)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const auto* Service = GetWorld()->GetSubsystem<USWWorldAudioSubsystem>();
	const auto* Set = Service ? Service->FindSound(ESWWorldSound::ShipLeak) : nullptr;
	struct FCandidate { UShipRepairPointComponent* Point; double Distance; };
	TArray<FCandidate, TInlineAllocator<64>> Candidates;
	if (bEnabled && Set && Set->Sound && Set->Sound->IsLooping())
	{
		for (TActorIterator<AShip> It(GetWorld()); It; ++It)
		{
			AShip* Ship = *It;
			if (Ship->IsHidden() || !Ship->IsShipRuntimePhysicsEnabled() || FVector::DistSquared(Ship->GetActorLocation(), Listener) > FMath::Square(6000.f)) { continue; }
			TInlineComponentArray<UShipRepairPointComponent*> Points(Ship);
			for (auto* Point : Points)
			{
				const double Distance = FVector::DistSquared(Point->GetComponentLocation(), Listener);
				if (Candidates.Num() < 64 && Point->IsRegistered() && Point->IsLeakActive() && Distance < FMath::Square(2500.f)) { Candidates.Add({Point, Distance}); }
			}
		}
	}
	Candidates.Sort([](const auto& A, const auto& B) { return A.Distance < B.Distance; });
	Candidates.SetNum(FMath::Min(Candidates.Num(), LeakSlots.Num()), EAllowShrinking::No);
	for (auto& Slot : LeakSlots)
	{
		const bool bSelected = Candidates.ContainsByPredicate([&Slot](const auto& C) { return Slot.Point == C.Point; });
		if (Slot.ReleaseAt == 0. && Slot.Point.IsValid() && !bSelected)
		{
			Slot.ReleaseAt = Now + 0.4;
			if (Slot.Audio) { Slot.Audio->FadeOut(0.3f, 0.f); }
		}
		if (Slot.ReleaseAt > 0. && Now >= Slot.ReleaseAt)
		{
			if (Slot.Audio) { Slot.Audio->Stop(); }
			Slot.Point.Reset(); Slot.ReleaseAt = 0.;
		}
		if (!Slot.Point.IsValid() && Slot.Audio && Slot.Audio->IsPlaying()) { Slot.Audio->Stop(); }
	}
	for (const auto& Candidate : Candidates)
	{
		auto* Slot = LeakSlots.FindByPredicate([&Candidate](const auto& S) { return S.Point == Candidate.Point && S.ReleaseAt == 0.; });
		if (!Slot) { Slot = LeakSlots.FindByPredicate([](const auto& S) { return !S.Point.IsValid() && S.ReleaseAt == 0.; }); }
		if (!Slot) { continue; }
		if (!Slot->Audio)
		{
			Slot->Audio = NewObject<UAudioComponent>(this);
			Slot->Audio->bAutoActivate = false; Slot->Audio->bAutoDestroy = false;
			Slot->Audio->bAllowSpatialization = true; Slot->Audio->SetSound(Set->Sound);
			Slot->Audio->AttenuationSettings = Set->Attenuation;
			if (Set->Concurrency) { Slot->Audio->ConcurrencySet.Add(Set->Concurrency); }
			Slot->Audio->RegisterComponentWithWorld(GetWorld());
		}
		Slot->Point = Candidate.Point;
		Slot->Audio->SetWorldLocation(Candidate.Point->GetComponentLocation());
		if (!Slot->Audio->IsPlaying()) { Slot->Audio->FadeIn(0.25f, Set->Volume); }
	}
}
