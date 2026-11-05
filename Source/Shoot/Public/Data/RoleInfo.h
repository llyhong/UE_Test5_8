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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Data")
	TObjectPtr<USkeletalMesh> SkeletalMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shoot|Data")
	TSubclassOf<UAnimInstance> AnimClass;
};
