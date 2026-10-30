#include "SWAudioAuthoringLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "EdGraph/EdGraphNode.h"
#include "Factories/SoundCueFactoryNew.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionLandscapeGrassOutput.h"
#include "Materials/MaterialExpressionLandscapeLayerSample.h"
#include "Materials/MaterialExpressionLandscapePhysicalMaterialOutput.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "HAL/PlatformProcess.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "Misc/PackageName.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeMixer.h"
#include "Sound/SoundNodeModulator.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundWave.h"
#include "WaterBodyActor.h"
#include "WaterSurfaceQueryLibrary.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "AudioDeviceHandle.h"
#include "Engine/Engine.h"
#include "UObject/UnrealType.h"
#include "Cannon.h"
#include "TimerManager.h"
#include "Ship.h"
#include "Repair/ShipRepairPointComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "SWCabinWaterCullData.h"

FVector USWAudioAuthoringLibrary::FindAudioProbeCabinPoint(AActor* Ship)
{
	const auto* Data = LoadObject<USWCabinWaterCullData>(nullptr, TEXT("/Game/Blueprints/Water/Culling/DA_SW_ShipCabinWaterCull"));
	if (!Ship || !Data) { return FVector::ZeroVector; }
	const int32 I = Data->OccupancyVoxels.FindLast(255);
	const FIntVector R = Data->Resolution;
	if (I == INDEX_NONE || R.X <= 0 || R.Y <= 0 || R.Z <= 0) { return FVector::ZeroVector; }
	const FVector Cell(I % R.X + 0.5, (I / R.X) % R.Y + 0.5, I / (R.X * R.Y) + 0.5);
	const FVector Local = Data->LocalBoundsMin + Cell / FVector(R) * (Data->LocalBoundsMax - Data->LocalBoundsMin);
	return Ship->GetActorTransform().TransformPosition(Local);
}

bool USWAudioAuthoringLibrary::QueueAudioProbeCharacter(UWorld* World, TSubclassOf<ACharacter> CharacterClass, FVector Location)
{
	if (!World || World->WorldType != EWorldType::PIE || World->GetNetMode() == NM_Client || !CharacterClass) { return false; }
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, [World, CharacterClass, Location]
	{
		FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (auto* Character = World->SpawnActor<ACharacter>(CharacterClass, Location, FRotator::ZeroRotator, Params))
		{
			Character->GetCharacterMovement()->DisableMovement();
			FTimerHandle Release;
			World->GetTimerManager().SetTimer(Release, FTimerDelegate::CreateWeakLambda(Character, [Character]
			{
				Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
			}), 3.f, false);
			UE_LOG(LogTemp, Display, TEXT("SW_AUDIO_NPC_PROBE_SPAWN World=%s Actor=%s"), *World->GetPathName(), *Character->GetName());
		}
	}), 0.1f, false);
	return true;
}

bool USWAudioAuthoringLibrary::QueueAudioProbeLeaks(AActor* Actor, int32 ActiveCount)
{
	auto* Ship = Cast<AShip>(Actor);
	if (!Ship || !Ship->HasAuthority() || !Ship->GetWorld() || Ship->GetWorld()->WorldType != EWorldType::PIE) { return false; }
	FTimerHandle Handle;
	Ship->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Ship, [Ship, ActiveCount]
	{
		TInlineComponentArray<UShipRepairPointComponent*> Points(Ship);
		for (int32 I = 0; I < Points.Num(); ++I)
			if (I < FMath::Clamp(ActiveCount, 0, 8)) { Points[I]->ActivateLeak(); } else { Points[I]->DeactivateLeak(); }
	}), 0.1f, false);
	return true;
}

