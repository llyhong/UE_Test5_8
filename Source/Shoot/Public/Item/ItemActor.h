// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "Interfaces/HoveringInterface.h"
#include "Data/QualityTypes.h"
#include "ItemActor.generated.h"
class USphereComponent;
class ABaseFoodActor;
class AChairSeat;
class AShootCharacter;
class UTexture2D;

UENUM(BlueprintType)
enum class EItemState : uint8
{
	EIS_Idle UMETA(DisplayName = "Idle"),
	EIS_Hovering UMETA(DisplayName = "Hovering"),
	EIS_Equipped UMETA(DisplayName = "Equipped"),
	EIS_Cooking UMETA(DisplayName = "Cooking"),
	EIS_Pickup UMETA(DisplayName = "Pickup"), // 用来标志是不是碟子，用来装菜
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSpawnActorEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRemoveActorEvent); //移除碟子的时候触发事件（由 ADish 使用）
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FChangeAttachActorEvent);

/** 食材解锁状态变化广播（true=已解锁可交互；false=已锁定） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIngredientUnlockStateChanged, bool, bUnlocked);

/** 熟度子阶段变化广播（按每个果实独立）：参数：阶段索引(0=未成熟/1=成熟/2=熟过头), 阶段名 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRipenessStageChanged, int32, StageIndex, FName, StageName);

class UBaseGameInstance;

UCLASS()
class SHOOT_API AItemActor : public AActor, public IHoveringInterface
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AItemActor();

	UFUNCTION(BlueprintCallable, Category="Item")
	void AttachMeshToSocket(USceneComponent* InParent, const FName& InSocketName);

	UFUNCTION(BlueprintCallable, Category="Item")
	void DeAttachMeshToSocket();

	UFUNCTION(BlueprintImplementableEvent, Category="Item")
	void OnSphereOverlapEvent();

	UFUNCTION(BlueprintImplementableEvent, Category="Item")
	void OnSphereEndOverlapEvent();		

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	bool CanInteract();
	
private:
	bool bIsHovering = false;
	EItemState ItemState = EItemState::EIS_Idle;
	bool bSetOutline = false;

	/** 是否在树上结果中（由植物生成果实时开启，采摘/离树时关闭），控制熟度计时是否推进 */
	bool bTrackRipening = false;

	/** 当前熟度子阶段：0=未成熟, 1=成熟, 2=熟过头（仅用于广播去重，避免每帧重复） */
	int32 CurrentRipenessStage = 0;
