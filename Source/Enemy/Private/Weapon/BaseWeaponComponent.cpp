// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/BaseWeaponComponent.h"
#include "BaseEnemy.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/WeaponDataAsset.h"

// Unreal
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Net/UnrealNetwork.h"


UBaseWeaponComponent::UBaseWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}


void UBaseWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
}

ABaseEnemy* UBaseWeaponComponent::GetOwningEnemy() const
{
	// Owner Cast
	return Cast<ABaseEnemy>(GetOwner());
}

const FWeaponDefinition* UBaseWeaponComponent::ResolveWeaponDefinition(FGameplayTag InTag) const
{
	if (!WeaponRegistry || !InTag.IsValid())
	{
		return nullptr;
	}

	return WeaponRegistry->FindWeaponDefinitionByTag(InTag);
}

const FWeaponDefinition* UBaseWeaponComponent::GetCurrentWeaponDefinition() const
{
	return ResolveWeaponDefinition(CurrentWeaponTag);
}

void UBaseWeaponComponent::InitializeLoadout(FGameplayTag InWeaponTag)
{
	InitializeLoadoutInternal(InWeaponTag, true);
}

void UBaseWeaponComponent::InitializeHolsteredLoadout(FGameplayTag InWeaponTag)
{
	InitializeLoadoutInternal(InWeaponTag, false);
}

void UBaseWeaponComponent::InitializeLoadoutInternal(FGameplayTag InWeaponTag, bool bEquipImmediately)
{
	ABaseEnemy* OwnerEnemy = GetOwningEnemy();
	// Owner가 없거나, 서버가 아니라면 return
	if (!OwnerEnemy || !OwnerEnemy->HasAuthority())
	{
		return;
	}
	// WeaponDA가 설정되지 않았다면 return
	if (!WeaponRegistry || CurrentWeapon)
	{
		return;
	}

	const FWeaponDefinition* WeaponDef = ResolveWeaponDefinition(InWeaponTag);
	if (!WeaponDef || !WeaponDef->WeaponActorClass)
	{
		return;
	}

	// 생성할 무기의 정보
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerEnemy;
	SpawnParams.Instigator = OwnerEnemy;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// CurrentWeapon에 무기 소환
	CurrentWeapon = OwnerEnemy->GetWorld()->SpawnActor<ABaseWeapon>(
		WeaponDef->WeaponActorClass,
		OwnerEnemy->GetActorLocation(),
		OwnerEnemy->GetActorRotation(),
		SpawnParams
	);
	if (!CurrentWeapon)
	{
		return;
	}
	// CurrentWeapon 변수의 Owner와 WeaponData를 Set해주기
	CurrentWeaponTag = InWeaponTag;
	CurrentWeapon->SetOwner(OwnerEnemy);
	// 무기의 초기 상태 지정
	WeaponState = EEnemyWeaponState::Holstered;
	if (bEquipImmediately)
	{
		EquipCurrentWeapon();
	}
	else
	{
		AttachWeaponToBack();
	}
}

float UBaseWeaponComponent::GetCurrentAttackRange() const
{
	const FWeaponDefinition* WeaponDefinition = GetCurrentWeaponDefinition();
	return WeaponDefinition ? FMath::Max(0.0f, WeaponDefinition->CombatData.AttackRange) : 0.0f;
}

void UBaseWeaponComponent::EquipCurrentWeapon()
{
	ABaseEnemy* OwnerEnemy = GetOwningEnemy();
	// 서버가 아니거나, Owner가 없거나, 무기가 없다면 return
	if (!OwnerEnemy || !OwnerEnemy->HasAuthority() || !CurrentWeapon)
	{
		return;
	}
	// 이미 장작한 상태라면 return
	if (WeaponState == EEnemyWeaponState::Equipped)
	{
		return;
	}
	// 무기를 장착하고 무기 상태를 바꾼다
	AttachWeaponToEquipSocket();
	WeaponState = EEnemyWeaponState::Equipped;
	// Weapon의 Ability를 부여
	GrantWeaponAbilities();
}

void UBaseWeaponComponent::UnequipCurrentWeapon()
{
	ABaseEnemy* OwnerEnemy = GetOwningEnemy();
	// 서버가 아니거나, Owner가 없거나, 무기가 없다면 return
	if (!OwnerEnemy || !OwnerEnemy->HasAuthority() || !CurrentWeapon)
	{
		return;
	}
	// 이미 장작 해제된 상태라면
	if (WeaponState == EEnemyWeaponState::Holstered)
	{
		return;
	}
	// Weapon에서 부여받은 Ability를 제거
	ClearWeaponAbilities();
	// BackSocket으로 무기 이관
	AttachWeaponToBack();
	// 무기 상태 변경
	WeaponState = EEnemyWeaponState::Holstered;
}

// 사용하지 않는 함수
/*void UBaseWeaponComponent::DestroyCurrentWeapon()
{
	ABaseEnemy* OwnerEnemy = GetOwningEnemy();
	// 서버가 아니거나, Owner가 없다면 return
	if (!OwnerEnemy || !OwnerEnemy->HasAuthority())
	{
		return;
	}
	// 확실하게 Weapon에서 부여된 GA제거
	ClearWeaponAbilities();

	// 현재 Weapon을 소멸
	if (CurrentWeapon)
	{
		CurrentWeapon->Destroy();
		CurrentWeapon = nullptr;
	}
	// 무기 상태 초기화
	WeaponState = EEnemyWeaponState::None;
}*/

