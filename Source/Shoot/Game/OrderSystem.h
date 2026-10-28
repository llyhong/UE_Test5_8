// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Game/DataClass.h"
#include "GameFramework/Actor.h"
#include "OrderSystem.generated.h"

class AGuestCharacter;

 UCLASS(BlueprintType, Blueprintable)
class UOrderHandle : public UObject
{
	GENERATED_BODY()
public:
	/** 订单数据（按值保存副本，不依赖 ULevelOrderCfg 资产生命周期，避免悬空指针） */
	FOrderData OrderData;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	float waitTime;
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	int orderId;
	/** 下单的客人：客人点餐产生的订单会绑定客人；定时自动生成的订单为 null */
	UPROPERTY(BlueprintReadWrite, Category = "Order")
	TObjectPtr<AGuestCharacter> Guest = nullptr;
	UFUNCTION(BlueprintCallable)
	float GetPercent();
	UFUNCTION(BlueprintCallable)
	void Reset();

	/** 订单展示数据（副本，蓝图用本函数取） */
	UFUNCTION(BlueprintPure, Category = "Order")
	FOrderData GetOrderData() const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOrderEvent2,const UOrderHandle*, Handle,const FOrderData&,Order); 
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
	UFUNCTION(BlueprintCallable)
	bool PushFoods(const TArray<int>& FoodIDs);

	/**
	 * 针对性上菜：仅完成绑定到指定客人的、且菜品集合匹配的订单（方案B 玩家端菜走到客人面前上菜）。
	 * @return 是否成功完成该客人的订单
	 */
	UFUNCTION(BlueprintCallable, Category = "Order")
	bool PushFoodsForGuest(const TArray<int>& FoodIDs, AGuestCharacter* TargetGuest);
	UFUNCTION(BlueprintCallable)
	void InitCfg(ULevelOrderCfg* cfg);
	UFUNCTION(BlueprintCallable)
	void StartSystem();
	UFUNCTION(BlueprintCallable)
	void StopSystem();

	/** 静默清空所有在制订单（不打超时罚分）：用于切换关卡时的“打烊清场” */
	UFUNCTION(BlueprintCallable, Category = "Order")
	void ClearAllOrders();

	/**
	 * 供客人点餐调用：从订单池随机取一个订单，绑定到该客人并入队（产生一个等待订单）。
	 * @return 新订单句柄；订单系统未启动 / 无可用订单配置时返回 nullptr
	 */
	UFUNCTION(BlueprintCallable, Category = "Order")
	UOrderHandle* RequestOrderForGuest(AGuestCharacter* Guest);

	/** 取消某客人尚未完成的订单（客人提前离场时调用），返回是否取消了订单 */
	UFUNCTION(BlueprintCallable, Category = "Order")
	bool CancelOrderForGuest(AGuestCharacter* Guest);

	/** 是否仍按 AppearTimes 定时自动生成订单。关闭后订单只在客人点餐时产生 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Order")
	bool bAutoGenerateByTime = false;
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	TObjectPtr<ULevelOrderCfg> orderCfg;
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	TArray<UOrderHandle*> waitList;
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	int CurOrderIndex = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	float CurTime = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Gameplay")
	bool IsStarted = false;
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOrderEvent2 OnFinishOrder;
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOrderEvent2 OnStartOrder;
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOrderEvent2 OnFailOrder;

	/** 最近一次 PushFoods 成功时实际服务的客人（供上菜台把成品送到客人位置；调用后立即读取，非持久状态） */
	AGuestCharacter* LastServedGuest = nullptr;

	/** 读取最近一次 PushFoods 服务的客人（无则为 nullptr） */
	AGuestCharacter* GetLastServedGuest() const { return LastServedGuest; }

private:
	/** 订单 ID 自增器（原先是文件作用域全局变量，改为成员以避免多实例/多世界共享状态与外部链接污染） */
	int32 OrderID = 0;
};
