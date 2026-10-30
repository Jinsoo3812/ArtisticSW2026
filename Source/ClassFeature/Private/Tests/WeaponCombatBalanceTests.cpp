#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Attacker/GA_PlayerBasicAttack.h"
#include "Animation/AnimMontage.h"
#include "AnimNotify/AN_SendGameplayEvent.h"
#include "AnimNotify/ANS_HitScanWindow.h"
#include "BaseAttributeSet.h"
#include "BasePlayerState.h"
#include "Equipment/WeaponDefinition.h"
#include "GASCombatLibrary.h"
#include "ItemData.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerAttackQueuedSectionTest,
	"ArtisticSW.Weapon.QueuedComboHitWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerAttackQueuedSectionTest::RunTest(const FString& Parameters)
{
	const UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Sword_Anims/Animations/HandsomeSwordV2/Manny_UE5/RootMotion/Attack/ComboAttack/AM_SwordCombo.AM_SwordCombo"));
	if (!TestNotNull(TEXT("Authored combo montage loads"), Montage)) return false;
	float FirstCommit = MAX_flt, FirstHit = MAX_flt;
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		if (const UAN_SendGameplayEvent* Notify = Cast<UAN_SendGameplayEvent>(Event.Notify))
			if (Notify->GetEventTag().ToString() == TEXT("Event.Attack.Combo.Commit")) FirstCommit = FMath::Min(FirstCommit, Event.GetTime());
		if (Cast<UANS_HitScanWindow>(Event.NotifyStateClass)) FirstHit = FMath::Min(FirstHit, Event.GetTime());
	}
	TestTrue(TEXT("Real authoring queues the next step before the first hit window"), FirstCommit < FirstHit && FirstHit < MAX_flt);
	UGA_PlayerBasicAttack* Ability = NewObject<UGA_PlayerBasicAttack>();
	for (int32 Index = 0; Index < Montage->GetNumSections(); ++Index) Ability->CachedComboSections.Add(Montage->GetSectionName(Index));
	Ability->CurrentComboIndex = 1; // Commit has queued Sec1; montage is still in Default.
	TestEqual(TEXT("Queuing the next combo does not discard the first section's hit"), Ability->ResolveHitWindowSection(TEXT("Default")), 0);
	Ability->LastOpenedComboIndex = 0;
	TestEqual(TEXT("Duplicate first-section notifies remain rejected"), Ability->ResolveHitWindowSection(TEXT("Default")), INDEX_NONE);
	TestEqual(TEXT("The next playing section opens its own hit window"), Ability->ResolveHitWindowSection(TEXT("Sec1")), 1);
	TestEqual(TEXT("A section that has not been queued cannot open"), Ability->ResolveHitWindowSection(TEXT("Sec2")), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponEnemyMatchupTest,
	"ArtisticSW.Weapon.EnemyHealthMatchups", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponEnemyMatchupTest::RunTest(const FString& Parameters)
{
	const UItemData* Items = LoadObject<UItemData>(nullptr, TEXT("/Game/Blueprints/Item/DA_ItemData.DA_ItemData"));
	const UDataTable* Stats = LoadObject<UDataTable>(nullptr, TEXT("/Game/GameplayAbilitySystem/Enemy/Data/DT_EnemyBaseStat.DT_EnemyBaseStat"));
	if (!TestNotNull(TEXT("Catalog"), Items) || !TestNotNull(TEXT("Actual BP-referenced enemy table"), Stats)) return false;
	const FFloatProperty* Health = FindFProperty<FFloatProperty>(Stats->GetRowStruct(), TEXT("MaxHealth"));
	if (!TestNotNull(TEXT("Enemy health property"), Health)) return false;
	const UClass* StateClass = LoadClass<ABasePlayerState>(nullptr, TEXT("/Game/Blueprints/Player/BP_BasePlayerState.BP_BasePlayerState_C"));
	if (!TestNotNull(TEXT("Authored player state"), StateClass)) return false;
	const float BaseStrength = StateClass->GetDefaultObject<ABasePlayerState>()->GetAttributeSet()->GetStrength();
	const FName NormalRows[] = { TEXT("T1_Ground_Melee"), TEXT("T1_Ground_Melee"), TEXT("T2_Ground_Melee"), TEXT("T3_Ground_Melee"), TEXT("T4_Ground_Melee") };
	const FName BossRows[] = { TEXT("T1_Boss"), TEXT("T1_Boss"), TEXT("T2_Boss"), TEXT("T3_Boss"), TEXT("T4_Boss") };
	int32 Count = 0;
	for (const auto& Pair : Items->ItemDefinitions)
	{
		if (Pair.Value.ProgressionKind != EItemProgressionKind::Weapon) continue;
		++Count;
		const int32 Stage = Pair.Value.ProgressionTier;
		if (!TestTrue(TEXT("Supported progression stage"), Stage >= 0 && Stage < UE_ARRAY_COUNT(NormalRows))) continue;
		FString Error;
		const UEquippableWeaponDefinition* Weapon = Items->CreateWeaponDefinition(Pair.Key, GetTransientPackage(), Error);
		if (!TestTrue(TEXT("Runtime combat definition"), Weapon && Weapon->CombatData)) continue;
		const float Damage = UGASCombatLibrary::CalculateStrengthDamage(BaseStrength + Weapon->CombatData->StrengthBonus, Weapon->CombatData->AttackCoefficient, 1.f);
		const uint8* Normal = Stats->FindRowUnchecked(NormalRows[Stage]);
		const uint8* Boss = Stats->FindRowUnchecked(BossRows[Stage]);
		if (!TestTrue(TEXT("Matching enemy rows"), Normal && Boss)) continue;
		const int32 NormalHits = FMath::CeilToInt(Health->GetPropertyValue_InContainer(Normal) / Damage);
		const int32 BossHits = FMath::CeilToInt(Health->GetPropertyValue_InContainer(Boss) / Damage);
		TestTrue(*FString::Printf(TEXT("%s cannot erase a matching normal enemy in one hit"), *Pair.Key.ToString()), NormalHits >= 2 && NormalHits <= 9);
		TestTrue(*FString::Printf(TEXT("%s retains a meaningful boss health budget"), *Pair.Key.ToString()), BossHits >= 4 && BossHits <= 24);
	}
	TestEqual(TEXT("All twenty weapons measured against actual enemy HP"), Count, 20);
	return true;
}
#endif
