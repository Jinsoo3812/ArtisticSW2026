#include "SWWeatherAssetSetupCommandlet.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "K2Node_Event.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "SWNetworkWeatherActor.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetData.h"
#include "ActorFactories/ActorFactoryBlueprint.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/TimelineTemplate.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Timeline.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Curves/CurveFloat.h"

namespace
{
	constexpr const TCHAR* CopyPath = TEXT("/Game/StylizedWeather/Blueprint/BP_StylizedWeather_Network.BP_StylizedWeather_Network");
	constexpr const TCHAR* MapPath = TEXT("/Game/Level/Lvl_CY");

	bool CompileWeather(UBlueprint* Blueprint)
	{
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Results);
		UE_LOG(LogTemp, Display, TEXT("SWWeather: compile %s errors=%d warnings=%d"), *Blueprint->GetName(), Results.NumErrors, Results.NumWarnings);
		return Results.NumErrors == 0 && Blueprint->Status != BS_Error && Blueprint->GeneratedClass->IsChildOf(ASWNetworkWeatherActor::StaticClass());
	}

	bool PatchWeather(UBlueprint* Blueprint)
	{
		Blueprint->Modify();
		Blueprint->ParentClass = ASWNetworkWeatherActor::StaticClass();
		FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		int32 RandomNodes = 0;
		int32 BeginPlayNodes = 0;
		for (UEdGraph* Graph : Graphs)
		{
			const TArray<TObjectPtr<UEdGraphNode>> Nodes = Graph->Nodes;
			for (UEdGraphNode* Node : Nodes)
			{
				if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
				{
					if (Event->EventReference.GetMemberName() == TEXT("ReceiveBeginPlay"))
					{
						if (UEdGraphPin* Pin = Event->FindPin(UEdGraphSchema_K2::PN_Then)) Pin->BreakAllPinLinks();
						++BeginPlayNodes;
					}
					if (Event->EventReference.GetMemberName() == TEXT("ReceiveTick"))
						if (UEdGraphPin* Pin = Event->FindPin(UEdGraphSchema_K2::PN_Then)) Pin->BreakAllPinLinks();
				}
				if (UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node))
				{
					if (Event->CustomFunctionName == TEXT("PlayTimeLine NewTime") || Event->CustomFunctionName == TEXT("ChangeWindDir") || Event->CustomFunctionName == TEXT("SetSunVisible"))
						if (UEdGraphPin* Pin = Event->FindPin(UEdGraphSchema_K2::PN_Then)) Pin->BreakAllPinLinks();
				}
				if (UK2Node_Timeline* Timeline = Cast<UK2Node_Timeline>(Node))
					for (UEdGraphPin* Pin : Timeline->Pins) if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) Pin->BreakAllPinLinks();
				if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
				{
					if (Graph->GetFName() == TEXT("SetUp")) Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
					if (Graph->GetFName() == TEXT("Time Skip"))
					{
						Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
						UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
						Graph->AddNode(Call, false, false);
						Call->CreateNewGuid();
						Call->SetFromFunction(ASWNetworkWeatherActor::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(ASWNetworkWeatherActor, SetNetworkWeatherTime)));
						Call->AllocateDefaultPins();
						const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
						if (!Schema->TryCreateConnection(Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then), Call->FindPinChecked(UEdGraphSchema_K2::PN_Execute))) { UE_LOG(LogTemp, Error, TEXT("SWWeather: Time Skip exec connection rejected")); return false; }
						for (FName Name : { FName(TEXT("Hour")), FName(TEXT("Minute")), FName(TEXT("Weather")) })
							if (!Schema->TryCreateConnection(Entry->FindPinChecked(Name), Call->FindPinChecked(Name))) { UE_LOG(LogTemp, Error, TEXT("SWWeather: Time Skip %s connection rejected: %s"), *Name.ToString(), *Schema->CanCreateConnection(Entry->FindPinChecked(Name), Call->FindPinChecked(Name)).Message.ToString()); return false; }
					}
				}
				if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
				{
					const FName Name = Call->FunctionReference.GetMemberName();
					FName Replacement;
					if (Name == TEXT("RandomFloatInRange")) { Replacement = GET_FUNCTION_NAME_CHECKED(ASWNetworkWeatherActor, ServerWeatherRandomFloat); ++RandomNodes; }
					if (Name == TEXT("RandomIntegerInRange")) { Replacement = GET_FUNCTION_NAME_CHECKED(ASWNetworkWeatherActor, ServerWeatherRandomInteger); ++RandomNodes; }
					if (Name == TEXT("SetSunVisible")) Replacement = GET_FUNCTION_NAME_CHECKED(ASWNetworkWeatherActor, SetNetworkSunVisible);
					if (!Replacement.IsNone())
					{
						Call->SetFromFunction(ASWNetworkWeatherActor::StaticClass()->FindFunctionByName(Replacement));
						Call->ReconstructNode();
						if (UEdGraphPin* Self = Call->FindPin(UEdGraphSchema_K2::PN_Self)) { Self->DefaultObject = nullptr; Self->DefaultValue.Reset(); }
					}
				}
			}
		}
		for (UTimelineTemplate* Timeline : Blueprint->Timelines) Timeline->bAutoPlay = false;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		UE_LOG(LogTemp, Display, TEXT("SWWeather: patched %d random nodes; %d BeginPlay nodes"), RandomNodes, BeginPlayNodes);
		return RandomNodes == 7 && BeginPlayNodes == 1;
	}

	bool VerifyWeather(UBlueprint* Blueprint, UWorld* World)
	{
		if (!Blueprint || !World || !Blueprint->GeneratedClass->IsChildOf(ASWNetworkWeatherActor::StaticClass())) return false;
		int32 NetworkActors = 0;
		int32 LegacyActors = 0;
		int32 Effects = 0;
		for (AActor* Actor : World->PersistentLevel->Actors)
		{
			if (!Actor) continue;
			if (Actor->GetClass() == Blueprint->GeneratedClass)
			{
				++NetworkActors;
				if (!Actor->GetIsReplicated() || !Actor->bAlwaysRelevant) return false;
				UE_LOG(LogTemp, Display, TEXT("SWWeather: map actor=%s class=%s transform=%s guid=%s"), *Actor->GetName(), *Actor->GetClass()->GetPathName(), *Actor->GetActorTransform().ToString(), *Actor->GetActorGuid().ToString());
			}
			if (Actor->GetClass()->GetName() == TEXT("BP_StylizedWeather_C")) ++LegacyActors;
			if (Actor->GetClass()->GetName() == TEXT("BP_WeatherEvent_Effect_C")) ++Effects;
		}
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
					if (Call->FunctionReference.GetMemberName() == TEXT("RandomFloatInRange") || Call->FunctionReference.GetMemberName() == TEXT("RandomIntegerInRange")) return false;
				if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
					if (Event->EventReference.GetMemberName() == TEXT("ReceiveBeginPlay") || Event->EventReference.GetMemberName() == TEXT("ReceiveTick"))
						if (UEdGraphPin* Pin = Event->FindPin(UEdGraphSchema_K2::PN_Then); Pin && !Pin->LinkedTo.IsEmpty()) return false;
			}
		for (UTimelineTemplate* Timeline : Blueprint->Timelines) if (Timeline->bAutoPlay) return false;
		UE_LOG(LogTemp, Display, TEXT("SWWeather: verify Network=%d Legacy=%d Effects=%d"), NetworkActors, LegacyActors, Effects);
		return NetworkActors == 1 && LegacyActors == 0 && Effects == 1;
	}
}

