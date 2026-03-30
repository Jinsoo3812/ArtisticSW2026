#include "Item/BaseItem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "ItemData.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "InteractableComponent.h"
#include "CollisionChannels.h"
#include "ItemSubsystem.h"

ABaseItem::ABaseItem()
{
	// 둥둥 뜰 때만 Tick 켜기
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// 실제 물리 연산을 하는 것은 DA로부터 받아온 Mesh
	ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMesh"));
	RootComponent = ItemMesh;

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
	InteractableComponent->SetupAttachment(RootComponent);

	bReplicates = true;
	SetReplicateMovement(true);
	ItemState = EItemState::Dropped_Simulating;
}

TSubclassOf<UGameplayAbility> ABaseItem::GetGrantedAbilityClass() const
{
	if (MyDefinition && !MyDefinition->GrantedAbilityClass.IsNull())
	{
		// SoftClassPtr에서 동기 로드하여 반환 (이미 로드된 경우 O(1) 캐시 반환)
		return MyDefinition->GrantedAbilityClass.LoadSynchronous();
	}
	return nullptr;
}
TSubclassOf<AActor> ABaseItem::GetSpawnClass() const
{
	if (MyDefinition && !MyDefinition->SpawnClass.IsNull())
	{
		// SpawnClass 출력
		return MyDefinition->SpawnClass.LoadSynchronous();
	}
	return nullptr;
}
UStaticMesh* ABaseItem::GetStaticMesh() const
{
	if (MyDefinition && !MyDefinition->ItemMesh.IsNull())
	{
		return MyDefinition->ItemMesh.LoadSynchronous();
	}
	return nullptr;
}
TArray<FGameplayTag> ABaseItem::GetCanUseAbilityList() const
{
	if (MyDefinition)
	{
		return MyDefinition->CanUseClassList;
	}
	return TArray<FGameplayTag>();
}

void ABaseItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABaseItem, ItemTag);
	DOREPLIFETIME(ABaseItem, ItemState);
}

void ABaseItem::BeginPlay()
{
	Super::BeginPlay();

	// [서버]
	if (HasAuthority()) {
		InitializeItem();

		// 물리 수면 이벤트(물리 연산을 더이상 하지 않는 최적화 모드로 들어감) 바인딩
		// 서버에서만 바인딩하고 호버링을 클라가 따라하면 됨.
		if (ItemMesh)
		{
			ItemMesh->OnComponentSleep.AddDynamic(this, &ABaseItem::OnMeshSleep);
		}
	}

	// 상호작용 이벤트 바인딩 (Interact GA 이관 전 임시로 클라도 바인딩)
	if (InteractableComponent)
	{
		InteractableComponent->OnInteracted.AddDynamic(this, &ABaseItem::OnInteractableTriggered);
	}

	if (ItemTag.IsValid())
	{
		if (const FItemDefinition* Def = GetDefinitionFromSubsystem())
		{
			// InteractComp에 UI 정보 세팅
			if (UInteractableComponent* InteractComp = FindComponentByClass<UInteractableComponent>())
			{
				InteractComp->InteractUIInfo.ObjectName = Def->ItemName;
				InteractComp->InteractUIInfo.ActionText = Def->HowToInteractText;
			}
		}
	}
}

const FItemDefinition* ABaseItem::GetDefinitionFromSubsystem() const
{
	if (UWorld* World = GetWorld())
	{
		if (UItemSubsystem* ItemSubsystem = World->GetSubsystem<UItemSubsystem>())
		{
			return ItemSubsystem->GetItemDefinition(ItemTag);
		}
	}
	return nullptr;
}

void ABaseItem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (ItemState == EItemState::Dropped_Hovering)
	{
		AddActorLocalRotation(FRotator(0.f, HoverSpeed * DeltaTime, 0.f));
		float NewZ = HoverBaseLoc.Z + (FMath::Sin(GetGameTimeSinceCreation() * 3.f) * 10.f);
		SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, NewZ));
	}
}

void ABaseItem::SetItemState(EItemState NewState)
{
	if (HasAuthority() && ItemState != NewState)
	{
		ItemState = NewState;
		OnRep_ItemState(); // 서버 로컬 적용
	}
}

