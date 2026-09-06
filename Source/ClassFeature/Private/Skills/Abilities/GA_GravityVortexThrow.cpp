#include "Skills/Abilities/GA_GravityVortexThrow.h"

#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "BasePlayer.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
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
	// Keep the two hold-to-aim skills mutually exclusive regardless of which
	// one was activated first. Area Slow already owns the reciprocal block.
	ActivationBlockedTags.AddTag(State_Aiming);
}

void UGA_GravityVortexThrow::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	bLoggedLaunchResolution = false;
	bLoggedAimLineResolution = false;
	UE_LOG(LogTemp, Warning,
		TEXT("[VortexPipeline][Activate] Ability=%s Avatar=%s Local=%s Authority=%s "
			"Socket=%s Fallback=%s Debug=%s Visual=%s AimLineClass=%s ProjectileClass=%s"),
		*GetPathNameSafe(GetClass()),
		*GetPathNameSafe(GetAvatarActorFromActorInfo()),
		ActorInfo && ActorInfo->IsLocallyControlled() ? TEXT("true") : TEXT("false"),
		ActorInfo && ActorInfo->IsNetAuthority() ? TEXT("true") : TEXT("false"),
		*SpawnSocketName.ToString(),
		*FallbackSpawnBoneName.ToString(),
		bDrawAimTrajectory ? TEXT("true") : TEXT("false"),
		bUpdateAimTrajectoryVisual ? TEXT("true") : TEXT("false"),
		*GetPathNameSafe(AimLineClass.Get()),
		*GetPathNameSafe(ProjectileClass.Get()));

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		UE_LOG(LogTemp, Error, TEXT("[VortexPipeline][Activate] CommitAbility failed."));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	if (!Player || !ProjectileClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[VortexPipeline][Activate] Invalid Player=%s or ProjectileClass=%s."),
			*GetPathNameSafe(Player), *GetPathNameSafe(ProjectileClass.Get()));
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
		UE_LOG(LogTemp, Warning,
			TEXT("[VortexPipeline][Activate] Starting trajectory timer interval=%.3f."),
			FMath::Max(0.01f, TrajectoryRefreshInterval));
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
	Params.MaxSimTime = FMath::Max(0.1f, TrajectoryMaxSimulationTime);
	Params.SimFrequency = FMath::Clamp(TrajectorySimulationFrequency, 2.0f, 30.0f);
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
		if (!AimLineClass)
		{
			if (!bLoggedAimLineResolution)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[VortexPipeline][AimLine] AimLineClass is None. "
						"The active ability class is %s."),
					*GetPathNameSafe(GetClass()));
				bLoggedAimLineResolution = true;
			}
		}
		else if (!IsValid(AimLineActor))
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
			UE_LOG(LogTemp, Warning,
				TEXT("[VortexPipeline][AimLine] Spawn class=%s result=%s mesh=%s material=%s."),
				*GetPathNameSafe(AimLineClass.Get()),
				*GetPathNameSafe(AimLineActor),
				AimLineActor ? *GetPathNameSafe(AimLineActor->AimLineMesh) : TEXT("None"),
				AimLineActor ? *GetPathNameSafe(AimLineActor->AimLineMaterial) : TEXT("None"));
			bLoggedAimLineResolution = true;
		}
		if (AimLineActor)
		{
			AimLineActor->SetTrajectory(WorldPoints);
		}
		else if (!bLoggedAimLineResolution)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[VortexPipeline][AimLine] Actor was not created for class=%s."),
				*GetPathNameSafe(AimLineClass.Get()));
			bLoggedAimLineResolution = true;
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
	const FVector OffsetFallbackLocation = OutSpawnLocation;
	const bool bShouldLogResolution = !bLoggedLaunchResolution;
	bLoggedLaunchResolution = true;

	TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes(Player);
	if (bShouldLogResolution)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[VortexPipeline][Launch] Player=%s ActorLocation=%s RequestedSocket=%s "
				"SkeletalMeshComponents=%d"),
			*GetPathNameSafe(Player),
			*Player->GetActorLocation().ToCompactString(),
			*SpawnSocketName.ToString(),
			SkeletalMeshes.Num());
		for (const USkeletalMeshComponent* Candidate : SkeletalMeshes)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[VortexPipeline][Launch] MeshComponent=%s Asset=%s Registered=%s "
					"Visible=%s RequestedExists=%s FallbackExists=%s"),
				*GetPathNameSafe(Candidate),
				Candidate ? *GetPathNameSafe(Candidate->GetSkeletalMeshAsset()) : TEXT("None"),
				Candidate && Candidate->IsRegistered() ? TEXT("true") : TEXT("false"),
				Candidate && Candidate->IsVisible() ? TEXT("true") : TEXT("false"),
				Candidate && Candidate->DoesSocketExist(SpawnSocketName) ? TEXT("true") : TEXT("false"),
				Candidate && Candidate->DoesSocketExist(FallbackSpawnBoneName) ? TEXT("true") : TEXT("false"));
		}
	}

	const USkeletalMeshComponent* ResolvedMesh = nullptr;
	FName ResolvedSocketName = NAME_None;
	if (!SpawnSocketName.IsNone())
	{
		ResolveSpawnSocket(
			Player,
			SpawnSocketName,
			FallbackSpawnBoneName,
			OutSpawnLocation,
			ResolvedMesh,
			ResolvedSocketName);
	}

	OutLaunchVelocity = AimDirection * FMath::Max(1.0f, ThrowSpeed);
	if (bShouldLogResolution)
	{
		if (ResolvedMesh)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[VortexPipeline][Launch] RESOLVED Mesh=%s Socket=%s Location=%s "
					"DistanceFromActor=%.1f Velocity=%s"),
				*GetPathNameSafe(ResolvedMesh),
				*ResolvedSocketName.ToString(),
				*OutSpawnLocation.ToCompactString(),
				FVector::Distance(Player->GetActorLocation(), OutSpawnLocation),
				*OutLaunchVelocity.ToCompactString());
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[VortexPipeline][Launch] SOCKET NOT FOUND. Using actor offset=%s "
					"(ForwardOffset=%.1f VerticalOffset=%.1f)."),
				*OffsetFallbackLocation.ToCompactString(),
				SpawnForwardOffset,
				SpawnVerticalOffset);
		}
	}
	return true;
}

