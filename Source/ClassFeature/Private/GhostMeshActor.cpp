// Fill out your copyright notice in the Description page of Project Settings.

#include "GhostMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

AGhostMeshActor::AGhostMeshActor()
{
	PrimaryActorTick.bCanEverTick = false; // 틱 불필요

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;

	// 고스트 메쉬는 어떠한 물리나 충돌 연산도 하지 않음
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCastShadow(false); // 그림자도 끄기
}

void AGhostMeshActor::BeginPlay()
{
	Super::BeginPlay();

	if (BaseGhostMaterial)
	{
		GhostMID = UMaterialInstanceDynamic::Create(BaseGhostMaterial, this);
		MeshComponent->SetMaterial(0, GhostMID);
	}
}

void AGhostMeshActor::SetGhostMesh(UStaticMesh* InMesh)
{
	if (InMesh && MeshComponent)
	{
		MeshComponent->SetStaticMesh(InMesh);
	}
}

void AGhostMeshActor::SetIsValidPosition(bool bIsValid)
{
	if (GhostMID)
	{
		// 머티리얼의 파라미터 이름이 "TintColor"라고 가정
		FLinearColor TargetColor = bIsValid ? ValidColor : InvalidColor;
		GhostMID->SetVectorParameterValue(FName("TintColor"), TargetColor);
	}
}

