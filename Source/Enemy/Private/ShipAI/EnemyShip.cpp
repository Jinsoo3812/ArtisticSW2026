// Fill out your copyright notice in the Description page of Project Settings.

#include "ShipAI/EnemyShip.h"
#include "Cannon.h"
#include "Components/ChildActorComponent.h"
#include "AbilitySystemComponent.h"
#include "ShipAttributeSet.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Components/WidgetComponent.h"
#include "Components/BaseHealthComponent.h"
#include "BaseAttributeSet.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "BuoyancyComponent.h"
#include "UI/HealthBarWidget.h"
#include "UObject/ConstructorHelpers.h"

AEnemyShip::AEnemyShip()
{
	PrimaryActorTick.bCanEverTick = true;

	HealthComponent = CreateDefaultSubobject<UBaseHealthComponent>(TEXT("HealthComponent"));
	HealthBarWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthBarWidgetComponent"));
	HealthBarWidgetComponent->SetupAttachment(RootComponent);
	HealthBarWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	HealthBarWidgetComponent->SetDrawAtDesiredSize(false);
	HealthBarWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FClassFinder<UHealthBarWidget> HealthBarWidgetFinder(TEXT("/Game/Blueprints/02_UI/UI_HUD/WBP_HealthBarWidget"));
	if (HealthBarWidgetFinder.Succeeded())
	{
		HealthBarWidgetClass = HealthBarWidgetFinder.Class;
		HealthBarWidgetComponent->SetWidgetClass(HealthBarWidgetClass);
	}
}

void AEnemyShip::BeginPlay()
{
	Super::BeginPlay();

	// HealthComponent를 Ship의 ASC에 바인딩 (BaseEnemy의 패턴과 동일)
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		if (HealthComponent)
		{
			HealthComponent->OnDeathStarted.AddUniqueDynamic(this, &AEnemyShip::OnDeathStarted);
			HealthComponent->OnHealthChanged.AddUniqueDynamic(this, &AEnemyShip::OnHealthChanged);
			HealthComponent->OnMaxHealthChanged.AddUniqueDynamic(this, &AEnemyShip::OnMaxHealthChanged);
			HealthComponent->InitializeWithAbilitySystem(ASC);
		}
	}

	InitializeHealthBarWidget();

	// 캐싱된 대포 목록 탐색
	FindAttachedCannons();

	// 0.5초마다 타겟과 가장 가까운 N개의 대포를 선정해 목록을 갱신하는 타이머 작동
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(ActiveCannonsTimerHandle, this, &AEnemyShip::UpdateActiveCannons, 0.5f, true);
	}
}

void AEnemyShip::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HealthComponent)
	{
		HealthComponent->OnDeathStarted.RemoveDynamic(this, &AEnemyShip::OnDeathStarted);
		HealthComponent->OnHealthChanged.RemoveDynamic(this, &AEnemyShip::OnHealthChanged);
		HealthComponent->OnMaxHealthChanged.RemoveDynamic(this, &AEnemyShip::OnMaxHealthChanged);
		HealthComponent->UninitializeFromAbilitySystem();
	}

	GetWorldTimerManager().ClearTimer(HealthBarHideTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void AEnemyShip::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (HasAuthority() && !bDeathHandled)
	{
		TickAIAimingAndFiring(DeltaTime);
	}
}

void AEnemyShip::OnDeathStarted(UBaseHealthComponent* InHealthComponent)
{
	if (HealthBarWidgetComponent)
	{
		HealthBarWidgetComponent->SetVisibility(false);
	}

	if (!bDeathHandled)
	{
		bDeathHandled = true;
		HandleShipDeath();
	}
}

void AEnemyShip::OnHealthChanged(UBaseHealthComponent* InHealthComponent, float OldValue, float NewValue, AActor* InstigatorActor)
{
	RefreshHealthBarWidget();
	UpdateHealthBarVisibilityAfterHealthChanged(OldValue, NewValue);
}

void AEnemyShip::OnMaxHealthChanged(UBaseHealthComponent* InHealthComponent, float OldValue, float NewValue, AActor* InstigatorActor)
{
	RefreshHealthBarWidget();
}

void AEnemyShip::InitializeHealthBarWidget()
{
	if (!HealthBarWidgetComponent)
	{
		return;
	}

	HealthBarWidgetComponent->SetRelativeLocation(HealthBarOffset);
	HealthBarWidgetComponent->SetDrawSize(HealthBarDrawSize);

	if (HealthBarWidgetClass)
	{
		HealthBarWidgetComponent->SetWidgetClass(HealthBarWidgetClass);
	}

	HealthBarWidgetComponent->InitWidget();
	RefreshHealthBarWidget();
	HealthBarWidgetComponent->SetVisibility(HealthBarVisibilityPolicy == EEnemyHealthBarVisibilityPolicy::AlwaysVisible);
}

