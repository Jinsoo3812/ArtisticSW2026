#include "SWAssetRepairLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "EdGraph/EdGraph.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

FString USWAssetRepairLibrary::DescribeBlueprintState(UBlueprint* Blueprint)
{
    if (!Blueprint || !Blueprint->GeneratedClass) return TEXT("{}");
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("parent"), GetPathNameSafe(Blueprint->ParentClass));
    TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
    TArray<TSharedPtr<FJsonValue>> Nodes;
    for (const UEdGraph* Graph : Graphs) for (const UEdGraphNode* Node : Graph->Nodes)
        if (Node) Nodes.Add(MakeShared<FJsonValueString>(Graph->GetName() + TEXT("|") + Node->GetName() + TEXT("|") + Node->GetClass()->GetPathName() + TEXT("|") + Node->NodeGuid.ToString()));
    Result->SetArrayField(TEXT("nodes"), Nodes);
    auto Defaults = MakeShared<FJsonObject>();
    UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
    for (TFieldIterator<FProperty> It(Blueprint->GeneratedClass); It; ++It)
    {
        if (!It->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible) || It->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) continue;
        FString Value; It->ExportText_InContainer(0, Value, CDO, nullptr, CDO, PPF_None);
        Defaults->SetStringField(It->GetName(), Value);
    }
    Result->SetObjectField(TEXT("defaults"), Defaults);
    FString Text; FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Text)); return Text;
}

bool USWAssetRepairLibrary::CreateHistoricalRedirector(const FString& PackageName, UObject* Destination)
{
    if (!GIsEditor || (GEditor && GEditor->PlayWorld) || !Destination
        || !PackageName.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackageName)
        || PackageName == Destination->GetOutermost()->GetName() || FPackageName::DoesPackageExist(PackageName)) return false;
    UBlueprint* Blueprint = Cast<UBlueprint>(Destination);
    if (Blueprint && (!Blueprint->GeneratedClass || Blueprint->Status == BS_Error)) return false;
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    UPackage* Package = CreatePackage(*PackageName);
    if (FindObject<UObject>(Package, *AssetName)) return false;
    auto Alias = [&](const FString& Name, UObject* Target, bool bAsset)
    {
        auto* Redirector = NewObject<UObjectRedirector>(Package, FName(*Name), RF_Public | (bAsset ? RF_Standalone : RF_NoFlags));
        Redirector->DestinationObject = Target;
        if (bAsset) FAssetRegistryModule::AssetCreated(Redirector);
        return Redirector;
    };
    UObjectRedirector* Redirector = Alias(AssetName, Destination, true);
    if (Blueprint)
    {
        // Historical maps and class soft references also address the generated class/CDO.
        Alias(AssetName + TEXT("_C"), Blueprint->GeneratedClass, false);
        Alias(TEXT("Default__") + AssetName + TEXT("_C"), Blueprint->GeneratedClass->GetDefaultObject(), false);
    }
    Package->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    return UPackage::SavePackage(Package, Redirector, *Filename, Args);
}
