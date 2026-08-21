#include "Projectiles/GrenadeProjectile.h"
#include "AbilitySystemComponent.h"
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

AGrenadeProjectile::AGrenadeProjectile()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    SetReplicateMovement(true);

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
        FTimerHandle ExplodeTimerHandle;
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
    if (ProjectileMovement)
    {
        ProjectileMovement->Velocity = LaunchVelocity;
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
