#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BossAI/GunnerBossCombatData.h"
#include "BossGunAimLine.generated.h"

class AGunnerBossEnemy;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EBossGunAimPhase : uint8 { Hidden, Tracking, Locked, Recovery };

USTRUCT()
struct FBossGunAimVisualState
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<AGunnerBossEnemy> Boss;
	UPROPERTY() FName Socket;
	UPROPERTY() FGunnerBossRaySettings Ray;
	UPROPERTY() EBossGunAimPhase Phase = EBossGunAimPhase::Hidden;
	UPROPERTY() FVector_NetQuantize End = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize LocalEnd = FVector::ZeroVector;
	UPROPERTY() bool bDeckLocalEnd = false;
	UPROPERTY() float PhaseStartTime = 0.f;
	UPROPERTY() float PhaseEndTime = 0.f;
	UPROPERTY() uint32 ShotSerial = 0;
	UPROPERTY() FVector_NetQuantize ShotStart = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize ShotEnd = FVector::ZeroVector;
	UPROPERTY() float ShotTime = 0.f;
	UPROPERTY() TObjectPtr<USceneComponent> EndAnchor;
	UPROPERTY() FName EndBone;
	UPROPERTY() bool bHasEndAnchor = false;
	UPROPERTY() bool bActive = false;
	UPROPERTY() float PlannedShotTime = -1.f;
	UPROPERTY() bool bAttackFinished = false;
};

/** Live muzzle-to-endpoint cylinder for one attack; damage remains the instantaneous server trace. */
UCLASS(NotBlueprintable)
class ENEMY_API ABossGunAimLine : public AActor
{
	GENERATED_BODY()
public:
	ABossGunAimLine();
	void Initialize(AGunnerBossEnemy* Boss, FName Socket, const FGunnerBossRaySettings& Ray);
	void SetAim(EBossGunAimPhase Phase, const FVector& End, float StartTime, float EndTime,
		USceneComponent* EndAnchor = nullptr, FName EndBone = NAME_None, float PlannedShotTime = -1.f);
	void ShowShot(const FVector& Start, const FVector& End, USceneComponent* EndAnchor = nullptr, FName EndBone = NAME_None);
	bool ResolveEndpoints(FVector& OutStart, FVector& OutEnd) const;
	bool IsVisibleAt(float ServerSeconds) const;
	/** Normal completion and cancellation both hide and destroy the cylinder immediately. */
	void FinishPresentation(bool bCancelled = true);
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> ShotCylinder;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ShotMaterialInstance;
	UPROPERTY(ReplicatedUsing = OnRep_VisualState) FBossGunAimVisualState VisualState;
	UFUNCTION() void OnRep_VisualState();
private:
	float ServerTime() const;
	void RefreshShotPresentation();
	void SetEndpoint(const FVector& End, USceneComponent* Anchor, FName Bone);
	TWeakObjectPtr<UMaterialInterface> AppliedMaterial;
};
