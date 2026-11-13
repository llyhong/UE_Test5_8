// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Game/DataClass.h"
#include "GameFramework/Actor.h"
#include "OrderSystem.generated.h"

class AGuestCharacter;
class UTexture2D;

 UCLASS(BlueprintType, Blueprintable)
class UOrderHandle : public UObject
{
	GENERATED_BODY()
public:
	/** 订单数据（按值保存副本，不依赖 ULevelOrderCfg 资产生命周期，避免悬空指针） */
	FOrderData OrderData;
	UPROPERTY(EditAnywhere,BlueprintReadWrite, Category = "Shoot|Order")
	float waitTime;
	UPROPERTY(EditAnywhere,BlueprintReadWrite, Category = "Shoot|Order")
	int orderId;
	/** 下单的客人：客人点餐产生的订单会绑定客人；定时自动生成的订单为 null */
	UPROPERTY(BlueprintReadWrite, Category = "Shoot|Order")
	TObjectPtr<AGuestCharacter> Guest = nullptr;
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order")
	float GetPercent();
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order")
	void Reset();

	/** 订单展示数据（副本，蓝图用本函数取；不含 Price / MaxWaitTime，见下方两个查询函数） */
	UFUNCTION(BlueprintPure, Category = "Shoot|Order")
	FOrderData GetOrderData() const;

	UFUNCTION(BlueprintPure, Category = "Shoot|Order")
	int32 GetOrderPrice() const;

	/** 基础价格（不含限时野味订单倍率）：订单超时扣分用这个，避免高价单放大惩罚 */
	UFUNCTION(BlueprintPure, Category = "Shoot|Order")
	int32 GetBaseOrderPrice() const;

	/** 备料窗口内追加的超时宽限（秒）：由 AOrderSystem::SetPrepWindowGrace 统一下发，窗口结束归零 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order|PrepWindow")
	void SetPrepWindowGraceSeconds(float Seconds);

	/** 标记为限时野味订单并应用价格倍率（>=1）；1 表示普通订单 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order|Wild")
	void SetWildOrder(float InPriceMultiplier);

	/** 本单是否为限时野味订单 */
	UFUNCTION(BlueprintPure, Category = "Shoot|Order|Wild")
	bool IsWildOrder() const { return bIsWildOrder; }

	UFUNCTION(BlueprintPure, Category = "Shoot|Order")
	int32 GetOrderSettlementPrice() const;

	UFUNCTION(BlueprintPure, Category = "Shoot|Order")
	float GetOrderMaxWaitTime(int32 MenuID = -1) const;

	UFUNCTION(BlueprintPure, Category = "Shoot|Order")
	float GetOrderTimeout() const;

	UFUNCTION(BlueprintPure, Category = "Shoot|Order")
	bool IsLate() const;

	UFUNCTION(BlueprintPure, Category = "Shoot|Order")
	bool IsExpired() const;

	void InitializePatience(const ULevelOrderCfg* Config);

	/** 单道菜的菜名：传入菜单 ID（MenuDataTable 的 FoodID）取该菜的 FMenuData::FoodName。
	 *  不传（MenuID < 0）时返回整单显示名（多道菜用 "+" 连接，如“番茄炒蛋+米饭”） */
	UFUNCTION(BlueprintPure, Category = "Shoot|Order")
	FString GetOrderDisplayName(int32 MenuID = -1) const;

	/** 本单需要的机器图片 ID（实时取各菜品的 FMenuData::MachineID，>0 且去重） */
	UFUNCTION(BlueprintPure, Category = "Shoot|Order")
	TArray<int32> GetMachineImageIDs() const;

	/** 本单的食材图标：取各菜 FMenuData::MenuItems 的 key（食材 ItemID）到 DT_FoodID（FItemActorInfo::Icon）
	 *  查图标；同一食材只出一次（保持首次出现顺序）。实时查表不缓存，换菜品表 / 插表后仍然生效 */
	UFUNCTION(BlueprintPure, Category = "Shoot|Order")
	TArray<UTexture2D*> GetOrderFoodIcons() const;

	/** 强制重新汇总价格 / 等待时间 / 显示名 / 机器：换了订单内容或菜品表后调用 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order")
	void RefreshOrderStats();

private:
	float NormalWaitTime = 0.f;
	float LateServeGraceSeconds = 0.f;
	float LateServePriceMultiplier = 1.f;
	/** 备料窗口追加宽限（不写进存档、不进 InitializePatience，窗口结束由订单系统清零） */
	float PrepWindowGraceSeconds = 0.f;
	/** 限时野味订单价格倍率（1=普通单） */
	float WildPriceMultiplier = 1.f;
	bool bIsWildOrder = false;
	TMap<int32, float> MenuWaitTimes;

