// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Animation/AnimMontage.h"
#include "Interfaces/PickupInterface.h"
#include "Logging/LogMacros.h"
#include "Item/Weapon.h"
#include "Item/ItemActor.h"
#include "ShootCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UAnimMontage;
class AItemActor;
class AFoodSpawner;
class ADish;
class ADesk;
class AFoodSpawnOther;
class ACookingMachine;
class UHUDWidget;
class UInventoryWidget;
class UShopWidget;
class AAnimalCharacter;
class UShootAnimInstance;
class APlant;
class AAttractCounter;
class AGuestCharacter;
class ADishwasher;
class AChairSeat;
class APestActor;
class UInteractionDialogueComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class UWidgetComponent;
struct FInputActionValue;



// 背包发生变化时广播(供 UI 刷新)
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryChanged);

// 背包槽位：仅记录物体的"类型/信息"。入包后世界中的物体会被销毁，
// 取出时根据 ItemClass 重新生成到角色手上(AItemActor 及其子类)。
USTRUCT(BlueprintType)
struct FInventorySlot
{
	GENERATED_BODY()

public:
	// 取出时用于生成对应 Actor 的类(保留蓝图子类)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Inventory")
	TSubclassOf<AItemActor> ItemClass = nullptr;

	// 物品信息数据表行(取出时回填到新生成的 Actor)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Inventory", meta = (RowType = "ItemActorInfo"))
	FDataTableRowHandle ItemInfoRow;

	// 食材ID列表(如碟子里的菜)，取出时回填
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Inventory")
	TArray<int> FoodIdArray;

	// 显示名(UI 用)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Inventory")
	FString DisplayName;

	// 数量：同类物品堆叠(相同 ItemClass 不占新槽位，仅数量+1)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Inventory")
	int32 Quantity = 1;
};

UENUM(BlueprintType)
enum class ECharacterState : uint8
{
	ECS_Idle UMETA(DisplayName = "Idle"),
	ECS_Holding UMETA(DisplayName = "Holding"),
	ECS_Repairing UMETA(DisplayName = "Repairing"),
	ECS_Dead UMETA(DisplayName = "Dead"),
};
UENUM(BlueprintType)
enum class ECharacterActionState : uint8
{
	EAS_Unoccupied UMETA(DisplayName = "Unoccupied"),
	EAS_Attacking UMETA(DisplayName = "Attacking"),
	EAS_Picking UMETA(DisplayName = "Picking"),
	EAS_Dash UMETA(DisplayName = "Dash")
};
UCLASS(abstract)
class AShootCharacter : public ACharacter, public IPickupInterface
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
private:
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<AItemActor> OverlappingItem;

	// 交互范围内的全部物品候选集（进入时注册、离开时注销），
	// OverlappingItem 始终指向其中距离最近的一个，取代旧的"单指针后进者胜"
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TArray<TObjectPtr<AItemActor>> OverlappingItems;

	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<AItemActor> PickingItem;

	// ============ 放下后的重拾锁定 ============
	// 刚放下的物品在 DropRePickLockTime 秒内不可再被拾取，
	// 避免连按 E 出现"拾取↔放下"抖动
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Pickup")
	float DropRePickLockTime = 0.3f;

	TObjectPtr<AItemActor> LastDroppedItem = nullptr;
	float LastDropTime = -1000.f;

	bool IsDropLockActive(AItemActor* Item) const;

	// 当前可交互的植物（靠近时由 APlant 的重叠球注册）
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<APlant> OverlappingPlant;

	// 植物候选集：进入时注册、离开时注销，OverlappingPlant 取其中距离最近的一株
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TArray<TObjectPtr<APlant>> OverlappingPlants;

	// 灶台候选集：同上，由 ACookingMachine 注册/注销
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TArray<TObjectPtr<ACookingMachine>> OverlappingCookMachines;

	// 以下 7 类交互目标统一采用"候选集 + 最近目标"：
	// 物体进入范围时 Add 自己、离开时 Remove 自己，单指针始终指向候选里
	// "视线可达且距离最近"的那一个，不再有"后进者胜"和"先离开者清空目标"。

	// 上菜台候选集
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TArray<TObjectPtr<class AServingCounter>> OverlappingServingCounters;

	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<class AServingCounter> OverlappingCounter;

	// 洗碗机候选集
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TArray<TObjectPtr<class ADishwasher>> OverlappingDishwashers;

	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<class ADishwasher> OverlappingDishwasher;

	// 招揽台候选集
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TArray<TObjectPtr<class AAttractCounter>> OverlappingAttractCounters;

	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<class AAttractCounter> OverlappingAttractCounter;

	// 大门交互台候选集
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TArray<TObjectPtr<class AGateCounter>> OverlappingGates;

	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<class AGateCounter> OverlappingGate;

	// 客人候选集
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TArray<TObjectPtr<class AGuestCharacter>> OverlappingGuests;

	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<class AGuestCharacter> OverlappingGuest;

	// 餐桌候选集
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TArray<TObjectPtr<class AChairSeat>> OverlappingChairSeats;

	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<class AChairSeat> OverlappingChairSeat;

	// 当前可收走的空碟（靠近空碟时由空碟的回收球注册；方案B 上菜闭环，收走后变成手上脏碟）
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TArray<TObjectPtr<class AItemActor>> OverlappingEmptyPlates;

	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<class AItemActor> OverlappingEmptyPlate;

	// 当前可投入食材的研究炉（靠近时由研究炉的重叠球注册）
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TArray<TObjectPtr<class AResearchStove>> OverlappingResearchStoves;

	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<class AResearchStove> OverlappingResearchStove;

	// 当前可存物/取物的仓库（靠近时由仓库的重叠球注册；用于 E 键把背包物品存入仓库）
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TArray<TObjectPtr<class AStorageWarehouse>> OverlappingWarehouses;

	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<class AStorageWarehouse> OverlappingWarehouse;

	// 当前可对话的交互对象（靠近时由 UInteractionDialogueComponent 注册；通用对话系统）
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TArray<TObjectPtr<class UInteractionDialogueComponent>> OverlappingDialogues;

	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<class UInteractionDialogueComponent> OverlappingDialogue;

	// 当前可抓捕的害虫（老鼠等突发事件生成；靠近时由害虫的交互球注册）
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<class APestActor> OverlappingPest;

	// ============ 背包系统 ============
	// 背包槽位：只记录物体的类型/信息，入包后世界中的物体会被销毁，取出时按类型重新生成
	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Inventory")
	TArray<FInventorySlot> InventoryItems;

	// 背包容量上限(<=0 表示不限制)
	UPROPERTY(EditAnywhere, Category = "Shoot|Inventory")
	int32 InventoryCapacity = 0;

	// 背包界面 Widget 类(蓝图继承 UInventoryWidget)
	UPROPERTY(EditAnywhere, Category = "Shoot|Inventory")
	TSubclassOf<UUserWidget> InventoryWidgetClass;

	// 当前打开的背包 Widget 实例(仅持有一个，关闭时不销毁，再次打开复用)
	UPROPERTY()
	TObjectPtr<UInventoryWidget> InventoryWidget;

	// 商店界面 Widget 类(蓝图继承 UShopWidget)
	UPROPERTY(EditAnywhere, Category = "Shoot|Shop")
	TSubclassOf<UUserWidget> ShopWidgetClass;

	// 当前打开的商店 Widget 实例(仅持有一个，关闭时不销毁，再次打开复用)
	UPROPERTY()
	TObjectPtr<UShopWidget> ShopWidget;

	UPROPERTY(EditAnywhere, Category = "Shoot|Weapon")
	AWeapon* EquippedWeapon;

	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Character")
	TObjectPtr<ACookingMachine> CookingMachine;

	void PlayPickMontage(FName SelectionName);

	/** 播放采集（摘取植被）动画，返回是否成功开始播放 */
	bool PlayGatheringMontage();

	/** 播放浇水动画，返回是否成功开始播放 */
	bool PlayWateringMontage();

	/** 采集蒙太奇结束回调：此时才真正收获并删除植物 */
	UFUNCTION()
	void GatheringMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/** 浇水蒙太奇结束回调：此时才真正补水并取消描边 */
	UFUNCTION()
	void WateringMontageEnded(UAnimMontage* Montage, bool bInterrupted);