void UBaseWeaponComponent::AttachWeaponToSocket(const FName& SocketName)
{
	ABaseEnemy* OwnerEnemy = GetOwningEnemy();
	// Owner, 무기가 없다면 return
	if (!OwnerEnemy || !CurrentWeapon || !OwnerEnemy->GetMesh())
	{
		return;
	}
	// 현재 무기를 SocketName에 부착
	CurrentWeapon->AttachToComponent(
		OwnerEnemy->GetMesh(),
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		SocketName
	);
}

// 등에 무기를 부착하는 AttachWeaponToSocket함수를 Call
void UBaseWeaponComponent::AttachWeaponToBack()
{
	const FWeaponDefinition* WeaponDef = GetCurrentWeaponDefinition();
	if (!CurrentWeapon || !WeaponDef)
	{
		return;
	}

	AttachWeaponToSocket(WeaponDef->SocketData.BackSocketName);
}

// EquipSocket에 무기 부착하는 AttachWeaponToSocket함수를 Call
void UBaseWeaponComponent::AttachWeaponToEquipSocket()
{
	const FWeaponDefinition* WeaponDef = GetCurrentWeaponDefinition();
	if (!CurrentWeapon || !WeaponDef)
	{
		return;
	}

	AttachWeaponToSocket(WeaponDef->SocketData.EquipSocketName);
}

// 무기 상태에 따라 올바른 위치로 Attach함수 Call
void UBaseWeaponComponent::SyncWeaponAttachment()
{
	if (!CurrentWeapon)
	{
		return;
	}
	
	switch (WeaponState)
	{
	case EEnemyWeaponState::Holstered:
		AttachWeaponToBack();
		break;

	case EEnemyWeaponState::Equipped:
		AttachWeaponToEquipSocket();
		break;

	default:
		break;
	}
}

void UBaseWeaponComponent::GrantWeaponAbilities()
{
	ABaseEnemy* OwnerEnemy = GetOwningEnemy();
	const FWeaponDefinition* WeaponDef = GetCurrentWeaponDefinition();
	
	if (!OwnerEnemy || !OwnerEnemy->HasAuthority() || !CurrentWeapon || !WeaponDef)
	{
		return;
	}

	UAbilitySystemComponent* ASC = OwnerEnemy->GetAbilitySystemComponent();
	if (!ASC || GrantedAbilityHandles.Num() > 0)
	{
		return;
	}

	// 무기에 있는 Ability를 ASC에 부여하고, 부여한 Ability의 Handle을 GrantedAbilityHandles에 저장
	for (const FGrantedWeaponAbility& AbilityInfo : WeaponDef->AbilityData.GrantedAbilities)
	{
		if (!AbilityInfo.AbilityClass)
		{
			continue;
		}

		FGameplayAbilitySpec AbilitySpec(AbilityInfo.AbilityClass, AbilityInfo.AbilityLevel, INDEX_NONE, CurrentWeapon);

		if (AbilityInfo.InputTag.IsValid())
		{
			AbilitySpec.GetDynamicSpecSourceTags().AddTag(AbilityInfo.InputTag);
		}

		const FGameplayAbilitySpecHandle SpecHandle = ASC->GiveAbility(AbilitySpec);
		GrantedAbilityHandles.Add(SpecHandle);
		// UE_LOG(LogTemp, Warning, TEXT("GrantedAbilityHandles"));
	}
}

void UBaseWeaponComponent::ClearWeaponAbilities()
{
	ABaseEnemy* OwnerEnemy = GetOwningEnemy();
	if (!OwnerEnemy || !OwnerEnemy->HasAuthority())
	{
		return;
	}

	UAbilitySystemComponent* ASC = OwnerEnemy->GetAbilitySystemComponent();
	if (!ASC)
	{
		return;
	}
	// 앞서 GrantWeaponAbilities함수에서 부여한 Ability를 제거
	for (const FGameplayAbilitySpecHandle& Handle : GrantedAbilityHandles)
	{
		if (Handle.IsValid())
		{
			ASC->ClearAbility(Handle);
		}
	}
	// GrantedAbilityHandles 초기화
	GrantedAbilityHandles.Empty();
}

void UBaseWeaponComponent::OnRep_CurrentWeaponTag()
{
	SyncWeaponAttachment();
}

void UBaseWeaponComponent::OnRep_CurrentWeapon()
{
	// 무기가 바뀌었을 때, 무기 상태에 맞게 부착 위치를 동기화
	SyncWeaponAttachment();
}

void UBaseWeaponComponent::OnRep_WeaponState()
{
	// 무기 상태가 바뀌었을 때, 무기 상태에 맞게 부착 위치를 동기화
	SyncWeaponAttachment();
}

void UBaseWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UBaseWeaponComponent, CurrentWeaponTag);
	DOREPLIFETIME(UBaseWeaponComponent, CurrentWeapon);
	DOREPLIFETIME(UBaseWeaponComponent, WeaponState);
}
