// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseEnemy.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/WeaponDataAsset.h"
#include "Weapon/BaseWeaponComponent.h"
#include "Component/PathMovement.h"
#include "WaveSystem/Route/EnemyWaypointMoveComponent.h"
#include "EnemyAttributeSet.h"
#include "AI/EnemyPathActor.h"
#include "AI/EnemySpawnPoint.h"

// Core

// Unreal
#include "AbilitySystemComponent.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"


ABaseEnemy::ABaseEnemy()
{
	PrimaryActorTick.bCanEverTick = true;
	
	bReplicates = true;
	SetReplicateMovement(false);
	bAlwaysRelevant = true;
	NetUpdateFrequency = 30.0f;
	MinNetUpdateFrequency = 15.0f;
	
	// ASC
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	ASCReplicationMode = EGameplayEffectReplicationMode::Minimal;
	AbilitySystemComponent->SetReplicationMode(ASCReplicationMode);
	
	// GAS
	BasicAttributes = CreateDefaultSubobject<UEnemyAttributeSet>(TEXT("BasicAttributeSet"));

	// Component
	WeaponComponent = CreateDefaultSubobject<UBaseWeaponComponent>(TEXT("WeaponComponent"));
	PathMovement = CreateDefaultSubobject<UPathMovement>(TEXT("PathMovementComponent"));
	PathMovement->SetIsReplicated(true);

	WaypointMoveComponent = CreateDefaultSubobject<UEnemyWaypointMoveComponent>(TEXT("WaypointMoveComponent"));

	if (GetCharacterMovement())
	{
		GetCharacterMovement()->SetIsReplicated(false);
	}

	
	// State_Dead Tag를 감지하는 Delegate 등록
	AbilitySystemComponent->RegisterGameplayTagEvent(State_Dead)
		.AddUObject(this, &ABaseEnemy::OnDeadTagChanged);
}

void ABaseEnemy::BeginPlay()
{
	Super::BeginPlay();
	//AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// StartingAbilities 능력 등록
	if (AbilitySystemComponent && HasAuthority())
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);

		if (StartingAbilities.Num() > 0)
		{
			GrantAbilities(StartingAbilities);
		}
		// 무기 관리
		if (WeaponComponent && DefaultWeaponTag.IsValid())
		{
			WeaponComponent->InitializeLoadout(DefaultWeaponTag);
		}
	}
}

TArray<FGameplayAbilitySpecHandle> ABaseEnemy::GrantAbilities(TArray<TSubclassOf<UGameplayAbility>> AbilitiesToGrant)
{
	// 모든 능력을 for loop를 통해서 일일히 Grant 해줌
	// HasAuthority는 서버에 있는 지 확인하는 함수
	if (!AbilitySystemComponent || !HasAuthority())
		// GrantAbilities는 서버에서만 동작하므로, 서버에서 클라로 보내는 것은 충돌 일어날 수 있다. 따라서 서버에서만 동작하도록 한다.
	{
		return TArray<FGameplayAbilitySpecHandle>();
	}

	TArray<FGameplayAbilitySpecHandle> AbilitiesHandles;
	
	for (TSubclassOf<UGameplayAbility> Ability : AbilitiesToGrant)
	{
		if (!Ability)
			continue;
		
		FGameplayAbilitySpecHandle SpecHandle= AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec
			(Ability, 1, -1, this));
		
		AbilitiesHandles.Add(SpecHandle);
	}

	// SendAbilitiesChangedEvent();
	return AbilitiesHandles;
}

void ABaseEnemy::HandleDeath_Implementation()
{
	// 중복 Death 방지
	if (bDeathHandled)
	{
		return;
	}

	bDeathHandled = true;
	
	// 사망 시 Death 처리
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->DisableMovement();

	// 죽으면 경로 이동 중지
	if (HasAuthority())
	{
		// 죽으면 경로 이동 중지
		if (PathMovement)
		{
			PathMovement->StopPathMovement();
		}

		if (WaypointMoveComponent)
		{
			WaypointMoveComponent->StopRoute(true);
		}

		// V2 WaveSpawnManager가 이 Delegate를 받아 AliveEnemyCount를 감소시킨다.
		OnBaseEnemyDeathNotified.Broadcast(this, EWaveEnemyRemoveReason::Death);
	}

	
	// Actor의 뒤와 위로 Impulse를 줘서 날아가도록 한다.
	FVector Impulse = GetActorForwardVector() * -20000.f;
	Impulse.Z = 15000.f;
	GetMesh()->AddImpulseAtLocation(Impulse, GetActorLocation());
}

void ABaseEnemy::OnDeadTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	if (NewCount > 0)
	{
		// 죽었을 때
		HandleDeath();
	}
	else
	{
		// 캐릭터가 부활했을 때 처리할 로직을 여기에 작성
	}
}

void ABaseEnemy::InitializePathMovement(AEnemyPathActor* InPath, float InStartDistance, bool bStartImmediately)
{
	if (!HasAuthority() || !PathMovement)
	{
		return;
	}

	PathMovement->InitializePath(InPath, InStartDistance);

	if (bStartImmediately)
	{
		PathMovement->StartPathMovement();
	}
}

void ABaseEnemy::InitializePathMovementFromSpawnPoint(AEnemySpawnPoint* InSpawnPoint, bool bStartImmediately)
{
	if (!HasAuthority() || !PathMovement || !InSpawnPoint)
	{
		return;
	}

	InitializePathMovement(
		InSpawnPoint->GetAssignedPath(),
		InSpawnPoint->GetClampedStartDistance(),
		bStartImmediately
	);
}

FVector ABaseEnemy::GetVelocity() const
{
	// 1. Path를 따라 이동 중일 때
	if (PathMovement && PathMovement->IsPathMovementActive() && !PathMovement->HasReachedGoal())
	{
		// BasicAttributes가 유효하다면 GAS의 MoveSpeed를 사용하고, 아니면 기본 속도 사용
		float CurrentSpeed = BasicAttributes ? BasicAttributes->GetMoveSpeed() : PathMovement->GetCurrentMoveSpeed();
		UE_LOG(LogTemp, Warning, TEXT("%f"), CurrentSpeed);
		return GetActorForwardVector() * CurrentSpeed;
	}

	// 2. 그 외의 상황
	return Super::GetVelocity();
}


