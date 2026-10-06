#pragma once

class UWorld;
class AActor;
class APawn;

struct ARTISTICSWCORE_API FSWVoyageSpawn
{
	static bool RegisterDeferredActorSpawn(AActor* Actor, ESWVoyageActorLifetime Lifetime,
		int32 ExpectedGeneration, const FGuid& PresentationCleanupId, FString& OutError);
	static ESWVoyageSpawnFinishState GetActorSpawnFinishState(const AActor* Actor);
	static void ShutdownSpawnTracking();
	template<typename T>
	static T* SpawnDeferred(UWorld* World, UClass* Class, const FTransform& Transform, AActor* Owner, APawn* Instigator,
		ESpawnActorCollisionHandlingMethod Collision, ESWVoyageActorLifetime Lifetime, int32 ExpectedGeneration);
	static AActor* BeginSpawn(UWorld* World, UClass* Class, const FTransform& Transform, AActor* Owner, APawn* Instigator,
		ESpawnActorCollisionHandlingMethod Collision, ESWVoyageActorLifetime Lifetime, int32 ExpectedGeneration);
	template<typename T>
	static T* SpawnDeferred(UWorld* World, UClass* Class, const FTransform& Transform, AActor* Owner, APawn* Instigator,
		ESpawnActorCollisionHandlingMethod Collision, ESWVoyageActorLifetime Lifetime, int32 ExpectedGeneration, const FGuid& PresentationCleanupId);
	static AActor* BeginSpawn(UWorld* World, UClass* Class, const FTransform& Transform, AActor* Owner, APawn* Instigator,
		ESpawnActorCollisionHandlingMethod Collision, ESWVoyageActorLifetime Lifetime, int32 ExpectedGeneration, const FGuid& PresentationCleanupId);
};

template<typename T>
T* FSWVoyageSpawn::SpawnDeferred(UWorld* World, UClass* Class, const FTransform& Transform, AActor* Owner, APawn* Instigator,
	ESpawnActorCollisionHandlingMethod Collision, ESWVoyageActorLifetime Lifetime, int32 ExpectedGeneration)
{
	return Class && Class->IsChildOf(T::StaticClass())
		? Cast<T>(BeginSpawn(World, Class, Transform, Owner, Instigator, Collision, Lifetime, ExpectedGeneration)) : nullptr;
}

template<typename T>
T* FSWVoyageSpawn::SpawnDeferred(UWorld* World, UClass* Class, const FTransform& Transform, AActor* Owner, APawn* Instigator,
	ESpawnActorCollisionHandlingMethod Collision, ESWVoyageActorLifetime Lifetime, int32 ExpectedGeneration, const FGuid& PresentationCleanupId)
{
	return Class && Class->IsChildOf(T::StaticClass())
		? Cast<T>(BeginSpawn(World, Class, Transform, Owner, Instigator, Collision, Lifetime, ExpectedGeneration, PresentationCleanupId)) : nullptr;
}
