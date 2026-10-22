// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/OrderSystem.h"
#include "OrderWidget.generated.h"

class UEconomySystem;

/**
 * 订单 UI 基类：显示当前等待中的订单（顶部订单条 / 订单列表等）。
 *
 * 使用方式（编辑器内）：
 *   1. 用 WBP 继承本类（例如把 UI_TopOrderTips / UI_OrderTips 的父类改成它）；
 *   2. 把该 WBP 配到 BP_RobotPlayerController 的 OrderWidgetClass；
 *   3. 在蓝图中实现 RefreshOrderList / RefreshOrderProgress / OnOrderAdded 等事件。
 *
 * 绑定是自动的：Widget 创建时会从 GameInstance->curLevelBP->orderSystem 取订单系统；
 * 若此时订单系统还没创建（关卡蓝图稍后才 InitOrderSystem），会在 Tick 中按间隔轮询直到绑定成功。
 *
 * 蓝图侧取订单展示数据：
 *   - Handle->GetOrderData()  -> FOrderData（SetName / FoodIDs / Price / MaxWaitTime / IamgeID ...）
 *   - Handle->GetPercent()    -> 剩余时间比例（1 = 刚下单，0 = 即将超时）
 */
UCLASS()
class SHOOT_API UOrderWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 绑定订单系统并订阅订单事件；会立即刷新一次订单列表 */
	UFUNCTION(BlueprintCallable, Category = "Order|UI")
	void BindOrderSystem(AOrderSystem* InOrderSystem);

	/** 解绑并清空本地订单缓存 */
	UFUNCTION(BlueprintCallable, Category = "Order|UI")
	void UnbindOrderSystem();

	/** 立即清空订单显示（不打事件）：用于切换关卡“打烊”时同步清掉上一关订单 UI */
	UFUNCTION(BlueprintCallable, Category = "Order|UI")
	void ClearAllOrdersUI();

	/** 当前订单系统（可能为空：尚未就绪） */
	UFUNCTION(BlueprintCallable, Category = "Order|UI")
	AOrderSystem* GetOrderSystem() const { return OrderSystem; }

	/** 绑定经济系统以获取/监听声誉与金钱（由创建方调用，如 BaseLevelBP BeginPlay） */
	UFUNCTION(BlueprintCallable, Category = "Order|UI")
	void BindEconomy(UEconomySystem* Eco);

	/** 当前声誉值（蓝图可读，实时） */
	UFUNCTION(BlueprintCallable, Category = "Order|UI")
	int32 GetReputation() const;

	/** 当前金钱值（蓝图可读，实时） */
	UFUNCTION(BlueprintCallable, Category = "Order|UI")
	int32 GetMoney() const;

	/** 声誉变化时通知蓝图（NewValue）：可在订单 UI 上同步显示/刷新声誉 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Order|UI")
	void OnReputationUpdated(int32 NewReputation);

	/** 当前等待中的订单（蓝图只读） */
	UPROPERTY(BlueprintReadOnly, Category = "Order|UI")
	TArray<UOrderHandle*> WaitingOrders;

	/** 订单系统未就绪时的重试间隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order|UI")
	float BindRetryInterval = 0.5f;

	// —— 以下为蓝图实现事件 ——

	/** 订单列表整表刷新（仅在绑定/重新绑定时调用，用于把订单系统已有订单一次性铺到 UI；
	 *  日常新增/完成/失败不再走此整表重建，改用下面的增量事件，性能更好） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Order|UI")
	void RefreshOrderList(const TArray<UOrderHandle*>& Orders);

	/** 每帧刷新：用于更新进度条 / 倒计时（遍历 WaitingOrders，调 Handle->GetPercent()） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Order|UI")
	void RefreshOrderProgress();

	/** 新订单出现（增量）：蓝图据此新增一个订单子控件 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Order|UI")
	void OnOrderAdded(const UOrderHandle* Handle, const FOrderData& Order);

	/** 订单完成（上菜成功，增量）：蓝图据此移除对应订单子控件 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Order|UI")
	void OnOrderFinished(const UOrderHandle* Handle, const FOrderData& Order);

	/** 订单失败（超时，增量）：蓝图据此移除对应订单子控件 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Order|UI")
	void OnOrderFailed(const UOrderHandle* Handle, const FOrderData& Order);

	/** 全部订单清空（打烊 / 解绑时）：蓝图应一次性销毁所有订单子控件，无需遍历 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Order|UI")
	void OnOrdersCleared();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 从订单系统重建本地订单缓存，并通知蓝图整表刷新 */
	void RebuildWaitingOrders();

	/** 尝试从 GameInstance->curLevelBP->orderSystem 绑定（订单系统尚未创建时由 Tick 反复调用） */
	void TryBindPending();

	UFUNCTION()
	void HandleOrderStarted(const UOrderHandle* Handle, const FOrderData& Order);

	UFUNCTION()
	void HandleOrderFinished(const UOrderHandle* Handle, const FOrderData& Order);

	UFUNCTION()
	void HandleOrderFailed(const UOrderHandle* Handle, const FOrderData& Order);

	UPROPERTY()
	TObjectPtr<AOrderSystem> OrderSystem;

	/** 经济系统（用于读取/监听声誉与金钱） */
	UPROPERTY()
	TObjectPtr<UEconomySystem> Economy;

	UFUNCTION()
	void HandleReputationChanged(int32 NewValue, int32 Delta);

private:
	/** Tick 轮询累计时间（订单系统未就绪时使用） */
	float BindRetryAccum = 0.f;
};