public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
	UFUNCTION()
	virtual void OnSphereOverlap( UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult & SweepResult) override;
	UFUNCTION()
	virtual void OnSphereEndOverlap( UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex) override;

	UFUNCTION(BlueprintCallable, Category="Item")
	FORCEINLINE EItemState GetItemState() const { return ItemState; }
	
	UFUNCTION(BlueprintCallable, Category="Item")
	FORCEINLINE void SetItemState(EItemState State){ItemState = State;}

	UFUNCTION(BlueprintCallable, Category="Item")
	FORCEINLINE int32 GetMenuFoodID() const { return MenuFoodID; }

	/** 读取物品 ID（取自数据表行名 RowName；行名不是数字时退回行内 ItemID 字段）；未配置或取不到返回 -1。
	 *  研究炉按此 ID 匹配研究配方。 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Item")
	int32 GetItemID() const;

	/** 读取物品图标（取自 ItemInfoRow / DT_ItemInfo 的 Icon）；未配置返回 null */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Item")
	UTexture2D* GetItemIcon() const;

	UFUNCTION(BlueprintCallable, Category="Item")
	FORCEINLINE void SetMenuFoodID(int32 InID) { MenuFoodID = InID; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UStaticMeshComponent* ItemMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	USphereComponent* Sphere;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	USceneComponent* HandIK_LPoint;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	USceneComponent* HandIK_RPoint;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<class UMaterialInterface> OverlayMaterial;

	/** 物品信息数据表行引用（DataTable: ItemInfo） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (RowType = "ItemActorInfo"))
	FDataTableRowHandle ItemInfoRow;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Force")
	float ImpuseIntensity = 800.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Force")
	float ImpuseUpDirectionIntensity = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Force")
	float ImpuseForwadDirectionIntensity = 1.0f;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	TArray<int> FoodIdArray;

	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	TArray<ABaseFoodActor*> FoodArray;

	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	TObjectPtr<AActor> PreAttachActor;

	/** 该成品对应的菜单菜品 ID（由烹饪机生成成品时写入），供上菜台校验是否在菜单中（-1 表示未设置） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	int32 MenuFoodID = -1;

	/** 是否可被玩家拾取：false 时禁用本 ItemActor 的拾取（Pickable）逻辑，仅作为场景普通物件
	 *  （例如挂在树上、由植物统一采摘入背包的果实）。默认 true。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	bool bPickable = true;

	// ========== 成品腐烂（未被玩家持有一段时间后自动销毁） ==========
	/** 是否启用腐烂：被启用后，若成品未被玩家拾取（不在手上），累计超过 DecayTime 则自动销毁。
	 *  玩家"拾取起来"时计时【暂停】（保留已累计时长，不清零）；"放下"后从原值【继续累计】。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Decay")
	bool bEnableDecay = false;

	/** 是否输出调试信息（屏幕消息） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Decay")
	bool bShowDebugInfo = false;

	/** 腐烂时限（秒）：未被玩家持有超过该时长即自动销毁 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Decay", meta = (EditCondition = "bEnableDecay", ClampMin = "0.5"))
	float DecayTime = 30.f;

	/** 临近腐烂提前量（秒）：距腐烂还剩该时长时广播 OnDecayWarning，供蓝图变色/动画（<=0 表示不预警） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Decay", meta = (EditCondition = "bEnableDecay", ClampMin = "0"))
	float DecayWarnLeadTime = 8.f;

	/** 运行时累计的"未被持有"时长（只读；在手上的那一刻会被清零） */
	UPROPERTY(BlueprintReadOnly, Category = "Item|Decay")
	float DecayTimer = 0.f;

	/** 腐烂销毁时广播（蓝图可做淡出/特效后再 Destroy 也可监听做反馈） */
	UPROPERTY(BlueprintAssignable, Category = "Item|Decay")
	FSpawnActorEvent OnDecayed;

	/** 临近腐烂时广播一次（进入警告窗口且尚未通知过），供蓝图做变色/循环动画，如：闪红、冒泡提示。
	 *  玩家捡起（计时暂停）会复位该状态，放下后若再次临近会重新广播。 */
	UPROPERTY(BlueprintAssignable, Category = "Item|Decay")
	FSpawnActorEvent OnDecayWarning;

	/** 由烹饪机在生成成品时调用，开启腐烂并设置时限 */
	UFUNCTION(BlueprintCallable, Category = "Item|Decay")
	void EnableDecay(float InDecayTime);

	/** 当前腐烂进度 0~1（DecayTimer / DecayTime），蓝图可每帧读取做平滑变色 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Item|Decay")
	float GetDecayProgress() const { return DecayTime > 0.f ? FMath::Clamp(DecayTimer / DecayTime, 0.f, 1.f) : 0.f; }

	/** 是否已经广播过临近警告（避免每帧重复广播）；普通成员，不暴露给蓝图 */
	bool bDecayWarned = false;

	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	TSubclassOf<class AItemActor> SpawnItemActorClass;

	UFUNCTION(BlueprintCallable, Category = "Gameplay",
		  meta = (DisplayName = "设置后处理描边"))
	static void SetActorsOutline(AActor* actor,bool bEnable);
	
	void OnClear();

	bool NeedOutLineEffect();
	
	UFUNCTION(BlueprintImplementableEvent, Category="Effect Events")
    void OnGiveEffectEvent();

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FChangeAttachActorEvent OnChangeAttachActorEvent;

	// ========== 食材解锁（关卡达成目标后解锁场景上预先放置的食材，如玉米田里的玉米） ==========
	/** 食材解锁 ID：>=0 表示需要对应 ID 解锁后才能交互（如玉米田里的玉米）；<0 表示无需解锁，默认可交互。
	 *  解锁由 UBaseGameInstance 统一管理（关卡达成目标后 UnlockIngredients）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Unlock")
	int32 IngredientUnlockID = -1;

	/** 运行时是否已解锁（<0 的 ID 永远为 true） */
	UPROPERTY(BlueprintReadOnly, Category = "Item|Unlock")
	bool bIngredientUnlocked = true;

	/** 解锁状态变化广播（true=已解锁可交互；false=已锁定）。蓝图做锁定/解锁视觉，如变灰→变亮、显示“已解锁”飘字 */
	UPROPERTY(BlueprintAssignable, Category = "Item|Unlock")
	FIngredientUnlockStateChanged OnIngredientUnlockStateChanged;

	/** 设置解锁状态并广播（供内部与 GameInstance 解锁事件调用） */
	UFUNCTION(BlueprintCallable, Category = "Item|Unlock")
	void SetIngredientUnlocked(bool bUnlocked);

	/** 监听 GameInstance 的食材解锁广播 */
	UFUNCTION()
	void HandleIngredientsUnlocked(const TArray<int32>& UnlockedIDs);

	// ========== 品质（食材采摘阶段 / 成品由食材最差品质决定） ==========
	/** 品质：普通 / 佳品 / 次品。食材在采摘瞬间由植物按熟度写入；成品由烹饪机取最差食材品质写入。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Quality")
	EIngredientQuality IngredientQuality = EIngredientQuality::Normal;

	/** 读取品质（供上菜结算额外奖励、UI 显示） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Item|Quality")
	EIngredientQuality GetIngredientQuality() const { return IngredientQuality; }

	/** 设置品质 */
	UFUNCTION(BlueprintCallable, Category = "Item|Quality")
	void SetIngredientQuality(EIngredientQuality InQuality) { IngredientQuality = InQuality; }

	// ========== 熟度（素材结果后的“成熟 / 熟过头”子阶段，按每个果实独立计时） ==========
	/** 是否启用熟度：由植物在生成果实时按 PlantInfo 写入 */
	UPROPERTY(BlueprintReadOnly, Category = "Item|Ripeness")
	bool bEnableRipeness = false;

	/** 成熟时间点（秒，距结果起）：此窗口内采摘 = 佳品（供 UI 进度条画刻度） */
	UPROPERTY(BlueprintReadOnly, Category = "Item|Ripeness")
	float RipeTime = 0.f;

	/** 熟过头时间点（秒，距结果起）：此窗口后采摘 = 次品（供 UI 进度条画刻度） */
	UPROPERTY(BlueprintReadOnly, Category = "Item|Ripeness")
	float OverripeTime = 0.f;

	/** 熟度计时（秒）：仅在“在树上结果中”（bTrackRipening=true）时推进 */
	UPROPERTY(BlueprintReadOnly, Category = "Item|Ripeness")
	float RipenessTimer = 0.f;

	/** 由植物在生成果实时写入熟度配置并启动计时（从“未成熟”开始累计） */
	UFUNCTION(BlueprintCallable, Category = "Item|Ripeness")
	void ConfigureAndStartRipening(bool bEnable, float InRipeTime, float InOverripeTime);

	/** 停止熟度计时（采摘离树 / 销毁前调用），此后品质已固定不再变化 */
	UFUNCTION(BlueprintCallable, Category = "Item|Ripeness")
	void StopRipening();

	/** 当前熟度子阶段名（未成熟 / 成熟 / 熟过头），用于 UI 提示该果实的阶段 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Item|Ripeness")
	FName GetRipenessStageName() const;

	/** 熟度进度 0..1（距“熟过头”的进度），供 UI 进度条显示 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Item|Ripeness")
	float GetRipenessProgress() const;

	/** 当前采摘品质：成熟=佳品，熟过头=次品，未成熟/未启用=普通 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Item|Ripeness")
	EIngredientQuality GetCurrentQuality() const;

	/** 熟度子阶段变化广播（供 UI 绑定单个果实做文字/进度刷新） */
	UPROPERTY(BlueprintAssignable, Category = "Item|Ripeness")
	FRipenessStageChanged OnRipenessChanged;

	// ========== 空碟 / 脏碟（方案B 用餐闭环 + 洗碗机回收） ==========
	/** 是否为“桌上空碟”：客人吃完后留下的，占据座位直到玩家按 E 收走 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Plate")
	bool bIsEmptyPlate = false;

	/** 是否为“脏碟”：玩家从桌上收走、拿在手上待交还洗碗机的碟子 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Plate")
	bool bIsDirtyPlate = false;

	/** 空碟所占据的座位（客人进食结束后写入；玩家收走时释放该座位） */
	UPROPERTY()
	TObjectPtr<AChairSeat> OwningSeat = nullptr;

	/** 回收交互球：仅在 bIsEmptyPlate=true 时启用，玩家靠近即注册为可回收目标 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item|Plate")
	TObjectPtr<USphereComponent> CollectSphere;

	/** 玩家按 E 从桌上收走空碟：释放所占座位，并把碟子拿在手上（变成脏碟）。返回是否成功 */
	UFUNCTION(BlueprintCallable, Category = "Item|Plate")
	bool PickUpByPlayer(AShootCharacter* Player);

	UFUNCTION()
	void OnCollectOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnCollectEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};