bool CanAttack(); 
	bool IsHolding();
	bool CanPick(); //可以捡
	bool IsSpawner();//是否是生产raw食物的箱子
	bool IsSpawnOther();//是否从raw已经切好成菜，
	bool NeedDropDown();//放下食物
	bool CanThrowToMachine(); // 是否可以将菜扔进锅
	bool NeedRepairMachine(); // 是否需要修理锅
	bool IsRepairing();       //正在修理ing
	bool MachineCookFinished(); //锅煮好菜
	bool CanGiveDishToGuest();  //是否可以上菜给客人

	bool IsPicking = false;
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input")
	UInputAction* MouseLookAction;

	/** Attack Input Action */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input")
	UInputAction* AttackAction;

	/** 拾取 Input Action：在招揽台范围内按下也会触发招揽（与 AttractAction 合并，以本键为准） */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input")
	UInputAction* PickAction;
	
	UPROPERTY(EditAnywhere, Category = "Shoot|Input")
	UInputAction* PushFoodAction;//扔食物

	UPROPERTY(EditAnywhere, Category = "Shoot|Input")
	UInputAction* WheelAction;//

	/** 放入背包 Input Action (Q 键) */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input")
	UInputAction* StoreToInventoryAction;

	/** 从背包取出(生成到手上) Input Action */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input")
	UInputAction* TakeOutInventoryAction;

	/** 打开/关闭背包界面 Input Action (B 键) */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input")
	UInputAction* OpenInventoryAction;

	/** 打开/关闭商店界面 Input Action (P 键) */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input")
	UInputAction* OpenShopAction;

	/** 冲刺 Input Action (LeftShift)：按住加速移动，松开恢复 */
	UPROPERTY(EditAnywhere, Category = "Shoot|Input")
	UInputAction* RushAction;

	// ============ 冲刺 / 速度 ============
	/** 常规行走速度（RushEnd 恢复到此值） */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Movement")
	float WalkSpeed = 600.f;

	/** 冲刺速度 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Movement")
	float SprintSpeed = 900.f;

	/** 常规最大加速度（起步跟手程度） */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Movement")
	float WalkAcceleration = 3000.f;

	/** 冲刺最大加速度：起步/转向时更快把实际速度拉到冲刺速
	 *  （600→900 约 0.06s，观感接近瞬时但不产生速度阶跃） */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Movement")
	float SprintAcceleration = 5200.f;

	/** 常规转向速率（Yaw） */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Movement")
	float WalkRotationRate = 500.f;

	/** 冲刺转向速率（Yaw）：高速下转向更跟手 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Movement")
	float SprintRotationRate = 720.f;

	/** 起步脉冲：当帧把水平速度直接推到冲刺速度（速度阶跃）。
	 *  注意：阶跃会让弹簧臂相机“先被甩开再追上来”，表现就是镜头猛地往前一突，
	 *  因此默认关闭；需要的那点跟手感交给 SprintAcceleration 平滑加速完成。 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Movement")
	bool bSprintInstantBurst = false;

	/** 相机跟随速度（越大越贴住角色）。
	 *  冲刺前后必须保持同一值——LagSpeed 一变，滞后量就跳变，镜头会前后抽动。 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Movement")
	float CameraLagSpeed = 35.f;

	/** 相机允许的最大滞后距离（0 = 不限制）。
	 *  保持 0：LagSpeed 已固定，稳态滞后只有一二十厘米，无需夹紧；
	 *  一旦设小值，滞后量会每帧撞上限被硬夹住，反而产生顿感。 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Movement")
	float CameraLagMaxDistance = 0.f;

	/** 是否启用冲刺 FOV 拉伸。关掉后镜头只提速不变焦，最不容易晕 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Movement")
	bool bUseSprintFOV = false;

	/** 非冲刺时的相机 FOV */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Movement")
	float NormalFOV = 90.f;

	/** 冲刺时的相机 FOV（小幅拉大做速度感，过大容易晕） */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Movement")
	float SprintFOV = 92.f;

	/** FOV 过渡速度：越大越快到位 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Movement")
	float FOVInterpSpeed = 10.f;

	/** 当前相机目标 FOV（Tick 中插值逼近） */
	float TargetFOV = 90.f;

	bool bIsSprinting = false;


