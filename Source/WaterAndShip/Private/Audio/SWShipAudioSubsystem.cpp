#include "Audio/SWShipAudioSubsystem.h"

#include "Audio/SWWorldAudio.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Ship.h"
#include "Repair/ShipRepairPointComponent.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "WaterSurfaceQueryLibrary.h"

namespace { constexpr float Range = 8000.f; constexpr int32 VoiceLimit = 4; }

bool USWShipAudioSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

bool USWShipAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->GetNetMode() != NM_DedicatedServer;
}

void USWShipAudioSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (World.GetNetMode() == NM_DedicatedServer) { return; }
	Slots.SetNum(VoiceLimit);
	LeakSlots.SetNum(3);
	World.GetTimerManager().SetTimer(Timer, this, &ThisClass::UpdateAudio, 0.25f, true);
}

float USWShipAudioSubsystem::MovementGain(float Speed)
{
	return FMath::IsFinite(Speed) ? FMath::Sqrt(FMath::Clamp((Speed - 20.f) / 580.f, 0.f, 1.f)) : 0.f;
}

bool USWShipAudioSubsystem::IsContinuousMotion(float Distance, float Seconds)
{
	return FMath::IsFinite(Distance) && FMath::IsFinite(Seconds) && Seconds > 0.f && Seconds <= 1.f && Distance <= 2000.f;
}

bool USWShipAudioSubsystem::ShouldPlayWave(float Speed, float WaterRiseSpeed, float Seconds)
{
	return FMath::IsFinite(Speed) && FMath::IsFinite(WaterRiseSpeed) && FMath::IsFinite(Seconds)
		&& Seconds > 0.f && Seconds <= 0.75f && Speed > 60.f && WaterRiseSpeed > 35.f;
}

void USWShipAudioSubsystem::Release(FSWShipAudioSlot& Slot, double Now)
{
	if (!Slot.Ship.IsValid() && Slot.ReleaseAt == 0. && (!Slot.Audio || !Slot.Audio->IsPlaying())) { return; }
	if (Slot.ReleaseAt == 0.)
	{
		Slot.ReleaseAt = Now + 0.5;
		Slot.Gain = 0.f;
		if (Slot.Audio) { Slot.Audio->FadeOut(0.45f, 0.f); }
	}
	if (Now >= Slot.ReleaseAt)
	{
		if (Slot.Audio) { Slot.Audio->Stop(); } // Reuse this bounded component slot.
		Slot.Ship.Reset(); Slot.ReleaseAt = 0.; Slot.Speed = 0.f;
	}
}

