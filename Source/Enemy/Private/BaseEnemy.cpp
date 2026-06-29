// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseEnemy.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/WeaponDataAsset.h"
#include "Weapon/BaseWeaponComponent.h"

// ArtisticSWCore
#include "ItemSubsystem.h"

// Enemy Folder
#include "AI/BaseAIController.h"
#include "GAS/EnemyAttributeSet.h"
#include "WaveSystem/Route/EnemyWaypointMoveComponent.h"

// Unreal
#include "AbilitySystemComponent.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Components/BaseHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

ABaseEnemy::ABaseEnemy()
{
	PrimaryActorTick.bCanEverTick = true;
	
	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = true;

	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(15.0f);
	
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	
	// ASC
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	ASCReplicationMode = EGameplayEffectReplicationMode::Minimal;
	AbilitySystemComponent->SetReplicationMode(ASCReplicationMode);
	
	// GAS
	BasicAttributes = CreateDefaultSubobject<UEnemyAttributeSet>(TEXT("BasicAttributeSet"));

	// Component
	WeaponComponent = CreateDefaultSubobject<UBaseWeaponComponent>(TEXT("WeaponComponent"));
	WaypointMoveComponent = CreateDefaultSubobject<UEnemyWaypointMoveComponent>(TEXT("WaypointMoveComponent"));
	HealthComponent = CreateDefaultSubobject<UBaseHealthComponent>(TEXT("HealthComponent"));

	if (GetCharacterMovement())
		GetCharacterMovement()->SetIsReplicated(true);
}

void ABaseEnemy::BeginPlay()
{
	Super::BeginPlay();

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
		if (HealthComponent)
		{
			HealthComponent->OnDeathStarted.AddUniqueDynamic(this, &ABaseEnemy::OnDeathStarted);
			HealthComponent->InitializeWithAbilitySystem(AbilitySystemComponent);
		}
	}

	// StartingAbilities 능력 등록
	if (AbilitySystemComponent && HasAuthority())
	{
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

void ABaseEnemy::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HealthComponent)
	{
		HealthComponent->OnDeathStarted.RemoveDynamic(this, &ABaseEnemy::OnDeathStarted);
		HealthComponent->UninitializeFromAbilitySystem();
	}

	Super::EndPlay(EndPlayReason);
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


void ABaseEnemy::NotifyRemovedFromWaveOnce(EWaveEnemyRemoveReason Reason)
{
	if (!HasAuthority() || bWaveRemoveNotified)
	{
		return;
	}

	bWaveRemoveNotified = true;

	if (WaypointMoveComponent)
	{
		WaypointMoveComponent->StopRoute(true);
	}

	OnBaseEnemyDeathNotified.Broadcast(this, Reason);
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

void ABaseEnemy::OnDeathStarted(UBaseHealthComponent* InHealthComponent)
{
	if (!bDeathHandled)
	{
		bDeathHandled = true;

		if (HasAuthority())
		{
			NotifyRemovedFromWaveOnce(EWaveEnemyRemoveReason::Death);
			Drop();
		}

		HandleDeath();
	}
}

void ABaseEnemy::InitializeFromWaveSpawn(float HealthMultiplier, float SpeedMultiplier, int32 EnemyLevel)
{
	if (!HasAuthority())
	{
		return;
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed *= FMath::Max(0.01f, SpeedMultiplier);
	}

	// 이후 AttributeSet 또는 GameplayEffect를 사용하여
	// HealthMultiplier와 EnemyLevel을 실제 스탯에 반영
}

void ABaseEnemy::InitializeEnemyDropData()
{
	// 데이터 테이블 전체를 가져옴
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
		// 구조체 Tag랑 BaseEnemy TypeTag가 같을 때 아래 실행

		EnemyDropData.EnemyTag = Row->EnemyTag;
		EnemyDropData.DropItemCount = Row->DropItemCount;

		// 드랍해야 하는 아이템 하나마다 Entry 구조체 하나를 가지게 됨
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

		// Entry 구조체를 EnemyDropData 구조체에 전부 저장
		// 드랍 할 정보는 EnemyDropData가 가지고 있음
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
	UE_LOG(LogTemp, Warning, TEXT("Drop Called"));

	bHasDropped = true;

	// DropData 구조체에 저장된 Entry(드랍템 개수)만큼 반복
	for (const FEnemyDropEntry& Entry : EnemyDropData.DropEntries)
	{
		if (!Entry.ItemTag.IsValid())
		{
			continue;
		}

		// 랜덤 수가 확률 이하일 때 드랍
		if (FMath::FRand() > Entry.DropChance)
		{
			continue;
		}

		// 아이템 서브시스템 생성
		UWorld* World = GetWorld();
		UItemSubsystem* ItemSubsystem = World->GetSubsystem<UItemSubsystem>();

		const FVector SpawnLoc =
			GetActorLocation()
			+ FVector(FMath::RandRange(-50.f, 50.f), FMath::RandRange(-50.f, 50.f), 30.f);

		const FTransform SpawnTransform(GetActorRotation(), SpawnLoc);

		// 스폰
		ABaseItem* SpawnedItem =
			ItemSubsystem->SpawnItem(Entry.ItemTag, SpawnTransform, EItemState::Dropped_Simulating, this);

		if (SpawnedItem)
		{
			UE_LOG(LogTemp, Warning, TEXT("Dropped"));
		}
	}
}
