#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemGlobals.h"
#include "BaseCharacter.h"
#include "Components/BaseHealthComponent.h"
#include "DeckAI/DeckRangedEnemy.h"
#include "Engine/CollisionProfile.h"
#include "GAS/SWCombatEffectContextLibrary.h"
#include "GAS/SWGameplayEffectContext.h"
#include "MeleeEnemy/MeleeEnemy.h"
#include "RangedEnemy/RangedEnemy.h"
#include "UObject/CoreNet.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeathRagdollDirectionTest,
	"ArtisticSW.Enemy.DeathRagdoll.DirectionAwayFromSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeathRagdollDirectionTest::RunTest(const FString& Parameters)
{
	const FVector VictimLocation(100.0, 200.0, 900.0);
	const FVector LeftAttacker(-100.0, 200.0, -500.0);
	const FVector RightAttacker(300.0, 200.0, 1500.0);

	const FVector AwayFromLeft = UBaseHealthComponent::CalculateKnockbackDirectionAwayFromSource(
		VictimLocation, LeftAttacker);
	const FVector AwayFromRight = UBaseHealthComponent::CalculateKnockbackDirectionAwayFromSource(
		VictimLocation, RightAttacker);

	TestTrue(TEXT("A left-side attack knocks the victim right"), AwayFromLeft.Equals(FVector::ForwardVector));
	TestTrue(TEXT("A right-side attack knocks the victim left"), AwayFromRight.Equals(-FVector::ForwardVector));
	TestEqual(TEXT("Vertical separation never changes ragdoll launch height"), AwayFromLeft.Z, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCombatEffectContextDeathDirectionTest,
	"ArtisticSW.Enemy.DeathRagdoll.CombatEffectContextDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatEffectContextDeathDirectionTest::RunTest(const FString& Parameters)
{
	FGameplayEffectContext* AllocatedContext =
		UAbilitySystemGlobals::Get().AllocGameplayEffectContext();
	TestNotNull(TEXT("AbilitySystemGlobals allocates a context"), AllocatedContext);
	if (!AllocatedContext)
	{
		return false;
	}

	TestTrue(TEXT("Project globals allocate FSWGameplayEffectContext"),
		AllocatedContext->GetScriptStruct()->IsChildOf(FSWGameplayEffectContext::StaticStruct()));
	FGameplayEffectContextHandle ContextHandle(AllocatedContext);

	const FVector AuthoredDirection(3.0, 4.0, 9.0);
	ContextHandle = USWCombatEffectContextLibrary::SetImpactDirection(
		ContextHandle, AuthoredDirection);
	FVector StoredDirection;
	TestTrue(TEXT("Combat context exposes its authored impact direction"),
		USWCombatEffectContextLibrary::GetImpactDirection(ContextHandle, StoredDirection));
	TestTrue(TEXT("Direction is normalized and flattened before replication"),
		StoredDirection.Equals(FVector(0.6, 0.8, 0.0), 0.01));

	const FGameplayEffectContextHandle Duplicate = ContextHandle.Duplicate();
	FVector DuplicatedDirection;
	TestTrue(TEXT("Per-target spec duplication preserves impact direction"),
		USWCombatEffectContextLibrary::GetImpactDirection(Duplicate, DuplicatedDirection));
	TestTrue(TEXT("Duplicated direction equals the original"),
		DuplicatedDirection.Equals(StoredDirection, 0.01));

	FSWPathCuePayload PathPayload;
	PathPayload.ReferenceActor = GetMutableDefault<ARangedEnemy>();
	PathPayload.StartLocal = FVector(100.0f, 20.0f, 0.0f);
	PathPayload.EndLocal = FVector(900.0f, 20.0f, 0.0f);
	PathPayload.SurfaceNormalLocal = FVector::UpVector;
	PathPayload.CorridorRadius = 120.0f;
	PathPayload.InstanceId = 7;
	ContextHandle = USWCombatEffectContextLibrary::SetPathCuePayload(
		ContextHandle, PathPayload);
	FSWPathCuePayload StoredPath;
	TestTrue(TEXT("Combat context exposes optional path presentation data"),
		USWCombatEffectContextLibrary::GetPathCuePayload(ContextHandle, StoredPath));
	TestEqual(TEXT("Path instance identity is preserved"), StoredPath.InstanceId, 7);
	TestTrue(TEXT("Path endpoints remain reference-frame local"),
		FVector(StoredPath.StartLocal).Equals(FVector(PathPayload.StartLocal), 0.01f)
			&& FVector(StoredPath.EndLocal).Equals(FVector(PathPayload.EndLocal), 0.01f));
	FSWPathCuePayload DuplicatedPath;
	TestTrue(TEXT("Context duplication preserves optional path data"),
		USWCombatEffectContextLibrary::GetPathCuePayload(
			ContextHandle.Duplicate(), DuplicatedPath));

	FSWGameplayEffectContext SenderContext;
	SenderContext.SetImpactDirection(AuthoredDirection);
	FNetBitWriter Writer(nullptr, 2048);
	bool bWriteSucceeded = false;
	SenderContext.NetSerialize(Writer, nullptr, bWriteSucceeded);
	TestTrue(TEXT("Combat context direction serializes without error"),
		bWriteSucceeded && !Writer.IsError());

	FSWGameplayEffectContext ReceiverContext;
	FNetBitReader Reader(nullptr, Writer.GetData(), Writer.GetNumBits());
	bool bReadSucceeded = false;
	ReceiverContext.NetSerialize(Reader, nullptr, bReadSucceeded);
	TestTrue(TEXT("Combat context direction deserializes without error"),
		bReadSucceeded && !Reader.IsError());
	TestTrue(TEXT("Network round trip preserves the normalized direction"),
		ReceiverContext.HasImpactDirection()
			&& ReceiverContext.GetImpactDirection().Equals(StoredDirection, 0.01));

	const AActor* Victim = GetDefault<ARangedEnemy>();
	const FDeathRagdollImpactData ImpactData =
		UBaseHealthComponent::BuildDeathRagdollImpactData(Victim, Duplicate, nullptr);
	TestTrue(TEXT("Lethal impact consumes the stored context direction"), ImpactData.bHasDirection);
	TestTrue(TEXT("Replicated death payload keeps the stored direction"),
		FVector(ImpactData.KnockbackDirection).Equals(StoredDirection, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNormalEnemyDeathRagdollDefaultsTest,
	"ArtisticSW.Enemy.DeathRagdoll.NormalEnemyDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNormalEnemyDeathRagdollDefaultsTest::RunTest(const FString& Parameters)
{
	const ARangedEnemy* RangedCDO = GetDefault<ARangedEnemy>();
	const AMeleeEnemy* MeleeCDO = GetDefault<AMeleeEnemy>();
	const ADeckRangedEnemy* DeckCDO = GetDefault<ADeckRangedEnemy>();

	TestTrue(TEXT("Ranged enemies enable directional death impulse"),
		RangedCDO && RangedCDO->IsDeathRagdollImpulseEnabled());
	TestTrue(TEXT("Melee enemies enable directional death impulse"),
		MeleeCDO && MeleeCDO->IsDeathRagdollImpulseEnabled());
	TestTrue(TEXT("Horizontal impulse remains designer tunable"),
		RangedCDO && RangedCDO->GetDeathRagdollHorizontalImpulse() >= 0.0f);
	TestTrue(TEXT("Upward impulse remains designer tunable"),
		RangedCDO && RangedCDO->GetDeathRagdollUpwardImpulse() >= 0.0f);
	TestTrue(TEXT("Deck pool return delay remains designer tunable"),
		DeckCDO && DeckCDO->GetReturnToPoolAfterDeathDelay() >= 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeathPresentationReplicationContractTest,
	"ArtisticSW.Enemy.DeathRagdoll.ReplicationContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeathPresentationReplicationContractTest::RunTest(const FString& Parameters)
{
	const FProperty* DeathPresentationProperty = FindFProperty<FProperty>(
		UBaseHealthComponent::StaticClass(), TEXT("DeathPresentation"));
	TestNotNull(TEXT("Combined death presentation property exists"), DeathPresentationProperty);
	if (DeathPresentationProperty)
	{
		TestTrue(TEXT("Death state and lethal impact payload replicate atomically"),
			DeathPresentationProperty->HasAnyPropertyFlags(CPF_Net));
		TestEqual(TEXT("Combined payload has one RepNotify"),
			DeathPresentationProperty->RepNotifyFunc, FName(TEXT("OnRep_DeathPresentation")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FShipDeckRagdollCollisionTest,
	"ArtisticSW.Enemy.DeathRagdoll.ShipDeckCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipDeckRagdollCollisionTest::RunTest(const FString& Parameters)
{
	FCollisionResponseTemplate ShipDeckProfile;
	if (TestTrue(TEXT("ShipDeck collision profile is registered"),
		UCollisionProfile::Get()->GetProfileTemplate(TEXT("ShipDeck"), ShipDeckProfile)))
	{
		TestEqual(TEXT("ShipDeck participates in Chaos contacts"),
			ShipDeckProfile.CollisionEnabled, ECollisionEnabled::QueryAndPhysics);
		TestEqual(TEXT("ShipDeck blocks living Pawn capsules"),
			ShipDeckProfile.ResponseToChannels.GetResponse(ECC_Pawn), ECR_Block);
		TestEqual(TEXT("ShipDeck blocks ragdoll PhysicsBody objects"),
			ShipDeckProfile.ResponseToChannels.GetResponse(ECC_PhysicsBody), ECR_Block);
	}

	FCollisionResponseTemplate RagdollProfile;
	if (TestTrue(TEXT("Ragdoll collision profile is registered"),
		UCollisionProfile::Get()->GetProfileTemplate(TEXT("Ragdoll"), RagdollProfile)))
	{
		TestEqual(TEXT("Ragdoll uses the PhysicsBody object channel"),
			RagdollProfile.ObjectType, ECC_PhysicsBody);
		TestEqual(TEXT("Ragdoll blocks WorldDynamic moving decks"),
			RagdollProfile.ResponseToChannels.GetResponse(ECC_WorldDynamic), ECR_Block);
	}
	return true;
}

#endif
