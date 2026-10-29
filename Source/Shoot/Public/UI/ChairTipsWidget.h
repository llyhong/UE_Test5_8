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

	/** 菜品名（取自 MenuDataTable 的 FoodName，取不到时退化成"菜品<ID>"） */
	UPROPERTY(BlueprintReadOnly, Category = "Chair|Tips")
	FString FoodName;

	/** 本菜品用到的食材 ItemID 列表（与 ItemIcons 一一对应，即 MenuDataTable 里 MenuItems 的键） */
	UPROPERTY(BlueprintReadOnly, Category = "Chair|Tips")
	TArray<int32> ItemIDs;

	/** 各食材的图标（与 ItemIDs 一一对应，取自菜单表 FoodIcon；某食材没配图标则为 null，蓝图先 IsValid 再设 Image） */
	UPROPERTY(BlueprintReadOnly, Category = "Chair|Tips")
	TArray<TObjectPtr<UTexture2D>> ItemIcons;

	/** 主图标（= 第一个食材的图标）；UI 只显示一个图标时用，未配置时为空 */
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
 *   3. 蓝图里按"变化频率"分别实现两个事件（已拆开，避免每帧重建菜单/图标）：
 *      - OnMenuContentChanged()  【低频】只在菜单内容 / 桌况真的变了时触发一次，
 *                                在这里调 GetMenuEntries() 重建列表与图标（不要放每帧逻辑）。
 *      - OnTipsTick()            【高频】每 AutoRefreshInterval 秒触发一次，
 *                                只在这里更新进度条 / 倒计时等连续变化的东西。
 *      - OnTipsRefreshed()       事件驱动的全量刷新（数据变化 + 手动 RefreshTips 都走这里），
 *                                建议只留状态 / 进度，重建列表请搬到 OnMenuContentChanged。
 *      取数据的函数：
 *      - GetMenuEntries()                           -> 菜单列表（菜品名 + 数量 + 图标，已按菜品合并，带缓存）
 *      - GetSeatState() / GetSeatStateText()        -> 当前桌况（等待点单 / 等待上菜 / 用餐中 / 待收空碟）
 *      - GetFoodIDs() / GetSetName() / GetOrderPrice() -> 订单菜品 ID / 显示名 / 价格
 *      - GetRemainPercent() / GetRemainSeconds()    -> 订单剩余时间（做倒计时条）
 *      - GetOrderIcon() / GetMachineImageIDs()      -> 订单图标（菜品表 MenuIcon）/ 需要的机器 ID
 *   4. 挂点位置：选中 BP_Chair 里的 TipsWidgetComp 直接拖动即可。
 *
 * 刷新时机由 AChairSeat 负责：下单 / 上菜 / 吃完留空碟 / 收走空碟 / 客人入座离开时都会主动调 RefreshTips。
 * 若需要倒计时之类的连续刷新，把 AutoRefreshInterval 设为 >0（默认 0.25 秒）即可（只驱动 OnTipsTick）。
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

	/** 【低频】只在菜单内容（菜品集合 / 数量 / 顺序）或桌况真的变了时触发一次：
	 *  蓝图在这里调 GetMenuEntries() 重建列表与图标。周期刷新（倒计时）不会触发它。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Chair|Tips")
	void OnMenuContentChanged();

	/** 【高频】每 AutoRefreshInterval 秒触发一次：蓝图只在这里更新进度条 / 倒计时 /
	 *  桌况文案等连续变化的东西。不要在这里重建菜单列表（内容真变了会走 OnMenuContentChanged）。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Chair|Tips")
	void OnTipsTick();

	/** 数据有变化时通知蓝图（事件驱动 / 手动刷新时触发，全量）：建议只放状态与进度，
	 *  重建列表放到 OnMenuContentChanged，避免每次事件都重铺图标 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Chair|Tips")
	void OnTipsRefreshed();

	/** 立即刷新一次：内容有变则触发 OnMenuContentChanged，并触发 OnTipsRefreshed；
	 *  由餐桌调用，也可在蓝图里手动调用 */
	UFUNCTION(BlueprintCallable, Category = "Chair|Tips")
	void RefreshTips();

	/** 菜单内容签名（桌况 + 菜品 ID 序列）：仅用于判断"内容是否变了"，蓝图一般不用 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	int32 GetMenuSignature() const;

	/** 丢弃菜单缓存（菜品表 / 物品表在运行时换过图标时调用，下次取值会重建） */
	UFUNCTION(BlueprintCallable, Category = "Chair|Tips")
	void InvalidateMenuCache();

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

	/** 订单数据副本：只有 FoodIDs（= 菜品表的 FoodID）。
	 *  Price / MaxWaitTime / 显示名 / 机器请用 Handle 上的查询函数实时取自菜品表 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	FOrderData GetOrderData() const;

	/** 菜单菜品 ID 列表（未合并，与客人点的顺序一致） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	TArray<int32> GetFoodIDs() const;

	/** 菜单列表：按菜品合并数量，含菜品名与图标，蓝图可直接铺 Item 列表。
	 *  结果带缓存（签名不变时复用同一份，不会重复查表 / 取图标），内容变化由 OnMenuContentChanged 通知 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	TArray<FChairMenuEntry> GetMenuEntries() const;

	/** 本单显示名（由菜品表 FoodName 拼接，多菜用 "+" 连接；可能为空） */
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

	/** 订单图标（取本单第一道菜的成品图标，来自菜品表 MenuIcon；原来用的 IamgeID 已废弃）。
	 *  本单多道菜时，用 GetMenuEntries() 逐条取图标。取不到时返回 null */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	UTexture2D* GetOrderIcon() const;

	/** 订单的机器图片 ID 列表（取各菜品的 FMenuData::MachineID，与顶部订单 UI 同一套取图规则） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	TArray<int32> GetMachineImageIDs() const;

	/** 按菜品 ID 取菜品名（取自 MenuDataTable；取不到时返回"菜品<ID>"） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	FString GetFoodName(int32 FoodID) const;

	/** 按菜品 ID 取成品图标（取自 MenuDataTable 的 MenuIcon；没配时退回第一个食材的图标） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	UTexture2D* GetFoodIcon(int32 FoodID) const;

	/** 按菜品 ID + 食材 ItemID 取该食材的图标（取自物品表 DT_ItemInfo 的 Icon）；取不到时返回 null */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	UTexture2D* GetFoodItemIcon(int32 FoodID, int32 ItemID) const;

	/** 该菜品用到的食材 ItemID 列表（MenuItems 的键，与 GetFoodItemIcon 的 ItemID 对应） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	TArray<int32> GetFoodItemIDs(int32 FoodID) const;

	/** 当前摆在挂点上的碟子（成品或空碟，可能为 null） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Chair|Tips")
	AItemActor* GetPlateOnSeat() const;

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 自动刷新累计时间 */
	float RefreshAccum = 0.f;

private:
	/** 菜单缓存：签名不变就复用，避免高频刷新时反复查菜品表 / 取图标 */
	mutable TArray<FChairMenuEntry> CachedMenuEntries;
	mutable int32 CachedMenuSignature = 0;
	mutable bool bMenuCacheValid = false;

	/** 按当前签名重建缓存（const：只改 mutable 缓存） */
	void RebuildMenuCache(int32 NewSignature) const;

	/** 内容有变化才重建缓存并广播 OnMenuContentChanged；没变则什么都不做 */
	void CheckMenuContentChanged();
};
