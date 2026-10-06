#include "SWSublevelAuditCommandlet.h"

#include "FileHelpers.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/Blueprint.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/LevelScriptActor.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
FString Cell(FString Value)
{
	Value.ReplaceInline(TEXT("\t"), TEXT(" "));
	Value.ReplaceInline(TEXT("\r"), TEXT(" "));
	Value.ReplaceInline(TEXT("\n"), TEXT(" "));
	return Value;
}

void DumpReferences(UObject* Object, const FString& Scope, FString& Output)
{
	TArray<UObject*> References;
	FReferenceFinder Finder(References, nullptr, false, true, false, true);
	Finder.FindReferences(Object);
	for (UObject* Reference : References)
	{
		AActor* Target = Cast<AActor>(Reference);
		if (!Target) Target = Reference ? Reference->GetTypedOuter<AActor>() : nullptr;
		if (!Target || Target == Object || Target == Object->GetTypedOuter<AActor>()) continue;
		Output += FString::Printf(TEXT("%s\t%s\t%s\t%s\n"), *Cell(Scope), *Cell(Object->GetPathName()),
			*Cell(Target->GetPathName()), *Cell(Reference->GetPathName()));
	}
}

void DumpProperties(UObject* Object, FString& Output)
{
	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		const FProperty* Property = *It;
		if (Property->HasAnyPropertyFlags(CPF_Transient)) continue;
		const FString OwnerType = Property->GetOwnerStruct()->GetPathName();
		const bool bRelevantOwner = OwnerType.StartsWith(TEXT("/Game/"))
			|| OwnerType.StartsWith(TEXT("/Script/ClassFeature.")) || OwnerType.StartsWith(TEXT("/Script/WaterAndShip."))
			|| OwnerType.StartsWith(TEXT("/Script/Enemy.")) || OwnerType.StartsWith(TEXT("/Script/ArtisticSWCore."))
			|| OwnerType.StartsWith(TEXT("/Script/Story.")) || OwnerType.StartsWith(TEXT("/Script/NPCDialogue."))
			|| OwnerType.StartsWith(TEXT("/Script/BlueprintGraph."));
		const FName Name = Property->GetFName();
		if (!bRelevantOwner && Name != TEXT("AttachParent") && Name != TEXT("ChildActorClass")
			&& Name != TEXT("ChildActorTemplate") && Name != TEXT("bReplicates")
			&& Name != TEXT("bNetLoadOnClient") && Name != TEXT("Mobility") && Name != TEXT("Owner")) continue;
		FString Value;
		Property->ExportText_InContainer(0, Value, Object, nullptr, Object, PPF_None);
		Output += FString::Printf(TEXT("%s\t%s\t%s\t%s\n"), *Cell(Object->GetPathName()),
			*Cell(Property->GetOwnerStruct()->GetPathName()), *Cell(Property->GetName()), *Cell(Value.Left(12000)));
	}
}

void DumpBlueprint(UBlueprint* Blueprint, FString& Nodes, FString& References, FString& Properties)
{
	if (!Blueprint) return;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			const FString Function = Call ? Call->FunctionReference.GetMemberName().ToString() : FString();
			Nodes += FString::Printf(TEXT("%s\t%s\t%s\t%s\t%s\t%s\n"), *Cell(Blueprint->GetPathName()),
				*Cell(Graph->GetName()), *Cell(Node->GetName()), *Cell(Node->GetClass()->GetName()),
				*Cell(Function), *Cell(Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString()));
			DumpReferences(Node, TEXT("GraphNode"), References);
			DumpProperties(Node, Properties);
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin) continue;
				FString Links;
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
					if (Linked) Links += Linked->GetOwningNode()->GetName() + TEXT(".") + Linked->PinName.ToString() + TEXT(";");
				Nodes += FString::Printf(TEXT("%s\t%s\t%s.%s\tPin\t%s\t%s | Object=%s | Links=%s\n"),
					*Cell(Blueprint->GetPathName()), *Cell(Graph->GetName()), *Cell(Node->GetName()), *Cell(Pin->PinName.ToString()),
					*Cell(Pin->PinType.PinCategory.ToString()), *Cell(Pin->DefaultValue),
					*Cell(Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : FString()), *Cell(Links));
			}
		}
	}
}
}

