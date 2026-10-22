// Fill out your copyright notice for Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interfaces/HitInterface.h"
#include "AnimalTypes.h"
#include "AnimalCharacter.generated.h"

class AItemActor;
class AAIController;
class AShootCharacter;
class UAIPerceptionComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class UUserWidget;
class UBoxComponent;
class UNiagaraSystem;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;

// 可生成的 Actor 类 + 其生成权重（概率），权重可在蓝图调整
USTRUCT(BlueprintType)
struct FSpawnClassEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TSubclassOf<AActor> Class;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "0.0"))
	float Weight = 1.0f; // 相对权重，越大生成概率越高
};

UCLASS()
class SHOOT_API AAnimalCharacter : public ACharacter, public IHitInterface
{
	GENERATED_BODY()

public:
	AAnimalCharacter();

protected:
	virtual void BeginPlay() override;

public:	
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void GetHit_Implementation(const FVector& ImpactPoint, AActor* Hitter) override;

	bool bDie = false;

	// ============================================================
	//  行为系统（状态机）
	// ============================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	EAnimalActiveBehavior ActiveBehavior = EAnimalActiveBehavior::RoamRandom; // 默认主动行为

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	EAnimalEngageBehavior EngageBehavior = EAnimalEngageBehavior::Ignore; // 受威胁时的交战行为

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	EAnimalRole AnimalRole = EAnimalRole::Prey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	bool bIsNocturnal = false; // true: 夜行动物(夜间活跃,白天休息)

	// RoamInRange / 感知范围参数
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	float WanderRadius = 1000.f; // 随机游荡半径(以出生点为圆心)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	float SightRange = 1500.f; // 视觉感知距离(同步用于 UAIPerception 视觉配置)

	// 互相避让参数(防止动物之间相交重叠，基于 RVO)
	// 注：RVO 避让半径由胶囊体自动决定，按体型调避让范围请改 CapsuleComponent 的 Capsule Radius
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Avoidance")
	bool bEnableAvoidance = true; // 是否启用 AI 互相避让

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Avoidance")
	float AvoidanceConsiderationRadius = 400.f; // 考虑避让的邻居范围

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Avoidance")
	float AvoidanceWeight = 0.5f; // 避让权重(越大越优先避让)

	// 速度区间常量：行走 1~300，跑步/逃跑 301~600（供动画 BlendSpace 按速度区分）
	static constexpr float WalkSpeedMin = 1.f;
	static constexpr float WalkSpeedMax = 300.f;
	static constexpr float RunSpeedMin = 301.f;
	static constexpr float RunSpeedMax = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior", meta = (ClampMin = "1.0", ClampMax = "300.0"))
	float MoveSpeed = 300.f; // 行走(漫游/觅食)速度，限定 1~300

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior", meta = (ClampMin = "301.0", ClampMax = "600.0"))
	float FleeSpeed = 600.f; // 逃跑/跑步速度，限定 301~600

	// 航点（用于 WaypointRandom / WaypointPath）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	TArray<FVector> Waypoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	FVector HomeLocation = FVector::ZeroVector; // Homebase 位置(GoToHome)

	UPROPERTY(BlueprintReadOnly, Category = "Animal|Behavior")
	bool bResting = false; // 当前是否处于休息(昼夜)

	// 当前实际执行的行为（运行时）
	UPROPERTY(BlueprintReadOnly, Category = "Animal|Behavior")
	EAnimalActiveBehavior CurrentBehavior = EAnimalActiveBehavior::Idle;

	// 逃跑模式（运行时）
	UPROPERTY(BlueprintReadOnly, Category = "Animal|Behavior")
	bool bFleeing = false; // 当前是否处于逃跑状态

	// 逃跑加速：从走路平滑加速到跑步
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	float FleeAccelRate = 200.f; // 逃跑加速速率(单位/秒，越大越快达到跑步速度)

	float CurrentMoveSpeed = 300.f; // 当前实际移动速度(运行时，逃跑时从 MoveSpeed 加速到 FleeSpeed)

