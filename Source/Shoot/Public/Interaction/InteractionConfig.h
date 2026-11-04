// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

class UWorld;

/**
 * 全局统一的交互判定尺度。
 *
 * 关键几何：Overlap 判定发生在「交互球/盒」与「角色胶囊（半径 35、半高 90）」之间，
 * 所以玩家真正感觉到的触发距离 ≈ 交互半径 + 35。
 * 同时球心必须抬到物体中部，否则贴地的球只在脚踝高度有判定，
 * 表现就是“明明站在旁边，按 E 却没反应”。
 *
 * 想整体调松/调紧，只改这里一处，全工程的交互范围同步变化。
 */
namespace InteractionDistance
{
	/*
	 * 数值口径：这里的半径是「交互球半径」，实际手感距离 = 半径 + 角色胶囊半径(35) - 物体自身半宽。
	 * 目标手感：站到物体跟前（离表面 0~40cm）即可交互，不要隔着半张桌子就能按 E。
	 * 下表按典型物件尺寸估算的「离物体表面」距离，便于后续微调：
	 *
	 *   Item     55 → 44  小球贴着地面食材：表面约 25~30cm
	 *   Plant    80 → 64  树/采集点：表面约 25cm
	 *   Machine  95 → 76  灶台（半宽 50）：贴到台面跟前才触发
	 *   Counter  95 → 76  上菜口/洗碗机：同上
	 *   Attract 105 → 84  招揽台：略宽，方便跑动中触发
	 *   Gate    120 → 96  大门：仍留少量余量，避免必须贴门
	 *   Guest    120      客人/餐椅：不参与 ×0.8 收紧，点单/上菜需要余量
	 *
	 * 本次在上一版基础上整体 ×0.8。想临时试更紧/更松不用改这里：
	 * 游戏里控制台 Shoot.InteractScale 0.8 / 1.2，0.15s 内生效，定好数值再回填这张表。
	 */
	/** 地面散落的食材/成品（约一步之内） */
	constexpr float Item = 44.f;
	/** 植株、采集点 */
	constexpr float Plant = 64.f;
	/** 灶台、砧板、水槽等台面设备 */
	constexpr float Machine = 76.f;
	/** 上菜口、洗碗机、研究炉、仓库 */
	constexpr float Counter = 76.f;
	/** 招揽台 */
	constexpr float Attract = 84.f;
	/** 大门（营业开关） */
	constexpr float Gate = 96.f;
	/** 客人、餐椅（点单/上菜范围）。这两类不跟随 ×0.8 收紧：
	 *  客人会走动、餐椅要从侧面递菜，半径太小会出现“明明站在桌边却接不了单”。 */
	constexpr float Guest = 120.f;

	/** 交互球心相对 Actor 原点多出的高度：抬到物体中部，保证竖直方向也落在胶囊上 */
	constexpr float CenterHeight = 60.f;

	/** 台面设备的盒体半高（Z）：配合 CenterHeight 覆盖 0~180，正好是角色胶囊全高 */
	constexpr float MachineHalfHeight = 90.f;

	/** 灶台/砧板/水槽的交互盒水平半径（X/Y）。
	 *  在 Machine 基准(76)上再收紧 40% → 46：灶台是玩家最贴身操作的设备，
	 *  半径一大就会“人还站在过道中间，E 却已经作用到灶台上”，也会抢走脚边食材的交互。
	 *  注意：盒体小于灶台网格footprint时，玩家会被网格挡在外面而永远进不了盒体 → 失效；
	 *  所以这类长条台面请在蓝图里单独把 InteractionBoxExtent 的 X/Y 调大。 */
	constexpr float MachineBox = 46.f;
}

/** 调试可视化总开关：控制台输入 Shoot.DebugInteract 1 打开，0 关闭 */
extern TAutoConsoleVariable<int32> CVarDebugInteract;

/**
 * 运行时半径倍率：控制台 Shoot.InteractScale 0.5 / 1.2 ...
 *
 * 只影响"交互判定球/盒"的尺寸，不影响物体本体。
 * 改完下一次角色刷新（0.15s 内）就会自动重设世界上所有交互球，
 * 不用重编、不用重启关卡，可以直接在游戏里试手感。
 * 定好手感后把 InteractionDistance 里的基准值按这个倍率手工调好即可。
 */
namespace InteractionScale
{
	/** 当前倍率（已做安全钳制，防止误输入 0 或离谱数值） */
	float Get();

	/** 把所有交互宿主（物品/植株/设备/客人/餐桌/仓库/对话）的判定球按当前倍率重设一遍 */
	void RefreshAllInteractionScales(UWorld* World);
}

/**
 * 交互的"朝向权重"：不只看谁近，还要看玩家脸朝哪。
 *
 * 用的口径不是硬性剔除（背后照样能交互），而是把"偏离正前方"折算成距离惩罚：
 *   有效距离 = 真实距离 × (1 + 权重 × 偏离度)，偏离度：正前方 0、正侧面 0.5、正后方 1。
 * 权重 1.2 时，正后方目标的有效距离是真实的 2.2 倍 —— 背后的 40cm 蔬菜
 * （44×2.2≈97）会输给正前方 90cm 的那个，不会再出现"一按 E 就转身给背后浇水"。
 *
 * 控制台 Shoot.InteractFacing 1.2 可实时调（0 = 完全关闭，退回纯距离）；
 * 定好手感后把默认值写回这里。
 */
namespace InteractionFacing
{
	/** 当前朝向权重（已钳制在 0~4） */
	float GetPenaltyScale();
}

/** 朝向权重的基准值：正后方目标的有效距离 = 真实距离 × (1 + 该值) */
constexpr float InteractFacingPenalty = 1.2f;

namespace InteractionDebug
{
	/** 交互范围可视化是否开启（仅可在 GameThread 调用） */
	FORCEINLINE bool IsEnabled()
	{
		return CVarDebugInteract.GetValueOnGameThread() > 0;
	}

	// 统一颜色约定
	FORCEINLINE FColor ColorCurrentTarget() { return FColor::Green; }   // 当前目标
	FORCEINLINE FColor ColorInRange()       { return FColor::White; }   // 在范围内但不是目标
	FORCEINLINE FColor ColorBlocked()       { return FColor::Red; }     // 在范围内但被墙/台面挡住
	FORCEINLINE FColor ColorIdle()          { return FColor(90, 90, 90); } // 无人靠近
}