bool USWAudioAuthoringLibrary::QueueAudioCannonShot(AActor* Actor, bool bAI)
{
	auto* Cannon = Cast<ACannon>(Actor);
	UWorld* World = Cannon ? Cannon->GetWorld() : nullptr;
	if (!Cannon || !Cannon->HasAuthority() || !World || !World->IsGameWorld() || World->bIsTearingDown) { return false; }
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Cannon, [Cannon, bAI]
	{
		FVector Direction = Cannon->GetActorForwardVector(); Direction.Z = -0.15f;
		const bool First = bAI ? Cannon->FireAICannonAtDirection(Direction) : Cannon->FireCannon();
		const bool Second = bAI ? Cannon->FireAICannonAtDirection(Direction) : Cannon->FireCannon();
		UE_LOG(LogTemp, Display, TEXT("SW_NATIVE_AUDIO_SHOT World=%s Cannon=%s Accepted=%d CooldownAttempt=%d"),
			*Cannon->GetWorld()->GetPathName(), *Cannon->GetName(), First, Second);
	}), 0.1f, false);
	return true;
}

USoundCue* USWAudioAuthoringLibrary::CreateWaterImpactCue(const FString& Path, const TArray<USoundWave*>& Attacks, USoundWave* Body, float Pitch)
{
	if (!Body || !FMath::IsFinite(Pitch) || Pitch < 0.7f || Pitch > 1.1f) { return nullptr; }
	USoundCue* Cue = CreateRandomCue(Path, Attacks);
	if (!Cue) { return nullptr; }
	if (auto* Existing = Cast<USoundNodeMixer>(Cue->FirstNode))
	{
		if (Existing->ChildNodes.Num() != 2) { return nullptr; }
		auto* Modulator = Cast<USoundNodeModulator>(Existing->ChildNodes[0]);
		auto* Player = Cast<USoundNodeWavePlayer>(Existing->ChildNodes[1]);
		auto* Random = Modulator && Modulator->ChildNodes.Num() == 1 ? Cast<USoundNodeRandom>(Modulator->ChildNodes[0]) : nullptr;
		if (!Random || !Player || Random->ChildNodes.Num() != Attacks.Num()) { return nullptr; }
		for (int32 I = 0; I < Attacks.Num(); ++I)
		{
			auto* AttackPlayer = Cast<USoundNodeWavePlayer>(Random->ChildNodes[I]);
			if (!AttackPlayer) { return nullptr; }
			AttackPlayer->SetSoundWave(Attacks[I]);
		}
		Modulator->PitchMin = Pitch; Modulator->PitchMax = Pitch + 0.035f;
		Player->SetSoundWave(Body); Existing->InputVolume = {0.8f, 0.5f};
		Cue->CacheAggregateValues(); Cue->MarkPackageDirty(); return Cue;
	}
	auto* Attack = Cue->ConstructSoundNode<USoundNodeModulator>(USoundNodeModulator::StaticClass(), false);
	Attack->PitchMin = Pitch; Attack->PitchMax = Pitch + 0.035f;
	Attack->VolumeMin = 1.f; Attack->VolumeMax = 1.f;
	TArray<USoundNode*> AttackChildren{Cue->FirstNode}; Attack->SetChildNodes(AttackChildren);
	auto* BodyPlayer = Cue->ConstructSoundNode<USoundNodeWavePlayer>(USoundNodeWavePlayer::StaticClass(), false);
	BodyPlayer->SetSoundWave(Body); BodyPlayer->bLooping = false;
	auto* Mixer = Cue->ConstructSoundNode<USoundNodeMixer>(USoundNodeMixer::StaticClass(), false);
	TArray<USoundNode*> Children{Attack, BodyPlayer}; Mixer->SetChildNodes(Children);
	Mixer->InputVolume = {0.8f, 0.5f};
	Attack->GetGraphNode()->ReconstructNode(); Mixer->GetGraphNode()->ReconstructNode();
	Cue->FirstNode = Mixer; Cue->LinkGraphNodesFromSoundNodes(); Cue->CacheAggregateValues(); Cue->MarkPackageDirty();
	return Cue;
}

