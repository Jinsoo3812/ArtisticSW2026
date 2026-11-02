#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PlayerLifeTestWorld.h"
#include "Attacker/GA_PlayerBasicAttack.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "BaseGameplayTags.h"
#include "Equipment/WeaponDefinition.h"
#include "Equipment/WeaponAnimationDataAsset.h"
#include "Item/Weapons/SwordItem.h"
#include "ItemData.h"
#include "GAS/Ability/GA_PlayerHitReaction.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerAttackInterruptionLifecycleTest,
 "ArtisticSW.Weapon.AttackInterruptionLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerAttackInterruptionLifecycleTest::RunTest(const FString& Parameters)
{
 PlayerLifeTests::FWorld Fixture;
 const auto Life = Fixture.SpawnLife();
 auto* Player = Life.Player;
 auto* ASC = Life.State->GetAbilitySystemComponent();
 Player->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
 Player->GetMesh()->InitAnim(true);
 UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr,
  TEXT("/Game/Sword_Anims/Animations/HandsomeSwordV2/Manny_UE5/RootMotion/Attack/ComboAttack/AM_SwordCombo.AM_SwordCombo"));
 if (!TestNotNull(TEXT("Attack montage"), Montage) || !TestNotNull(TEXT("Anim instance"), Player->GetMesh()->GetAnimInstance())) return false;
 auto* Definition = NewObject<UEquippableWeaponDefinition>();
 Definition->AnimationData = NewObject<UWeaponAnimationDataAsset>();
 Definition->CombatData = NewObject<UWeaponCombatDataAsset>();
 const_cast<FWeaponAnimationEntry&>(Definition->AnimationData->GetDefaultEntry()).BasicAttackMontage = Montage;
 FItemDefinition Row;
 Row.WeaponDefinition = Definition;
 auto* Sword = Fixture.World->SpawnActor<ASwordItem>();
 Sword->MyDefinition = &Row;
 Sword->ItemTag = Item_Id_Weapon_Sword_SwordA1;
 Player->EquippedItem = Sword;
 const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UGA_PlayerBasicAttack::StaticClass(), 1, INDEX_NONE, Sword));
 auto Instance = [&]() { return Cast<UGA_PlayerBasicAttack>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance()); };
 auto Activate = [&]() {
  const bool Result = ASC->TryActivateAbility(Handle);
  return TestTrue(TEXT("Fresh attack activates"), Result) && TestTrue(TEXT("Attack remains active"), Instance() && Instance()->IsActive());
 };
 if (!Activate()) return false;
 auto* Ability = Instance();
 auto* OldTask = Ability->AttackMontageTask.Get();
 TestNotNull(TEXT("Attack owns montage task"), OldTask);
 if (!OldTask) return false;
 Ability->bComboInputBuffered = true;
 OldTask->OnMontageBlendingOut(Montage, false);
 TestTrue(TEXT("Normal blendout preserves late combo notifications"), Ability->IsActive());
 OldTask->OnMontageEnded(Montage, true);
 TestFalse(TEXT("Interruption after blendout ends ability"), Ability->IsActive());
 TestFalse(TEXT("Attack tag released"), ASC->HasMatchingGameplayTag(State_Attacking));
 TestFalse(TEXT("Player attack flag released"), Player->bIsAttacking);
 TestFalse(TEXT("Combo buffer discarded"), Ability->bComboInputBuffered);
 TestNull(TEXT("Hit scan event task released"), Ability->HitScanStartEventTask.Get());
 TestFalse(TEXT("Pose refresh ownership released"), Ability->bServerCombatPoseRefreshAcquired);
 if (!Activate()) return false;
 OldTask->OnCompleted.Broadcast();
 OldTask->OnInterrupted.Broadcast();
 TestTrue(TEXT("Old task cannot end the next attack"), Ability->IsActive());
 auto* HitClass = LoadClass<UGA_PlayerHitReaction>(nullptr,
  TEXT("/Game/GameplayAbilitySystem/Ability/GA_HitReact.GA_HitReact_C"));
 if (!TestNotNull(TEXT("Player hit reaction class"), HitClass)) return false;
 const auto HitHandle = ASC->GiveAbility(FGameplayAbilitySpec(HitClass, 1));
 Sword->SetActorLocation(Player->GetActorLocation() + FVector(100, 0, 0));
 FGameplayEventData Hit;
 Hit.Instigator = Sword;
 Hit.Target = Player;
 Hit.EventMagnitude = 1.f;
 ASC->HandleGameplayEvent(GameplayAbility_HitReaction, &Hit);
 TestFalse(TEXT("Hit cancellation ends attack"), Ability->IsActive());
 TestFalse(TEXT("Hit cancellation releases attack tag"), ASC->HasMatchingGameplayTag(State_Attacking));
 TestTrue(TEXT("Hit reaction blocks attacks"), ASC->HasMatchingGameplayTag(State_Damaged));
 TestFalse(TEXT("Attack rejected during hit reaction"), ASC->TryActivateAbility(Handle));
 auto* HitAbility = ASC->FindAbilitySpecFromHandle(HitHandle)->GetPrimaryInstance();
 if (!TestNotNull(TEXT("Active hit reaction"), HitAbility)) return false;
 const auto* TaskProperty = FindFProperty<FObjectProperty>(HitClass, TEXT("HitReactionMontageTask"));
 auto* HitTask = TaskProperty ? Cast<UAbilityTask_PlayMontageAndWait>(TaskProperty->GetObjectPropertyValue_InContainer(HitAbility)) : nullptr;
 if (!TestNotNull(TEXT("Hit reaction montage task"), HitTask)) return false;
 UAnimMontage* HitMontage = HitAbility->GetCurrentMontage();
 HitTask->OnMontageBlendingOut(HitMontage, false);
 HitTask->OnMontageEnded(HitMontage, true);
 TestFalse(TEXT("Interrupted hit reaction ends"), HitAbility->IsActive());
 TestFalse(TEXT("Hit reaction tag released"), ASC->HasMatchingGameplayTag(State_Damaged));
 TestFalse(TEXT("Player hit flag released"), Player->bIsHitReacting);
 if (!Activate()) return false;
 Ability->AttackMontageTask->OnMontageEnded(Montage, false);
 TestFalse(TEXT("Normal completion ends attack"), Ability->IsActive());
 // Duplicate completion must not consume state belonging to a different action.
 ASC->AddLooseGameplayTag(State_Attacking);
 Ability->OnAttackMontageCompleted();
 TestEqual(TEXT("Duplicate end preserves foreign tag count"), ASC->GetTagCount(State_Attacking), 1);
 ASC->RemoveLooseGameplayTag(State_Attacking);
 // A failed activation with foreign attack state must not remove that state either.
 ASC->AddLooseGameplayTag(State_Attacking);
 ASC->TryActivateAbility(Handle);
 TestEqual(TEXT("Rejected activation preserves foreign tag count"), ASC->GetTagCount(State_Attacking), 1);
 ASC->RemoveLooseGameplayTag(State_Attacking);
 Player->GetMesh()->SetAnimInstanceClass(nullptr);
 ASC->RefreshAbilityActorInfo();
 ASC->TryActivateAbility(Handle);
 TestFalse(TEXT("Playback failure ends synchronously"), Ability->IsActive());
 TestFalse(TEXT("Playback failure leaves no attack tag"), ASC->HasMatchingGameplayTag(State_Attacking));
 TestFalse(TEXT("Playback failure leaves no attack flag"), Player->bIsAttacking);
 TestEqual(TEXT("Playback failure does not restore a combo index after cleanup"), Ability->CurrentComboIndex, INDEX_NONE);
 return !HasAnyErrors();
}
#endif
