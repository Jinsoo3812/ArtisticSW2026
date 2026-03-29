// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseEnemy.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/WeaponDataAsset.h"
#include "Weapon/BaseWeaponComponent.h"

// ArtisticSWCore
#include "BaseItem.h"

// Enemy Folder
#include "BaseAIController.h"
#include "EnemyAttributeSet.h"

// Unreal
#include "AbilitySystemComponent.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

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

	InitializeEnemyDropData();
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
		Drop();
		// 죽었을 때
		HandleDeath();
	}
	else
	{
		// 캐릭터가 부활했을 때 처리할 로직을 여기에 작성
	}
}

void ABaseEnemy::InitializeEnemyDropData()
{
	EnemyDropData = FEnemyDropData();

	if (!EnemyDropDataTable || !EnemyTypeTag.IsValid())
	{
		return;
	}

	
	static const FString ContextString(TEXT("EnemyDropData"));
	TArray<FEnemyDropDataRow*> Rows;
	EnemyDropDataTable->GetAllRows(ContextString, Rows);

	// 전체 데이터중 해당하는 Row 검색 후 데이터 가져옴
	for (const FEnemyDropDataRow* Row : Rows)
	{
		if (!Row || Row->EnemyTag != EnemyTypeTag)
		{
			continue;
		}

		EnemyDropData.EnemyTag = Row->EnemyTag;
		EnemyDropData.DropItemCount = Row->DropItemCount;

		auto AddEntry = [this](const FGameplayTag& ItemTag, float Chance)
			{
				if (ItemTag.IsValid() && Chance > 0.f)
				{
					FEnemyDropEntry Entry;
					Entry.ItemTag = ItemTag;
					Entry.DropChance = Chance;
					EnemyDropData.DropEntries.Add(Entry);
				}
			};

		AddEntry(Row->ItemTag_1, Row->DropChance_1);
		AddEntry(Row->ItemTag_2, Row->DropChance_2);
		AddEntry(Row->ItemTag_3, Row->DropChance_3);
		break;
	}
}

void ABaseEnemy::Drop()
{
	if (!HasAuthority() || bHasDropped)
	{
		return;
	}

	bHasDropped = true;

	for (const FEnemyDropEntry& Entry : EnemyDropData.DropEntries)
	{
		if (!Entry.ItemTag.IsValid())
		{
			continue;
		}

		if (FMath::FRand() > Entry.DropChance)
		{
			continue;
		}

		const FVector SpawnLoc =
			GetActorLocation()
			+ FVector(FMath::RandRange(-50.f, 50.f), FMath::RandRange(-50.f, 50.f), 30.f);

		const FTransform SpawnTransform(GetActorRotation(), SpawnLoc);

		// 디퍼드 스폰을 하여 BaseItem 스폰하고, 태그 전달
		ABaseItem* SpawnedItem =
			GetWorld()->SpawnActorDeferred<ABaseItem>(
				ABaseItem::StaticClass(),
				SpawnTransform,
				this,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

		if (!SpawnedItem)
		{
			continue;
		}

		SpawnedItem->ItemTag = Entry.ItemTag;
		UGameplayStatics::FinishSpawningActor(SpawnedItem, SpawnTransform);
	}
}