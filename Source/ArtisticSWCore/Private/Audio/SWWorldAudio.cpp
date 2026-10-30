#include "Audio/SWWorldAudio.h"

#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundMix.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWWorldAudio, Log, All);
namespace
{
	TAutoConsoleVariable<int32> AudioEnabled(TEXT("SW.Audio.Enabled"), 1, TEXT("Enable project world sound events."));
	TAutoConsoleVariable<int32> AudioDebug(TEXT("SW.Audio.DebugEvents"), 0, TEXT("Log world audio submissions, not hardware voice counts."));
	FAutoConsoleCommandWithWorld AudioStatus(TEXT("SW.Audio.Status"), TEXT("Report world audio readiness and submissions."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (const auto* S = World ? World->GetSubsystem<USWWorldAudioSubsystem>() : nullptr)
			{
				UE_LOG(LogSWWorldAudio, Display, TEXT("World=%s Ready=%d Played=%llu NotReady=%llu"),
					*World->GetName(), S->GetProfile() != nullptr, S->Played, S->NotReady);
			}
		}));
}

bool USWWorldAudioSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

bool USWWorldAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->GetNetMode() != NM_DedicatedServer;
}

void USWWorldAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() == NM_DedicatedServer) { return; }
	const FSoftObjectPath Path(TEXT("/Game/Audio/World/DA_WorldAudio.DA_WorldAudio"));
	ProfileLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,
		FStreamableDelegate::CreateWeakLambda(this, [this, Path]
		{
			if (GetWorld() && !GetWorld()->bIsTearingDown)
			{
				Profile = Cast<USWWorldAudioProfile>(Path.ResolveObject());
				if (!Profile) { UE_LOG(LogSWWorldAudio, Warning, TEXT("World audio profile unavailable: %s"), *Path.ToString()); }
			}
		}));
}

const FSWWorldSoundSet* USWWorldAudioSubsystem::FindSound(ESWWorldSound Event) const
{
	return Profile ? Profile->Events.Find(Event) : nullptr;
}

bool USWWorldAudioSubsystem::PlayEvent(const UObject* Context, ESWWorldSound Event,
	FVector Location, AActor* Emitter, float Volume)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || !World->IsGameWorld() || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer ||
		!AudioEnabled.GetValueOnGameThread() || Location.ContainsNaN() || !FMath::IsFinite(Volume) || Volume <= 0.f) { return false; }
	auto* Service = World->GetSubsystem<USWWorldAudioSubsystem>();
	if (!Service) { return false; }
	if (!Service->Profile) { ++Service->NotReady; return false; }
	const auto* Set = Service->FindSound(Event);
	if (!Set || !Set->Sound || Set->Sound->IsLooping()) { return false; }
	UGameplayStatics::PlaySoundAtLocation(World, Set->Sound, Location, FRotator::ZeroRotator,
		FMath::Clamp(Volume, 0.f, 4.f) * FMath::Clamp(Set->Volume, 0.f, 4.f), 1.f, 0.f,
		Set->Attenuation, Set->Concurrency, Emitter);
	++Service->Played;
	if (AudioDebug.GetValueOnGameThread())
	{
		UE_LOG(LogSWWorldAudio, Display, TEXT("Submit World=%s NetMode=%d Event=%d Actor=%s Location=%s Sound=%s"),
			*World->GetName(), int32(World->GetNetMode()), int32(Event), *GetNameSafe(Emitter), *Location.ToCompactString(), *Set->Sound->GetPathName());
	}
	return true;
}

void USWWorldAudioSubsystem::SetUnderwater(bool bEnabled)
{
	if (bUnderwater == bEnabled || !Profile || !Profile->UnderwaterMix) { return; }
	bUnderwater = bEnabled;
	if (bEnabled) { UGameplayStatics::PushSoundMixModifier(this, Profile->UnderwaterMix); }
	else { UGameplayStatics::PopSoundMixModifier(this, Profile->UnderwaterMix); }
}

void USWWorldAudioSubsystem::SetCabin(bool bEnabled)
{
	if (bCabin == bEnabled || !Profile || !Profile->CabinMix) { return; }
	bCabin = bEnabled;
	if (bEnabled) { UGameplayStatics::PushSoundMixModifier(this, Profile->CabinMix); }
	else { UGameplayStatics::PopSoundMixModifier(this, Profile->CabinMix); }
}

void USWWorldAudioSubsystem::Deinitialize()
{
	SetUnderwater(false);
	SetCabin(false);
	if (ProfileLoad) { ProfileLoad->CancelHandle(); ProfileLoad.Reset(); }
	Profile = nullptr;
	Super::Deinitialize();
}

#if WITH_EDITOR
#include "Misc/DataValidation.h"
EDataValidationResult USWWorldAudioProfile::IsDataValid(FDataValidationContext& Context) const
{
	bool bError = Super::IsDataValid(Context) == EDataValidationResult::Invalid;
	for (const auto& Pair : Events)
	{
		const bool bLoop = Pair.Key == ESWWorldSound::Ocean || Pair.Key == ESWWorldSound::Shore || Pair.Key == ESWWorldSound::Underwater || Pair.Key == ESWWorldSound::ShipMovement || Pair.Key == ESWWorldSound::ShipLeak;
		if (!Pair.Value.Sound || !FMath::IsFinite(Pair.Value.Volume) || Pair.Value.Volume < 0.f || Pair.Value.Volume > 4.f ||
			Pair.Value.Sound->IsLooping() != bLoop)
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("Invalid sound/loop/volume for world audio event %d"), int32(Pair.Key))));
			bError = true;
		}
	}
	return bError ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}
#endif