void ABaseItem::OnMeshSleep(UPrimitiveComponent* SleepingComponent, FName BoneName)
{
	// 수면 상태 진입 시 호버링 상태로 전환
	if (HasAuthority() && ItemState == EItemState::Dropped_Simulating)
	{
		SetItemState(EItemState::Dropped_Hovering);
	}
}

void ABaseItem::PickUpItem(AActor* Picker)
{
	if (!Picker) return;

	AttachToComponent(Cast<ACharacter>(Picker)->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, MyDefinition->AttachmentSocketName);
}

void ABaseItem::OnInteractableTriggered(AActor* Interactor)
{
	// 방송이 들어오면 기존의 PickUpItem을 실행하여 로직 재사용
	PickUpItem(Interactor);
}

void ABaseItem::InitializeItem()
{
	if (ItemTag.IsValid())
	{
		if (const FItemDefinition* Def = GetDefinitionFromSubsystem())
		{
			MyDefinition = Def;

			if (UStaticMesh* LoadedMesh = MyDefinition->ItemMesh.LoadSynchronous())
			{
				ItemMesh->SetStaticMesh(LoadedMesh);

				ItemMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
				ItemMesh->SetCollisionResponseToChannel(ECC_Interactable, ECR_Ignore);

				// 메쉬와 충돌체가 준비되었으니 이제 중력과 물리를 킴
				ItemMesh->SetSimulatePhysics(true);
				ItemMesh->SetGenerateOverlapEvents(true);

				// Slepp 이벤트를 받기 위함
				ItemMesh->BodyInstance.bGenerateWakeEvents = true;
			}
		}
	}
}

void ABaseItem::OnThrown(FVector LaunchVelocity, AActor* Thrower)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	SetItemState(EItemState::Dropped_Simulating);

	if (ItemMesh)
	{
		if (Thrower)
		{
			ItemMesh->MoveIgnoreActors.Add(Thrower);
		}
		ItemMesh->AddImpulse(LaunchVelocity, NAME_None, true);
	}
}

void ABaseItem::OnRep_ItemTag()
{
	InitializeItem();
}

void ABaseItem::OnRep_ItemState()
{
	switch (ItemState)
	{
	case EItemState::Dropped_Simulating:
		SetActorHiddenInGame(false);
		SetActorTickEnabled(false);
		if (InteractableComponent) InteractableComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		if (ItemMesh)
		{
			ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			ItemMesh->SetSimulatePhysics(true);
		}
		break;

	case EItemState::Dropped_Hovering:
		SetActorHiddenInGame(false);
		SetActorTickEnabled(true);
		HoverBaseLoc = GetActorLocation() + FVector(0.f, 0.f, 40.f);
		if (InteractableComponent) InteractableComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		if (ItemMesh)
		{
			ItemMesh->SetSimulatePhysics(false);
			// 주울 수 있어야 하므로 메쉬 콜리전은 켜두되 카메라 채널 등은 무시하도록 프로파일 설정 요망
			ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
		break;

	case EItemState::Equipped:
		// 시점 버그 완벽 차단: 콜리전을 NoCollision으로 설정
		SetActorHiddenInGame(false);
		SetActorTickEnabled(false);
		if (InteractableComponent) InteractableComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (ItemMesh)
		{
			ItemMesh->SetSimulatePhysics(false);
			ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		break;

	case EItemState::InItemSlot:
		SetActorHiddenInGame(true);
		SetActorTickEnabled(false);
		if (InteractableComponent) InteractableComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (ItemMesh)
		{
			ItemMesh->SetSimulatePhysics(false);
			ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		break;
	}
}

UTexture2D* ABaseItem::GetItemIcon() const
{
	if (const FItemDefinition* Def = GetDefinitionFromSubsystem())
	{
		return Def->Icon2D.LoadSynchronous();
	}
	return nullptr;
}

FText ABaseItem::GetItemNameText() const
{
	if (const FItemDefinition* Def = GetDefinitionFromSubsystem())
	{
		return Def->ItemName;
	}
	return FText::FromString(ItemTag.ToString());
}