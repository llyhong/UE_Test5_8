// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "EconomySystem.generated.h"

/** 内置示例商店道具的 ID（购买后由对应系统读取数量来生效）。
 *  若你在 GameInstance 的 EconomyDefaultShopItems / 商店数据表里改用其它 ID，请同步修改这里。 */
namespace EconomyShopItemIDs
{
	constexpr int32 ExtraTable = 1001;    // 新餐桌：提升店铺可容纳顾客数
	constexpr int32 AdvancedStove = 1002; // 高级灶台：加快烹饪速度
	constexpr int32 NeonSign = 1003;      // 霓虹招牌：提升招揽顾客数量
}

/**
 * 经济/声誉配置行（数据表 RowStruct = FEconomyConfigRow）。
 * 在编辑器中创建一张 DataTable（行结构选 FEconomyConfigRow）即可配置下列数值，
 * 未配置 ConfigTable 时回退到 UEconomySystem::DefaultConfig 内置默认值。
 */
USTRUCT(BlueprintType)
struct FEconomyConfigRow : public FTableRowBase
{
	GENERATED_BODY()
public:
	/** 完成订单时增加的声誉 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	int32 ReputationGainOnComplete = 5;

	/** 订单失败（超时未上）时扣除的声誉 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	int32 ReputationLossOnFail = 3;

	/** 完成订单的固定金钱奖励（与价格奖励叠加） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	int32 MoneyPerOrder = 0;

	/** 完成订单金钱 = MoneyPerOrder + OrderPrice * MoneyPriceMultiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	float MoneyPriceMultiplier = 1.0f;

	/** 完成订单至少获得的金钱（避免价格为 0 时白干） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	int32 MinMoneyOnComplete = 1;
};

/**
 * 商店商品配置行（数据表 RowStruct = FEconomyShopItem）。
 * 在编辑器中创建一张 DataTable（行结构选 FEconomyShopItem）即可配置可购买道具。
 */
USTRUCT(BlueprintType)
struct FEconomyShopItem : public FTableRowBase
{
	GENERATED_BODY()
public:
	/** 道具唯一 ID（与 BuyItem(ItemID) 对应） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	int32 ItemID = 0;

	/** 展示名称 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	FText DisplayName;

	/** 购买花费的金钱 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	int32 Cost = 0;

	/** 描述 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	FText Description;

	/** 图标（可选） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	TObjectPtr<UTexture2D> Icon = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEconomyChanged, int32, NewValue, int32, Delta);

/** 金钱变动来源：用于区分是否计入关卡“金钱目标”等 */
UENUM(BlueprintType)
enum class EMoneySource : uint8
{
	None			UMETA(DisplayName = "无/其它"),
	OrderComplete	UMETA(DisplayName = "订单完成"),
	DailyIncome		UMETA(DisplayName = "每日收入"),
	YearlyPrize		UMETA(DisplayName = "年末奖金"),
	ShopPurchase	UMETA(DisplayName = "商店消费"),
	Maintenance		UMETA(DisplayName = "设施维护"),
	Wages			UMETA(DisplayName = "工资支出"),
	Debug			UMETA(DisplayName = "调试"),
};

/** 金钱变化广播（NewValue, Delta, Source）。声誉仍用 FEconomyChanged（无来源） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEconomyMoneyChanged, int32, NewValue, int32, Delta, EMoneySource, Source);

/**
 * 经济系统：统一管理 声誉(Reputation) 与 金钱(Money)。
 * - 订单完成 → 增加声誉 + 获得金钱
 * - 订单失败 → 扣除声誉
 * - 金钱可用于购买/升级道具（BuyItem / SpendMoney）
 * 数据持久化到 UMySaveGame（随存档保存）。
 * 由 UBaseGameInstance 持有并初始化。
 */
UCLASS(Blueprintable)
class SHOOT_API UEconomySystem : public UObject
{
	GENERATED_BODY()

public:
	/** 配置数据表（RowStruct = FEconomyConfigRow）。为空则用内置默认值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	UDataTable* ConfigTable = nullptr;

	/** 配置表行名 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	FName ConfigRowName = TEXT("Default");

	/** 内置默认配置（未配置 ConfigTable 时使用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy")
	FEconomyConfigRow DefaultConfig;

	/** 商店商品数据表（RowStruct = FEconomyShopItem）。为空则用内置默认商品列表 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Shop")
	UDataTable* ShopItemsTable = nullptr;

	/** 内置默认商品列表（未配置 ShopItemsTable 时使用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy|Shop")
	TArray<FEconomyShopItem> DefaultShopItems;

	/** 返回当前生效的商店商品列表（优先取数据表，否则取内置默认） */
	UFUNCTION(BlueprintCallable, Category = "Economy|Shop")
	TArray<FEconomyShopItem> GetShopItems() const;

	/** 初始化：读取配置表并加载存档数据 */
	UFUNCTION(BlueprintCallable, Category = "Economy")
	void Init();

	UFUNCTION(BlueprintCallable, Category = "Economy")
	int32 GetReputation() const { return Reputation; }

	UFUNCTION(BlueprintCallable, Category = "Economy")
	int32 GetMoney() const { return Money; }

	/** 订单完成：增加声誉 + 获得金钱（Money 由 OrderPrice 换算） */
	UFUNCTION(BlueprintCallable, Category = "Economy")
	void OnOrderCompleted(int32 OrderPrice);

	/** 订单失败：扣除声誉 */
	UFUNCTION(BlueprintCallable, Category = "Economy")
	void OnOrderFailed();

	/** 花费金钱：足够则扣减并返回 true，不足返回 false */
	UFUNCTION(BlueprintCallable, Category = "Economy")
	bool SpendMoney(int32 Amount, EMoneySource Source = EMoneySource::None);

	/** 购买/升级道具：花费 Cost 金钱，成功后该道具计数 +1 */
	UFUNCTION(BlueprintCallable, Category = "Economy")
	bool BuyItem(int32 ItemID, int32 Cost);

	/** 已拥有某道具的数量 */
	UFUNCTION(BlueprintCallable, Category = "Economy")
	int32 GetOwnedItemCount(int32 ItemID) const;

	/** 直接增减（调试/外部调用） */
	UFUNCTION(BlueprintCallable, Category = "Economy")
	void AddReputation(int32 Delta);

	UFUNCTION(BlueprintCallable, Category = "Economy")
	void AddMoney(int32 Delta, EMoneySource Source = EMoneySource::None);

	/** 声誉变化广播（NewValue, Delta） */
	UPROPERTY(BlueprintAssignable, Category = "Economy")
	FEconomyChanged OnReputationChanged;

	/** 金钱变化广播（NewValue, Delta, Source） */
	UPROPERTY(BlueprintAssignable, Category = "Economy")
	FEconomyMoneyChanged OnMoneyChanged;

	/** 写入存档 */
	void Save();
	/** 从存档读取 */
	void Load();

private:
	int32 Reputation = 0;
	int32 Money = 0;
	TMap<int32, int32> OwnedProps;

	/** 解析后的最终配置（Init 时确定） */
	FEconomyConfigRow ResolvedConfig;
};
