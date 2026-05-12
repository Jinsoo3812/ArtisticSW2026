#include "Trap.h"
#include "Components/StaticMeshComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "BaseGameplayTags.h" // Team_Enemy 태그 등

ATrap::ATrap()
{
	PrimaryActorTick.bCanEverTick = false; // 틱 불필요 (GAS가 타이머를 돌려줌)
	bReplicates = true;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	RootComponent = MeshComp;

	// 블루프린트에서 세팅하시겠지만, 기본적으로 오버랩 이벤트를 켜둡니다.
	MeshComp->SetGenerateOverlapEvents(true);
}

void ATrap::BeginPlay()
{
	Super::BeginPlay();

	// [서버 권한] 오직 서버에서만 적 판정 및 데미지 로직을 수행합니다.
	if (HasAuthority())
	{
		MeshComp->OnComponentBeginOverlap.AddDynamic(this, &ATrap::OnTrapBeginOverlap);
		MeshComp->OnComponentEndOverlap.AddDynamic(this, &ATrap::OnTrapEndOverlap);
	}
}

void ATrap::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// [중요 방어 코드] 
	// 누군가 함정을 밟고 있는 도중에 함정이 파괴되거나 지속시간이 끝날 경우,
	// 적들에게 걸려있던 주기적 데미지를 강제로 모두 해제해주어야 합니다.
	if (HasAuthority())
	{
		for (const auto& Pair : ActiveDamageEffects)
		{
			if (UAbilitySystemComponent* ASC = Pair.Key)
			{
				ASC->RemoveActiveGameplayEffect(Pair.Value);
			}
		}
		ActiveDamageEffects.Empty();
	}

	Super::EndPlay(EndPlayReason);
}

void ATrap::OnTrapBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 자신과의 충돌이거나 스펙이 없으면 무시
	if (!OtherActor || OtherActor == this || !DamageEffectSpecHandle.IsValid()) return;

	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OtherActor);
	if (!TargetASC) return;

	// 적군 태그를 들고 있는지 확인 (지금 Enemy에 Tag가 안 붙어있음 ;;)
	// if (!TargetASC->HasMatchingGameplayTag(Team_Enemy))
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("ATrap::ActivateAbility : %s stepped on the trap but is not an enemy. Ignoring."), *OtherActor->GetName());	
	// 	return;
	// }

	// 이미 밟고 있어서 관리 대상인 ASC라면 무시
	if (ActiveDamageEffects.Contains(TargetASC)) return;

	// 무한 유지 + 1초 주기 데미지 GE 적용
	FActiveGameplayEffectHandle ActiveHandle = TargetASC->ApplyGameplayEffectSpecToSelf(*DamageEffectSpecHandle.Data.Get());

	// 성공적으로 적용되었다면 맵에 저장해둠
	if (ActiveHandle.WasSuccessfullyApplied())
	{
		ActiveDamageEffects.Add(TargetASC, ActiveHandle);
		UE_LOG(LogTemp, Warning, TEXT("ATrap::ActivateAbility : Trap stepped on by %s. Applied GE."), *OtherActor->GetName());
	}
}

void ATrap::OnTrapEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!OtherActor || OtherActor == this) return;

	UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OtherActor);
	if (!TargetASC) return;

	// 맵에서 나간 대상의 GE 핸들을 찾아 주기적 데미지를 꺼버림
	if (FActiveGameplayEffectHandle* FoundHandle = ActiveDamageEffects.Find(TargetASC))
	{
		TargetASC->RemoveActiveGameplayEffect(*FoundHandle);
		ActiveDamageEffects.Remove(TargetASC);
		UE_LOG(LogTemp, Log, TEXT("ATrap::ActivateAbility : Trap left by %s. Removed GE."), *OtherActor->GetName());
	}
}