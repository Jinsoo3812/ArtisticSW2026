#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerDeathCameraComponent.generated.h"

class USkeletalMeshComponent;
class USpringArmComponent;

/** Local camera presentation. Physics moves the mesh; the gameplay capsule stays untouched. */
UCLASS(ClassGroup=(Camera), meta=(BlueprintSpawnableComponent))
class CLASSFEATURE_API UPlayerDeathCameraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerDeathCameraComponent();
	void StartFollowing(USpringArmComponent* InCameraBoom, USkeletalMeshComponent* InMesh);
	void StopFollowing();
	bool IsFollowing() const { return bFollowing; }
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category="Death Camera")
	FName FocusBone = TEXT("pelvis");

	/** World-space offset keeps the pivot above the fallen body without inheriting its roll. */
	UPROPERTY(EditDefaultsOnly, Category="Death Camera")
	FVector FocusOffset = FVector(0.0f, 0.0f, 45.0f);

	/** Zero follows immediately; positive values smooth the transition and physics movement. */
	UPROPERTY(EditDefaultsOnly, Category="Death Camera", meta=(ClampMin="0.0"))
	float FollowInterpSpeed = 12.0f;

private:
	FVector GetFocusLocation() const;
	TWeakObjectPtr<USpringArmComponent> CameraBoom;
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	FTransform InitialBoomRelativeTransform;
	ETickingGroup InitialBoomTickGroup = TG_PrePhysics;
	bool bFollowing = false;
};
