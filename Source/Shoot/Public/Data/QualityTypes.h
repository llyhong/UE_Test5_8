// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "QualityTypes.generated.h"

/**
 * 食材 / 成品品质。
 * - Normal  普通：未成熟时采摘，或无可判定品质的素材（如箱子里取出的通用食材）。
 * - Premium 佳品：素材在“成熟”窗口内采摘（如番茄成熟时摘下）。
 * - Inferior次品：素材在“熟过头”后采摘（如番茄熟过头才摘下）。
 * 成品品质 = 所有入锅食材中的最差品质（任一为次品则成品为次品）。
 */
UENUM(BlueprintType)
enum class EIngredientQuality : uint8
{
	Normal   UMETA(DisplayName = "普通"),
	Premium  UMETA(DisplayName = "佳品"),
	Inferior UMETA(DisplayName = "次品"),
};

/** 品质排序辅助：数值越小越差（用于聚合，成品取最差食材品质）。
 *  顺序：Inferior(0) < Normal(1) < Premium(2)。 */
inline int32 QualityRank(EIngredientQuality Q)
{
	switch (Q)
	{
	case EIngredientQuality::Inferior: return 0;
	case EIngredientQuality::Normal:   return 1;
	case EIngredientQuality::Premium:  return 2;
	default:                          return 1;
	}
}

/** 取两者中较差（排序更靠后）的品质 */
inline EIngredientQuality MinQuality(EIngredientQuality A, EIngredientQuality B)
{
	return QualityRank(A) <= QualityRank(B) ? A : B;
}