	/** 汇总结果缓存：Tick 里每帧都要读等待时间，避免每次都遍历菜品表 */
	mutable int32 CachedPrice = 0;
	mutable float CachedMaxWaitTime = 0.f;
	mutable FString CachedDisplayName;
	mutable TArray<int32> CachedMachineImageIDs;
	mutable bool bStatsCached = false;
	void EnsureStatsCached() const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOrderEvent2,const UOrderHandle*, Handle,const FOrderData&,Order);

/** 限时野味订单发放：供 HUD 提示「需要野外食材」。见 营业忙乱机制配置.md 第 16 节 P0 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWildOrderOfferedSignature, const UOrderHandle*, Handle);
UCLASS()
class SHOOT_API AOrderSystem : public AActor
{
	GENERATED_BODY()

public:	
	// Sets default values for this actor's properties
	AOrderSystem();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	// 关卡/游戏结束：清理未完成订单与委托绑定，避免退出时残留 UObject 引用导致崩溃
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	// Called every frame
	FOrderData* GetRandomOrder();

	/**
	 * 按关卡订单配置（ULevelOrderCfg::Orders，如 Level_01_Order）的顺序取下一单：
	 * 按数组顺序依次发放，跳过 FoodIDs 为空的配置项；一轮发完按 bLoopConfigOrders 决定回卷还是停发。
	 * @return 是否取到订单；orderCfg 为空 / 已发完且不循环时返回 false
	 */
	bool GetNextConfigOrder(FOrderData& OutOrder);
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order")
	bool PushFoods(const TArray<int>& FoodIDs);

	/**
	 * 针对性上菜：仅完成绑定到指定客人的、且菜品集合匹配的订单（方案B 玩家端菜走到客人面前上菜）。
	 * @return 是否成功完成该客人的订单
	 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order")
	bool PushFoodsForGuest(const TArray<int>& FoodIDs, AGuestCharacter* TargetGuest);
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order")
	void InitCfg(ULevelOrderCfg* cfg);
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order")
	void StartSystem();
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order")
	void StopSystem();

	/** 静默清空所有在制订单（不打超时罚分）：用于切换关卡时的“打烊清场” */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order")
	void ClearAllOrders();

	/**
	 * 供客人点餐调用：优先按关卡订单配置（Level_01_Order 等 ULevelOrderCfg::Orders）的顺序取下一单；
	 * 未开启顺序出单或配置里没有订单时，才从随机订单池取。绑定到该客人并入队（产生一个等待订单）。
	 * @return 新订单句柄；订单系统未启动 / 无可用订单时返回 nullptr
	 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order")
	UOrderHandle* RequestOrderForGuest(AGuestCharacter* Guest);

	UFUNCTION(BlueprintPure, Category = "Shoot|Order")
	bool CanAcceptNewOrder() const;

	/** 取消某客人尚未完成的订单（客人提前离场时调用），返回是否取消了订单 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order")
	bool CancelOrderForGuest(AGuestCharacter* Guest);

	/** 是否仍按 AppearTimes 定时自动生成订单。关闭后订单只在客人点餐时产生 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Order")
	bool bAutoGenerateByTime = false;

	/** 客人点餐是否按关卡订单配置（ULevelOrderCfg::Orders，例如 Level_01_Order）的顺序依次发放。
	 *  开启且配置里有订单时只按顺序出单，不再随机；配置为空时仍退回菜品表概率随机池 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Order")
	bool bUseConfigOrderSequence = true;

	/** 配置里的订单发完一轮后是否回到第一单继续发；false 则发完即不再出单（客人点不到单会离场） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Order")
	bool bLoopConfigOrders = true;

	/** 配置订单的顺序游标：下一个客人领取 Orders[CurConfigOrderIndex]；InitCfg 时归零 */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Order")
	int32 CurConfigOrderIndex = 0;

	// ========== 备料窗口 / 限时野味订单（第 16 节 P0） ==========
	/** 备料窗口内是否优先发放限时野味订单（菜品表里勾选 bRequiresWildIngredient 的菜）。
	 *  默认开启；但没有任何野味菜品时静默跳过，行为与旧版完全一致。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Order|Wild")
	bool bEnableWildOrderInPrepWindow = true;

	/** 每个营业日最多发放几单野味订单（0 = 不发放） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Order|Wild", meta = (ClampMin = "0"))
	int32 WildOrderDailyLimit = 3;

	/** 野味订单价格倍率：只影响收益，订单超时扣分仍按基础价 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Order|Wild", meta = (ClampMin = "1.0"))
	float WildOrderPriceMultiplier = 2.f;

	/** 本营业日已发放的野味订单数（运行时只读，跨日自动归零） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Order|Wild")
	int32 WildOrdersIssuedToday = 0;

	/** 当前是否处于备料窗口（由 AGuestManager 下发，运行时只读） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Order|PrepWindow")
	bool bPrepWindowActive = false;

	/** 统一下发备料窗口状态：给在场订单追加宽限，并记住状态供窗口内新接订单继承。
	 *  bWindowActive 与 Seconds 分开传，允许把宽限配成 0 而窗口仍然有效。 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order|PrepWindow")
	void SetPrepWindowGrace(float Seconds, bool bWindowActive);

	/** 限时野味订单发放广播（蓝图挂 HUD 提示「需要野外食材」） */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Order|Wild")
	FOnWildOrderOfferedSignature OnWildOrderOffered;

	/** 订单池：只存"点了哪几道菜"（FoodIDs = MenuDataTable 的 FoodID）。
	 *  价格 / 等待时间 / 显示名 / 机器图片由 UOrderHandle 的查询函数实时按菜品表（FMenuData）派生。
	 *  权重（OrderWeights）来自菜品表的 Probabilitys。 */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Order")
	TArray<FOrderData> OrderPool;

	/** 与 OrderPool 一一对应的随机权重（取自菜品 FMenuData::Probabilitys） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Order")
	TArray<float> OrderWeights;

	/** 重建订单池（InitCfg 自动调用一次；运行时换了菜单表也可手动调用刷新） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Order")
	void RebuildOrderPool();
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Gameplay")
	TObjectPtr<ULevelOrderCfg> orderCfg;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Gameplay")
	TArray<UOrderHandle*> waitList;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Gameplay")
	int CurOrderIndex = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Gameplay")
	float CurTime = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Gameplay")
	bool IsStarted = false;
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Events")
	FOrderEvent2 OnFinishOrder;
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Events")
	FOrderEvent2 OnStartOrder;
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Events")
	FOrderEvent2 OnFailOrder;

	/** 最近一次 PushFoods 成功时实际服务的客人（供上菜台把成品送到客人位置；调用后立即读取，非持久状态） */
	AGuestCharacter* LastServedGuest = nullptr;

	/** 读取最近一次 PushFoods 服务的客人（无则为 nullptr） */
	AGuestCharacter* GetLastServedGuest() const { return LastServedGuest; }

