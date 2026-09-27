#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "BaseGameplayTags.h"
#include "Components/EquipmentStatComponent.h"
#include "Components/CombatHitResolverComponent.h"
#include "Components/CombatPresentationComponent.h"
#include "GASCombatLibrary.h"
#include "GASAttributeDamageGameplayEffect.h"
#include "GASDamageInstantGameplayEffect.h"
#include "GASStrengthEquipmentGameplayEffect.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include <limits>
#include "Item/Projectiles/ArrowProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/BoxComponent.h"
#include "BaseCharacter.h"
#include "CollisionChannels.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Components/CombatHurtboxComponent.h"

namespace CombatArchitectureTests
{
	struct FWorldScope
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldScope() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
		~FWorldScope() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		UAbilitySystemComponent* MakeASC(AActor*& Actor)
		{
			Actor = World->SpawnActor<AActor>();
			auto* ASC = NewObject<UAbilitySystemComponent>(Actor);
			Actor->AddInstanceComponent(ASC);
			ASC->RegisterComponent();
			ASC->InitAbilityActorInfo(Actor, Actor);
			ASC->AddAttributeSetSubobject(NewObject<UBaseAttributeSet>(Actor));
			return ASC;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimatedCombatHurtboxPolicyTest,
	"ArtisticSW.GAS.Combat.AnimatedHurtboxPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAnimatedCombatHurtboxPolicyTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("QuestItem (has an invalid ResultItemTag|contains an invalid ingredient)"),
		EAutomationExpectedErrorFlags::Contains, 0);
	CombatArchitectureTests::FWorldScope Scope;
	ABaseCharacter* Target = Scope.World->SpawnActor<ABaseCharacter>();
	if (!TestNotNull(TEXT("Target character is created"), Target)) return false;
	Target->CombatHurtboxComponent->Mode = ECombatHurtboxMode::AnimatedPhysicsAsset;
	Target->GetMesh()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Fab/Samurai/UE_Samurai/SKM_UE_Samurai.SKM_UE_Samurai")));
	Target->GetMesh()->SetPhysicsAsset(LoadObject<UPhysicsAsset>(nullptr, TEXT("/Game/Fab/Samurai/SKM_Samurai_PhysicsAsset.SKM_Samurai_PhysicsAsset")), true);
	Target->InitializeAnimatedCombatHurtbox();
	TestTrue(TEXT("PhysicsAsset-backed animated hurtbox activates"), Target->UsesAnimatedCombatHurtbox());
	TestEqual(TEXT("Skeletal mesh owns the combat hurtbox object channel"),
		Target->GetMesh()->GetCollisionObjectType(), ECC_CombatHurtbox);
	TestEqual(TEXT("Animated hurtbox remains query-only"),
		Target->GetMesh()->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestEqual(TEXT("Arrow collision bypasses the movement capsule"),
		Target->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Arrow), ECR_Ignore);
	TestEqual(TEXT("Authority refreshes bones for pose-accurate queries"),
		Target->GetMesh()->VisibilityBasedAnimTickOption,
		EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedEquipmentModelTest, "ArtisticSW.GAS.Strength.SharedEquipmentModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSharedEquipmentModelTest::RunTest(const FString& Parameters)
{
	CombatArchitectureTests::FWorldScope Scope;
	AActor* Actor;
	auto* ASC = Scope.MakeASC(Actor);
	auto* Model = UEquipmentStatComponent::GetOrCreate(Actor);
	auto* A = Scope.World->SpawnActor<AActor>();
	auto* B = Scope.World->SpawnActor<AActor>();
	TestTrue(TEXT("Equip +5"), Model->Equip(ASC, A, 5.f));
	TestEqual(TEXT("Strength is 15"), ASC->GetNumericAttribute(UBaseAttributeSet::GetStrengthAttribute()), 15.f);
	TestTrue(TEXT("Repeated equip is idempotent"), Model->Equip(ASC, A, 5.f));
	TestEqual(TEXT("Revision does not advance on duplicate"), Model->GetRevision(), 1u);
	TestFalse(TEXT("Invalid equipment GE preserves old gear"), Model->Equip(ASC, B, 20.f, UGASDamageInstantGameplayEffect::StaticClass()));
	TestTrue(TEXT("Old gear still equipped"), Model->IsEquipped(A));
	TestFalse(TEXT("NaN bonus rejected"), Model->Equip(ASC, B, std::numeric_limits<float>::quiet_NaN()));
	FGameplayEffectSpecHandle Buff = ASC->MakeOutgoingSpec(UGASStrengthEquipmentGameplayEffect::StaticClass(), 1.f, ASC->MakeEffectContext());
	Buff.Data->SetSetByCallerMagnitude(Data_StrengthBonus, 7.f);
	ASC->ApplyGameplayEffectSpecToSelf(*Buff.Data.Get());
	TestTrue(TEXT("Replace +5 with +20"), Model->Equip(ASC, B, 20.f));
	TestEqual(TEXT("Base + independent buff + new gear"), ASC->GetNumericAttribute(UBaseAttributeSet::GetStrengthAttribute()), 37.f);
	A->Destroy();
	TestEqual(TEXT("Destroying previous item cannot remove current bonus"), ASC->GetNumericAttribute(UBaseAttributeSet::GetStrengthAttribute()), 37.f);
	B->Destroy();
	TestEqual(TEXT("Destroy removes only owned gear effect"), ASC->GetNumericAttribute(UBaseAttributeSet::GetStrengthAttribute()), 17.f);
	A = Scope.World->SpawnActor<AActor>();
	TestTrue(TEXT("Re-equip after destruction"), Model->Equip(ASC, A, 5.f));
	ASC->AddLooseGameplayTag(State_Dead);
	TestEqual(TEXT("Death removes gear contribution"), ASC->GetNumericAttribute(UBaseAttributeSet::GetStrengthAttribute()), 17.f);
	TestFalse(TEXT("Dead owner cannot equip"), Model->Equip(ASC, A, 5.f));
	ASC->RemoveLooseGameplayTag(State_Dead);
	TestTrue(TEXT("Restore after death/pool reset"), Model->Equip(ASC, A, 5.f));
	TestTrue(TEXT("Clear for pool"), Model->Clear());
	TestTrue(TEXT("Repeated clear is safe"), Model->Clear());
	Actor->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client cannot mutate equipment"), Model->Equip(ASC, A, 50.f));
	Actor->SetRole(ROLE_Authority);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedCombatResolverTest, "ArtisticSW.GAS.Strength.SharedHitResolver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSharedCombatResolverTest::RunTest(const FString& Parameters)
{
	CombatArchitectureTests::FWorldScope Scope;
	AActor* Source;
	AActor* Target;
	auto* SourceASC = Scope.MakeASC(Source);
	auto* TargetASC = Scope.MakeASC(Target);
	auto* Causer = Scope.World->SpawnActor<AActor>();
	SourceASC->SetNumericAttributeBase(UBaseAttributeSet::GetStrengthAttribute(), 30.f);
	SourceASC->AddLooseGameplayTag(Team_Player);
	TargetASC->AddLooseGameplayTag(Team_Enemy);
	FStrengthDamageRequest Request;
	Request.SourceASC = SourceASC;
	Request.InstigatorActor = Source;
	Request.EffectCauser = Causer;
	Request.AttackCoefficient = 1.5f;
	const auto Spec = UGASCombatLibrary::MakeStrengthDamageEffectSpec(Request);
	if (!TestTrue(TEXT("Native execution spec"), Spec.IsValid())) return false;
	TestFalse(TEXT("Native spec does not populate legacy Data.Damage"), Spec.Data->SetByCallerTagMagnitudes.Contains(Data_Damage));
	auto* Resolver = Causer->FindComponentByClass<UCombatHitResolverComponent>();
	TestTrue(TEXT("Open server window"), Resolver && Resolver->OpenWindow(Spec));
	if (!Resolver) return false;
	SourceASC->SetNumericAttributeBase(UBaseAttributeSet::GetStrengthAttribute(), 90.f);
	TestTrue(TEXT("Authoritative hit confirms damage"), Resolver->ResolveHit(TargetASC, FHitResult()));
	TestEqual(TEXT("Hit uses captured Strength 30, not current 90"), TargetASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()), 55.f);
	TestFalse(TEXT("Same target cannot be hit twice"), Resolver->ResolveHit(TargetASC, FHitResult()));
	TestTrue(TEXT("Duplicate open does not reset deduplication"), Resolver->OpenWindow(Spec));
	TestFalse(TEXT("Duplicate open still cannot damage twice"), Resolver->ResolveHit(TargetASC, FHitResult()));
	Resolver->CloseWindow();
	TestFalse(TEXT("Closed token cannot reopen"), Resolver->OpenWindow(Spec));
	TestFalse(TEXT("Closed window cannot damage"), Resolver->ResolveHit(TargetASC, FHitResult()));
	TestNull(TEXT("Per-target enrichment does not mutate source context"), Spec.Data->GetContext().GetHitResult());
	const auto NextSpec = UGASCombatLibrary::MakeStrengthDamageEffectSpec(Request);
	TestTrue(TEXT("New server window opens"), Resolver->OpenWindow(NextSpec));
	TargetASC->AddLooseGameplayTag(State_Invulnerable);
	TestFalse(TEXT("Invulnerability rejects hit"), Resolver->ResolveHit(TargetASC, FHitResult()));
	TargetASC->RemoveLooseGameplayTag(State_Invulnerable);
	TargetASC->AddLooseGameplayTag(Team_Player);
	TestFalse(TEXT("Friendly target rejects hit"), Resolver->ResolveHit(TargetASC, FHitResult()));
	TargetASC->RemoveLooseGameplayTag(Team_Player);
	Causer->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client-only collision cannot apply damage"), Resolver->ResolveHit(TargetASC, FHitResult()));
	Causer->SetRole(ROLE_Authority);
	TestTrue(TEXT("Lethal hit confirmed"), Resolver->ResolveHit(TargetASC, FHitResult()));
	TestEqual(TEXT("Health clamps at zero"), TargetASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()), 0.f);
	TestEqual(TEXT("Damage meta attribute consumed"), TargetASC->GetNumericAttribute(UBaseAttributeSet::GetDamageAttribute()), 0.f);
	Request.AttackCoefficient = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("Infinite coefficient rejected"), UGASCombatLibrary::MakeStrengthDamageEffectSpec(Request).IsValid());
	Request.AttackCoefficient = 0.f;
	TestFalse(TEXT("Zero coefficient rejected"), UGASCombatLibrary::MakeStrengthDamageEffectSpec(Request).IsValid());
	Request.AttackCoefficient = 1.f;