USWSublevelAuditCommandlet::USWSublevelAuditCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 USWSublevelAuditCommandlet::Main(const FString& Params)
{
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Diagnostics/SublevelAudit_20261005"));
	if (!IFileManager::Get().MakeDirectory(*Directory, true)) return 1;
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Level/Lvl_CY"));
	if (!World || World->HasBegunPlay()) return 2;
	FString Actors = TEXT("Path\tLabel\tClass\tLevel\tAttachParent\tTags\tLocation\tComponents\n");
	FString References = TEXT("Scope\tSource\tTargetActor\tTargetObject\n");
	FString Properties = TEXT("Object\tDeclaringType\tProperty\tValue\n");
	FString Nodes = TEXT("Blueprint\tGraph\tNode\tType\tFunctionOrCategory\tTitleOrValue\n");
	FString Meta = FString::Printf(TEXT("World=%s\nWorldType=%d\nHasBegunPlay=%d\nWorldPartition=%d\nLevels=%d\nStreamingLevels=%d\n"),
		*World->GetPathName(), static_cast<int32>(World->WorldType), World->HasBegunPlay(),
		World->PersistentLevel->GetWorldPartition() != nullptr, World->GetLevels().Num(), World->GetStreamingLevels().Num());
	for (ULevelStreaming* Streaming : World->GetStreamingLevels())
		if (Streaming) Meta += FString::Printf(TEXT("Streaming=%s Class=%s Loaded=%d\n"),
			*Streaming->GetWorldAssetPackageName(), *Streaming->GetClass()->GetName(), Streaming->GetLoadedLevel() != nullptr);
	TSet<UBlueprint*> Blueprints;
	int32 ActorCount = 0;
	for (ULevel* Level : World->GetLevels())
	{
		if (!Level) continue;
		Meta += FString::Printf(TEXT("Level=%s Actors=%d\n"), *Level->GetPathName(), Level->Actors.Num());
		UBlueprint* Script = Level->GetLevelScriptBlueprint(true);
		Meta += FString::Printf(TEXT("LevelScriptBlueprint=%s LevelScriptActor=%s\n"),
			*GetPathNameSafe(Script), *GetPathNameSafe(Level->GetLevelScriptActor()));
		if (Script) Blueprints.Add(Script);
		for (AActor* Actor : Level->Actors)
		{
			if (!IsValid(Actor)) continue;
			++ActorCount;
			FString Tags;
			for (FName Tag : Actor->Tags) Tags += Tag.ToString() + TEXT(";");
			TArray<UActorComponent*> Components;
			Actor->GetComponents(Components);
			FString ComponentNames;
			for (UActorComponent* Component : Components)
			{
				ComponentNames += Component->GetName() + TEXT(":") + Component->GetClass()->GetName() + TEXT(";");
				DumpReferences(Component, TEXT("Component"), References);
				DumpProperties(Component, Properties);
			}
			Actors += FString::Printf(TEXT("%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n"),
				*Cell(Actor->GetPathName()), *Cell(Actor->GetActorLabel()), *Cell(Actor->GetClass()->GetPathName()),
				*Cell(Level->GetPathName()), *Cell(GetPathNameSafe(Actor->GetAttachParentActor())),
				*Cell(Tags), *Actor->GetActorLocation().ToCompactString(), *Cell(ComponentNames));
			DumpReferences(Actor, TEXT("Actor"), References);
			DumpProperties(Actor, Properties);
			for (UClass* Class = Actor->GetClass(); Class; Class = Class->GetSuperClass())
				if (UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy)) Blueprints.Add(Blueprint);
		}
	}
	for (UBlueprint* Blueprint : Blueprints) DumpBlueprint(Blueprint, Nodes, References, Properties);
	Meta += FString::Printf(TEXT("ValidActors=%d\nBlueprints=%d\nWorldDirty=%d\n"),
		ActorCount, Blueprints.Num(), World->GetOutermost()->IsDirty());
	const bool bWritten = FFileHelper::SaveStringToFile(Meta, *FPaths::Combine(Directory, TEXT("Metadata.txt")))
		&& FFileHelper::SaveStringToFile(Actors, *FPaths::Combine(Directory, TEXT("Actors.tsv")))
		&& FFileHelper::SaveStringToFile(References, *FPaths::Combine(Directory, TEXT("References.tsv")))
		&& FFileHelper::SaveStringToFile(Properties, *FPaths::Combine(Directory, TEXT("Properties.tsv")))
		&& FFileHelper::SaveStringToFile(Nodes, *FPaths::Combine(Directory, TEXT("BlueprintNodes.tsv")));
	UE_LOG(LogTemp, Display, TEXT("SWSublevelAudit Actors=%d Blueprints=%d BegunPlay=%d Output=%s SavedAssets=0"),
		ActorCount, Blueprints.Num(), World->HasBegunPlay(), *Directory);
	return bWritten ? 0 : 3;
}
