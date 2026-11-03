// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ItemScatterSpawner.generated.h"

class AItemActor;

/** 生成 / 回收食材时广播，供蓝图挂特效、音效、提示 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FScatterItemEvent, AItemActor*, Item);

/** 生成区域形状 */
UENUM(BlueprintType)
enum class EScatterAreaShape : uint8
{
	/** 圆形 / 环形（以 Actor 为圆心、SpawnRadius 为半径，InnerRadius 为内圈排除） */
	Circle UMETA(DisplayName = "圆形(环形)"),
	/** 矩形盒（以 Actor 为中心，BoxExtent 为半尺寸；Z 分量不参与采样） */
	Box    UMETA(DisplayName = "矩形(盒)"),
};

/**
 * 一条生成项：一种食材类 + 权重 + 该类同时存在的数量上限。
 * 直接在生成器实例的 Entries 数组里加多条，就能同时撒多种食材。
 */
USTRUCT(BlueprintType)
struct FScatterSpawnEntry
{
	GENERATED_BODY()

	/** 要生成的食材 Actor 类（如 BP_RawMushroom、BP_Tomato …） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
	TSubclassOf<AItemActor> ItemClass;

	/** 随机权重（越大出现越多），<=0 视为 1 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float Weight = 1.f;

	/** 该类同一时间最多存在几个；0 = 不单独限制（只受 SpawnCount / MaxAliveTotal 约束） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta = (ClampMin = "0"))
	int32 MaxAlive = 0;
};

/** 运行时记录的一个"坑位"：原始位置 + 当前占用它的实例 + 重生倒计时 */
USTRUCT()
struct FScatterSlot
{
	GENERATED_BODY()

	/** 当前占用该坑位的实例（被玩家拿起 / 销毁后置空） */
	UPROPERTY()
	TObjectPtr<AItemActor> Actor = nullptr;

	/** 原始生成位置（bRespawnAtOriginalPoint=true 时重生回这里） */
	UPROPERTY()
	FVector OriginLocation = FVector::ZeroVector;

	/** 原始生成旋转（含随机 Yaw / 地面法线对齐） */
	UPROPERTY()
	FRotator OriginRotation = FRotator::ZeroRotator;

	/** 该坑位使用的 Entries 下标 */
	UPROPERTY()
	int32 EntryIndex = INDEX_NONE;

	/** >0 表示该坑位已空、正在等待重生（秒） */
	UPROPERTY()
	float RespawnCountdown = -1.f;
};

/**
 * 运行时食材散布器（随机生成器）。
 *
 * 用途：在编辑器里只放一个本 Actor 到"农场附近"，运行时按配置随机撒出 N 个食材（AItemActor），
 *       无需手动摆放；玩家摘走 / 物品被销毁后，隔一段时间自动在原位或新位置重新长出来。
 *
 * 特性：
 *  - 多食材：Entries 数组可配多种 AItemActor，按 Weight 加权随机，支持每类独立数量上限；
 *  - 避让：任何两个生成点之间保持 MinSpacing 的水平距离；开启 bAvoidWorldItems 时
 *          也不会压在关卡里已摆放的物品上；
 *  - 地面吸附：向下 LineTrace 贴合地面（可选沿法线旋转），打不到地面的点直接丢弃；
 *  - 重生：检测"被玩家拿起（EIS_Equipped / 已 Attach 到 Pawn）"与"被 Destroy"两种离场方式，
 *          按 RespawnDelayMin~Max 随机延迟后重生，可配一半回原位、一半换新位置；
 *  - 全部参数均为 EditAnywhere，可在关卡里的实例上直接调，无需改代码。
 *
 * 典型用法：关卡里放一个 Actor，SpawnRadius 调到覆盖农场，SpawnCount=12，
 *           Entries 加一条 BP_RawMushroom（Weight=1），BeginPlay 自动生成。
 */
UCLASS(Blueprintable)
class SHOOT_API AItemScatterSpawner : public AActor
{
	GENERATED_BODY()

public:
	AItemScatterSpawner();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ==================== 配置：生成什么 ====================

