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
class UInteractionDialogueComponent;
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TSubclassOf<AItemActor> ItemClass = nullptr;

	// 物品信息数据表行(取出时回填到新生成的 Actor)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (RowType = "ItemActorInfo"))
	FDataTableRowHandle ItemInfoRow;

	// 食材ID列表(如碟子里的菜)，取出时回填
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TArray<int> FoodIdArray;

	// 显示名(UI 用)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FString DisplayName;

	// 数量：同类物品堆叠(相同 ItemClass 不占新槽位，仅数量+1)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
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
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
private:
	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<AItemActor> OverlappingItem;

	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<AItemActor> PickingItem;

	// 当前可交互的植物（靠近时由 APlant 的重叠球注册）
	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<APlant> OverlappingPlant;

	// 当前可提交菜品的上菜台（靠近时由上菜台的重叠球注册）
	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<class AServingCounter> OverlappingCounter;

	// 当前可交还碟子的洗碗机（靠近时由洗碗机的重叠球注册）
	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<class ADishwasher> OverlappingDishwasher;

	// 当前可招揽顾客的招揽台（靠近时由招揽台的重叠球注册）
	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<class AAttractCounter> OverlappingAttractCounter;

	// 当前可开门营业的大门交互台（靠近时由大门的重叠球注册）
	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<class AGateCounter> OverlappingGate;

	// 当前可接单的客人（靠近时由客人的交互球注册；仅"需要玩家接单"模式使用）
	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<class AGuestCharacter> OverlappingGuest;

	// 当前可收走的空碟（靠近空碟时由空碟的回收球注册；方案B 上菜闭环，收走后变成手上脏碟）
	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<class AItemActor> OverlappingEmptyPlate;

	// 当前可投入食材的研究炉（靠近时由研究炉的重叠球注册；用于 E 键投入食材研究新菜谱）
	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<class AResearchStove> OverlappingResearchStove;

	// 当前可对话的交互对象（靠近时由 UInteractionDialogueComponent 注册；通用对话系统）
	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<class UInteractionDialogueComponent> OverlappingDialogue;

	// ============ 背包系统 ============
	// 背包槽位：只记录物体的类型/信息，入包后世界中的物体会被销毁，取出时按类型重新生成
	UPROPERTY(VisibleInstanceOnly, Category = "Inventory")
	TArray<FInventorySlot> InventoryItems;

	// 背包容量上限(<=0 表示不限制)
	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 InventoryCapacity = 0;

	// 背包界面 Widget 类(蓝图继承 UInventoryWidget)
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<UUserWidget> InventoryWidgetClass;

	// 当前打开的背包 Widget 实例(仅持有一个，关闭时不销毁，再次打开复用)
	UPROPERTY()
	TObjectPtr<UInventoryWidget> InventoryWidget;

	// 商店界面 Widget 类(蓝图继承 UShopWidget)
	UPROPERTY(EditAnywhere, Category = "Shop")
	TSubclassOf<UUserWidget> ShopWidgetClass;

	// 当前打开的商店 Widget 实例(仅持有一个，关闭时不销毁，再次打开复用)
	UPROPERTY()
	TObjectPtr<UShopWidget> ShopWidget;

	UPROPERTY(EditAnywhere, Category = Weapon)
	AWeapon* EquippedWeapon;

	UPROPERTY(VisibleInstanceOnly)
	TObjectPtr<ACookingMachine> CookingMachine;

	void PlayPickMontage(FName SelectionName);

	/** 播放采集（摘取植被）动画，返回是否成功开始播放 */
	bool PlayGatheringMontage();

	/** 播放浇水动画，返回是否成功开始播放 */
	bool PlayWateringMontage();

	/** 采集蒙太奇结束回调：此时才真正收获并删除植物 */
	UFUNCTION()
	void GatheringMontageEnded(UAnimMontage* Montage, bool bInterrupted);

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
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

	/** Attack Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* AttackAction;

	/** 拾取 Input Action：在招揽台范围内按下也会触发招揽（与 AttractAction 合并，以本键为准） */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* PickAction;
	
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* PushFoodAction;//扔食物

	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* WheelAction;//

	/** 放入背包 Input Action (Q 键) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* StoreToInventoryAction;

	/** 从背包取出(生成到手上) Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* TakeOutInventoryAction;

	/** 打开/关闭背包界面 Input Action (B 键) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* OpenInventoryAction;

	/** 打开/关闭商店界面 Input Action (P 键) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* OpenShopAction;


public:

	/** Constructor */
	AShootCharacter();	

