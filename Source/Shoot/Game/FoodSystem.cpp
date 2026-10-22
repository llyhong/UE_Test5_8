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
	TArray<FMenuData*> AllRows;
	FString ContextString(TEXT("Getting All Rows"));
	MenuDataTable->GetAllRows<FMenuData>(ContextString, AllRows);
	GEngine->AddOnScreenDebugMessage(-1,2,FColor::Yellow,TEXT("初始化菜单:"));
	for (int i = 0; i < AllRows.Num(); i++)
	{
		FMenuData* MenuRow = AllRows[i];
		if (MenuRow)
		{
			MenuTables.Add(i,MenuRow);
			// FString Buffer;
			// Buffer +=TEXT("MenuTables:");
			// Buffer += MenuRow->FoodName;
			// UE_LOG(LogTemp, Error, TEXT("%s"),*Buffer);
			// GEngine->AddOnScreenDebugMessage(-1,2,FColor::Yellow,MenuRow->FoodName);
		}
	}
}

 FMenuData UFoodSystem::GetMenuData(int32 Index)
{
	FMenuData** data = MenuTables.Find(Index);
	if (data == nullptr)
	{
		auto err = FMenuData();
		err.FoodID =-1;
		return err;
	}
	return **data;
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