	/** 生成项列表：每一项 = 一种食材类 + 权重 + 该类数量上限。可加多条混撒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Items")
	TArray<FScatterSpawnEntry> Entries;

	// ==================== 配置：洒在哪 ====================

	/** 区域形状：圆形(可带内圈) / 矩形 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Area")
	EScatterAreaShape AreaShape = EScatterAreaShape::Circle;

	/** 外半径（Circle 用）：以本 Actor 位置为圆心 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Area",
		meta = (ClampMin = "0", EditCondition = "AreaShape==EScatterAreaShape::Circle", EditConditionHides))
	float SpawnRadius = 600.f;

	/** 内半径（Circle 用）：留空区域，0 = 实心圆；配成环形可沿农场外围一圈撒 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Area",
		meta = (ClampMin = "0", EditCondition = "AreaShape==EScatterAreaShape::Circle", EditConditionHides))
	float InnerRadius = 0.f;

	/** 矩形半尺寸（Box 用）：Z 分量不参与采样 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Area",
		meta = (EditCondition = "AreaShape==EScatterAreaShape::Box", EditConditionHides))
	FVector BoxExtent = FVector(600.f, 600.f, 0.f);

	/** 随机朝向范围（Yaw，度）：默认 0~360 完全随机；设成 0~0 则朝向固定 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Area")
	FVector2D YawRange = FVector2D(0.f, 360.f);

	// ==================== 配置：贴地 ====================

	/** 生成点向下打线贴合地面；关掉则直接用采样点的 Z（采样 Z 取本 Actor 的 Z） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Ground")
	bool bSnapToGround = true;

	/** 打线起点：采样点上方多少 cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Ground", meta = (EditCondition = "bSnapToGround", ClampMin = "0"))
	float TraceUp = 800.f;

	/** 打线长度：采样点下方多少 cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Ground", meta = (EditCondition = "bSnapToGround", ClampMin = "0"))
	float TraceDown = 2000.f;

	/** 沿地面法线的抬高量（cm）：0=贴地，正值略微悬空，负值略微陷入 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Ground", meta = (EditCondition = "bSnapToGround"))
	float GroundOffset = 0.f;

	/** 让物品的 Z 轴对齐地面法线（斜坡上不歪）；关掉则保持竖直 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Ground", meta = (EditCondition = "bSnapToGround"))
	bool bAlignToGroundNormal = true;

	/** 地面检测使用的碰撞通道 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Ground", meta = (EditCondition = "bSnapToGround"))
	TEnumAsByte<ECollisionChannel> GroundChannel = ECC_Visibility;

	/** 是否只把 Actor 生成在地形（Landscape）上：开启后向下打线会跳过屋顶 / 楼板等非地形命中，
	 *  继续向下查找 Landscape 表面；整条打线都碰不到地形时该点被丢弃。
	 *  适合户外农场区域（防止食材生成到屋顶上）；若某生成器区域是普通静态网格地面，请关闭此项。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Ground", meta = (EditCondition = "bSnapToGround"))
	bool bOnlyOnLandscape = true;

	// ==================== 配置：数量与避让 ====================

	/** 开局（BeginPlay）要生成的总个数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Count", meta = (ClampMin = "0"))
	int32 SpawnCount = 12;

	/** 全场同时存在的总上限；0 = 不限制（只用 SpawnCount 控制初始数量） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Count", meta = (ClampMin = "0"))
	int32 MaxAliveTotal = 0;

	/** 两个生成点之间的最小水平距离（cm）：互相避让，避免重叠 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Spacing", meta = (ClampMin = "0"))
	float MinSpacing = 150.f;

	/** 同时避开关卡里已手动摆放的其他 AItemActor（含其他生成器撒出的） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Spacing")
	bool bAvoidWorldItems = true;

	/** 单个点位最多尝试几次采样；尝试满仍找不到空位就跳过这个点 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Spacing", meta = (ClampMin = "1"))
	int32 MaxPlaceAttempts = 40;

	// ==================== 配置：采摘后重生 ====================

	/** BeginPlay 时自动生成（关掉则只能在蓝图里手动调 SpawnAll） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Respawn")
	bool bSpawnOnBeginPlay = true;

	/** 摘走后自动重生 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Respawn")
	bool bAutoRespawn = true;

	/** 重生延迟下限（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Respawn",
		meta = (EditCondition = "bAutoRespawn", ClampMin = "0"))
	float RespawnDelayMin = 25.f;

	/** 重生延迟上限（秒）：每个坑位在 [Min,Max] 内独立随机，避免整齐划一 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Respawn",
		meta = (EditCondition = "bAutoRespawn", ClampMin = "0"))
	float RespawnDelayMax = 60.f;

	/** true=重生回原来那个点；false=在原区域里重新随机一个不重叠的位置 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Respawn",
		meta = (EditCondition = "bAutoRespawn"))
	bool bRespawnAtOriginalPoint = true;

	/** 检测"是否被玩家拿走"的轮询间隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Respawn",
		meta = (EditCondition = "bAutoRespawn", ClampMin = "0.1"))
	float PickCheckInterval = 0.5f;

	/** 随机种子：0 = 每次运行都不同；填非 0 可复现同一套位置（调试用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Debug")
	int32 RandomSeed = 0;

	/** 在场景里画出生成区域（编辑器与运行时都可见） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter|Debug")
	bool bDrawDebugArea = false;

	// ==================== 对外事件 ====================

	/** 成功生成一个食材时广播 */
	UPROPERTY(BlueprintAssignable, Category = "Scatter|Events")
	FScatterItemEvent OnItemSpawned;