USoundCue* USWAudioAuthoringLibrary::CreateShipLoopCue(const FString& Path, USoundWave* Wake, USoundWave* Sea)
{
	USoundCue* Cue = CreateRandomCue(Path, {Wake, Sea});
	if (!Cue || Cast<USoundNodeMixer>(Cue->FirstNode)) { return Cue; }
	auto* Random = Cast<USoundNodeRandom>(Cue->FirstNode);
	if (!Random) { return nullptr; }
	TArray<USoundNode*> Children;
	for (USoundNode* Node : Random->ChildNodes)
	{
		if (auto* Player = Cast<USoundNodeWavePlayer>(Node)) { Player->bLooping = true; }
		Children.Add(Node);
	}
	auto* Mixer = Cue->ConstructSoundNode<USoundNodeMixer>(USoundNodeMixer::StaticClass(), false);
	Mixer->SetChildNodes(Children); Mixer->InputVolume = {0.25f, 0.75f};
	Mixer->GetGraphNode()->ReconstructNode(); Cue->FirstNode = Mixer;
	Cue->LinkGraphNodesFromSoundNodes(); Cue->CacheAggregateValues(); Cue->MarkPackageDirty(); return Cue;
}

FString USWAudioAuthoringLibrary::ConfigureAudioPIE(int32 Clients)
{
	auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	auto Json = MakeShared<FJsonObject>();
	EPlayNetMode Mode; bool Single; int32 Count;
	Settings->GetPlayNetMode(Mode); Settings->GetRunUnderOneProcess(Single); Settings->GetPlayNumberOfClients(Count);
	Json->SetNumberField(TEXT("Mode"), Mode); Json->SetBoolField(TEXT("Single"), Single); Json->SetNumberField(TEXT("Clients"), Count);
	for (FName Name : {TEXT("EnableGameSound"), TEXT("SoloAudioInFirstPIEClient"), TEXT("CreateAudioDeviceForEveryPlayer"), TEXT("bLaunchSeparateServer")})
	{
		if (auto* Property = FindFProperty<FBoolProperty>(Settings->GetClass(), Name))
		{
			Json->SetBoolField(Name.ToString(), Property->GetPropertyValue_InContainer(Settings));
			Property->SetPropertyValue_InContainer(Settings, Name == TEXT("EnableGameSound") || Name == TEXT("CreateAudioDeviceForEveryPlayer"));
		}
	}
	for (FName Name : {TEXT("ClientWindowWidth"), TEXT("ClientWindowHeight")})
	{
		if (auto* Property = FindFProperty<FIntProperty>(Settings->GetClass(), Name))
		{
			Json->SetNumberField(Name.ToString(), Property->GetPropertyValue_InContainer(Settings));
			Property->SetPropertyValue_InContainer(Settings, Name == TEXT("ClientWindowWidth") ? 960 : 540);
		}
	}
	Settings->SetPlayNumberOfClients(FMath::Clamp(Clients, 1, 2));
	Settings->SetPlayNetMode(Clients > 1 ? PIE_ListenServer : PIE_Standalone); Settings->SetRunUnderOneProcess(true);
	FString Result; FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Result)); return Result;
}

void USWAudioAuthoringLibrary::RestoreAudioPIE(const FString& Snapshot)
{
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Snapshot), Json) || !Json) { return; }
	auto* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
	Settings->SetPlayNetMode(EPlayNetMode(int32(Json->GetNumberField(TEXT("Mode")))));
	Settings->SetRunUnderOneProcess(Json->GetBoolField(TEXT("Single"))); Settings->SetPlayNumberOfClients(int32(Json->GetNumberField(TEXT("Clients"))));
	for (FName Name : {TEXT("EnableGameSound"), TEXT("SoloAudioInFirstPIEClient"), TEXT("CreateAudioDeviceForEveryPlayer"), TEXT("bLaunchSeparateServer")})
		if (auto* Property = FindFProperty<FBoolProperty>(Settings->GetClass(), Name)) Property->SetPropertyValue_InContainer(Settings, Json->GetBoolField(Name.ToString()));
	for (FName Name : {TEXT("ClientWindowWidth"), TEXT("ClientWindowHeight")})
		if (auto* Property = FindFProperty<FIntProperty>(Settings->GetClass(), Name)) Property->SetPropertyValue_InContainer(Settings, int32(Json->GetNumberField(Name.ToString())));
	// Begin PIE saves this CDO even when our setup helper does not. Restore the
	// user's per-project preferences on disk as well as in the current session.
	Settings->SaveConfig();
}

