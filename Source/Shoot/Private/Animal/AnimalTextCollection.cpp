// UAnimalTextCollection 工具函数实现
#include "Animal/AnimalTypes.h"

FText UAnimalTextCollection::GetNeedLine(EAnimalNeedState State) const
{
	if (const FAnimalNeedLineSet* Set = NeedStateLines.Find(State))
	{
		if (Set->Lines.Num() > 0)
		{
			return Set->Lines[FMath::RandRange(0, Set->Lines.Num() - 1)];
		}
	}
	return FText::GetEmpty();
}

FText UAnimalTextCollection::GetRandomLine() const
{
	if (RandomLines.IsEmpty())
	{
		return FText::GetEmpty();
	}

	// 累计权重
	float TotalWeight = 0.f;
	for (const FAnimalTextLine& Line : RandomLines)
	{
		TotalWeight += FMath::Max(0.f, Line.Weight);
	}

	if (TotalWeight <= 0.f)
	{
		// 权重全为 0：退回均匀随机
		return RandomLines[FMath::RandRange(0, RandomLines.Num() - 1)].Text;
	}

	float Roll = FMath::FRandRange(0.f, TotalWeight);
	for (const FAnimalTextLine& Line : RandomLines)
	{
		Roll -= FMath::Max(0.f, Line.Weight);
		if (Roll <= 0.f)
		{
			return Line.Text;
		}
	}
	return RandomLines.Last().Text;
}
