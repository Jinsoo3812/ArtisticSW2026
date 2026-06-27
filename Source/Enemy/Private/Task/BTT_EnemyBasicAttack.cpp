// Fill out your copyright notice in the Description page of Project Settings.


// Enemy

#include "task/BTT_EnemyBasicAttack.h"

#include "BaseEnemy.h"
#include "AI/BaseAIController.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/BaseWeaponComponent.h"
#include "Weapon/WeaponDataAsset.h"

// Core
#include "BaseGameplayTags.h"

// Unreal Folder
#include "Engine/Engine.h"

#include "AbilitySystemComponent.h"
#include "GameplayAbilitySpec.h"
#include "Abilities/GameplayAbility.h"


UBTT_EnemyBasicAttack::UBTT_EnemyBasicAttack()
{
	NodeName = TEXT("Enemy Basic Attack");
	bNotifyTick = true;
	bCreateNodeInstance = true;

	AttackAbilityAssetTag = GameplayAbility_BasicAttack;
	AttackStateTag = State_Attacking;
}

EBTNodeResult::Type UBTT_EnemyBasicAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	CleanupTagDelegate();
	bObservedAttackStart = false;

	ABaseAIController* AIController = Cast<ABaseAIController>(OwnerComp.GetOwner());
	if (!AIController)
	{
		return EBTNodeResult::Failed;
	}

	ABaseEnemy* Enemy = Cast<ABaseEnemy>(AIController->GetPawn());
	if (!Enemy)
	{
		return EBTNodeResult::Failed;
	}

	UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
	// ASC가 없거나 이미 실행중이라면, return
	if (!ASC || ASC->HasMatchingGameplayTag(AttackStateTag))
	{
		return EBTNodeResult::Failed;
	}

	CachedOwnerComp = &OwnerComp;
	CachedASC = ASC;

	// State_Attack tag를 받으면 
	AttackTagDelegateHandle = ASC->RegisterGameplayTagEvent(AttackStateTag).AddUObject(this,
		&UBTT_EnemyBasicAttack::OnAttackTagChanged);

	if (!ActivateCurrentWeaponAbilityByAssetTag(Enemy, AttackAbilityAssetTag))
	{
		CleanupTagDelegate();
		return EBTNodeResult::Failed;
	}

	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTT_EnemyBasicAttack::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	CleanupTagDelegate();
	bObservedAttackStart = false;
	CachedOwnerComp.Reset();
	CachedASC.Reset();

	return Super::AbortTask(OwnerComp, NodeMemory);
}

// Debug용 코드
/*FString UBTT_EnemyBasicAttack::GetStaticDescription() const
{
    return FString::Printf(TEXT("AbilityAssetTag: %s\nAttackStateTag: %s"),
        *AttackAbilityAssetTag.ToString(),
        *AttackStateTag.ToString());
}*/

bool UBTT_EnemyBasicAttack::ActivateCurrentWeaponAbilityByAssetTag(ABaseEnemy* Enemy, const FGameplayTag& AbilityAssetTag) const
{
    if (!Enemy || !AbilityAssetTag.IsValid())
    {
        return false;
    }

	// WeaponComponent가 있고, 무기가 Equipped상태라면
    UBaseWeaponComponent* WeaponComponent = Enemy->GetWeaponComponent();
    if (!WeaponComponent )
    {
    	//!WeaponComponent->IsWeaponEquipped() 만약 무기의 장착상태일때만 하고싶다면,
        return false;
    }
	
	const FGameplayTag CurrentWeaponTag = Enemy->GetDefaultWeaponTag();
	if (!CurrentWeaponTag.IsValid())
	{
		return false;
	}

    UWeaponDataAsset* WeaponRegistry = WeaponComponent->GetWeaponRegistry();
    if (!WeaponRegistry)
    {
        return false;
    }

    const FWeaponDefinition* WeaponDef = WeaponRegistry->FindWeaponDefinitionByTag(CurrentWeaponTag);
    if (!WeaponDef)
    {
        return false;
    }

    UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
    ABaseWeapon* CurrentWeapon = WeaponComponent->GetCurrentWeapon();
    if (!ASC || !CurrentWeapon)
    {
        return false;
    }

    TArray<FGameplayAbilitySpec> ActivatableAbilities = ASC->GetActivatableAbilities();

    for (const FGrantedWeaponAbility& AbilityInfo : WeaponDef->AbilityData.GrantedAbilities)
    {
        if (!AbilityInfo.AbilityClass || !IsAbilityClassTagged(AbilityInfo.AbilityClass, AbilityAssetTag))
        {
            continue;
        }

        for (const FGameplayAbilitySpec& Spec : ActivatableAbilities)
        {
            if (!Spec.Ability)
            {
                continue;
            }

            const bool bSameClass = (Spec.Ability->GetClass() == AbilityInfo.AbilityClass);
            const bool bFromCurrentWeapon = (Spec.SourceObject == CurrentWeapon);

            if (bSameClass && bFromCurrentWeapon)
            {
            	ASC->TryActivateAbility(Spec.Handle);
            	UE_LOG(LogTemp, Log, TEXT("Activated ability: %s from weapon: %s"), *Spec.Ability->GetName(), *CurrentWeapon->GetName());
            	return true;
            }
        }
    }

    return false;
}

bool UBTT_EnemyBasicAttack::IsAbilityClassTagged(TSubclassOf<UGameplayAbility> AbilityClass, const FGameplayTag& AbilityAssetTag) const
{
    if (!AbilityClass || !AbilityAssetTag.IsValid())
    {
        return false;
    }

    const UGameplayAbility* AbilityCDO = AbilityClass->GetDefaultObject<UGameplayAbility>();
    if (!AbilityCDO)
    {
        return false;
    }

    return AbilityCDO->GetAssetTags().HasTagExact(AbilityAssetTag);
}

void UBTT_EnemyBasicAttack::CleanupTagDelegate()
{
    if (CachedASC.IsValid() && AttackTagDelegateHandle.IsValid())
    {
        CachedASC->RegisterGameplayTagEvent(AttackStateTag).Remove(AttackTagDelegateHandle);
    }

    AttackTagDelegateHandle.Reset();
}

void UBTT_EnemyBasicAttack::OnAttackTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
    if (!CachedOwnerComp.IsValid())
    {
        CleanupTagDelegate();
        return;
    }

    if (NewCount > 0)
    {
        bObservedAttackStart = true;
    	// UE_LOG(LogTemp, Log, TEXT("Enemy Basic Attack started. Tag: %s"), *CallbackTag.ToString());
        return;
    }

    if (bObservedAttackStart && NewCount == 0)
    {
        UBehaviorTreeComponent* OwnerComp = CachedOwnerComp.Get();
        CleanupTagDelegate();
        bObservedAttackStart = false;
        CachedOwnerComp.Reset();
        CachedASC.Reset();

        FinishLatentTask(*OwnerComp, EBTNodeResult::Succeeded);
    }
}