TArray<UWorld*> USWAudioAuthoringLibrary::GetAudioPIEWorlds()
{
	TArray<UWorld*> Worlds;
	if (GEngine) for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::PIE && Context.World() && Context.World()->GetNetMode() != NM_DedicatedServer) Worlds.Add(Context.World());
	return Worlds;
}

FString USWAudioAuthoringLibrary::DescribeAudioWorld(UWorld* World)
{
	return World ? FString::Printf(TEXT("World=%s NetMode=%d AudioDevice=%u"), *World->GetPathName(), int32(World->GetNetMode()), uint32(World->GetAudioDevice().GetDeviceID())) : TEXT("NoWorld");
}

USoundCue* USWAudioAuthoringLibrary::CreateRandomCue(const FString& PackagePath, const TArray<USoundWave*>& Waves)
{
	if (!FPackageName::IsValidLongPackageName(PackagePath) || Waves.IsEmpty() || Waves.Contains(nullptr)) { return nullptr; }
	if (FPackageName::DoesPackageExist(PackagePath))
	{
		return LoadObject<USoundCue>(nullptr, *(PackagePath + TEXT(".") + FPackageName::GetLongPackageAssetName(PackagePath)));
	}
	UPackage* Package = CreatePackage(*PackagePath);
	auto* Factory = NewObject<USoundCueFactoryNew>();
	auto* Cue = Cast<USoundCue>(Factory->FactoryCreateNew(USoundCue::StaticClass(), Package,
		FName(*FPackageName::GetLongPackageAssetName(PackagePath)), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
	if (!Cue) { return nullptr; }
	auto* Random = Cue->ConstructSoundNode<USoundNodeRandom>(USoundNodeRandom::StaticClass(), false);
	Random->bRandomizeWithoutReplacement = true;
	TArray<USoundNode*> Children;
	for (USoundWave* Wave : Waves)
	{
		auto* Player = Cue->ConstructSoundNode<USoundNodeWavePlayer>(USoundNodeWavePlayer::StaticClass(), false);
		Player->SetSoundWave(Wave);
		Player->bLooping = false;
		Children.Add(Player);
	}
	Random->SetChildNodes(Children);
	Random->GetGraphNode()->ReconstructNode();
	Cue->FirstNode = Random;
	Cue->VolumeMultiplier = 1.f;
	Cue->LinkGraphNodesFromSoundNodes();
	Cue->CacheAggregateValues();
	Cue->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Cue);
	return Cue;
}

FString USWAudioAuthoringLibrary::InspectTerrain(UWorld* World)
{
	auto Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Records;
	if (World)
	{
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			auto Record = MakeShared<FJsonObject>();
			Record->SetStringField(TEXT("actor"), It->GetPathName());
			Record->SetStringField(TEXT("material"), GetPathNameSafe(It->GetLandscapeMaterial()));
			Record->SetStringField(TEXT("bounds"), It->GetComponentsBoundingBox(true).ToString());
			TArray<TSharedPtr<FJsonValue>> Layers;
			if (const ULandscapeInfo* Info = It->GetLandscapeInfo())
			{
				for (const auto& Layer : Info->Layers)
				{
					if (!Layer.LayerInfoObj) { continue; }
					auto Item = MakeShared<FJsonObject>();
					Item->SetStringField(TEXT("name"), Layer.LayerInfoObj->GetLayerName().ToString());
					Item->SetStringField(TEXT("asset"), Layer.LayerInfoObj->GetPathName());
					Item->SetStringField(TEXT("physical_material"), GetPathNameSafe(Layer.LayerInfoObj->GetPhysicalMaterial()));
					Layers.Add(MakeShared<FJsonValueObject>(Item));
				}
			}
			Record->SetArrayField(TEXT("layers"), Layers);
			Records.Add(MakeShared<FJsonValueObject>(Record));
		}
	}
	Root->SetArrayField(TEXT("landscapes"), Records);
	FString Result;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Result));
	return Result;
}

