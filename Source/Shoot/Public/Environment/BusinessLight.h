#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BusinessLight.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;
class UTimeSystem;

/**
 * 营业灯饰：默认熄灭，仅在餐厅营业（UTimeSystem::bIsBusinessOpen）时点亮，
 * 模拟现实中“开门营业才亮灯”的效果。监听 TimeSystem 的营业状态变化自动开关，
 * 并在 BeginPlay 时按当前营业状态初始化（避免错峰亮灯/灭灯）。
 */
UCLASS(Blueprintable)
class SHOOT_API ABusinessLight : public AActor
{
	GENERATED_BODY()

public:
	ABusinessLight();

	virtual void BeginPlay() override;

	/** 灯泡外观（可选；蓝图可挂招牌 / 灯笼模型，营业时一并可见） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BulbMesh;

	/** 实际光源 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPointLightComponent> Light;

	/** 营业时灯的强度（打烊时为 0） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	float LitIntensity = 1500.f;

	/** 营业时灯的颜色（默认暖黄，营造营业氛围） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FLinearColor LitColor = FLinearColor(1.f, 0.78f, 0.42f);

	/** 营业时灯光影响半径（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	float LitRadius = 600.f;

	/** 根据营业状态开关灯（true=点亮） */
	UFUNCTION(BlueprintCallable, Category = "Light")
	void SetLit(bool bLit);

	/** 营业状态变化回调（绑定到 TimeSystem） */
	UFUNCTION()
	void HandleBusinessOpenChanged(bool bOpen);

protected:
	/** 绑定 TimeSystem 的营业状态委托；成功返回 TimeSystem 指针，失败返回 nullptr */
	UTimeSystem* BindTimeSystem();
};
