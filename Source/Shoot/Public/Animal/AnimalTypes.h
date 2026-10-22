// 动物生态系统：共享类型定义
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/DataAsset.h"
#include "AnimalTypes.generated.h"

class AAnimalCharacter; // 前向声明，避免与 AnimalCharacter.h 循环依赖

// 主动行为（日常活动）
UENUM(BlueprintType)
enum class EAnimalActiveBehavior : uint8
{
	Idle			UMETA(DisplayName = "Stay In Place"),
	RoamRandom		UMETA(DisplayName = "前往导航网格随机位置"),
	RoamInRange		UMETA(DisplayName = "前往指定范围内随机地点"),
	WaypointRandom	UMETA(DisplayName = "前往随机航点"),
	WaypointPath	UMETA(DisplayName = "沿航点路径行进"),
	GoToPOI			UMETA(DisplayName = "前往附近景点"),
	GoToHome		UMETA(DisplayName = "前往 Homebase"),
	SeekNeed		UMETA(DisplayName = "寻找食物/水源(需求)")
};

// 交战行为（感知到威胁/猎物时）
UENUM(BlueprintType)
enum class EAnimalEngageBehavior : uint8
{
	Ignore			UMETA(DisplayName = "忽略"),
	Flee			UMETA(DisplayName = "逃跑"),
	Defend			UMETA(DisplayName = "保卫"),
	Attack			UMETA(DisplayName = "攻击")
};

// 驯服状态
UENUM(BlueprintType)
enum class EAnimalTameState : uint8
{
	Wild			UMETA(DisplayName = "野生"),
	Taming			UMETA(DisplayName = "驯服中"),
	Tamed			UMETA(DisplayName = "已驯服")
};

// 动物角色
UENUM(BlueprintType)
enum class EAnimalRole : uint8
{
	Prey			UMETA(DisplayName = "猎物"),
	Predator		UMETA(DisplayName = "捕食者")
};

// 需求类型
UENUM(BlueprintType)
enum class EAnimalNeed : uint8
{
	Food			UMETA(DisplayName = "食物"),
	Water			UMETA(DisplayName = "水源")
};

// 需求显示状态(供 NeedWidget 在 UI 上按状态切换贴图)
UENUM(BlueprintType)
enum class EAnimalNeedState : uint8
{
	None			UMETA(DisplayName = "无需求"),
	Food			UMETA(DisplayName = "饿了"),
	Water			UMETA(DisplayName = "渴了"),
	Both			UMETA(DisplayName = "又饿又渴")
};

// 子物种类型（用于动画实例区分不同动物的骨骼/动画 blend）
UENUM(BlueprintType)
enum class EAnimalSpecies : uint8
{
	Deer			UMETA(DisplayName = "鹿"),
	Chicken			UMETA(DisplayName = "鸡"),
	Horse			UMETA(DisplayName = "马")
};

// 需求头顶 Widget 的 C++ 基类：自带 AnimalCharacter 引用，
// 解决 UMG Widget 没有 "Get Owning Actor" 无法反查 Animal 的问题。
// 在编辑器里把你的 NeedWidget 蓝图父类设为 NeedUserWidget 即可直接读取 OwningAnimal。
UCLASS()
class SHOOT_API UNeedUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 指向挂载该 Widget 的 AnimalCharacter（由 C++ 在创建时注入）
	UPROPERTY(BlueprintReadOnly, Category = "Animal|Needs")
	TObjectPtr<AAnimalCharacter> OwningAnimal = nullptr;
};

// 按事件/状态组织的动物文本行
USTRUCT(BlueprintType)
struct FAnimalTextLine
{
	GENERATED_BODY()

	// 对应的事件/状态文本（如 发现玩家/逃跑/饥饿/口渴 等）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText Text;

	// 权重：GetRandomLine 时按权重随机抽取，越大越容易抽到
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (ClampMin = "0.0"))
	float Weight = 1.f;
};

// 某一需求状态对应的多条台词（包装 TArray，便于作为 TMap 的 value）
USTRUCT(BlueprintType)
struct FAnimalNeedLineSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	TArray<FText> Lines;
};

// 动物文本集合 DataAsset：
// 每种动物一份资产（如 DA_Deer_Text / DA_Chicken_Text），
// 存放该动物的显示名、各行为状态的台词，供 AnimalCharacter 与 UI 统一读取。
UCLASS(BlueprintType)
class SHOOT_API UAnimalTextCollection : public UDataAsset
{
	GENERATED_BODY()

public:
	// 动物显示名称（如 鹿 / 鸡 / 马），可用于 UI
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText DisplayName;

	// 警觉/发现玩家时的台词（显示 "?" 阶段）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText AlertLine;

	// 开始逃跑时的台词（显示 "!" 阶段）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText FleeLine;

	// 日常待机随机台词（Idle/Roam 时偶尔发出）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	TArray<FAnimalTextLine> RandomLines;

	// 需求状态台词：无需求/饿/渴/又饿又渴 → 供 BubbleWidget 显示（每个状态可配多条，按状态取后随机抽一条）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	TMap<EAnimalNeedState, FAnimalNeedLineSet> NeedStateLines;

	// 工具：按 EAnimalNeedState 取需求台词；未配置则返回空
	UFUNCTION(BlueprintPure, Category = "Text")
	FText GetNeedLine(EAnimalNeedState State) const;

	// 工具：从 RandomLines 按权重随机取一行；为空则返回空
	UFUNCTION(BlueprintPure, Category = "Text")
	FText GetRandomLine() const;
};

// 头顶气泡 Widget 的 C++ 基类（继承 NeedUserWidget，自带 OwningAnimal 引用）。
// 在编辑器里把你的气泡 Widget 蓝图父类设为 AnimalBubbleWidget，
// 并实现 ShowBubbleText 来把文本显示到一个 TextBlock，并在 Duration 后隐藏。
UCLASS()
class SHOOT_API UAnimalBubbleWidget : public UNeedUserWidget
{
	GENERATED_BODY()

public:
	// 显示一次气泡文本，Duration 秒后自动隐藏（由蓝图实现具体表现）
	UFUNCTION(BlueprintImplementableEvent, Category = "Animal|Bubble")
	void ShowBubbleText(const FText& Text, float Duration);
};
