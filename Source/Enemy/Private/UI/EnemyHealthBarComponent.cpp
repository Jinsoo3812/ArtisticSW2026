#include "UI/EnemyHealthBarComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "BaseGameplayTags.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionChannels.h"
#include "Components/BaseHealthComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UI/EnemyHealthBarWidget.h"

UEnemyHealthBarComponent::UEnemyHealthBarComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = VisibilityEvaluationInterval;

	SetWidgetSpace(EWidgetSpace::Screen);
	SetWidgetClass(UEnemyHealthBarWidget::StaticClass());
	SetDrawAtDesiredSize(false);
	SetDrawSize(FVector2D(180.0f, 24.0f));
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetWindowFocusable(false);
	SetVisibility(false);
}

void UEnemyHealthBarComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UEnemyHealthBarComponent, bRevealedByPlayerDamage);
}

void UEnemyHealthBarComponent::BeginPlay()
{
	Super::BeginPlay();
	PrimaryComponentTick.TickInterval = FMath::Max(0.02f, VisibilityEvaluationInterval);
	BindHealthComponent();
	InitWidget();
	RefreshHealth();
	RefreshLocalVisibility();
}

void UEnemyHealthBarComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeathHideTimerHandle);
	}
	UnbindHealthComponent();
	Super::EndPlay(EndPlayReason);
}

void UEnemyHealthBarComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshLocalVisibility();
}

void UEnemyHealthBarComponent::ConfigurePresentation(
	const FVector& RelativeOffset,
	const FVector2D& InDrawSize)
{
	SetRelativeLocation(RelativeOffset);
	SetDrawSize(InDrawSize);
}

void UEnemyHealthBarComponent::SetVisibilitySourceComponent(
	UPrimitiveComponent* InVisibilitySourceComponent)
{
	VisibilitySourceComponent = InVisibilitySourceComponent;
	RefreshLocalVisibility();
}

void UEnemyHealthBarComponent::SetOwnerPresentationActive(bool bNewActive)
{
	bOwnerPresentationActive = bNewActive;
	RefreshLocalVisibility();
}

void UEnemyHealthBarComponent::ResetRevealState()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeathHideTimerHandle);
	}
	bDeathHideDelayActive = false;
	bHiddenForDepletedHealth = false;

	if (bRevealedByPlayerDamage)
	{
		bRevealedByPlayerDamage = false;
		Owner->ForceNetUpdate();
	}
	RefreshLocalVisibility();
}

bool UEnemyHealthBarComponent::ResolveShouldDisplay(
	bool bRevealed,
	bool bInViewport,
	bool bHasLineOfSight,
	bool bVisibilitySourcePresented,
	bool bDead,
	bool bPresentationActive)
{
	return bRevealed && bInViewport && bHasLineOfSight
		&& bVisibilitySourcePresented && !bDead && bPresentationActive;
}

void UEnemyHealthBarComponent::HandleHealthChanged(
	UBaseHealthComponent* InHealthComponent,
	float OldValue,
	float NewValue,
	AActor* InstigatorActor)
{
	RefreshHealth();
	if (OldValue > 0.0f && NewValue <= 0.0f)
	{
		StartDepletedHealthHideDelay();
	}
	else if (NewValue > 0.0f && (bDeathHideDelayActive || bHiddenForDepletedHealth))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(DeathHideTimerHandle);
		}
		bDeathHideDelayActive = false;
		bHiddenForDepletedHealth = false;
		RefreshLocalVisibility();
	}

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || bRevealedByPlayerDamage
		|| OldValue <= NewValue || !IsPlayerDamageSource(InstigatorActor))
	{
		return;
	}

	bRevealedByPlayerDamage = true;
	Owner->ForceNetUpdate();
	RefreshLocalVisibility();
}

void UEnemyHealthBarComponent::HandleMaxHealthChanged(
	UBaseHealthComponent* InHealthComponent,
	float OldValue,
	float NewValue,
	AActor* InstigatorActor)
{
	RefreshHealth();
}

void UEnemyHealthBarComponent::StartDepletedHealthHideDelay()
{
	UWorld* World = GetWorld();
	if (!World || DeathHideDelay <= 0.0f)
	{
		HideAfterDeathDelay();
		return;
	}

	bHiddenForDepletedHealth = false;
	bDeathHideDelayActive = true;
	World->GetTimerManager().ClearTimer(DeathHideTimerHandle);
	World->GetTimerManager().SetTimer(
		DeathHideTimerHandle,
		this,
		&UEnemyHealthBarComponent::HideAfterDeathDelay,
		DeathHideDelay,
		false);
	RefreshLocalVisibility();
}

