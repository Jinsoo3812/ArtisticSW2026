#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "BossAI/ShipBossEnemy.h"
#include "Components/CombatHurtboxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "CollisionChannels.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "GAS/Ability/Boss/GA_BossDashSlash.h"
#include "AbilitySystemComponent.h"
#include "BaseAttributeSet.h"
#include "Components/BaseHealthComponent.h"
#include "Components/BoxComponent.h"
#include "GASCombatLibrary.h"
#include "Item/Projectiles/ArrowProjectile.h"
#include "UObject/Script.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBossAnimatedHurtboxRegressionTest,
	"ArtisticSW.Enemy.BossHurtbox.BlueprintSurfacePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBossAnimatedHurtboxRegressionTest::RunTest(const FString& Parameters)
{
	FEditorScriptExecutionGuard ScriptExecutionGuard;
	AddExpectedError(TEXT("QuestItem (has an invalid ResultItemTag|contains an invalid ingredient)"),
		EAutomationExpectedErrorFlags::Contains, 0);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	UClass* BossClass = LoadClass<AShipBossEnemy>(nullptr,
		TEXT("/Game/GameplayAbilitySystem/Enemy/BP_Ship_BossEnemy.BP_Ship_BossEnemy_C"));
	AShipBossEnemy* Boss = BossClass ? World->SpawnActor<AShipBossEnemy>(BossClass) : nullptr;
	if (TestNotNull(TEXT("Authored boss Blueprint spawns"), Boss))
	{
		UCombatHurtboxComponent* Hurtbox = Boss->CombatHurtboxComponent;
		TestEqual(TEXT("Boss explicitly requires PhysicsAsset hits"), Hurtbox->Mode, ECombatHurtboxMode::AnimatedPhysicsAsset);
		FString Failure;
		TestTrue(FString::Printf(TEXT("Authored boss configuration: %s"), *Failure), Hurtbox->ValidateConfiguration(Failure));
		if (!Failure.IsEmpty()) AddError(Failure);
		Hurtbox->InitializeHurtbox();
		TestTrue(TEXT("Authored boss animated surface is ready"), Hurtbox->IsAnimatedHurtboxReady());
		USkeletalMeshComponent* Mesh = Boss->GetMesh();
		Mesh->RefreshBoneTransforms();
		// Exercise real scene queries against the authored bodies, including a
		// server-evaluated windup pose. No synthetic body/channel stands in for the asset.
		FHitResult LastBodyHit;
		auto CheckPoseQueries = [this, World, Boss, Mesh, Hurtbox, &LastBodyHit]()
		{
			bool bFoundBodyHit = false;
			if (const UPhysicsAsset* Physics = Mesh->GetPhysicsAsset())
			{
				for (const USkeletalBodySetup* Body : Physics->SkeletalBodySetups)
				{
					if (!Body) continue;
					const FVector Center = Mesh->GetBoneLocation(Body->BoneName);
					const FVector Start = Center - FVector(300, 0, 0);
					const FVector End = Center + FVector(300, 0, 0);
					FHitResult BodyHit, AimHit;
					FCollisionQueryParams Query(SCENE_QUERY_STAT(BossHurtboxRegression), false);
					if (!World->LineTraceSingleByObjectType(BodyHit, Start, End,
						FCollisionObjectQueryParams(ECC_CombatHurtbox), Query)) continue;
					TestTrue(TEXT("Real trace reports the boss mesh and body bone"),
						BodyHit.GetComponent() == Mesh && !BodyHit.BoneName.IsNone() && Hurtbox->AcceptsHit(BodyHit));
					TestTrue(TEXT("WeaponAim hits the same animated surface"),
						World->LineTraceSingleByChannel(AimHit, Start, End, ECC_WeaponAim, Query)
						&& AimHit.GetComponent() == Mesh && AimHit.ImpactPoint.Equals(BodyHit.ImpactPoint, 0.1f));
					bFoundBodyHit = true;
					LastBodyHit = BodyHit;
					break;
				}
			}
			TestTrue(TEXT("At least one authored body is queryable in this pose"), bFoundBodyHit);
		};
		CheckPoseQueries();
		UClass* DashClass = LoadClass<UGA_BossDashSlash>(nullptr,
			TEXT("/Game/GameplayAbilitySystem/Ability/Enemy/Boss/BPGA_SlashDash.BPGA_SlashDash_C"));
		UAnimInstance* Anim = Mesh->GetAnimInstance();
		if (TestNotNull(TEXT("Boss AnimInstance exists"), Anim) && TestNotNull(TEXT("Authored dash exists"), DashClass))
		{
			const FDashSlashMontageConfig& Config = GetDefault<UGA_BossDashSlash>(DashClass)->GetMontageConfig();
			if (TestNotNull(TEXT("Authored dash montage exists"), Config.Montage.Get()))
			{
				TestTrue(TEXT("Server can play boss montage"), Anim->Montage_Play(Config.Montage) > 0.f);
				Anim->Montage_JumpToSection(Config.WindupEnterSectionName, Config.Montage);
				Mesh->TickAnimation(0.15f, false);
				Mesh->RefreshBoneTransforms();
				CheckPoseQueries();
			}
		}
		UCapsuleComponent* Capsule = Boss->GetCapsuleComponent();
		const ECollisionEnabled::Type MovementCollision = Capsule->GetCollisionEnabled();
		TestEqual(TEXT("Arrow bypasses capsule"), Capsule->GetCollisionResponseToChannel(ECC_Arrow), ECR_Ignore);
		TestEqual(TEXT("Aim bypasses capsule"), Capsule->GetCollisionResponseToChannel(ECC_WeaponAim), ECR_Ignore);
		FHitResult CapsuleHit(Boss, Capsule, Boss->GetActorLocation(), FVector::UpVector);
		TestFalse(TEXT("Capsule hit is rejected"), Hurtbox->AcceptsHit(CapsuleHit));
		if (UPhysicsAsset* Physics = Mesh->GetPhysicsAsset())
		{
			for (const USkeletalBodySetup* Body : Physics->SkeletalBodySetups)
			{
				if (!Body || Body->CollisionReponse == EBodyCollisionResponse::BodyCollision_Disabled) continue;
				FHitResult BodyHit(Boss, Mesh, Mesh->GetBoneLocation(Body->BoneName), FVector::UpVector);
				BodyHit.BoneName = Body->BoneName;
				TestTrue(TEXT("Authored PhysicsAsset body is accepted"), Hurtbox->AcceptsHit(BodyHit));
				Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				TestFalse(TEXT("Disabled mesh fails closed"), Hurtbox->AcceptsHit(BodyHit));
				TestFalse(TEXT("Disabled mesh never falls back to capsule"), Hurtbox->AcceptsHit(CapsuleHit));
				Hurtbox->InitializeHurtbox();
				Mesh->SetCollisionObjectType(ECC_Pawn);
				TestFalse(TEXT("Wrong object type fails closed"), Hurtbox->AcceptsHit(BodyHit));
				Hurtbox->InitializeHurtbox();
				Mesh->SetComponentTickEnabled(false);
				TestFalse(TEXT("Stopped server bones fail closed"), Hurtbox->AcceptsHit(BodyHit));
				Hurtbox->InitializeHurtbox();
				break;
			}
		}
		Hurtbox->ProfileName = TEXT("MissingCombatHurtboxProfile");
		Hurtbox->InitializeHurtbox();
		TestFalse(TEXT("Missing profile fails closed"), Hurtbox->AcceptsHit(CapsuleHit));
		TestFalse(TEXT("Failure reason is retained"), Hurtbox->InitializationFailure.IsEmpty());
		Hurtbox->ProfileName = TEXT("CharacterHurtbox");
		Hurtbox->InitializeHurtbox();
		Boss->SetBossHidden(true);
		Boss->SetBossHidden(false);
		TestTrue(TEXT("Vanish reveal keeps animated surface"), Hurtbox->IsAnimatedHurtboxReady());
		TestEqual(TEXT("Vanish reveal preserves movement collision"), Capsule->GetCollisionEnabled(), MovementCollision);
		TestEqual(TEXT("Vanish reveal preserves capsule Arrow bypass"), Capsule->GetCollisionResponseToChannel(ECC_Arrow), ECR_Ignore);
		TestEqual(TEXT("Dash damage volume stays disabled outside dash"), Boss->GetDashDamageVolume()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		// Send the real trace through the arrow's collision adapter and GAS. A
		// rejected capsule event must not consume the projectile or reserve the hit.
		AActor* Source = World->SpawnActor<AActor>();
		UAbilitySystemComponent* SourceASC = NewObject<UAbilitySystemComponent>(Source);
		Source->AddInstanceComponent(SourceASC);
		SourceASC->RegisterComponent();
		SourceASC->InitAbilityActorInfo(Source, Source);
		SourceASC->AddAttributeSetSubobject(NewObject<UBaseAttributeSet>(Source));
		UAbilitySystemComponent* TargetASC = Boss->GetAbilitySystemComponent();
		TargetASC->InitAbilityActorInfo(Boss, Boss);
		TargetASC->AddAttributeSetSubobject(CastChecked<UBaseAttributeSet>(Boss->GetDefaultSubobjectByName(TEXT("BasicAttributeSet"))));
		TargetASC->SetNumericAttributeBase(UBaseAttributeSet::GetMaxHealthAttribute(), 100.f);
		TargetASC->SetNumericAttributeBase(UBaseAttributeSet::GetHealthAttribute(), 100.f);
		Boss->GetHealthComponent()->InitializeWithAbilitySystem(TargetASC);
		int32 ConfirmedHits = 0;
		const FDelegateHandle Confirmation = Boss->GetHealthComponent()->OnConfirmedDamage.AddLambda(
			[&ConfirmedHits](float, const FGameplayEffectContextHandle&, bool) { ++ConfirmedHits; });
		AArrowProjectile* Arrow = World->SpawnActor<AArrowProjectile>();
		FStrengthDamageRequest Request;
		Request.SourceASC = SourceASC;
		Request.InstigatorActor = Source;
		Request.EffectCauser = Arrow;
		TestTrue(TEXT("Arrow initializes Strength damage"), Arrow->InitializeStrengthDamage(
			SourceASC, Source, UGASCombatLibrary::MakeStrengthDamageEffectSpec(Request)));
		Arrow->GetCollisionComp()->OnComponentHit.Broadcast(Arrow->GetCollisionComp(), Boss, Capsule, FVector::ZeroVector, CapsuleHit);
		TestEqual(TEXT("Capsule arrow hit does not change health"), TargetASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()), 100.f);
		TestEqual(TEXT("Capsule arrow hit emits no confirmed feedback"), ConfirmedHits, 0);
		TestFalse(TEXT("Capsule hit does not consume arrow"), Arrow->IsActorBeingDestroyed());
		if (LastBodyHit.GetComponent())
		{
			Arrow->GetCollisionComp()->OnComponentHit.Broadcast(Arrow->GetCollisionComp(), Boss, Mesh, FVector::ZeroVector, LastBodyHit);
			TestTrue(TEXT("PhysicsAsset arrow hit reduces health"), TargetASC->GetNumericAttribute(UBaseAttributeSet::GetHealthAttribute()) < 100.f);
			TestEqual(TEXT("PhysicsAsset arrow hit confirms feedback once"), ConfirmedHits, 1);
			TestTrue(TEXT("Confirmed body hit consumes arrow"), Arrow->IsActorBeingDestroyed());
		}
		Boss->GetHealthComponent()->OnConfirmedDamage.Remove(Confirmation);
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return !HasAnyErrors();
}
#endif
