#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Audio/SWFoleyTypes.h"
#include "SWFoleyComponent.generated.h"

class USWFoleyAudioProfile;
class USkeletalMeshComponent;
class ACharacter;
class UPrimitiveComponent;
struct FSWFoleySoundSet;

/** Avatar-local cosmetic events; no component Tick, replication or persistent PlayerState state. */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class CLASSFEATURE_API USWFoleyComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	USWFoleyComponent();
	/** Per-character authored profile. BasePlayer assigns the shared mannequin profile by default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	TSoftObjectPtr<USWFoleyAudioProfile> ProfileOverride;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foley")
	bool bEnabled = true;
	/** The production player already supplies accepted Jump/Land boundaries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	bool bUseMovementEventsForJumpAndLand = true;
	/** NPCs without compatible foot-contact notifies use base-relative distance cadence. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	bool bUseDistanceDrivenSteps = false;
	/** NPC jump/land boundaries, independent of the foot-contact clock. Players use their accepted movement events. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	bool bPollMovementForJumpAndLand = false;
	/** Airborne non-swimming NPC entry only; queries share the listener-side Foley budget. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley")
	bool bDetectWaterWithoutSwimming = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley", meta = (ClampMin = "50.0"))
	float WalkStepDistance = 110.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foley", meta = (ClampMin = "50.0"))
	float RunStepDistance = 170.f;
	UFUNCTION(BlueprintCallable, Category = "Foley")
	bool PlayFoleyEvent(const FSWFoleyEvent& Event);
	/** Ownership/profile activation hook; requests local audio without submitting a contact. */
	bool PrepareLocalAudio();
	void HandleAnimNotify(USkeletalMeshComponent* Mesh, const FSWFoleyEvent& Event);
	void PlayJump(float HorizontalSpeed, float AgeSeconds = 0.0f);
	void PlayLanding(float ImpactSpeed, float AgeSeconds = 0.0f);

	TSoftObjectPtr<USWFoleyAudioProfile> GetEffectiveProfile() const;
	bool CanPresent(ESWFoleyGroup Group) const;
	FVector ResolveContactLocation(const USWFoleyAudioProfile& Profile, ESWFoleySide Side,
		const FSWFoleySoundSet* Definition = nullptr) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	void UpdateMovementAudio();
	FTimerHandle MovementAudioTimer;
	TWeakObjectPtr<UPrimitiveComponent> LastMovementBase;
	FVector LastMovementPosition = FVector::ZeroVector;
	float StepTravel = 0.f;
	float SwimTravel = 0.f;
	float LastVerticalSpeed = 0.f;
	double LastWaterSoundTime = -DBL_MAX;
	double LastFullWaterEntryTime = -DBL_MAX;
	double OutOfWaterSince = -1.;
	bool bFullWaterEntryArmed = true;
	double NextWaterProbeTime = 0.;
	int32 SampledNPCWaterState = 0;
	int32 LastWaterState = 0;
	bool bMovementInitialized = false;
	bool bWasGrounded = false;
	bool bNextLeft = true;
	friend class USWFoleySubsystem;
	friend class FSWFoleyLifetimeTest;
	/** Bounded family/side storage, never stored on the shared AnimNotify UObject. */
	double LastAdmission[15];
};
