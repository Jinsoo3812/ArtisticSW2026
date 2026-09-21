#include "Components/CombatPresentationComponent.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimInstance.h"

UCombatPresentationComponent* UCombatPresentationComponent::GetOrCreate(AActor* Owner)
{
	if (!IsValid(Owner)) return nullptr;
	if (auto* Existing = Owner->FindComponentByClass<UCombatPresentationComponent>()) return Existing;
	auto* Component = NewObject<UCombatPresentationComponent>(Owner);
	Owner->AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

void UCombatPresentationComponent::Initialize(UAbilitySystemComponent* ASC)
{
	if (AbilitySystem.Get() == ASC) return;
	Uninitialize();
	AbilitySystem = ASC;
	if (!ASC) return;
	StrengthHandle = ASC->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetStrengthAttribute())
		.AddUObject(this, &UCombatPresentationComponent::StrengthChanged);
	AttackSpeedHandle = ASC->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute())
		.AddUObject(this, &UCombatPresentationComponent::AttackSpeedChanged);
	OnStrengthChanged.Broadcast(GetStrength());
}

void UCombatPresentationComponent::Uninitialize()
{
	if (AbilitySystem.IsValid())
	{
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetStrengthAttribute()).Remove(StrengthHandle);
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UBaseAttributeSet::GetAttackSpeedMultiplierAttribute()).Remove(AttackSpeedHandle);
	}
	AbilitySystem.Reset();
	StrengthHandle.Reset();
	AttackSpeedHandle.Reset();
}

float UCombatPresentationComponent::GetStrength() const
{
	return AbilitySystem.IsValid() ? AbilitySystem->GetNumericAttribute(UBaseAttributeSet::GetStrengthAttribute()) : 0.f;
}

void UCombatPresentationComponent::StrengthChanged(const FOnAttributeChangeData& Data)
{
	OnStrengthChanged.Broadcast(Data.NewValue);
}

void UCombatPresentationComponent::AttackSpeedChanged(const FOnAttributeChangeData& Data)
{
	UAbilitySystemComponent* ASC = AbilitySystem.Get();
	if (!ASC || !ASC->IsOwnerActorAuthoritative() || Data.OldValue <= KINDA_SMALL_NUMBER
		|| FMath::IsNearlyEqual(Data.OldValue, Data.NewValue)) return;
	const UGameplayAbility* Ability = ASC->GetAnimatingAbility();
	const FGameplayAbilityActorInfo* Info = Ability ? Ability->GetCurrentActorInfo() : nullptr;
	UAnimInstance* Anim = Info ? Info->GetAnimInstance() : nullptr;
	if (Ability && Anim && ASC->GetCurrentMontage() && Ability->GetAssetTags().HasTagExact(GameplayAbility_BasicAttack))
	{
		const float Rate = Anim->Montage_GetPlayRate(ASC->GetCurrentMontage());
		ASC->CurrentMontageSetPlayRate(FMath::Max(0.01f, Rate * Data.NewValue / Data.OldValue));
	}
}

void UCombatPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Uninitialize();
	Super::EndPlay(Reason);
}
