#include "SWShipBoardingValidationLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Animation/AnimInstance.h"
#include "BaseGameplayTags.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GAS/Ability/GA_PlayerRoll.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Serialization/JsonSerializer.h"
#include "Ship.h"
#include "SwimmingComponent.h"
#include "TimerManager.h"

bool USWShipBoardingValidationLibrary::QueueSeaBoard(ACharacter* Character, AShip* Ship, bool bStarboard)
{
	if (!IsValid(Character) || !IsValid(Ship) || !Ship->HasAuthority() || !Character->HasAuthority() ||
	    Character->GetWorld() != Ship->GetWorld() || Ship->GetWorld()->WorldType != EWorldType::PIE)
		return false;
	Ship->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(
	    [Pawn = TWeakObjectPtr<ACharacter>(Character), Target = TWeakObjectPtr<AShip>(Ship), bStarboard] {
		    if (!Pawn.IsValid() || !Target.IsValid())
			    return;
		    UFunction* Function =
		        Target->FindFunction(bStarboard ? TEXT("HandleStarboardSeaBoarding") : TEXT("HandlePortSeaBoarding"));
		    if (Function)
		    {
			    struct
			    {
				    AActor* Interactor;
			    } Args{Pawn.Get()};
			    Target->ProcessEvent(Function, &Args);
		    }
	    }));
	return true;
}

bool USWShipBoardingValidationLibrary::QueueRelocate(ACharacter* Character, FVector Location)
{
	if (!IsValid(Character) || !Character->HasAuthority() || Character->GetWorld()->WorldType != EWorldType::PIE)
		return false;
	Character->GetWorldTimerManager().SetTimerForNextTick(
	    FTimerDelegate::CreateLambda([Pawn = TWeakObjectPtr<ACharacter>(Character), Location] {
		    if (!Pawn.IsValid())
			    return;
		    Pawn->GetCharacterMovement()->StopMovementImmediately();
		    Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		    Pawn->TeleportTo(Location, Pawn->GetActorRotation(), false, true);
	    }));
	return true;
}

FString USWShipBoardingValidationLibrary::DescribeRollState(ACharacter* Character)
{
	if (!IsValid(Character))
		return TEXT("{}");
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const USwimmingComponent* Swim = Character->FindComponentByClass<USwimmingComponent>();
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Character);
	TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
	Row->SetStringField(TEXT("player"), Character->GetName());
	Row->SetBoolField(TEXT("authority"), Character->HasAuthority());
	Row->SetBoolField(TEXT("local"), Character->IsLocallyControlled());
	Row->SetBoolField(TEXT("grounded"), Movement && Movement->IsMovingOnGround());
	Row->SetBoolField(TEXT("floor"), Movement && Movement->CurrentFloor.IsWalkableFloor());
	Row->SetNumberField(TEXT("mode"), Movement ? int32(Movement->MovementMode) : -1);
	Row->SetBoolField(TEXT("swimming"), Swim && Swim->IsCustomSwimming());
	Row->SetBoolField(TEXT("vertical_input"), Swim && Swim->HasVerticalSwimInput());
	Row->SetNumberField(TEXT("swim_tag"), ASC ? ASC->GetTagCount(State_Swimming) : -1);
	Row->SetNumberField(TEXT("rolling_tag"), ASC ? ASC->GetTagCount(State_Rolling) : -1);
	Row->SetNumberField(TEXT("damaged_tag"), ASC ? ASC->GetTagCount(State_Damaged) : -1);
	Row->SetNumberField(TEXT("dead_tag"), ASC ? ASC->GetTagCount(State_Dead) : -1);
	Row->SetBoolField(TEXT("roll_granted"), false);
	Row->SetBoolField(TEXT("roll_can_activate"), false);
	if (ASC)
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (!Spec.Ability || !Spec.Ability->IsA<UGA_PlayerRoll>())
				continue;
			Row->SetBoolField(TEXT("roll_granted"), true);
			Row->SetBoolField(TEXT("roll_can_activate"),
			                  Spec.Ability->CanActivateAbility(Spec.Handle, ASC->AbilityActorInfo.Get()));
			Row->SetBoolField(TEXT("roll_active"), Spec.IsActive());
		}
	UAnimInstance* Anim = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	Row->SetStringField(TEXT("montage"), Anim ? GetNameSafe(Anim->GetCurrentActiveMontage()) : TEXT("None"));
	FString Result;
	FJsonSerializer::Serialize(Row, TJsonWriterFactory<>::Create(&Result));
	return Result;
}
