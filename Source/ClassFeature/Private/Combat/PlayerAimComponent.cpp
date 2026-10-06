#include "Combat/PlayerAimComponent.h"
#include "Combat/PlayerBowAimResolver.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Item/Projectiles/ProjectileShotPreparation.h"
#include "SceneView.h"

bool FGameplayAbilityTargetData_ProjectileShot::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << ShotId;
	bOutSuccess = !Ar.IsError();
	return true;
}

UPlayerAimComponent::UPlayerAimComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetIsReplicatedByDefault(true);
}

bool UPlayerAimComponent::CreateReleaseRequest(FGameplayEventData& EventData) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled()) return false;
	auto* Request = new FGameplayAbilityTargetData_ProjectileShot();
	Request->ShotId = FGuid::NewGuid();
	EventData.TargetData.Add(Request);
	return true;
}

bool UPlayerAimComponent::BeginShot(const FGameplayEventData& EventData, FGuid& OutShotId)
{
	OutShotId.Invalidate();
	const FGameplayAbilityTargetData* Data = EventData.TargetData.Num() == 1 ? EventData.TargetData.Get(0) : nullptr;
	if (!Data || Data->GetScriptStruct() != FGameplayAbilityTargetData_ProjectileShot::StaticStruct()
		|| ActiveShotId.IsValid()) return false;
	const FGuid Id = static_cast<const FGameplayAbilityTargetData_ProjectileShot*>(Data)->ShotId;
	if (!Id.IsValid() || RecentShotIds.Contains(Id)) return false;
	if (RecentShotIds.Num() == 32) RecentShotIds.RemoveAt(0);
	RecentShotIds.Add(Id);
	ActiveShotId = Id;
	OutShotId = Id;
	ShotView = FPlayerShotView();
	bHasView = false;
	bViewRejected = false;
	bShotResolved = false;
	bShotSucceeded = false;
	return true;
}

void UPlayerAimComponent::EndShot(const FGuid& ShotId)
{
	if (ActiveShotId != ShotId) return;
	ActiveShotId.Invalidate();
	ShotView = FPlayerShotView();
	bHasView = false;
	bViewRejected = false;
	bShotResolved = false;
	bShotSucceeded = false;
}

void UPlayerAimComponent::CompleteShot(const FGuid& ShotId, bool bSucceeded)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ShotId.IsValid()
		|| ShotId != ActiveShotId || bShotResolved) return;
	bShotResolved = true;
	bShotSucceeded = bSucceeded;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (Pawn && !Pawn->IsLocallyControlled()) ClientResolveShot(ShotId, bSucceeded);
}

void UPlayerAimComponent::ClientResolveShot_Implementation(const FGuid& ShotId, bool bSucceeded)
{
	if (!ActiveShotId.IsValid() || ShotId != ActiveShotId || bShotResolved) return;
	bShotResolved = true;
	bShotSucceeded = bSucceeded;
}

bool UPlayerAimComponent::TryGetShotResolution(const FGuid& ShotId, bool& OutSucceeded) const
{
	OutSucceeded = bShotSucceeded;
	return ShotId.IsValid() && ShotId == ActiveShotId && bShotResolved;
}

bool UPlayerAimComponent::CaptureShotView(const FGuid& ShotId)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled() || ShotId != ActiveShotId || !ShotId.IsValid()) return false;
	if (bHasView) return true;
	FPlayerShotView View;
	if (!CaptureCurrentView(View)) return false;
	View.ShotId = ShotId;
	if (!ValidateView(View)) return false;
	AcceptView(View);
	if (!Pawn->HasAuthority()) ServerSubmitShotView(View);
	return bHasView;
}

bool UPlayerAimComponent::CaptureCurrentView(FPlayerShotView& View) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled()) return false;
	const APlayerController* Controller = Cast<APlayerController>(Pawn->GetController());
	const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	if (!LocalPlayer || !LocalPlayer->ViewportClient) return false;
	FSceneViewProjectionData ProjectionData;
	if (!LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, ProjectionData)) return false;
	const FIntRect ViewRect = ProjectionData.GetConstrainedViewRect();
	const FVector2D Center = FVector2D(ViewRect.Min) + FVector2D(ViewRect.Size()) * 0.5;
	FVector Origin, Direction;
	if (!Controller->DeprojectScreenPositionToWorld(Center.X, Center.Y, Origin, Direction)) return false;
	View.Origin = Origin;
	View.Direction = Direction.GetSafeNormal();
	View.SampleServerTime = ProjectileShotPreparation::GetServerTime(GetWorld());
	return !Origin.ContainsNaN() && !Direction.ContainsNaN() && !Direction.IsNearlyZero();
}