TArray<FVector> USWAudioAuthoringLibrary::BakeShorePoints(UWorld* World, float Spacing)
{
	TArray<FVector> Result;
	if (!World || !FMath::IsFinite(Spacing)) { return Result; }
	Spacing = FMath::Clamp(Spacing, 500.f, 10000.f);
	float SeaZ = 0.f;
	bool bWater = false;
	for (TActorIterator<AWaterBody> It(World); It; ++It)
	{
		if (FWaterSurfaceQueryLibrary::QueryWaterSurface(World, It->GetActorLocation(), SeaZ)) { bWater = true; break; }
	}
	if (!bWater) { return Result; }
	TSet<FIntPoint> Occupied;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		const FBox Bounds = It->GetComponentsBoundingBox(true);
		// A bounded authoring pass; use larger spacing for unusually large worlds.
		const float Step = FMath::Max(Spacing, float(FMath::Max(Bounds.GetSize().X, Bounds.GetSize().Y) / 300.0));
		for (double X = Bounds.Min.X; X < Bounds.Max.X; X += Step)
		{
			for (double Y = Bounds.Min.Y; Y < Bounds.Max.Y; Y += Step)
			{
				const FVector P(X, Y, SeaZ);
				const TOptional<float> H = It->GetHeightAtLocation(P);
				if (!H.IsSet()) { continue; }
				for (const FVector& Offset : { FVector(Step, 0, 0), FVector(0, Step, 0) })
				{
					const TOptional<float> Other = It->GetHeightAtLocation(P + Offset);
					if (!Other.IsSet() || (H.GetValue() - SeaZ) * (Other.GetValue() - SeaZ) > 0.f ||
						FMath::IsNearlyEqual(H.GetValue(), Other.GetValue())) { continue; }
					const FVector Shore = P + Offset * FMath::Clamp((SeaZ - H.GetValue()) / (Other.GetValue() - H.GetValue()), 0.f, 1.f);
					const FIntPoint Cell(FMath::FloorToInt(Shore.X / 500.f), FMath::FloorToInt(Shore.Y / 500.f));
					if (!Occupied.Contains(Cell)) { Result.Add(Shore); Occupied.Add(Cell); }
				}
			}
		}
	}
	return Result;
}

bool USWAudioAuthoringLibrary::RebuildLandscapePhysics(UWorld* World)
{
	if (!World || !FApp::CanEverRender()) { return false; }
	World->SendAllEndOfFrameUpdates();
	FlushRenderingCommands();
	if (GShaderCompilingManager) { GShaderCompilingManager->FinishAllCompilation(); }
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		It->InvalidatePhysicalMaterial();
	}
	const double Deadline = FPlatformTime::Seconds() + 90.0;
	double NextLog = 0.0;
	for (;;)
	{
		// These permutations are requested by the first render task, after the initial
		// shader flush. Apply completed shader maps while running without editor ticks.
		if (GShaderCompilingManager) { GShaderCompilingManager->ProcessAsyncResults(false, false); }
		World->SendAllEndOfFrameUpdates();
		int32 Pending = 0;
		int32 Renderable = 0;
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			It->BuildPhysicalMaterial();
			for (ULandscapeComponent* Component : It->LandscapeComponents)
			{
				Renderable += Component->SceneProxy != nullptr;
			}
			Pending += It->GetOudatedPhysicalMaterialComponentsCount();
		}
		if (Pending == 0) { break; }
		if (FPlatformTime::Seconds() >= NextLog)
		{
			UE_LOG(LogTemp, Display, TEXT("[AudioSurfaceBake] Pending=%d Renderable=%d"), Pending, Renderable);
			NextLog = FPlatformTime::Seconds() + 5.0;
		}
		if (FPlatformTime::Seconds() > Deadline) { return false; }
		FlushRenderingCommands();
		FPlatformProcess::Sleep(0.01f);
	}
	// FinalizePhysicalMaterial already rebuilds Chaos through the engine's
	// immediate-apply path. Recreating the components here would discard the bake.
	return true;
}