public:
	
	UFUNCTION(BlueprintCallable, Category="PickupInterface")
	virtual void SetOverlappingItem(AItemActor* Item) override;
	
	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	FORCEINLINE AItemActor* GetOverlapItem() const { return OverlappingItem; }

	// 植物交互：靠近时由 APlant 注册/反注册
	UFUNCTION(BlueprintCallable, Category="Plant")
	void SetOverlappingPlant(APlant* Plant);

	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	FORCEINLINE APlant* GetOverlappingPlant() const { return OverlappingPlant; }

	// 上菜台交互：靠近时由 AServingCounter 注册/反注册
	UFUNCTION(BlueprintCallable, Category="Serve")
	void SetOverlappingCounter(AServingCounter* Counter);

	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	FORCEINLINE AServingCounter* GetOverlappingCounter() const { return OverlappingCounter; }

	// 招揽台交互：靠近时由 AAttractCounter 注册/反注册
	UFUNCTION(BlueprintCallable, Category="Attract")
	void SetOverlappingAttractCounter(AAttractCounter* Counter);

	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	FORCEINLINE AAttractCounter* GetOverlappingAttractCounter() const { return OverlappingAttractCounter; }

	// 大门交互：靠近时由 AGateCounter 注册/反注册（用于 E 键开门营业）
	UFUNCTION(BlueprintCallable, Category="Gate")
	void SetOverlappingGate(class AGateCounter* Gate);

	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	FORCEINLINE AGateCounter* GetOverlappingGate() const { return OverlappingGate; }

	// 客人交互：靠近时由 AGuestCharacter 的交互球注册/反注册（用于 E 键接单）
	UFUNCTION(BlueprintCallable, Category="Guest")
	void SetOverlappingGuest(AGuestCharacter* Guest);

	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	FORCEINLINE AGuestCharacter* GetOverlappingGuest() const { return OverlappingGuest; }

	// 洗碗机交互：靠近时由洗碗机的重叠球注册/反注册（用于 E 键交还脏碟）
	UFUNCTION(BlueprintCallable, Category="Dishwasher")
	void SetOverlappingDishwasher(ADishwasher* Dishwasher);

	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	FORCEINLINE ADishwasher* GetOverlappingDishwasher() const { return OverlappingDishwasher; }

	// 空碟回收：靠近空碟时由空碟的回收球注册/反注册（用于 E 键收走空碟，方案B）
	UFUNCTION(BlueprintCallable, Category="Dish")
	void SetOverlappingEmptyPlate(AItemActor* Plate);

	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	FORCEINLINE AItemActor* GetOverlappingEmptyPlate() const { return OverlappingEmptyPlate; }

	// 研究炉交互：靠近时由研究炉的重叠球注册/反注册（用于 E 键投入食材研究新菜谱）
	UFUNCTION(BlueprintCallable, Category="Research")
	void SetOverlappingResearchStove(AResearchStove* Stove);

	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	FORCEINLINE AResearchStove* GetOverlappingResearchStove() const { return OverlappingResearchStove; }


	// 通用对话交互：靠近时由 UInteractionDialogueComponent 注册/反注册（用于 E 键对话）
	UFUNCTION(BlueprintCallable, Category="Dialogue")
	void SetOverlappingDialogue(UInteractionDialogueComponent* Comp);

	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	FORCEINLINE UInteractionDialogueComponent* GetOverlappingDialogue() const { return OverlappingDialogue; }

	FORCEINLINE void SetPickingItem(AItemActor* Item){PickingItem = Item;}

	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	FORCEINLINE AItemActor* GetPickingItem() const { return PickingItem; }

	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	void SetEqiuppedWeapon(AWeapon* Weapon)
	{
		EquippedWeapon = Weapon;
		// 同步设置武器 Actor 的 Owner，供武器在 OnBoxOverlap 中通过 GetOwner() 判断持有者
		if (Weapon)
		{
			Weapon->SetOwner(this);
		}
	}

	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	FORCEINLINE AWeapon* GetEqiuppedWeapon() const { return EquippedWeapon; }

	// ============ 背包系统 API ============
	// 将手中/世界中的物品记录进背包，并销毁该物体(节省世界实例)；成功广播 OnInventoryChanged。
	// 注意：这里记录的是"类型"，取出时按 ItemClass 重新生成。
	UFUNCTION(BlueprintCallable, Category="Inventory")
	bool AddItemToInventory(AItemActor* Item);

	// 按槽位索引从背包移除(仅删记录，不会生成物体)；索引无效则失败。
	UFUNCTION(BlueprintCallable, Category="Inventory")
	bool RemoveItemFromInventory(int32 SlotIndex);

	// 背包是否包含该槽位
	UFUNCTION(BlueprintCallable, Category="Inventory")
	bool HasItemInInventory(int32 SlotIndex) const;

	// 背包槽位列表(只读)
	UFUNCTION(BlueprintCallable, Category="Inventory")
	const TArray<FInventorySlot>& GetInventoryItems() const { return InventoryItems; }

	// 背包当前数量
	UFUNCTION(BlueprintCallable, Category="Inventory")
	int32 GetInventoryCount() const { return InventoryItems.Num(); }

	// 背包是否已满(InventoryCapacity > 0 且已达上限)
	UFUNCTION(BlueprintCallable, Category="Inventory")
	bool IsInventoryFull() const;

	// 把当前手上的物品(PickingItem)记录进背包并销毁之(手上无物品则无效)。
	UFUNCTION(BlueprintCallable, Category="Inventory")
	void StoreHeldItemToInventory();

	// 从背包取出指定槽位物品：按记录的类型在手上重新生成 Actor 并作为 PickingItem 持有。
	// 取出成功后该槽位从背包移除并广播 OnInventoryChanged。返回生成的 Actor(失败返回 nullptr)。
	UFUNCTION(BlueprintCallable, Category="Inventory")
	AItemActor* TakeOutInventoryItem(int32 SlotIndex);

	// 取出背包中第一个物品(快捷键用)
	UFUNCTION(BlueprintCallable, Category="Inventory")
	void TakeOutFirstInventoryItem();

	// 打开/关闭背包界面(B 键触发)；已打开则关闭，否则创建并刷新。
	UFUNCTION(BlueprintCallable, Category="Inventory")
	void ToggleInventory();

	// 背包变化事件(物品增删时广播)
	UPROPERTY(BlueprintAssignable, Category="Inventory")
	FOnInventoryChanged OnInventoryChanged;


	virtual void SetCookMachineItem(ACookingMachine* Item) override;
	
	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	FORCEINLINE ACookingMachine* GetCookMachineItem() const { return CookingMachine; }

	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
	void SetPickingItemStatus();

	/** 把物品直接拿在手上（挂到手部插槽并设为手持物），不放入背包。bSetAsPickingItem=false 时仅挂到手上不作为主手持物 */
	UFUNCTION(BlueprintCallable, Category="ShootCharacter")
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

	void PushFoodStart(const FInputActionValue& Value);
	
	void PushFoodComplete(const FInputActionValue& Value);
	
	/** 打开/关闭商店界面 (P 键) */
	void ToggleShop();

	void OnMouseWheelAxis(const FInputActionValue& Value);
	
	/** 与附近植物交互（浇水 / 驱虫 / 收获，按状态自动选择） */
	void InteractWithPlant();
	
	void HandleEventCallbackFunc();
	
	UFUNCTION(BlueprintCallable)
	void SetWeaponCollisionEnabled(ECollisionEnabled::Type CollisionEnabled);
	