	bool bHasFleeSpawnedEver = false; // 本动物是否曾因逃跑生成过 Actor(保证每只只生成一次)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	float FleeSpawnCooldown = 10.f; // 逃跑触发 SpawnActor 后的冷却(秒)，避免反复生成；<=0 表示只生成一次后永不再生成

	double NextFleeSpawnTime = -1.0; // 下次允许因逃跑生成的时间点(运行时)

	// 逃跑时检测正前方玩家(AShootCharacter)的 FOV 半角(度)，与 SightAngleDegrees 共用
	// 在 UpdateBehavior 中调用：进入正前方视锥内则切换到逃跑并触发 SpawnActor
	UFUNCTION(BlueprintCallable, Category = "Animal|Behavior")
	bool CheckFleeFromPlayer();

	// 警觉/逃跑标记（问号=思考中，感叹号=正在逃跑）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal|Marks")
	UStaticMeshComponent* QuestionMarkComp = nullptr; // 问号模型(蓝图赋网格)

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal|Marks")
	UStaticMeshComponent* ExclamationMarkComp = nullptr; // 感叹号模型(蓝图赋网格)

	// 警觉(思考)阶段：发现玩家到真正逃跑之间的随机延迟
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	float AlertThinkTimeMin = 1.f; // 思考最短时长(秒)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	float AlertThinkTimeMax = 3.f; // 思考最长时长(秒)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	float FleeHideDistance = 0.f; // 逃跑跑出此距离后隐藏感叹号(<=0 表示用 SightRange)

	UPROPERTY(BlueprintReadOnly, Category = "Animal|Behavior")
	bool bAlerting = false; // 当前是否处于警觉(思考/问号)状态

	float AlertRemaining = 0.f; // 警觉剩余思考时间(运行时)

	// 进入警觉：显示问号，启动随机思考计时
	void EnterAlert();
	// 真正开始逃跑：隐藏问号，显示感叹号
	void StartFlee();
	// 隐藏全部标记
	void HideMarks();

	// ============================================================
	//  需求系统
	// ============================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Needs")
	float Hunger = 0.f; // 0=饱足 100=极饿

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Needs")
	float Thirst = 0.f; // 0=不渴 100=极渴

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Needs")
	float HungerRate = 1.f; // 每秒饥饿增长

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Needs")
	float ThirstRate = 1.5f; // 每秒口渴增长

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Needs")
	float NeedThreshold = 60.f; // 超过此值触发寻找需求

	UPROPERTY(BlueprintReadOnly, Category = "Animal|Needs")
	bool bSeekingNeed = false; // 是否正在寻找食物/水

	// 处于饥饿/口渴(需求)状态时是否抑制 Spawn 行为；蓝图可关闭(关闭则照常生成)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Needs")
	bool bDisableSpawnWhenNeeding = true;

	// 当前需求状态(供 NeedWidget 在 UI 上按状态切换贴图：无/饿/渴/都)
	UPROPERTY(BlueprintReadOnly, Category = "Animal|Needs")
	EAnimalNeedState CurrentNeedState = EAnimalNeedState::None;