void UEnemyHealthBarComponent::OnRep_RevealedByPlayerDamage()
{
	RefreshLocalVisibility();
}

void UEnemyHealthBarComponent::HideAfterDeathDelay()
{
	bDeathHideDelayActive = false;
	bHiddenForDepletedHealth = true;
	RefreshLocalVisibility();
}

void UEnemyHealthBarComponent::BindHealthComponent()
{
	UnbindHealthComponent();
	BoundHealthComponent = GetOwner() ? GetOwner()->FindComponentByClass<UBaseHealthComponent>() : nullptr;
	if (!BoundHealthComponent)
	{
		return;
	}

	BoundHealthComponent->OnHealthChanged.AddUniqueDynamic(
		this, &UEnemyHealthBarComponent::HandleHealthChanged);
	BoundHealthComponent->OnMaxHealthChanged.AddUniqueDynamic(
		this, &UEnemyHealthBarComponent::HandleMaxHealthChanged);
}

void UEnemyHealthBarComponent::UnbindHealthComponent()
{
	if (!BoundHealthComponent)
	{
		return;
	}

	BoundHealthComponent->OnHealthChanged.RemoveDynamic(
		this, &UEnemyHealthBarComponent::HandleHealthChanged);
	BoundHealthComponent->OnMaxHealthChanged.RemoveDynamic(
		this, &UEnemyHealthBarComponent::HandleMaxHealthChanged);
	BoundHealthComponent = nullptr;
}

void UEnemyHealthBarComponent::RefreshHealth()
{
	if (UEnemyHealthBarWidget* HealthBarWidget =
		Cast<UEnemyHealthBarWidget>(GetUserWidgetObject()))
	{
		HealthBarWidget->SetHealthValues(
			BoundHealthComponent ? BoundHealthComponent->GetHealth() : 0.0f,
			BoundHealthComponent ? BoundHealthComponent->GetMaxHealth() : 0.0f);
	}
}

void UEnemyHealthBarComponent::RefreshLocalVisibility()
{
	const bool bDead = !BoundHealthComponent || bHiddenForDepletedHealth
		|| (BoundHealthComponent->GetHealth() <= 0.0f && !bDeathHideDelayActive);
	APlayerController* PlayerController = GetWorld()
		? GetWorld()->GetFirstPlayerController()
		: nullptr;
	const bool bHasLocalView = PlayerController && PlayerController->IsLocalController();
	const bool bVisibilitySourcePresented = IsVisibilitySourcePresented();
	const bool bInViewport = bHasLocalView && IsAnchorInLocalViewport(PlayerController);
	const bool bHasLineOfSight = bInViewport && HasLineOfSightFromLocalCamera(PlayerController);
	const bool bShouldDisplay = ResolveShouldDisplay(
		bRevealedByPlayerDamage,
		bInViewport,
		bHasLineOfSight,
		bVisibilitySourcePresented,
		bDead,
		bOwnerPresentationActive);

	SetVisibility(bShouldDisplay);
	if (!bShouldDisplay)
	{
		// Screen-space widget components normally leave the game layer during their
		// next component tick. Depleted health disables that tick immediately, so
		// remove this widget now instead of leaving it visible until owner teardown.
		RemoveWidgetFromScreen();
	}
	SetComponentTickEnabled(
		bHasLocalView && bRevealedByPlayerDamage && !bDead && bOwnerPresentationActive);
}

bool UEnemyHealthBarComponent::IsVisibilitySourcePresented() const
{
	const AActor* Owner = GetOwner();
	if (!Owner || Owner->IsHidden())
	{
		return false;
	}

	return !VisibilitySourceComponent
		|| (VisibilitySourceComponent->IsRegistered() && VisibilitySourceComponent->IsVisible());
}

