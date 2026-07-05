#include "Attacker/GA_BowAimFire.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Item/Components/BowComponent.h"
#include "Item/Projectiles/ArrowProjectile.h"
#include "Item/Weapons/BowItem.h"

UGA_BowAimFire::UGA_BowAimFire()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UGA_BowAimFire::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CacheBowFromAvatar())
	{
		UE_LOG(LogTemp, Warning, TEXT("UGA_BowAimFire::ActivateAbility : Equipped item is not a valid bow."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(State_Aiming);
	}

	CachedBowComponent->SetAiming(true);
	CachedBowComponent->SetDrawAlpha(0.0f);

	UAbilityTask_WaitInputRelease* WaitRightReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	if (WaitRightReleaseTask)
	{
		WaitRightReleaseTask->OnRelease.AddDynamic(this, &UGA_BowAimFire::OnRightClickReleased);
		WaitRightReleaseTask->ReadyForActivation();
	}

	UAbilityTask_WaitGameplayEvent* WaitLeftPressedTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_LeftClick, nullptr, true, true);
	if (WaitLeftPressedTask)
	{
		WaitLeftPressedTask->EventReceived.AddDynamic(this, &UGA_BowAimFire::OnLeftClickPressed);
		WaitLeftPressedTask->ReadyForActivation();
	}

	UAbilityTask_WaitGameplayEvent* WaitLeftReleasedTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_LeftClick_Released, nullptr, true, true);
	if (WaitLeftReleasedTask)
	{
		WaitLeftReleasedTask->EventReceived.AddDynamic(this, &UGA_BowAimFire::OnLeftClickReleased);
		WaitLeftReleasedTask->ReadyForActivation();
	}
}

void UGA_BowAimFire::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	ResetBowState();

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Aiming);
		ASC->RemoveLooseGameplayTag(State_Attacking);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_BowAimFire::OnLeftClickPressed(FGameplayEventData Payload)
{
	if (!IsActive() || bIsDrawing || !CachedBowComponent)
	{
		return;
	}

	bIsDrawing = true;
	DrawStartTime = GetWorld()->GetTimeSeconds();
	CachedBowComponent->SetDrawAlpha(0.0f);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(State_Attacking);
	}

	GetWorld()->GetTimerManager().SetTimer(
		ChargeTimerHandle,
		this,
		&UGA_BowAimFire::UpdateDrawAlpha,
		ChargeTickRate,
		true);
}

void UGA_BowAimFire::OnLeftClickReleased(FGameplayEventData Payload)
{
	if (!IsActive() || !bIsDrawing || !CachedBowComponent)
	{
		return;
	}

	UpdateDrawAlpha();
	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);

	const float FinalDrawAlpha = CachedBowComponent->GetDrawAlpha();
	if (FinalDrawAlpha >= MinDrawAlphaToFire)
	{
		FireArrow();
	}

	bIsDrawing = false;
	CachedBowComponent->SetDrawAlpha(0.0f);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Attacking);
	}
}

void UGA_BowAimFire::OnRightClickReleased(float TimeHeld)
{
	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
}

void UGA_BowAimFire::UpdateDrawAlpha()
{
	if (!CachedBowComponent)
	{
		return;
	}

	const float HeldTime = GetWorld()->GetTimeSeconds() - DrawStartTime;
	const float DrawAlpha = FMath::Clamp(HeldTime / MaxChargeTime, 0.0f, 1.0f);
	CachedBowComponent->SetDrawAlpha(DrawAlpha);
}

void UGA_BowAimFire::FireArrow()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !Player->HasAuthority() || !CachedBow || !CachedBowComponent)
	{
		return;
	}

	UClass* SpawnClass = CachedBow->GetSpawnClass();
	if (!SpawnClass || !SpawnClass->IsChildOf(AArrowProjectile::StaticClass()))
	{
		UE_LOG(LogTemp, Warning, TEXT("UGA_BowAimFire::FireArrow : Bow SpawnClass must derive from AArrowProjectile."));
		return;
	}

	const FVector LaunchDirection = Player->GetBaseAimRotation().Vector();
	const float FireSpeed = CachedBowComponent->GetCurrentFireSpeed();
	const FVector LaunchVelocity = LaunchDirection * FireSpeed;

	FTransform SpawnTransform = CachedBowComponent->BuildArrowSpawnTransform();
	SpawnTransform.SetRotation(LaunchDirection.Rotation().Quaternion());

	AArrowProjectile* Arrow = GetWorld()->SpawnActorDeferred<AArrowProjectile>(
		SpawnClass,
		SpawnTransform,
		Player,
		Player,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!Arrow)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		const float DrawAlpha = CachedBowComponent->GetDrawAlpha();
		const float ChargeDamageMultiplier = FMath::Lerp(MinChargeDamageMultiplier, MaxChargeDamageMultiplier, DrawAlpha);
		Arrow->InitializeDamage(ASC, Player, ChargeDamageMultiplier);
	}

	Arrow->SetOwner(Player);
	Arrow->SetInstigator(Player);
	Arrow->FinishSpawning(SpawnTransform);
	Arrow->LaunchArrow(LaunchVelocity);
	CachedBow->Multicast_PlayReleaseFX();
}

void UGA_BowAimFire::ResetBowState()
{
	GetWorld()->GetTimerManager().ClearTimer(ChargeTimerHandle);
	bIsDrawing = false;

	if (CachedBowComponent)
	{
		CachedBowComponent->SetDrawAlpha(0.0f);
		CachedBowComponent->SetAiming(false);
	}
}

bool UGA_BowAimFire::CacheBowFromAvatar()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !IsValid(Player->EquippedItem))
	{
		CachedBow = nullptr;
		CachedBowComponent = nullptr;
		return false;
	}

	CachedBow = Cast<ABowItem>(Player->EquippedItem);
	CachedBowComponent = CachedBow ? CachedBow->GetBowComponent() : nullptr;

	return CachedBow && CachedBowComponent;
}