	// 需求状态变化通知(供 NeedWidget 绑定，实时切换贴图，无需每帧 Tick)
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNeedStateChanged, EAnimalNeedState, NewState);
	UPROPERTY(BlueprintAssignable, Category = "Animal|Needs")
	FOnNeedStateChanged OnNeedStateChanged;

	UPROPERTY(BlueprintReadOnly, Category = "Animal|Needs")
	bool bIsEating = false; // 是否正在进食(驱动吃动画)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Needs")
	float EatDuration = 3.f; // 进食动画持续时间(秒)
	float EatRemaining = 0.f; // 进食剩余时间

	// 注：需求图标与气泡文本已合并到同一个 BubbleWidgetComp 中显示，
	// 需求图标由 Widget 内部绑定 OwningAnimal 的 OnNeedStateChanged 自行切换，
	// 气泡文本由 ShowBubble() -> ShowBubbleText() 驱动。不再单独使用 NeedWidgetComp。

	// ============================================================
	//  群体系统
	// ============================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Flock")
	int32 FlockID = -1; // -1 表示无群体

	UPROPERTY(BlueprintReadWrite, Category = "Animal|Flock")
	bool bIsFlockLeader = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animal|Flock")
	TArray<AAnimalCharacter*> FlockMembers; // 仅 Leader 维护

	// ============================================================
	//  驯服系统
	// ============================================================
	UPROPERTY(BlueprintReadOnly, Category = "Animal|Tame")
	EAnimalTameState TameState = EAnimalTameState::Wild;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Tame")
	FName CompanionName; // 驯服后名字

	// ============================================================
	//  子物种（用于动画实例 blend 区分不同动物）
	// ============================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Species")
	EAnimalSpecies Species = EAnimalSpecies::Deer; // 物种类型：鹿/鸡/马

	// 该动物的文本集合(显示名/台词/需求提示)，每种动物在蓝图里指定一份即可
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Text")
	TObjectPtr<UAnimalTextCollection> TextCollection = nullptr;

	// 头顶合并 Widget：同时负责需求图标显示 + 气泡文本(气泡 UI)
	// Widget 类请在蓝图里直接给 BubbleWidgetComp 组件的 Widget Class 属性指定
	// (父类应为 AnimalBubbleWidget，自带 OwningAnimal + ShowBubbleText)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal|Bubble")
	UWidgetComponent* BubbleWidgetComp = nullptr; // 头顶合并 Widget 组件(运行时创建)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Bubble", meta = (ClampMin = "0.1"))
	float BubbleDuration = 3.f; // 气泡默认显示时长(秒)

	// 弹出一次气泡文本(从 TextCollection 取不到则忽略)；供 C++/蓝图调用
	UFUNCTION(BlueprintCallable, Category = "Animal|Bubble")
	void ShowBubble(const FText& Text, float Duration = -1.f);

	// 死亡掉落：被击杀时生成的 Actor 类(蓝图直接选择，如鸡腿/鹿肉等)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Drop")
	TSubclassOf<AActor> DropActorClass;

	// 死亡特效：从动物变成掉落物(鹿 -> 鹿肉)的变化过程表现(蓝图选择 Niagara 特效)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Drop")
	UNiagaraSystem* DeathEffect = nullptr;

	// 蓝图可调用：驯服/释放
	UFUNCTION(BlueprintCallable, Category = "Animal|Tame")
	void StartTaming();

	UFUNCTION(BlueprintCallable, Category = "Animal|Tame")
	void Release();

	// ============================================================
	//  战斗 / 血量系统
	// ============================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Combat", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.f; // 最大血量

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Combat", meta = (ClampMin = "0.0"))
	float Health = 100.f; // 当前血量(运行时)

	// 当前血量(供攻击方读取，决定 Die 或扣血)
	UFUNCTION(BlueprintCallable, Category = "Animal|Combat")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintCallable, Category = "Animal|Combat")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintCallable, Category = "Animal|Combat")
	bool IsDead() const { return bDie; }

	// 扣血(不致死，血量夹到 0)；返回扣血后的剩余血量
	UFUNCTION(BlueprintCallable, Category = "Animal|Combat")
	float ApplyDamage(float DamageAmount);

	// 受武器攻击：读取当前血量，耗尽则触发 Die，否则扣血(更新血量)。
	// 命中处理(读血 / 判死 / 扣血)统一放在动物自身；武器只传入一刀的伤害量。
	UFUNCTION(BlueprintCallable, Category = "Animal|Combat")
	void TakeWeaponDamage(float DamageAmount);

	// 死亡：生成掉落物并销毁(由攻击方在血量耗尽时调用)
	UFUNCTION(BlueprintCallable, Category = "Animal|Combat")
	void Die();

	// 攻击命中检测盒：比胶囊更贴合动物体型，专门作为武器伤害检测的命中体。
	// 仅查询(不产生物理碰撞)，并已关闭与自身胶囊(Pawn)的碰撞，二者互不碰撞。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal|Combat")
	UBoxComponent* DamageBox = nullptr;

	// 检测盒半尺寸(可在蓝图按动物体型调整)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Combat")
	FVector DamageBoxExtent = FVector(50.f, 50.f, 100.f);

	// ============================================================
	//  性能：感知禁用（按与玩家距离）
	// ============================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Perf")
	float AIDisableDistance = 5000.f; // 玩家超过此距离则停用 AI 逻辑

	UPROPERTY(BlueprintReadOnly, Category = "Animal|Perf")
	bool bAIActive = true; // 当前 AI 是否启用

	// ============================================================
	//  定时生成 Actor（原有功能保留）
	// ============================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TArray<FSpawnClassEntry> SpawnActorClasses;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	FName SpawnSocketName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	float SpawnInterval = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	bool bAutoSpawn = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	bool bRandomClass = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	bool bUseWeightedSpawn = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	int32 MaxSpawnCount = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Spawn")
	int32 SpawnedCount = 0;

	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void StartSpawning();

	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void StopSpawning();

	UFUNCTION(BlueprintCallable, Category = "Spawn")
	AActor* SpawnOnce();

	// 蓝图可覆写：被感知到威胁/猎物时的反应
	UFUNCTION(BlueprintNativeEvent, Category = "Animal|Behavior")
	void OnPerceiveThreat(AActor* Threat);

	// 蓝图可覆写：需求满足回调
	UFUNCTION(BlueprintNativeEvent, Category = "Animal|Needs")
	void OnNeedSatisfied(EAnimalNeed Need);

	// ============================================================
	//  AI 感知（视觉/听觉）
	// ============================================================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animal|Perception")
	UAIPerceptionComponent* PerceptionComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Perception")
	float SightAngleDegrees = 90.f; // 视锥半角(度)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Perception")
	float HearingRange = 800.f; // 听觉感知距离(唯一来源, 同步用于 UAIPerception 听觉配置)

	// 视为威胁的 Actor 标签(在编辑器中配置，例如 "Player")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Perception")
	TArray<FName> ThreatTags;

	// 视为猎物的 Actor 标签(捕食者使用，例如 "Prey")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Perception")
	TArray<FName> PreyTags;

	UFUNCTION(BlueprintCallable, Category = "Animal|Perception")
	void SetupPerception();

	// 感知回调
	UFUNCTION()
	void OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

