#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BossAI/GunnerBossCombatData.h"
#include "BossGunAimLine.generated.h"

class AGunnerBossEnemy;
class ULineBatchComponent;

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
};

/** Asset-free ray renderer. Server replicates state; every rendering client draws its own line. */
UCLASS(NotBlueprintable)
class ENEMY_API ABossGunAimLine : public AActor
{
	GENERATED_BODY()
public:
	ABossGunAimLine();
	void Initialize(AGunnerBossEnemy* Boss, FName Socket, const FGunnerBossRaySettings& Ray);
	void SetAim(EBossGunAimPhase Phase, const FVector& End, float StartTime, float EndTime);
	void ShowShot(const FVector& Start, const FVector& End);
	void FinishPresentation();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UPROPERTY(VisibleAnywhere) TObjectPtr<ULineBatchComponent> RayLines;
	UPROPERTY(ReplicatedUsing = OnRep_VisualState) FBossGunAimVisualState VisualState;
	UFUNCTION() void OnRep_VisualState();
private:
	float ServerTime() const;
	FVector ResolveEnd() const;
	FVector RenderedEnd = FVector::ZeroVector;
	bool bHaveEndpoint = false;
	EBossGunAimPhase PresentedPhase = EBossGunAimPhase::Hidden;
	float PresentedPhaseStart = -1.f;
};