public:

	/** Constructor */
	AShootCharacter();	

public:
	
	UFUNCTION(BlueprintCallable, Category = "Shoot|PickupInterface")
	virtual void SetOverlappingItem(AItemActor* Item) override;

	// 物品进入交互范围：加入候选集并重选最近目标
	UFUNCTION(BlueprintCallable, Category = "Shoot|PickupInterface")
	virtual void AddOverlappingItem(AItemActor* Item) override;

	// 物品离开交互范围：从候选集移除并重选最近目标
	UFUNCTION(BlueprintCallable, Category = "Shoot|PickupInterface")
	virtual void RemoveOverlappingItem(AItemActor* Item) override;

	// 从候选集中选出距离最近的物品作为 OverlappingItem（同步描边与 HUD）
	void RefreshBestOverlappingItem();

	// ============ 灶台候选集 ============
	// 进入/离开时由 ACookingMachine 调用；CookingMachine 始终指向候选里最近的一台
	UFUNCTION(BlueprintCallable, Category = "Shoot|PickupInterface")
	virtual void AddOverlappingCookMachine(ACookingMachine* Machine) override;

	UFUNCTION(BlueprintCallable, Category = "Shoot|PickupInterface")
	virtual void RemoveOverlappingCookMachine(ACookingMachine* Machine) override;

	void RefreshBestCookMachine();

	// ============ 植物候选集 ============
	// 进入/离开时由 APlant 调用；OverlappingPlant 始终指向候选里最近的一株
	UFUNCTION(BlueprintCallable, Category = "Shoot|PickupInterface")
	virtual void AddOverlappingPlant(APlant* Plant) override;

	UFUNCTION(BlueprintCallable, Category = "Shoot|PickupInterface")
	virtual void RemoveOverlappingPlant(APlant* Plant) override;

	void RefreshBestPlant();

	// ============ 其余交互目标候选集（上菜台/洗碗机/招揽台/大门/客人/餐桌/研究炉） ============
	// 这七类都由各自的交互球直接调用角色的 Add/Remove，不再走"单指针覆盖"。
	// 语义与物品/灶台/植物一致：只登记自己，选谁由角色按"视线可达 + 距离最近"决定。
	void AddOverlappingCounter(class AServingCounter* Counter);
	void RemoveOverlappingCounter(class AServingCounter* Counter);
	void RefreshBestCounter();

	void AddOverlappingDishwasher(class ADishwasher* Dishwasher);
	void RemoveOverlappingDishwasher(class ADishwasher* Dishwasher);
	void RefreshBestDishwasher();

	void AddOverlappingAttractCounter(class AAttractCounter* Counter);
	void RemoveOverlappingAttractCounter(class AAttractCounter* Counter);
	void RefreshBestAttractCounter();

	void AddOverlappingGate(class AGateCounter* Gate);
	void RemoveOverlappingGate(class AGateCounter* Gate);
	void RefreshBestGate();

	void AddOverlappingGuest(class AGuestCharacter* Guest);
	void RemoveOverlappingGuest(class AGuestCharacter* Guest);
	void RefreshBestGuest();

	void AddOverlappingChairSeat(class AChairSeat* Seat);
	void RemoveOverlappingChairSeat(class AChairSeat* Seat);
	void RefreshBestChairSeat();

	void AddOverlappingResearchStove(class AResearchStove* Stove);
	void RemoveOverlappingResearchStove(class AResearchStove* Stove);
	void RefreshBestResearchStove();

	// 空碟 / 仓库 / 对话目标：同样走候选集，避免"多张桌子空碟互相抢目标"
	void AddOverlappingEmptyPlate(class AItemActor* Plate);
	void RemoveOverlappingEmptyPlate(class AItemActor* Plate);
	void RefreshBestEmptyPlate();

	void AddOverlappingWarehouse(class AStorageWarehouse* Warehouse);
	void RemoveOverlappingWarehouse(class AStorageWarehouse* Warehouse);
	void RefreshBestWarehouse();

	void AddOverlappingDialogue(class UInteractionDialogueComponent* Comp);
	void RemoveOverlappingDialogue(class UInteractionDialogueComponent* Comp);
	void RefreshBestDialogue();

	// ============ 交互视线与统一刷新 ============
	/** 视线校验：角色眼睛到目标（用包围盒上离眼睛最近的点）之间被墙/台面挡住则返回 false。
	 *  解决“站在墙另一边也能浇水/投料”的问题 */
	bool HasInteractionLineOfSight(const AActor* Target) const;

	/** 定期重估三个候选集的最优目标：玩家移动、绕到墙后、物体被搬走都能及时反映 */
	void RefreshAllInteractionTargets();

	// ============ 交互提示 ============
	struct FInteractOption
	{
		int32 Tier = 2;
		/** 真实距离（用于显示与调试） */
		float Dist = FLT_MAX;
		/** 排序用的"朝向加权距离"：背后目标会被放大，正前方不放大 */
		float Score = FLT_MAX;
		FString Name;
		/** 该动作作用于哪个 Actor；没有具体目标（如"放下"）时为 nullptr */
		AActor* Target = nullptr;
		TFunction<bool()> Run;
	};

	// ============ 交互朝向 ============
	/** 交互判定用的朝向（水平化）：优先角色脸朝向，退化到控制器/相机朝向 */
	FVector GetInteractionFacing() const;

	/** 角色到目标的真实交互距离：用包围盒最近点，大件设备不会因为 pivot 远而被算远 */
	float GetInteractionDistanceTo(const AActor* Target) const;

	/** 朝向加权距离：真实距离 × (1 + 权重 × 偏离度)。
	 *  背后 40cm 的蔬菜（≈97）会输给正前方 90cm 的那个，避免按 E 时莫名转身 */
	float GetFacingWeightedDistance(const AActor* Target) const;

	/** 收集当前所有可执行的交互（分层 + 距离），不执行任何副作用 */
	void BuildInteractionOptions(TArray<FInteractOption, TInlineAllocator<12>>& OutOptions);

	void RefreshInteractionPromptTarget();
	void UpdateInteractionPrompt();

	UPROPERTY(VisibleInstanceOnly, Category = "Shoot|Interaction")
	TObjectPtr<AActor> InteractionPromptTarget;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Interaction", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWidgetComponent> InteractionPrompt;

	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Interaction")
	bool bEnableInteractionPrompt = true;

	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Interaction")
	float InteractionPromptHeight = 22.f;

	/** 目标重估间隔（秒）：不必每帧，0.15s 足够跟手且几乎无开销 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Interaction")
	float InteractionRefreshInterval = 0.15f;

	float InteractionRefreshTimer = 0.f;

	/** 上一次生效的 Shoot.InteractScale 倍率：变了就重设世界里所有交互球 */
	float LastAppliedInteractionScale = 1.f;
	
	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE AItemActor* GetOverlapItem() const { return OverlappingItem; }

	// 植物交互：靠近时由 APlant 注册/反注册
	UFUNCTION(BlueprintCallable, Category = "Shoot|Plant")
	void SetOverlappingPlant(APlant* Plant);

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE APlant* GetOverlappingPlant() const { return OverlappingPlant; }

	// 上菜台交互：靠近时由 AServingCounter 注册/反注册
	UFUNCTION(BlueprintCallable, Category = "Shoot|Serve")
	void SetOverlappingCounter(AServingCounter* Counter);

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE AServingCounter* GetOverlappingCounter() const { return OverlappingCounter; }

	// 招揽台交互：靠近时由 AAttractCounter 注册/反注册
	UFUNCTION(BlueprintCallable, Category = "Shoot|Attract")
	void SetOverlappingAttractCounter(AAttractCounter* Counter);

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE AAttractCounter* GetOverlappingAttractCounter() const { return OverlappingAttractCounter; }

	// 大门交互：靠近时由 AGateCounter 注册/反注册（用于 E 键开门营业）
	UFUNCTION(BlueprintCallable, Category = "Shoot|Gate")
	void SetOverlappingGate(class AGateCounter* Gate);

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE AGateCounter* GetOverlappingGate() const { return OverlappingGate; }

	// 客人交互：靠近时由 AGuestCharacter 的交互球注册/反注册（用于 E 键接单）
	UFUNCTION(BlueprintCallable, Category = "Shoot|Guest")
	void SetOverlappingGuest(AGuestCharacter* Guest);

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE AGuestCharacter* GetOverlappingGuest() const { return OverlappingGuest; }

	// 餐桌交互：靠近时由 AChairSeat 的交互球注册/反注册（用于 E 键点单 / 上菜 / 收空碟）
	UFUNCTION(BlueprintCallable, Category = "Shoot|Chair")
	void SetOverlappingChairSeat(AChairSeat* Seat);

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE AChairSeat* GetOverlappingChairSeat() const { return OverlappingChairSeat; }

	// 洗碗机交互：靠近时由洗碗机的重叠球注册/反注册（用于 E 键交还脏碟）
	UFUNCTION(BlueprintCallable, Category = "Shoot|Dishwasher")
	void SetOverlappingDishwasher(ADishwasher* Dishwasher);

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE ADishwasher* GetOverlappingDishwasher() const { return OverlappingDishwasher; }

	// 空碟回收：靠近空碟时由空碟的回收球注册/反注册（用于 E 键收走空碟，方案B）
	UFUNCTION(BlueprintCallable, Category = "Shoot|Dish")
	void SetOverlappingEmptyPlate(AItemActor* Plate);

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE AItemActor* GetOverlappingEmptyPlate() const { return OverlappingEmptyPlate; }

	// 研究炉交互：靠近时由研究炉的重叠球注册/反注册（用于 E 键投入食材研究新菜谱）
	UFUNCTION(BlueprintCallable, Category = "Shoot|Research")
	void SetOverlappingResearchStove(AResearchStove* Stove);

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE AResearchStove* GetOverlappingResearchStove() const { return OverlappingResearchStove; }

	// 仓库交互：靠近时由 AStorageWarehouse 的重叠球注册/反注册（用于 E 键把背包物品存入仓库）
	UFUNCTION(BlueprintCallable, Category = "Shoot|Warehouse")
	void SetOverlappingWarehouse(AStorageWarehouse* Warehouse);

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE AStorageWarehouse* GetOverlappingWarehouse() const { return OverlappingWarehouse; }


	// 害虫交互：靠近时由 APestActor 的交互球注册/反注册（用于 E 键抓捕）
	UFUNCTION(BlueprintCallable, Category = "Shoot|Pest")
	void SetOverlappingPest(APestActor* Pest);

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE APestActor* GetOverlappingPest() const { return OverlappingPest; }

	// 通用对话交互：靠近时由 UInteractionDialogueComponent 注册/反注册（用于 E 键对话）
	UFUNCTION(BlueprintCallable, Category = "Shoot|Dialogue")
	void SetOverlappingDialogue(UInteractionDialogueComponent* Comp);

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE UInteractionDialogueComponent* GetOverlappingDialogue() const { return OverlappingDialogue; }

	FORCEINLINE void SetPickingItem(AItemActor* Item){PickingItem = Item;}

	/** 交出/放下手上物品后复位手持状态（清空 PickingItem、动作状态与手部 IK）。
	 *  上菜成功等"物品已交给别人"的分支统一调用，避免各处重复写一长串复位。 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	void ClearHeldItemState();

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE AItemActor* GetPickingItem() const { return PickingItem; }

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	void SetEqiuppedWeapon(AWeapon* Weapon)
	{
		EquippedWeapon = Weapon;
		// 同步设置武器 Actor 的 Owner，供武器在 OnBoxOverlap 中通过 GetOwner() 判断持有者
		if (Weapon)
		{
			Weapon->SetOwner(this);
			Weapon->SetItemState(EItemState::EIS_Equipped);
			Weapon->SetInteractionHighlight(false);
			RemoveOverlappingItem(Weapon);
		}
	}

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE AWeapon* GetEqiuppedWeapon() const { return EquippedWeapon; }

	// ============ 背包系统 API ============
	// 将手中/世界中的物品记录进背包，并销毁该物体(节省世界实例)；成功广播 OnInventoryChanged。
	// 注意：这里记录的是"类型"，取出时按 ItemClass 重新生成。
	UFUNCTION(BlueprintCallable, Category = "Shoot|Inventory")
	bool AddItemToInventory(AItemActor* Item);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Shoot|Harvest")
	TObjectPtr<UNiagaraSystem> HarvestEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Shoot|Harvest")
	FName HarvestEffectSocket = TEXT("ItemSocket");

	UFUNCTION(BlueprintCallable, Category = "Shoot|Harvest")
	void PlayHarvestEffect(FVector SourceLocation);

	void UpdateHarvestEffects();

	TArray<TWeakObjectPtr<UNiagaraComponent>> ActiveHarvestEffects;

	// 直接把一条背包槽位记录追加进背包（按 ItemClass 堆叠），并广播 OnInventoryChanged。
	// 用于仓库取回等场景（不需要一个真实 Actor，仅搬运轻量记录）。
	UFUNCTION(BlueprintCallable, Category = "Shoot|Inventory")
	bool AddSlotToInventory(const FInventorySlot& Slot);

	// 按槽位索引从背包移除(仅删记录，不会生成物体)；索引无效则失败。
	UFUNCTION(BlueprintCallable, Category = "Shoot|Inventory")
	bool RemoveItemFromInventory(int32 SlotIndex);

	// 背包是否包含该槽位
	UFUNCTION(BlueprintCallable, Category = "Shoot|Inventory")
	bool HasItemInInventory(int32 SlotIndex) const;

	// 背包槽位列表(只读)
	UFUNCTION(BlueprintCallable, Category = "Shoot|Inventory")
	const TArray<FInventorySlot>& GetInventoryItems() const { return InventoryItems; }

	// 背包当前数量
	UFUNCTION(BlueprintCallable, Category = "Shoot|Inventory")
	int32 GetInventoryCount() const { return InventoryItems.Num(); }

	// 背包是否已满(InventoryCapacity > 0 且已达上限)
	UFUNCTION(BlueprintCallable, Category = "Shoot|Inventory")
	bool IsInventoryFull() const;

	// 把当前手上的物品(PickingItem)记录进背包并销毁之(手上无物品则无效)。
	UFUNCTION(BlueprintCallable, Category = "Shoot|Inventory")
	void StoreHeldItemToInventory();

	// 从背包取出指定槽位物品：按记录的类型在手上重新生成 Actor 并作为 PickingItem 持有。
	// 取出成功后该槽位从背包移除并广播 OnInventoryChanged。返回生成的 Actor(失败返回 nullptr)。
	UFUNCTION(BlueprintCallable, Category = "Shoot|Inventory")
	AItemActor* TakeOutInventoryItem(int32 SlotIndex);

	// 取出背包中第一个物品(快捷键用)
	UFUNCTION(BlueprintCallable, Category = "Shoot|Inventory")
	void TakeOutFirstInventoryItem();

	// 打开/关闭背包界面(B 键触发)；已打开则关闭，否则创建并刷新。
	UFUNCTION(BlueprintCallable, Category = "Shoot|Inventory")
	void ToggleInventory();

	// 背包变化事件(物品增删时广播)
	UPROPERTY(BlueprintAssignable, Category = "Shoot|Inventory")
	FOnInventoryChanged OnInventoryChanged;


	virtual void SetCookMachineItem(ACookingMachine* Item) override;
	
	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	FORCEINLINE ACookingMachine* GetCookMachineItem() const { return CookingMachine; }

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	void SetPickingItemStatus();

	/** 把物品直接拿在手上（挂到手部插槽并设为手持物），不放入背包。bSetAsPickingItem=false 时仅挂到手上不作为主手持物 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	void HoldItemInHand(AItemActor* Item, bool bSetAsPickingItem = true);
protected:

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	void Attack(const FInputActionValue& Value);

	void Pick(const FInputActionValue& Value);

	/** E 键松开：结束"按住范围拾取"状态 */
	void PickReleased(const FInputActionValue& Value);

	// ============ 交互动画期间的输入缓冲 ============
	/** 交互动画（拾取/采集/浇水）播放中按下的 E 会被记下，动画结束后在 PickBufferWindow 内补执行一次。
	 *  这样连按不会再被动画吞掉，也不会在动画期间触发"放下→再拾取"的抖动。 */
	void BufferPickInput();
	void TickBufferedPick(float DeltaTime);

	/** 当前是否正在播交互动画（角色状态无法区分，因为采集/浇水不改 CharacterActionState） */
	bool IsInteractionAnimPlaying();

	bool bHasBufferedPick = false;
	float BufferedPickTime = 0.f;
	/** 正在消费缓冲按键：此时跳过"放下"兜底，避免刚捡起的东西被缓冲键立刻放掉 */
	bool bConsumingBufferedPick = false;

	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Interaction|Buffer")
	float PickBufferWindow = 0.35f;

	// ============ 按住 E 范围拾取（野外收割） ============
	/** 按住 E 超过 RangePickHoldTime 后，把 RangePickRadius 内可拾取的散落物品直接收进背包 */
	void TickRangePick(float DeltaTime);

	/** 半径内最近的、可被范围拾取的物品（已过滤空碟/脏碟/手上物品/烹饪中/隔墙） */
	AItemActor* FindNearestRangePickItem() const;

	/** 身边是否贴着设备类交互目标（灶台/上菜台/门等）：有的话不吸，避免把待加工食材吸走 */
	bool HasBlockingInteractionTarget() const;

	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Interaction|RangePick")
	bool bRangePickEnabled = true;

	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Interaction|RangePick")
	float RangePickRadius = 150.f;

	/** 按住多久后才进入范围拾取（短按仍然只做一次普通交互） */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Interaction|RangePick")
	float RangePickHoldTime = 0.25f;

	/** 连续吸取的间隔 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Interaction|RangePick")
	float RangePickInterval = 0.12f;

	bool bPickHeld = false;
	float PickHeldTime = 0.f;
	float RangePickCooldown = 0.f;

	/** 冲刺开始（按住 LeftShift）：提速并放大相机 FOV */
	void RushStart(const FInputActionValue& Value);

	/** 冲刺结束（松开）：恢复速度与 FOV */
	void RushEnd(const FInputActionValue& Value);

	void PushFoodStart(const FInputActionValue& Value);
	
	void PushFoodComplete(const FInputActionValue& Value);
	
	/** 打开/关闭商店界面 (P 键) */
	void ToggleShop();

	void OnMouseWheelAxis(const FInputActionValue& Value);
	
	/** 与附近植物交互（浇水 / 驱虫 / 收获，按状态自动选择） */
	void InteractWithPlant();
	
	void HandleEventCallbackFunc();
	
	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	void SetWeaponCollisionEnabled(ECollisionEnabled::Type CollisionEnabled);
	
