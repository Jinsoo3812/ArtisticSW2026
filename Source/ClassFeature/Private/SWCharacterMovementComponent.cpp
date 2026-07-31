#include "SWCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "SwimmingComponent.h"

namespace
{
	class FSavedMove_SWCharacter final : public FSavedMove_Character
	{
	public:
		using Super = FSavedMove_Character;

		uint8 bSavedSwimDive : 1;
		uint8 bSavedSwimAscend : 1;
		ESwimDepthMode SavedSwimDepthMode = ESwimDepthMode::Surface;

		FSavedMove_SWCharacter()
			: bSavedSwimDive(false)
			, bSavedSwimAscend(false)
		{
		}

		virtual void Clear() override
		{
			Super::Clear();
			bSavedSwimDive = false;
			bSavedSwimAscend = false;
			SavedSwimDepthMode = ESwimDepthMode::Surface;
		}

		virtual uint8 GetCompressedFlags() const override
		{
			uint8 Result = Super::GetCompressedFlags();
			if (bSavedSwimDive)
			{
				Result |= FLAG_Custom_0;
			}
			if (bSavedSwimAscend)
			{
				Result |= FLAG_Custom_1;
			}
			return Result;
		}

		virtual bool CanCombineWith(
			const FSavedMovePtr& NewMove,
			ACharacter* InCharacter,
			float MaxDelta) const override
		{
			const FSavedMove_SWCharacter* NewSWMove =
				static_cast<const FSavedMove_SWCharacter*>(NewMove.Get());
			if (bSavedSwimDive != NewSWMove->bSavedSwimDive
				|| bSavedSwimAscend != NewSWMove->bSavedSwimAscend
				|| SavedSwimDepthMode != NewSWMove->SavedSwimDepthMode)
			{
				return false;
			}
			return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
		}

		virtual void SetMoveFor(
			ACharacter* Character,
			float InDeltaTime,
			const FVector& NewAcceleration,
			FNetworkPredictionData_Client_Character& ClientData) override
		{
			Super::SetMoveFor(Character, InDeltaTime, NewAcceleration, ClientData);
			if (const USWCharacterMovementComponent* Movement =
				Cast<USWCharacterMovementComponent>(Character->GetCharacterMovement()))
			{
				const float VerticalInput = Movement->GetSwimmingVerticalInput();
				bSavedSwimDive = VerticalInput < -KINDA_SMALL_NUMBER;
				bSavedSwimAscend = VerticalInput > KINDA_SMALL_NUMBER;
				SavedSwimDepthMode = Movement->GetSwimmingDepthMode();
			}
		}

		virtual void PrepMoveFor(ACharacter* Character) override
		{
			Super::PrepMoveFor(Character);
			if (USWCharacterMovementComponent* Movement =
				Cast<USWCharacterMovementComponent>(Character->GetCharacterMovement()))
			{
				const float VerticalInput = bSavedSwimDive ? -1.0f : (bSavedSwimAscend ? 1.0f : 0.0f);
				Movement->RestoreSavedSwimmingState(VerticalInput, SavedSwimDepthMode);
			}
		}
	};

	class FNetworkPredictionData_Client_SWCharacter final
		: public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FNetworkPredictionData_Client_SWCharacter(
			const UCharacterMovementComponent& ClientMovement)
			: FNetworkPredictionData_Client_Character(ClientMovement)
		{
		}

		virtual FSavedMovePtr AllocateNewMove() override
		{
			return MakeShared<FSavedMove_SWCharacter>();
		}
	};
}

void USWCharacterMovementComponent::SetSwimmingVerticalInput(float InVerticalInput)
{
	if (ACharacter* CharOwner = CharacterOwner)
	{
		if (USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			SwimComp->SetVerticalSwimInput(InVerticalInput);
		}
	}
}

float USWCharacterMovementComponent::GetSwimmingVerticalInput() const
{
	if (const ACharacter* CharOwner = CharacterOwner)
	{
		if (const USwimmingComponent* SwimComp =
			CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			return SwimComp->GetVerticalSwimInput();
		}
	}
	return 0.0f;
}

ESwimDepthMode USWCharacterMovementComponent::GetSwimmingDepthMode() const
{
	if (const ACharacter* CharOwner = CharacterOwner)
	{
		if (const USwimmingComponent* SwimComp =
			CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			return SwimComp->GetDepthMode();
		}
	}
	return ESwimDepthMode::Surface;
}

void USWCharacterMovementComponent::RestoreSavedSwimmingState(
	float InVerticalInput,
	ESwimDepthMode InDepthMode)
{
	if (ACharacter* CharOwner = CharacterOwner)
	{
		if (USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			SwimComp->RestorePredictedDepthMode(InDepthMode);
			SwimComp->SetVerticalSwimInput(InVerticalInput);
		}
	}
}

void USWCharacterMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	Super::PhysCustom(DeltaTime, Iterations);

	if (CustomMovementMode == static_cast<uint8>(ECustomMovementMode::CMOVE_Swimming))
	{
		if (ACharacter* CharOwner = CharacterOwner)
		{
			if (USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
			{
				SwimComp->UpdateSwimmingMovement(DeltaTime);
			}
		}
	}
}

void USWCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	const bool bDive = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	const bool bAscend = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
	const float VerticalInput = bDive ? -1.0f : (bAscend ? 1.0f : 0.0f);
	SetSwimmingVerticalInput(VerticalInput);
}

FNetworkPredictionData_Client* USWCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		USWCharacterMovementComponent* MutableThis =
			const_cast<USWCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData =
			new FNetworkPredictionData_Client_SWCharacter(*this);
	}
	return ClientPredictionData;
}

void USWCharacterMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);

	if (ACharacter* CharOwner = CharacterOwner)
	{
		if (USwimmingComponent* SwimComp = CharOwner->FindComponentByClass<USwimmingComponent>())
		{
			SwimComp->CheckWaterTransitions(DeltaSeconds);
		}
	}
}
