#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FoliageOcclusionFaderSubsystem.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;

struct FFoliageFadeInstanceKey
{
	const UInstancedStaticMeshComponent* Component = nullptr;
	int32 InstanceIndex = INDEX_NONE;

	bool operator==(const FFoliageFadeInstanceKey& Other) const
	{
		return Component == Other.Component && InstanceIndex == Other.InstanceIndex;
	}

	friend uint32 GetTypeHash(const FFoliageFadeInstanceKey& Key)
	{
		return HashCombine(GetTypeHash(reinterpret_cast<UPTRINT>(Key.Component)), GetTypeHash(Key.InstanceIndex));
	}
};

/**
 * 视野遮挡淡出：当配置的植被实例（默认 SM_tree_01 / SM_tree_bush_01）
 * 挡在相机与玩家之间时，把它的 PerInstanceCustomData 逐帧推到 1，
 * 植被主材质（M_foliage / M_bark）里已有 PerInstanceCustomData -> OneMinus
 * -> DitherTemporalAA 链路，因此会以抖动方式淡出；玩家走开后恢复到 0。
 *
 * 全程纯数学判定，不依赖碰撞，植被不需要开碰撞。
 */
UCLASS(Config = Game)
class SHOOT_API UFoliageOcclusionFaderSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFoliageOcclusionFaderSubsystem();

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual TStatId GetStatId() const override;
	virtual void Tick(float DeltaTime) override;

private:
	struct FTrackedInstance
	{
		TWeakObjectPtr<UInstancedStaticMeshComponent> Component;
		int32 InstanceIndex = INDEX_NONE;
		FVector Center = FVector::ZeroVector;
		float Radius = 0.f;
		float CurrentFade = 0.f;
		float WrittenFade = -1.f;
	};

	void RebuildCache();
	void UpdateInstances(float DeltaTime);

	/** 总开关 */
	UPROPERTY(Config, EditAnywhere, Category = "Shoot|Foliage Fade")
	bool bEnabled = true;

	/** 需要参与淡出的植被网格（软路径），其余植被实例不受影响 */
	UPROPERTY(Config, EditAnywhere, Category = "Shoot|Foliage Fade")
	TArray<FSoftObjectPath> FadeMeshPaths;

	/** 材质里 PerInstanceCustomData 使用的通道索引（M_foliage/M_bark 用 0） */
	UPROPERTY(Config, EditAnywhere, Category = "Shoot|Foliage Fade")
	int32 CustomDataIndex = 0;

	/** 用实例包围球半径的比例来判定遮挡，1.0 = 球碰到视线即淡出 */
	UPROPERTY(Config, EditAnywhere, Category = "Shoot|Foliage Fade")
	float OcclusionRadiusScale = 0.85f;

	/** 判定遮挡时额外放宽的距离（厘米） */
	UPROPERTY(Config, EditAnywhere, Category = "Shoot|Foliage Fade")
	float OcclusionPadding = 40.f;

	/** 实例离相机近到这个范围（球半径 + 该值）也会淡出，避免相机穿进树里 */
	UPROPERTY(Config, EditAnywhere, Category = "Shoot|Foliage Fade")
	float NearCameraPadding = 120.f;

	/** 只处理离玩家这个距离内的实例（厘米） */
	UPROPERTY(Config, EditAnywhere, Category = "Shoot|Foliage Fade")
	float MaxPlayerDistance = 8000.f;

	/** 淡出插值速度（越大越快） */
	UPROPERTY(Config, EditAnywhere, Category = "Shoot|Foliage Fade")
	float FadeOutSpeed = 10.f;

	/** 恢复插值速度 */
	UPROPERTY(Config, EditAnywhere, Category = "Shoot|Foliage Fade")
	float FadeInSpeed = 4.f;

	/** 重建实例缓存的间隔（秒） */
	UPROPERTY(Config, EditAnywhere, Category = "Shoot|Foliage Fade")
	float RescanInterval = 2.f;

	TArray<FTrackedInstance> TrackedInstances;
	TSet<FString> FadeMeshPathStrings;
	float TimeSinceRebuild = 0.f;
	bool bCacheBuilt = false;
};
