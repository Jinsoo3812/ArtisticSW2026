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

static TAutoConsoleVariable<int32> CVarStrengthCombatDebug(TEXT("sw.Combat.Strength.Debug"), 0, TEXT("Log authoritative Strength hit decisions."), ECVF_Cheat);
#include "GAS/SWCombatEffectContextLibrary.h"

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
	bool bIgnoreSameTeam, bool bRequireAnimatedHurtbox)
{
	if (!GetOwner()->HasAuthority() || !ActiveSpec.IsValid() || !TargetASC
		|| (Hit.GetActor() && Hit.GetActor() != TargetASC->GetAvatarActor())
		|| !TargetASC->IsOwnerActorAuthoritative() || HitTargets.Contains(TargetASC)
		|| !TargetASC->HasAttributeSetForAttribute(UBaseAttributeSet::GetHealthAttribute())
		|| TargetASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()) <= 0.f
		|| TargetASC->HasMatchingGameplayTag(State_Dead) || TargetASC->HasMatchingGameplayTag(State_Invulnerable)) return false;
	const FGameplayEffectContextHandle& Context = ActiveSpec.Data->GetContext();
	UAbilitySystemComponent* SourceASC = Context.GetOriginalInstigatorAbilitySystemComponent();
	AActor* SourceActor = Context.GetOriginalInstigator();
	if (SourceASC == TargetASC || TargetASC->GetAvatarActor() == GetOwner()) return false;
	// Captured source tags survive source actor destruction and weapon changes in flight.
	const FGameplayTagContainer* SourceTags = ActiveSpec.Data->CapturedSourceTags.GetAggregatedTags();
	if (bIgnoreSameTeam && SourceTags &&
		((SourceTags->HasTag(Team_Player) && TargetASC->HasMatchingGameplayTag(Team_Player)) ||
		 (SourceTags->HasTag(Team_Enemy) && TargetASC->HasMatchingGameplayTag(Team_Enemy)))) return false;
	AActor* TargetActor = TargetASC->GetAvatarActor();
	if (!IsValid(TargetActor) || Hit.ImpactPoint.ContainsNaN() || Hit.TraceStart.ContainsNaN()) return false;
	if (bRequireAnimatedHurtbox && !UCombatHurtboxComponent::IsValidHitSurface(TargetActor, Hit)) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(CombatDamageOcclusion), false, GetOwner());
	Query.AddIgnoredActor(TargetActor);
	if (IsValid(SourceActor)) Query.AddIgnoredActor(SourceActor);
	FHitResult Obstruction;
	const FVector TraceStart = Hit.GetActor() ? FVector(Hit.TraceStart) : GetOwner()->GetActorLocation();
	const FVector TraceEnd = Hit.GetActor() ? FVector(Hit.ImpactPoint) : TargetActor->GetActorLocation();
	if (GetWorld() && GetWorld()->LineTraceSingleByObjectType(Obstruction, TraceStart, TraceEnd,
		FCollisionObjectQueryParams(ECC_WorldStatic), Query)) return false;
	HitTargets.Add(TargetASC); // reserve before callbacks
	FGameplayEffectSpec TargetSpec(*ActiveSpec.Data.Get());
	USWCombatEffectContextLibrary::EnrichCombatEffectSpec(TargetSpec, SourceActor, GetOwner(),
		TargetASC->GetAvatarActor(), &Hit, GetOwner()->GetVelocity());
	UBaseAttributeSet* Attributes = const_cast<UBaseAttributeSet*>(TargetASC->GetSet<UBaseAttributeSet>());
	if (!Attributes) return false;
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
	if (CVarStrengthCombatDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogTemp, Display, TEXT("StrengthHit Causer=%s Window=%llu Target=%s Health=%.2f->%.2f Confirmed=%d"),
			*GetNameSafe(GetOwner()), Sequence, *GetNameSafe(TargetActor), HealthBefore, Attributes->GetHealth(), bConfirmed);
	}
	return bConfirmed;
}
