#include "BossAI/BossGunCombatComponent.h"
#include "BossAI/GunnerBossEnemy.h"
#include "BossAI/GunnerBossCombatData.h"
#include "BossAI/BossGunMovementComponent.h"
#include "BaseGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Components/BaseHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ShipAI/EnemyShip.h"
#include "Net/UnrealNetwork.h"

UBossGunCombatComponent::UBossGunCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

AGunnerBossEnemy* UBossGunCombatComponent::Boss() const { return Cast<AGunnerBossEnemy>(GetOwner()); }
const UGunnerBossCombatData* UBossGunCombatComponent::Data() const { return Boss() ? Boss()->CombatData.Get() : nullptr; }

void UBossGunCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBossGunCombatComponent, bAttackLocked);
}

bool UBossGunCombatComponent::IsLiveTarget(AActor* Target) const
{
	return IsValid(Boss()) && Boss()->CanEngageActor(Target);
}

bool UBossGunCombatComponent::IsRuntimeValid(AActor* Target) const
{
	const AGunnerBossEnemy* OwnerBoss = Boss();
	if (!IsValid(OwnerBoss) || !OwnerBoss->HasAuthority() || !IsValid(OwnerBoss->GetHostShip())
		|| OwnerBoss->IsActorBeingDestroyed() || OwnerBoss->GetHealthComponent()->IsDead() || !IsLiveTarget(Target)) return false;
	const UAbilitySystemComponent* ASC = OwnerBoss->GetAbilitySystemComponent();
	return ASC && !ASC->HasMatchingGameplayTag(State_Dead) && !ASC->HasMatchingGameplayTag(State_Status_Stun)
		&& !ASC->HasMatchingGameplayTag(State_Control_MovementBlocked) && !ASC->HasMatchingGameplayTag(State_Status_Knockback);
}

bool UBossGunCombatComponent::GetOrigin(FVector& Out) const
{
	const UGunnerBossCombatData* Settings = Data();
	const USkeletalMeshComponent* Mesh = Boss() ? Boss()->GetMesh() : nullptr;
	if (!Settings || !Mesh || Settings->FireSocketName.IsNone() || !Mesh->DoesSocketExist(Settings->FireSocketName)) return false;
	Out = Mesh->GetSocketLocation(Settings->FireSocketName);
	return !Out.ContainsNaN();
}

FVector UBossGunCombatComponent::GetAimPoint(AActor* Target) const
{
	return Target && Data() ? Target->GetActorTransform().TransformPosition(Data()->TargetAimOffset) : FVector::ZeroVector;
}

bool UBossGunCombatComponent::Trace(const FVector& Start, const FVector& Direction, FHitResult& OutHit, FVector& OutEnd) const
{
	const UGunnerBossCombatData* Settings = Data();
	OutHit = FHitResult();
	if (!Settings || !GetWorld() || Start.ContainsNaN() || Direction.ContainsNaN() || Direction.IsNearlyZero()) return false;
	OutEnd = Start + Direction.GetSafeNormal() * Settings->AttackRange;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BossGunShot), Settings->bTraceComplex, GetOwner());
	TArray<AActor*> Attachments;
	GetOwner()->GetAttachedActors(Attachments, true, true);
	Params.AddIgnoredActors(Attachments);
	// Keep HostShip in this query so its rails/walls remain real cover.
	GetWorld()->LineTraceSingleByChannel(OutHit, Start, OutEnd, Settings->TraceChannel, Params);
	if (OutHit.bBlockingHit) OutEnd = OutHit.ImpactPoint;
	return true;
}

bool UBossGunCombatComponent::HasClearLineFrom(AActor* Target, const FVector& Start) const
{
	if (!IsLiveTarget(Target) || !Data()) return false;
	const FVector Delta = GetAimPoint(Target) - Start;
	if (Delta.IsNearlyZero() || Delta.SizeSquared() > FMath::Square(Data()->AttackRange)) return false;
	FHitResult Hit;
	FVector End;
	return Trace(Start, Delta, Hit, End) && (!Hit.bBlockingHit || Hit.GetActor() == Target
		|| FVector::DistSquared(Start, Hit.ImpactPoint) > Delta.SizeSquared());
}

bool UBossGunCombatComponent::HasClearLine(AActor* Target) const
{
	FVector Start;
	return GetOrigin(Start) && HasClearLineFrom(Target, Start);
}

