#include "GameplayCue/SWGameplayCueNotify_LoopingFeedback.h"

#include "Components/SceneComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

ASWGameplayCueNotify_LoopingFeedback::ASWGameplayCueNotify_LoopingFeedback()
{
	bAutoDestroyOnRemove = true;
	bAutoAttachToOwner = false;
	bUniqueInstancePerInstigator = true;
	PrimaryActorTick.bCanEverTick = false;
	LoopNiagara = CreateDefaultSubobject<UNiagaraComponent>(TEXT("LoopNiagara"));
	SetRootComponent(LoopNiagara);
	LoopNiagara->SetAutoActivate(false);
}

bool ASWGameplayCueNotify_LoopingFeedback::StartFeedback(AActor* Target)
{
	if (GetNetMode() == NM_DedicatedServer) return true;
	if (!IsValid(Target) || !Target->GetRootComponent()) { StopFeedback(); return false; }
	if (AttachedTarget.Get() != Target)
	{
		StopFeedback();
		AttachedTarget = Target;
		AttachToComponent(Target->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		SetActorRelativeLocation(LocalOffset);
	}
	LoopNiagara->SetVisibility(true);
	if (NiagaraSystem && !LoopNiagara->IsActive())
	{
		LoopNiagara->SetAsset(NiagaraSystem);
		LoopNiagara->Activate(true);
	}
	return true;
}

bool ASWGameplayCueNotify_LoopingFeedback::OnActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	return StartFeedback(Target);
}

bool ASWGameplayCueNotify_LoopingFeedback::WhileActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	return StartFeedback(Target);
}

bool ASWGameplayCueNotify_LoopingFeedback::OnRemove_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	StopFeedback();
	return true;
}

void ASWGameplayCueNotify_LoopingFeedback::StopFeedback()
{
	LoopNiagara->DeactivateImmediate();
	LoopNiagara->SetVisibility(false);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	AttachedTarget.Reset();
}

bool ASWGameplayCueNotify_LoopingFeedback::Recycle()
{
	StopFeedback();
	return Super::Recycle();
}