protected:
	// 行为驱动
	void UpdateBehavior(float DeltaTime);
	void ExecuteActiveBehavior(float DeltaTime);
	void UpdateNeeds(float DeltaTime);
	void UpdateDayNight(bool bIsNight);
	void UpdateAIDistanceCull();

	// 工具：移动到目标点(通过 AIController)
	bool MoveToLocation(const FVector& Goal, float Speed);
	FVector GetRandomPointInNavMesh(float Radius) const;

	AAIController* GetAnimalController() const;

	// 运行时
	FVector SpawnOrigin = FVector::ZeroVector; // 出生点(游荡圆心)
	FTimerHandle SpawnTimerHandle;
	int32 SpawnClassIndex = 0;
	double NextManualSpawnTime = 0.0;
	AActor* DoSpawn();
	void SpawnTick();

	// 行为内部计时
	FTimerHandle BehaviorTimerHandle;
	int32 WaypointIndex = 0;
	int32 LastLogSecond = -1; // 状态日志限频用(记录上次打印的整秒)r

	// 当前移动目标(到达后才选下一个点，避免每帧抖动)
	FVector CurrentDestination = FVector::ZeroVector;
	bool bHasDestination = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	float ArriveThreshold = 120.f; // 视为到达的距离阈值

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	float WaitAtPointTimeMin = 1.f; // 到达后停留最短时长(秒)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Behavior")
	float WaitAtPointTimeMax = 3.f; // 到达后停留最长时长(秒)，Min>=Max 则取 Min(不停留)

	float WaitRemaining = 0.f; // 当前剩余停留时间

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animal|Debug")
	bool bEnableDebugLog = false; // 是否输出状态日志(蓝图可在运行时开关)
};