	TestTrue(TEXT("Valid requests always use the shared pipeline"), UGASCombatLibrary::MakeStrengthDamageEffectSpec(Request).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatPresentationBindingTest, "ArtisticSW.GAS.Strength.PresentationBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCombatPresentationBindingTest::RunTest(const FString& Parameters)
{
	CombatArchitectureTests::FWorldScope Scope;
	AActor* Source;
	AActor* Replacement;
	auto* ASC = Scope.MakeASC(Source);
	auto* NewASC = Scope.MakeASC(Replacement);
	auto* Presenter = UCombatPresentationComponent::GetOrCreate(Source);
	Presenter->Initialize(ASC);
	TestEqual(TEXT("Late UI can read current Strength"), Presenter->GetStrength(), 10.f);
	NewASC->SetNumericAttributeBase(UBaseAttributeSet::GetStrengthAttribute(), 25.f);
	Presenter->Initialize(NewASC);
	ASC->SetNumericAttributeBase(UBaseAttributeSet::GetStrengthAttribute(), 90.f);
	TestEqual(TEXT("Rebinding reads replacement ASC"), Presenter->GetStrength(), 25.f);
	Presenter->Uninitialize();
	TestEqual(TEXT("Unbound presenter has no stale state"), Presenter->GetStrength(), 0.f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStrengthArrowLaunchTest, "ArtisticSW.GAS.Strength.ArrowLaunch",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStrengthArrowLaunchTest::RunTest(const FString& Parameters)
{
 CombatArchitectureTests::FWorldScope Scope;
 AActor* Source;
 auto* ASC = Scope.MakeASC(Source);
 const TCHAR* Classes[] = {
  TEXT("/Script/ArtisticSWCore.ArrowProjectile"),
  TEXT("/Game/GameplayAbilitySystem/Weapon/BP_Arrow.BP_Arrow_C"),
  TEXT("/Game/GameplayAbilitySystem/Enemy/Weapon/BP_EnemyProjectile.BP_EnemyProjectile_C")};
 for (const TCHAR* Path : Classes)
 {
  UClass* Class = LoadClass<AArrowProjectile>(nullptr, Path);
  if (!TestNotNull(Path, Class)) continue;
  const FTransform SpawnTransform(FVector(0.f, 0.f, 1000.f));
  auto* Arrow = Scope.World->SpawnActorDeferred<AArrowProjectile>(Class, SpawnTransform, Source,
   nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if (!TestNotNull(TEXT("Deferred arrow"), Arrow)) continue;
  Arrow->FinishSpawning(SpawnTransform);
  FStrengthDamageRequest Request;
  Request.SourceASC = ASC;
  Request.InstigatorActor = Source;
  Request.EffectCauser = Arrow;
  Request.AttackCoefficient = Arrow->GetAttackCoefficient();
  TestTrue(TEXT("Authored arrow accepts shared native damage"), Arrow->InitializeStrengthDamage(
   ASC, Source, UGASCombatLibrary::MakeStrengthDamageEffectSpec(Request)));
  auto* Movement = Arrow->GetProjectileMovement();
  Movement->Deactivate();
  Movement->SetUpdatedComponent(nullptr);
  Movement->bSimulationEnabled = false;
  const FVector Velocity(2000.f, 0.f, 0.f);
  Arrow->LaunchArrow(Velocity);
  TestTrue(TEXT("Launch restores movement after Blueprint construction"),
   Movement->IsActive() && Movement->bSimulationEnabled && Movement->UpdatedComponent == Arrow->GetCollisionComp());
  TestTrue(TEXT("Launch retains authoritative world velocity"), Movement->Velocity.Equals(Velocity));
  const FVector Before = Arrow->GetActorLocation();
  Movement->TickComponent(0.02f, LEVELTICK_All, nullptr);
  TestTrue(TEXT("Projectile actually moves forward"), Arrow->GetActorLocation().X > Before.X + 1.f);
  Arrow->SetRole(ROLE_SimulatedProxy);
  const FVector ServerVelocity = Movement->Velocity;
  Arrow->LaunchArrow(-Velocity);
  TestTrue(TEXT("Client cannot relaunch arrow"), Movement->Velocity.Equals(ServerVelocity));
  Arrow->SetRole(ROLE_Authority);
  Arrow->Destroy();
 }
 return true;
}
#endif
