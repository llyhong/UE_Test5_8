// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/MenuData.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Interaction/InteractionConfig.h"
#include "CookingMachine.generated.h"
class UWidgetComponent;
class UBoxComponent;
class AItemActor;
class USceneComponent;
class APlateSystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNoParmaEvent);

UCLASS()
class SHOOT_API ACookingMachine : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ACookingMachine();


	
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Food")
	TMap<int32,int32> FoodMaterial; //持有的食材
	/** 本机器 ID：菜品表里 FMenuData::MachineID 与之匹配（或为 0 通用）的菜才能做。
	 *  只要填了这个值，下面手填的 MenuIDsArray 就不再被读取；
	 *  0 = 不按机器过滤，继续沿用 MenuIDsArray（旧配置兼容） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Food")
	int32 MachineID = 0;
	/** 可烹饪菜单（手填兜底：MachineID>0 且菜品表已初始化时不再读取，留空即可） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoot|Food", meta = (RowType = "MenuData"))
	TArray<FDataTableRowHandle> MenuIDsArray;
	UPROPERTY(BlueprintReadWrite, Category = "Shoot|Food")
	int32 TargetMenuID;//目标菜单ID
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Food")
	FMenuData TargetMenuData; //目标菜单
	UPROPERTY(BlueprintReadWrite, Category = "Shoot|Food")
	int32 TargetMateiralCount; // 目标食材数量
	UPROPERTY(BlueprintReadWrite, Category = "Shoot|Food")
	int32 CurFoodCount; //当前食材数量

	TArray<int32> PotentialMenuIDs; //可能烹饪菜单
	
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Food")
	float CurCookingTime = 0;//当前烹煮的时间
	UPROPERTY(BlueprintReadWrite, Category = "Shoot|Food")
	bool IsCooking = false; //烹饪中
	UPROPERTY(BlueprintReadWrite, Category = "Shoot|Food")
	bool IsFull = false; //满食材
	UPROPERTY(BlueprintReadWrite, Category = "Shoot|Food")
	bool IsFinish = false; //完成了
	UPROPERTY(BlueprintReadWrite, Category = "Shoot|Food")
	bool OnFire = false; //着火
	UPROPERTY(BlueprintReadWrite, Category = "Shoot|Food")
	bool Repairing = false; //维修中
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Food")
	float CurRepairTime = 0; //维修时间
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Food")
	float OverCookTime = 0; //煮糊的时间
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Food")
	float GreenCookTime = 999; //当前食材需要的时间
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Food")
	float MaxOverCookTime = 7.0;//最少超时时间
	UPROPERTY(EditAnywhere,BlueprintReadWrite, Category = "Shoot|Food")
	float TargetRepairTime = 5;//需要修理的时间

	// ========== 烹饪速度（受“高级灶台”道具影响） ==========
	/** 每持有 1 个“高级灶台”(道具ID=1002) 减少的烹饪时间比例（0.15 = 减 15%） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Food")
	float CookSpeedBonusPerStove = 0.15f;
	/** 烹饪速度倍率下限（防止购买过多灶台后时间归零/负数） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Food")
	float MinCookSpeedMultiplier = 0.3f;

	/** 当前烹饪速度倍率（持有越多高级灶台越快，运行时动态计算） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Food")
	float GetCookSpeedMultiplier() const;
	/** 当前这道菜实际需要的总烹饪时间（已乘速度倍率） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Food")
	float GetRequiredCookTime() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Food Events")
	void OnAddFoodMateiral();
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Food Events")
	void OnOverCookEvent();
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Food Events")
	void OnCookingFinishEvent();
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Food Events")
	void OnStartCookingEvent();
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Food Events")
	void OnRepairFinish();
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Food Events")
	void OnGetFood();
	
	UFUNCTION(BlueprintCallable, Category = "Shoot|Food")
	bool AddFoodMateral(int32 FoodMaterialID); //添加食材
	UFUNCTION(BlueprintCallable, Category = "Shoot|Food")
	bool CanAddFoodMateral(int32 FoodMaterialID);//能否添加食材
	UFUNCTION(BlueprintCallable, Category = "Shoot|Food")
	int GetMenuFood();//取餐



	UFUNCTION()
	virtual void OnBoxOverlap( UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult & SweepResult);
	UFUNCTION()
	virtual void OnBoxEndOverlap( UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
	
	UPROPERTY(VisibleAnywhere, Category = "Shoot|Food")
	UBoxComponent* BoxCollision;

	// ============ 交互判定尺度（统一标准，见 InteractionConfig.h） ============
	/** 交互盒半尺寸。C++ 在 OnConstruction / BeginPlay 强制应用，覆盖蓝图残留的默认值
	 *  （引擎默认只有 32 立方，站在灶台端头就够不到）。
	 *  水平方向用 MachineBox（灶台专用，比统一基准再紧 40%）；
	 *  长条台面 / 双灶这类宽度大的设备，请在自己蓝图里把 X/Y 调大，别去动 InteractionConfig。
	 *  Z 仍是 MachineHalfHeight：竖直方向必须覆盖 0~180，缩了会出现“站着按不到”。 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Interaction")
	FVector InteractionBoxExtent = FVector(
		InteractionDistance::MachineBox,
		InteractionDistance::MachineBox,
		InteractionDistance::MachineHalfHeight);

	/** 交互盒中心抬高量：让盒体覆盖 0~180，正好是角色胶囊全高 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Interaction")
	float InteractionCenterHeight = InteractionDistance::MachineHalfHeight;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Machine")
	UStaticMeshComponent* ItemMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Machine")
	UWidgetComponent* TipsWidget;

	// ========== 烹饪完成后生成成品 ==========

	/** 开关：烹饪完成后是否自动生成成品 Actor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Spawn")
	bool bSpawnOnCookingFinish = false;

	/** 生成成品时是否消耗一个可用碟子。
	 *  洗碗机闭环：只有存在"空闲碟子"时才自动生成成品；
	 *  没有空闲碟子则本次不出成品，走回原始逻辑（保持完成状态继续加热 → 煮糊/着火），
	 *  需玩家把桌上的脏碟收走并交还洗碗机清洗（+1）后，下一次烹饪才能正常出菜。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Spawn")
	bool bConsumePlateOnFinish = true;

	/** 没有空闲碟子、本次不出成品时通知（蓝图可播放提示/冒烟等）。同一锅只触发一次。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Spawn")
	void OnNoPlateAvailable();

	/** 烹饪完成后生成的默认 Actor 类（菜品行的 CookedActorClass 未配置时兜底使用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Spawn")
	TSubclassOf<AActor> CookedActorClass;

	/** 生成位置标记（可在蓝图中摆放到指定位置） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Spawn")
	USceneComponent* SpawnPoint;

	/** 已生成的成品堆栈（按生成顺序叠放，用于计算下一次的叠高位置；元素被销毁后自动置空） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Spawn")
	TArray<TObjectPtr<AActor>> CookedActorStack;

	/** 每层成品之间的额外间隔 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Spawn")
	float StackSpacing = 2.0f;

	/** 叠高上限层数（0 = 不限制，达到上限后不再生成） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Spawn")
	int32 MaxStackCount = 0;

	/** 成品（菜品）是否启用腐烂：未被玩家拾取的成品超过 ProductDecayTime 后自动销毁 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Spawn")
	bool bProductsDecay = true;

	/** 成品腐烂时限（秒）：未被玩家持有超过该时长即自动销毁（放下后重新计时） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Spawn", meta = (EditCondition = "bProductsDecay", ClampMin = "1"))
	float ProductDecayTime = 30.f;

	/** 清空成品堆栈记录（不会销毁已有 Actor） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Spawn")
	void ClearCookedStack();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** 编辑器里改 InteractionBoxExtent 时立即写进组件，关卡视口中即可看到真实判定盒 */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 把 InteractionRadius（× Shoot.InteractScale 倍率）写进交互组件；OnConstruction/BeginPlay/运行时改倍率都会调用，必须幂等 */
	UFUNCTION()
	void ApplyInteractionScale();

	void ResetFoodData();
	/** 收集本机可做的菜品行：MachineID>0 时按菜品表的 MachineID 过滤（菜品 MachineID=0 视为通用）；
	 *  MachineID<=0 或菜品表未就绪时退回手填的 MenuIDsArray */
	void CollectCandidateMenus(TArray<FMenuData>& OutMenus) const;
	bool FindMatchMenu(int32 FoodMaterialID, FMenuData& matchMenuData);
	bool CheckMenu(int32 FoodMaterialID, FMenuData& matchMenuData);

	/** 烹饪完成后生成成品 Actor（受 bSpawnOnCookingFinish 开关控制） */
	void SpawnCookedActor();

	/** 取盘子系统：优先关卡蓝图上的实例，否则在场景中查找第一个 */
	APlateSystem* FindPlateSystem() const;

	/** “没有空闲碟子”提示是否已提示过（同一锅只提示一次，避免刷屏；ResetFoodData 时复位） */
	bool bNoPlateNotified = false;

private:
	ULocalPlayer* LocalPlayer;
	
public:	
	TArray<AItemActor*> FoodActors; //存锅里的食物，煮饭删掉
};