void AEnemyShip::RefreshHealthBarWidget()
{
	if (!HealthComponent || !HealthBarWidgetComponent)
	{
		return;
	}

	if (UHealthBarWidget* HealthBarWidget = Cast<UHealthBarWidget>(HealthBarWidgetComponent->GetUserWidgetObject()))
	{
		HealthBarWidget->SetShowHealthText(false);
		HealthBarWidget->SetHealthValues(HealthComponent->GetHealth(), HealthComponent->GetMaxHealth());
	}
}

void AEnemyShip::UpdateHealthBarVisibilityAfterHealthChanged(float OldValue, float NewValue)
{
	if (!HealthBarWidgetComponent || HealthBarVisibilityPolicy != EEnemyHealthBarVisibilityPolicy::ShowOnDamage)
	{
		return;
	}

	if (OldValue <= NewValue)
	{
		return;
	}

	HealthBarWidgetComponent->SetVisibility(true);
	GetWorldTimerManager().ClearTimer(HealthBarHideTimerHandle);
	GetWorldTimerManager().SetTimer(HealthBarHideTimerHandle, this, &AEnemyShip::HideHealthBarForDamagePolicy, HealthBarVisibleDurationAfterDamage, false);
}

void AEnemyShip::HideHealthBarForDamagePolicy()
{
	if (HealthBarWidgetComponent && HealthBarVisibilityPolicy == EEnemyHealthBarVisibilityPolicy::ShowOnDamage)
	{
		HealthBarWidgetComponent->SetVisibility(false);
	}
}

void AEnemyShip::HandleShipDeath()
{
	if (!HasAuthority()) return;

	// 1. 사망 로그 출력 (이름 + 마지막 체력)
	float FinalHealth = 0.0f;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		FinalHealth = ASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute());
	}
	UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::HandleShipDeath - [%s] destroyed! Final Health: %.1f"), *GetName(), FinalHealth);

	// 2. AI Behavior Tree 먼저 정지 (AddForce 경고 방지)
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		if (UBrainComponent* BrainComp = AIC->GetBrainComponent())
		{
			BrainComp->StopLogic(TEXT("Ship Destroyed"));
		}
	}

	// 3. BuoyancyCoefficient를 0으로 설정 → 부력만 완전히 제거, 중력으로 자연 침몰
	if (UBuoyancyComponent* BuoyancyComp = FindComponentByClass<UBuoyancyComponent>())
	{
		BuoyancyComp->BuoyancyData.BuoyancyCoefficient = 0.0f;
	}

	// 4. 대포 발사/조준 타이머 정지
	GetWorldTimerManager().ClearTimer(ActiveCannonsTimerHandle);
	ActiveAICannons.Empty();

	// 5. N초 후 Destroy
	GetWorldTimerManager().SetTimer(DeathDestroyTimerHandle, FTimerDelegate::CreateLambda([this]()
	{
		Destroy();
	}), DestroyAfterDeathDelay, false);
}

