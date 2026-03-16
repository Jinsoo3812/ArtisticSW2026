#include "Abilities/GA_ThrowGrenade.h"
#include "Projectiles/GrenadeProjectile.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "GameFramework/Character.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/SphereComponent.h"
#include "BaseGameplayTags.h"


// TODO: BasePlayer 관련 헤더가 필요하다면 추후 주석 해제하세요.
// #include "BasePlayer.h"

UGA_ThrowGrenade::UGA_ThrowGrenade()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    ThrowForce = 1500.f;
    /*ActivationOwnedTags.AddTag(State_Aiming);*/
}

void UGA_ThrowGrenade::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

    // 1. 스킬이 켜질 때 내 몸에 조준(Aiming) 태그를 수동으로 콱 박아줍니다!
    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
    {
        ASC->AddLooseGameplayTag(State_Aiming);
    }

    // 2. 키 뗌 대기 태스크 실행
    UAbilityTask_WaitInputRelease* WaitInputTask = UAbilityTask_WaitInputRelease::WaitInputRelease(this);
    WaitInputTask->OnRelease.AddDynamic(this, &UGA_ThrowGrenade::OnInputReleased);
    WaitInputTask->ReadyForActivation();
}

void UGA_ThrowGrenade::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    // 3. 스킬이 끝날 때(던지거나 취소될 때) 조준 태그를 다시 뺏어옵니다.
    if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
    {
        ASC->RemoveLooseGameplayTag(State_Aiming);
    }

    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_ThrowGrenade::OnInputReleased(float TimeHeld)
{
    ACharacter* AvatarChar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
    UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();

    if (AvatarChar && ASC && HasAuthority(&CurrentActivationInfo))
    {
        if (GrenadeClass)
        {
            FVector LaunchVelocity = CalculateLaunchVelocity(AvatarChar);

            // 1. 스폰 위치를 캐릭터의 'hand_r' 소켓 위치로 정확히 지정!
            FVector SpawnLocation = AvatarChar->GetActorLocation(); // 기본값
            if (AvatarChar->GetMesh())
            {
                // ABaseItem에서 설정한 소켓 이름과 동일해야 합니다!
                SpawnLocation = AvatarChar->GetMesh()->GetSocketLocation(FName("hand_r"));
            }

            FTransform SpawnTransform(LaunchVelocity.Rotation(), SpawnLocation);

            // 2. 데미지 GE Spec 생성
            FGameplayEffectSpecHandle DamageSpecHandle;
            if (DamageEffectClass)
            {
                FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
                ContextHandle.AddInstigator(AvatarChar, AvatarChar);
                DamageSpecHandle = ASC->MakeOutgoingSpec(DamageEffectClass, 1.0f, ContextHandle);
            }

            // 3. 지연 생성
            AGrenadeProjectile* Grenade = GetWorld()->SpawnActorDeferred<AGrenadeProjectile>(
                GrenadeClass, SpawnTransform, AvatarChar, AvatarChar, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

            if (Grenade)
            {
                Grenade->SetInstigator(AvatarChar);
                Grenade->SetOwner(AvatarChar);

                // 4. 충돌 무시 로직
                if (Grenade->CollisionComp)
                {
                    Grenade->CollisionComp->IgnoreActorWhenMoving(AvatarChar, true);
                    TArray<AActor*> AttachedActors;
                    AvatarChar->GetAttachedActors(AttachedActors);
                    for (AActor* AttachedActor : AttachedActors)
                    {
                        Grenade->CollisionComp->IgnoreActorWhenMoving(AttachedActor, true);
                    }
                }

                Grenade->DamageEffectSpecHandle = DamageSpecHandle;

                Grenade->FinishSpawning(SpawnTransform);
            }
        }
    }

    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

FVector UGA_ThrowGrenade::CalculateLaunchVelocity(ACharacter* AvatarChar)
{
    // 1. 카메라 시점에서 화면 중앙(크로스헤어)이 가리키는 실제 타겟 위치를 찾습니다.
    FRotator AimRotation = AvatarChar->GetBaseAimRotation();
    FVector AimDirection = AimRotation.Vector();

    // 3인칭이므로 카메라(혹은 눈) 위치에서 레이저를 쏩니다.
    APlayerController* PC = Cast<APlayerController>(AvatarChar->GetController());
    FVector TraceStart = AvatarChar->GetActorLocation();
    if (PC) PC->GetPlayerViewPoint(TraceStart, AimRotation);

    FVector TraceEnd = TraceStart + (AimDirection * 10000.f);

    FHitResult HitResult;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(AvatarChar);

    TArray<AActor*> AttachedActors;
    AvatarChar->GetAttachedActors(AttachedActors);
    Params.AddIgnoredActors(AttachedActors);

    bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, Params);
    FVector TargetLoc = bHit ? HitResult.Location : TraceEnd;

    // 2. 투척 시작점을 '손 소켓'으로 잡습니다.
    FVector SpawnLocation = AvatarChar->GetActorLocation();
    if (AvatarChar->GetMesh())
    {
        SpawnLocation = AvatarChar->GetMesh()->GetSocketLocation(FName("hand_r"));
    }
    // 방향 계산
    FVector LaunchDir = (TargetLoc - SpawnLocation).GetSafeNormal();
    LaunchDir.Z += 0.15f;
    FVector FinalVelocity = LaunchDir.GetSafeNormal() * ThrowForce;

    return FinalVelocity;
}