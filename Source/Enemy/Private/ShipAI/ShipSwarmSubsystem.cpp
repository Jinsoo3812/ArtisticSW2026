// Fill out your copyright notice in the Description page of Project Settings.

#include "ShipAI/ShipSwarmSubsystem.h"
#include "ShipAI/EnemyShip.h"
#include "ShipAI/EnemyShipArchetypeData.h"

void UShipSwarmSubsystem::RegisterShip(AEnemyShip* Ship)
{
	if (!Ship) return;

	FName SquadID = Ship->SquadID;
	TArray<TWeakObjectPtr<AEnemyShip>>& SquadArray = SquadMap.FindOrAdd(SquadID);

	// 중복 등록 방지
	TWeakObjectPtr<AEnemyShip> ShipWeakPtr(Ship);
	if (!SquadArray.Contains(ShipWeakPtr))
	{
		SquadArray.Add(ShipWeakPtr);
		UE_LOG(LogTemp, Log, TEXT("UShipSwarmSubsystem::RegisterShip - Registered [%s] to Squad [%s]. Total members: %d"), 
			*Ship->GetName(), *SquadID.ToString(), SquadArray.Num());
		RecalculateSquadOrbitDistances(SquadID);
	}
}

void UShipSwarmSubsystem::UnregisterShip(AEnemyShip* Ship)
{
	if (!Ship) return;

	FName SquadID = Ship->SquadID;
	if (TArray<TWeakObjectPtr<AEnemyShip>>* SquadArray = SquadMap.Find(SquadID))
	{
		TWeakObjectPtr<AEnemyShip> ShipWeakPtr(Ship);
		int32 RemovedCount = SquadArray->Remove(ShipWeakPtr);
		if (RemovedCount > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("UShipSwarmSubsystem::UnregisterShip - Unregistered [%s] from Squad [%s]. Remaining members: %d"), 
				*Ship->GetName(), *SquadID.ToString(), SquadArray->Num());
		}

		// 군집이 비어있으면 맵에서 정리
		if (SquadArray->Num() == 0)
		{
			SquadMap.Remove(SquadID);
		}
		else if (RemovedCount > 0)
		{
			RecalculateSquadOrbitDistances(SquadID);
		}
	}
}

TArray<AEnemyShip*> UShipSwarmSubsystem::GetSquadMembers(FName SquadID)
{
	TArray<AEnemyShip*> ValidMembers;
	
	if (TArray<TWeakObjectPtr<AEnemyShip>>* SquadArray = SquadMap.Find(SquadID))
	{
		// 역순으로 탐색하여 파괴된 객체(Null) 정리와 동시에 유효한 멤버 수집
		for (int32 i = SquadArray->Num() - 1; i >= 0; --i)
		{
			if (AEnemyShip* Member = (*SquadArray)[i].Get())
			{
				ValidMembers.Add(Member);
			}
			else
			{
				// 파괴되어 메모리에서 사라진 배 자동 정리 (TWeakObjectPtr의 강점)
				SquadArray->RemoveAtSwap(i);
			}
		}
	}

	return ValidMembers;
}

void UShipSwarmSubsystem::RecalculateSquadOrbitDistances(FName SquadID)
{
	TArray<AEnemyShip*> Members = GetSquadMembers(SquadID);
	Members.RemoveAll([](const AEnemyShip* Member)
	{
		return !IsValid(Member) || !IsValid(Member->EnemyShipArchetype) || !Member->GetNavigationComponent();
	});
	if (Members.IsEmpty())
	{
		return;
	}

	Members.Sort([](const AEnemyShip& Left, const AEnemyShip& Right)
	{
		return Left.GetFName().LexicalLess(Right.GetFName());
	});

	float IdealDistanceSum = 0.0f;
	float SpacingSum = 0.0f;
	for (const AEnemyShip* Member : Members)
	{
		IdealDistanceSum += Member->EnemyShipArchetype->NavigationProfile.IdealDistance;
		SpacingSum += FMath::Max(0.0f, Member->EnemyShipArchetype->OrbitDistanceSpacing);
	}

	const float AverageIdealDistance = IdealDistanceSum / Members.Num();
	const float AverageSpacing = SpacingSum / Members.Num();
	const float CenterIndex = (Members.Num() - 1) * 0.5f;
	for (int32 Index = 0; Index < Members.Num(); ++Index)
	{
		const float AssignedDistance = AverageIdealDistance + (Index - CenterIndex) * AverageSpacing;
		Members[Index]->SetSquadAssignedIdealDistance(FMath::Max(1.0f, AssignedDistance));
	}
}
