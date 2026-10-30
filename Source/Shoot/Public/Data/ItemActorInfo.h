// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "ItemActorInfo.generated.h"

/**
 * 物品信息数据表行结构
 *
 * 物品 ID 的约定（与菜品表 FMenuData 一致）：
 *   - 以数据表行名（RowName）为准：行名写成纯数字（如 "3"）即代表 ItemID = 3；
 *   - 行内的 ItemID 字段只作为"行名不是数字"时的兼容兜底；
 *   - 取值一律走 ResolveItemID(RowName)，不要再直接读 ItemID 字段。
 */
USTRUCT(BlueprintType)
struct FItemActorInfo : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 物品 ID（兼容兜底用）：行名为纯数字时以行名为准，本字段仅在行名不是数字时生效。
	 *  取值请用 ResolveItemID(RowName) / FItemActorInfo::GetItemIDFromRowName(RowName) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ItemID = -1;

	/** 物品描述 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ItemDes;

	/** 物品图标：食材 / 物品在 UI 上显示的图标（DT_ItemInfo / DT_FoodID 按行配置）。
	 *  菜品 / 订单 UI 也按 FMenuData::MenuItems 的 key（食材 ItemID）到这里查同一张图 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** 从行名解析物品 ID：行名为纯数字（约定 RowName == ItemID）时返回该值，否则返回 INDEX_NONE */
	static int32 GetItemIDFromRowName(FName RowName)
	{
		const FString RowNameStr = RowName.ToString();
		return RowNameStr.IsNumeric() ? FCString::Atoi(*RowNameStr) : INDEX_NONE;
	}

	/** 本行的物品 ID：行名为数字时以行名为准，否则退回 ItemID 字段（兼容未改行名的旧数据） */
	int32 ResolveItemID(FName RowName) const
	{
		const int32 RowNameID = GetItemIDFromRowName(RowName);
		return RowNameID != INDEX_NONE ? RowNameID : ItemID;
	}
};
