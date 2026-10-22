// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/DialogueWidget.h"

void UDialogueWidget::ReportButtonClicked(int32 ButtonID)
{
	OnButtonClicked.Broadcast(ButtonID);
}
