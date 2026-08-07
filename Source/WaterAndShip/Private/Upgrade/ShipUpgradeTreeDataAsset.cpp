#include "Upgrade/ShipUpgradeTreeDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FShipUpgradeNodeDefinition* UShipUpgradeTreeDataAsset::FindNode(FName NodeId) const
{
	return Nodes.FindByPredicate([NodeId](const FShipUpgradeNodeDefinition& Node)
	{
		return Node.NodeId == NodeId;
	});
}

bool UShipUpgradeTreeDataAsset::ValidateTree(TArray<FText>& OutErrors) const
{
	OutErrors.Reset();
	TSet<FName> NodeIds;
	for (const FShipUpgradeNodeDefinition& Node : Nodes)
	{
		if (Node.NodeId.IsNone())
		{
			OutErrors.Add(NSLOCTEXT("ShipUpgrade", "EmptyNodeId", "강화 노드에 비어 있는 NodeId가 있습니다."));
			continue;
		}
		if (NodeIds.Contains(Node.NodeId))
		{
			OutErrors.Add(FText::Format(NSLOCTEXT("ShipUpgrade", "DuplicateNodeId", "중복 NodeId: {0}"), FText::FromName(Node.NodeId)));
		}
		NodeIds.Add(Node.NodeId);
		if (Node.StatModifiers.IsEmpty())
		{
			OutErrors.Add(FText::Format(NSLOCTEXT("ShipUpgrade", "EmptyModifiers", "스탯 변경이 없는 노드: {0}"), FText::FromName(Node.NodeId)));
		}
	}

	for (const FShipUpgradeNodeDefinition& Node : Nodes)
	{
		TSet<FName> UniquePrerequisites;
		for (FName Prerequisite : Node.PrerequisiteNodeIds)
		{
			if (UniquePrerequisites.Contains(Prerequisite))
			{
				OutErrors.Add(FText::Format(NSLOCTEXT("ShipUpgrade", "DuplicatePrerequisite", "Node {0} contains duplicate prerequisite {1}."), FText::FromName(Node.NodeId), FText::FromName(Prerequisite)));
			}
			UniquePrerequisites.Add(Prerequisite);
			if (Prerequisite == Node.NodeId)
			{
				OutErrors.Add(FText::Format(NSLOCTEXT("ShipUpgrade", "SelfReference", "자기 자신을 선행 조건으로 지정한 노드: {0}"), FText::FromName(Node.NodeId)));
			}
			else if (!NodeIds.Contains(Prerequisite))
			{
				OutErrors.Add(FText::Format(NSLOCTEXT("ShipUpgrade", "MissingPrerequisite", "{0}의 선행 노드 {1}가 존재하지 않습니다."), FText::FromName(Node.NodeId), FText::FromName(Prerequisite)));
			}
		}

		TMap<FGameplayTag, int64> MaterialTotals;
		for (const FCraftingItemStack& Cost : Node.ActivationCosts)
		{
			if (!Cost.ItemTag.IsValid() || Cost.Quantity <= 0)
			{
				OutErrors.Add(FText::Format(NSLOCTEXT("ShipUpgrade", "InvalidActivationCost", "Node {0} contains an invalid activation material or quantity."), FText::FromName(Node.NodeId)));
				continue;
			}
			const int64 Total = MaterialTotals.FindOrAdd(Cost.ItemTag) += Cost.Quantity;
			if (Total > MAX_int32)
			{
				OutErrors.Add(FText::Format(NSLOCTEXT("ShipUpgrade", "ActivationCostOverflow", "Node {0} has an activation material total larger than int32."), FText::FromName(Node.NodeId)));
			}
		}
	}

	TSet<FName> Visiting;
	TSet<FName> Visited;
	TFunction<bool(FName)> HasCycle = [&](FName NodeId)
	{
		if (Visiting.Contains(NodeId)) return true;
		if (Visited.Contains(NodeId)) return false;
		Visiting.Add(NodeId);
		if (const FShipUpgradeNodeDefinition* Node = FindNode(NodeId))
		{
			for (FName Prerequisite : Node->PrerequisiteNodeIds)
			{
				if (HasCycle(Prerequisite)) return true;
			}
		}
		Visiting.Remove(NodeId);
		Visited.Add(NodeId);
		return false;
	};

	for (FName NodeId : NodeIds)
	{
		if (HasCycle(NodeId))
		{
			OutErrors.Add(NSLOCTEXT("ShipUpgrade", "Cycle", "강화 노드 그래프에 순환 참조가 있습니다."));
			break;
		}
	}
	return OutErrors.IsEmpty();
}

#if WITH_EDITOR
EDataValidationResult UShipUpgradeTreeDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = CombineDataValidationResults(Super::IsDataValid(Context), EDataValidationResult::Valid);
	TArray<FText> Errors;
	if (!ValidateTree(Errors))
	{
		for (const FText& Error : Errors)
		{
			Context.AddError(Error);
		}
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif
