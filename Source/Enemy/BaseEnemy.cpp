// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseEnemy.h"

#include "BaseCharacter.h"

#include "BaseAIController.h"
#include "EnemyAttributeSet.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Kismet/GameplayStatics.h"

ABaseEnemy::ABaseEnemy()
{
	PrimaryActorTick.bCanEverTick = true;

	ASCReplicationMode = EGameplayEffectReplicationMode::Minimal;
	BasicAttributes = CreateDefaultSubobject<UEnemyAttributeSet>(TEXT("BasicAttributeSet"));
}

void ABaseEnemy::BeginPlay()
{
	Super::BeginPlay();

	AIController = Cast<ABaseAIController>(UAIBlueprintHelperLibrary::GetAIController(this));
	BasePlayerClass = Cast<ABaseCharacter>(UGameplayStatics::GetPlayerCharacter(GetWorld(), 0));
}

void ABaseEnemy::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (GetAIController() && GetBasePlayerClass())
	{
		UAIBlueprintHelperLibrary::SimpleMoveToActor(GetAIController(), GetBasePlayerClass());
	}
}


