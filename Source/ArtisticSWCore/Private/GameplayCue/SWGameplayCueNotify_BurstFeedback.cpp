#include "GameplayCue/SWGameplayCueNotify_BurstFeedback.h"

#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

USWGameplayCueNotify_BurstFeedback::USWGameplayCueNotify_BurstFeedback()
{
	IsOverride = true;
}

bool USWGameplayCueNotify_BurstFeedback::OnExecute_Implementation(
	AActor* MyTarget,
	const FGameplayCueParameters& Parameters) const
{
	UWorld* World = MyTarget ? MyTarget->GetWorld() : GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	const FVector Location = ResolveFeedbackLocation(MyTarget, Parameters);
	FVector Normal = Parameters.Normal;
	if (const FHitResult* HitResult = Parameters.EffectContext.GetHitResult())
	{
		Normal = HitResult->ImpactNormal;
	}
	if (Normal.IsNearlyZero())
	{
		Normal = MyTarget ? MyTarget->GetActorUpVector() : FVector::UpVector;
	}

	if (NiagaraSystem)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World,
			NiagaraSystem,
			Location,
			Normal.Rotation());
	}
	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(World, Sound, Location);
	}

	if (CameraShakeClass && CameraShakeScale > 0.0f)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PlayerController = It->Get();
			if (!PlayerController || !PlayerController->IsLocalController()
				|| !PlayerController->PlayerCameraManager
				|| !ShouldShakeController(*PlayerController, MyTarget, Parameters))
			{
				continue;
			}

			const float LocalScale = CameraShakeRecipient
				== ESWGameplayCueCameraShakeRecipient::AllLocalPlayersInRadius
				? CalculateCameraShakeScale(*PlayerController->PlayerCameraManager, Location)
				: CameraShakeScale;
			if (LocalScale > KINDA_SMALL_NUMBER)
			{
				PlayerController->PlayerCameraManager->StartCameraShake(
					CameraShakeClass,
					LocalScale);
			}
		}
	}

	return true;
}

FVector USWGameplayCueNotify_BurstFeedback::ResolveFeedbackLocation(
	AActor* MyTarget,
	const FGameplayCueParameters& Parameters)
{
	if (const FHitResult* HitResult = Parameters.EffectContext.GetHitResult())
	{
		return HitResult->ImpactPoint;
	}
	if (!Parameters.Location.IsNearlyZero())
	{
		return Parameters.Location;
	}
	return MyTarget ? MyTarget->GetActorLocation() : FVector::ZeroVector;
}

bool USWGameplayCueNotify_BurstFeedback::ShouldShakeController(
	const APlayerController& PlayerController,
	AActor* MyTarget,
	const FGameplayCueParameters& Parameters) const
{
	if (CameraShakeRecipient == ESWGameplayCueCameraShakeRecipient::AllLocalPlayersInRadius)
	{
		return true;
	}

	AActor* RelevantActor = CameraShakeRecipient
		== ESWGameplayCueCameraShakeRecipient::InstigatorLocalPlayer
		? Parameters.GetInstigator()
		: MyTarget;
	const APawn* RelevantPawn = Cast<APawn>(RelevantActor);
	return RelevantPawn && RelevantPawn->GetController() == &PlayerController;
}

float USWGameplayCueNotify_BurstFeedback::CalculateCameraShakeScale(
	const APlayerCameraManager& CameraManager,
	const FVector& Epicenter) const
{
	const float InnerRadius = FMath::Max(0.0f, CameraShakeInnerRadius);
	const float OuterRadius = FMath::Max(InnerRadius, CameraShakeOuterRadius);
	const float Distance = FVector::Distance(CameraManager.GetCameraLocation(), Epicenter);
	if (Distance > OuterRadius)
	{
		return 0.0f;
	}
	if (Distance <= InnerRadius || FMath::IsNearlyEqual(InnerRadius, OuterRadius))
	{
		return CameraShakeScale;
	}

	const float Alpha = 1.0f - FMath::Clamp(
		(Distance - InnerRadius) / (OuterRadius - InnerRadius),
		0.0f,
		1.0f);
	return CameraShakeScale * FMath::Pow(Alpha, FMath::Max(0.01f, CameraShakeFalloff));
}
