#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BasePlayer.h"
#include "BasePlayerState.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "WeaponInputAbilitySystemComponent.h"
#include "Equipment/PlayerEquipmentComponent.h"
#include "Equipment/WeaponDefinition.h"
#include "Equipment/WeaponAnimationDataAsset.h"
#include "GAS/Ability/WeaponGameplayAbility.h"
#include "Attacker/GA_PlayerBasicAttack.h"
#include "Item/BaseItem.h"
#include "Item/Projectiles/ArrowProjectile.h"
#include "ItemData.h"
#include "ItemSubsystem.h"
#include "Settings_Item.h"
#include "Balance/FixedChestDropData.h"
#include "Item/Weapons/SwordItem.h"
#include "Item/Weapons/BowItem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponEquipmentLifecycleTest,
	"ArtisticSW.Weapon.EquipmentLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponEquipmentLifecycleTest::RunTest(const FString& Parameters)
{
	// This unit fixture must not depend on unrelated quest crafting table authoring.
	TGuardValue<TSoftObjectPtr<UDataTable>> CraftingTableGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("WeaponEquipmentTest"));
	if (!TestNotNull(TEXT("World"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ABasePlayer* Player = World->SpawnActor<ABasePlayer>();
	ABasePlayerState* State = World->SpawnActor<ABasePlayerState>();
	auto* ASC = Cast<UWeaponInputAbilitySystemComponent>(State->GetAbilitySystemComponent());
	TestNotNull(TEXT("PlayerState uses tag input ASC"), ASC);
	ASC->InitAbilityActorInfo(State, Player);
	Player->CachedAbilitySystemComponent = ASC;
	UPlayerEquipmentComponent* Equipment = Player->GetEquipmentComponent();
	Equipment->PlayerOwner = Player;
	UEquippableWeaponDefinition* Definition = NewObject<UEquippableWeaponDefinition>();
	Definition->AbilitySet = NewObject<UWeaponAbilitySet>();
	Definition->AnimationData = NewObject<UWeaponAnimationDataAsset>();
	Definition->CombatData = NewObject<UWeaponCombatDataAsset>();
	FWeaponAbilityEntry Entry;
	Entry.InputTag = Key_Default_Mouse_LeftClick;
	Entry.AbilityClass = UGA_PlayerBasicAttack::StaticClass();
	Definition->AbilitySet->Abilities.Add(Entry);
	FItemDefinition Row;
	Row.ProgressionKind = EItemProgressionKind::Weapon;
	Row.WeaponDefinition = Definition;
	ABaseItem* Weapon = World->SpawnActor<ABaseItem>();
	Weapon->MyDefinition = &Row;
	TestTrue(TEXT("Valid weapon grants"), Equipment->GrantEquippedItemAbility(Weapon));
	TestTrue(TEXT("Repeated grant is idempotent"), Equipment->GrantEquippedItemAbility(Weapon));
	TestEqual(TEXT("One spec"), ASC->GetActivatableAbilities().Num(), 1);
	const FGameplayAbilitySpecHandle Handle = Equipment->ItemGrants.FindChecked(Weapon).Abilities[0];
	FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
	TestTrue(TEXT("Weapon is source object"), Spec && Spec->SourceObject == Weapon);
	TestEqual(TEXT("No CRC input ID"), Spec->InputID, INDEX_NONE);
	TestFalse(TEXT("Pending source cannot activate"), Spec->Ability->CanActivateAbility(Handle, ASC->AbilityActorInfo.Get()));
	Player->EquippedItem = Weapon;
	TestTrue(TEXT("Equipped source can activate"), Spec->Ability->CanActivateAbility(Handle, ASC->AbilityActorInfo.Get()));
	ASC->InputTagPressed(Key_Default_Mouse_LeftClick);
	TestTrue(TEXT("Exact tag press reaches weapon spec"), Spec->InputPressed);
	ASC->InputTagReleased(Key_Default_Mouse_LeftClick);
	TestFalse(TEXT("Release clears input state"), Spec->InputPressed);
	Player->GrantDefaultAbility(Entry.AbilityClass);
	Player->GrantAbilityToSlot(Entry.InputTag, Entry.AbilityClass);
	TestEqual(TEXT("Legacy routes cannot grant weapon abilities"), ASC->GetActivatableAbilities().Num(), 1);
	// A grant owned elsewhere must survive equipment removal, even with the same class and input.
	FGameplayAbilitySpec Other(Entry.AbilityClass, 1, INDEX_NONE, Player);
	Other.GetDynamicSpecSourceTags().AddTag(Entry.InputTag);
	const FGameplayAbilitySpecHandle OtherHandle = ASC->GiveAbility(Other);
	Definition->AbilitySet->Abilities.Add(Entry);
	ABaseItem* Invalid = World->SpawnActor<ABaseItem>();
	Invalid->MyDefinition = &Row;
	TestFalse(TEXT("Duplicate input rejected before replacing current weapon"), Equipment->GrantEquippedItemAbility(Invalid));
	TestNotNull(TEXT("Old weapon grant remains"), ASC->FindAbilitySpecFromHandle(Handle));
	Definition->AbilitySet->Abilities.Pop();
	Equipment->RemoveEquippedItemAbility(Weapon);
	TestNull(TEXT("Owned grant removed"), ASC->FindAbilitySpecFromHandle(Handle));
	TestNotNull(TEXT("Unrelated grant preserved"), ASC->FindAbilitySpecFromHandle(OtherHandle));
	Equipment->GrantEquippedItemAbility(Weapon);
	const auto DestroyedHandle = Equipment->ItemGrants.FindChecked(Weapon).Abilities[0];
	Weapon->Destroy();
	TestNull(TEXT("Destroy removes grant"), ASC->FindAbilitySpecFromHandle(DestroyedHandle));
	TestNull(TEXT("Destroy clears equipped pointer"), Player->EquippedItem.Get());
	ABaseItem* Next = World->SpawnActor<ABaseItem>();
	Next->MyDefinition = &Row;
	Equipment->GrantEquippedItemAbility(Next);
	Player->EquippedItem = Next;
	const auto DeathHandle = Equipment->ItemGrants.FindChecked(Next).Abilities[0];
	ASC->AddLooseGameplayTag(State_Dead);
	TestNull(TEXT("Death removes weapon grant from persistent ASC"), ASC->FindAbilitySpecFromHandle(DeathHandle));
	TestNotNull(TEXT("Death preserves unrelated grant"), ASC->FindAbilitySpecFromHandle(OtherHandle));
	Invalid->MyDefinition = nullptr;
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponDefinitionAssetsTest,
	"ArtisticSW.Weapon.DefinitionAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponDefinitionAssetsTest::RunTest(const FString& Parameters)
{
	const UItemData* Registry = LoadObject<UItemData>(nullptr, TEXT("/Game/Blueprints/Item/DA_ItemData.DA_ItemData"));
	if (!TestNotNull(TEXT("Registry"), Registry)) return false;
	for (const TCHAR* PlayerPath : {TEXT("/Game/Blueprints/Player/BP_Player_Man.BP_Player_Man_C"),
		TEXT("/Game/Blueprints/Player/BP_Player_Woman.BP_Player_Woman_C")})
		TestNotNull(TEXT("Player Blueprint loads after property removal"), LoadClass<ABasePlayer>(nullptr, PlayerPath));
	UClass* StateClass = LoadClass<ABasePlayerState>(nullptr, TEXT("/Game/Blueprints/Player/BP_BasePlayerState.BP_BasePlayerState_C"));
	if (TestNotNull(TEXT("PlayerState BP loads"), StateClass))
		TestNotNull(TEXT("Blueprint PlayerState inherits tag input ASC"), Cast<UWeaponInputAbilitySystemComponent>(StateClass->GetDefaultObject<ABasePlayerState>()->GetAbilitySystemComponent()));
	int32 Migrated = 0;
	TSet<const UDataTable*> BalanceTables;
	TSet<const UWeaponFamilyProfile*> FamilyProfiles;
	for (const auto& Pair : Registry->ItemDefinitions)
	{
		if (Pair.Value.ProgressionKind != EItemProgressionKind::Weapon) continue;
		TestTrue(TEXT("No individual weapon DA reference remains"), Pair.Value.WeaponDefinition.IsNull());
		TestFalse(TEXT("Catalog references a DT row directly"), Pair.Value.WeaponBalanceRow.IsNull());
		FString ResolveError;
		const auto* Definition = Registry->CreateWeaponDefinition(Pair.Key, GetTransientPackage(), ResolveError);
		if (!TestNotNull(*Pair.Key.ToString(), Definition)) continue;
		TestTrue(TEXT("Weapon definition is runtime-only"), Definition->HasAnyFlags(RF_Transient));
		const FWeaponBalanceRow* Balance = Definition->BalanceRow.GetRow<FWeaponBalanceRow>(TEXT("Weapon catalog test"));
		if (TestNotNull(TEXT("Balance row resolves"), Balance))
		{
			TestTrue(TEXT("Balance row retains the stable item identity"), Balance->ItemTag == Pair.Key);
			BalanceTables.Add(Definition->BalanceRow.DataTable);
			FamilyProfiles.Add(Balance->Profile);
			if (Definition->CombatData)
			{
				TestEqual(TEXT("Runtime strength comes from DT"), Definition->CombatData->StrengthBonus, Balance->StrengthBonus);
				TestEqual(TEXT("Runtime damage coefficient comes from DT"), Definition->CombatData->AttackCoefficient, Balance->AttackCoefficient);
				TestTrue(TEXT("Combat numbers are runtime-only copies"), Definition->CombatData->HasAnyFlags(RF_Transient));
			}
		}
		++Migrated;
		UClass* ActorClass = Definition->ActorClass.LoadSynchronous();
		TestNotNull(TEXT("Actor class resolves"), ActorClass);
		const bool bBow = Pair.Key.MatchesTag(Item_Id_Weapon_Bow);
		TestTrue(TEXT("Weapon actor matches its item family"), ActorClass && ActorClass->IsChildOf(
			bBow ? ABowItem::StaticClass() : ASwordItem::StaticClass()));
		TestTrue(TEXT("Legacy GA and spawn paths cleared"), Pair.Value.GrantedAbilityClass.IsNull()
			&& Pair.Value.SpawnClassByCrafting.IsNull() && Pair.Value.SpawnClass.IsNull() && !Pair.Value.UseKeyTag.IsValid());
		if (!TestNotNull(TEXT("Ability set"), Definition->AbilitySet.Get())
			|| !TestNotNull(TEXT("Animation data redirect resolves"), Definition->AnimationData.Get())
			|| !TestNotNull(TEXT("Combat data"), Definition->CombatData.Get())) continue;
		TestEqual(TEXT("One activation entry"), Definition->AbilitySet->Abilities.Num(), 1);
		for (const auto& Ability : Definition->AbilitySet->Abilities)
		{
			TestTrue(TEXT("Weapon GA subclass"), Ability.AbilityClass && Ability.AbilityClass->IsChildOf(UWeaponGameplayAbility::StaticClass()));
			TestTrue(TEXT("Valid input tag"), Ability.InputTag.IsValid());
		}
		const auto* Animation = Definition->AnimationData->FindEntryForTag(Pair.Key);
		TestTrue(TEXT("Weapon has attack montage"), Animation && (Animation->BasicAttackMontage || Animation->AimCycleMontage));
		if (Animation)
		{
			const float Rate = bBow ? Animation->AimCyclePlayRate : Animation->BasicAttackPlayRate;
			TestTrue(TEXT("Attack animation speed is finite and positive"), FMath::IsFinite(Rate) && Rate > 0.f);
			if (Balance)
				TestEqual(TEXT("Runtime attack speed comes from DT"), Rate, bBow ? Balance->AimCyclePlayRate : Balance->BasicAttackPlayRate);
		}
		TestTrue(TEXT("Weapon strength is finite and nonnegative"), FMath::IsFinite(Definition->CombatData->StrengthBonus)
			&& Definition->CombatData->StrengthBonus >= 0.f);
		TestTrue(TEXT("Weapon damage coefficient is finite and positive"), FMath::IsFinite(Definition->CombatData->AttackCoefficient)
			&& Definition->CombatData->AttackCoefficient > 0.f);
		if (bBow)
		{
			UClass* Projectile = Definition->CombatData->ProjectileClass.LoadSynchronous();
			TestTrue(TEXT("Bow projectile valid"), Projectile && Projectile->IsChildOf(AArrowProjectile::StaticClass()));
		}
	}
	TestEqual(TEXT("All twenty authored weapons have complete profiles"), Migrated, 20);
	TestEqual(TEXT("One authoritative weapon balance table"), BalanceTables.Num(), 1);
	TestEqual(TEXT("Four shared family profiles"), FamilyProfiles.Num(), 4);
	for (const TCHAR* Family : { TEXT("Sword.SwordA"), TEXT("Sword.SwordB"), TEXT("Bow.ShortBow"), TEXT("Bow.LongBow") })
	{
		float PreviousStrength = -1.f;
		for (int32 Tier = 1; Tier <= 5; ++Tier)
		{
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*FString::Printf(TEXT("Item.Id.Weapon.%s%d"), Family, Tier)));
			const FItemDefinition* Item = Registry->FindItemDefinition(Tag);
			FString ResolveError;
			const UEquippableWeaponDefinition* Definition = Item ? Registry->CreateWeaponDefinition(Tag, GetTransientPackage(), ResolveError) : nullptr;
			if (!TestTrue(*Tag.ToString(), Definition && Definition->CombatData)) continue;
			TestTrue(TEXT("Higher weapon grades increase strength"), Definition->CombatData->StrengthBonus > PreviousStrength);
			PreviousStrength = Definition->CombatData->StrengthBonus;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponBalanceResolutionTest,
	"ArtisticSW.Weapon.BalanceTableResolution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponBalanceResolutionTest::RunTest(const FString& Parameters)
{
	UWeaponFamilyProfile* Profile = NewObject<UWeaponFamilyProfile>();
	Profile->ActorClass = ASwordItem::StaticClass();
	Profile->AbilitySet = NewObject<UWeaponAbilitySet>(Profile);
	FWeaponAbilityEntry& Ability = Profile->AbilitySet->Abilities.AddDefaulted_GetRef();
	Ability.InputTag = Key_Default_Mouse_LeftClick;
	Ability.AbilityClass = UGA_PlayerBasicAttack::StaticClass();
	Profile->AnimationData = NewObject<UWeaponAnimationDataAsset>(Profile);
	const float SharedSpeed = Profile->AnimationData->GetDefaultEntry().BasicAttackPlayRate;
	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FWeaponBalanceRow::StaticStruct();
	FWeaponBalanceRow Row;
	Row.ItemTag = FGameplayTag::RequestGameplayTag(TEXT("Item.Id.Weapon.Sword.SwordA1"));
	Row.Profile = Profile;
	Row.StrengthBonus = 17.f;
	Row.AttackCoefficient = 1.25f;
	Row.BasicAttackPlayRate = 1.15f;
	Table->AddRow(TEXT("Probe"), Row);
	UEquippableWeaponDefinition* Definition = NewObject<UEquippableWeaponDefinition>();
	Definition->BalanceRow.DataTable = Table;
	Definition->BalanceRow.RowName = TEXT("Probe");
	FString Error;
	if (!TestTrue(TEXT("Valid DT row resolves through existing definition API"), Definition->RefreshBalanceData(Error))) return false;
	TestEqual(TEXT("Table strength is applied"), Definition->CombatData->StrengthBonus, 17.f);
	TestEqual(TEXT("Table coefficient is applied"), Definition->CombatData->AttackCoefficient, 1.25f);
	TestEqual(TEXT("Table animation speed is applied"), Definition->AnimationData->GetDefaultEntry().BasicAttackPlayRate, 1.15f);
	TestEqual(TEXT("Shared animation asset remains unchanged"), Profile->AnimationData->GetDefaultEntry().BasicAttackPlayRate, SharedSpeed);
	TestTrue(TEXT("Ability set is shared"), Definition->AbilitySet == Profile->AbilitySet);
	Row.StrengthBonus = 29.f;
	Row.BasicAttackPlayRate = 1.3f;
	Table->AddRow(TEXT("Probe"), Row);
	TestTrue(TEXT("New sessions can refresh an already loaded alias"), Definition->RefreshBalanceData(Error));
	TestEqual(TEXT("DT edit overrides prior runtime values"), Definition->CombatData->StrengthBonus, 29.f);
	Row.AttackCoefficient = -1.f;
	Table->AddRow(TEXT("Probe"), Row);
	TestFalse(TEXT("Invalid DT data is rejected"), Definition->RefreshBalanceData(Error));
	TestNull(TEXT("Rejected data cannot expose stale combat values"), Definition->CombatData.Get());
	TestFalse(TEXT("Rejected data supplies a diagnostic"), Error.IsEmpty());
	Definition->BalanceRow.RowName = TEXT("Missing");
	TestFalse(TEXT("Missing row is rejected"), Definition->RefreshBalanceData(Error));
	UEquippableWeaponDefinition* Legacy = NewObject<UEquippableWeaponDefinition>();
	Legacy->CombatData = NewObject<UWeaponCombatDataAsset>(Legacy);
	Legacy->CombatData->StrengthBonus = 9.f;
	TestTrue(TEXT("Unmigrated standalone definitions remain supported"), Legacy->RefreshBalanceData(Error));
	TestEqual(TEXT("Legacy fallback retains authored values"), Legacy->CombatData->StrengthBonus, 9.f);
	UItemData* Catalog = NewObject<UItemData>();
	FItemDefinition Item;
	Item.ProgressionKind = EItemProgressionKind::Weapon;
	Item.WeaponBalanceRow.DataTable = Table;
	Item.WeaponBalanceRow.RowName = TEXT("Probe");
	Catalog->ItemDefinitions.Add(Row.ItemTag, Item);
	Row.AttackCoefficient = 1.f;
	Table->AddRow(TEXT("Probe"), Row);
	TestNotNull(TEXT("Direct catalog DT reference resolves without an asset"), Catalog->CreateWeaponDefinition(Row.ItemTag, Catalog, Error));
	Row.ItemTag = Item_Id_Weapon_Bow_ShortBow1;
	Table->AddRow(TEXT("Probe"), Row);
	TestNull(TEXT("Wrong item identity is rejected instead of equipping another row"), Catalog->CreateWeaponDefinition(
		FGameplayTag::RequestGameplayTag(TEXT("Item.Id.Weapon.Sword.SwordA1")), Catalog, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponDirectCatalogTest,
	"ArtisticSW.Weapon.DirectCatalogResolution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponDirectCatalogTest::RunTest(const FString& Parameters)
{
	TGuardValue<TSoftObjectPtr<UDataTable>> RecipeGuard(GetMutableDefault<USettings_Item>()->CraftingRecipeDataTable, {});
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("DirectWeaponCatalogTest"));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	UItemSubsystem* Items = World->GetSubsystem<UItemSubsystem>();
	const UItemData* Catalog = Items ? Items->GetItemDataAsset() : nullptr;
	int32 Count = 0;
	if (TestNotNull(TEXT("World owns the catalog"), Catalog))
	{
		for (const auto& Pair : Catalog->ItemDefinitions)
		{
			if (Pair.Value.ProgressionKind != EItemProgressionKind::Weapon) continue;
			++Count;
			UEquippableWeaponDefinition* Runtime = Items->GetWeaponDefinition(Pair.Key);
			if (!TestNotNull(*Pair.Key.ToString(), Runtime)) continue;
			TestTrue(TEXT("Each world owns its runtime definition"), Runtime->GetOuter() == Items && Runtime->HasAnyFlags(RF_Transient));
			ABaseItem* Weapon = World->SpawnActor<ABaseItem>();
			Weapon->ItemTag = Pair.Key;
			Weapon->MyDefinition = &Pair.Value;
			TestTrue(TEXT("Existing actor API uses the cached direct DT result"), Weapon->GetWeaponDefinition() == Runtime);
			TestTrue(TEXT("Repeated lookup reuses the cache"), Items->GetWeaponDefinition(Pair.Key) == Runtime);
		}
	}
	TestEqual(TEXT("All twenty weapons resolve without DA files"), Count, 20);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkillMaterialDropAssetsTest,
	"ArtisticSW.Skill.MaterialDropAssets", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkillMaterialDropAssetsTest::RunTest(const FString& Parameters)
{
	const USettings_Item* Settings = GetDefault<USettings_Item>();
	const UFixedChestDropData* Drops = Settings->FixedChestDropData.LoadSynchronous();
	const UItemData* Items = Settings->ItemAssetRegistry.LoadSynchronous();
	if (!TestNotNull(TEXT("Configured fixed drops load"), Drops) || !TestNotNull(TEXT("Configured item registry loads"), Items)) return false;
	const FGameplayTag Materials[] = { Item_Id_Material_SkillMaterial_RareSkill,
		Item_Id_Material_SkillMaterial_EpicSkill, Item_Id_Material_SkillMaterial_LegendarySkill };
	const EProgressionZone FirstSupplyZones[] = { EProgressionZone::Mid1, EProgressionZone::Mid2, EProgressionZone::Mid3 };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Materials); ++Index)
	{
		TestNotNull(TEXT("Skill material is registered"), Items->FindItemDefinition(Materials[Index]));
		int32 Matches = 0;
		for (const FFixedChestDropEntry& Entry : Drops->Drops)
		{
			if (Entry.ItemTag != Materials[Index]) continue;
			++Matches;
			TestTrue(TEXT("Skill material stack is positive"), Entry.Quantity > 0);
			TestTrue(TEXT("Skill material is available before its campaign unlock"), Entry.GetChance(FirstSupplyZones[Index]) > 0.f);
			TestTrue(TEXT("Skill material remains available in the final zone"), Entry.GetChance(EProgressionZone::Final) > 0.f);
			TSet<EProgressionZone> SeenZones;
			for (const FFixedChestZoneChance& ZoneChance : Entry.ZoneChances)
			{
				TestFalse(TEXT("One probability per zone"), SeenZones.Contains(ZoneChance.Zone));
				SeenZones.Add(ZoneChance.Zone);
				TestTrue(TEXT("Valid probability percentage"), FMath::IsFinite(ZoneChance.ChancePercent)
					&& ZoneChance.ChancePercent >= 0.f && ZoneChance.ChancePercent <= 100.f);
			}
		}
		TestEqual(TEXT("Skill material has exactly one independent drop entry"), Matches, 1);
	}
	return true;
}
#endif
