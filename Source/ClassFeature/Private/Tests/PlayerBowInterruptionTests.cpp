#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PlayerLifeTestWorld.h"
#include "Attacker/GA_BowAimFire.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimInstance.h"
#include "BaseGameplayTags.h"
#include "Equipment/WeaponDefinition.h"
#include "Equipment/WeaponAnimationDataAsset.h"
#include "GAS/Ability/GA_PlayerHitReaction.h"
#include "Item/Components/BowComponent.h"
#include "Item/Weapons/BowItem.h"
#include "ItemData.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerBowInterruptionLifecycleTest,
 "ArtisticSW.PlayerBow.AimInterruptionLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlayerBowInterruptionLifecycleTest::RunTest(const FString& Parameters)
{
 PlayerLifeTests::FWorld Fixture;
 const auto Life = Fixture.SpawnLife();
 auto* Player = Life.Player;
 auto* ASC = Life.State->GetAbilitySystemComponent();
 Player->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
 Player->GetMesh()->InitAnim(true);
 auto* Definition = NewObject<UEquippableWeaponDefinition>();
 Definition->AnimationData = LoadObject<UWeaponAnimationDataAsset>(nullptr, TEXT("/Game/Blueprints/DAs/Weapon_DAs/Bow/WDA_Bow.WDA_Bow"));
 Definition->CombatData = NewObject<UWeaponCombatDataAsset>();
 FItemDefinition Row;
 Row.WeaponDefinition = Definition;
 auto* Bow = Fixture.World->SpawnActor<ABowItem>();
 Bow->SetOwner(Player);
 Bow->MyDefinition = &Row;
 Bow->ItemTag = Item_Id_Weapon_Bow_ShortBow1;
 Player->EquippedItem = Bow;
 if (!TestTrue(TEXT("Production arrow anchor binds"), Bow->BindArrowAnchor(Player->GetMesh()))) return false;
 auto* BowClass = LoadClass<UGA_BowAimFire>(nullptr, TEXT("/Game/GameplayAbilitySystem/Ability/Player/BPGA_Bow_AimShoot.BPGA_Bow_AimShoot_C"));
 auto* HitClass = LoadClass<UGA_PlayerHitReaction>(nullptr, TEXT("/Game/GameplayAbilitySystem/Ability/GA_HitReact.GA_HitReact_C"));
 if (!TestNotNull(TEXT("Production bow ability"), BowClass) || !TestNotNull(TEXT("Hit ability"), HitClass)) return false;
 TestTrue(TEXT("Production bow can be cancelled by hits"), BowClass->GetDefaultObject<UGA_BowAimFire>()->GetAssetTags().HasTagExact(GameplayAbility_InterruptibleByHit));
 const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(BowClass, 1, INDEX_NONE, Bow));
 const auto HitHandle = ASC->GiveAbility(FGameplayAbilitySpec(HitClass, 1));
 for (int32 Stage = 0; Stage < 4; ++Stage)
 {
  ASC->FindAbilitySpecFromHandle(Handle)->InputPressed = true;
  if (!TestTrue(TEXT("Aim activates again"), ASC->TryActivateAbility(Handle))) return false;
  auto* Ability = Cast<UGA_BowAimFire>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
  if (!TestTrue(TEXT("Aim stays active"), Ability && Ability->IsAimSessionActive())) return false;
  TestFalse(TEXT("New aim starts without a nocked arrow"), Bow->GetBowComponent()->IsArrowNocked());
  if (Stage > 0)
  {
   Ability->bUseAimCycleDrawAlphaCurve = false;
   Ability->OnLeftClickPressed(FGameplayEventData());
   if (!TestTrue(TEXT("Drawing starts"), Ability->bIsDrawing)) return false;
   Ability->OnNockArrowEvent(FGameplayEventData());
   if (Stage > 1)
   {
    Ability->DrawStartTime -= 10.f;
    Ability->UpdateDrawAlpha();
    TestTrue(TEXT("Full draw reached"), Ability->bIsFullyDrawn);
   }
   if (Stage > 2)
   {
    FGameplayEventData Release;
    auto* Data = new FGameplayAbilityTargetData_ProjectileShot();
    Data->ShotId = FGuid::NewGuid();
    Release.TargetData.Add(Data);
    Ability->BeginRelease(Release);
    Ability->QueueReleaseShot();
    TestTrue(TEXT("Release is queued before hit"), Ability->bReleaseQueued);
   }
  }
  auto* OldTask = Ability->BowMontageTask.Get();
  auto OldShotComponent = Ability->ShotComponent;
  const FGuid OldShot = Ability->PendingShotId;
  Bow->SetActorLocation(Player->GetActorLocation() + FVector(100, 0, 0));
  FGameplayEventData Hit;
  Hit.Instigator = Bow; Hit.Target = Player; Hit.EventMagnitude = 1.f;
  ASC->HandleGameplayEvent(GameplayAbility_HitReaction, &Hit);
  TestFalse(TEXT("Hit ends entire aim session"), Ability->IsActive());
  TestFalse(TEXT("Aim flag cleared"), Bow->GetBowComponent()->IsAiming());
  TestFalse(TEXT("Arrow removed"), Bow->GetBowComponent()->IsArrowNocked());
  TestEqual(TEXT("String restored"), Bow->GetBowComponent()->GetDrawAlpha(), 0.f);
  TestFalse(TEXT("Charge timer stopped"), Fixture.World->GetTimerManager().IsTimerActive(Ability->ChargeTimerHandle));
  TestFalse(TEXT("Pose refresh released"), Ability->bOwnsServerPoseRefresh);
  TestFalse(TEXT("Shot request cleared"), Ability->PendingShotId.IsValid());
  if (OldShotComponent.IsValid()) TestFalse(TEXT("Queued shot no longer ticks"), OldShotComponent->IsComponentTickEnabled());
  TestEqual(TEXT("Input tasks released"), Ability->AimInputTasks.Num(), 0);
  for (const FGameplayTag Tag : TArray<FGameplayTag>{ State_Aiming, State_Attacking, State_Bow_Drawing, State_Bow_FullyDrawn, State_Bow_Releasing })
   TestEqual(TEXT("Bow state tag released"), ASC->GetTagCount(Tag), 0);
  TestFalse(TEXT("No aiming during hit reaction"), ASC->TryActivateAbility(Handle));
  ASC->CancelAbilityHandle(HitHandle);
  ASC->FindAbilitySpecFromHandle(Handle)->InputPressed = true;
  ASC->TryActivateAbility(Handle);
  if (OldTask) { OldTask->OnCompleted.Broadcast(); OldTask->OnInterrupted.Broadcast(); }
  Ability->OnShotCommitted(false, OldShot);
  TestTrue(TEXT("Previous callbacks cannot end next aim"), Ability->IsAimSessionActive());
  Ability->OnRightClickReleased(0.f);
 }
 // Intentional short-draw cancellation keeps aim, but detaches the old montage.
 ASC->FindAbilitySpecFromHandle(Handle)->InputPressed = true;
 ASC->TryActivateAbility(Handle);
 auto* Ability = Cast<UGA_BowAimFire>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
 Ability->OnLeftClickPressed(FGameplayEventData());
 auto* OldDraw = Ability->BowMontageTask.Get();
 Ability->OnLeftClickReleased(FGameplayEventData());
 TestTrue(TEXT("Short draw keeps right-click aim active"), Ability->IsAimSessionActive());
 Ability->OnLeftClickPressed(FGameplayEventData());
 if (OldDraw) OldDraw->OnInterrupted.Broadcast();
 TestTrue(TEXT("Previous draw cannot cancel a new draw"), Ability->bIsDrawing && Ability->IsAimSessionActive());
 auto* Task = Ability->BowMontageTask.Get();
 if (!TestNotNull(TEXT("New draw montage task"), Task)) return false;
 UAnimMontage* Montage = Ability->GetCurrentMontage();
 Task->OnMontageBlendingOut(Montage, false);
 Task->OnMontageEnded(Montage, true);
 TestFalse(TEXT("External interruption after blendout ends aiming"), Ability->IsActive());
 TestFalse(TEXT("External interruption clears visual aim"), Bow->GetBowComponent()->IsAiming());
 return !HasAnyErrors();
}
#endif