void USWShipAudioSubsystem::UpdateAudio()
{
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown) { return; }
	const double Now = World->GetTimeSeconds();
	const auto* Service = World->GetSubsystem<USWWorldAudioSubsystem>();
	const auto* Set = Service ? Service->FindSound(ESWWorldSound::ShipMovement) : nullptr;
	auto* PC = UGameplayStatics::GetPlayerController(World, 0);
	static const auto* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("SW.Audio.Enabled"));
	if (!PC || !PC->IsLocalController() || !Set || !Set->Sound || !Set->Sound->IsLooping() || (Enabled && Enabled->GetInt() == 0))
	{
		for (auto& Slot : Slots) { Release(Slot, Now); }
		UpdateLeakAudio(FVector::ZeroVector, false);
		Observations.Reset();
		return;
	}
	FVector Listener, Front, Right;
	PC->GetAudioListenerPosition(Listener, Front, Right);
	FVector AttenuationListener;
	if (PC->GetAudioListenerAttenuationOverridePosition(AttenuationListener)) { Listener = AttenuationListener; }
	if (Listener.ContainsNaN()) { return; }
	UpdateLeakAudio(Listener, true);
	struct FCandidate { AShip* Ship; float Distance; float Speed; float SurfaceZ = 0.f; };
	TArray<FCandidate, TInlineAllocator<64>> Candidates;
	TSet<TWeakObjectPtr<AShip>> Seen;
	for (TActorIterator<AShip> It(World); It; ++It)
	{
		AShip* Ship = *It;
		if (Ship->IsHidden() || Ship->IsSinking() || !Ship->IsShipRuntimePhysicsEnabled()) { continue; }
		const FVector Position = Ship->GetActorLocation();
		const float Distance = FVector::DistSquared(Position, Listener);
		if (Distance > FMath::Square(Range) || Seen.Num() >= 64 || Position.ContainsNaN()) { continue; }
		Seen.Add(Ship);
		FMotion* Existing = Observations.Find(Ship);
		if (!Existing) { Observations.Add(Ship, FMotion{Position, Now, 0.f}); continue; }
		const float Seconds = float(Now - Existing->Time);
		const float Travel = FVector::Dist2D(Position, Existing->Position);
		const bool bContinuous = IsContinuousMotion(Travel, Seconds);
		if (!bContinuous) { Existing->WaterTime = 0.; }
		Existing->Speed = bContinuous ? FMath::Lerp(Existing->Speed, Travel / Seconds, 1.f - FMath::Exp(-Seconds / 0.3f)) : 0.f;
		Existing->Time = Now; Existing->Position = Position;
		const bool bPlaying = Slots.ContainsByPredicate([Ship](const auto& S) { return S.Ship == Ship && S.ReleaseAt == 0.; });
		if (bContinuous && Existing->Speed > (bPlaying ? 20.f : 40.f)) { Candidates.Add({Ship, Distance, Existing->Speed}); }
	}
	for (auto It = Observations.CreateIterator(); It; ++It) { if (!Seen.Contains(It.Key())) { It.RemoveCurrent(); } }
	Candidates.Sort([](const auto& A, const auto& B) { return A.Distance < B.Distance; });
	TArray<FCandidate, TInlineAllocator<4>> Selected;
	// Limit water probes as well as voices when many pooled ships are nearby.
	for (int32 I = 0; I < FMath::Min(Candidates.Num(), 8) && Selected.Num() < VoiceLimit; ++I)
	{
		float SurfaceZ = 0.f;
		const FVector Position = Candidates[I].Ship->GetActorLocation();
		const FVector Bow = Position + Candidates[I].Ship->GetActorForwardVector() * 800.f;
		if (FWaterSurfaceQueryLibrary::QueryWaterSurface(World, Bow, SurfaceZ, true) && FMath::Abs(Position.Z - SurfaceZ) < 900.f)
		{
			Candidates[I].SurfaceZ = SurfaceZ;
			Selected.Add(Candidates[I]);
		}
	}
	for (auto& Slot : Slots)
	{
		if (Slot.ReleaseAt > 0. || !Selected.ContainsByPredicate([&Slot](const auto& C) { return Slot.Ship == C.Ship; })) { Release(Slot, Now); }
	}
	for (const auto& Candidate : Selected)
	{
		FSWShipAudioSlot* Slot = Slots.FindByPredicate([&](const auto& S) { return S.Ship == Candidate.Ship && S.ReleaseAt == 0.; });
		if (!Slot) { Slot = Slots.FindByPredicate([](const auto& S) { return !S.Ship.IsValid() && S.ReleaseAt == 0.; }); }
		if (!Slot) { continue; }
		const bool bNew = Slot->Ship != Candidate.Ship;
		Slot->Ship = Candidate.Ship; Slot->Speed = Candidate.Speed; Slot->Gain = MovementGain(Candidate.Speed);
		// A wave rising against the bow is separate from the continuous wake.
		// Reuse the existing bounded water query, with no additional sound RPC.
		FMotion& Motion = Observations.FindChecked(Candidate.Ship);
		const float Clearance = Candidate.SurfaceZ - Candidate.Ship->GetActorLocation().Z;
		const float WaterSeconds = float(Now - Motion.WaterTime);
		if (Motion.WaterTime > 0. && ShouldPlayWave(Candidate.Speed, (Clearance - Motion.Clearance) / FMath::Max(WaterSeconds, 0.001f), WaterSeconds) && Now >= Motion.NextWaveAt)
		{
			USWWorldAudioSubsystem::PlayEvent(this, ESWWorldSound::ShipWaveImpact,
				Candidate.Ship->GetActorLocation() + Candidate.Ship->GetActorForwardVector() * 800.f,
				Candidate.Ship, 0.5f + Slot->Gain * 0.5f);
			Motion.NextWaveAt = Now + FMath::FRandRange(4.f, 6.f);
		}
		Motion.Clearance = Clearance; Motion.WaterTime = Now;
		if (!Slot->Audio)
		{
			Slot->Audio = NewObject<UAudioComponent>(this);
			Slot->Audio->bAutoActivate = false; Slot->Audio->bAutoDestroy = false;
			Slot->Audio->bAllowSpatialization = true;
			Slot->Audio->SetSound(Set->Sound); Slot->Audio->AttenuationSettings = Set->Attenuation;
			if (Set->Concurrency) { Slot->Audio->ConcurrencySet.Add(Set->Concurrency); }
			Slot->Audio->RegisterComponentWithWorld(World);
		}
		Slot->Audio->SetWorldLocation(Candidate.Ship->GetActorLocation());
		Slot->Audio->SetPitchMultiplier(FMath::Lerp(0.85f, 1.12f, Slot->Gain));
		if (bNew || !Slot->Audio->IsPlaying()) { Slot->Audio->FadeIn(0.3f, Slot->Gain * Set->Volume); }
		else { Slot->Audio->AdjustVolume(0.3f, Slot->Gain * Set->Volume); }
	}
}

FString USWShipAudioSubsystem::GetAudioState() const
{
	FString Text = FString::Printf(TEXT("World=%s NetMode=%d Observed=%d"), *GetNameSafe(GetWorld()), GetWorld() ? int32(GetWorld()->GetNetMode()) : -1, Observations.Num());
	for (const auto& Slot : Slots)
		if (Slot.Ship.IsValid()) Text += FString::Printf(TEXT(" | Ship=%s Enemy=%d Speed=%.1f Gain=%.3f Playing=%d Releasing=%d"), *Slot.Ship->GetName(), Slot.Ship->IsEnemyShipForEffects(), Slot.Speed, Slot.Gain, Slot.Audio && Slot.Audio->IsPlaying(), Slot.ReleaseAt > 0.);
	for (const auto& Slot : LeakSlots)
		if (Slot.Point.IsValid()) Text += FString::Printf(TEXT(" | Leak=%s Playing=%d Releasing=%d"), *Slot.Point->GetName(), Slot.Audio && Slot.Audio->IsPlaying(), Slot.ReleaseAt > 0.);
	return Text;
}

void USWShipAudioSubsystem::Deinitialize()
{
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(Timer); }
	for (auto& Slot : Slots) { if (Slot.Audio) { Slot.Audio->Stop(); Slot.Audio->DestroyComponent(); } }
	Slots.Reset(); Observations.Reset();
	for (auto& Slot : LeakSlots) { if (Slot.Audio) { Slot.Audio->Stop(); Slot.Audio->DestroyComponent(); } }
	LeakSlots.Reset();
	Super::Deinitialize();
}
