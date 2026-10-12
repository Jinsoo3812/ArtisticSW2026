#include "BossAI/GunnerBossEnemy.h"
#include "BossAI/GunnerBossCombatData.h"
#include "BossAI/BossGunCombatComponent.h"
#include "BossAI/BossGunMovementComponent.h"
#include "GAS/Ability/Boss/GA_BossGunAttack.h"
#include "BasePlayer.h"
#include "Components/BaseHealthComponent.h"
#include "AbilitySystemComponent.h"
#include "BossAI/GunnerBossTags.h"

AGunnerBossEnemy::AGunnerBossEnemy(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	GunCombat = CreateDefaultSubobject<UBossGunCombatComponent>(TEXT("GunCombat"));
	GunMovement = CreateDefaultSubobject<UBossGunMovementComponent>(TEXT("GunMovement"));
	StartingAbilities.RemoveAll([](const TSubclassOf<UGameplayAbility>& Class)
	{
		return Class && Class->IsChildOf(UBossGameplayAbility::StaticClass());
	});
	StartingAbilities.Add(UGA_BossGunAttack::StaticClass());
	StartingAbilities.Add(UGA_BossGunBurst::StaticClass());
	// The gun mesh may be attached in BP; do not grant the sword's legacy melee attack.
	bEquipWeaponOnSpawn = false;
	DefaultWeaponTag = FGameplayTag();
}

void AGunnerBossEnemy::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		FString Error;
		if (!CombatData || !CombatData->Validate(Error))
			UE_LOG(LogTemp, Error, TEXT("GunnerBoss %s: %s"), *GetName(), CombatData ? *Error : TEXT("Assign CombatData."));
	}
}

bool AGunnerBossEnemy::CanEngageActor_Implementation(AActor* Candidate) const
{
	const ABasePlayer* Player = Cast<ABasePlayer>(Candidate);
	return IsValid(Player) && Player->GetHealthComponent() && !Player->GetHealthComponent()->IsDead()
		&& Super::CanEngageActor_Implementation(Candidate);
}

void AGunnerBossEnemy::HandleDeath_Implementation()
{
	GunMovement->Stop();
	GunCombat->ResetCombat();
	Super::HandleDeath_Implementation();
}

bool AGunnerBossEnemy::CanMoveOnDeck() const
{
	return Super::CanMoveOnDeck() && !GunCombat->IsAttackLocked();
}

void AGunnerBossEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelGunAbilities();
	GunMovement->Stop();
	GunCombat->ResetCombat();
	Super::EndPlay(EndPlayReason);
}

void AGunnerBossEnemy::CancelGunAbilities()
{
	if (!HasAuthority()) return;
	FGameplayTagContainer GunAbilityTags;
	GunAbilityTags.AddTag(GunnerBossTags::Shot);
	GunAbilityTags.AddTag(GunnerBossTags::Burst);
	GetAbilitySystemComponent()->CancelAbilities(&GunAbilityTags);
}

bool AGunnerBossEnemy::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError)
{
	// Active aiming is transient; a restored encounter begins with normal BT decisions.
	if (Part.Domain == ESWRoomDomain::Boss)
	{
		CancelGunAbilities();
		GunMovement->Stop();
		GunCombat->ResetCombat();
	}
	return Super::RestoreRoomDomain(Part, OutError);
}
