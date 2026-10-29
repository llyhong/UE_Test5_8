// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/FoodSystem.h"

void UFoodSystem::InitMenuData(UDataTable* MenuDataTable)
{
	if (!MenuDataTable)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load DataTable!"));
		return;
	}
	MenuTables.Empty();
	GEngine->AddOnScreenDebugMessage(-1,2,FColor::Yellow,TEXT("初始化菜单:"));

	// 以行名（RowName）为索引：约定行名就是菜品 FoodID（如 "3"），
	// 这样查表与数据表行顺序完全无关；行名不是纯数字时退回读行内的 FoodID 字段。
	for (const TPair<FName, uint8*>& Pair : MenuDataTable->GetRowMap())
	{
		FMenuData* MenuRow = reinterpret_cast<FMenuData*>(Pair.Value);
		if (!MenuRow)
		{
			continue;
		}

		const int32 FoodID = MenuRow->ResolveFoodID(Pair.Key);
		if (FoodID < 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FoodSystem] 菜品行 %s 无法确定 FoodID（行名不是数字且 FoodID 字段未填），已跳过"),
				*Pair.Key.ToString());
			continue;
		}

		if (FMenuData::GetFoodIDFromRowName(Pair.Key) != INDEX_NONE && MenuRow->FoodID != FoodID)
		{
			// 行名与行内字段不一致时以行名为准，提醒一下方便修正数据表
			UE_LOG(LogTemp, Warning, TEXT("[FoodSystem] 菜品行 %s 的行名与 FoodID 字段(%d)不一致，按行名 %d 处理"),
				*Pair.Key.ToString(), MenuRow->FoodID, FoodID);
		}

		MenuTables.Add(FoodID, MenuRow);
	}
}

 FMenuData UFoodSystem::GetMenuData(int32 FoodID)
{
	FMenuData** data = MenuTables.Find(FoodID);
	if (data == nullptr || *data == nullptr)
	{
		auto err = FMenuData();
		err.FoodID =-1;
		return err;
	}

	// 返回值里的 FoodID 统一成查表用的键：行名与字段不一致时也不会拿到错误 ID
	FMenuData Row = **data;
	Row.FoodID = FoodID;
	return Row;
}

UTexture2D* UFoodSystem::GetFoodIcon(int32 FoodID)
{
	FMenuData** data = MenuTables.Find(FoodID);
	return (data && *data) ? (*data)->Icon.Get() : nullptr;
}

TArray<int32> UFoodSystem::GetFoodIDsByMachine(int32 MachineID) const
{
	TArray<int32> FoodIDs;
	for (const TPair<int32, FMenuData*>& Pair : MenuTables)
	{
		if (!Pair.Value)
		{
			continue;
		}

		// 菜品的 MachineID 为 0 = 通用；MachineID<=0 时不过滤（返回全部）
		if (MachineID <= 0 || Pair.Value->MachineID == 0 || Pair.Value->MachineID == MachineID)
		{
			FoodIDs.Add(Pair.Key);
		}
	}
	return FoodIDs;
}
ABaseFoodActor* UFoodSystem::CreateFoodActor(int32 FoodID)
{
	UClass* LoadedClass = LoadObject<UClass>(nullptr, *FString::Printf(TEXT("/Game/_Demo/Blueprints/Cooking/Food/BP_Food_%d.BP_Food_%d_C"),FoodID,FoodID));
	if (LoadedClass)
	{
		ABaseFoodActor* NewActor = GetWorld()->SpawnActor<ABaseFoodActor>(LoadedClass, FVector::Zero(), FRotator::ZeroRotator);
		return NewActor;
	}
	GEngine->AddOnScreenDebugMessage(-1,2,FColor::Red,TEXT("无效食物ID "));
	return nullptr;
}