bool UGA_GravityVortexThrow::ResolveSpawnSocket(
	const ABasePlayer* Player,
	FName RequestedSocketName,
	FName FallbackBoneName,
	FVector& OutWorldLocation,
	const USkeletalMeshComponent*& OutMesh,
	FName& OutResolvedName)
{
	OutMesh = nullptr;
	OutResolvedName = NAME_None;
	if (!IsValid(Player) || RequestedSocketName.IsNone())
	{
		return false;
	}

	TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes(Player);
	auto FindSocketMesh =
		[Player, &SkeletalMeshes](FName SocketName) -> const USkeletalMeshComponent*
	{
		if (Player->GetMesh() && Player->GetMesh()->DoesSocketExist(SocketName))
		{
			return Player->GetMesh();
		}
		for (const USkeletalMeshComponent* Candidate : SkeletalMeshes)
		{
			if (IsValid(Candidate) && Candidate->DoesSocketExist(SocketName))
			{
				return Candidate;
			}
		}
		return nullptr;
	};

	OutResolvedName = RequestedSocketName;
	OutMesh = FindSocketMesh(OutResolvedName);
	if (!OutMesh && !FallbackBoneName.IsNone())
	{
		OutResolvedName = FallbackBoneName;
		OutMesh = FindSocketMesh(OutResolvedName);
	}
	if (!OutMesh)
	{
		OutResolvedName = NAME_None;
		return false;
	}

	OutWorldLocation =
		OutMesh->GetSocketTransform(OutResolvedName, RTS_World).GetLocation();
	return true;
}

void UGA_GravityVortexThrow::SpawnProjectileOnServer()
{
	ABasePlayer* Player = Cast<ABasePlayer>(GetAvatarActorFromActorInfo());
	FVector SpawnLocation;
	FVector LaunchVelocity;
	if (!Player || !Player->HasAuthority() || !GetLaunchData(SpawnLocation, LaunchVelocity))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[VortexPipeline][Projectile] Server spawn preconditions failed Player=%s Authority=%s."),
			*GetPathNameSafe(Player),
			Player && Player->HasAuthority() ? TEXT("true") : TEXT("false"));
		return;
	}

	const FTransform SpawnTransform(LaunchVelocity.Rotation(), SpawnLocation);
	AGravityVortexProjectile* Projectile = GetWorld()->SpawnActorDeferred<AGravityVortexProjectile>(
		ProjectileClass, SpawnTransform, Player, Player, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Projectile)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[VortexPipeline][Projectile] Spawned=%s Class=%s Location=%s Velocity=%s."),
			*GetPathNameSafe(Projectile),
			*GetPathNameSafe(ProjectileClass.Get()),
			*SpawnLocation.ToCompactString(),
			*LaunchVelocity.ToCompactString());
		Projectile->FinishSpawning(SpawnTransform);
		Projectile->LaunchProjectile(LaunchVelocity);
	}
}
