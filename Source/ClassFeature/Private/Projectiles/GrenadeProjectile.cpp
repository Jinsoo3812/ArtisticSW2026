#include "Projectiles/GrenadeProjectile.h"
#include "AbilitySystemComponent.h"
#include "Item/Projectiles/ProjectileLaunchInitialization.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/Character.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Components/PrimitiveComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayTagContainer.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "BaseGameplayTags.h"
#include "Engine/OverlapResult.h"
#include "GAS/SWCombatEffectContextLibrary.h"
#include "Room/SWRoomSnapshotComponent.h"
#include "Room/SWRoomSnapshotSubsystem.h"
#include "GameplayEffect.h"
#include "TimerManager.h"

AGrenadeProjectile::AGrenadeProjectile()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    SetReplicateMovement(true);
    CreateDefaultSubobject<USWRoomSnapshotComponent>(TEXT("RoomSnapshot"));

    // 메시 컴포넌트 생성 및 루트 등록 
    MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
    RootComponent = MeshComp;

    // 충돌 및 물리
    MeshComp->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    MeshComp->SetSimulatePhysics(false);

    // PMC 컴포넌트 생성
    ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
    ProjectileMovement->bAutoActivate = false;
    ProjectileMovement->bShouldBounce = true;
}

void AGrenadeProjectile::BeginPlay()
{
    Super::BeginPlay();

    // 서버에서만 폭발 타이머 작동
    if (HasAuthority())
    {
        if (!GetWorld()->GetSubsystem<USWRoomSnapshotSubsystem>()->IsRestoringSnapshot())
            GetWorld()->GetTimerManager().SetTimer(ExplodeTimerHandle, this, &AGrenadeProjectile::Explode, ExplosionDelay, false);
    }

	if (APawn* InstigatorPawn = GetInstigator())
	{
		MeshComp->IgnoreActorWhenMoving(InstigatorPawn, true);
	}
}

void AGrenadeProjectile::LaunchProjectile(const FVector& LaunchVelocity)
{
    // PMC에 발사 속도 적용
    if (ProjectileMovement && ProjectileLaunchInitialization::ApplyWorldVelocity(
        ProjectileMovement, LaunchVelocity, ProjectileMovement->MaxSpeed))
    {
        ProjectileMovement->Activate();
    }
}

// 지연 생성 단계에서 메시 에셋을 채워넣는 함수
void AGrenadeProjectile::SetGrenadeMesh(UStaticMesh* InMesh)
{
    if (MeshComp && InMesh)
    {
        MeshComp->SetStaticMesh(InMesh);
    }
}

void AGrenadeProjectile::Explode()
{
	if (!HasAuthority() || bExploded) return;
	bExploded = true;
	GetWorldTimerManager().ClearTimer(ExplodeTimerHandle);
	TArray<FOverlapResult> OverlapResults;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this); // 난 폭발에 안 맞게
	QueryParams.bTraceComplex = false;

	GetWorld()->OverlapMultiByObjectType(
		OverlapResults,
		GetActorLocation(),
		FQuat::Identity,
		FCollisionObjectQueryParams(ECollisionChannel::ECC_Pawn), // 적 채널을 따로 만드는게 더 좋긴함
		FCollisionShape::MakeSphere(ExplosionRadius),
		QueryParams
	);

	if (DamageEffectSpecHandle.IsValid())
	{
		// 중복 처리를 막기 위해 이미 데미지를 적용한 액터를 기록할 배열
		TArray<AActor*> ProcessedActors;
		ProcessedActors.Reserve(OverlapResults.Num()); // 메모리 예약

		for (const FOverlapResult& Result : OverlapResults)
		{
			AActor* TargetActor = Result.GetActor();

			// TargetActor가 유효하지 않거나, 이미 배열에 존재한다면(데미지를 줬다면) 스킵
			if (!TargetActor || ProcessedActors.Contains(TargetActor))
			{
				continue;
			}

			// 새 타겟이므로 배열에 추가
			ProcessedActors.Add(TargetActor);

			// TargetActor의 ASC 획득
			if (UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor))
			{
				// Team_Enemy 태그를 들고 있는지 확인
				if (TargetASC->HasMatchingGameplayTag(Team_Enemy) || true /*지금 당장은 적이 없으니 일단*/)
				{
					FGameplayEffectSpec TargetSpec(*DamageEffectSpecHandle.Data.Get());
					USWCombatEffectContextLibrary::EnrichCombatEffectSpec(
						TargetSpec, GetInstigator(), this, TargetActor, nullptr,
						TargetActor->GetActorLocation() - GetActorLocation());
					TargetASC->ApplyGameplayEffectSpecToSelf(TargetSpec);
					UE_LOG(LogTemp, Log, TEXT("AGrenadeProjectile: Applied damage to %s"), *TargetActor->GetName());
				}
			}
		}
	}

	// 폭발 이펙트 등 클라 전용 처리를 위해 Multicast
	Multicast_OnExploded();

	Destroy();
}

