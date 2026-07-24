#include "Skills/Abilities/GA_GravityVortexThrow.h"

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
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	ProjectileClass = AGravityVortexProjectile::StaticClass();
	SkillTag = GameplayAbility_Skill_GravityVortex;
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
	UE_LOG(LogTemp, Warning,
		TEXT("[GravityVortex][GA] ActivateAbility entered. Avatar=%s Authority=%s LocallyControlled=%s Handle=%s"),
		*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
		ActorInfo && ActorInfo->AvatarActor.IsValid() && ActorInfo->AvatarActor->HasAuthority()
			? TEXT("true") : TEXT("false"),
		ActorInfo && ActorInfo->IsLocallyControlled() ? TEXT("true") : TEXT("false"),
		*Handle.ToString());
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		UE_LOG(LogTemp, Error, TEXT("[GravityVortex][GA] CommitAbility failed."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !ProjectileClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GravityVortex][GA] Invalid activation data. Player=%s ProjectileClass=%s"),
			*GetNameSafe(Player),
			*GetPathNameSafe(ProjectileClass.Get()));
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
		UE_LOG(LogTemp, Warning, TEXT("[GravityVortex][GA] Waiting for left click."));
	}

	UAbilityTask_WaitGameplayEvent* RightClickTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_RightClick, nullptr, true, true);
	if (RightClickTask)
	{
		RightClickTask->EventReceived.AddDynamic(this, &UGA_GravityVortexThrow::OnRightClickPressed);
		RightClickTask->ReadyForActivation();
		UE_LOG(LogTemp, Warning, TEXT("[GravityVortex][GA] Waiting for right click cancel."));
	}

	UAbilityTask_WaitInputRelease* InputReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	if (InputReleaseTask)
	{
		InputReleaseTask->OnRelease.AddDynamic(this, &UGA_GravityVortexThrow::OnActivationInputReleased);
		InputReleaseTask->ReadyForActivation();
		UE_LOG(LogTemp, Warning, TEXT("[GravityVortex][GA] Waiting for skill-key release cancel."));
	}

	if (Player->IsLocallyControlled() && bDrawAimTrajectory)
	{
		GetWorld()->GetTimerManager().SetTimer(
			TrajectoryTimerHandle, this, &UGA_GravityVortexThrow::DrawAimTrajectory,
			FMath::Max(0.01f, TrajectoryRefreshInterval), true, 0.0f);
		UE_LOG(LogTemp, Warning,
			TEXT("[GravityVortex][GA] Aim trajectory timer started. Interval=%.3f"),
			FMath::Max(0.01f, TrajectoryRefreshInterval));
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GravityVortex][GA] Aim trajectory not drawn. Local=%s DrawEnabled=%s"),
			Player->IsLocallyControlled() ? TEXT("true") : TEXT("false"),
			bDrawAimTrajectory ? TEXT("true") : TEXT("false"));
	}
}

void UGA_GravityVortexThrow::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	UE_LOG(LogTemp, Warning,
		TEXT("[GravityVortex][GA] EndAbility. Avatar=%s Cancelled=%s ThrowRequested=%s"),
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		bWasCancelled ? TEXT("true") : TEXT("false"),
		bThrowRequested ? TEXT("true") : TEXT("false"));
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
	UE_LOG(LogTemp, Warning,
		TEXT("[GravityVortex][GA] Left click received. Active=%s ThrowRequested=%s Authority=%s"),
		IsActive() ? TEXT("true") : TEXT("false"),
		bThrowRequested ? TEXT("true") : TEXT("false"),
		GetAvatarActorFromActorInfo() && GetAvatarActorFromActorInfo()->HasAuthority()
			? TEXT("true") : TEXT("false"));
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
			if (!TryConsumeSkillUse())
			{
				UE_LOG(LogTemp, Error, TEXT("[GravityVortex][GA] Material consumption failed on server."));
				EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
				return;
			}
			SpawnProjectileOnServer();
			EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
		}
	}
}

void UGA_GravityVortexThrow::OnRightClickPressed(FGameplayEventData Payload)
{
	UE_LOG(LogTemp, Warning,
		TEXT("[GravityVortex][GA] Right click cancel received. Active=%s"),
		IsActive() ? TEXT("true") : TEXT("false"));
	if (IsActive() && !bThrowRequested)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}

void UGA_GravityVortexThrow::OnActivationInputReleased(float TimeHeld)
{
	UE_LOG(LogTemp, Warning,
		TEXT("[GravityVortex][GA] Skill key released. Active=%s TimeHeld=%.3f ThrowRequested=%s"),
		IsActive() ? TEXT("true") : TEXT("false"),
		TimeHeld,
		bThrowRequested ? TEXT("true") : TEXT("false"));
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
