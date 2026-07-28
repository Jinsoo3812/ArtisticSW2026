#include "Item/Weapons/SwordItem.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "BaseGameplayTags.h"
#include "Components/SceneComponent.h"
#include "InteractableComponent.h"
#include "Kismet/KismetSystemLibrary.h"

ASwordItem::ASwordItem()
{
	TraceStartPoint = CreateDefaultSubobject<USceneComponent>(TEXT("TraceStartPoint"));
	TraceStartPoint->SetupAttachment(ItemMesh);

	TraceEndPoint = CreateDefaultSubobject<USceneComponent>(TEXT("TraceEndPoint"));
	TraceEndPoint->SetupAttachment(ItemMesh);

	TraceObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	// Sword actors participate in the same generic pickup event path as all
	// other inventory items without requiring a Blueprint-only default.
	if (InteractableComponent)
	{
		InteractableComponent->InteractionTag = Interaction_PickUp;
	}
}

void ASwordItem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HitScanEnd();
	Super::EndPlay(EndPlayReason);
}

float ASwordItem::CalculateDamage(float AttackPower) const
{
	return FMath::Max(0.0f, BaseDamage + FMath::Max(0.0f, AttackPower) * AttackPowerMultiplier);
}

bool ASwordItem::HitScanStart(const FGameplayEffectSpecHandle& DamageEffectSpecHandle)
{
	if (!HasAuthority() || !GetWorld() || !TraceStartPoint || !TraceEndPoint)
	{
		return false;
	}

	if (!DamageEffectSpecHandle.IsValid() || !DamageEffectSpecHandle.Data.IsValid())
	{
		return false;
	}

	HitScanEnd();

	CachedDamageEffectSpecHandle = DamageEffectSpecHandle;
	HitActors.Reset();
	bHitScanActive = true;
	bHasPreviousTracePoints = true;
	PreviousTraceStart = TraceStartPoint->GetComponentLocation();
	PreviousTraceEnd = TraceEndPoint->GetComponentLocation();

	// Capture actors already intersecting the blade when the window opens.
	TraceSegment(PreviousTraceStart, PreviousTraceEnd);
	return true;
}

void ASwordItem::HitScanEnd()
{
	ClearHitScanState();
}

void ASwordItem::SampleHitScan()
{
	if (!HasAuthority() || !GetWorld() || !bHitScanActive || !TraceStartPoint || !TraceEndPoint)
	{
		return;
	}

	if (!CachedDamageEffectSpecHandle.IsValid() || !CachedDamageEffectSpecHandle.Data.IsValid())
	{
		HitScanEnd();
		return;
	}

	const FVector CurrentStart = TraceStartPoint->GetComponentLocation();
	const FVector CurrentEnd = TraceEndPoint->GetComponentLocation();

	// Cover both the current blade and the volume crossed since the previous
	// sample. This is more tolerant of fast montage motion than sampling only
	// the current blade segment.
	TraceSegment(CurrentStart, CurrentEnd);

	if (bHasPreviousTracePoints)
	{
		TraceSegment(PreviousTraceStart, CurrentStart);
		TraceSegment(PreviousTraceEnd, CurrentEnd);
		TraceSegment((PreviousTraceStart + PreviousTraceEnd) * 0.5f, (CurrentStart + CurrentEnd) * 0.5f);
	}

	PreviousTraceStart = CurrentStart;
	PreviousTraceEnd = CurrentEnd;
	bHasPreviousTracePoints = true;
}

void ASwordItem::TraceSegment(const FVector& Start, const FVector& End)
{
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(this);
	if (AActor* OwnerActor = GetOwner())
	{
		ActorsToIgnore.AddUnique(OwnerActor);
	}
	if (AActor* InstigatorActor = GetInstigator())
	{
		ActorsToIgnore.AddUnique(InstigatorActor);
	}

	TArray<FHitResult> HitResults;
	UKismetSystemLibrary::SphereTraceMultiForObjects(
		this,
		Start,
		End,
		TraceRadius,
		TraceObjectTypes,
		bTraceComplex,
		ActorsToIgnore,
		bDrawDebugTrace ? EDrawDebugTrace::ForOneFrame : EDrawDebugTrace::None,
		HitResults,
		true);

	for (const FHitResult& HitResult : HitResults)
	{
		HandleHit(HitResult);
	}
}

