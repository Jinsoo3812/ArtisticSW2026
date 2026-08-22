#include "GAS/SWCombatEffectContextLibrary.h"

#include "AbilitySystemComponent.h"
#include "GAS/SWGameplayEffectContext.h"
#include "GameplayEffect.h"

namespace
{
	FSWGameplayEffectContext* GetMutableSWContext(FGameplayEffectContextHandle& ContextHandle)
	{
		FGameplayEffectContext* Context = ContextHandle.Get();
		return Context && Context->GetScriptStruct()->IsChildOf(FSWGameplayEffectContext::StaticStruct())
			? static_cast<FSWGameplayEffectContext*>(Context)
			: nullptr;
	}

	const FSWGameplayEffectContext* GetSWContext(const FGameplayEffectContextHandle& ContextHandle)
	{
		const FGameplayEffectContext* Context = ContextHandle.Get();
		return Context && Context->GetScriptStruct()->IsChildOf(FSWGameplayEffectContext::StaticStruct())
			? static_cast<const FSWGameplayEffectContext*>(Context)
			: nullptr;
	}
}

FGameplayEffectContextHandle USWCombatEffectContextLibrary::MakeCombatEffectContext(
	UAbilitySystemComponent* SourceASC,
	AActor* InstigatorActor,
	AActor* EffectCauser,
	AActor* TargetActor,
	bool bAddHitResult,
	const FHitResult& HitResult,
	const FVector& ExplicitImpactDirection)
{
	if (!SourceASC)
	{
		return FGameplayEffectContextHandle();
	}

	FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
	EnrichCombatEffectContext(
		ContextHandle,
		InstigatorActor,
		EffectCauser,
		TargetActor,
		bAddHitResult ? &HitResult : nullptr,
		ExplicitImpactDirection);
	return ContextHandle;
}

FGameplayEffectContextHandle USWCombatEffectContextLibrary::SetImpactDirection(
	FGameplayEffectContextHandle ContextHandle,
	const FVector& ImpactDirection)
{
	if (FSWGameplayEffectContext* Context = GetMutableSWContext(ContextHandle))
	{
		Context->SetImpactDirection(ImpactDirection);
	}
	return ContextHandle;
}

bool USWCombatEffectContextLibrary::GetImpactDirection(
	const FGameplayEffectContextHandle& ContextHandle,
	FVector& OutImpactDirection)
{
	OutImpactDirection = FVector::ZeroVector;
	const FSWGameplayEffectContext* Context = GetSWContext(ContextHandle);
	if (!Context || !Context->HasImpactDirection())
	{
		return false;
	}

	OutImpactDirection = Context->GetImpactDirection();
	return !OutImpactDirection.IsNearlyZero();
}

FVector USWCombatEffectContextLibrary::ResolveImpactDirection(
	const AActor* InstigatorActor,
	const AActor* EffectCauser,
	const AActor* TargetActor,
	const FHitResult* HitResult,
	const FVector& ExplicitImpactDirection)
{
	FVector Direction = ExplicitImpactDirection.GetSafeNormal2D();
	if (Direction.IsNearlyZero() && HitResult)
	{
		Direction = (HitResult->TraceEnd - HitResult->TraceStart).GetSafeNormal2D();
	}
	if (Direction.IsNearlyZero() && InstigatorActor && TargetActor && InstigatorActor != TargetActor)
	{
		Direction = (TargetActor->GetActorLocation() - InstigatorActor->GetActorLocation()).GetSafeNormal2D();
	}
	// Effect causers include attached melee weapons as well as projectiles. Their
	// velocity is therefore only a fallback; projectile callers should pass an
	// explicit launch/current velocity when it is authoritative.
	if (Direction.IsNearlyZero() && EffectCauser && EffectCauser != InstigatorActor)
	{
		Direction = EffectCauser->GetVelocity().GetSafeNormal2D();
	}
	if (Direction.IsNearlyZero() && HitResult)
	{
		Direction = (-HitResult->ImpactNormal).GetSafeNormal2D();
	}
	return Direction;
}

bool USWCombatEffectContextLibrary::EnrichCombatEffectContext(
	FGameplayEffectContextHandle& ContextHandle,
	AActor* InstigatorActor,
	AActor* EffectCauser,
	AActor* TargetActor,
	const FHitResult* HitResult,
	const FVector& ExplicitImpactDirection)
{
	if (!ContextHandle.IsValid())
	{
		return false;
	}

	ContextHandle.AddInstigator(InstigatorActor, EffectCauser);
	if (EffectCauser)
	{
		ContextHandle.AddSourceObject(EffectCauser);
	}
	if (HitResult)
	{
		ContextHandle.AddHitResult(*HitResult, true);
	}

	FSWGameplayEffectContext* Context = GetMutableSWContext(ContextHandle);
	if (!Context)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Combat context is not FSWGameplayEffectContext. Verify AbilitySystemGlobalsClassName."));
		return false;
	}

	Context->SetImpactDirection(ResolveImpactDirection(
		InstigatorActor, EffectCauser, TargetActor, HitResult, ExplicitImpactDirection));
	return Context->HasImpactDirection();
}

bool USWCombatEffectContextLibrary::EnrichCombatEffectSpec(
	FGameplayEffectSpec& EffectSpec,
	AActor* InstigatorActor,
	AActor* EffectCauser,
	AActor* TargetActor,
	const FHitResult* HitResult,
	const FVector& ExplicitImpactDirection)
{
	FGameplayEffectContextHandle ContextHandle = EffectSpec.GetContext().Duplicate();
	if (!ContextHandle.IsValid())
	{
		return false;
	}

	const bool bHasDirection = EnrichCombatEffectContext(
		ContextHandle,
		InstigatorActor,
		EffectCauser,
		TargetActor,
		HitResult,
		ExplicitImpactDirection);
	EffectSpec.SetContext(ContextHandle);
	return bHasDirection;
}
