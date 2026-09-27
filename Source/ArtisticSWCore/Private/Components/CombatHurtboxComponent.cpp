#include "Components/CombatHurtboxComponent.h"
#include "CollisionChannels.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Character.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UCombatHurtboxComponent::UCombatHurtboxComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UCombatHurtboxComponent::ValidateConfiguration(FString& OutFailure) const
{
	OutFailure.Reset();
	if (Mode == ECombatHurtboxMode::MovementCapsule) return true;
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	const UPhysicsAsset* Physics = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	FCollisionResponseTemplate Profile;
	if (!Character || !Character->GetCapsuleComponent()) OutFailure = TEXT("Character movement capsule is missing");
	else if (!Mesh || !Mesh->GetSkeletalMeshAsset()) OutFailure = TEXT("Skeletal mesh is missing");
	else if (!Physics) OutFailure = TEXT("Physics Asset is missing");
	else if (!Physics->SkeletalBodySetups.ContainsByPredicate([Mesh](const USkeletalBodySetup* Body)
	{
		return Body && Body->CollisionReponse != EBodyCollisionResponse::BodyCollision_Disabled
			&& Body->AggGeom.GetElementCount() > 0 && Mesh->GetBoneIndex(Body->BoneName) != INDEX_NONE;
	})) OutFailure = TEXT("Physics Asset has no enabled body on a mesh bone");
	else if (!UCollisionProfile::Get()->GetProfileTemplate(ProfileName, Profile)) OutFailure = TEXT("Hurtbox collision profile is missing");
	else if (Profile.ObjectType != ECC_CombatHurtbox || !CollisionEnabledHasQuery(Profile.CollisionEnabled)
		|| Profile.ResponseToChannels.GetResponse(ECC_Arrow) != ECR_Block
		|| Profile.ResponseToChannels.GetResponse(ECC_WeaponAim) != ECR_Block)
		OutFailure = TEXT("Hurtbox profile must enable queries, use CombatHurtbox, and block Arrow and WeaponAim");
	return OutFailure.IsEmpty();
}

void UCombatHurtboxComponent::InitializeHurtbox()
{
	bInitialized = false;
	InitializationFailure.Reset();
	if (Mode == ECombatHurtboxMode::MovementCapsule) return;
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	// Never fall back to capsule hits, including when authored mesh data is invalid.
	if (Character && Character->GetCapsuleComponent())
	{
		Character->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Arrow, ECR_Ignore);
		Character->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_WeaponAim, ECR_Ignore);
	}
	if (!ValidateConfiguration(InitializationFailure))
	{
		UE_LOG(LogTemp, Warning, TEXT("CombatHurtbox %s: %s. Direct hits are rejected."),
			*GetPathNameSafe(GetOwner()), *InitializationFailure);
		return;
	}
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	Mesh->SetCollisionProfileName(ProfileName);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetGenerateOverlapEvents(false);
	if (Character->HasAuthority())
	{
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->bEnableUpdateRateOptimizations = false;
		Mesh->SetComponentTickEnabled(true);
	}
	bInitialized = true;
}

bool UCombatHurtboxComponent::IsAnimatedHurtboxReady() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	return Mode == ECombatHurtboxMode::AnimatedPhysicsAsset && bInitialized && Mesh
		&& Mesh->GetPhysicsAsset() && Mesh->IsQueryCollisionEnabled()
		&& Mesh->GetCollisionObjectType() == ECC_CombatHurtbox
		&& Mesh->GetCollisionResponseToChannel(ECC_Arrow) == ECR_Block
		&& Mesh->GetCollisionResponseToChannel(ECC_WeaponAim) == ECR_Block
		&& (!Character->HasAuthority() || (Mesh->IsComponentTickEnabled() && !Mesh->bPauseAnims
			&& Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones));
}

bool UCombatHurtboxComponent::AcceptsHit(const FHitResult& Hit) const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) return false;
	if (Mode == ECombatHurtboxMode::MovementCapsule) return Hit.GetComponent() == Character->GetCapsuleComponent();
	if (!IsAnimatedHurtboxReady() || Hit.GetComponent() != Character->GetMesh() || Hit.BoneName.IsNone()) return false;
	const UPhysicsAsset* Physics = Character->GetMesh()->GetPhysicsAsset();
	const int32 BodyIndex = Physics->FindBodyIndex(Hit.BoneName);
	return Physics->SkeletalBodySetups.IsValidIndex(BodyIndex) && Physics->SkeletalBodySetups[BodyIndex]
		&& Physics->SkeletalBodySetups[BodyIndex]->CollisionReponse != EBodyCollisionResponse::BodyCollision_Disabled;
}

bool UCombatHurtboxComponent::IsValidHitSurface(const AActor* Target, const FHitResult& Hit)
{
	const UCombatHurtboxComponent* Hurtbox = Target ? Target->FindComponentByClass<UCombatHurtboxComponent>() : nullptr;
	return !Hurtbox || Hurtbox->AcceptsHit(Hit);
}

#if WITH_EDITOR
EDataValidationResult UCombatHurtboxComponent::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	FString Failure;
	if (!ValidateConfiguration(Failure))
	{
		Context.AddError(FText::FromString(Failure));
		return EDataValidationResult::Invalid;
	}
	return ParentResult == EDataValidationResult::Invalid ? ParentResult : EDataValidationResult::Valid;
}
#endif
