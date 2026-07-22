#include "Attacker/GA_WaterBombCannonMode.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "Cannon.h"
#include "GameFramework/Pawn.h"
#include "WaterBombCannonball.h"

UGA_WaterBombCannonMode::UGA_WaterBombCannonMode()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	ProjectileClass = AWaterBombCannonball::StaticClass();

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(GameplayAbility_Skill_WaterBomb);
	SetAssetTags(AssetTags);
}

void UGA_WaterBombCannonMode::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo) || !ProjectileClass)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ACannon* Cannon = FindRiddenCannon();
	if (!Cannon || !Cannon->ActivateWaterBombModeFromAbility(
		this, ProjectileClass, EffectDurationSeconds, AttackSpeedMultiplier))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WaterBomb] GA activation rejected: avatar must currently be riding a player-controlled cannon."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActiveCannon = Cannon;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(GameplayAbility_Skill_WaterBomb);
		bAddedActivationTag = true;
	}

	UE_LOG(LogTemp, Warning, TEXT("[WaterBomb] GA active: avatar=%s, cannon=%s"),
		*GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(Cannon));
}

void UGA_WaterBombCannonMode::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (ACannon* Cannon = ActiveCannon.Get())
	{
		Cannon->DeactivateWaterBombModeFromAbility(this);
	}
	ActiveCannon.Reset();

	if (bAddedActivationTag)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveLooseGameplayTag(GameplayAbility_Skill_WaterBomb);
		}
		bAddedActivationTag = false;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

ACannon* UGA_WaterBombCannonMode::FindRiddenCannon() const
{
	const APawn* PlayerPawn = Cast<APawn>(GetAvatarActorFromActorInfo());
	if (!PlayerPawn || !PlayerPawn->HasAuthority())
	{
		return nullptr;
	}

	for (AActor* Parent = PlayerPawn->GetAttachParentActor(); Parent; Parent = Parent->GetAttachParentActor())
	{
		if (ACannon* Cannon = Cast<ACannon>(Parent))
		{
			return Cannon->GetRidingPlayer() == PlayerPawn ? Cannon : nullptr;
		}
	}

	return nullptr;
}
