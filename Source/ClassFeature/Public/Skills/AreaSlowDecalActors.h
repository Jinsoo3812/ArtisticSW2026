#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Skills/AreaSlowSkillDataAsset.h"
#include "AreaSlowDecalActors.generated.h"

class UAreaSlowSkillDataAsset;
class UDecalComponent;
class UMaterialInterface;
class USceneComponent;

/** Owning-client-only decal that follows the player while the skill key is held. */
UCLASS(Blueprintable)
class CLASSFEATURE_API AAreaSlowTargetingDecal : public AActor
{
	GENERATED_BODY()

public:
	AAreaSlowTargetingDecal();
	virtual void Tick(float DeltaSeconds) override;

	void ConfigurePreview(AActor* InSourceActor, UAreaSlowSkillDataAsset* InSkillData);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area Slow|Preview")
	TObjectPtr<USceneComponent> PreviewRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area Slow|Preview")
	TObjectPtr<UDecalComponent> Decal;

private:
	void RefreshFromSource();

	TWeakObjectPtr<AActor> SourceActor;

	UPROPERTY(Transient)
	TObjectPtr<UAreaSlowSkillDataAsset> SkillData;
};

/** Short-lived, server-spawned replicated decal shown to every client after confirmation. */
UCLASS(Blueprintable)
class CLASSFEATURE_API AAreaSlowConfirmedDecal : public AActor
{
	GENERATED_BODY()

public:
	AAreaSlowConfirmedDecal();

	void InitializeConfirmedVisual(
		const FAreaSlowRange& InRange,
		float InProjectionDepth,
		UMaterialInterface* InMaterial,
		float InDuration);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area Slow|Confirmed")
	TObjectPtr<USceneComponent> VisualRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area Slow|Confirmed")
	TObjectPtr<UDecalComponent> Decal;

protected:
	UFUNCTION()
	void OnRep_VisualConfig();

private:
	void ApplyVisualConfig();

	UPROPERTY(ReplicatedUsing = OnRep_VisualConfig)
	FVector_NetQuantize10 ReplicatedBoxExtent = FVector::ZeroVector;

	UPROPERTY(ReplicatedUsing = OnRep_VisualConfig)
	float ReplicatedProjectionDepth = 300.0f;

	UPROPERTY(ReplicatedUsing = OnRep_VisualConfig)
	TObjectPtr<UMaterialInterface> ReplicatedMaterial;
};