public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoJumpEnd();

	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoAttack();

	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoPick();

	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoSpwan();

	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoSpwanOther();
	
	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoDropDown();

	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void OnPushFoodComplete();
	
	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoThrow();

	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoRepair();

	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoRepairEnd();

	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoPutDish();

	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoGiveDishToGuest();

	UFUNCTION(BlueprintCallable, Category = "Shoot|Input")
	virtual void DoPushDishTest();

	UFUNCTION(BlueprintCallable, Category = "Shoot|Character")
	void TriggerDeadFunc(int32 RebirthTime);
	
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Character Events")
	void OnCountdownFinishedEvent();

	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Character Events")
	void OnUpdateCountdownEvent();
	
public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	/** 按 bUseTopDownCamera 同步相机臂参数与朝向，每帧调用以支持运行时切换 */
	void UpdateCameraView();

	/** 按关卡里的相机区域 Volume 更新 bInsideCameraArea / bUseTopDownCamera（带进出回差） */
	void UpdateCameraAreaState();

	/** 是否使用俯视（Topdown）相机；关闭即恢复原来的第三人称跟随视角 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Camera")
	bool bUseTopDownCamera = true;

	/** 俯视相机臂长，越大视野越广 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Camera", meta = (EditCondition = "bUseTopDownCamera", ClampMin = "100.0"))
	float TopDownArmLength = 1400.0f;

	/** 俯视相机俯仰角（负值向下看，-62 约为斜俯视） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Camera", meta = (EditCondition = "bUseTopDownCamera", ClampMin = "-89.0", ClampMax = "-15.0"))
	float TopDownPitch = -62.0f;

	/** 俯视时相机是否跟随鼠标左右旋转；关闭则固定朝向世界 +X */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Camera", meta = (EditCondition = "bUseTopDownCamera"))
	bool bTopDownYawFollowsControl = true;

	/** 非俯视（第三人称跟随）模式下的相机臂长；默认 650，比原 400 更宽松，不贴脸 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Camera", meta = (EditCondition = "!bUseTopDownCamera", ClampMin = "100.0"))
	float ThirdPersonArmLength = 650.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Camera", meta = (ClampMin = "-89.0", ClampMax = "0.0"))
	float ThirdPersonEntryPitch = -45.0f;

	/** 臂长切换/调整时的插值速度（0 =  instantly） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Camera", meta = (ClampMin = "0.1"))
	float ArmLengthInterpSpeed = 6.0f;

	/** 是否按关卡里摆放的 CameraAreaVolume 自动切换俯视 / 第三人称；关闭后视角完全由 bUseTopDownCamera 控制 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Camera|Area")
	bool bAutoSwitchCameraByAreaVolume = true;

	/** 进出判定回差（cm）：已在区域内时查询半径向外扩张，避免站在边界上视角来回闪 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Camera|Area", meta = (ClampMin = "0.0"))
	float CameraAreaHysteresis = 80.0f;

	/** 进入区域切俯视的平滑速度；越小过渡越慢越柔 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Camera|Area", meta = (ClampMin = "0.1"))
	float CameraModeBlendSpeed = 2.5f;

	/** 离开区域回第三人称的平滑速度；比进入快，避免纵向视角迟迟回不到鼠标方向 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Camera|Area", meta = (ClampMin = "0.1"))
	float CameraModeExitBlendSpeed = 6.0f;

	/** 当前是否处在任一相机区域体积内（运行时） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Camera|Area")
	bool bInsideCameraArea = false;

	/** 当前镜头混合权重：0 = 第三人称，1 = 俯视（运行时，可用于调试） */
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Camera|Area")
	float CameraViewAlpha = 0.0f;

	/** 首帧直接对齐目标视角，避免开局从 CDO 视角缓动过去 */
	bool bCameraViewInitialized = false;
	
	UPROPERTY(BlueprintReadWrite, Category = "Shoot|Character State")
     ECharacterState CharacterState = ECharacterState::ECS_Idle;
	
     UPROPERTY(BlueprintReadWrite, Category = "Shoot|Character State")
	 ECharacterActionState CharacterActionState = ECharacterActionState::EAS_Unoccupied;
	
	 UPROPERTY(EditDefaultsOnly, Category = "Shoot|Montages")
	 UAnimMontage* AttackMontage;

 	 UPROPERTY(EditDefaultsOnly, Category = "Shoot|Montages")
	 UAnimMontage* PickMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Montages")
	UAnimMontage* PutDownMontage;

	 UPROPERTY(EditDefaultsOnly, Category = "Shoot|Montages")
	 UAnimMontage* ThrowMontage;

	/** 采集（摘取植被）动画：在触发植物收获时播放 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Montages")
	UAnimMontage* GatheringMontage;

	/** 浇水动画：在触发给植被浇水时播放 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Montages")
	UAnimMontage* WateringMontage;

	/** 采集时是否让角色转向（朝向）待采摘的植被；关闭则保持原朝向。默认开启。 */
	UPROPERTY(EditDefaultsOnly, Category = "Shoot|Montages")
	bool bFacePlantWhenGathering = true;

	/** 采集蒙太奇结束委托（用于动画播完后才真正收获并删除植物） */
	FOnMontageEnded OnGatheringMontageEnded;

	/** 正在等待采集动画结束、随后要收获的植物（避免动画途中被销毁） */
	TObjectPtr<APlant> PendingGatherPlant = nullptr;

	/** 浇水蒙太奇结束委托（用于动画播完后才真正补水） */
	FOnMontageEnded OnWateringMontageEnded;

	/** 正在等待浇水动画结束、随后要补水的植物（同时用于高亮描边） */
	TObjectPtr<APlant> PendingWaterPlant = nullptr;

	/** 浇水完成定时器：即使浇水蒙太奇被设为“循环”，也能在播放一轮时长后强制结束浇水流程 */
	FTimerHandle WateringDoneTimerHandle;

	/** 采集完成兜底定时器：即使采集蒙太奇被设为“循环”或被中断，也能在播放一轮时长后强制收获，
	 *  防止 PendingGatherPlant 永不置空导致后续所有采摘被守卫吞掉 */
	FTimerHandle GatheringDoneTimerHandle;

	// ============ 采集/浇水时的平滑转身 ============
	// 目标朝向（仅水平），Tick 中用 RInterpTo 插值逼近，取代原来的瞬时 SetActorRotation
	FRotator InteractFaceRotation = FRotator::ZeroRotator;
	bool bHasInteractFaceRotation = false;


	 UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Character Events")
	 void OnPickEvent();	

	 UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Character Events")
	 void OnReleasePickEvent();

	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Character Events")
	void OnThrowSucessEvent();