bool USWAudioAuthoringLibrary::ConfigureLandscapeSurfaces(UMaterial* Material,
	UPhysicalMaterial* Sand, UPhysicalMaterial* Grass, UPhysicalMaterial* Soil)
{
	if (!Material || !Sand || !Grass || !Soil) { return false; }
	UMaterialExpression* SandWeight = nullptr;
	UMaterialExpression* GrassWeight = nullptr;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (Cast<UMaterialExpressionLandscapePhysicalMaterialOutput>(Expression)) { return true; }
		if (auto* Sample = Cast<UMaterialExpressionLandscapeLayerSample>(Expression); Sample && Sample->ParameterName == TEXT("Sand")) { SandWeight = Sample; }
		if (auto* Output = Cast<UMaterialExpressionLandscapeGrassOutput>(Expression); Output && !Output->GrassTypes.IsEmpty())
		{
			GrassWeight = Output->GrassTypes[0].Input.Expression;
		}
	}
	if (!SandWeight || !GrassWeight) { return false; }
	Material->Modify();
	auto* Maximum = NewObject<UMaterialExpressionMax>(Material, NAME_None, RF_Transactional);
	Maximum->A.Connect(0, SandWeight);
	Maximum->B.Connect(0, GrassWeight);
	auto* SoilWeight = NewObject<UMaterialExpressionOneMinus>(Material, NAME_None, RF_Transactional);
	SoilWeight->Input.Connect(0, Maximum);
	auto* Output = NewObject<UMaterialExpressionLandscapePhysicalMaterialOutput>(Material, NAME_None, RF_Transactional);
	for (const auto& Pair : { TPair<UPhysicalMaterial*, UMaterialExpression*>(Sand, SandWeight),
		TPair<UPhysicalMaterial*, UMaterialExpression*>(Grass, GrassWeight), TPair<UPhysicalMaterial*, UMaterialExpression*>(Soil, SoilWeight) })
	{
		FPhysicalMaterialInput Input;
		Input.PhysicalMaterial = Pair.Key;
		Input.Input.Connect(0, Pair.Value);
		Output->Inputs.Add(Input);
	}
	Maximum->MaterialExpressionEditorX = -500; Maximum->MaterialExpressionEditorY = 1800;
	SoilWeight->MaterialExpressionEditorX = -300; SoilWeight->MaterialExpressionEditorY = 1800;
	Output->MaterialExpressionEditorX = 0; Output->MaterialExpressionEditorY = 1800;
	Output->Desc = TEXT("Audio surfaces: painted sand, actual procedural grass density, remaining soil. Visual outputs unchanged.");
	for (UMaterialExpression* Expression : TArray<UMaterialExpression*>{ Maximum, SoilWeight, Output })
	{
		Expression->Material = Material;
		Material->GetExpressionCollection().AddExpression(Expression);
	}
	Material->PostEditChange();
	Material->MarkPackageDirty();
	return true;
}

