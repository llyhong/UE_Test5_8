#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "StarSettlementData.generated.h"

/** 星级理由取哪个当日快照字段。 */
UENUM(BlueprintType)
enum class EStarReasonStat : uint8
{
	Served    		UMETA(DisplayName = "服务客人数"),
	Failed    		UMETA(DisplayName = "失败订单"),
	Timeouts  		UMETA(DisplayName = "超时订单"),
	AvgWait   		UMETA(DisplayName = "平均等待(秒)"),
	WildServed		UMETA(DisplayName = "野味订单"),
	Profit    		UMETA(DisplayName = "当日营业利润"),
	Revenue   		UMETA(DisplayName = "当日营收"),
	Reputation		UMETA(DisplayName = "当前声誉"),
};

/** 达标方向：AtLeast = 实际值 >= 阈值算达标；AtMost = 实际值 <= 阈值算达标。 */
UENUM(BlueprintType)
enum class EStarReasonCompare : uint8
{
	AtLeast UMETA(DisplayName = "不低于阈值达标"),
	AtMost  UMETA(DisplayName = "不高于阈值达标"),
};

/** DT_StarTiers 的行结构：一档星级一行（Stars = 1..5）。 */
USTRUCT(BlueprintType)
struct FStarTierRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 星级（1~5） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star Settlement", meta = (ClampMin = "1", ClampMax = "5"))
	int32 Stars = 1;

	/** 达到该星级所需的最低日评级分（LastGradeScore） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star Settlement")
	int32 MinScore = 0;

	/** 档位名：完美 / 出色 / 合格 / 勉强 / 失利 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star Settlement")
	FString TierName;

	/** 定性文案：干得漂亮！/ 稳步前进 等 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star Settlement")
	FString Phrase;

	/** 星星 / 文案主色（十六进制，如 F0B93F） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star Settlement")
	FString AccentHex = TEXT("F0B93F");

	/** 还没到顶星时的差距提示；{0}=差距分 {1}=下一星数 {2}=下一档名 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star Settlement")
	FString GapFormat = TEXT("再得 {0} 分升到 {1} 星 · {2}");

	/** 已满星时的提示 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star Settlement")
	FString MaxPhrase = TEXT("已是最高评价，保持住！");
};

/** DT_StarReasons 的行结构：一条星级理由一行，最多显示前 3 条（按 SortOrder）。 */
USTRUCT(BlueprintType)
struct FStarReasonRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 显示顺序（小的在前） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star Settlement")
	int32 SortOrder = 0;

	/** 取哪个当日字段 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star Settlement")
	EStarReasonStat Stat = EStarReasonStat::Served;

	/** 达标方向 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star Settlement")
	EStarReasonCompare Compare = EStarReasonCompare::AtLeast;

	/** 判定阈值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star Settlement")
	float Threshold = 0.f;

	/** 达标文案；{0}=实际值 {1}=阈值（可写 ✓ 图标） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star Settlement")
	FString PassText;

	/** 未达标文案；{0}=实际值 {1}=阈值（可写 ○ 图标） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star Settlement")
	FString FailText;

	/** 勾选后只在达标时显示该条（默认关闭 = 达标/未达标都显示，用勾/圈区分） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star Settlement")
	bool bOnlyShowWhenPassed = false;
};
