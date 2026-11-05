// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Interaction/InteractionConfig.h"
#include "GameFramework/Actor.h"
#include "ResearchStove.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class ULocalPlayer;
class AItemActor;
class AShootCharacter;

/** 一条研究配方：投入指定食材（ItemID-数量）后研究一段时间，完成即解锁对应菜谱（FoodID） */
USTRUCT(BlueprintType)
struct FResearchRecipe
{
	GENERATED_BODY()

	/** 需要投入的食材：键 = 食材 ItemID（见 DT_ItemInfo），值 = 所需数量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Research")
	TMap<int32, int32> RequiredItems;

	/** 研究成功后解锁的菜谱（菜品 FoodID，见 DT_Foods） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Research")
	int32 UnlockFoodID = -1;

	/** 该配方的研究耗时（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Research", meta = (ClampMin = "0"))
	float ResearchTime = 5.f;

	/** 配方名称（仅用于显示 / 日志，便于排查） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Research")
	FString RecipeName;
};

/**
 * 研究炉：专门负责研究新菜谱。
 * 玩家手持食材靠近按 E 投入炉中；当投入的食材组合与某条研究配方完全匹配时自动开始研究，
 * 研究计时结束后解锁该配方对应的菜谱（FoodID，写入 UBaseGameInstance::UnlockedRecipeIDs 并广播）。
 *
 * 在关卡中摆放此 Actor 并在 Recipes 里配置配方即可；靠近时由重叠球把自身注册到玩家
 * （AShootCharacter::OverlappingResearchStove）。
 */
UCLASS(Blueprintable)
class SHOOT_API AResearchStove : public AActor
{
	GENERATED_BODY()

public:
	AResearchStove();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** 炉子外观模型（可在蓝图/实例上选择网格） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Components")
	UStaticMeshComponent* StoveMesh;

	/** 交互范围：玩家进入后注册为可投入食材的研究炉 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Components")
	USphereComponent* InteractionSphere;

	// ============ 交互判定尺度（统一标准，见 InteractionConfig.h） ============
	/** 交互判定半径。C++ 在 OnConstruction（编辑器视口）与 BeginPlay（运行时）都会强制应用，
	 *  会覆盖组件上残留的旧值；要单独调这台设备就改这里，不要直接改组件上的半径 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Interaction", meta = (ClampMin = "10.0"))
	float InteractionRadius = InteractionDistance::Counter;

	/** 判定球心相对 Actor 原点的高度：抬到物体中部，否则贴地的球只在脚踝高度有判定 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Interaction")
	float InteractionCenterHeight = InteractionDistance::CenterHeight;

	/** 编辑器里改上面的值时立即写进组件，关卡视口中即可看到真实判定球 */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 把 InteractionRadius（× Shoot.InteractScale 倍率）写进交互组件；OnConstruction/BeginPlay/运行时改倍率都会调用，必须幂等 */
	UFUNCTION()
	void ApplyInteractionScale();

	/** 交互提示控件（如“按 E 投入食材研究”）：靠近时显示，离开隐藏；Widget Class 由蓝图配置 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Components")
	TObjectPtr<UWidgetComponent> TipsWidget;

	/** 可研究的配方列表（在蓝图/实例上配置） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Research")
	TArray<FResearchRecipe> Recipes;

	/** 研究完成后是否清空炉内食材（默认 true） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Research")
	bool bClearAfterResearch = true;

	/** 当前炉内已投入的食材（ItemID 列表，含重复项以计数量） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Research")
	TArray<int32> StoredItemIDs;

	/** 是否正在研究中 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Research")
	bool bResearching = false;

	/** 研究剩余时间（秒） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Research")
	float ResearchRemainTime = 0.f;

	/** 当前正在研究的配方索引（未研究时为 INDEX_NONE） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shoot|Research")
	int32 ActiveRecipeIndex = INDEX_NONE;

	/**
	 * 玩家投入一个食材到炉中（食材被消耗销毁）。
	 * 投入后若与某条配方完全匹配则自动开始研究。
	 * @return true 表示投入成功；false 表示失败（无效食材/研究中/无 ItemID）
	 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Research")
	bool AddFood(AItemActor* Food);

	/** 清空炉内食材（正在研究则同时取消研究） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Research")
	void ClearFoods();

	/** 当前投入的食材是否匹配某条配方（返回索引；无匹配返回 INDEX_NONE） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Research")
	int32 FindMatchingRecipeIndex() const;

	/** 投入指定食材后是否仍"有可能"凑成某条配方（用于避免玩家把食材白白扔进炉里） */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Research")
	bool CanAcceptItem(int32 ItemID) const;

	/** 投入成功通知（蓝图可做特效/音效/UI） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Research")
	void OnFoodAdded(int32 ItemID, int32 TotalCount);

	/** 开始研究通知（蓝图可播放研究动画/进度条，Duration 为总时长） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Research")
	void OnResearchStart(int32 UnlockFoodID, float Duration);

	/** 研究完成并解锁菜谱通知 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Research")
	void OnResearchComplete(int32 UnlockFoodID);

	/** 研究/投入失败通知。Reason: "NoFood" / "NoItemID" / "Busy" / "NoRecipe"（该食材凑不出任何配方，不会被投入） */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shoot|Research")
	void OnResearchFailed(const FString& Reason);

	UFUNCTION()
	void OnInteractionOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnInteractionEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

protected:
	/** 匹配成功后开始研究 */
	void StartResearch(int32 RecipeIndex);

	/** 研究计时结束：解锁菜谱 */
	void FinishResearch();

	/** 写入 GameInstance 解锁菜谱并广播 */
	void UnlockRecipe(int32 FoodID);

	/** 当前占用提示控件的本地玩家（多人分屏隔离用） */
	UPROPERTY()
	TObjectPtr<ULocalPlayer> LocalPlayer;
};
