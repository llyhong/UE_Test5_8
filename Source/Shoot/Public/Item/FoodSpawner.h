// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Item/ItemActor.h"
#include "FoodSpawner.generated.h"

class UWidgetComponent;

/**
 * 生成食物的箱子：从 AItemActor 中抽取出来的独立类（原 EIT_Spawn 类型）。
 * 玩家拾取（按 E）此箱子时，会依据 SpawnItemActorClass 生成一个食材/食物到手上。
 * SpawnItemActorClass 仍保留在基类 AItemActor 上（切菜生成 EIT_SpawnOther 也复用），
 * 本类主要提供“生成箱”的独立类型身份与 OnSpawnActor 蓝图事件。
 */
UCLASS()
class SHOOT_API AFoodSpawner : public AItemActor
{
	GENERATED_BODY()

public:
	AFoodSpawner();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 箱内剩余可生成数量：
	 *  -1 表示无限生成（默认，保持原行为）；
	 *   0 表示已空，不再生成任何食材；
	 *  >0 表示剩余数量，每次拾取生成后减一，减到 0 即停。
	 * 在蓝图实例上按食材种类设置（如番茄箱设 5）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Spawn")
	int32 RemainingCount = -1;

	/** 是否允许本箱产出「野外专属食材」（DT_ItemInfo 行里 bWildOnly=true 的物品）。
	 *  默认 false：野外专属食材只能靠采摘 / 散布 / 打猎获得，食材箱不出（见第 16 节）。
	 *  设为 true 可用于 A/B 对比或临时补料，行为与旧版完全一致。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Spawn")
	bool bAllowWildOnlyItems = false;

	/** 从第几个营业日开始出现（UTimeSystem::Day）。<=1 表示一直可用；
	 *  例如番茄箱设为 2：第 1 天整个箱子隐藏、不可交互，也不算订单系统的食材来源，第 2 天自动出现。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Spawn", meta = (ClampMin = "0"))
	int32 AvailableFromDay = 0;

	/** 当前营业日是否还没到 AvailableFromDay（为真时箱子隐藏、关闭碰撞、不能生成） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Spawn")
	bool IsDayLocked() const;

	/** 是否还能生成：仓库驱动模式下取决于仓库是否有该食材库存；否则原逻辑（无限或剩余>0） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Spawn")
	bool CanSpawn() const;

	/** 成功生成一次后消耗一个数量（无限模式不影响），并写回存档 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Spawn")
	void ConsumeOne();

	/** 补货：把 Quantity 加进有限模式的剩余数量并写回存档（无限模式不变）。
	 *  由仓库在玩家背包入库后调用，让箱子里的食材和仓库库存对应上。 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Spawn")
	void AddStock(int32 Quantity);

	// ==================== 低库存补货提示 ====================

	/** 补货提示控件：挂在箱子上方，库存低于阈值且玩家靠近时显示“需要到野外补货” */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Spawn|RestockTip")
	TObjectPtr<UWidgetComponent> RestockTipComp;

	/** 提示显示距离（cm）：任意本地玩家进入该距离才显示；<=0 表示不限制距离 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Spawn|RestockTip", meta = (ClampMin = "0.0"))
	float RestockTipHideDistance = 600.f;

	/** 提示相对箱子的高度（cm）：按箱子大小微调到模型上方 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Spawn|RestockTip")
	float RestockTipHeight = 130.f;

	/** 当前有效库存：仓库联动模式取仓库库存，否则取 RemainingCount（-1 为无限） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Spawn|RestockTip")
	int32 GetEffectiveStock() const;

	/** 是否低于低库存阈值（UBaseGameInstance::SpawnerLowStockThreshold，默认 3；无限箱永远不低） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Shoot|Spawn|RestockTip")
	bool IsLowStock() const;

	/** 关卡加载时从存档恢复剩余数量（首次则写入初始默认值） */
	virtual void BeginPlay() override;

	/**
	 * 存档用的稳定标识（自动推导，只读）。
	 * 由「关卡名 + Actor 名」做确定性哈希得到：编辑器 / PIE / 打包 exe 三种环境下完全一致，
	 * 因此不依赖只存在于编辑器构建的 Actor GUID（AActor::GetActorGuid 在打包版不存在）。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Spawn|Save", meta = (DisplayName = "存档标识(自动)"))
	FGuid SpawnerGuid;

	/**
	 * 手动指定存档 Key：留空则用「关卡名 + Actor 名」。
	 * 关卡里给箱子改过名字、或箱子是运行时生成的（名字不稳定）时，填一个固定字符串，
	 * 就能保证重命名之后仍然读回同一份存档数据。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Spawn|Save")
	FName SpawnerKeyOverride;

	/** 取存档标识（首次调用时计算并缓存）；本类读写存档都用它，也可在蓝图里查看 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Spawn|Save")
	FGuid GetSpawnerSaveGuid();

	/** 触发生成时广播（蓝图可挂特效/音效，或自行处理生成相关逻辑） */
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Spawn")
	FSpawnActorEvent OnSpawnActor;

	/** 触发生成广播（保持原 AItemActor::OnSpawnActorEvent 的行为） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Spawn")
	void OnSpawnActorEvent();

private:
	/** 拼出存档 Key 文本：SpawnerKeyOverride 优先，否则「关卡名|Actor 名」 */
	FString BuildSaveKeyText() const;

	/** 低库存阈值（取 GameInstance 配置，缺省 3） */
	int32 GetLowStockThreshold() const;

	/** 显示条件（低库存 + 本地玩家靠近）成立时刷出提示，否则隐藏 */
	void UpdateRestockTip();

	/** 按 IsDayLocked 切换箱子的显隐与碰撞（只在状态变化时真正改动） */
	void UpdateDayLock();

	/** 上次应用的天数锁状态：-1 未应用，0 已开放，1 已隐藏 */
	int8 AppliedDayLockState = -1;

	/** 是否有任意本地玩家在提示显示距离内 */
	bool IsLocalPlayerNearRestockTip() const;

	/** 提示标题：食材名 + 当前库存/阈值 */
	FText GetRestockTipTitle() const;

	/** 上次写入提示文本的签名（食材名 + 库存），避免每次检查都刷文本 */
	int32 LastRestockTipSignature = INDEX_NONE;

	/** 两次检查的累计时间（0.2 秒检查一次即可） */
	float RestockTipAccum = 0.f;

	/** 是否已经计算过 SpawnerGuid */
	bool bSpawnerGuidResolved = false;
};