bool UEnemyHealthBarComponent::IsAnchorInLocalViewport(APlayerController* PlayerController) const
{
	if (!PlayerController)
	{
		return false;
	}

	FVector2D ScreenPosition;
	if (!UGameplayStatics::ProjectWorldToScreen(
		PlayerController, GetComponentLocation(), ScreenPosition, true))
	{
		return false;
	}

	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
	return ViewportWidth > 0 && ViewportHeight > 0
		&& ScreenPosition.X >= 0.0f && ScreenPosition.X <= static_cast<float>(ViewportWidth)
		&& ScreenPosition.Y >= 0.0f && ScreenPosition.Y <= static_cast<float>(ViewportHeight);
}

bool UEnemyHealthBarComponent::HasLineOfSightFromLocalCamera(
	APlayerController* PlayerController) const
{
	const AActor* Owner = GetOwner();
	const APlayerCameraManager* CameraManager = PlayerController
		? PlayerController->PlayerCameraManager
		: nullptr;
	UWorld* World = GetWorld();
	if (!Owner || !CameraManager || !World)
	{
		return false;
	}

	const FVector CameraLocation = CameraManager->GetCameraLocation();
	TArray<FVector> SamplePoints;
	BuildLineOfSightSamplePoints(CameraLocation, SamplePoints);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EnemyHealthBarLOS), false);
	QueryParams.AddIgnoredActor(Owner);
	if (const APawn* ViewerPawn = PlayerController->GetPawn())
	{
		QueryParams.AddIgnoredActor(ViewerPawn);
	}

	for (const FVector& SamplePoint : SamplePoints)
	{
		FHitResult WorldHit;
		const bool bBlockedByWorld = World->LineTraceSingleByChannel(
			WorldHit, CameraLocation, SamplePoint, ECC_Visibility, QueryParams);

		FHitResult ShipHit;
		const bool bBlockedByShip = World->LineTraceSingleByChannel(
			ShipHit, CameraLocation, SamplePoint, ECC_EnemyHealthBarLOS, QueryParams);
		if (!bBlockedByWorld && !bBlockedByShip)
		{
			return true;
		}
	}

	return false;
}

void UEnemyHealthBarComponent::BuildLineOfSightSamplePoints(
	const FVector& CameraLocation,
	TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	FBox TargetBounds(EForceInit::ForceInit);
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Owner);
	for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (PrimitiveComponent && PrimitiveComponent != this
			&& PrimitiveComponent->IsRegistered()
			&& PrimitiveComponent->Bounds.SphereRadius > KINDA_SMALL_NUMBER)
		{
			TargetBounds += PrimitiveComponent->Bounds.GetBox();
		}
	}

	FVector BoundsOrigin = Owner->GetActorLocation();
	FVector BoundsExtent(40.0f, 40.0f, 90.0f);
	if (TargetBounds.IsValid)
	{
		BoundsOrigin = TargetBounds.GetCenter();
		BoundsExtent = TargetBounds.GetExtent();
	}

	OutPoints.Reserve(5);
	OutPoints.Add(BoundsOrigin);
	OutPoints.Add(BoundsOrigin + FVector(0.0f, 0.0f, BoundsExtent.Z * 0.6f));
	OutPoints.Add(BoundsOrigin - FVector(0.0f, 0.0f, BoundsExtent.Z * 0.35f));

	const FVector CameraDirection2D = (BoundsOrigin - CameraLocation).GetSafeNormal2D();
	const FVector LateralDirection(-CameraDirection2D.Y, CameraDirection2D.X, 0.0f);
	const float LateralExtent = FMath::Max(BoundsExtent.X, BoundsExtent.Y) * 0.5f;
	OutPoints.Add(BoundsOrigin + LateralDirection * LateralExtent);
	OutPoints.Add(BoundsOrigin - LateralDirection * LateralExtent);
}

bool UEnemyHealthBarComponent::IsPlayerDamageSource(const AActor* SourceActor)
{
	if (!IsValid(SourceActor))
	{
		return false;
	}

	if (const APawn* SourcePawn = Cast<APawn>(SourceActor); SourcePawn && SourcePawn->IsPlayerControlled())
	{
		return true;
	}

	if (const IAbilitySystemInterface* AbilitySystemSource = Cast<IAbilitySystemInterface>(SourceActor))
	{
		if (const UAbilitySystemComponent* SourceASC = AbilitySystemSource->GetAbilitySystemComponent())
		{
			if (SourceASC->HasMatchingGameplayTag(Team_Player))
			{
				return true;
			}
		}
	}

	return SourceActor->ActorHasTag(TEXT("Player"))
		&& !SourceActor->ActorHasTag(TEXT("Enemy"));
}
