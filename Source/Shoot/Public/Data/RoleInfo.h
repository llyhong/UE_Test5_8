// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Engine/DataTable.h"
#include "RoleInfo.generated.h"

class USkeletalMesh;
class UAnimInstance;

USTRUCT(BlueprintType)
struct FRoleInfo : public FTableRowBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Data")
	int32 ModelID;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Data")
	TObjectPtr<UTexture2D> UIIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Data")
	FString Title;

	/** 角色显示名（优先于 Title；为空时界面回退到 Title） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Data")
	FText DisplayName;

	/** 角色介绍（选角界面显示） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Data")
	FText Description;

	/** 性格 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Data")
	FText Personality;

	/** 喜好（可填多条，界面用「、」拼接显示） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Data")
	TArray<FText> Likes;

	/** 是否出现在选角界面（占位 / 旧数据行可关掉） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Data")
	bool bSelectable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Data")
	TObjectPtr<USkeletalMesh> SkeletalMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Data")
	TSubclassOf<UAnimInstance> AnimClass;
};
