#include "Item/BaseItem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "ItemData.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "InteractableComponent.h"

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
	bIsHovering = false;

	/* ------------------------- LAGACY -------------------------*/
	/*
	// Mesh보다 큰 범위에서 Player와의 상호작용을 감지하는 Sphere Collider
	InteractSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractSphere"));
	InteractSphere->SetupAttachment(RootComponent);
	InteractSphere->SetSphereRadius(150.f);
	InteractSphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	*/
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
		return MyDefinition->CanUseAbilityList;
	}
	return TArray<FGameplayTag>();
}

void ABaseItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// ItemTag만 동기화하고 각 클라이언트가 Tag를 통해 알아서 처리
	DOREPLIFETIME(ABaseItem, ItemTag);
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
}

void ABaseItem::OnRep_ItemTag()
{
	InitializeItem();
}

void ABaseItem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Tick 켜졌다는 것은 OnMeshSleep이 호출되었다는 것. 필요하면 조건문으로 처리할 것.

	AddActorLocalRotation(FRotator(0.f, HoverSpeed * DeltaTime, 0.f));

	float NewZ = HoverBaseLoc.Z + (FMath::Sin(GetGameTimeSinceCreation() * 3.f) * 10.f);
	SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, NewZ));
}

void ABaseItem::OnMeshSleep(UPrimitiveComponent* SleepingComponent, FName BoneName)
{

	// 호버링 중에는 물리 법칙을 무시하고 둥둥 뜨니까
	if (ItemMesh)
	{
		ItemMesh->SetSimulatePhysics(false);
	}

	HoverBaseLoc = GetActorLocation() + FVector(0.f, 0.f, 40.f);

	// 둥둥 뜨기 시작할 때만 Tick 활성화
	bIsHovering = true;
	SetActorTickEnabled(true);
}

void ABaseItem::PickUpItem(AActor* Picker)
{
	if (!Picker) return;

	// 호버링 끄기
	bIsHovering = false;
	SetActorTickEnabled(false);

	// 각종 물리 옵션 끄기
	// InteractSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision); LAGACY

	InteractableComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ItemMesh->SetSimulatePhysics(false);
	ItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 플레이어에게 부착
	AttachToComponent(Cast<ACharacter>(Picker)->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, MyDefinition->AttachmentSocketName);
}

void ABaseItem::OnInteractableTriggered(AActor* Interactor)
{
	// 방송이 들어오면 기존의 PickUpItem을 실행하여 로직 재사용
	PickUpItem(Interactor);
}

void ABaseItem::InitializeItem()
{
	if (ItemDataAsset && ItemTag.IsValid())
	{
		if (const FItemDefinition* Def = ItemDataAsset->FindItemDefinition(ItemTag))
		{
			MyDefinition = Def;

			if (UStaticMesh* LoadedMesh = MyDefinition->ItemMesh.LoadSynchronous())
			{
				ItemMesh->SetStaticMesh(LoadedMesh);

				ItemMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));

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
	// 플레이어 손에서 분리
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	/* LAGACY
	// 상호작용(Overlap) 콜리전 복구
	if (InteractSphere)
	{
		InteractSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	*/

	if (InteractableComponent)
	{
		InteractableComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}

	// 메쉬 물리 및 충돌 복구
	if (ItemMesh)
	{
		ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		ItemMesh->SetSimulatePhysics(true);

		// 던진 직후 플레이어와 충돌해서 튕겨나가는 것 방지
		if (Thrower)
		{
			ItemMesh->MoveIgnoreActors.Add(Thrower);
		}

		// 방향 벡터(속도) 가하기
		ItemMesh->AddImpulse(LaunchVelocity, NAME_None, true);
	}
}