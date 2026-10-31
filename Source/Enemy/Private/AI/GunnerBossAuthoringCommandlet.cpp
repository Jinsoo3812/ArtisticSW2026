#include "AI/GunnerBossAuthoringCommandlet.h"

#if WITH_EDITOR
#include "AI/EnemyBehaviorSet.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BossAI/GunnerBossEnemy.h"
#include "BossAI/GunnerBossCombatData.h"
#include "BossAI/GunnerBossTags.h"
#include "Components/StaticMeshComponent.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/StaticMesh.h"
#include "GAS/Ability/Boss/GA_BossGunAttack.h"
#include "GAS/Ability/Boss/GA_BossBasicAttack.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Task/BTT_GunnerBossCombat.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Weapon/BaseWeapon.h"
#include "Weapon/BaseWeaponComponent.h"
#include "Weapon/WeaponDataAsset.h"

namespace T1GunnerAuthoring
{
	const FString Folder = TEXT("/Game/GameplayAbilitySystem/Enemy/Balancing/T1/");
	template<class T> T* Load(const FString& Path)
	{
		return LoadObject<T>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path)));
	}
	template<class T> T* Dedicated(const TCHAR* Name, T* Template = nullptr)
	{
		const FString Path = Folder + Name;
		if (T* Existing = Load<T>(Path)) return Existing;
		UPackage* Package = CreatePackage(*Path);
		return Template ? CastChecked<T>(StaticDuplicateObject(Template, Package, Name))
			: NewObject<T>(Package, Name, RF_Public | RF_Standalone | RF_Transactional);
	}
	bool Save(UObject* Asset)
	{
		Asset->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Asset->GetPackage(), Asset,
			*FPackageName::LongPackageNameToFilename(Asset->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
	}
	void SetObject(UObject* Owner, FName Name, UObject* Value)
	{
		auto* Property = FindFProperty<FObjectPropertyBase>(Owner->GetClass(), Name);
		check(Property);
		Property->SetObjectPropertyValue_InContainer(Owner, Value);
	}
	UBehaviorTree* CombatTree(UBlackboardData* Blackboard)
	{
		auto* Tree = Dedicated<UBehaviorTree>(TEXT("BT_T1Boss_GunnerCombat"));
		// Preserve an authored tree. Fail rather than silently overwrite an unrelated graph.
		if (Tree->RootNode)
			return Tree->BlackboardAsset == Blackboard && (Tree->RootNode->Children.Num() == 1 || Cast<UBTComposite_SimpleParallel>(Tree->RootNode))
				&& Cast<UBTT_GunnerBossCombat>(Tree->RootNode->Children[0].ChildTask) ? Tree : nullptr;
		Tree->BlackboardAsset = Blackboard;
		Tree->RootNode = NewObject<UBTComposite_Selector>(Tree, NAME_None, RF_Transactional);
		Tree->RootNode->Children.AddDefaulted_GetRef().ChildTask = NewObject<UBTT_GunnerBossCombat>(Tree, NAME_None, RF_Transactional);
		auto* Graph = CastChecked<UBehaviorTreeGraph>(FBlueprintEditorUtils::CreateNewGraph(Tree, TEXT("Behavior Tree"),
			UBehaviorTreeGraph::StaticClass(), GetDefault<UBehaviorTreeGraph>()->Schema));
		Tree->BTGraph = Graph;
		Graph->LockUpdates();
		Graph->GetSchema()->CreateDefaultNodesForGraph(*Graph);
		Tree->BlackboardAsset = Blackboard;
		for (UEdGraphNode* Node : Graph->Nodes)
			if (auto* Root = Cast<UBehaviorTreeGraphNode_Root>(Node)) Root->BlackboardAsset = Blackboard;
		Graph->OnCreated(); Graph->Initialize(); Graph->UnlockUpdates();
		return Tree;
	}
}
#endif

UGunnerBossAuthoringCommandlet::UGunnerBossAuthoringCommandlet()
{
	IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}

int32 UGunnerBossAuthoringCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace T1GunnerAuthoring;
	UBlueprint* BossBP = Load<UBlueprint>(Folder + TEXT("BP_T1Boss_Enemy"));
	auto* Boss = BossBP && BossBP->GeneratedClass ? Cast<AGunnerBossEnemy>(BossBP->GeneratedClass->GetDefaultObject()) : nullptr;
	auto* RootTree = Load<UBehaviorTree>(TEXT("/Game/GameplayAbilitySystem/Enemy/AI/BT_BossBase"));
	auto* AIBase = Load<UEnemyBehaviorSet>(TEXT("/Game/GameplayAbilitySystem/Enemy/DA/AI/T1_DA_ShipBoss_AI"));
	auto* Rifle = Load<UStaticMesh>(TEXT("/Game/Fab/Vetterli_M78_Rifle/Rifle"));
	if (!Boss || !RootTree || !RootTree->BlackboardAsset || !AIBase || !Rifle)
	{
		UE_LOG(LogTemp, Error, TEXT("T1 gunner authoring: missing BP/GunnerBossEnemy parent, root BT/Blackboard, AI template or rifle. No assets saved."));
		return 1;
	}
	UBehaviorTree* Tree = CombatTree(RootTree->BlackboardAsset);
	if (!Tree) return 1;
	auto* Combat = Dedicated<UGunnerBossCombatData>(TEXT("T1_DA_GunnerCombat"));
	auto* Set = Dedicated<UEnemyBehaviorSet>(TEXT("T1_DA_GunnerAI"), AIBase);
	Set->ConfigureEditorSubtree(EEnemyAIState::Combat, FGameplayTag::RequestGameplayTag(TEXT("AI.Behavior.Combat")), Tree);

	UBlueprint* GunBP = Load<UBlueprint>(Folder + TEXT("BP_T1Boss_Gun"));
	if (!GunBP)
		GunBP = FKismetEditorUtilities::CreateBlueprint(ABaseWeapon::StaticClass(),
			CreatePackage(*(Folder + TEXT("BP_T1Boss_Gun"))), TEXT("BP_T1Boss_Gun"), BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!GunBP || !GunBP->GeneratedClass || !GunBP->GeneratedClass->IsChildOf(ABaseWeapon::StaticClass())) return 1;
	auto* Gun = GunBP->GeneratedClass->GetDefaultObject<ABaseWeapon>();
	// Only initialize the mesh when empty; retain later visual adjustments on rerun.
	if (!Gun->GetWeaponMesh()->GetStaticMesh()) Gun->GetWeaponMesh()->SetStaticMesh(Rifle);
	Gun->GetWeaponMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Gun->GetWeaponMesh()->SetSimulatePhysics(false);
	FBlueprintEditorUtils::MarkBlueprintAsModified(GunBP);
	FKismetEditorUtilities::CompileBlueprint(GunBP);
	if (GunBP->Status == BS_Error) return 1;

	auto* Weapons = Dedicated<UWeaponDataAsset>(TEXT("T1_DA_GunnerWeapons"));
	FWeaponDefinition* Entry = Weapons->WeaponDefinitions.FindByPredicate([](const FWeaponDefinition& Def) { return Def.WeaponTag == GunnerBossTags::WeaponGun; });
	if (!Entry)
	{
		Entry = &Weapons->WeaponDefinitions.AddDefaulted_GetRef();
		Entry->WeaponTag = GunnerBossTags::WeaponGun;
		Entry->SocketData.EquipSocketName = TEXT("Gun_Grip");
	}
	Entry->WeaponActorClass = GunBP->GeneratedClass;
	Entry->AbilityData.GrantedAbilities.Reset();
	Boss->CombatData = Combat;
	SetObject(Boss->GetWeaponComponent(), TEXT("WeaponRegistry"), Weapons);
	SetObject(Boss, TEXT("BehaviorTree"), RootTree);
	SetObject(Boss, TEXT("BehaviorSet"), Set);
	FindFProperty<FBoolProperty>(Boss->GetClass(), TEXT("bEquipWeaponOnSpawn"))->SetPropertyValue_InContainer(Boss, true);
	*FindFProperty<FStructProperty>(Boss->GetClass(), TEXT("DefaultWeaponTag"))->ContainerPtrToValuePtr<FGameplayTag>(Boss) = GunnerBossTags::WeaponGun;
	auto* Abilities = FindFProperty<FArrayProperty>(Boss->GetClass(), TEXT("StartingAbilities"))->ContainerPtrToValuePtr<TArray<TSubclassOf<UGameplayAbility>>>(Boss);
	Abilities->RemoveAll([](const TSubclassOf<UGameplayAbility>& Class)
	{
		return Class && (Class->IsChildOf(UBossGameplayAbility::StaticClass())
			|| Class->IsChildOf(UGA_BossBasicAttack::StaticClass()));
	});
	Abilities->Add(UGA_BossGunAttack::StaticClass());
	FBlueprintEditorUtils::MarkBlueprintAsModified(BossBP);
	FKismetEditorUtilities::CompileBlueprint(BossBP);
	if (BossBP->Status == BS_Error) return 1;
	for (UObject* Asset : TArray<UObject*>{Tree, Combat, Set, GunBP, Weapons, BossBP})
		if (!T1GunnerAuthoring::Save(Asset)) return 1;
	UE_LOG(LogTemp, Display, TEXT("T1_GUNNER_AUTHORING_OK: BP, gun, registry, CombatData, shared root and dedicated Combat subtree saved. Manual: grip/muzzle sockets, montage/AnimBP, sound/flash, stats and encounter placement. PIE not verified."));
	return 0;
#else
	return 1;
#endif
}
