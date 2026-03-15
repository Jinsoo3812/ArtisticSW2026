// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseEnemy.h"

// Player Folder
#include "BasePlayer.h"

// Enemy Folder
#include "BaseAIController.h"
#include "EnemyAttributeSet.h"
#include "BehaviorTree/BehaviorTree.h"

// Unreal
#include "Blueprint/AIBlueprintHelperLibrary.h"

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
}


/*
void ABaseEnemy::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

} */


