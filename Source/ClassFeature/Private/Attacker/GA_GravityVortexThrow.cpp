#include "Attacker/GA_GravityVortexThrow.h"

#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Projectiles/GravityVortexProjectile.h"

UGA_GravityVortexThrow::UGA_GravityVortexThrow()
{
	ProjectileClass = AGravityVortexProjectile::StaticClass();
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(GameplayAbility_Skill_GravityVortex);
	SetAssetTags(AssetTags);
}

void UGA_GravityVortexThrow::ActivateAbility(
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

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !ProjectileClass)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	bThrowRequested = false;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(State_Aiming);
		ASC->AddLooseGameplayTag(GameplayAbility_Skill_GravityVortex);
	}
	Player->StopSprint();

	UAbilityTask_WaitGameplayEvent* LeftClickTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_LeftClick, nullptr, true, true);
	if (LeftClickTask)
	{
		LeftClickTask->EventReceived.AddDynamic(this, &UGA_GravityVortexThrow::OnLeftClickPressed);
		LeftClickTask->ReadyForActivation();
	}

	UAbilityTask_WaitInputRelease* InputReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	if (InputReleaseTask)
	{
		InputReleaseTask->OnRelease.AddDynamic(this, &UGA_GravityVortexThrow::OnActivationInputReleased);
		InputReleaseTask->ReadyForActivation();
	}

	if (Player->IsLocallyControlled() && bDrawAimTrajectory)
	{
		GetWorld()->GetTimerManager().SetTimer(
			TrajectoryTimerHandle, this, &UGA_GravityVortexThrow::DrawAimTrajectory,
			FMath::Max(0.01f, TrajectoryRefreshInterval), true, 0.0f);
	}
}

void UGA_GravityVortexThrow::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(TrajectoryTimerHandle);
	}
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Aiming);
		ASC->RemoveLooseGameplayTag(GameplayAbility_Skill_GravityVortex);
	}
	bThrowRequested = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_GravityVortexThrow::OnLeftClickPressed(FGameplayEventData Payload)
{
	if (!IsActive() || bThrowRequested)
	{
		return;
	}

	bThrowRequested = true;
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(TrajectoryTimerHandle);
	}
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(State_Aiming);
	}

	if (ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo()))
	{
		if (Player->HasAuthority())
		{
			SpawnProjectileOnServer();
			EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
		}
	}
}

void UGA_GravityVortexThrow::OnActivationInputReleased(float TimeHeld)
{
	if (IsActive() && !bThrowRequested)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}

void UGA_GravityVortexThrow::DrawAimTrajectory()
{
	FVector SpawnLocation;
	FVector LaunchVelocity;
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !Player->IsLocallyControlled() || !GetLaunchData(SpawnLocation, LaunchVelocity))
	{
		return;
	}

	FPredictProjectilePathParams Params;
	Params.StartLocation = SpawnLocation;
	Params.LaunchVelocity = LaunchVelocity;
	Params.ProjectileRadius = 5.0f;
	Params.MaxSimTime = 3.0f;
	Params.SimFrequency = 20.0f;
	Params.bTraceWithCollision = false;
	Params.DrawDebugType = EDrawDebugTrace::ForDuration;
	Params.DrawDebugTime = FMath::Max(0.01f, TrajectoryRefreshInterval);

	FPredictProjectilePathResult Result;
	UGameplayStatics::PredictProjectilePath(this, Params, Result);
}

bool UGA_GravityVortexThrow::GetLaunchData(FVector& OutSpawnLocation, FVector& OutLaunchVelocity) const
{
	const ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player)
	{
		return false;
	}

	FVector AimDirection = Player->GetBaseAimRotation().Vector();
	AimDirection.Z += UpwardAimBias;
	AimDirection.Normalize();

	OutSpawnLocation = Player->GetActorLocation()
		+ Player->GetActorForwardVector() * SpawnForwardOffset
		+ FVector::UpVector * SpawnVerticalOffset;
	if (!SpawnSocketName.IsNone() && Player->GetMesh() && Player->GetMesh()->DoesSocketExist(SpawnSocketName))
	{
		OutSpawnLocation = Player->GetMesh()->GetSocketLocation(SpawnSocketName);
	}

	OutLaunchVelocity = AimDirection * FMath::Max(1.0f, ThrowSpeed);
	return true;
}

void UGA_GravityVortexThrow::SpawnProjectileOnServer()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	FVector SpawnLocation;
	FVector LaunchVelocity;
	if (!Player || !Player->HasAuthority() || !GetLaunchData(SpawnLocation, LaunchVelocity))
	{
		return;
	}

	const FTransform SpawnTransform(LaunchVelocity.Rotation(), SpawnLocation);
	AGravityVortexProjectile* Projectile = GetWorld()->SpawnActorDeferred<AGravityVortexProjectile>(
		ProjectileClass, SpawnTransform, Player, Player, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Projectile)
	{
		Projectile->FinishSpawning(SpawnTransform);
		Projectile->LaunchProjectile(LaunchVelocity);
		UE_LOG(LogTemp, Log, TEXT("[GRAVITY-VORTEX] Projectile spawned by %s at %s"),
			*GetNameSafe(Player), *SpawnLocation.ToString());
	}
}
