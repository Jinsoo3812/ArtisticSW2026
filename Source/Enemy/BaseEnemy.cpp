// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseEnemy.h"

#include "EnemyAttributeSet.h"

ABaseEnemy::ABaseEnemy()
{
	PrimaryActorTick.bCanEverTick = true;

	ASCReplicationMode = EGameplayEffectReplicationMode::Minimal;
	BasicAttributes = CreateDefaultSubobject<UEnemyAttributeSet>(TEXT("BasicAttributeSet"));
}

void ABaseEnemy::BeginPlay()
{
	Super::BeginPlay();
	
}

void ABaseEnemy::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}