void AEnemyShip::FindAttachedCannons()
{
	AttachedCannons.Empty();

	// 1. 레벨 상에서 자식으로 부착된 Actor 탐색 (Actor Attachment)
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	for (AActor* Actor : AttachedActors)
	{
		if (ACannon* Cannon = Cast<ACannon>(Actor))
		{
			AttachedCannons.Add(Cannon);
		}
	}

	// 2. 블루프린트 내부 컴포넌트로 들어있는 ChildActorComponent 내 대포 탐색
	TArray<UActorComponent*> ChildComps;
	GetComponents(UChildActorComponent::StaticClass(), ChildComps);
	for (UActorComponent* Comp : ChildComps)
	{
		if (UChildActorComponent* CAC = Cast<UChildActorComponent>(Comp))
		{
			if (ACannon* Cannon = Cast<ACannon>(CAC->GetChildActor()))
			{
				AttachedCannons.Add(Cannon);
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("AEnemyShip::FindAttachedCannons - Found %d cannons attached to %s."), AttachedCannons.Num(), *GetName());
}

void AEnemyShip::UpdateActiveCannons()
{
	if (!HasAuthority()) return;

	if (!AITargetShip || AttachedCannons.Num() == 0)
	{
		// 타겟이 없거나 대포가 없으면 활성 대포 정렬을 비우고 기존 대포는 정렬 리셋
		ActiveAICannons.Empty();
		for (ACannon* Cannon : AttachedCannons)
		{
			if (Cannon)
			{
				Cannon->SetAIAimRotation(0.f, 0.f);
			}
		}
		return;
	}

	FVector TargetLoc = AITargetShip->GetActorLocation();

	// 타겟 선박과의 거리 기준 정렬 (제곱 거리로 연산 최소화)
	TArray<ACannon*> SortedCannons = AttachedCannons;
	SortedCannons.Sort([TargetLoc](const ACannon& A, const ACannon& B) {
		float DistA = FVector::DistSquared(A.GetActorLocation(), TargetLoc);
		float DistB = FVector::DistSquared(B.GetActorLocation(), TargetLoc);
		return DistA < DistB;
	});

	ActiveAICannons.Empty();
	int32 CountToSelect = FMath::Min(MaxActiveCannons, SortedCannons.Num());
	for (int32 i = 0; i < CountToSelect; ++i)
	{
		ActiveAICannons.Add(SortedCannons[i]);
	}

	// 활성화되지 못한 나머지 대포들은 조준 초기화(정면 복귀)
	for (ACannon* Cannon : AttachedCannons)
	{
		if (Cannon && !ActiveAICannons.Contains(Cannon))
		{
			Cannon->SetAIAimRotation(0.f, 0.f);
		}
	}
}

void AEnemyShip::TickAIAimingAndFiring(float DeltaTime)
{
	if (!AITargetShip || ActiveAICannons.Num() == 0)
	{
		return;
	}

	// 1. 배의 스탯으로부터 대포알 발사 속도 및 중력 정보 획득
	float ProjectileSpeed = 3000.f; // Fallback 기본값
	if (UAbilitySystemComponent* ShipASC = GetAbilitySystemComponent())
	{
		ProjectileSpeed = ShipASC->GetNumericAttribute(UShipAttributeSet::GetCannonballSpeedAttribute());
	}

	UWorld* World = GetWorld();
	if (!World) return;

	float Gravity = FMath::Abs(World->GetGravityZ());
	if (Gravity <= 0.01f || ProjectileSpeed <= 10.f)
	{
		return; // 비정상 물리 상태 예외 처리
	}

	FVector TargetLoc = AITargetShip->GetActorLocation();

	// 2. 활성 대포별로 각각 조준각 연산 및 발사 진행
	for (ACannon* Cannon : ActiveAICannons)
	{
		if (!Cannon) continue;

		FVector StartLoc = Cannon->GetActorLocation();
		FVector ToTarget = TargetLoc - StartLoc;

		float HorizDist = FVector::Dist2D(StartLoc, TargetLoc);
		float VertDist = ToTarget.Z;

		// 3. 탄도학 투사 궤적 공식 대입 (해석학적 공식)
		// Disc = v^4 - g * (g * x^2 + 2 * y * v^2)
		float SpeedSq = ProjectileSpeed * ProjectileSpeed;
		float Speed4 = SpeedSq * SpeedSq;
		float Disc = Speed4 - Gravity * (Gravity * HorizDist * HorizDist + 2.f * VertDist * SpeedSq);

		if (Disc < 0.f)
		{
			// 최대 사거리를 벗어난 경우 조준을 풀고 대기
			Cannon->SetAIAimRotation(0.f, 0.f);
			continue;
		}

		// 저각 탄도 계산
		float PitchRad = FMath::Atan2(SpeedSq - FMath::Sqrt(Disc), Gravity * HorizDist);
		float PitchDeg = FMath::RadiansToDegrees(PitchRad);

		// 월드 공간 발사 방향 벡터 생성
		FVector HorizDir = FVector(ToTarget.X, ToTarget.Y, 0.f).GetSafeNormal();
		FVector LaunchDir = HorizDir * FMath::Cos(PitchRad) + FVector(0.f, 0.f, FMath::Sin(PitchRad));

		// 대포의 로컬 공간으로 변환하여 Yaw / Pitch 도출
		FVector LocalLaunchDir = Cannon->GetActorTransform().InverseTransformVector(LaunchDir);
		FRotator TargetRot = LocalLaunchDir.Rotation();

		float TargetPitch = TargetRot.Pitch;
		float TargetYaw = TargetRot.Yaw;

		// 4. 180도 고개 돌림 방지 체크 (로컬 Yaw가 좌우 90도를 초과하면 조준 불가 상태 처리)
		if (FMath::Abs(TargetYaw) > 90.f)
		{
			// 조준하지 않고 정면 정렬 대기
			Cannon->SetAIAimRotation(0.f, 0.f);
		}
		else
		{
			// 조준 제어 적용
			Cannon->SetAIAimRotation(TargetPitch, TargetYaw);

			// 선회(Orbit) 또는 도망(Retreat) 상태 시 지속 발사
			if (CurrentCombatState == ENavalCombatState::Orbit || CurrentCombatState == ENavalCombatState::Retreat)
			{
				Cannon->FireCannon();
			}
		}
	}
}
