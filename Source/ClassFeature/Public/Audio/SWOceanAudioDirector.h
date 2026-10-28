#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWOceanAudioDirector.generated.h"

class UAudioComponent;
class ASWNetworkWeatherActor;

/** One non-replicated director per ocean map. Shore positions are baked in the editor. */
UCLASS(BlueprintType)
class CLASSFEATURE_API ASWOceanAudioDirector : public AActor
{
	GENERATED_BODY()
public:
	ASWOceanAudioDirector();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ocean Audio")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Audio")
	TArray<FVector> ShorePoints;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Audio", meta = (ClampMin = "100.0", Units = "cm"))
	float ShoreAudibleDistance = 3500.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Audio", meta = (ClampMin = "100.0", Units = "cm"))
	float InlandAudibleDistance = 10000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ocean Audio", meta = (ClampMin = "0.1", Units = "s"))
	float FadeSeconds = 0.8f;
	UPROPERTY(BlueprintReadOnly, Category = "Ocean Audio|Diagnostics")
	bool bListenerUnderwater = false;
	UPROPERTY(BlueprintReadOnly, Category = "Ocean Audio|Diagnostics")
	bool bListenerInCabin = false;
	UPROPERTY(BlueprintReadOnly, Category = "Ocean Audio|Diagnostics")
	float NearestShoreDistance = BIG_NUMBER;
	UPROPERTY(BlueprintReadOnly, Category = "Ocean Audio|Diagnostics")
	float CurrentOceanGain = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Ocean Audio|Diagnostics")
	float CurrentShoreGain = 0.f;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void UpdateAudio();
	void SetLoop(UAudioComponent*& Slot, uint8 Event, float Gain);
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> OceanLoop;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> ShoreLoop;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> UnderwaterLoop;
	FTimerHandle AudioTimer;
	float DistanceRefreshElapsed = 1.f;
	bool bOwnsDirectorSlot = false;
	TWeakObjectPtr<ASWNetworkWeatherActor> Weather;
	double NextSeagullAt = 0.;
};
