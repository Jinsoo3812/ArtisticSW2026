#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Abilities/BaseHitReactionGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "BaseGameplayTags.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/UnrealType.h"

namespace HitReactionRootMotionTests
{
	struct FScopedWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("HitReactionRootMotionTestWorld"));
		FScopedWorld()
		{
			if (World)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
			}
		}
		~FScopedWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FHitReactionAuthorityRootMotionTest,
	"ArtisticSW.GAS.HitReaction.AuthorityRootMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FHitReactionAuthorityRootMotionTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Man"));
	OutTestCommands.Add(TEXT("/Game/Blueprints/Player/BP_Player_Man.BP_Player_Man_C"));
	OutBeautifiedNames.Add(TEXT("Woman"));
	OutTestCommands.Add(TEXT("/Game/Blueprints/Player/BP_Player_Woman.BP_Player_Woman_C"));
}

bool FHitReactionAuthorityRootMotionTest::RunTest(const FString& Parameters)
{
	const UClass* PlayerClass = LoadObject<UClass>(nullptr, *Parameters);
	const UClass* AbilityClass = LoadObject<UClass>(nullptr,
		TEXT("/Game/GameplayAbilitySystem/Ability/GA_HitReact.GA_HitReact_C"));
	if (!TestNotNull(TEXT("Player mesh configuration loads"), PlayerClass)
		|| !TestNotNull(TEXT("Player hit reaction configuration loads"), AbilityClass))
	{
		return false;
	}
	const ACharacter* PlayerDefaults = PlayerClass->GetDefaultObject<ACharacter>();
	const UBaseHitReactionGameplayAbility* AbilityDefaults =
		AbilityClass->GetDefaultObject<UBaseHitReactionGameplayAbility>();
	if (!TestNotNull(TEXT("Player character defaults exist"), PlayerDefaults)
		|| !TestNotNull(TEXT("Hit reaction defaults exist"), AbilityDefaults))
	{
		return false;
	}

	// Existing item-table errors from the game-world subsystem, unrelated to movement.
	AddExpectedError(TEXT("QuestItem has an invalid ResultItemTag"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("QuestItem contains an invalid ingredient"), EAutomationExpectedErrorFlags::Contains, 2);
	HitReactionRootMotionTests::FScopedWorld Scope;
	if (!TestNotNull(TEXT("Test world exists"), Scope.World))
	{
		return false;
	}

	// Use the production montage assets with the shared native GA. A plain
	// character/AnimInstance isolates GAS + CMC from player input and AnimGraph.
	// This is an authority regression, not a multi-client replication test.
	ACharacter* Character = Scope.World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("Authority character spawns"), Character))
	{
		return false;
	}
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	Mesh->SetSkeletalMeshAsset(PlayerDefaults->GetMesh()->GetSkeletalMeshAsset());
	Mesh->SetAnimInstanceClass(UAnimInstance::StaticClass());
	UAnimInstance* Anim = Mesh->GetAnimInstance();
	if (!TestNotNull(TEXT("Offscreen animation instance initializes"), Anim))
	{
		return false;
	}
	Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
	Character->DispatchBeginPlay();
	TestTrue(TEXT("Character executes as authority"), Character->HasAuthority());

	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->bRunPhysicsWithNoController = true;
	// Remove gravity to isolate horizontal animation movement. No input, force,
	// fallback RMS, or direct location updates are used to move the capsule.
	Movement->GravityScale = 0.0f;
	Movement->SetMovementMode(MOVE_Falling);
	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Character);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Character, Character);
	const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(
		FGameplayAbilitySpec(UBaseHitReactionGameplayAbility::StaticClass(), 1));
	FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
	UGameplayAbility* Ability = Spec ? Spec->GetPrimaryInstance() : nullptr;
	if (!TestNotNull(TEXT("Instanced hit reaction is granted"), Ability))
	{
		return false;
	}
	TestEqual(TEXT("Hit reaction remains server initiated"),
		Ability->GetNetExecutionPolicy(), EGameplayAbilityNetExecutionPolicy::ServerInitiated);
	FObjectProperty* FrontProperty = FindFProperty<FObjectProperty>(
		UBaseHitReactionGameplayAbility::StaticClass(), TEXT("FrontHitReactionMontage"));
	if (!TestNotNull(TEXT("Front montage property exists"), FrontProperty))
	{
		return false;
	}

	// A hit result provides a stable source position for each playback.
	FGameplayEventData Event;
	Event.EventTag = GameplayAbility_HitReaction;
	Event.Target = Character;
	Event.ContextHandle = ASC->MakeEffectContext();

	for (const FName PropertyName : { FName(TEXT("FrontHitReactionMontage")), FName(TEXT("BackHitReactionMontage")) })
	{
		const FObjectProperty* Property = FindFProperty<FObjectProperty>(AbilityClass, PropertyName);
		UAnimMontage* Montage = Property
			? Cast<UAnimMontage>(Property->GetObjectPropertyValue_InContainer(AbilityDefaults)) : nullptr;
		if (!TestNotNull(*PropertyName.ToString(), Montage))
		{
			continue;
		}
		TestTrue(TEXT("Configured montage enables root motion"), Montage->HasRootMotion());
		// Exercise both assets through the real shared playback function.
		FrontProperty->SetObjectPropertyValue_InContainer(Ability, Montage);
		Movement->StopMovementImmediately();
		FHitResult Hit;
		Hit.ImpactPoint = Character->GetActorLocation() + Character->GetActorForwardVector() * 1000.0f;
		Event.ContextHandle.AddHitResult(Hit, true);
		const FVector StartLocation = Character->GetActorLocation();
		TestEqual(TEXT("Server event activates one hit reaction"), ASC->HandleGameplayEvent(Event.EventTag, &Event), 1);
		TestTrue(TEXT("GAS actually starts the montage"), Anim->Montage_IsPlaying(Montage));
		TestEqual(TEXT("Authority preserves authored root motion scale during playback"),
			Character->GetAnimRootMotionTranslationScale(), 1.0f);
		TestTrue(TEXT("Damaged state is held during playback"), ASC->HasMatchingGameplayTag(State_Damaged));

		double MaxHorizontalDisplacement = 0.0;
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			++GFrameCounter;
			Movement->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
			MaxHorizontalDisplacement = FMath::Max(MaxHorizontalDisplacement,
				FVector::Dist2D(StartLocation, Character->GetActorLocation()));
		}
		AddInfo(FString::Printf(TEXT("%s authority capsule displacement: %.3f cm"),
			*PropertyName.ToString(), MaxHorizontalDisplacement));
		TestTrue(TEXT("CMC moves the authority capsule with montage root motion"), MaxHorizontalDisplacement > 1.0);
		ASC->CancelAbilityHandle(Handle);
		TestFalse(TEXT("Cancelling clears damaged state"), ASC->HasMatchingGameplayTag(State_Damaged));
		TestEqual(TEXT("Cancelling leaves the default movement scale"), Character->GetAnimRootMotionTranslationScale(), 1.0f);
	}
	return !HasAnyErrors();
}

#endif
