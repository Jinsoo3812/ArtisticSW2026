#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BossGunAimLine.generated.h"

class AGunnerBossEnemy;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;

UENUM(BlueprintType)
enum class EBossGunAimPhase : uint8 { Hidden, Tracking, Locked, Recovery };

USTRUCT()
struct FBossGunAimVisualState
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<AGunnerBossEnemy> Boss;
	UPROPERTY() FName Socket;
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
	UPROPERTY() bool bBlockingHit = false;
};

/** Replicated presentation only. No aiming decisions, collision queries or damage live here. */
UCLASS(Blueprintable)
class ENEMY_API ABossGunAimLine : public AActor
{
	GENERATED_BODY()
public:
	ABossGunAimLine();
	bool HasWarningVisual() const;
	void Initialize(AGunnerBossEnemy* Boss, FName Socket);
	void SetAim(EBossGunAimPhase Phase, const FVector& End, float StartTime, float EndTime);
	void ShowShot(const FVector& Start, const FVector& End, bool bBlockingHit);
	void FinishPresentation();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UPROPERTY(EditDefaultsOnly, Category = "Warning") TObjectPtr<UStaticMesh> WarningMesh;
	/** Mesh's longest axis must be local X (e.g. an engine Cube). Bounds are scaled automatically. */
	UPROPERTY(EditDefaultsOnly, Category = "Warning") TObjectPtr<UMaterialInterface> WarningMaterial;
	UPROPERTY(EditDefaultsOnly, Category = "Warning") TObjectPtr<UNiagaraSystem> WarningEffect;
	UPROPERTY(EditDefaultsOnly, Category = "Warning", meta = (ClampMin = "0.1", Units = "cm")) float LineThickness = 3.f;
	UPROPERTY(EditDefaultsOnly, Category = "Warning") FLinearColor TrackingColor = FLinearColor::Red;
	UPROPERTY(EditDefaultsOnly, Category = "Warning") FLinearColor LockedColor = FLinearColor(1.f, 0.6f, 0.f);
	UPROPERTY(EditDefaultsOnly, Category = "Warning") FName ColorParameter = TEXT("BeamColor");
	UPROPERTY(EditDefaultsOnly, Category = "Niagara") FName StartParameter = TEXT("User.BeamStart");
	UPROPERTY(EditDefaultsOnly, Category = "Niagara") FName EndParameter = TEXT("User.BeamEnd");
	UPROPERTY(EditDefaultsOnly, Category = "Niagara") FName NiagaraColorParameter = TEXT("User.BeamColor");
	UPROPERTY(EditDefaultsOnly, Category = "Niagara") FName ProgressParameter = TEXT("User.Progress");
	UPROPERTY(EditDefaultsOnly, Category = "Shot") TObjectPtr<UNiagaraSystem> ShotEffect;
	UPROPERTY(EditDefaultsOnly, Category = "Shot") TObjectPtr<UNiagaraSystem> MuzzleEffect;
	UPROPERTY(EditDefaultsOnly, Category = "Shot") TObjectPtr<UNiagaraSystem> ImpactEffect;
	UPROPERTY(EditDefaultsOnly, Category = "Audio") TObjectPtr<USoundBase> PrepareSound;
	UPROPERTY(EditDefaultsOnly, Category = "Audio") TObjectPtr<USoundBase> LockSound;
	UPROPERTY(EditDefaultsOnly, Category = "Audio") TObjectPtr<USoundBase> FireSound;
	UPROPERTY(EditDefaultsOnly, Category = "Shot", meta = (ClampMin = "0.01", Units = "s")) float ShotEffectLifetime = 0.15f;
	/** Allows the final replicated shot to reach clients even with zero attack recovery. */
	UPROPERTY(EditDefaultsOnly, Category = "Networking", meta = (ClampMin = "0.01", Units = "s")) float EndPresentationLifetime = 0.3f;
	UPROPERTY(EditDefaultsOnly, Category = "Networking", meta = (ClampMin = "0.0")) float EndpointInterpolationSpeed = 20.f;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> LineMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UNiagaraComponent> WarningNiagara;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> LineMaterial;
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> ShotNiagara;
	UPROPERTY(ReplicatedUsing = OnRep_VisualState) FBossGunAimVisualState VisualState;
	UFUNCTION() void OnRep_VisualState();
private:
	float ServerTime() const;
	FVector ResolveEnd() const;
	void PresentState();
	FVector RenderedEnd = FVector::ZeroVector;
	bool bHaveEndpoint = false;
	EBossGunAimPhase PresentedPhase = EBossGunAimPhase::Hidden;
	float PresentedPhaseStart = -1.f;
	uint32 PresentedShotSerial = 0;
};
