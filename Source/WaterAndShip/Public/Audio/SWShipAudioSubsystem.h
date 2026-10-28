#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SWShipAudioSubsystem.generated.h"

class AShip;
class UAudioComponent;
class UShipRepairPointComponent;

USTRUCT()
struct FSWLeakAudioSlot
{
	GENERATED_BODY()
	UPROPERTY() TWeakObjectPtr<UShipRepairPointComponent> Point;
	UPROPERTY() TObjectPtr<UAudioComponent> Audio;
	double ReleaseAt = 0.;
};

USTRUCT()
struct FSWShipAudioSlot
{
	GENERATED_BODY()
	UPROPERTY() TWeakObjectPtr<AShip> Ship;
	UPROPERTY() TObjectPtr<UAudioComponent> Audio;
	float Gain = 0.f;
	float Speed = 0.f;
	double ReleaseAt = 0.;
};

/** Four listener-side ship-water loops. Uses replicated motion; no sound RPCs. */
UCLASS()
class WATERANDSHIP_API USWShipAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;
	UFUNCTION(BlueprintPure, Category="Audio|Diagnostics")
	FString GetAudioState() const;
	static float MovementGain(float Speed);
	static bool IsContinuousMotion(float Distance, float Seconds);
	static bool ShouldPlayWave(float Speed, float WaterRiseSpeed, float Seconds);
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
	void UpdateAudio();
	void Release(FSWShipAudioSlot& Slot, double Now);
	void UpdateLeakAudio(const FVector& Listener, bool bEnabled);
	struct FMotion
	{
		FVector Position = FVector::ZeroVector;
		double Time = 0.; float Speed = 0.f;
		float Clearance = 0.f; double WaterTime = 0.; double NextWaveAt = 0.;
	};
	TMap<TWeakObjectPtr<AShip>, FMotion> Observations;
	UPROPERTY(Transient) TArray<FSWShipAudioSlot> Slots;
	UPROPERTY(Transient) TArray<FSWLeakAudioSlot> LeakSlots;
	FTimerHandle Timer;
};
