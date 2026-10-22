// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class ShootEditorTarget : TargetRules
{
	public ShootEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		// UE5.8：DefaultBuildSettings 不设置时默认是 BuildSettingsVersion.V1（最老兼容模式），
		// 会把严格符合模式与各项警告等级降级，导致 Editor target 与 UnrealEditor 校验失败。
		// V7 即 BuildSettingsVersion.Latest，会一次性把这些设成引擎期望值。
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		CppStandard = CppStandardVersion.Cpp20;
		ExtraModuleNames.Add("Shoot");
	}
}
