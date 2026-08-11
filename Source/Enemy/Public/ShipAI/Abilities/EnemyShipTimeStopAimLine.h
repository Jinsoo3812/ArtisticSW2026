#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyShipTimeStopAimLine.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UMaterialInterface;
class AShip;

/** Fixed-direction warning laser whose length clips dynamically against the designated Player Ship. */
UCLASS(Blueprintable)
class ENEMY_API AEnemyShipTimeStopAimLine : public AActor
{
	GENERATED_BODY()

public:
	AEnemyShipTimeStopAimLine();

	void InitializeAimLine(
		const FVector& InStart,
		const FVector& InDirection,
		AShip* InTargetShip,
		float InMaximumDistance,
		float InTraceIntervalSeconds);

	virtual void Tick(float DeltaSeconds) override;

	static FVector ResolveClippedLineEnd(
		const FVector& InStart,
		const FVector& InDirection,
		const AShip* InTargetShip,
		float InMaximumDistance);

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Time Stop|Aim Line")
	FVector GetLineStart() const { return LineStart; }

	UFUNCTION(BlueprintPure, Category = "Enemy Ship|Time Stop|Aim Line")
	FVector GetLineEnd() const { return LineEnd; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> LineMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Aim Line", meta = (ClampMin = "1.0", Units = "cm"))
	float LineThickness = 4.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy Ship|Time Stop|Aim Line")
	TObjectPtr<UMaterialInterface> LaserMaterial;

private:
	UFUNCTION()
	void OnRep_LineEndpoints();

	void UpdateClippedEndpoint();
	void RefreshLineVisual();

	UPROPERTY(ReplicatedUsing = OnRep_LineEndpoints)
	FVector_NetQuantize LineStart = FVector::ZeroVector;

	UPROPERTY(ReplicatedUsing = OnRep_LineEndpoints)
	FVector_NetQuantize LineEnd = FVector::ZeroVector;

	TWeakObjectPtr<AShip> TargetShip;
	FVector FixedDirection = FVector::ForwardVector;
	float MaximumDistance = 200000.0f;
	float TraceIntervalSeconds = 0.05f;
	float TraceTimeAccumulator = 0.0f;
};
