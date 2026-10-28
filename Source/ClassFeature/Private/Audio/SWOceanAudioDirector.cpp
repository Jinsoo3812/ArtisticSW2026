#include "Audio/SWOceanAudioDirector.h"
#include "Audio/SWWorldAudio.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "SWCabinWaterCullComponent.h"
#include "TimerManager.h"
#include "WaterSurfaceQueryLibrary.h"
#include "SWNetworkWeatherActor.h"

ASWOceanAudioDirector::ASWOceanAudioDirector()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASWOceanAudioDirector::BeginPlay()
{
	Super::BeginPlay();
	if (!GetWorld()->IsGameWorld() || GetNetMode() == NM_DedicatedServer) { return; }
	// Streamed or accidentally duplicated directors must not stack ambience/mix modifiers.
	for (TActorIterator<ASWOceanAudioDirector> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->bOwnsDirectorSlot) { return; }
	}
	bOwnsDirectorSlot = true;
	for (TActorIterator<ASWNetworkWeatherActor> It(GetWorld()); It; ++It) { Weather = *It; break; }
	NextSeagullAt = GetWorld()->GetTimeSeconds() + FMath::FRandRange(12.f, 22.f);
	GetWorldTimerManager().SetTimer(AudioTimer, this, &ThisClass::UpdateAudio, 0.1f, true);
}

void ASWOceanAudioDirector::SetLoop(UAudioComponent*& Slot, uint8 Event, float Gain)
{
	const auto* Service = GetWorld()->GetSubsystem<USWWorldAudioSubsystem>();
	const auto* Set = Service ? Service->FindSound(static_cast<ESWWorldSound>(Event)) : nullptr;
	if (!Set || !Set->Sound) { return; }
	if (!Slot)
	{
		if (Gain <= 0.001f) { return; }
		Slot = NewObject<UAudioComponent>(this);
		Slot->bAutoActivate = false;
		Slot->bAutoDestroy = false;
		Slot->bAllowSpatialization = false;
		Slot->bIsUISound = false;
		Slot->SetSound(Set->Sound);
		Slot->VolumeMultiplier = 1.f;
		Slot->RegisterComponentWithWorld(GetWorld());
		Slot->FadeIn(0.15f, Gain * Set->Volume);
	}
	Slot->AdjustVolume(0.15f, Gain * Set->Volume);
}

void ASWOceanAudioDirector::UpdateAudio()
{
	auto* Service = GetWorld()->GetSubsystem<USWWorldAudioSubsystem>();
	if (!Service || !Service->GetProfile()) { return; }
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC || !PC->IsLocalController()) { return; }
	FVector Listener, Front, Right;
	PC->GetAudioListenerPosition(Listener, Front, Right);
	if (Listener.ContainsNaN()) { return; }
	float WaterZ = 0.f;
	const bool bHasWater = FWaterSurfaceQueryLibrary::QueryWaterSurface(GetWorld(), Listener, WaterZ, true);
	const bool bCabin = USWCabinWaterCullComponent::IsWorldPositionInsideAnyCabin(GetWorld(), Listener);
	static const IConsoleVariable* EnabledVar = IConsoleManager::Get().FindConsoleVariable(TEXT("SW.Audio.Enabled"));
	const bool bActive = bEnabled && (!EnabledVar || EnabledVar->GetInt() != 0);
	// Camera/listener, not the character's capsule. Hysteresis prevents wave-edge chatter.
	bListenerUnderwater = bActive && bHasWater && !bCabin &&
		(WaterZ - Listener.Z > (bListenerUnderwater ? -15.f : 15.f));
	Service->SetUnderwater(bListenerUnderwater);
	bListenerInCabin = bActive && bCabin;
	Service->SetCabin(bListenerInCabin);
	DistanceRefreshElapsed += 0.1f;
	if (DistanceRefreshElapsed >= 0.5f)
	{
		DistanceRefreshElapsed = 0.f;
		NearestShoreDistance = BIG_NUMBER;
		for (const FVector& Point : ShorePoints) { NearestShoreDistance = FMath::Min(NearestShoreDistance, float(FVector::Dist2D(Listener, Point))); }
	}
	// A short trace distinguishes ocean beneath an island from water visible to the listener.
	FHitResult Ground;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SWOceanAudio), false, PC->GetPawn());
	Params.AddIgnoredActor(PC->GetViewTarget());
	const bool bGroundAboveWater = bHasWater && GetWorld()->LineTraceSingleByChannel(Ground,
		Listener, FVector(Listener.X, Listener.Y, WaterZ - 30.f), ECC_Visibility, Params) && Ground.ImpactPoint.Z > WaterZ + 30.f;
	const float LandGain = 1.f - FMath::Clamp(NearestShoreDistance / FMath::Max(100.f, InlandAudibleDistance), 0.f, 1.f);
	const float TargetOcean = bActive && bHasWater && !bListenerUnderwater ? (bGroundAboveWater ? LandGain : 1.f) * (bCabin ? 0.35f : 1.f) : 0.f;
	const float TargetShore = bActive && !bListenerUnderwater ?
		(1.f - FMath::Clamp(NearestShoreDistance / FMath::Max(100.f, ShoreAudibleDistance), 0.f, 1.f)) * (bCabin ? 0.2f : 1.f) : 0.f;
	const float Speed = 1.f / FMath::Max(0.1f, FadeSeconds);
	CurrentOceanGain = FMath::FInterpConstantTo(CurrentOceanGain, TargetOcean, 0.1f, Speed);
	CurrentShoreGain = FMath::FInterpConstantTo(CurrentShoreGain, TargetShore, 0.1f, Speed);
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextSeagullAt)
	{
		const bool bBirds = bActive && !bListenerUnderwater && !bCabin && CurrentOceanGain > 0.1f &&
			NearestShoreDistance < 5000.f && Weather.IsValid() && Weather->AllowsCoastalWildlifeAudio();
		if (bBirds)
		{
			const float Angle = FMath::FRandRange(0.f, 2.f * PI);
			const FVector Offset(FMath::Cos(Angle) * 1800.f, FMath::Sin(Angle) * 1800.f, 900.f);
			USWWorldAudioSubsystem::PlayEvent(this, ESWWorldSound::Seagull, Listener + Offset, this);
		}
		NextSeagullAt = Now + (bBirds ? FMath::FRandRange(35.f, 65.f) : FMath::FRandRange(8.f, 15.f));
	}
	UAudioComponent* Ocean = OceanLoop;
	UAudioComponent* Shore = ShoreLoop;
	UAudioComponent* Underwater = UnderwaterLoop;
	SetLoop(Ocean, uint8(ESWWorldSound::Ocean), CurrentOceanGain);
	SetLoop(Shore, uint8(ESWWorldSound::Shore), CurrentShoreGain);
	SetLoop(Underwater, uint8(ESWWorldSound::Underwater), bListenerUnderwater ? 1.f : 0.f);
	OceanLoop = Ocean; ShoreLoop = Shore; UnderwaterLoop = Underwater;
}

void ASWOceanAudioDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(AudioTimer);
	if (bOwnsDirectorSlot)
	{
		if (auto* Service = GetWorld()->GetSubsystem<USWWorldAudioSubsystem>()) { Service->SetUnderwater(false); Service->SetCabin(false); }
	}
	for (UAudioComponent* Component : { OceanLoop.Get(), ShoreLoop.Get(), UnderwaterLoop.Get() })
	{
		if (Component) { Component->Stop(); Component->DestroyComponent(); }
	}
	Super::EndPlay(Reason);
}