bool UBossGunCombatComponent::CanStart(AActor* Target) const
{
	FString Error;
	return !bAttackLocked && Data() && Data()->Validate(Error) && IsRuntimeValid(Target)
		&& GetWorld()->GetTimeSeconds() >= NextAttackTime && HasClearLine(Target);
}

uint32 UBossGunCombatComponent::BeginAttack(AActor* Target)
{
	if (!CanStart(Target)) return 0;
	AGunnerBossEnemy* OwnerBoss = Boss();
	FActorSpawnParameters Params;
	Params.Owner = OwnerBoss;
	Params.Instigator = OwnerBoss;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AimLine = GetWorld()->SpawnActor<ABossGunAimLine>(Data()->AimLineClass, OwnerBoss->GetActorTransform(), Params);
	if (!AimLine) return 0;
	AimLine->Initialize(OwnerBoss, Data()->FireSocketName);
	if (++NextAttackId == 0) ++NextAttackId;
	ActiveAttackId = NextAttackId;
	bAimLocked = false;
	OwnerBoss->GunMovement->Stop();
	bAttackLocked = true;
	ApplyMovementLock();
	OwnerBoss->ForceNetUpdate();
	return ActiveAttackId;
}

void UBossGunCombatComponent::OnRep_AttackLocked() { ApplyMovementLock(); }

void UBossGunCombatComponent::ApplyMovementLock()
{
	AGunnerBossEnemy* OwnerBoss = Boss();
	UCharacterMovementComponent* Movement = OwnerBoss ? OwnerBoss->GetCharacterMovement() : nullptr;
	USkeletalMeshComponent* Mesh = OwnerBoss ? OwnerBoss->GetMesh() : nullptr;
	if (!Movement || !Mesh) return;
	UAnimInstance* Anim = Mesh->GetAnimInstance();
	if (bAttackLocked && !bLocalLockApplied)
	{
		bLocalLockApplied = true;
		SetComponentTickEnabled(OwnerBoss->HasAuthority());
		OldBaseSpeed = OwnerBoss->GetBaseMovementSpeed();
		bOldControllerYaw = OwnerBoss->bUseControllerRotationYaw;
		bOldOrientToMovement = Movement->bOrientRotationToMovement;
		bOldDesiredRotation = Movement->bUseControllerDesiredRotation;
		OldPoseTickPolicy = static_cast<uint8>(Mesh->VisibilityBasedAnimTickOption);
		OldRootMotionMode = Anim ? static_cast<uint8>(Anim->RootMotionMode.GetValue()) : 0;
		OwnerBoss->bUseControllerRotationYaw = false;
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = false;
		if (Anim) Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
		if (OwnerBoss->HasAuthority())
		{
			if (AAIController* AI = Cast<AAIController>(OwnerBoss->GetController()))
			{
				AI->StopMovement();
				AI->ClearFocus(EAIFocusPriority::Gameplay);
			}
			OwnerBoss->ConsumeMovementInputVector();
			Movement->StopMovementImmediately();
			OwnerBoss->SetBaseMovementSpeed(0.f);
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Mesh->RefreshBoneTransforms();
		}
	}
	else if (!bAttackLocked && bLocalLockApplied)
	{
		bLocalLockApplied = false;
		SetComponentTickEnabled(false);
		Mesh->VisibilityBasedAnimTickOption = static_cast<EVisibilityBasedAnimTickOption>(OldPoseTickPolicy);
		if (Anim) Anim->SetRootMotionMode(static_cast<ERootMotionMode::Type>(OldRootMotionMode));
		if (!OwnerBoss->GetHealthComponent()->IsDead())
		{
			OwnerBoss->bUseControllerRotationYaw = bOldControllerYaw;
			Movement->bOrientRotationToMovement = bOldOrientToMovement;
			Movement->bUseControllerDesiredRotation = bOldDesiredRotation;
			if (OwnerBoss->HasAuthority()) OwnerBoss->SetBaseMovementSpeed(OldBaseSpeed);
		}
	}
}

FVector UBossGunCombatComponent::LockedWorldDirection() const
{
	return LockedFrame.IsValid() ? LockedFrame->GetComponentTransform().TransformVectorNoScale(LocalLockedDirection).GetSafeNormal()
		: LocalLockedDirection.GetSafeNormal();
}