void AGrenadeProjectile::CaptureRoomDomains(TArray<FSWRoomDomainPart>& OutParts, TArray<FSWRoomCaptureIssue>& OutIssues) const
{
	FSWRoomGrenadeState State;
	State.ExplosionDelay = ExplosionDelay;
	State.ExplosionRadius = ExplosionRadius;
	State.bExploded = bExploded;
	State.RemainingTime = GetWorld() && GetWorldTimerManager().IsTimerActive(ExplodeTimerHandle)
		? FMath::Max(0.f, GetWorldTimerManager().GetTimerRemaining(ExplodeTimerHandle)) : 0.f;
	if (DamageEffectSpecHandle.IsValid())
	{
		const FGameplayEffectSpec& Spec = *DamageEffectSpecHandle.Data.Get();
		State.DamageEffectClass = FSoftClassPath(Spec.Def->GetClass());
		State.DamageEffectLevel = Spec.GetLevel();
		for (const TPair<FGameplayTag, float>& Entry : Spec.SetByCallerTagMagnitudes)
		{
			FSWRoomSetByCallerTagValue& Value = State.DamageTagMagnitudes.AddDefaulted_GetRef();
			Value.Tag = Entry.Key;
			Value.Value = Entry.Value;
		}
		for (const TPair<FName, float>& Entry : Spec.SetByCallerNameMagnitudes)
		{
			FSWRoomSetByCallerNameValue& Value = State.DamageNameMagnitudes.AddDefaulted_GetRef();
			Value.Name = Entry.Key;
			Value.Value = Entry.Value;
		}
	}
	CaptureGrenadeSubclassState(State);
	FSWRoomDomainPart& Part = OutParts.AddDefaulted_GetRef();
	Part.Domain = ESWRoomDomain::Projectile;
	Part.Version = 1;
	if (!FSWRoomStructCodec::Write(State, Part.Bytes))
	{
		OutParts.Pop();
		FSWRoomCaptureIssue& Issue = OutIssues.AddDefaulted_GetRef();
		Issue.Domain = TEXT("Projectile");
		Issue.FieldKey = TEXT("GrenadeState");
		Issue.Reason = TEXT("Grenade adapter serialization failed");
	}
}

bool AGrenadeProjectile::RestoreRoomDomain(const FSWRoomDomainPart& Part, FString& OutError)
{
	FSWRoomGrenadeState State;
	if (Part.Domain != ESWRoomDomain::Projectile || Part.Version != 1 || !FSWRoomStructCodec::Read(Part.Bytes, State)
		|| !FMath::IsFinite(State.ExplosionDelay) || !FMath::IsFinite(State.ExplosionRadius)
		|| !FMath::IsFinite(State.RemainingTime) || State.RemainingTime < 0.f
		|| !FMath::IsFinite(State.DamageEffectLevel) || State.DamageEffectLevel <= 0.f)
	{
		OutError = TEXT("Invalid grenade state");
		return false;
	}
	ExplosionDelay = State.ExplosionDelay;
	ExplosionRadius = State.ExplosionRadius;
	bExploded = State.bExploded;
	GetWorldTimerManager().ClearTimer(ExplodeTimerHandle);
	DamageEffectSpecHandle = FGameplayEffectSpecHandle();
	if (!State.DamageEffectClass.IsNull())
	{
		UClass* EffectClass = State.DamageEffectClass.TryLoadClass<UGameplayEffect>();
		if (!EffectClass)
		{
			OutError = FString::Printf(TEXT("Grenade effect class missing: %s"), *State.DamageEffectClass.ToString());
			return false;
		}
		FGameplayEffectContextHandle Context(UAbilitySystemGlobals::Get().AllocGameplayEffectContext());
		DamageEffectSpecHandle = FGameplayEffectSpecHandle(new FGameplayEffectSpec(EffectClass->GetDefaultObject<UGameplayEffect>(), Context, State.DamageEffectLevel));
		for (const FSWRoomSetByCallerTagValue& Value : State.DamageTagMagnitudes)
			DamageEffectSpecHandle.Data->SetSetByCallerMagnitude(Value.Tag, Value.Value);
		for (const FSWRoomSetByCallerNameValue& Value : State.DamageNameMagnitudes)
			DamageEffectSpecHandle.Data->SetSetByCallerMagnitude(Value.Name, Value.Value);
	}
	RestoreGrenadeSubclassState(State);
	PendingRoomState = MoveTemp(State);
	bHasPendingRoomState = true;
	return true;
}

bool AGrenadeProjectile::FinalizeRoomRestore(const TMap<FGuid, AActor*>& RegisteredActors, FString& OutError)
{
	if (!bHasPendingRoomState) return true;
	bHasPendingRoomState = false;
	if (!bExploded)
	{
		if (PendingRoomState.RemainingTime <= 0.f)
			ExplodeTimerHandle = GetWorldTimerManager().SetTimerForNextTick(this, &AGrenadeProjectile::Explode);
		else
			GetWorldTimerManager().SetTimer(ExplodeTimerHandle, this, &AGrenadeProjectile::Explode,
				PendingRoomState.RemainingTime, false);
	}
	return true;
}

void AGrenadeProjectile::Multicast_OnExploded_Implementation()
{
	// 추후 폭발 이펙트 (VFX/SFX) 삽입부

	// 디버그 드로우 (시전자 머신에서만) ---
	APawn* InstigatorPawn = Cast<APawn>(GetInstigator());
	if (InstigatorPawn && InstigatorPawn->IsLocallyControlled())
	{
		DrawDebugSphere(
			GetWorld(),
			GetActorLocation(),
			ExplosionRadius,
			32,
			FColor::Red,
			false,
			2.0f
		);
	}
}
