// Fill out your copyright notice in the Description page of Project Settings.


#include "Weapon/BaseWeapon.h"
#include "WeaponFeedback/WeaponFeedbackComponent.h"

// Unreal Engine
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameplayEffect.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/UnrealNetwork.h"


ABaseWeapon::ABaseWeapon()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bNetUseOwnerRelevancy = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(10.0f);
	SetMinNetUpdateFrequency(1.0f);

	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);

	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetGenerateOverlapEvents(false);

	TraceStartPoint = CreateDefaultSubobject<USceneComponent>(TEXT("TraceStartPoint"));
	TraceStartPoint->SetupAttachment(WeaponMesh);

	TraceEndPoint = CreateDefaultSubobject<USceneComponent>(TEXT("TraceEndPoint"));
	TraceEndPoint->SetupAttachment(WeaponMesh);

	WeaponFeedbackComponent = CreateDefaultSubobject<UWeaponFeedbackComponent>(TEXT("WeaponFeedbackComponent"));
	WeaponFeedbackComponent->SetTrailEndpointComponents(TraceStartPoint, TraceEndPoint);
	
	TraceObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));
}

// GA로부터 HitScanEffectSpecHandle을 받아 HitScan을 시작하는 함수
void ABaseWeapon::HitScanStart(const FGameplayEffectSpecHandle& HitScanEffectSpecHandle)
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	if (!HitScanEffectSpecHandle.IsValid() || !HitScanEffectSpecHandle.Data.IsValid())
	{
		return;
	}

	// HitScan 시작 전 변수 초기화
	CachedEffectSpecHandle = HitScanEffectSpecHandle;
	HitActors.Reset();
	bIsHitScanActive = true;

	// Timer를 설정해서 ProcessTrace함수를 HitScanInterval마다 부른다.
	GetWorldTimerManager().ClearTimer(HitScanTimerHandle);
	GetWorldTimerManager().SetTimer(
		HitScanTimerHandle,
		this,
		&ABaseWeapon::ProcessTrace,
		FMath::Max(HitScanInterval, KINDA_SMALL_NUMBER),
		true
	);

	// 시작 직후 첫 타격 프레임을 놓치지 않도록 즉시 1회 실행
	ProcessTrace();
}

void ABaseWeapon::ProcessTrace()
{
	if (!HasAuthority() || !GetWorld() || !bIsHitScanActive || !TraceStartPoint || !TraceEndPoint)
	{
		return;
	}

	if (!CachedEffectSpecHandle.IsValid() || !CachedEffectSpecHandle.Data.IsValid())
	{
		HitScanEnd();
		return;
	}

	// Trace시작 점과 끝점을 넣어서 SphereTraceMultiForObjects를 실행한다.
	// HitScanInterval마다 이 함수가 호출되면서 지속적으로 Trace가 이루어진다.
	const FVector TraceStart = TraceStartPoint->GetComponentLocation();
	const FVector TraceEnd = TraceEndPoint->GetComponentLocation();

	// 무시할 Actor들을 추가
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(this);

	if (AActor* OwnerActor = GetOwner())
	{
		ActorsToIgnore.Add(OwnerActor);
	}

	if (AActor* InstigatorActor = GetInstigator())
	{
		ActorsToIgnore.AddUnique(InstigatorActor);
	}

	// Trace를 보여주는 함수
	TArray<FHitResult> HitResults;
	UKismetSystemLibrary::SphereTraceMultiForObjects(
		this,
		TraceStart,
		TraceEnd,
		TraceRadius,
		TraceObjectTypes,
		bTraceComplex,
		ActorsToIgnore,
		bDrawDebugTrace ? EDrawDebugTrace::ForDuration : EDrawDebugTrace::None,
		HitResults,
		true
	);

	// HitResult를 HitScan에 넘겨주는 함수
	for (const FHitResult& HitResult : HitResults)
	{
		HitScan(HitResult);
	}
}

// HitScan함수가 종료되었을 때 호출되는 함수. GA에서 EndAbility이후에 호출하자.
void ABaseWeapon::HitScanEnd()
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(HitScanTimerHandle);
	}

	ClearHitScanInternalState();
}


void ABaseWeapon::HitScan(const FHitResult& HitResult)
{
	// HitActor가 없거나, ShouldIgnoreActor함수의 대상이라면 return
	AActor* HitActor = HitResult.GetActor();
	if (!HitActor || ShouldIgnoreActor(HitActor))
	{
		return;
	}

	// 이미 Hit 처리한 Actor라면 return
	const TWeakObjectPtr<AActor> HitActorPtr(HitActor);
	if (HitActors.Contains(HitActorPtr))
	{
		return;
	}

	// HitActor의 ASC, 없다면 return
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);
	if (!TargetASC)
	{
		return;
	}

	HitActors.Add(HitActorPtr);
	ApplyEffectToTarget(HitActor, HitResult);
}

// HitScan으로 감지된 Actor에게 CachedEffectSpecHandle의 GameplayEffect를 적용하는 함수
void ABaseWeapon::ApplyEffectToTarget(AActor* TargetActor, const FHitResult& HitResult) const
{
	if (!TargetActor || !CachedEffectSpecHandle.IsValid() || !CachedEffectSpecHandle.Data.IsValid())
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	if (!TargetASC)
	{
		return;
	}
	
	// UE_LOG(LogTemp, Log, TEXT("Applying effect to target: %s"), *TargetActor->GetName());
	FGameplayEffectSpec TargetEffectSpec(*CachedEffectSpecHandle.Data.Get());
	FGameplayEffectContextHandle EffectContext = TargetEffectSpec.GetContext();

	AActor* SourceActor = GetInstigator();
	if (!SourceActor)
	{
		SourceActor = GetOwner();
	}

	if (SourceActor)
	{
		EffectContext.AddInstigator(SourceActor, const_cast<ABaseWeapon*>(this));
	}

	EffectContext.AddSourceObject(const_cast<ABaseWeapon*>(this));
	EffectContext.AddHitResult(HitResult, true);
	TargetEffectSpec.SetContext(EffectContext);

	TargetASC->ApplyGameplayEffectSpecToSelf(TargetEffectSpec);
}

// HitScan시 무시해야할 대상
bool ABaseWeapon::ShouldIgnoreActor(const AActor* OtherActor) const
{
	if (!OtherActor || OtherActor == this)
	{
		return true;
	}

	if (OtherActor == GetOwner())
	{
		return true;
	}

	if (OtherActor == GetInstigator())
	{
		return true;
	}

	return false;
}

// HitScanEnd시 호출, SpecHandle, HitActor, Timer 초기화
void ABaseWeapon::ClearHitScanInternalState()
{
	bIsHitScanActive = false;
	CachedEffectSpecHandle = FGameplayEffectSpecHandle();
	HitActors.Reset();
	HitScanTimerHandle.Invalidate();
}

void ABaseWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}
