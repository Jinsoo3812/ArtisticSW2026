#include "SWEnemyShipISMMaterialsCommandlet.h"
#include "Rendering/SWEnemyShipISMMaterialPath.h"
#include "AssetToolsModule.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "FileHelpers.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"

USWEnemyShipISMMaterialsCommandlet::USWEnemyShipISMMaterialsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 USWEnemyShipISMMaterialsCommandlet::Main(const FString& Params)
{
	IAssetTools& Assets = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	TMap<UMaterialInterface*, UMaterialInterface*> Copies;
	TSet<UMaterialInterface*> Visiting;
	TArray<UPackage*> Packages;
	TFunction<UMaterialInterface*(UMaterialInterface*)> Copy;
	Copy = [&](UMaterialInterface* Source) -> UMaterialInterface*
	{
		if (!Source) return nullptr;
		if (UMaterialInterface** Existing = Copies.Find(Source)) return *Existing;
		if (Visiting.Contains(Source)) return nullptr;
		Visiting.Add(Source);
		UMaterialInterface* ParentCopy = nullptr;
		if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Source))
		{
			ParentCopy = Copy(Instance->Parent);
			if (!ParentCopy) return nullptr;
		}
		else if (!Source->IsA<UMaterial>()) return nullptr;
		const FString Path = SWEnemyShipISMMaterials::ObjectPath(Source->GetPathName());
		UMaterialInterface* Target = LoadObject<UMaterialInterface>(nullptr, *Path);
		if (!Target) Target = Cast<UMaterialInterface>(Assets.DuplicateAsset(
			SWEnemyShipISMMaterials::AssetName(Source->GetPathName()), SWEnemyShipISMMaterials::Folder(), Source));
		if (!Target || Target->GetClass() != Source->GetClass()) return nullptr;
		Target->Modify();
		if (UMaterial* Material = Cast<UMaterial>(Target))
		{
			bool bNeedsRecompile = false;
			if (!Material->SetMaterialUsage(bNeedsRecompile, MATUSAGE_InstancedStaticMeshes)) return nullptr;
			Material->PostEditChange();
		}
		else CastChecked<UMaterialInstanceConstant>(Target)->SetParentEditorOnly(ParentCopy);
		Target->MarkPackageDirty();
		Packages.AddUnique(Target->GetOutermost());
		Copies.Add(Source, Target);
		Visiting.Remove(Source);
		UE_LOG(LogTemp, Display, TEXT("[EnemyShipISMMaterials] Source=%s Variant=%s"), *Source->GetPathName(), *Target->GetPathName());
		return Target;
	};
	for (const TCHAR* Path : {
		TEXT("/Game/Ship_Mesh/Costum_JoseonShip_fin/StaticMeshes/SM_Ship_Visual.SM_Ship_Visual"),
		TEXT("/Game/Blueprints/Ship/Cannon/Mesh/SM_Cannon.SM_Cannon")})
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
		if (!Mesh) return 1;
		for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
			if (!Copy(Slot.MaterialInterface)) return 2;
	}
	if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
	for (const auto& Pair : Copies)
		if (!Pair.Value->GetMaterial()->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes)) return 3;
	if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, true)) return 4;
	UE_LOG(LogTemp, Display, TEXT("[EnemyShipISMMaterials] Result=Saved Packages=%d"), Packages.Num());
	return 0;
}
