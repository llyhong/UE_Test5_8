#pragma once

#include "CoreMinimal.h"
#include "Item/ItemActor.h"
#include "PlateCollectionTray.generated.h"

class ADishwasher;

UCLASS()
class SHOOT_API APlateCollectionTray : public AItemActor
{
	GENERATED_BODY()

public:
	APlateCollectionTray();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shoot|Tray", meta = (ClampMin = "1"))
	int32 Capacity = 3;

	/** 按商店里已购买的托盘档位刷新容量（4 / 6 / 8，取已购最高档；只增不减，蓝图自定义更大容量会保留）。
	 *  BeginPlay 自动调用一次；购买托盘后由商店立即调用，手上/场景里的托盘立刻生效。 */
	UFUNCTION(BlueprintCallable, Category = "Shoot|Tray")
	void RefreshCapacityFromUpgrades();

	UFUNCTION(BlueprintPure, Category = "Shoot|Tray")
	int32 GetPlateCount() const;

	bool HasRoom() const { return GetPlateCount() < Capacity; }
	void AddPlate(AItemActor* Plate);
	int32 WashPlates(ADishwasher& Washer);

private:
	UPROPERTY()
	TArray<TObjectPtr<AItemActor>> Plates;
};
