// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "Engine/DataTable.h"
#include "UI/ShopWidget.h"
#include "ShopScreenWidget.generated.h"

class AShootCharacter;
class APlayerController;
class UBorder;
class UCanvasPanel;
class UTextBlock;
class UDataTable;
class USoundBase;
class UScrollBox;

/**
 * 食材商店配置行（RowStruct = FShopIngredientRow）。
 * 可选数据表：/Game/_Demo/Data/DT_ShopIngredients。找不到时用控件内置的默认价格（略贵）。
 */
USTRUCT(BlueprintType)
struct FShopIngredientRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 食材 ItemID（与 DT_FoodID 行名一致）；行名为纯数字时可留 -1，按行名解析 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Shop")
	int32 ItemID = -1;

	/** 单件售价 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Shop")
	int32 Price = 0;

	/** 是否上架（false = 本行不卖） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Shop")
	bool bAvailable = true;

	/** 显示名覆盖（留空用物品表里的名字） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Shop")
	FString DisplayNameOverride;
};

/** 商店卡片上的购买按钮：记录商品 ID 与分类，点击时回调所属商店界面 */
UCLASS()
class SHOOT_API UShopBuyButton : public UButton
{
	GENERATED_BODY()

public:
	UShopBuyButton();

	void InitOffer(class UShopScreenWidget* InOwner, int32 InItemID, bool bInEquipment);

	int32 ItemID = 0;
	bool bEquipment = false;

	TWeakObjectPtr<class UShopScreenWidget> OwnerWidget;

private:
	UFUNCTION()
	void HandleClicked();
};

/**
 * 田园杂货铺（C++ 构建，无需 WBP）：
 * - 画风与选角界面一致：奶油/蜂蜜色卡片、圆角描边、叶片装饰、Bold 字体 + 描边；
 * - 背景使用实时模糊（UBackgroundBlur）+ 奶油色薄纱，不额外占用背景图；
 * - 两个页签：设备（托盘三档 / 煎锅升级 / 榨汁机）与食材（价格可用数据表覆盖）；
 * - 购买结果写入 UEconomySystem（道具计数随存档持久化），食材直接放进玩家背包。
 *
 * 入口：AShootCharacter::ToggleShop（P 键）→ UShopScreenWidget::ToggleForPlayer。
 */
UCLASS()
class SHOOT_API UShopScreenWidget : public UShopWidget
{
	GENERATED_BODY()

public:
	UShopScreenWidget(const FObjectInitializer& ObjectInitializer);

	/** 打开/关闭某位本地玩家的商店（每位玩家独立一份） */
	static void ToggleForPlayer(APlayerController* PC, AShootCharacter* Character);

	/** 刷新金钱显示与当前页签内容 */
	void Refresh();

	/** 购买设备（卡片按钮回调） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Shop")
	void BuyEquipment(int32 ItemID);

	/** 购买一份食材并放进玩家背包（卡片按钮回调） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Shop")
	void BuyIngredient(int32 ItemID);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UFUNCTION()
	void HandleMoneyChangedInternal(int32 NewValue, int32 Delta, EMoneySource Source);

	UFUNCTION()
	void OnEquipmentTabClicked();

	UFUNCTION()
	void OnIngredientTabClicked();

	UFUNCTION()
	void OnCloseClicked();

private:
	void SelectTab(bool bEquipmentTab);
	void RebuildContent();
	void RebuildEquipmentCards();
	void RebuildIngredientCards();
	void ApplyEquipmentEffect(int32 ItemID);
	void UpdateMoneyText();
	void ShowToast(const FString& Message, bool bSuccess);
	void HideToast();
	void SetUIInputEnabled(bool bEnabled);
	void CloseSelf();

	void GatherIngredientSources();
	int32 GetIngredientPrice(int32 ItemID, bool& bOutAvailable) const;
	FString GetItemDisplayNameSafe(int32 ItemID) const;

	class UWidget* BuildOfferCard(int32 OfferIndex, int32 Level, const FString& StatusText,
		bool bButtonEnabled, const FString& ButtonLabel);
	class UWidget* BuildIngredientCard(int32 ItemID, const FString& ItemName, class UTexture2D* Icon,
		bool bWildOnly, int32 Price, bool bAffordable);

	UPROPERTY()
	TObjectPtr<UCanvasPanel> ContentHost = nullptr;

	UPROPERTY()
	TObjectPtr<UTextBlock> MoneyText = nullptr;

	UPROPERTY()
	TObjectPtr<UTextBlock> InventoryHintText = nullptr;

	UPROPERTY()
	TObjectPtr<UBorder> ToastPanel = nullptr;

	UPROPERTY()
	TObjectPtr<UTextBlock> ToastText = nullptr;

	UPROPERTY()
	TObjectPtr<UButton> TabEquipmentButton = nullptr;

	UPROPERTY()
	TObjectPtr<UButton> TabIngredientButton = nullptr;

	UPROPERTY()
	TObjectPtr<UTextBlock> TabEquipmentText = nullptr;

	UPROPERTY()
	TObjectPtr<UTextBlock> TabIngredientText = nullptr;

	UPROPERTY()
	TObjectPtr<UDataTable> IngredientTable = nullptr;

	UPROPERTY()
	TObjectPtr<USoundBase> BuySound = nullptr;

	UPROPERTY()
	TObjectPtr<USoundBase> FailSound = nullptr;

	TWeakObjectPtr<APlayerController> OwnerController;
	TWeakObjectPtr<AShootCharacter> OwnerPawn;

	/** 食材 ItemID -> 可生成物品类 / 物品表行（首次打开商店时扫描一次） */
	TMap<int32, TSubclassOf<class AItemActor>> IngredientClasses;
	TMap<int32, FDataTableRowHandle> IngredientRows;
	bool bGatheredIngredients = false;
	bool bTriedLoadIngredientTable = false;
	bool bBoundMoney = false;

	bool bEquipmentTab = true;
	FTimerHandle ToastTimer;
};
