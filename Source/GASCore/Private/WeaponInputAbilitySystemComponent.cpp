#include "WeaponInputAbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Item/BaseItem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	TAutoConsoleVariable<int32> CVarCombatInputDiagnostics(TEXT("SW.Debug.CombatInput"), 0,
		TEXT("Log combat input gates, ability routing and GAS lifecycle. 0=off, 1=on."), ECVF_Default);
#endif
}

bool UWeaponInputAbilitySystemComponent::IsInputDiagnosticsEnabled()
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	return CVarCombatInputDiagnostics.GetValueOnGameThread() != 0;
#else
	return false;
#endif
}

void UWeaponInputAbilitySystemComponent::LogInputDiagnostic(const TCHAR* Event, const FString& Detail) const
{
	if (!IsInputDiagnosticsEnabled()) return;
	FGameplayTagContainer Tags;
	GetOwnedGameplayTags(Tags);
	const FGameplayTagContainer Blocked = GetBlockedAbilityTags();
	FString Active;
	for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (Spec.IsActive()) Active += FString::Printf(TEXT("%s#%s;"), *GetNameSafe(Spec.Ability), *Spec.Handle.ToString());
	}
	const AActor* Avatar = GetAvatarActor();
	UE_LOG(LogTemp, Display, TEXT("[SWCombatInput] Event=%s World=%s NetMode=%d Avatar=%s Authority=%d ASC=%s %s Tags=[%s] BlockedAbilities=[%s] Active=[%s]"),
		Event, *GetNameSafe(GetWorld()), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
		*GetNameSafe(Avatar), Avatar && Avatar->HasAuthority(), *GetName(), *Detail, *Tags.ToStringSimple(), *Blocked.ToStringSimple(), *Active);
}

void UWeaponInputAbilitySystemComponent::NotifyAbilityActivated(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability)
{
	Super::NotifyAbilityActivated(Handle, Ability);
	if (IsInputDiagnosticsEnabled()) LogInputDiagnostic(TEXT("Activated"), FString::Printf(TEXT("Ability=%s Handle=%s"), *GetNameSafe(Ability), *Handle.ToString()));
}

void UWeaponInputAbilitySystemComponent::NotifyAbilityFailed(const FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, const FGameplayTagContainer& FailureReason)
{
	Super::NotifyAbilityFailed(Handle, Ability, FailureReason);
	if (IsInputDiagnosticsEnabled()) LogInputDiagnostic(TEXT("ActivationFailed"), FString::Printf(TEXT("Ability=%s Handle=%s Failure=[%s]"), *GetNameSafe(Ability), *Handle.ToString(), *FailureReason.ToStringSimple()));
}

void UWeaponInputAbilitySystemComponent::NotifyAbilityEnded(FGameplayAbilitySpecHandle Handle, UGameplayAbility* Ability, bool bWasCancelled)
{
	Super::NotifyAbilityEnded(Handle, Ability, bWasCancelled);
	if (IsInputDiagnosticsEnabled()) LogInputDiagnostic(TEXT("Ended"), FString::Printf(TEXT("Ability=%s Handle=%s Cancelled=%d"), *GetNameSafe(Ability), *Handle.ToString(), bWasCancelled));
}

void UWeaponInputAbilitySystemComponent::InputTagPressed(FGameplayTag InputTag) { RouteInput(InputTag, true); }
void UWeaponInputAbilitySystemComponent::InputTagReleased(FGameplayTag InputTag) { RouteInput(InputTag, false); }

void UWeaponInputAbilitySystemComponent::RouteInput(FGameplayTag InputTag, bool bPressed)
{
	if (!InputTag.IsValid()) return;
	const bool bDiagnostic = IsInputDiagnosticsEnabled();
	if (bDiagnostic) LogInputDiagnostic(bPressed ? TEXT("RoutePressed") : TEXT("RouteReleased"), FString::Printf(TEXT("Input=%s"), *InputTag.ToString()));
	ABILITYLIST_SCOPE_LOCK();
	// Equipped item bindings take precedence over character defaults on the same input.
	const bool bHasItemBinding = GetActivatableAbilities().ContainsByPredicate([InputTag](const FGameplayAbilitySpec& Spec)
	{
		return Cast<ABaseItem>(Spec.SourceObject.Get()) && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag);
	});
	int32 Matched = 0;
	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (!Spec.Ability || !Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag)) continue;
		if (bPressed && bHasItemBinding && !Cast<ABaseItem>(Spec.SourceObject.Get())) continue;
		++Matched;
		if (bDiagnostic) LogInputDiagnostic(TEXT("MatchedSpec"), FString::Printf(TEXT("Input=%s Ability=%s Handle=%s Source=%s Active=%d Policy=%d"),
			*InputTag.ToString(), *GetNameSafe(Spec.Ability), *Spec.Handle.ToString(), *GetNameSafe(Spec.SourceObject.Get()), Spec.IsActive(), static_cast<int32>(Spec.Ability->GetNetExecutionPolicy())));
		Spec.InputPressed = bPressed;
		if (Spec.IsActive())
		{
			if (Spec.Ability->bReplicateInputDirectly && !IsOwnerActorAuthoritative())
			{
				if (bPressed) ServerSetInputPressed(Spec.Handle);
				else ServerSetInputReleased(Spec.Handle);
			}
			if (bPressed) AbilitySpecInputPressed(Spec);
			else AbilitySpecInputReleased(Spec);
			// WaitInputRelease/Pressed uses the active instance's prediction key.
			const TArray<UGameplayAbility*> Instances = Spec.GetAbilityInstances();
			const FPredictionKey Key = Instances.IsEmpty() ? FPredictionKey()
				: Instances.Last()->GetCurrentActivationInfo().GetActivationPredictionKey();
			InvokeReplicatedEvent(bPressed ? EAbilityGenericReplicatedEvent::InputPressed
				: EAbilityGenericReplicatedEvent::InputReleased, Spec.Handle, Key);
		}
		else if (bPressed)
		{
			const FGameplayAbilitySpecHandle Handle = Spec.Handle;
			const bool bAccepted = TryActivateAbility(Handle);
			if (bDiagnostic) LogInputDiagnostic(TEXT("ActivationRequest"), FString::Printf(TEXT("Input=%s Handle=%s Accepted=%d (request acceptance is not server confirmation)"), *InputTag.ToString(), *Handle.ToString(), bAccepted));
		}
	}
	if (bDiagnostic && Matched == 0) LogInputDiagnostic(TEXT("NoMatchingSpec"), FString::Printf(TEXT("Input=%s ItemBinding=%d"), *InputTag.ToString(), bHasItemBinding));
}
