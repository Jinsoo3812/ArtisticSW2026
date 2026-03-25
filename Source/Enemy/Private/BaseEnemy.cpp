// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseEnemy.h"

// Core
#include "ArtisticSWCore/Public/Item/BaseItem.h"

// Player Folder
#include "BasePlayer.h"

// Enemy Folder
#include "BaseAIController.h"
#include "EnemyAttributeSet.h"

// Unreal
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Net/UnrealNetwork.h"

ABaseEnemy::ABaseEnemy()
{
	PrimaryActorTick.bCanEverTick = true;
	
	ASCReplicationMode = EGameplayEffectReplicationMode::Minimal;
	
	BasicAttributes = CreateDefaultSubobject<UEnemyAttributeSet>(TEXT("BasicAttributeSet"));
	BehaviorTree = CreateDefaultSubobject<UBehaviorTree>(TEXT("BehaviorTree"));
}

void ABaseEnemy::BeginPlay()
{
	Super::BeginPlay();

	// AIController 변수 Cast해주기
	AIController = Cast<ABaseAIController>(UAIBlueprintHelperLibrary::GetAIController(this));

	if (HasAuthority() && DefaultWeaponClass && !CurrentWeapon)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		CurrentWeapon = GetWorld()->SpawnActor<ABaseItem>(
			DefaultWeaponClass,
			GetActorLocation(),
			GetActorRotation(),
			SpawnParams
		);

		if (CurrentWeapon)
		{
			CurrentWeapon->SetOwner(this);
			// 여기서 바로 붙이지 않고, GA_Equip에서 PickUpItem(this) 하게 둘 수 있음
		}
	}
}

void ABaseEnemy::OnRep_CurrentWeapon()
{
	
}

void ABaseEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABaseEnemy, CurrentWeapon);
}


/*
void ABaseEnemy::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

} */


