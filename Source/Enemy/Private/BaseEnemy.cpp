// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseEnemy.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/WeaponDataAsset.h"
#include "Weapon/BaseWeaponComponent.h"

// Enemy Folder
#include "BaseAIController.h"
#include "EnemyAttributeSet.h"

// Unreal
#include "AbilitySystemComponent.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ABaseEnemy::ABaseEnemy()
{
	PrimaryActorTick.bCanEverTick = true;
	// ASC
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	ASCReplicationMode = EGameplayEffectReplicationMode::Minimal;
	AbilitySystemComponent->SetReplicationMode(ASCReplicationMode);
	
	// GAS
	BasicAttributes = CreateDefaultSubobject<UEnemyAttributeSet>(TEXT("BasicAttributeSet"));

	// Component
	WeaponComponent = CreateDefaultSubobject<UBaseWeaponComponent>(TEXT("WeaponComponent"));

	// State_Dead Tag를 감지하는 Delegate 등록
	AbilitySystemComponent->RegisterGameplayTagEvent(State_Dead)
		.AddUObject(this, &ABaseEnemy::OnDeadTagChanged);
}

void ABaseEnemy::BeginPlay()
{
	Super::BeginPlay();

	// AIController 변수 Cast해주기
	AIController = Cast<ABaseAIController>(UAIBlueprintHelperLibrary::GetAIController(this));
	if (AIController && BehaviorTree)
	{
		AIController->RunBehaviorTree(BehaviorTree);
	}

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
		FGameplayAbilitySpecHandle SpecHandle= AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec
			(Ability, 1, -1, this));
		
		AbilitiesHandles.Add(SpecHandle);
	}

	// SendAbilitiesChangedEvent();
	return AbilitiesHandles;
}

void ABaseEnemy::HandleDeath_Implementation()
{
	// 사망 시 Death 처리
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->DisableMovement();
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




