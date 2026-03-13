// Fill out your copyright notice in the Description page of Project Settings.


#include "BaseCharacter.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "BaseGameplayTags.h"


// Sets default values
ABaseCharacter::ABaseCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Ability System Component 추가
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(ASCReplicationMode);

	GetCapsuleComponent()->InitCapsuleSize(35.f, 90.f);

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 500.f, 0.f);

	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;

	// Basic Attribute Set 추가
	// BasicAttributes = CreateDefaultSubobject<UBaseAttributeSet>(TEXT("BasicAttributeSet"));

	// ASC Owner가 State.Dead tag를 가질 때, OnDeadTagChanged함수를 Call
	AbilitySystemComponent->RegisterGameplayTagEvent(State_Dead)
		.AddUObject(this, &ABaseCharacter::OnDeadTagChanged);
}


void ABaseCharacter::BeginPlay()
{
	Super::BeginPlay();
	
}

void ABaseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
		
}

void ABaseCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

UAbilitySystemComponent* ABaseCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

TArray<FGameplayAbilitySpecHandle> ABaseCharacter::GrantAbilities(
	TArray<TSubclassOf<UGameplayAbility>> AbilitiesToGrant)
{
	// 모든 능력을 for loop를 통해서 일일히 Grant 해줌
	if (!AbilitySystemComponent || !HasAuthority())// HasAuthority는 서버에 있는 지 확인하는 함수
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

	SendAbilitiesChangedEvent();
	return AbilitiesHandles;
}

void ABaseCharacter::RemoveAbilities(TArray<FGameplayAbilitySpecHandle> AbilityHandlesToRemove)
{
	if (!AbilitySystemComponent && !HasAuthority())
	{
		return;
	}

	for (FGameplayAbilitySpecHandle Abilityhandle : AbilityHandlesToRemove)
	{
		AbilitySystemComponent->ClearAbility(Abilityhandle);
	}

	SendAbilitiesChangedEvent();
}

void ABaseCharacter::SendAbilitiesChangedEvent()
{
	FGameplayEventData EventData;
	EventData.EventTag = Event_Ability_Changed;
	EventData.Instigator = this;
	EventData.Target = this;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(this, EventData.EventTag, EventData);
}

void ABaseCharacter::ServerSendGameplayEventToSelf_Implementation(FGameplayEventData EventData)
{
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(this, EventData.EventTag, EventData);
}

void ABaseCharacter::HandleDeath_Implementation()
{
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->DisableMovement();

	FVector Impulse = GetActorForwardVector() * -20000.f;
	Impulse.Z = 15000.f;
	GetMesh()->AddImpulseAtLocation(Impulse, GetActorLocation());
}

void ABaseCharacter::OnDeadTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
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