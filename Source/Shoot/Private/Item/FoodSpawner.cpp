// Fill out your copyright notice in the Description page of Project Settings.

#include "Item/FoodSpawner.h"
#include "Game/BaseGameInstance.h"
#include "Engine/World.h"

namespace
{
	/**
	 * FNV-1a 32 位哈希：结果与平台、构建类型、进程运行次数完全无关，
	 * 因此可以拿它把「关卡名 + Actor 名」映射成一份稳定不变的存档 Key。
	 * （AActor::GetActorGuid 只在编辑器构建中存在，打包 exe 里拿不到，故不再使用。）
	 */
	uint32 StableHash32(const FString& Text, uint32 Seed)
	{
		uint32 Hash = 2166136261u ^ Seed;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const uint32 Code = static_cast<uint32>(Text[Index]);
			Hash = (Hash ^ (Code & 0xFFu)) * 16777619u;
			Hash = (Hash ^ ((Code >> 8) & 0xFFu)) * 16777619u;
		}

		// 收尾再混一次，避免不同 seed 生成的 32 位段之间相关性过强
		Hash ^= Hash >> 13;
		Hash *= 0x5BD1E995u;
		Hash ^= Hash >> 15;
		return Hash;
	}
}

FString AFoodSpawner::BuildSaveKeyText() const
{
	// 手动指定优先（重命名过箱子 / 运行时生成的箱子用这个固定住存档）
	if (!SpawnerKeyOverride.IsNone())
	{
		return SpawnerKeyOverride.ToString();
	}

	FString MapName;
	if (const UWorld* World = GetWorld())
	{
		// PIE 下地图名带 UEDPIE_0_ 前缀，去掉后与打包 exe 里读到的名字完全一致
		MapName = UWorld::RemovePIEPrefix(World->GetMapName());
	}

	// 同一关卡内 Actor 名唯一，因此「关卡名 + Actor 名」足以稳定定位一个箱子
	return FString::Printf(TEXT("%s|%s"), *MapName, *GetFName().ToString());
}

FGuid AFoodSpawner::GetSpawnerSaveGuid()
{
	if (!bSpawnerGuidResolved)
	{
		const FString KeyText = BuildSaveKeyText();

		// 用四个不同种子的哈希拼成 GUID：同一次构建、编辑器与打包版之间都保持一致
		const uint32 PartA = StableHash32(KeyText, 0x00000000u);
		const uint32 PartB = StableHash32(KeyText, 0x9E3779B9u);
		const uint32 PartC = StableHash32(KeyText, 0x85EBCA6Bu);
		const uint32 PartD = StableHash32(KeyText, 0xC2B2AE35u);

		SpawnerGuid = FGuid(PartA, PartB, PartC, PartD);
		bSpawnerGuidResolved = true;
	}
	return SpawnerGuid;
}

void AFoodSpawner::BeginPlay()
{
	Super::BeginPlay();

	// 仅有限模式参与存档；无限(-1)不需要持久化
	if (RemainingCount < 0)
	{
		return;
	}

	if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()))
	{
		const FGuid SaveGuid = GetSpawnerSaveGuid();
		const int32 Saved = GI->GetSpawnerRemaining(SaveGuid);
		if (Saved >= 0)
		{
			// 有存档记录：用存档值覆盖（玩家之前摘取过）
			RemainingCount = Saved;
		}
		else
		{
			// 首次出现：把编辑器里设置的初始值写入存档内存（落盘统一在关卡退出时由 BaseLevelBP::EndPlay 完成）
			GI->SetSpawnerRemaining(SaveGuid, RemainingCount);
		}
	}
}

void AFoodSpawner::ConsumeOne()
{
	// 仅在有限模式下递减；无限(-1)保持不动
	if (RemainingCount > 0)
	{
		--RemainingCount;
		// 写回存档内存（落盘统一在关卡退出时由 BaseLevelBP::EndPlay 完成）
		if (UBaseGameInstance* GI = Cast<UBaseGameInstance>(GetWorld()->GetGameInstance()))
		{
			GI->SetSpawnerRemaining(GetSpawnerSaveGuid(), RemainingCount);
		}
	}
}

void AFoodSpawner::OnSpawnActorEvent()
{
	OnSpawnActor.Broadcast();
}
