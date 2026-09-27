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
#include "Settings_Item.h"
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
	for (const auto& Pair : Registry->ItemDefinitions)
	{
		if (Pair.Value.WeaponDefinition.IsNull()) continue;
		const auto* Definition = Pair.Value.WeaponDefinition.LoadSynchronous();
		if (!TestNotNull(*Pair.Key.ToString(), Definition)) continue;
		++Migrated;
		TestNotNull(TEXT("Actor class resolves"), Definition->ActorClass.LoadSynchronous());
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
		if (!Definition->CombatData->ProjectileClass.IsNull())
		{
			UClass* Projectile = Definition->CombatData->ProjectileClass.LoadSynchronous();
			TestTrue(TEXT("Bow projectile valid"), Projectile && Projectile->IsChildOf(AArrowProjectile::StaticClass()));
		}
	}
	TestEqual(TEXT("Four authored weapon profiles migrated"), Migrated, 4);
	return true;
}
#endif