bool UPlayerAimComponent::ValidateView(const FPlayerShotView& View) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const FVector Direction = View.Direction;
	const double Age = ProjectileShotPreparation::GetServerTime(GetWorld()) - View.SampleServerTime;
	if (!Pawn || !GetWorld() || !View.ShotId.IsValid() || View.Origin.ContainsNaN()
		|| Direction.ContainsNaN() || !FMath::IsNearlyEqual(Direction.SizeSquared(), 1.0, 0.01)
		|| !FMath::IsFinite(Age) || Age < -MaxFutureViewTime || Age > MaxShotViewAge
		|| !FMath::IsFinite(TraceDistance) || TraceDistance < 100.0f) return false;
	return FVector::DistSquared(View.Origin, Pawn->GetPawnViewLocation()) <= FMath::Square(MaxViewOriginDistance)
		&& FVector::DotProduct(Direction, Pawn->GetBaseAimRotation().Vector())
			>= FMath::Cos(FMath::DegreesToRadians(MaxViewAngleDegrees));
}

void UPlayerAimComponent::AcceptView(const FPlayerShotView& View)
{
	// One packet per authorized release. A late/duplicate packet cannot alter another shot.
	if (!ActiveShotId.IsValid() || View.ShotId != ActiveShotId || bHasView || bViewRejected) return;
	bViewRejected = !ValidateView(View);
	if (bViewRejected)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ProjectileAim] Invalid view Id=%s Player=%s"), *View.ShotId.ToString(), *GetNameSafe(GetOwner()));
		return;
	}
	ShotView = View;
	bHasView = true;
	const IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("sw.Projectile.DebugLaunch"));
	if (Debug && Debug->GetInt() != 0)
		UE_LOG(LogTemp, Display, TEXT("[ProjectileAim] Id=%s Role=%d Sample=%.3f Received=%.3f Origin=%s Direction=%s"),
			*View.ShotId.ToString(), int32(GetOwner()->GetLocalRole()), View.SampleServerTime,
			ProjectileShotPreparation::GetServerTime(GetWorld()), *FVector(View.Origin).ToString(), *FVector(View.Direction).ToString());
}

void UPlayerAimComponent::ServerSubmitShotView_Implementation(const FPlayerShotView& View)
{
	AcceptView(View);
}

EPlayerShotAimResult UPlayerAimComponent::ResolveShotAim(const FGuid& ShotId, const AActor* Weapon, const FVector& Muzzle,
	FVector& OutTarget, FVector& OutViewDirection, double& OutAimTime) const
{
	OutTarget = FVector::ZeroVector;
	OutViewDirection = FVector::ZeroVector;
	OutAimTime = 0.0;
	if (!ShotId.IsValid() || ShotId != ActiveShotId || bViewRejected) return EPlayerShotAimResult::Rejected;
	if (!bHasView) return EPlayerShotAimResult::Pending;
	// Recheck age, not direction against a later ControlRotation: intent was validated on receipt.
	const double Age = ProjectileShotPreparation::GetServerTime(GetWorld()) - ShotView.SampleServerTime;
	if (!GetWorld() || !FMath::IsFinite(Age) || Age > MaxShotViewAge || Age < -MaxFutureViewTime)
		return EPlayerShotAimResult::Rejected;
	OutAimTime = ShotView.SampleServerTime;
	return TraceView(ShotView, Weapon, Muzzle, OutTarget, OutViewDirection)
		? EPlayerShotAimResult::Ready : EPlayerShotAimResult::Rejected;
}

bool UPlayerAimComponent::TraceView(const FPlayerShotView& View, const AActor* Weapon, const FVector& Muzzle,
	FVector& OutTarget, FVector& OutViewDirection) const
{
	return PlayerBowAimResolver::Resolve(GetWorld(), GetOwner(), Weapon, Muzzle,
		View.Origin, View.Direction, TraceDistance, View.ShotId, OutTarget, OutViewDirection);
}

bool UPlayerAimComponent::ResolveCurrentAim(const AActor* Weapon, const FVector& Muzzle, FVector& OutTarget,
	FVector& OutDirection, double& OutTime) const
{
	FPlayerShotView View;
	if (!GetWorld() || !FMath::IsFinite(TraceDistance) || TraceDistance < 100.0f || !CaptureCurrentView(View)) return false;
	OutTime = View.SampleServerTime;
	return TraceView(View, Weapon, Muzzle, OutTarget, OutDirection);
}

void UPlayerAimComponent::SetObstructionQuery(FPlayerAimObstructionQuery Query)
{
	ObstructionQuery = MoveTemp(Query);
	bPreviewObstructed = false;
	SetComponentTickEnabled(ObstructionQuery.IsBound());
}

void UPlayerAimComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (!ObstructionQuery.IsBound()) { SetComponentTickEnabled(false); bPreviewObstructed = false; return; }
	const FPlayerAimObstructionQuery Query = ObstructionQuery;
	bPreviewObstructed = Query.Execute();
}

void UPlayerAimComponent::ReportShotObstruction(bool bBlocked)
{
	ObstructionUntil = bBlocked && GetWorld() ? GetWorld()->GetTimeSeconds() + 0.5 : 0.0;
}

bool UPlayerAimComponent::IsShotObstructed() const
{
	return bPreviewObstructed || (GetWorld() && GetWorld()->GetTimeSeconds() < ObstructionUntil);
}
