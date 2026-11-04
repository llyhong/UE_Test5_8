// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PickupInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UPickupInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class SHOOT_API IPickupInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	virtual void SetOverlappingItem(class AItemActor* Item);
	virtual void SetCookMachineItem(class ACookingMachine* Item);

	// 带身份语义的注册/注销（取代 SetOverlappingItem 的"单指针覆盖"用法）：
	// 物品进入交互范围时注册自己，离开时注销自己；由角色内部维护候选集并重选最近目标
	virtual void AddOverlappingItem(class AItemActor* Item) {}
	virtual void RemoveOverlappingItem(class AItemActor* Item) {}

	// 灶台、植物同理：只登记/注销自己，选谁是角色的事。
	// 这样两个灶台/两株植物的判定范围交叠时，不会出现"后进入者胜"或"先离开者把目标清空"。
	virtual void AddOverlappingCookMachine(class ACookingMachine* Machine) {}
	virtual void RemoveOverlappingCookMachine(class ACookingMachine* Machine) {}
	virtual void AddOverlappingPlant(class APlant* Plant) {}
	virtual void RemoveOverlappingPlant(class APlant* Plant) {}
	//virtual void AddSouls(class ASoul* Soul);
	//virtual void AddGold(class ATreasure* Treasure);
};
