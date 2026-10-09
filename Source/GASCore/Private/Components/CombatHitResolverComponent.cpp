#include "Components/CombatHitResolverComponent.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Components/EquipmentStatComponent.h"
#include "Components/CombatHurtboxComponent.h"
#include "CollisionChannels.h"
#include "Components/PrimitiveComponent.h"
#include "Item/Weapons/SwordItem.h"

static TAutoConsoleVariable<int32> CVarStrengthCombatDebug(TEXT("sw.Combat.Strength.Debug"), 0, TEXT("Log authoritative Strength hit decisions."), ECVF_Cheat);
#include "GAS/SWCombatEffectContextLibrary.h"

static bool IsHitDecisionDebugEnabled(const AActor* Causer)
{
	if (CVarStrengthCombatDebug.GetValueOnGameThread() != 0) return true;
	const IConsoleVariable* MeleeDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("sw.Combat.Melee.Debug"));
	return Causer && Causer->IsA<ASwordItem>() && MeleeDebug && MeleeDebug->GetInt() != 0;
}

UCombatHitResolverComponent* UCombatHitResolverComponent::GetOrCreate(AActor* Causer)
{
	if (!IsValid(Causer) || !Causer->HasAuthority()) return nullptr;
	if (auto* Existing = Causer->FindComponentByClass<UCombatHitResolverComponent>()) return Existing;
	auto* Component = NewObject<UCombatHitResolverComponent>(Causer);
	Causer->AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

bool UCombatHitResolverComponent::OpenWindow(const FGameplayEffectSpecHandle& Spec)
{
	if (!GetOwner()->HasAuthority() || !Spec.IsValid() || !Spec.Data.IsValid()
		|| Spec.Data->GetContext().GetEffectCauser() != GetOwner()) return false;
	if (LastSpec.Pin() == Spec.Data) return ActiveSpec.Data == Spec.Data;
	CloseWindow();
	LastSpec = Spec.Data;
	ActiveSpec = Spec;
	++Sequence;
	return true;
}

void UCombatHitResolverComponent::CloseWindow()
{
	ActiveSpec = FGameplayEffectSpecHandle();
	HitTargets.Reset();
}

bool UCombatHitResolverComponent::ResolveHit(UAbilitySystemComponent* TargetASC, const FHitResult& Hit,
	bool bIgnoreSameTeam, bool bRequireAnimatedHurtbox, bool bCheckWorldStaticOcclusion)
{
	const auto Reject = [this, TargetASC, &Hit](const TCHAR* Reason)
	{
		if (IsHitDecisionDebugEnabled(GetOwner()))
		{
			UE_LOG(LogTemp, Display, TEXT("StrengthHit Rejected=%s Causer=%s Target=%s Component=%s Bone=%s"),
				Reason, *GetNameSafe(GetOwner()), *GetNameSafe(TargetASC ? TargetASC->GetAvatarActor() : nullptr),
				*GetNameSafe(Hit.GetComponent()), *Hit.BoneName.ToString());
		}
		return false;
	};
	// Repeated trace samples are expected within a window; keep them quiet.
	if (TargetASC && HitTargets.Contains(TargetASC)) return false;
	if (!GetOwner()->HasAuthority()) return Reject(TEXT("CauserNotAuthoritative"));
	if (!ActiveSpec.IsValid()) return Reject(TEXT("NoActiveWindow"));
	if (!TargetASC) return Reject(TEXT("MissingTargetASC"));
	if (Hit.GetActor() && Hit.GetActor() != TargetASC->GetAvatarActor()) return Reject(TEXT("TargetAvatarMismatch"));
	if (!TargetASC->IsOwnerActorAuthoritative()) return Reject(TEXT("TargetNotAuthoritative"));
	if (!TargetASC->HasAttributeSetForAttribute(UBaseAttributeSet::GetHealthAttribute())) return Reject(TEXT("MissingHealthAttribute"));
	if (TargetASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()) <= 0.f) return Reject(TEXT("TargetHealthZero"));
	if (TargetASC->HasMatchingGameplayTag(State_Dead)) return Reject(TEXT("TargetDead"));
	if (TargetASC->HasMatchingGameplayTag(State_Invulnerable)) return Reject(TEXT("TargetInvulnerable"));
	const FGameplayEffectContextHandle& Context = ActiveSpec.Data->GetContext();
	UAbilitySystemComponent* SourceASC = Context.GetOriginalInstigatorAbilitySystemComponent();
	AActor* SourceActor = Context.GetOriginalInstigator();
	if (SourceASC == TargetASC || TargetASC->GetAvatarActor() == GetOwner()) return Reject(TEXT("SelfHit"));
	// Captured source tags survive source actor destruction and weapon changes in flight.
	const FGameplayTagContainer* SourceTags = ActiveSpec.Data->CapturedSourceTags.GetAggregatedTags();
	if (bIgnoreSameTeam && SourceTags &&
		((SourceTags->HasTag(Team_Player) && TargetASC->HasMatchingGameplayTag(Team_Player)) ||
		 (SourceTags->HasTag(Team_Enemy) && TargetASC->HasMatchingGameplayTag(Team_Enemy)))) return Reject(TEXT("SameTeam"));
	AActor* TargetActor = TargetASC->GetAvatarActor();
	if (!IsValid(TargetActor) || Hit.ImpactPoint.ContainsNaN() || Hit.TraceStart.ContainsNaN()) return Reject(TEXT("InvalidHitCoordinates"));
	if (bRequireAnimatedHurtbox && !UCombatHurtboxComponent::IsValidHitSurface(TargetActor, Hit)) return Reject(TEXT("InvalidHitSurface"));
	if (bCheckWorldStaticOcclusion)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(CombatDamageOcclusion), false, GetOwner());
		Query.AddIgnoredActor(TargetActor);
		if (IsValid(SourceActor)) Query.AddIgnoredActor(SourceActor);
		const FVector TraceStart = Hit.GetActor() ? FVector(Hit.TraceStart) : GetOwner()->GetActorLocation();
		const FVector TraceEnd = Hit.GetActor() ? FVector(Hit.ImpactPoint) : TargetActor->GetActorLocation();
		TArray<FHitResult> Obstructions;
		if (GetWorld()) GetWorld()->LineTraceMultiByObjectType(Obstructions, TraceStart, TraceEnd,
			FCollisionObjectQueryParams(ECC_WorldStatic), Query);
		static const FName PCGVolumeBoundsProfile(TEXT("PCGVolumeBounds"));
		for (const FHitResult& Obstruction : Obstructions)
		{
			// Object queries ignore channel responses. Filter the explicitly assigned
			// bounds profile, while retaining walls and generated meshes in the same query.
			const UPrimitiveComponent* Component = Obstruction.GetComponent();
			if (Component && Component->GetCollisionProfileName() == PCGVolumeBoundsProfile)
			{
				if (IsHitDecisionDebugEnabled(GetOwner()))
				{
					UE_LOG(LogTemp, Display, TEXT("StrengthHit IgnoredCollisionProfile=%s Actor=%s Component=%s"),
						*PCGVolumeBoundsProfile.ToString(), *GetNameSafe(Obstruction.GetActor()), *GetNameSafe(Component));
				}
				continue;
			}
			if (IsHitDecisionDebugEnabled(GetOwner()))
			{
				UE_LOG(LogTemp, Display, TEXT("StrengthHit Obstruction=%s Component=%s Profile=%s Start=%s End=%s"),
					*GetNameSafe(Obstruction.GetActor()), *GetNameSafe(Component),
					Component ? *Component->GetCollisionProfileName().ToString() : TEXT("None"),
					*TraceStart.ToString(), *TraceEnd.ToString());
			}
			return Reject(TEXT("WorldStaticOcclusion"));
		}
	}
	HitTargets.Add(TargetASC); // reserve before callbacks
	FGameplayEffectSpec TargetSpec(*ActiveSpec.Data.Get());
	USWCombatEffectContextLibrary::EnrichCombatEffectSpec(TargetSpec, SourceActor, GetOwner(),
		TargetASC->GetAvatarActor(), &Hit, GetOwner()->GetVelocity());
	UBaseAttributeSet* Attributes = const_cast<UBaseAttributeSet*>(TargetASC->GetSet<UBaseAttributeSet>());
	if (!Attributes) return Reject(TEXT("MissingBaseAttributes"));
	bool bConfirmed = false;
	const FGameplayEffectContext* ExpectedContext = TargetSpec.GetContext().Get();
	const FDelegateHandle Handle = Attributes->OnDamageResolved.AddLambda(
		[&bConfirmed, ExpectedContext](const FGameplayEffectContextHandle& ResolvedContext, float AppliedDamage)
		{
			if (ResolvedContext.Get() == ExpectedContext && AppliedDamage > 0.f) bConfirmed = true;
		});
	const float HealthBefore = Attributes->GetHealth();
	TargetASC->ApplyGameplayEffectSpecToSelf(TargetSpec);
	Attributes->OnDamageResolved.Remove(Handle);
	if (IsHitDecisionDebugEnabled(GetOwner()))
	{
		UE_LOG(LogTemp, Display, TEXT("StrengthHit Causer=%s Window=%llu Target=%s Health=%.2f->%.2f Confirmed=%d"),
			*GetNameSafe(GetOwner()), Sequence, *GetNameSafe(TargetActor), HealthBefore, Attributes->GetHealth(), bConfirmed);
	}
	return bConfirmed;
}
