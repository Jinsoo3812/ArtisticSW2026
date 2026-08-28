#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Actor.h"
#include "GAS/SWGameplayEffectContext.h"
#include "SWPathGameplayCueNotify.generated.h"

class UDecalComponent;
class UNiagaraComponent;

/** Native moving-reference-frame implementation shared by path cue Blueprints. */
UCLASS(Abstract, Blueprintable)
class ARTISTICSWCORE_API ASWPathGameplayCueNotify : public AGameplayCueNotify_Actor
{
	GENERATED_BODY()

public:
	ASWPathGameplayCueNotify();

	virtual void Tick(float DeltaSeconds) override;
	virtual bool OnActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool WhileActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool Recycle() override;

protected:
	bool InitializePath(const FGameplayCueParameters& Parameters);
	void UpdatePathTransform();
	void ResetPath();

	UFUNCTION(BlueprintImplementableEvent, Category = "GameplayCue|Path")
	void OnPathInitialized(const FSWPathCuePayload& PathPayload);

	UFUNCTION(BlueprintImplementableEvent, Category = "GameplayCue|Path")
	void OnPathTransformUpdated(FVector StartWorld, FVector EndWorld, float CorridorRadius);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameplayCue|Path")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameplayCue|Path")
	TObjectPtr<UDecalComponent> PathDecal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GameplayCue|Path")
	TObjectPtr<UNiagaraComponent> PathNiagara;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameplayCue|Path", meta = (ClampMin = "1.0", Units = "cm"))
	float ProjectionDepth = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameplayCue|Path", meta = (ClampMin = "0.0", Units = "cm"))
	float SurfaceOffset = 2.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameplayCue|Path", meta = (ClampMin = "0.0", Units = "cm"))
	float VisualWidthOverride = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameplayCue|Path|Niagara")
	FName NiagaraStartParameter = TEXT("User.PathStart");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameplayCue|Path|Niagara")
	FName NiagaraEndParameter = TEXT("User.PathEnd");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GameplayCue|Path|Niagara")
	FName NiagaraWidthParameter = TEXT("User.PathWidth");

private:
	FSWPathCuePayload ActivePath;
	bool bPathInitialized = false;
};
