#include "Skills/Abilities/GA_GravityVortexThrow.h"

#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Projectiles/GravityVortexProjectile.h"
#include "Skills/VortexAimLine.h"

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

	UAbilityTask_WaitGameplayEvent* RightClickTask =
		UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, Key_Default_Mouse_RightClick, nullptr, true, true);
	if (RightClickTask)
	{
		RightClickTask->EventReceived.AddDynamic(this, &UGA_GravityVortexThrow::OnRightClickPressed);
		RightClickTask->ReadyForActivation();
	}

	UAbilityTask_WaitInputRelease* InputReleaseTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	if (InputReleaseTask)
	{
		InputReleaseTask->OnRelease.AddDynamic(this, &UGA_GravityVortexThrow::OnActivationInputReleased);
		InputReleaseTask->ReadyForActivation();
	}

	if (Player->IsLocallyControlled() && (bDrawAimTrajectory || bUpdateAimTrajectoryVisual))
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
	if (CurrentActorInfo && CurrentActorInfo->IsLocallyControlled() && bUpdateAimTrajectoryVisual)
	{
		K2_OnAimTrajectoryCleared();
	}
	if (AimLineActor)
	{
		AimLineActor->ClearTrajectory();
		AimLineActor->Destroy();
		AimLineActor = nullptr;
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
			if (!TryConsumeSkillUse())
			{
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
	if (IsActive() && !bThrowRequested)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
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
	Params.DrawDebugType = bDrawAimTrajectory ? EDrawDebugTrace::ForDuration : EDrawDebugTrace::None;
	Params.DrawDebugTime = FMath::Max(0.01f, TrajectoryRefreshInterval);

	FPredictProjectilePathResult Result;
	UGameplayStatics::PredictProjectilePath(this, Params, Result);
	if (bUpdateAimTrajectoryVisual)
	{
		TArray<FVector> WorldPoints;
		WorldPoints.Reserve(Result.PathData.Num());
		for (const FPredictProjectilePathPointData& Point : Result.PathData)
		{
			WorldPoints.Add(Point.Location);
		}
		if (AimLineClass && !IsValid(AimLineActor))
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Owner = Player;
			SpawnParameters.Instigator = Player;
			SpawnParameters.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AimLineActor = GetWorld()->SpawnActor<AVortexAimLine>(
				AimLineClass,
				Player->GetActorLocation(),
				FRotator::ZeroRotator,
				SpawnParameters);
		}
		if (AimLineActor)
		{
			AimLineActor->SetTrajectory(WorldPoints);
		}
		K2_OnAimTrajectoryUpdated(WorldPoints);
	}
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
	if (!SpawnSocketName.IsNone())
	{
		const USkeletalMeshComponent* SocketMesh = nullptr;
		FName ResolvedSocketName = SpawnSocketName;
		auto FindSocketMesh = [Player](FName SocketName) -> const USkeletalMeshComponent*
		{
			if (Player->GetMesh() && Player->GetMesh()->DoesSocketExist(SocketName))
			{
				return Player->GetMesh();
			}
			TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes(Player);
			for (const USkeletalMeshComponent* Candidate : SkeletalMeshes)
			{
				if (IsValid(Candidate) && Candidate->DoesSocketExist(SocketName))
				{
					return Candidate;
				}
			}
			return nullptr;
		};

		SocketMesh = FindSocketMesh(ResolvedSocketName);
		if (!SocketMesh && !FallbackSpawnBoneName.IsNone())
		{
			ResolvedSocketName = FallbackSpawnBoneName;
			SocketMesh = FindSocketMesh(ResolvedSocketName);
		}

		if (SocketMesh)
		{
			OutSpawnLocation = SocketMesh->GetSocketTransform(ResolvedSocketName, RTS_World).GetLocation();
		}
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
	}
}
