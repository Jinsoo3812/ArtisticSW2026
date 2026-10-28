#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Subsystems/WorldSubsystem.h"
#include "SWWorldAudio.generated.h"

class USoundBase;
class USoundAttenuation;
class USoundConcurrency;
class USoundMix;
struct FStreamableHandle;

UENUM(BlueprintType)
enum class ESWWorldSound : uint8
{
	CannonFire, CannonWaterImpact, WaterBombBurst, WaterEnter, WaterExit,
	SwimStroke, UnderwaterStroke, Ocean, Shore, Underwater,
	/** Reserved, silent until an appropriate recording is authored. */
	CannonSolidImpact, ShipCreak,
	ShipMovement, WaterWadeEnter, ShipWaveImpact, ShipLeak, Seagull
};

USTRUCT(BlueprintType)
struct ARTISTICSWCORE_API FSWWorldSoundSet
{
	GENERATED_BODY()
	/** A Cue may randomize several recordings without changing gameplay code. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> Sound;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundAttenuation> Attenuation;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundConcurrency> Concurrency;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float Volume = 1.0f;
};

/** Shared presentation data. Hard sound dependencies are cooked with this primary asset. */
UCLASS(BlueprintType)
class ARTISTICSWCORE_API USWWorldAudioProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TMap<ESWWorldSound, FSWWorldSoundSet> Events;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundMix> UnderwaterMix;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundMix> CabinMix;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

/** World-local, client-only playback service. Events arriving before async readiness are dropped. */
UCLASS()
class ARTISTICSWCORE_API USWWorldAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	const USWWorldAudioProfile* GetProfile() const { return Profile; }
	const FSWWorldSoundSet* FindSound(ESWWorldSound Event) const;
	UFUNCTION(BlueprintCallable, Category = "Audio", meta = (WorldContext = "WorldContextObject"))
	static bool PlayEvent(const UObject* WorldContextObject, ESWWorldSound Event,
		FVector Location, AActor* Emitter = nullptr, float Volume = 1.0f);
	void SetUnderwater(bool bEnabled);
	void SetCabin(bool bEnabled);
	uint64 Played = 0;
	uint64 NotReady = 0;
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
	UPROPERTY(Transient)
	TObjectPtr<USWWorldAudioProfile> Profile;
	TSharedPtr<FStreamableHandle> ProfileLoad;
	bool bUnderwater = false;
	bool bCabin = false;
};