private:
	/** 订单 ID 自增器（原先是文件作用域全局变量，改为成员以避免多实例/多世界共享状态与外部链接污染） */
	int32 OrderID = 0;

	/** 备料窗口当前下发的宽限秒数：窗口内新接的订单也继承这个值 */
	float ActivePrepWindowGrace = 0.f;

	/** 上次发放野味订单时的营业日（UTimeSystem::Day）；跨日自动重置计数 */
	int32 WildOrderDay = INDEX_NONE;

	int32 GetCurrentBusinessDay() const;

	/** 从菜品表里挑一道 bRequiresWildIngredient 的菜组成订单；表里没配时返回 false（静默跳过） */
	bool TryBuildWildOrder(FOrderData& OutOrder, const TSet<int32>& SuppliedItemIDs, const TSet<int32>& AvailableMachineIDs) const;

	/** 收集当前关卡能稳定获得的食材 ItemID（食材箱 / 散布器 / 可采植株 / 猎物掉落，以及这些来源再加工出的物品）。
	 *  订单只用这里有的食材出单，避免“表里加了菜但没摆食材箱 → 客人点了永远做不出来”。 */
	void CollectSuppliedItemIDs(TSet<int32>& OutItemIDs) const;

	/** 一道菜能不能进订单池：菜谱已解锁，且全部食材在当前关卡有来源。OutReason 供日志排查 */
	bool IsMenuServable(int32 FoodID, const TSet<int32>& SuppliedItemIDs, FString& OutReason) const;

	/** 本关是否摆了这道菜需要的机器（FMenuData::MachineID；0 = 通用，不检查）。OutReason 供日志排查 */
	bool IsMenuMachinePresent(int32 FoodID, const TSet<int32>& AvailableMachineIDs, FString& OutReason) const;

	/** 收集本关所有加工机器的 MachineID（做不出的菜不进订单池） */
	void CollectAvailableMachineIDs(TSet<int32>& OutMachineIDs) const;

	/** 最近一次 RebuildOrderPool 收集到的食材来源（野味订单按它过滤，避免每次点餐都扫场景） */
	TSet<int32> CachedSuppliedItemIDs;

	/** 最近一次 RebuildOrderPool 收集到的本关机器 ID */
	TSet<int32> CachedAvailableMachineIDs;

	/** 研究炉解锁新菜谱：重建订单池，新菜立刻可以点到 */
	UFUNCTION()
	void HandleRecipesUnlocked(const TArray<int32>& UnlockedFoodIDs);

	/** 食材解锁：重建订单池，依赖该食材的菜从“做不出来”变成可点 */
	UFUNCTION()
	void HandleIngredientsUnlocked(const TArray<int32>& UnlockedIDs);
};