	/** 某个坑位空了（被摘走 / 被销毁，即将进入重生倒计时）时广播 */
	UPROPERTY(BlueprintAssignable, Category = "Scatter|Events")
	FScatterItemEvent OnItemRemoved;

	// ==================== 蓝图接口 ====================

	/** 清空现有全部生成物，并按 SpawnCount 重新生成一遍（可用于"换一天"重置） */
	UFUNCTION(BlueprintCallable, Category = "Scatter")
	void SpawnAll();

	/** 只生成一个；EntryIndex<0 时按权重随机挑一种 */
	UFUNCTION(BlueprintCallable, Category = "Scatter")
	AItemActor* SpawnOne(int32 EntryIndex = -1);

	/** 销毁本生成器撒出的全部实例（不影响玩家手上拿着的），并把坑位清空 */
	UFUNCTION(BlueprintCallable, Category = "Scatter")
	void ClearAllSpawned();

	/** 当前仍在场景里的实例数量 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Scatter")
	int32 GetAliveCount() const;

	/** 当前正在等待重生的坑位数量 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Scatter")
	int32 GetPendingRespawnCount() const;

	/** 立即重生指定坑位（调试用） */
	UFUNCTION(BlueprintCallable, Category = "Scatter")
	void RespawnSlotNow(int32 SlotIndex);

protected:
	/** 运行时坑位列表 */
	UPROPERTY()
	TArray<FScatterSlot> Slots;

	/** 关卡里已存在、需要避让的其他 AItemActor 位置（每次生成前刷新一次） */
	UPROPERTY()
	TArray<FVector> WorldReservedPoints;

	FRandomStream Rand;

	/** 摘取检测的累积时间 */
	float CheckAccum = 0.f;

	/** 是否已初始化随机流 */
	bool bRandReady = false;

	/** 初始化随机流（BeginPlay / 首次生成时调用，幂等） */
	void EnsureRandomReady();

	/** 采样一个区域内的候选点（未贴地） */
	FVector SamplePointInArea();

	/** 把候选点贴到地面并算出旋转；返回 false 表示打不到地面，应丢弃 */
	bool ProjectToGroundAndRotate(const FVector& InPoint, FVector& OutLocation, FRotator& OutRotation);

	/** 判断某个水平位置是否满足避让（够远） */
	bool IsLocationFree(const FVector& InLocation) const;

	/** 按权重挑一个 Entry 下标（会跳过已达 MaxAlive 上限的项）；没有可用项返回 INDEX_NONE */
	int32 PickEntryIndex();

	/** 统计某一项当前存活的实例数 */
	int32 CountAliveOfEntry(int32 EntryIndex) const;

	/** 刷新需要避让的关卡已有物品位置 */
	void RebuildWorldReserved();

	/** 判断某个实例是否是本生成器撒出来的 */
	bool IsSpawnedByMe(const AItemActor* Item) const;

	/** 在指定坑位生成一个实例；成功返回实例，失败返回 nullptr */
	AItemActor* SpawnIntoSlot(int32 SlotIndex, bool bUseOriginalPoint);

	/** 生成后被销毁的回调（玩家存进背包等） */
	UFUNCTION()
	void HandleSpawnedActorDestroyed(AActor* DestroyedActor);

	/** 判断实例是否已被玩家拿在手上 */
	bool IsHeldByPlayer(const AItemActor* Item) const;

	/** 把一个坑位标记为空，并排定重生倒计时 */
	void VacateSlot(int32 SlotIndex, AItemActor* Item);

	/** 推进重生倒计时 */
	void UpdateRespawnTimers(float DeltaSeconds);

	/** 轮询检测哪些实例已被玩家拿走 */
	void CheckPickedUpItems();
};