FString USWAudioAuthoringLibrary::SampleTerrainSurfaces(UWorld* World, float Spacing)
{
	auto Root = MakeShared<FJsonObject>();
	TMap<int32, int32> Counts;
	TMap<int32, int32> SimpleCounts;
	TMap<int32, int32> PointCounts;
	TArray<TSharedPtr<FJsonValue>> Points;
	if (World && FMath::IsFinite(Spacing))
	{
		Spacing = FMath::Max(2000.f, Spacing);
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			const FBox Bounds = It->GetComponentsBoundingBox(true);
			for (double X = Bounds.Min.X + Spacing * 0.5; X < Bounds.Max.X; X += Spacing)
			{
				for (double Y = Bounds.Min.Y + Spacing * 0.5; Y < Bounds.Max.Y; Y += Spacing)
				{
					const FVector P(X, Y, 0);
					const auto H = It->GetHeightAtLocation(P);
					if (!H.IsSet()) { continue; }
					UPhysicalMaterial* PM = It->GetPhysicalMaterialAtLocation(P);
					const int32 Surface = int32(UPhysicalMaterial::DetermineSurfaceType(PM));
					++Counts.FindOrAdd(Surface);
					const int32 SimpleSurface = int32(UPhysicalMaterial::DetermineSurfaceType(It->GetPhysicalMaterialAtLocation(P, EHeightfieldSource::Simple)));
					++SimpleCounts.FindOrAdd(SimpleSurface);
					float Sea = 0.f;
					FWaterSurfaceQueryLibrary::QueryWaterSurface(World, P, Sea);
					if (H.GetValue() <= Sea + 100.f) { continue; }
					const auto HX = It->GetHeightAtLocation(P + FVector(100, 0, 0));
					const auto HY = It->GetHeightAtLocation(P + FVector(0, 100, 0));
					const bool bWalkable = HX.IsSet() && HY.IsSet() &&
						FMath::Square(HX.GetValue() - H.GetValue()) + FMath::Square(HY.GetValue() - H.GetValue()) < FMath::Square(95.f);
					int32 ReplaceIndex = INDEX_NONE;
					if (PointCounts.FindRef(Surface) >= 8)
					{
						if (bWalkable)
						{
							ReplaceIndex = Points.IndexOfByPredicate([Surface](const TSharedPtr<FJsonValue>& Value)
							{
								const auto Object = Value->AsObject();
								return int32(Object->GetNumberField(TEXT("surface"))) == Surface && !Object->GetBoolField(TEXT("walkable"));
							});
						}
						if (ReplaceIndex == INDEX_NONE) { continue; }
					}
					auto Item = MakeShared<FJsonObject>();
					Item->SetNumberField(TEXT("surface"), Surface);
					Item->SetNumberField(TEXT("simple_surface"), SimpleSurface);
					Item->SetBoolField(TEXT("walkable"), bWalkable);
					Item->SetNumberField(TEXT("x"), X); Item->SetNumberField(TEXT("y"), Y); Item->SetNumberField(TEXT("z"), H.GetValue());
					Item->SetStringField(TEXT("material"), GetPathNameSafe(PM));
					FHitResult Hit;
					FCollisionQueryParams Params(SCENE_QUERY_STAT(SWAudioSurfaceVerification), false);
					Params.bReturnPhysicalMaterial = true;
					if (World->LineTraceSingleByChannel(Hit, FVector(X, Y, H.GetValue() + 150.f), FVector(X, Y, H.GetValue() - 30.f), ECC_Visibility, Params))
					{
						Item->SetNumberField(TEXT("trace_surface"), int32(UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get())));
						Item->SetStringField(TEXT("trace_actor"), GetPathNameSafe(Hit.GetActor()));
					}
					if (ReplaceIndex != INDEX_NONE) { Points[ReplaceIndex] = MakeShared<FJsonValueObject>(Item); }
					else { Points.Add(MakeShared<FJsonValueObject>(Item)); ++PointCounts.FindOrAdd(Surface); }
				}
			}
		}
	}
	auto CountJson = MakeShared<FJsonObject>();
	for (const auto& Pair : Counts) { CountJson->SetNumberField(FString::FromInt(Pair.Key), Pair.Value); }
	Root->SetObjectField(TEXT("counts"), CountJson);
	auto SimpleJson = MakeShared<FJsonObject>();
	for (const auto& Pair : SimpleCounts) { SimpleJson->SetNumberField(FString::FromInt(Pair.Key), Pair.Value); }
	Root->SetObjectField(TEXT("simple_counts"), SimpleJson);
	Root->SetArrayField(TEXT("points"), Points);
	FString Result;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Result));
	return Result;
}
