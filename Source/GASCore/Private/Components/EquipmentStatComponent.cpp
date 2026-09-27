#include "Components/EquipmentStatComponent.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "GASStrengthEquipmentGameplayEffect.h"

UEquipmentStatComponent* UEquipmentStatComponent::GetOrCreate(AActor* Owner)
{
	if (!IsValid(Owner) || !Owner->HasAuthority()) return nullptr;
	if (auto* Existing = Owner->FindComponentByClass<UEquipmentStatComponent>()) return Existing;
	auto* Component = NewObject<UEquipmentStatComponent>(Owner);
	Owner->AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

bool UEquipmentStatComponent::IsEquipped(const AActor* Item) const
{
	return IsValid(Item) && EquippedItem.Get() == Item && AppliedASC.IsValid()
		&& (!EffectHandle.IsValid() || AppliedASC->GetActiveGameplayEffect(EffectHandle));
}

bool UEquipmentStatComponent::Equip(UAbilitySystemComponent* ASC, AActor* Item, float Bonus,
	TSubclassOf<UGameplayEffect> EffectClass)
{
	if (!GetOwner()->HasAuthority() || bTransitioning || !IsValid(Item) || !ASC
		|| !ASC->IsOwnerActorAuthoritative() || ASC->GetAvatarActor() != GetOwner()
		|| !ASC->HasAttributeSetForAttribute(UBaseAttributeSet::GetStrengthAttribute())
		|| ASC->HasMatchingGameplayTag(State_Dead) || !FMath::IsFinite(Bonus) || Bonus < 0.f) return false;
	if (IsEquipped(Item) && AppliedASC.Get() == ASC) return FMath::IsNearlyEqual(AppliedBonus, Bonus);
	if (!EffectClass) EffectClass = UGASStrengthEquipmentGameplayEffect::StaticClass();
	const UGameplayEffect* Definition = EffectClass.GetDefaultObject();
	if (!Definition || !EffectClass->IsChildOf(UGASStrengthEquipmentGameplayEffect::StaticClass())
		|| Definition->DurationPolicy != EGameplayEffectDurationType::Infinite
		|| Definition->Modifiers.Num() != 1 || !Definition->Executions.IsEmpty()
		|| Definition->StackingType != EGameplayEffectStackingType::None) return false;
	const FGameplayModifierInfo& Modifier = Definition->Modifiers[0];
	if (Modifier.Attribute != UBaseAttributeSet::GetStrengthAttribute() || Modifier.ModifierOp != EGameplayModOp::Additive
		|| Modifier.ModifierMagnitude.GetMagnitudeCalculationType() != EGameplayEffectMagnitudeCalculation::SetByCaller
		|| Modifier.ModifierMagnitude.GetSetByCallerFloat().DataTag != Data_StrengthBonus) return false;
	TGuardValue<bool> Guard(bTransitioning, true);
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(Item);
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, 1.f, Context);
	if (!Spec.IsValid()) return false;
	Spec.Data->SetSetByCallerMagnitude(Data_StrengthBonus, Bonus);
	// Stage the new contribution while transitions block attacks. A rejected GE leaves the old item intact.
	const FActiveGameplayEffectHandle NewHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	if (!NewHandle.IsValid()) return false;
	if (AppliedASC.IsValid() && EffectHandle.IsValid() && AppliedASC->GetActiveGameplayEffect(EffectHandle)
		&& !AppliedASC->RemoveActiveGameplayEffect(EffectHandle))
	{
		ASC->RemoveActiveGameplayEffect(NewHandle);
		return false;
	}
	Unbind();
	AppliedASC = ASC;
	EquippedItem = Item;
	EffectHandle = NewHandle;
	Item->OnDestroyed.AddDynamic(this, &UEquipmentStatComponent::OnItemDestroyed);
	Item->OnEndPlay.AddDynamic(this, &UEquipmentStatComponent::OnItemEndPlay);
	AppliedBonus = Bonus;
	AppliedEffectClass = EffectClass;
	DeathTagHandle = ASC->RegisterGameplayTagEvent(State_Dead).AddUObject(this, &UEquipmentStatComponent::OnDeathTagChanged);
	++Revision;
	return true;
}

bool UEquipmentStatComponent::Clear()
{
	if (!GetOwner()->HasAuthority() || bTransitioning) return false;
	TGuardValue<bool> Guard(bTransitioning, true);
	if (AppliedASC.IsValid() && EffectHandle.IsValid() && AppliedASC->GetActiveGameplayEffect(EffectHandle)
		&& !AppliedASC->RemoveActiveGameplayEffect(EffectHandle)) return false;
	Unbind();
	EffectHandle.Invalidate();
	AppliedASC.Reset();
	EquippedItem.Reset();
	++Revision;
	return true;
}

void UEquipmentStatComponent::Unbind()
{
	if (AActor* Item = EquippedItem.Get())
	{
		Item->OnDestroyed.RemoveDynamic(this, &UEquipmentStatComponent::OnItemDestroyed);
		Item->OnEndPlay.RemoveDynamic(this, &UEquipmentStatComponent::OnItemEndPlay);
	}
	if (AppliedASC.IsValid()) AppliedASC->RegisterGameplayTagEvent(State_Dead).Remove(DeathTagHandle);
	DeathTagHandle.Reset();
}

void UEquipmentStatComponent::OnItemDestroyed(AActor* Item) { Clear(); }
void UEquipmentStatComponent::OnItemEndPlay(AActor* Item, EEndPlayReason::Type Reason) { Clear(); }
void UEquipmentStatComponent::OnDeathTagChanged(const FGameplayTag Tag, int32 Count) { if (Count > 0) Clear(); }
void UEquipmentStatComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Clear();
	Unbind();
	Super::EndPlay(Reason);
}

bool UEquipmentStatComponent::RebindAbilitySystem(UAbilitySystemComponent* ASC)
{
	if (AppliedASC.Get() == ASC || !EquippedItem.IsValid()) return true;
	if (!ASC) return Clear();
	return Equip(ASC, EquippedItem.Get(), AppliedBonus, AppliedEffectClass);
}
