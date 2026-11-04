// Fill out your copyright notice in the Description page of Project Settings.

#include "Machine/StorageWarehouse.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"

AStorageWarehouse::AStorageWarehouse()
{
	PrimaryActorTick.bCanEverTick = false;

	// 外观模型作为根组件，交互球挂在其上
	WarehouseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WarehouseMesh"));
	RootComponent = WarehouseMesh;

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(RootComponent);
	InteractionSphere->SetSphereRadius(InteractionRadius * InteractionScale::Get());
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	InteractionSphere->SetGenerateOverlapEvents(true);
	// 视口里未选中也画出判定球（黄色=仓库），方便摆放时目测交互范围
	InteractionSphere->ShapeColor = FColor(230, 200, 60);
	InteractionSphere->bDrawOnlyIfSelected = false;

	// 提示控件：靠近时显示“按 E 存入仓库”等提示。
	// 控件类由蓝图在 TipsWidgetComp 的 Widget Class 上配置（这里不给默认类，避免蓝图未配时占一个空控件）。
	TipsWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("TipsWidgetComp"));
	TipsWidgetComp->SetupAttachment(RootComponent);
	TipsWidgetComp->SetRelativeLocation(TipsOffset);
	TipsWidgetComp->SetWidgetSpace(EWidgetSpace::Screen);
	TipsWidgetComp->SetDrawAtDesiredSize(true);
	TipsWidgetComp->SetTwoSided(true);
	TipsWidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TipsWidgetComp->SetGenerateOverlapEvents(false);
	TipsWidgetComp->SetVisibility(false);
}

void AStorageWarehouse::SetTipsVisible(bool bVisible)
{
	if (!TipsWidgetComp)
	{
		return;
	}
	// 组件隐藏时不会创建控件实例，显示前先确保建出来（InitWidget 内部幂等）
	if (bVisible)
	{
		TipsWidgetComp->InitWidget();
	}
	if (TipsWidgetComp->IsVisible() != bVisible)
	{
		TipsWidgetComp->SetVisibility(bVisible);
	}
}

void AStorageWarehouse::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 编辑器视口预览：改 InteractionRadius 立刻反映到组件上
	ApplyInteractionScale();
}

void AStorageWarehouse::ApplyInteractionScale()
{
	if (InteractionSphere)
	{
		InteractionSphere->SetSphereRadius(InteractionRadius * InteractionScale::Get(), false);
	}
}

void AStorageWarehouse::BeginPlay()
{
	Super::BeginPlay();

	// 让蓝图里调整的 InteractionRadius 生效
	ApplyInteractionScale();

	InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &AStorageWarehouse::OnInteractionOverlap);
	InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &AStorageWarehouse::OnInteractionEndOverlap);
}

void AStorageWarehouse::OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		Character->AddOverlappingWarehouse(this);
		SetTipsVisible(true);
		OnPlayerNearby(true); // 通知蓝图刷新提示内容（如“按 E 存入仓库（背包 X 件）”）
	}
}

void AStorageWarehouse::OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (AShootCharacter* Character = Cast<AShootCharacter>(OtherActor))
	{
		// 只注销自己，多仓库重叠时不会把别的仓库目标清掉
		Character->RemoveOverlappingWarehouse(this);
	}
	SetTipsVisible(false);
	OnPlayerNearby(false);
}

int32 AStorageWarehouse::DepositFromInventory(AShootCharacter* Player)
{
	if (!Player)
	{
		return 0;
	}

	int32 Deposited = 0;
	// 逐个把背包第 0 槽位转入仓库（RemoveItemFromInventory 会移除并广播 OnInventoryChanged）；
	// 命中容量上限则停止，剩余留在背包。
	while (Player->GetInventoryCount() > 0)
	{
		if (StorageCapacity > 0 && StoredItems.Num() >= StorageCapacity)
		{
			break;
		}

		const TArray<FInventorySlot>& Slots = Player->GetInventoryItems();
		FInventorySlot Slot = Slots[0];

		// 按 ItemClass 堆叠到已有槽位
		bool bStacked = false;
		for (FInventorySlot& Existing : StoredItems)
		{
			if (Existing.ItemClass == Slot.ItemClass)
			{
				Existing.Quantity += Slot.Quantity;
				bStacked = true;
				break;
			}
		}
		if (!bStacked)
		{
			StoredItems.Add(Slot);
		}

		Player->RemoveItemFromInventory(0);
		Deposited++;
	}

	OnWarehouseChanged.Broadcast();
	OnPlayerNearby(true); // 库存变化后刷新提示（如数量），玩家仍在范围内

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green,
			FString::Printf(TEXT("[仓库] 已存入 %d 种，仓库共 %d 种"), Deposited, StoredItems.Num()));
	}
	return Deposited;
}

bool AStorageWarehouse::RetrieveToInventory(AShootCharacter* Player, int32 SlotIndex)
{
	if (!Player || !StoredItems.IsValidIndex(SlotIndex))
	{
		return false;
	}

	FInventorySlot Slot = StoredItems[SlotIndex];
	StoredItems.RemoveAt(SlotIndex);
	Player->AddSlotToInventory(Slot);

	OnWarehouseChanged.Broadcast();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Green,
			FString::Printf(TEXT("[仓库] 已取回 %s，仓库剩余 %d 种"), *Slot.DisplayName, StoredItems.Num()));
	}
	return true;
}

FString AStorageWarehouse::GetStoredDisplayName(int32 SlotIndex) const
{
	if (StoredItems.IsValidIndex(SlotIndex))
	{
		return StoredItems[SlotIndex].DisplayName;
	}
	return TEXT("");
}

int32 AStorageWarehouse::GetStoredQuantity(TSubclassOf<AItemActor> ItemClass) const
{
	if (!ItemClass)
	{
		return 0;
	}
	int32 Total = 0;
	for (const FInventorySlot& Slot : StoredItems)
	{
		if (Slot.ItemClass == ItemClass)
		{
			Total += Slot.Quantity;
		}
	}
	return Total;
}

bool AStorageWarehouse::ConsumeOne(TSubclassOf<AItemActor> ItemClass)
{
	if (!ItemClass)
	{
		return false;
	}
	for (int32 i = 0; i < StoredItems.Num(); ++i)
	{
		FInventorySlot& Slot = StoredItems[i];
		if (Slot.ItemClass == ItemClass)
		{
			Slot.Quantity--;
			if (Slot.Quantity <= 0)
			{
				StoredItems.RemoveAt(i);
			}
			OnWarehouseChanged.Broadcast();
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Yellow,
					FString::Printf(TEXT("[仓库] 消耗 %s，剩余 %d 种"), *Slot.DisplayName, StoredItems.Num()));
			}
			return true;
		}
	}
	return false;
}
