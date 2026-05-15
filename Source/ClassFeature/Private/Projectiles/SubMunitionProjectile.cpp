#include "Projectiles/SubMunitionProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "Engine/OverlapResult.h"
#include "TimerManager.h"
#include "Math/UnrealMathUtility.h"

ASubMunitionProjectile::ASubMunitionProjectile()
{
	PrimaryActorTick.bCanEverTick = false; // Tick에서 타이머 체크를 제거했으므로 false로 변경
	bReplicates = true;
	SetReplicateMovement(true);

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	RootComponent = MeshComp;

	// 콜리전 설정은 Blueprint에서 하도록 명세
	MeshComp->SetCollisionProfileName(TEXT("Projectile")); 
	MeshComp->SetSimulatePhysics(false);
	MeshComp->OnComponentBeginOverlap.AddDynamic(this, &ASubMunitionProjectile::OnOverlapBegin);
	MeshComp->OnComponentHit.AddDynamic(this, &ASubMunitionProjectile::OnHit);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->bAutoActivate = false;
	ProjectileMovement->bShouldBounce = true;
	ProjectileMovement->bRotationFollowsVelocity = true; // 날아가는 방향을 바라봄
	ProjectileMovement->Bounciness = 0.4f; // 적당히 튕기도록
	ProjectileMovement->Friction = 0.6f;   // 구르다가 멈추도록

	CurrentState = ESubMunitionState::Moving;
}

void ASubMunitionProjectile::BeginPlay()
{
	Super::BeginPlay();

	if (APawn* InstigatorPawn = Cast<APawn>(GetInstigator()))
	{
		MeshComp->IgnoreActorWhenMoving(InstigatorPawn, true);
	}
}

void ASubMunitionProjectile::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// 기존 타이머 체크 로직 제거됨
}

void ASubMunitionProjectile::LaunchSubMunition(const FVector& LaunchVelocity)
{
	if (ProjectileMovement)
	{
		ProjectileMovement->Velocity = LaunchVelocity;
		ProjectileMovement->Activate();
	}
}

void ASubMunitionProjectile::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority() || OtherActor == this)
	{
		return;
	}

	// 적과 닿았는지 체크 (팀 태그 확인 등)
	if (UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OtherActor))
	{
		// 적중하면 무조건 폭발
		ExplodeAndDestroy();
	}
}

void ASubMunitionProjectile::OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!HasAuthority() || OtherActor == this)
	{
		return;
	}

	if (CurrentState == ESubMunitionState::Installed)
	{
		return; // 이미 설치된 상태면 무시
	}

	if (bExplodeOnImpact)
	{
		// 폭발 즉시 모드: 어떤 표면/객체든 닿기만 하면 폭발
		ExplodeAndDestroy();
	}
	else
	{
		// 지뢰 모드: 바닥 경사를 체크하여 설치
		float SlopeDot = FVector::DotProduct(FVector::UpVector, Hit.ImpactNormal);
		float MinSlopeDot = FMath::Cos(FMath::DegreesToRadians(MaxInstallSlopeAngle));

		if (SlopeDot >= MinSlopeDot)
		{
			// 경사가 M도 이하인 평탄한 바닥이므로 지뢰 설치
			TransitionToInstalled();
		}
		// 경사가 심한 벽 등에 맞았다면 튕기도록 내버려둠 (bShouldBounce가 처리)
	}
}

void ASubMunitionProjectile::ExplodeAndDestroy()
{
	if (!HasAuthority()) return;

	TArray<FOverlapResult> OverlapResults;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.bTraceComplex = false;

	// 반경 내 액터 검색
	GetWorld()->OverlapMultiByObjectType(
		OverlapResults,
		GetActorLocation(),
		FQuat::Identity,
		FCollisionObjectQueryParams(ECollisionChannel::ECC_Pawn),
		FCollisionShape::MakeSphere(ExplosionRadius),
		QueryParams
	);

	if (DamageEffectSpecHandle.IsValid())
	{
		TArray<AActor*> ProcessedActors;
		for (const FOverlapResult& Result : OverlapResults)
		{
			AActor* TargetActor = Result.GetActor();
			if (!TargetActor || ProcessedActors.Contains(TargetActor)) continue;
			
			ProcessedActors.Add(TargetActor);
			if (UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor))
			{
				TargetASC->ApplyGameplayEffectSpecToSelf(*DamageEffectSpecHandle.Data.Get());
			}
		}
	}

	Multicast_OnExploded();
	Destroy();
}

void ASubMunitionProjectile::Multicast_OnExploded_Implementation()
{
	// TODO: 파티클/사운드 재생 (현재는 디버그 스피어로 시각화)
	DrawDebugSphere(GetWorld(), GetActorLocation(), ExplosionRadius, 16, FColor::Orange, false, 2.0f);
}

void ASubMunitionProjectile::TransitionToInstalled()
{
	if (CurrentState == ESubMunitionState::Installed) return;
	
	CurrentState = ESubMunitionState::Installed;

	// 이동 정지 및 지뢰화
	if (ProjectileMovement)
	{
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}

	// 설치 후 일정 시간 뒤에 스스로 폭발하도록 타이머 설정
	FTimerHandle AutoExplodeTimerHandle;
	GetWorld()->GetTimerManager().SetTimer(AutoExplodeTimerHandle, this, &ASubMunitionProjectile::ExplodeAndDestroy, InstalledLifeSpan, false);

	// 클라이언트에 상태 동기화
	Multicast_SetInstalled();
}

void ASubMunitionProjectile::Multicast_SetInstalled_Implementation()
{
	CurrentState = ESubMunitionState::Installed;

	if (ProjectileMovement)
	{
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}

	// TODO: 설치 완료 상태를 나타내는 시각 효과 (예: 메시 머티리얼 변경, 이펙트 등)
}
