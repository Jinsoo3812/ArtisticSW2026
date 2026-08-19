#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponFeedbackComponent.generated.h"

class UNiagaraComponent;
class USceneComponent;
class UWeaponFeedbackDataAsset;

/**
 * Cosmetic-only sound and trail controller shared by player and enemy weapon actors.
 * Gameplay authority, damage, and traces intentionally remain outside this component.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class ARTISTICSWCORE_API UWeaponFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponFeedbackComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "Weapon Feedback")
	UWeaponFeedbackDataAsset* GetFeedbackData() const { return FeedbackData; }

	UFUNCTION(BlueprintCallable, Category = "Weapon Feedback")
	void SetFeedbackData(UWeaponFeedbackDataAsset* InFeedbackData);

	UFUNCTION(BlueprintPure, Category = "Weapon Feedback")
	bool HasFeedbackData() const { return FeedbackData != nullptr; }

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Weapon Feedback|Sound")
	bool PlaySwingSound(FName SoundSetName = TEXT("Default"), float VolumeMultiplier = 1.0f, float PitchMultiplier = 1.0f);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Weapon Feedback|Trail")
	bool BeginWeaponTrail();

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Weapon Feedback|Trail")
	void UpdateWeaponTrail();

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Weapon Feedback|Trail")
	void EndWeaponTrail();

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Weapon Feedback|Trail")
	void ForceStopWeaponTrail(bool bImmediate = true);

	/** Native weapon classes can reuse their trace anchors instead of authoring mesh sockets. */
	void SetTrailEndpointComponents(USceneComponent* InStartComponent, USceneComponent* InEndComponent);

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Feedback")
	TObjectPtr<UWeaponFeedbackDataAsset> FeedbackData;

private:
	USceneComponent* ResolveAttachmentComponent() const;
	bool ResolveTrailLocations(FVector& OutStartLocation, FVector& OutEndLocation) const;
	bool ShouldRunCosmetics() const;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveTrailComponent;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> TrailStartComponent;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> TrailEndComponent;

	int32 TrailRequestCount = 0;
};