public:

	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category = "Shoot|Character")
	TObjectPtr<USkeletalMeshComponent> RetargetMesh;

	FORCEINLINE ECharacterState GetCharacterState() const { return CharacterState; }
	FORCEINLINE ECharacterActionState GetActionState() const { return CharacterActionState; }
	
	//FORCEINLINE void SetCharacterActionState(ECharacterActionState State){ CharacterActionState = State;}
	//FORCEINLINE void SetCharacterState(ECharacterState State){ CharacterState = State;}

	UPROPERTY(BlueprintReadOnly, Category = "Shoot|IK")
	float FBIK_Alpha;

	USceneComponent* HandIK_LPoint;
	USceneComponent* HandIK_RPoint;
	UShootAnimInstance* CacheAnimInstance;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite, Category = "Shoot|Character")
	FName ItemSocketName;

	
	// 倒计时变量
	UPROPERTY(BlueprintReadOnly, Category = "Shoot|Character")
	int32 CountdownTime;
	UPROPERTY(BlueprintReadWrite, Category = "Shoot|Character")
	bool FreezeInput = true;

	TObjectPtr<UMaterialInterface> BaseGrayscaleMaterial;
	TObjectPtr<UMaterialInstanceDynamic> GrayscaleMID;
private:


	UPROPERTY(EditAnywhere, Category = "Shoot|Debug")
	bool bRotationPicth = true;

	// 控制变量
	bool bNeedRotation = false;
	bool bIsPKeyHeld = false;
	
	float CurrentAngle = 0.0f;

	float RotationThreshold = 10;
	float RotationSpeed = 10;
	// 当前目标旋转
	FRotator TargetTurnRotation;
    
	UPROPERTY(EditAnywhere, Category = "Shoot|Projectile")
	float MaxVerticalAngle = 60.0f;   // 最大仰角
    
	UPROPERTY(EditAnywhere, Category = "Shoot|Projectile")
	float MinVerticalAngle = -30.0f;   // 最小仰角
	
	UPROPERTY(EditAnywhere, Category = "Shoot|Projectile")
	float MaxPredictionTime = 5.0f;
	// 绘制参数
	UPROPERTY(EditAnywhere, Category = "Shoot|Debug")
	float PointSize = 10.0f;
    
	UPROPERTY(EditAnywhere, Category = "Shoot|Debug")
	FColor PointColor = FColor::Green;
    
	UPROPERTY(EditAnywhere, Category = "Shoot|Debug")
	bool bDrawLines = true;
    
	UPROPERTY(EditAnywhere, Category = "Shoot|Debug")
	FColor LineColor = FColor::Red;
    
	// 存储的路径点
	TArray<FVector> CurrentPathPoints;

	// 抛物线预测
	void PredictAndDrawParabola();
	void ClearPrediction();
    
	// 绘制抛物线点
	void DrawPredictCurve(const TArray<FVector>& PathPoints);
    
	// 计算发射参数
	FVector GetLaunchVelocity() const;
	FVector GetLaunchLocation() const;

	void UpdateCharacterRotation(float DeltaTime);


	// 计时器句柄
	FTimerHandle CountdownTimerHandle;
	// 每秒钟执行的函数
	void UpdateCountdown();

	// 倒计时结束执行的函数
	void OnCountdownFinished();

	// —— 食物箱剩余数量 HUD 提示 ——
	/** 懒获取本关卡常驻 HUD（从 ABaseLevelBP 取，缓存以避免每帧查找） */
	UHUDWidget* GetHUDWidget();

	/** 缓存的 HUD 指针 */
	UHUDWidget* CachedHUD = nullptr;

	/** 上次推送到 HUD 的食物箱与数量，避免每帧重复广播 */
	AFoodSpawner* LastSpawnerForHUD = nullptr;
	int32 LastSpawnerCount = -2;
};