int32 USWWeatherAssetSetupCommandlet::Main(const FString& Params)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, TEXT("/Game/StylizedWeather/Blueprint/BP_StylizedWeather.BP_StylizedWeather"));
	if (!Blueprint || !Blueprint->GeneratedClass) return 1;
	if (Params.Contains(TEXT("VerifyOnly")))
	{
		UBlueprint* Copy = LoadObject<UBlueprint>(nullptr, CopyPath);
		if (!Copy || !CompileWeather(Copy)) return 10;
		return VerifyWeather(Copy, UEditorLoadingAndSavingUtils::LoadMap(MapPath)) ? 0 : 11;
	}
	if (Params.Contains(TEXT("Apply")))
	{
		if (LoadObject<UBlueprint>(nullptr, CopyPath)) { UE_LOG(LogTemp, Error, TEXT("SWWeather: refusing to overwrite existing copy")); return 12; }
		UBlueprint* Copy = Cast<UBlueprint>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().DuplicateAsset(
			TEXT("BP_StylizedWeather_Network"), TEXT("/Game/StylizedWeather/Blueprint"), Blueprint));
		if (!Copy || !PatchWeather(Copy) || !CompileWeather(Copy)) return 13;
		AActor* Defaults = CastChecked<AActor>(Copy->GeneratedClass->GetDefaultObject());
		ASWNetworkWeatherActor* NativeDefaults = CastChecked<ASWNetworkWeatherActor>(Defaults);
		for (UTimelineTemplate* Timeline : Copy->Timelines)
			if (Timeline->GetName() == TEXT("TL_SetSunVisible_Template") && Timeline->FloatTracks.Num() == 1)
				NativeDefaults->SunVisibilityCurve = Timeline->FloatTracks[0].CurveFloat;
		if (!NativeDefaults->SunVisibilityCurve) return 22;
		Defaults->SetReplicates(true);
		Defaults->bAlwaysRelevant = true;
		Defaults->SetReplicateMovement(false);
		Defaults->PrimaryActorTick.bCanEverTick = true;
		Defaults->PrimaryActorTick.bStartWithTickEnabled = true;
		UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(MapPath);
		if (!World) return 14;
		AActor* OriginalActor = nullptr;
		int32 OriginalCount = 0;
		int32 ActorCount = 0;
		for (AActor* Actor : World->PersistentLevel->Actors)
		{
			if (!Actor) continue;
			++ActorCount;
			if (Actor->GetClass() == Blueprint->GeneratedClass) { OriginalActor = Actor; ++OriginalCount; }
		}
		if (OriginalCount != 1 || OriginalActor->GetAttachParentActor() || !OriginalActor->Children.IsEmpty()) return 15;
		const FTransform Transform = OriginalActor->GetActorTransform();
		const FString Label = OriginalActor->GetActorLabel();
		const FGuid Guid = OriginalActor->GetActorGuid();
		const FName Name = OriginalActor->GetFName();
		const FName Folder = OriginalActor->GetFolderPath();
		TArray<FSWWeatherPropertyValue> Configuration;
		for (TFieldIterator<FProperty> It(Blueprint->GeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient | CPF_InstancedReference)) continue;
			if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				UObject* Value = ObjectProperty->GetObjectPropertyValue_InContainer(OriginalActor);
				if (Value && (Value->IsIn(OriginalActor) || Value->GetOutermost() == World->GetOutermost())) continue;
			}
			FSWWeatherPropertyValue& Entry = Configuration.AddDefaulted_GetRef();
			Entry.Name = Property->GetFName();
			Property->ExportTextItem_Direct(Entry.Value, Property->ContainerPtrToValuePtr<void>(OriginalActor), nullptr, OriginalActor, PPF_None);
		}
		TArray<AActor*> Replacements;
		UActorFactoryBlueprint* Factory = NewObject<UActorFactoryBlueprint>();
		UEditorActorSubsystem::ReplaceActors(Factory, FAssetData(Copy), {OriginalActor}, &Replacements, false);
		if (Replacements.Num() != 1) return 16;
		AActor* Replacement = Replacements[0];
		for (const FSWWeatherPropertyValue& Entry : Configuration)
		{
			FProperty* Property = Replacement->GetClass()->FindPropertyByName(Entry.Name);
			if (!Property || !Property->ImportText_Direct(*Entry.Value, Property->ContainerPtrToValuePtr<void>(Replacement), Replacement, PPF_None)) return 17;
		}
		Replacement->SetActorTransform(Transform);
		Replacement->SetActorLabel(Label, false);
		Replacement->SetFolderPath(Folder);
		Replacement->SetReplicates(true);
		Replacement->bAlwaysRelevant = true;
		Replacement->SetReplicateMovement(false);
		Replacement->RerunConstructionScripts();
		if (Replacement->GetFName() != Name || Replacement->GetActorGuid() != Guid || !Replacement->GetActorTransform().Equals(Transform)) return 18;
		int32 AfterCount = 0;
		for (AActor* Actor : World->PersistentLevel->Actors) if (Actor) ++AfterCount;
		if (AfterCount != ActorCount || !VerifyWeather(Copy, World)) return 19;
		if (!UEditorLoadingAndSavingUtils::SavePackages({Copy->GetOutermost()}, true)) return 20;
		if (!UEditorLoadingAndSavingUtils::SaveMap(World, MapPath)) return 21;
		UE_LOG(LogTemp, Display, TEXT("SWWeather: saved copy + Lvl_CY; preserved %d instance properties and %d actors"), Configuration.Num(), ActorCount);
		return 0;
	}
	FString Report;
	for (UTimelineTemplate* Timeline : Blueprint->Timelines)
	{
		Report += FString::Printf(TEXT("TIMELINE %s length=%f autoplay=%d loop=%d\n"), *Timeline->GetName(), Timeline->TimelineLength, Timeline->bAutoPlay, Timeline->bLoop);
		for (const FTTFloatTrack& Track : Timeline->FloatTracks)
			if (Track.CurveFloat)
				for (const FRichCurveKey& Key : Track.CurveFloat->FloatCurve.GetConstRefOfKeys())
					Report += FString::Printf(TEXT("  KEY %f %f interp=%d\n"), Key.Time, Key.Value, static_cast<int32>(Key.InterpMode));
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		Report += FString::Printf(TEXT("\nGRAPH %s\n"), *Graph->GetPathName());
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			FString Member;
			if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node)) Member = Call->FunctionReference.GetMemberName().ToString();
			if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node)) Member = Variable->VariableReference.GetMemberName().ToString();
			if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node)) Member = Event->EventReference.GetMemberName().ToString();
			Report += FString::Printf(TEXT("NODE %s %s %s\n"), *Node->GetName(), *Node->GetClass()->GetName(), *Member);
			for (UEdGraphPin* Pin : Node->Pins)
			{
				Report += FString::Printf(TEXT("  %s %s %s default=%s object=%s ->"), Pin->Direction == EGPD_Input ? TEXT("IN") : TEXT("OUT"), *Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString(), *Pin->DefaultValue, *GetNameSafe(Pin->DefaultObject));
				for (UEdGraphPin* Link : Pin->LinkedTo) Report += FString::Printf(TEXT(" %s.%s"), *Link->GetOwningNode()->GetName(), *Link->PinName.ToString());
				Report += TEXT("\n");
			}
		}
	}
	for (TFieldIterator<UFunction> It(Blueprint->GeneratedClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		Report += FString::Printf(TEXT("\nFUNCTION %s flags=%x\n"), *It->GetName(), It->FunctionFlags);
		for (TFieldIterator<FProperty> Property(*It); Property && Property->HasAnyPropertyFlags(CPF_Parm); ++Property)
			Report += FString::Printf(TEXT("  %s %s\n"), *Property->GetName(), *Property->GetCPPType());
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("WeatherNetwork");
	IFileManager::Get().MakeDirectory(*Directory, true);
	if (!FFileHelper::SaveStringToFile(Report, *(Directory / TEXT("OriginalGraph.txt")))) return 2;
	UE_LOG(LogTemp, Display, TEXT("SWWeather: read-only graph report written; %d graphs"), Graphs.Num());
	return 0;
}