public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoAttack();

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoPick();

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoSpwan();

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoSpwanOther();
	
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoDropDown();

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void OnPushFoodComplete();
	
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoThrow();

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoRepair();

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoRepairEnd();

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoPutDish();

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoGiveDishToGuest();

	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoPushDishTest();

	UFUNCTION(BlueprintCallable, Category="Character")
	void TriggerDeadFunc(int32 RebirthTime);
	
	UFUNCTION(BlueprintImplementableEvent, Category="Character Events")
	void OnCountdownFinishedEvent();

	UFUNCTION(BlueprintImplementableEvent, Category="Character Events")
	void OnUpdateCountdownEvent();
	
public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
	
	UPROPERTY(BlueprintReadWrite, Category="Character State")
     ECharacterState CharacterState = ECharacterState::ECS_Idle;
	
     UPROPERTY(BlueprintReadWrite, Category="Character State")
	 ECharacterActionState CharacterActionState = ECharacterActionState::EAS_Unoccupied;
	
	 UPROPERTY(EditDefaultsOnly, Category="Montages")
	 UAnimMontage* AttackMontage;

 	 UPROPERTY(EditDefaultsOnly, Category="Montages")
	 UAnimMontage* PickMontage;

	UPROPERTY(EditDefaultsOnly, Category="Montages")
	UAnimMontage* PutDownMontage;

	 UPROPERTY(EditDefaultsOnly, Category="Montages")
	 UAnimMontage* ThrowMontage;

	/** 采集（摘取植被）动画：在触发植物收获时播放 */
	UPROPERTY(EditDefaultsOnly, Category="Montages")
	UAnimMontage* GatheringMontage;

	/** 浇水动画：在触发给植被浇水时播放 */
	UPROPERTY(EditDefaultsOnly, Category="Montages")
	UAnimMontage* WateringMontage;

	/** 采集时是否让角色转向（朝向）待采摘的植被；关闭则保持原朝向。默认开启。 */
	UPROPERTY(EditDefaultsOnly, Category="Montages")
	bool bFacePlantWhenGathering = true;

	/** 采集蒙太奇结束委托（用于动画播完后才真正收获并删除植物） */
	FOnMontageEnded OnGatheringMontageEnded;

	/** 正在等待采集动画结束、随后要收获的植物（避免动画途中被销毁） */
	TObjectPtr<APlant> PendingGatherPlant = nullptr;


	 UFUNCTION(BlueprintImplementableEvent, Category="Character Events")
	 void OnPickEvent();	

	 UFUNCTION(BlueprintImplementableEvent, Category="Character Events")
	 void OnReleasePickEvent();

	UFUNCTION(BlueprintImplementableEvent, Category="Character Events")
	void OnThrowSucessEvent();

