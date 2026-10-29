// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/DataClass.h"
#include "NPC/ChairTypes.h"
#include "ChairTipsWidget.generated.h"

class AChairSeat;
class AItemActor;
class UOrderHandle;

/** 菜单条目：同一道菜按数量合并，方便蓝图直接铺列表（如「番茄炒蛋 x2」） */
USTRUCT(BlueprintType)
struct FChairMenuEntry
{
	GENERATED_BODY()

	/** 菜品 ID（对应 DT_Foods / MenuDataTable 的 FoodID） */
	UPROPERTY(BlueprintReadOnly, Category = "Chair|Tips")
	int32 FoodID = -1;

	/** 该菜品在订单里出现的次数 */
	UPROPERTY(BlueprintReadOnly, Category = "Chair|Tips")
	int32 Count = 0;

	/** 菜品名（取自 MenuDataTable 的 FoodName，取不到时退化成“菜品<ID>”） */
	UPROPERTY(BlueprintReadOnly, Category = "Chair|Tips")
	FString FoodName;

	/** 菜品图标（取自 MenuDataTable 的 Icon；未配置时为空，蓝图先 IsValid 再设 Image） */
	UPROPERTY(BlueprintReadOnly, Category = "Chair|Tips")
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** 拼好的显示文本：「番茄炒蛋 x2」 */
	UPROPERTY(BlueprintReadOnly, Category = "Chair|Tips")
	FText DisplayText;
};

/**
 * 餐桌 TipsWidget 基类：用于显示当前客人点的菜单。
 *
 * 使用方式（编辑器内）：
 *   1. 用 WBP 继承本类，排版成气泡 / 小票 / 菜单板；
 *   2. 把该 WBP 配到 BP_Chair 的 TipsWidgetComp 的 Widget Class 上；
 *   3. 在 WBP 里实现 OnTipsRefreshed，用下面这些函数取数据显示：
 *      - GetSeatState() / GetSeatStateText()        -> 当前桌况（等待点单 / 等待上菜 / 用餐中 / 待收空碟）
 *      - GetMenuEntries()                           -> 菜单列表（菜品名 + 数量，已按菜品合并）
 *      - GetFoodIDs() / GetSetName() / GetOrderPrice() -> 原始订单数据
 *      - GetRemainPercent() / GetRemainSeconds()    -> 订单剩余时间（做倒计时条）
 *      - GetOrderImageID() / GetMachineImageIDs()   -> 订单自带的图片 ID（与顶部订单 UI 同一套取图规则）
 *   4. 挂点位置：选中 BP_Chair 里的 TipsWidgetComp 直接拖动即可。
 *
 * 刷新时机由 AChairSeat 负责：下单 / 上菜 / 吃完留空碟 / 收走空碟 / 客人入座离开时都会主动调 RefreshTips。
 * 若需要倒计时之类的连续刷新，把 AutoRefreshInterval 设为 >0（默认 0.25 秒）即可。
 */
UCLASS()
class SHOOT_API UChairTipsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 拥有本控件的餐桌（由 AChairSeat 在刷新时主动赋值）；蓝图可直接取用，也可自行 Cast */
	UPROPERTY(BlueprintReadOnly, Category = "Chair|Tips")
	TObjectPtr<AChairSeat> OwnerSeat;

	/** C++ 把 OwnerSeat 赋值完成后调用一次：蓝图可在此安全地初始化（Construct 时 OwnerSeat 还是 null） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Chair|Tips")
	void OnOwnerSeatSet();

	/** 数据有变化时通知蓝图：读下面的 GetXXX 函数，把菜单 / 状态刷到控件上 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Chair|Tips")
	void OnTipsRefreshed();

	/** 立即刷新一次（触发 OnTipsRefreshed）；由餐桌调用，也可在蓝图里手动调用 */
	UFUNCTION(BlueprintCallable, Category = "Chair|Tips")
	void RefreshTips();

	/** 自动刷新间隔（秒）：>0 时按此间隔周期性刷新，用于订单倒计时；<=0 关闭（只由餐桌事件驱动） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chair|Tips")
	float AutoRefreshInterval = 0.25f;

	// ==================== 显示数据（蓝图只读） ====================

	/** 当前桌况（空 / 入座中 / 等待点单 / 等待上菜 / 用餐中 / 待收空碟） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	EChairTipsState GetSeatState() const;

	/** 桌况对应的默认中文文案（蓝图可直接用，也可按状态显示自己的美术字） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	FText GetSeatStateText() const;

	/** 本座位是否绑定了菜单（客人已点单、还没上菜） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	bool HasOrder() const;

	/** 本座位绑定的订单（可能为 null） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	UOrderHandle* GetOrderHandle() const;

	/** 订单展示数据副本（SetName / FoodIDs / Price / MaxWaitTime / IamgeID / MachineImageID） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	FOrderData GetOrderData() const;

	/** 菜单菜品 ID 列表（未合并，与客人点的顺序一致） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	TArray<int32> GetFoodIDs() const;

	/** 菜单列表：按菜品合并数量，含菜品名与图标，蓝图可直接铺 Item 列表 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	TArray<FChairMenuEntry> GetMenuEntries() const;

	/** 套餐名（订单的 SetName，可能为空） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	FString GetSetName() const;

	/** 订单基础价格 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	int32 GetOrderPrice() const;

	/** 订单剩余时间比例：1 = 刚下单，0 = 即将超时（未绑单时为 0） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	float GetRemainPercent() const;

	/** 订单剩余秒数（未绑单时为 0） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	float GetRemainSeconds() const;

	/** 订单图片 ID（与顶部订单 UI 的 IamgeID 同一套取图规则） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	int32 GetOrderImageID() const;

	/** 订单的机器图片 ID 列表（与顶部订单 UI 的 MachineImageID 同一套取图规则） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	TArray<int32> GetMachineImageIDs() const;

	/** 按菜品 ID 取菜品名（取自 MenuDataTable；取不到时返回“菜品<ID>”） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	FString GetFoodName(int32 FoodID) const;

	/** 按菜品 ID 取菜品图标（取自 MenuDataTable 的 Icon；取不到时返回 null） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	UTexture2D* GetFoodIcon(int32 FoodID) const;

	/** 当前摆在挂点上的碟子（成品或空碟，可能为 null） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	AItemActor* GetPlateOnSeat() const;

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 自动刷新累计时间 */
	float RefreshAccum = 0.f;
};
