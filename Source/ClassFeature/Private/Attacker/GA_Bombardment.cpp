#include "Attacker/GA_Bombardment.h"

#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "Bombardment.h"
#include "GameFramework/Pawn.h"
#include "Ship.h"

UGA_Bombardment::UGA_Bombardment()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	BombardmentClass = ABombardment::StaticClass();

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(GameplayAbility_Skill_Bombardment);
	SetAssetTags(AssetTags);
}

void UGA_Bombardment::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo) || !BombardmentClass)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AShip* Ship = FindRiddenShip();
	if (!Ship || !Ship->ActivateBombardmentModeFromAbility(this, BombardmentClass))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Bombardment] GA activation rejected: avatar must currently be riding a player-controlled ship."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActiveShip = Ship;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(GameplayAbility_Skill_Bombardment);
		bAddedActivationTag = true;
	}
}

void UGA_Bombardment::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (AShip* Ship = ActiveShip.Get())
	{
		Ship->DeactivateBombardmentModeFromAbility(this);
	}
	ActiveShip.Reset();

	if (bAddedActivationTag)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveLooseGameplayTag(GameplayAbility_Skill_Bombardment);
		}
		bAddedActivationTag = false;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

AShip* UGA_Bombardment::FindRiddenShip() const
{
	const APawn* PlayerPawn = Cast<APawn>(GetAvatarActorFromActorInfo());
	if (!PlayerPawn || !PlayerPawn->HasAuthority())
	{
		return nullptr;
	}

	for (AActor* Parent = PlayerPawn->GetAttachParentActor(); Parent; Parent = Parent->GetAttachParentActor())
	{
		if (AShip* Ship = Cast<AShip>(Parent))
		{
			return Ship->GetRidingPlayer() == PlayerPawn ? Ship : nullptr;
		}
	}
	return nullptr;
}