public:

	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="ShootCharacter")
	TObjectPtr<USkeletalMeshComponent> RetargetMesh;

	FORCEINLINE ECharacterState GetCharacterState() const { return CharacterState; }
	FORCEINLINE ECharacterActionState GetActionState() const { return CharacterActionState; }
	
	//FORCEINLINE void SetCharacterActionState(ECharacterActionState State){ CharacterActionState = State;}
	//FORCEINLINE void SetCharacterState(ECharacterState State){ CharacterState = State;}

	UPROPERTY(BlueprintReadOnly, Category = IK)
	float FBIK_Alpha;

	USceneComponent* HandIK_LPoint;
	USceneComponent* HandIK_RPoint;
	UShootAnimInstance* CacheAnimInstance;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	FName ItemSocketName;

	
	// 倒计时变量
	UPROPERTY(BlueprintReadOnly, Category  = "ShootCharacter")
	int32 CountdownTime;
	UPROPERTY(BlueprintReadWrite, Category = "ShootCharacter")
	bool FreezeInput = true;

	TObjectPtr<UMaterialInterface> BaseGrayscaleMaterial;
	TObjectPtr<UMaterialInstanceDynamic> GrayscaleMID;
private:


	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bRotationPicth = true;

	// 控制变量
	bool bNeedRotation = false;
	bool bIsPKeyHeld = false;
	
	float CurrentAngle = 0.0f;

	float RotationThreshold = 10;
	float RotationSpeed = 10;
	// 当前目标旋转
	FRotator TargetTurnRotation;
    
	UPROPERTY(EditAnywhere, Category = "Projectile")
	float MaxVerticalAngle = 60.0f;   // 最大仰角
    
	UPROPERTY(EditAnywhere, Category = "Projectile")
	float MinVerticalAngle = -30.0f;   // 最小仰角
	
	UPROPERTY(EditAnywhere, Category = "Projectile")
	float MaxPredictionTime = 5.0f;
	// 绘制参数
	UPROPERTY(EditAnywhere, Category = "Debug")
	float PointSize = 10.0f;
    
	UPROPERTY(EditAnywhere, Category = "Debug")
	FColor PointColor = FColor::Green;
    
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDrawLines = true;
    
	UPROPERTY(EditAnywhere, Category = "Debug")
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