void ASwordItem::HandleHit(const FHitResult& HitResult)
{
	AActor* HitActor = HitResult.GetActor();
	if (!HitActor)
	{
		return;
	}

	const TWeakObjectPtr<AActor> HitActorPtr(HitActor);
	if (HitActors.Contains(HitActorPtr))
	{
		return;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);
	if (!TargetASC || ShouldIgnoreActor(HitActor, TargetASC))
	{
		return;
	}

	// Add before applying the effect so callbacks caused by damage cannot hit
	// the same actor re-entrantly during this attack window.
	HitActors.Add(HitActorPtr);
	ApplyEffectToTarget(TargetASC, HitResult);
}

void ASwordItem::ApplyEffectToTarget(UAbilitySystemComponent* TargetASC, const FHitResult& HitResult)
{
	if (!TargetASC || !CachedDamageEffectSpecHandle.IsValid() || !CachedDamageEffectSpecHandle.Data.IsValid())
	{
		return;
	}

	FGameplayEffectSpec TargetEffectSpec(*CachedDamageEffectSpecHandle.Data.Get());
	FGameplayEffectContextHandle EffectContext = TargetEffectSpec.GetContext();
	AActor* SourceActor = ResolveSourceActor();
	EffectContext.AddInstigator(SourceActor, this);
	EffectContext.AddSourceObject(this);
	EffectContext.AddHitResult(HitResult, true);
	TargetEffectSpec.SetContext(EffectContext);

	TargetASC->ApplyGameplayEffectSpecToSelf(TargetEffectSpec);
}

bool ASwordItem::ShouldIgnoreActor(const AActor* OtherActor, const UAbilitySystemComponent* TargetASC) const
{
	if (!OtherActor || OtherActor == this || OtherActor == GetOwner() || OtherActor == GetInstigator())
	{
		return true;
	}

	if (TargetASC && TargetASC->HasMatchingGameplayTag(State_Dead))
	{
		return true;
	}

	if (!bIgnoreSameTeam || !TargetASC)
	{
		return false;
	}

	const UAbilitySystemComponent* SourceASC = ResolveSourceAbilitySystem();
	if (!SourceASC)
	{
		return false;
	}

	const bool bBothPlayers = SourceASC->HasMatchingGameplayTag(Team_Player) && TargetASC->HasMatchingGameplayTag(Team_Player);
	const bool bBothEnemies = SourceASC->HasMatchingGameplayTag(Team_Enemy) && TargetASC->HasMatchingGameplayTag(Team_Enemy);
	return bBothPlayers || bBothEnemies;
}

UAbilitySystemComponent* ASwordItem::ResolveSourceAbilitySystem() const
{
	return UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(ResolveSourceActor());
}

AActor* ASwordItem::ResolveSourceActor() const
{
	if (CachedDamageEffectSpecHandle.IsValid() && CachedDamageEffectSpecHandle.Data.IsValid())
	{
		const FGameplayEffectContextHandle& Context = CachedDamageEffectSpecHandle.Data->GetContext();
		if (AActor* OriginalInstigator = Context.GetOriginalInstigator())
		{
			return OriginalInstigator;
		}
	}

	if (AActor* InstigatorActor = GetInstigator())
	{
		return InstigatorActor;
	}

	return GetOwner();
}

void ASwordItem::ClearHitScanState()
{
	bHitScanActive = false;
	bHasPreviousTracePoints = false;
	CachedDamageEffectSpecHandle = FGameplayEffectSpecHandle();
	HitActors.Reset();
	PreviousTraceStart = FVector::ZeroVector;
	PreviousTraceEnd = FVector::ZeroVector;
}