bool UBossGunCombatComponent::UpdateAim(uint32 AttackId, AActor* Target, EBossGunAimPhase Phase, float StartTime, float EndTime)
{
	if (!OwnsAttack(AttackId) || !IsRuntimeValid(Target) || !AimLine) return false;
	FVector Start;
	if (!GetOrigin(Start)) return false;
	FVector Direction = bAimLocked ? LockedWorldDirection() : (GetAimPoint(Target) - Start).GetSafeNormal();
	if (Direction.IsNearlyZero()) return false;
	if (!bAimLocked)
	{
		const UStaticMeshComponent* Frame = Boss()->GetHostShip()->GetDeckMeshComplex();
		const FVector Up = Frame ? Frame->GetUpVector() : FVector::UpVector;
		const FVector Facing = FVector::VectorPlaneProject(Direction, Up).GetSafeNormal();
		if (!Facing.IsNearlyZero()) Boss()->SetActorRotation(FRotationMatrix::MakeFromXZ(Facing, Up).Rotator());
		if (!GetOrigin(Start)) return false;
		Direction = (GetAimPoint(Target) - Start).GetSafeNormal();
	}
	FHitResult Hit;
	FVector End;
	if (!Trace(Start, Direction, Hit, End)) return false;
	AimLine->SetAim(Phase, End, StartTime, EndTime);
	return true;
}

bool UBossGunCombatComponent::LockAim(uint32 AttackId, AActor* Target)
{
	FVector Start;
	if (!OwnsAttack(AttackId) || !IsRuntimeValid(Target) || !GetOrigin(Start)) return false;
	const FVector Direction = (GetAimPoint(Target) - Start).GetSafeNormal();
	if (Direction.IsNearlyZero()) return false;
	LockedFrame = Boss()->GetHostShip()->GetDeckMeshComplex();
	LocalLockedDirection = LockedFrame.IsValid() ? LockedFrame->GetComponentTransform().InverseTransformVectorNoScale(Direction) : Direction;
	bAimLocked = true;
	return true;
}

bool UBossGunCombatComponent::BuildShot(uint32 AttackId, FHitResult& OutHit, FVector& OutStart, FVector& OutEnd) const
{
	return OwnsAttack(AttackId) && bAimLocked && GetOrigin(OutStart) && Trace(OutStart, LockedWorldDirection(), OutHit, OutEnd);
}

void UBossGunCombatComponent::ShowShot(uint32 AttackId, const FVector& Start, const FVector& End, bool bBlockingHit)
{
	if (OwnsAttack(AttackId) && AimLine) AimLine->ShowShot(Start, End, bBlockingHit);
}

void UBossGunCombatComponent::SetHiddenPhase(uint32 AttackId, EBossGunAimPhase Phase, float StartTime, float EndTime)
{
	if (!OwnsAttack(AttackId)) return;
	bAimLocked = false;
	if (AimLine) AimLine->SetAim(Phase, FVector::ZeroVector, StartTime, EndTime);
}

void UBossGunCombatComponent::FinishAttack(uint32 AttackId, float PostAttackGap, bool bKeepFinalShot)
{
	if (!GetOwner()->HasAuthority() || !OwnsAttack(AttackId)) return;
	NextAttackTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, PostAttackGap);
	ActiveAttackId = 0;
	bAimLocked = false;
	LockedFrame.Reset();
	if (AimLine)
	{
		if (bKeepFinalShot) AimLine->FinishPresentation();
		else AimLine->Destroy();
	}
	AimLine = nullptr;
	bAttackLocked = false;
	ApplyMovementLock();
	GetOwner()->ForceNetUpdate();
}

void UBossGunCombatComponent::ResetCombat()
{
	if (GetOwner()->HasAuthority())
	{
		FinishAttack(ActiveAttackId, 0.f);
		NextAttackTime = 0.;
	}
	else { bAttackLocked = false; ApplyMovementLock(); }
}

void UBossGunCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetCombat();
	Super::EndPlay(EndPlayReason);
}

void UBossGunCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AGunnerBossEnemy* OwnerBoss = Boss();
	if (!OwnerBoss || !OwnerBoss->HasAuthority() || !bAttackLocked) return;
	// Also protects atomic attacks from a different BT subtree issuing walking commands.
	OwnerBoss->ConsumeMovementInputVector();
	OwnerBoss->SetBaseMovementSpeed(0.f);
	if (OwnerBoss->GetCharacterMovement()->IsMovingOnGround()) OwnerBoss->GetCharacterMovement()->StopMovementImmediately();
}
