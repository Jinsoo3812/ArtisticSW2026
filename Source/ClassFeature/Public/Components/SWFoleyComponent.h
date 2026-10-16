#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Audio/SWFoleyTypes.h"
#include "SWFoleyComponent.generated.h"

class USWFoleyAudioProfile;
class USkeletalMeshComponent;
class ACharacter;
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	friend class USWFoleySubsystem;
	friend class FSWFoleyLifetimeTest;
	/** Bounded family/side storage, never stored on the shared AnimNotify UObject. */
	double LastAdmission[15];
};
