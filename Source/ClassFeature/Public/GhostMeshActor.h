// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GhostMeshActor.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class CLASSFEATURE_API AGhostMeshActor : public AActor
{
	GENERATED_BODY()

public:
	AGhostMeshActor();

	// 고스트 메쉬 외형 설정
	void SetGhostMesh(class UStaticMesh* InMesh);

	// 유효성 상태에 따라 머티리얼 색상 변경
	void SetIsValidPosition(bool bIsValid);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	// 색상 변경을 위한 동적 머티리얼 인스턴스 (MID)
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GhostMID;

	// 블루프린트에서 할당할 기본 고스트 머티리얼 (반투명 재질 권장)
	UPROPERTY(EditDefaultsOnly, Category = "Ghost")
	TObjectPtr<class UMaterialInterface> BaseGhostMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Ghost")
	FLinearColor ValidColor = FLinearColor(0.0f, 1.0f, 0.0f, 0.5f); // 초록색

	UPROPERTY(EditDefaultsOnly, Category = "Ghost")
	FLinearColor InvalidColor = FLinearColor(1.0f, 0.0f, 0.0f, 0.5f); // 빨간색
};
