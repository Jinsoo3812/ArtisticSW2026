#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Actor.h"
#include "SWGameplayCueNotify_LoopingFeedback.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

/** Cosmetic, locally attached persistent feedback. GAS owns replicated cue lifetime. */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "SW Gameplay Cue Looping Feedback"))
class ARTISTICSWCORE_API ASWGameplayCueNotify_LoopingFeedback : public AGameplayCueNotify_Actor
{
	GENERATED_BODY()
public:
	ASWGameplayCueNotify_LoopingFeedback();
	virtual bool OnActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool WhileActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool Recycle() override;
protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback|VFX")
	TObjectPtr<UNiagaraSystem> NiagaraSystem;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback|VFX")
	FVector LocalOffset = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Feedback|VFX")
	TObjectPtr<UNiagaraComponent> LoopNiagara;
private:
	bool StartFeedback(AActor* Target);
	void StopFeedback();
	TWeakObjectPtr<AActor> AttachedTarget;
};
