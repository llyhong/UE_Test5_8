#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Game/OrderSystem.h"
#include "Game/BusinessSimulation.h"
#include "Game/BusinessTime.h"
#include "Game/EconomySystem.h"
#include "Game/SeasonSystem.h"
#include "MySaveGame.h"
#include "Game/BaseGameInstance.h"
#include "Kismet/GameplayStatics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrepDeadlineTest, "Shoot.Gameplay.PrepDeadlineNeverShrinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPrepDeadlineTest::RunTest(const FString&)
{
	UOrderHandle* Order = NewObject<UOrderHandle>();
	Order->NormalWaitTime = 48.f;
	Order->LateServeGraceSeconds = 8.f;
	Order->SetPrepWindowGraceSeconds(8.f);
	const float Before = Order->GetOrderTimeout();
	TestEqual(TEXT("Prep adds eight seconds to a 56 second deadline"), Before, 64.f);
	Order->SetPrepWindowGraceSeconds(0.f);
	TestEqual(TEXT("Closing a prep window retains the granted deadline"), Order->GetOrderTimeout(), Before);
	Order->SetPrepWindowGraceSeconds(16.f);
	TestEqual(TEXT("A larger grace extends the deadline"), Order->GetOrderTimeout(), 72.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInvestmentLedgerTest, "Shoot.Gameplay.InvestmentIsNotOperatingLoss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FInvestmentLedgerTest::RunTest(const FString&)
{
	UEconomySystem* Economy = NewObject<UEconomySystem>();
	UBusinessSimulation* Business = NewObject<UBusinessSimulation>();
	Business->Init(Economy);
	Economy->AddMoney(1000, EMoneySource::Debug);
	Economy->AddMoney(500, EMoneySource::OrderComplete);
	TestTrue(TEXT("Can buy equipment"), Economy->SpendMoney(700, EMoneySource::ShopPurchase));
	TestTrue(TEXT("Can pay maintenance"), Economy->SpendMoney(20, EMoneySource::Maintenance));
	const FBusinessReport Report = Business->GetTodayReport();
	TestEqual(TEXT("Cash flow includes equipment"), Report.Net, -220);
	TestEqual(TEXT("Operating profit excludes equipment"), Report.OperatingNet, 480);
	TestEqual(TEXT("Investment is visible"), Report.Investment, 700);
	USeasonSystem* Season = NewObject<USeasonSystem>();
	Season->SeasonDay = 8;
	Season->RecordDailyReport(Report);
	TestEqual(TEXT("First actual report creates one record"), Season->DailyFinancialHistory.Num(), 1);
	if (Season->DailyFinancialHistory.Num() == 1)
	{
		const FDailyFinancialRecord& Record = Season->DailyFinancialHistory[0];
		TestEqual(TEXT("History uses season day rather than report date"), Record.Day, 8);
		TestEqual(TEXT("History keeps actual revenue, not profit or cash"), Record.Revenue, 500);
		TestEqual(TEXT("History excludes equipment from operating expense"), Record.OperatingExpense, 20);
		TestEqual(TEXT("History keeps equipment investment separately"), Record.Investment, 700);
		TestEqual(TEXT("History keeps actual wallet balance"), Record.Cash, 780);
		TestEqual(TEXT("History without an economy has no unpaid bills"), Record.UnpaidBills, 0);
		TestEqual(TEXT("History reconstructs actual cash flow"),
			Record.Revenue - Record.OperatingExpense - Record.Investment, Report.Net);
	}
	Season->EvaluateDay(Report.OperatingNet, Report.Money, 600, 2, Report.Investment);
	TestEqual(TEXT("Healthy investment does not count as debt"), Season->ConsecutiveDebtDays, 0);
	Business->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FManualPrepClockTest, "Shoot.Gameplay.PreparationDoesNotSpendBusinessTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FManualPrepClockTest::RunTest(const FString&)
{
	UTimeSystem* Time = NewObject<UTimeSystem>();
	Time->bAutoOpenBusiness = false;
	Time->bPauseBeforeOpening = true;
	Time->DayStartHour = 8.f;
	Time->OpenHour = 8.f;
	Time->CloseHour = 21.f;
	Time->Init();
	Time->Tick(60.f);
	TestEqual(TEXT("Planning a menu does not advance time"), Time->DayProgress, 0.f);
	TestTrue(TEXT("Player can open the restaurant"), Time->OpenBusiness());
	Time->Tick(1.f);
	TestTrue(TEXT("Business advances after opening"), Time->DayProgress > 0.f);
	Time->SetTickableTickType(ETickableTickType::Never);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShortCampaignTest, "Shoot.Gameplay.ShortCampaignAndLegacySave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShortCampaignTest::RunTest(const FString&)
{
	UDataTable* Table = LoadObject<UDataTable>(nullptr, TEXT("/Game/_Demo/Data/DT_SeasonConfig.DT_SeasonConfig"));
	if (!TestNotNull(TEXT("Season settings asset exists"), Table)) return false;
	UBaseGameInstance* GI = NewObject<UBaseGameInstance>();
	USeasonSystem* Season = NewObject<USeasonSystem>(GI);
	Season->ConfigTable = Table;
	Season->ConfigRowName = TEXT("ShortCampaign");
	Season->Init(nullptr, nullptr);
	TestEqual(TEXT("New campaign lasts six days"), Season->Config.SeasonDays, 6);
	UMySaveGame* Legacy = NewObject<UMySaveGame>();
	GI->curSaveGameData = Legacy;
	Season->Load();
	TestEqual(TEXT("A legacy save retains its long season"), Season->Config.SeasonDays, 48);
	Season->ResetForNewGame();
	TestEqual(TEXT("New game after legacy load restores short profile"), Season->Config.SeasonDays, 6);
	for (int32 Day = 1; Day <= 6; ++Day)
	{
		for (int32 Order = 0; Order < 8; ++Order) Season->NotifyOrderServed(20.f, false, 45);
		const int32 Reputation = 100 + Day * 8 * 3;
		Season->EvaluateDay(340, Day * 340, Reputation, Season->GetStarsForReputation(Reputation));
	}
	TestTrue(TEXT("Eight ordinary orders per day can win without wild food"), Season->bSeasonWon);
	TestTrue(TEXT("The sixth day ends the campaign"), Season->bSeasonEnded);
	UMySaveGame* ShortSave = NewObject<UMySaveGame>();
	GI->curSaveGameData = ShortSave;
	Season->Save();
	TestEqual(TEXT("Save records the selected profile"), ShortSave->SeasonConfigProfile, FName(TEXT("ShortCampaign")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFirstDayGoalClockTest, "Shoot.Gameplay.FirstDayGoalEndsOnceAndResumesNormalDay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFirstDayGoalClockTest::RunTest(const FString&)
{
	UBaseGameInstance* GI = NewObject<UBaseGameInstance>();
	UEconomySystem* Economy = NewObject<UEconomySystem>();
	UBusinessSimulation* Business = NewObject<UBusinessSimulation>(GI);
	GI->businessSimulation = Business;
	Business->DailyRentIncome = 100;
	Business->DailyMaintenance = 0;
	Business->Init(Economy);
	UTimeSystem* Time = Business->TimeSystem;
	Time->bAutoOpenBusiness = false;
	Time->bPauseBeforeOpening = true;
	Time->DayStartHour = Time->OpenHour = 8.f;
	Time->CloseHour = 21.f;
	Time->SetGoalDrivenBusiness(true);
	TestTrue(TEXT("Trial opens normally"), Time->OpenBusiness());
	Time->Tick(3600.f);
	TestEqual(TEXT("No time limit on the trial day"), Time->DayProgress, 0.f);
	TestEqual(TEXT("Trial remains day one"), Time->Day, 1);
	TestTrue(TEXT("Order goal can end the morning session"), Time->FinishBusinessDayEarly());
	TestTrue(TEXT("Trial enters settlement"), Time->IsEndOfDayReached());
	TestFalse(TEXT("Trial closes admissions"), Time->IsBusinessOpen());
	const int32 SettledMoney = Economy->GetMoney();
	TestEqual(TEXT("Daily income applied once"), SettledMoney, 100);
	TestFalse(TEXT("Repeated completion does not settle again"), Time->FinishBusinessDayEarly());
	TestFalse(TEXT("Cannot reopen a settled trial"), Time->OpenBusiness());
	Time->Tick(3600.f);
	TestEqual(TEXT("Settlement waits for confirmation"), Time->Day, 1);
	Time->BeginNextBusinessDay();
	Time->BeginNextBusinessDay();
	TestEqual(TEXT("Confirmation advances exactly once"), Time->Day, 2);
	TestFalse(TEXT("Normal day clears the trial clock flag"), Time->IsGoalDrivenBusiness());
	TestEqual(TEXT("Next day does not charge daily costs again"), Economy->GetMoney(), SettledMoney);
	TestTrue(TEXT("Normal day can open"), Time->OpenBusiness());
	Time->Tick(1.f);
	TestTrue(TEXT("Normal day clock advances"), Time->DayProgress > 0.f);
	Time->CloseBusiness();
	TestFalse(TEXT("Ordinary manual closing keeps the usual settlement time"), Time->IsEndOfDayReached());
	Business->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDailyFinancialHistoryTest, "Shoot.Gameplay.DailyFinancialHistoryKeepsRealLastSevenDays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDailyFinancialHistoryTest::RunTest(const FString&)
{
	USeasonSystem* Season = NewObject<USeasonSystem>();
	TestEqual(TEXT("A season starts with genuinely empty history"), Season->DailyFinancialHistory.Num(), 0);
	for (int32 Day = 1; Day <= 10; ++Day)
	{
		Season->SeasonDay = Day;
		FBusinessReport Report;
		Report.Day = 29;
		Report.Income = Day * 100;
		Report.Investment = Day * 5;
		Report.Expense = Day * 10 + Report.Investment;
		Report.Money = 5000 - Day * 17;
		Season->RecordDailyReport(Report);
		TestEqual(TEXT("Only reported days exist, capped at seven"), Season->DailyFinancialHistory.Num(), FMath::Min(Day, 7));
	}
	if (Season->DailyFinancialHistory.Num() == 7)
	{
		for (int32 Index = 0; Index < 7; ++Index)
		{
			const FDailyFinancialRecord& Record = Season->DailyFinancialHistory[Index];
			const int32 Day = Index + 4;
			TestEqual(TEXT("Oldest records are trimmed chronologically"), Record.Day, Day);
			TestEqual(TEXT("Historical revenue remains the reported value"), Record.Revenue, Day * 100);
			TestEqual(TEXT("Historical operating expense remains the reported value"), Record.OperatingExpense, Day * 10);
			TestEqual(TEXT("Historical investment remains the reported value"), Record.Investment, Day * 5);
			TestEqual(TEXT("Historical cash is not inferred from profit"), Record.Cash, 5000 - Day * 17);
		}
	}

	FBusinessReport Replacement;
	Replacement.Income = 123;
	Replacement.Expense = 70;
	Replacement.Investment = 40;
	Replacement.Money = 321;
	Season->RecordDailyReport(Replacement);
	TestEqual(TEXT("Same-day report replaces rather than duplicates"), Season->DailyFinancialHistory.Num(), 7);
	if (!Season->DailyFinancialHistory.IsEmpty())
	{
		TestEqual(TEXT("Same-day replacement keeps the season ordinal"), Season->DailyFinancialHistory.Last().Day, 10);
		TestEqual(TEXT("Same-day replacement uses new actual revenue"), Season->DailyFinancialHistory.Last().Revenue, 123);
	}
	Season->SeasonDay = 7;
	Season->RecordDailyReport(Replacement);
	TestEqual(TEXT("Replaying a day removes that day and all later reports"), Season->DailyFinancialHistory.Num(), 4);
	if (Season->DailyFinancialHistory.Num() == 4)
	{
		TestEqual(TEXT("Replay retains earlier reports"), Season->DailyFinancialHistory[0].Day, 4);
		TestEqual(TEXT("Replay appends only its actual report"), Season->DailyFinancialHistory.Last().Day, 7);
	}
	Season->bSeasonEnded = true;
	Season->SeasonDay = 8;
	Replacement.Income = 999;
	Season->RecordDailyReport(Replacement);
	TestEqual(TEXT("An ended season does not append history"), Season->DailyFinancialHistory.Num(), 4);
	if (!Season->DailyFinancialHistory.IsEmpty())
	{
		TestEqual(TEXT("An ended season does not overwrite history"), Season->DailyFinancialHistory.Last().Revenue, 123);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDailyFinancialHistorySaveTest, "Shoot.Gameplay.DailyFinancialHistoryMemorySaveAndLegacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDailyFinancialHistorySaveTest::RunTest(const FString&)
{
	UBaseGameInstance* GI = NewObject<UBaseGameInstance>();
	USeasonSystem* Season = NewObject<USeasonSystem>(GI);
	for (const int32 Count : {0, 1, 7, 10})
	{
		GI->curSaveGameData = NewObject<UMySaveGame>();
		Season->ResetForNewGame();
		for (int32 Day = 1; Day <= Count; ++Day)
		{
			Season->SeasonDay = Day;
			FBusinessReport Report;
			Report.Income = Day * 101;
			Report.Expense = Day * 13;
			Report.Investment = Day * 3;
			Report.Money = 1000 + Day * 7;
			Season->RecordDailyReport(Report);
			Season->DailyFinancialHistory.Last().UnpaidBills = Day * 2;
		}
		Season->SeasonDay = Count + 1;
		const TArray<FDailyFinancialRecord> Expected = Season->DailyFinancialHistory;
		Season->Save();
		TestEqual(TEXT("Season Save copies actual history"), GI->curSaveGameData->SeasonDailyFinancialHistory.Num(), FMath::Min(Count, 7));
		TArray<uint8> Bytes;
		if (!TestTrue(TEXT("Financial history serializes without a disk slot"), UGameplayStatics::SaveGameToMemory(GI->curSaveGameData, Bytes))) return false;
		UMySaveGame* Loaded = Cast<UMySaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
		if (!TestNotNull(TEXT("Financial history deserializes from memory"), Loaded)) return false;
		GI->curSaveGameData = Loaded;
		Season->DailyFinancialHistory.Reset();
		Season->Load();
		TestEqual(TEXT("History count survives the memory round trip"), Season->DailyFinancialHistory.Num(), Expected.Num());
		TestEqual(TEXT("Season day survives the memory round trip"), Season->SeasonDay, Count + 1);
		for (int32 Index = 0; Index < FMath::Min(Expected.Num(), Season->DailyFinancialHistory.Num()); ++Index)
		{
			const FDailyFinancialRecord& Actual = Season->DailyFinancialHistory[Index];
			const FDailyFinancialRecord& Original = Expected[Index];
			TestEqual(TEXT("Saved day"), Actual.Day, Original.Day);
			TestEqual(TEXT("Saved revenue"), Actual.Revenue, Original.Revenue);
			TestEqual(TEXT("Saved operating expense"), Actual.OperatingExpense, Original.OperatingExpense);
			TestEqual(TEXT("Saved investment"), Actual.Investment, Original.Investment);
			TestEqual(TEXT("Saved cash"), Actual.Cash, Original.Cash);
			TestEqual(TEXT("Saved unpaid bills"), Actual.UnpaidBills, Original.UnpaidBills);
		}
	}

	UMySaveGame* Oversized = NewObject<UMySaveGame>();
	for (int32 Day = 1; Day <= 10; ++Day)
	{
		FDailyFinancialRecord Record;
		Record.Day = Day;
		Record.Revenue = Day * 100;
		Oversized->SeasonDailyFinancialHistory.Add(Record);
	}
	GI->curSaveGameData = Oversized;
	Season->Load();
	TestEqual(TEXT("Loading oversized history caps it to seven"), Season->DailyFinancialHistory.Num(), 7);
	if (Season->DailyFinancialHistory.Num() == 7)
	{
		TestEqual(TEXT("Load trims oldest rather than newest history"), Season->DailyFinancialHistory[0].Day, 4);
		TestEqual(TEXT("Load retains the last real day"), Season->DailyFinancialHistory.Last().Day, 10);
	}
	UMySaveGame* Legacy = NewObject<UMySaveGame>();
	Legacy->SeasonDay = 20;
	Legacy->SeasonMoneyEarned = 9999;
	Legacy->Money = 4321;
	GI->curSaveGameData = Legacy;
	Season->Load();
	TestEqual(TEXT("Legacy data does not fabricate revenue from accumulated money"), Season->DailyFinancialHistory.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDailyFinancialHistoryRetryTest, "Shoot.Gameplay.DailyFinancialHistoryCheckpointRetryAndNewGame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDailyFinancialHistoryRetryTest::RunTest(const FString&)
{
	UBaseGameInstance* GI = NewObject<UBaseGameInstance>();
	GI->curSaveGameData = NewObject<UMySaveGame>();
	USeasonSystem* Season = NewObject<USeasonSystem>(GI);
	for (int32 Day = 14; Day <= 20; ++Day)
	{
		Season->SeasonDay = Day;
		FBusinessReport Report;
		Report.Income = Day * 100;
		Season->RecordDailyReport(Report);
	}
	Season->CheckpointDay = 17;
	Season->bCheckpointValid = true;
	Season->RetryAct();
	TestEqual(TEXT("Retry without failure does not truncate history"), Season->DailyFinancialHistory.Num(), 7);
	Season->bSeasonFailed = true;
	Season->bSeasonEnded = true;
	Season->RetryAct();
	TestEqual(TEXT("Retry returns to the checkpoint day"), Season->SeasonDay, 17);
	TestEqual(TEXT("Retry removes the checkpoint day and all later reports"), Season->DailyFinancialHistory.Num(), 3);
	TestEqual(TEXT("Retry writes the truncated history to the in-memory save object"), GI->curSaveGameData->SeasonDailyFinancialHistory.Num(), 3);
	for (const FDailyFinancialRecord& Record : Season->DailyFinancialHistory)
	{
		TestTrue(TEXT("Only days before the checkpoint remain"), Record.Day < 17);
		TestEqual(TEXT("Retry preserves earlier actual revenue"), Record.Revenue, Record.Day * 100);
	}
	TestFalse(TEXT("Retry permits real reports again"), Season->bSeasonEnded);
	FBusinessReport Replay;
	Replay.Income = 77;
	Season->RecordDailyReport(Replay);
	TestEqual(TEXT("Checkpoint replay adds one actual day"), Season->DailyFinancialHistory.Num(), 4);
	Season->ResetForNewGame();
	TestEqual(TEXT("New game clears all financial history"), Season->DailyFinancialHistory.Num(), 0);
	TestEqual(TEXT("New game restores day one"), Season->SeasonDay, 1);
	Season->Save();
	TestEqual(TEXT("Saving a new game clears the stored history"), GI->curSaveGameData->SeasonDailyFinancialHistory.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDailyFinancialSettlementTest, "Shoot.Gameplay.DailyFinancialHistoryCloseAndFallbackSettlement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDailyFinancialSettlementTest::RunTest(const FString&)
{
	UEconomySystem* Economy = NewObject<UEconomySystem>();
	UBusinessSimulation* Business = NewObject<UBusinessSimulation>();
	Business->Init(Economy);
	Business->TimeSystem->SetTickableTickType(ETickableTickType::Never);
	Business->TimeSystem->bAutoOpenBusiness = false;
	Business->TimeSystem->Year = 3;
	Business->TimeSystem->Month = 2;
	Business->TimeSystem->Day = 29;
	USeasonSystem* Season = NewObject<USeasonSystem>();
	Season->SeasonDay = 8;
	Season->Init(Economy, Business);
	Business->SeasonSystem = Season;
	Economy->AddReputation(600);
	Economy->AddMoney(1000, EMoneySource::Debug);
	Economy->AddMoney(500, EMoneySource::OrderComplete);
	TestTrue(TEXT("Equipment purchase succeeds"), Economy->SpendMoney(700, EMoneySource::ShopPurchase));
	TestTrue(TEXT("Operating purchase succeeds"), Economy->SpendMoney(20, EMoneySource::IngredientCost));
	Economy->AddUnpaidBill(75);
	Season->NotifyOrderServed(12.f, false, 500);
	Business->TimeSystem->OnBusinessDayEnded.Broadcast();
	TestEqual(TEXT("Closing records exactly one real day"), Season->DailyFinancialHistory.Num(), 1);
	if (Season->DailyFinancialHistory.Num() == 1)
	{
		const FDailyFinancialRecord& Record = Season->DailyFinancialHistory[0];
		TestEqual(TEXT("Close history uses the season ordinal"), Record.Day, 8);
		TestEqual(TEXT("Close revenue includes actual passive income"), Record.Revenue, 550);
		TestEqual(TEXT("Close expense includes paid operating costs only"), Record.OperatingExpense, 40);
		TestEqual(TEXT("Close keeps actual equipment investment"), Record.Investment, 700);
		TestEqual(TEXT("Close records the actual ending wallet"), Record.Cash, 810);
		TestEqual(TEXT("Close captures unpaid bills before evaluation consumes them"), Record.UnpaidBills, 75);
	}
	TestEqual(TEXT("Existing evaluation still consumes unpaid bills"), Economy->GetUnpaidBills(), 0);
	TestEqual(TEXT("Existing unpaid-bill debt semantics remain unchanged"), Season->ConsecutiveDebtDays, 1);
	TestEqual(TEXT("Existing service snapshot remains unchanged"), Season->LastServed, 1);
	TestEqual(TEXT("Existing daily operating profit remains unchanged"), Season->LastDailyNet, 510);
	TestEqual(TEXT("Existing daily investment remains unchanged"), Season->LastDailyInvestment, 700);
	TestEqual(TEXT("Service statistics still reset after evaluation"), Season->TodayServed, 0);
	TestEqual(TEXT("Daily financial counters reset after evaluation"), Business->TodayIncome, 0);
	Business->TimeSystem->OnBusinessDayEnded.Broadcast();
	TestEqual(TEXT("Repeated close cannot duplicate history"), Season->DailyFinancialHistory.Num(), 1);
	TestEqual(TEXT("Repeated close cannot charge passive cash again"), Economy->GetMoney(), 810);
	Business->SkipNextDaySettlement();
	Business->TimeSystem->OnDayChanged.Broadcast(3, 2, 30);
	TestEqual(TEXT("Confirmation skips duplicate financial history"), Season->DailyFinancialHistory.Num(), 1);
	TestEqual(TEXT("Confirmation skips duplicate passive cash"), Economy->GetMoney(), 810);
	Business->TimeSystem->OnDayChanged.Broadcast(3, 3, 1);
	TestEqual(TEXT("Fallback OnDay also records real history"), Season->DailyFinancialHistory.Num(), 2);
	if (Season->DailyFinancialHistory.Num() == 2)
	{
		const FDailyFinancialRecord& Record = Season->DailyFinancialHistory[1];
		TestEqual(TEXT("Fallback uses season day rather than month day one"), Record.Day, 9);
		TestEqual(TEXT("Fallback revenue is only that day's actual income"), Record.Revenue, 50);
		TestEqual(TEXT("Fallback operating expense is only that day's paid maintenance"), Record.OperatingExpense, 20);
		TestEqual(TEXT("Fallback does not replay prior investment"), Record.Investment, 0);
		TestEqual(TEXT("Fallback ending cash is real"), Record.Cash, 840);
		TestEqual(TEXT("Consumed bills do not leak into next day's history"), Record.UnpaidBills, 0);
	}
	Business->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDailyFinancialCountersSaveTest, "Shoot.Gameplay.DailyFinancialCountersSurviveMidDayMemoryLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDailyFinancialCountersSaveTest::RunTest(const FString&)
{
	UBaseGameInstance* GI = NewObject<UBaseGameInstance>();
	UEconomySystem* Economy = NewObject<UEconomySystem>();
	UBusinessSimulation* Business = NewObject<UBusinessSimulation>(GI);
	Business->Init(Economy);
	Business->TimeSystem->SetTickableTickType(ETickableTickType::Never);
	Business->TimeSystem->bAutoOpenBusiness = false;
	GI->curSaveGameData = NewObject<UMySaveGame>();
	Economy->AddMoney(1000, EMoneySource::Debug);
	Economy->AddMoney(500, EMoneySource::OrderComplete);
	TestTrue(TEXT("Mid-day investment succeeds"), Economy->SpendMoney(700, EMoneySource::ShopPurchase));
	TestTrue(TEXT("Mid-day operating cost succeeds"), Economy->SpendMoney(20, EMoneySource::Maintenance));
	TestEqual(TEXT("Revenue is mirrored into the current save during the money delegate"), GI->curSaveGameData->BizTodayIncome, 500);
	TestEqual(TEXT("All paid expenses are mirrored into the current save"), GI->curSaveGameData->BizTodayExpense, 720);
	TestEqual(TEXT("Investment is mirrored separately into the current save"), GI->curSaveGameData->BizTodayInvestment, 700);
	Economy->AddMoney(5, EMoneySource::Debug);
	Economy->AddMoney(-5, EMoneySource::SeasonReset);
	TestEqual(TEXT("Debug and retry cash are still excluded from revenue"), GI->curSaveGameData->BizTodayIncome, 500);
	TestEqual(TEXT("Debug and retry cash are still excluded from expenses"), GI->curSaveGameData->BizTodayExpense, 720);

	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("Mid-day counters serialize without writing a slot"), UGameplayStatics::SaveGameToMemory(GI->curSaveGameData, Bytes)))
	{
		GI->curSaveGameData = nullptr;
		Business->Shutdown();
		return false;
	}
	UMySaveGame* Loaded = Cast<UMySaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("Mid-day counters deserialize from memory"), Loaded))
	{
		GI->curSaveGameData = nullptr;
		Business->Shutdown();
		return false;
	}
	GI->curSaveGameData = Loaded;
	Business->TodayIncome = Business->TodayExpense = Business->TodayInvestment = 0;
	Business->Load();
	const FBusinessReport Restored = Business->GetTodayReport();
	TestEqual(TEXT("Mid-day load restores actual revenue"), Restored.Income, 500);
	TestEqual(TEXT("Mid-day load restores paid expenses"), Restored.Expense, 720);
	TestEqual(TEXT("Mid-day load restores investment"), Restored.Investment, 700);
	TestEqual(TEXT("Mid-day load keeps correct cash flow"), Restored.Net, -220);
	TestEqual(TEXT("Mid-day load keeps correct operating profit"), Restored.OperatingNet, 480);
	Economy->AddMoney(25, EMoneySource::OrderComplete);
	TestTrue(TEXT("Post-load operating cost succeeds"), Economy->SpendMoney(5, EMoneySource::Maintenance));
	TestEqual(TEXT("Revenue continues from restored counters"), Loaded->BizTodayIncome, 525);
	TestEqual(TEXT("Expenses continue from restored counters"), Loaded->BizTodayExpense, 725);
	TestEqual(TEXT("Investment is not lost or duplicated after load"), Loaded->BizTodayInvestment, 700);
	GI->curSaveGameData = NewObject<UMySaveGame>();
	Business->Load();
	TestEqual(TEXT("Legacy save restores zero in-flight revenue"), Business->TodayIncome, 0);
	TestEqual(TEXT("Legacy save restores zero in-flight expenses"), Business->TodayExpense, 0);
	TestEqual(TEXT("Legacy save restores zero in-flight investment"), Business->TodayInvestment, 0);
	GI->curSaveGameData = nullptr;
	Business->Shutdown();
	return true;
}

#if WITH_EDITOR
#include "UI/SeasonDailyCardWidget.h"
#include "UI/TutorialGuideWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "RenderingThread.h"
#include "Slate/SceneViewport.h"
#include "Slate/WidgetRenderer.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SViewport.h"

namespace
{
	struct FIsolatedUiWorld
	{
		UWorld* World = nullptr;
		UBaseGameInstance* Instance = nullptr;
		UGameViewportClient* Client = nullptr;
		TArray<ULocalPlayer*> Players;
		TSharedPtr<SViewport> ViewportWidget;
		TSharedPtr<FSceneViewport> Viewport;
		TArray<TSharedPtr<SWidget>> PreviousFocus;

		FIsolatedUiWorld()
		{
			for (int32 User = 0; User < 2; ++User)
			{
				PreviousFocus.Add(FSlateApplication::Get().GetUserFocusedWidget(User));
			}
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("FinancialUiAutomation"), nullptr,
				true, ERHIFeatureLevel::Num, &Values);
			Instance = NewObject<UBaseGameInstance>(GEngine);
			Instance->AddToRoot();
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = Instance;
			World->SetGameInstance(Instance);
			Context.SetCurrentWorld(World);
			Client = NewObject<UGameViewportClient>(GEngine);
			Client->AddToRoot();
			Context.GameViewport = Client;
			Client->Init(Context, Instance, false);
			ViewportWidget = SNew(SViewport);
			Viewport = Client->CreateViewport(ViewportWidget);
			Viewport->SetInitialSize(FIntPoint(1920, 1080));
			World->InitializeActorsForPlay(FURL());
		}

		APlayerController* AddPlayer()
		{
			ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
			Instance->AddLocalPlayer(Player, FPlatformUserId::CreateFromInternalId(Players.Num()));
			Players.Add(Player);
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Controller->SetPlayer(Player);
			return Controller;
		}

		~FIsolatedUiWorld()
		{
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				if (!It->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) && It->GetWorld() == World)
				{
					It->RemoveFromParent();
					It->ReleaseSlateResources(true);
				}
			}
			for (ULocalPlayer* Player : Players)
			{
				Instance->RemoveLocalPlayer(Player);
			}
			Viewport->SetViewportClient(nullptr);
			Viewport.Reset();
			ViewportWidget.Reset();
			Client->DetachViewportClient();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			Instance->RemoveFromRoot();
			for (int32 User = 0; User < PreviousFocus.Num(); ++User)
			{
				if (PreviousFocus[User].IsValid())
				{
					FSlateApplication::Get().SetUserFocus(User, PreviousFocus[User], EFocusCause::SetDirectly);
				}
				else
				{
					FSlateApplication::Get().ClearUserFocus(User, EFocusCause::SetDirectly);
				}
			}
		}
	};

	template<typename T>
	T* FindTestWidget(UWorld* World, APlayerController* Player)
	{
		for (TObjectIterator<T> It; It; ++It)
		{
			if (!It->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
				&& It->GetWorld() == World && It->GetOwningPlayer() == Player && It->IsInViewport())
			{
				return *It;
			}
		}
		return nullptr;
	}

	bool SaveTestWidget(UUserWidget* Widget, FVector2D Size, const TCHAR* Name)
	{
		FWidgetRenderer Renderer(false);
		UTextureRenderTarget2D* Target = FWidgetRenderer::CreateTargetFor(Size, TF_Bilinear, false);
		if (!Target) return false;
		Target->AddToRoot();
		for (int32 Frame = 0; Frame < 12; ++Frame)
		{
			Renderer.DrawWidget(Target, Widget->TakeWidget(), Size, 0.04f);
			if (USeasonDailyCardWidget* Card = Cast<USeasonDailyCardWidget>(Widget))
			{
				Card->NativeTick(Card->GetCachedGeometry(), 0.04f);
			}
			else if (UTutorialGuideWidget* Guide = Cast<UTutorialGuideWidget>(Widget))
			{
				Guide->NativeTick(Guide->GetCachedGeometry(), 0.04f);
			}
			FlushRenderingCommands();
		}
		FImage Image;
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("Screenshots/WindowsEditor");
		IFileManager::Get().MakeDirectory(*Directory, true);
		const bool bSaved = FImageUtils::GetRenderTargetImage(Target, Image)
			&& FImageUtils::SaveImageByExtension(*(Directory / Name), Image);
		Target->RemoveFromRoot();
		return bSaved;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFinancialUiRenderTest, "Shoot.UI.FinancialReportAndChecklist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFinancialUiRenderTest::RunTest(const FString&)
{
	if (!GEngine || GEngine->GameViewport || !FSlateApplication::IsInitialized() || !FApp::CanEverRender())
	{
		AddError(TEXT("UI isolation test requires a renderer and no active game viewport."));
		return false;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
		{
			AddError(TEXT("UI isolation test requires no running gameplay world."));
			return false;
		}
	}
	FIsolatedUiWorld Fixture;
	APlayerController* PC = Fixture.AddPlayer();
	if (!TestNotNull(TEXT("Isolated local controller"), PC)) return false;
	TestTrue(TEXT("Controller is associated with a real local player"), PC->IsLocalPlayerController());
	TestNull(TEXT("UI fixture never has a user save object"), Fixture.Instance->curSaveGameData);
	USeasonSystem* Season = NewObject<USeasonSystem>(Fixture.Instance);
	Fixture.Instance->seasonSystem = Season;
	Season->LastGrade = ESeasonGrade::Great;
	Season->LastGradeScore = 82;
	Season->LastServed = 8;
	Season->LastFailed = 1;
	Season->LastAvgWait = 12.5f;
	Season->LastHint = TEXT("今天服务很顺利，明天可以尝试不同菜单。");
	Season->LastDailyNet = 480;
	Season->LastMoney = 1000;
	for (const int32 Count : {0, 1, 7})
	{
		Season->DailyFinancialHistory.Reset();
		for (int32 Day = 1; Day <= Count; ++Day)
		{
			Season->SeasonDay = Day;
			FBusinessReport Report;
			Report.Income = 240 + Day * 110;
			Report.Investment = Day == 7 ? 300 : 0;
			Report.Expense = 60 + Day * 15 + Report.Investment;
			Report.Money = 900 + Day * 210;
			Season->RecordDailyReport(Report);
		}
		USeasonDailyCardWidget::ShowForPlayer(PC, Season, true);
		USeasonDailyCardWidget* Card = FindTestWidget<USeasonDailyCardWidget>(Fixture.World, PC);
		if (!TestNotNull(TEXT("Public entry creates a real player-screen financial card"), Card)) return false;
		TestTrue(TEXT("Financial card waits for confirmation"), Card->IsAwaitingNextDay());
		const FString Filename = FString::Printf(TEXT("DailyFinancialReport_UIValidation_%dDays.png"), Count);
		TestTrue(TEXT("Actual UMG renders into a screenshot"), SaveTestWidget(Card, FVector2D(1920, 1080), *Filename));
		UTextBlock* Revenue = Cast<UTextBlock>(Card->WidgetTree->FindWidget(TEXT("MetricValue0")));
		if (!TestNotNull(TEXT("Revenue text is an editable UMG control"), Revenue)) return false;
		TestEqual(TEXT("UI uses actual fixture revenue and leaves missing history unknown"), Revenue->GetText().ToString(),
			Count == 0 ? FString(TEXT("暂无记录")) : FText::AsNumber(240 + Count * 110).ToString());
		UBorder* Border = Cast<UBorder>(Card->WidgetTree->FindWidget(TEXT("Card")));
		const FGeometry Geometry = Border->GetCachedGeometry();
		const FVector2D Start = Geometry.LocalToAbsolute(FVector2D::ZeroVector);
		const FVector2D End = Geometry.LocalToAbsolute(Geometry.GetLocalSize());
		TestTrue(TEXT("Large card stays inside the render surface"), Start.X >= -1.0 && Start.Y >= -1.0
			&& End.X <= 1921.0 && End.Y <= 1081.0);
	}
	USeasonDailyCardWidget* Card = FindTestWidget<USeasonDailyCardWidget>(Fixture.World, PC);
	TestTrue(TEXT("Financial card fits a smaller viewport"), SaveTestWidget(Card, FVector2D(1366, 768), TEXT("DailyFinancialReport_UIValidation_1366x768.png")));
	Season->SeasonDay = 8;
	FBusinessReport Loss;
	Loss.Expense = 75;
	Loss.Money = 2295;
	Season->RecordDailyReport(Loss);
	Season->LastHint = TEXT("今天的营业支出高于营收。可以调整备料和菜单，减少浪费；设备投资单独列出，不会混入营业亏损。明天再试试更适合客人的菜品。");
	USeasonDailyCardWidget::ShowForPlayer(PC, Season, true);
	TestTrue(TEXT("Loss and long Chinese hints render"), SaveTestWidget(Card, FVector2D(1366, 768), TEXT("DailyFinancialReport_UIValidation_Loss.png")));
	UTextBlock* Profit = Cast<UTextBlock>(Card->WidgetTree->FindWidget(TEXT("MetricValue2")));
	TestEqual(TEXT("Loss is shown rather than clamped to zero"), Profit->GetText().ToString(), FText::AsNumber(-75).ToString());

	Fixture.Instance->businessSimulation = NewObject<UBusinessSimulation>(Fixture.Instance);
	UTimeSystem* Clock = NewObject<UTimeSystem>(Fixture.Instance);
	Clock->SetTickableTickType(ETickableTickType::Never);
	Fixture.Instance->businessSimulation->TimeSystem = Clock;
	Clock->DayStartHour = 8.f;
	Clock->OpenHour = 8.f;
	Clock->bAutoOpenBusiness = false;
	Clock->Init();
	TestTrue(TEXT("Fixture opens without a real economy or disk save"), Clock->OpenBusiness());
	TestTrue(TEXT("Fixture reaches the ordinary end-of-day gate"), Clock->FinishBusinessDayEarly());
	UButton* NextDay = Cast<UButton>(Card->WidgetTree->FindWidget(TEXT("NextDayButton")));
	if (!TestNotNull(TEXT("Next day is a real bound UMG button"), NextDay)) return false;
	NextDay->OnClicked.Broadcast();
	TestEqual(TEXT("Actual button callback advances exactly one day"), Clock->Day, 2);
	TestFalse(TEXT("Actual button dismisses the card"), Card->IsAwaitingNextDay());
	NextDay->OnClicked.Broadcast();
	TestEqual(TEXT("A repeated callback cannot advance twice"), Clock->Day, 2);

	Clock->Day = 1;
	TestTrue(TEXT("Checklist fixture is in the open-business phase"), Clock->OpenBusiness());
	ATutorialTaskManager* Manager = Fixture.World->SpawnActor<ATutorialTaskManager>();
	UDataTable* Tasks = NewObject<UDataTable>(Manager);
	Tasks->RowStruct = FTutorialTaskRow::StaticStruct();
	const TCHAR* Descriptions[] = {TEXT("拿起篮子"), TEXT("捡起鸡蛋"), TEXT("制作煎鸡蛋")};
	const ETutorialTaskType Types[] = {ETutorialTaskType::PickupItem, ETutorialTaskType::AddToCooker, ETutorialTaskType::CookDish};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FTutorialTaskRow Task;
		Task.StepOrder = Index;
		Task.TaskType = Types[Index];
		Task.Description = FText::FromString(Descriptions[Index]);
		Tasks->AddRow(*FString::Printf(TEXT("Fixture%d"), Index), Task);
	}
	Manager->TaskTable = Tasks;
	Manager->DayExtraNotes.Add(1, FText::FromString(TEXT("跟随箭头前往目标位置")));
	Manager->Tick(0.5f);
	ATutorialTaskManager::NotifyAction(Fixture.World, Types[0], -1);
	ATutorialTaskManager::NotifyAction(Fixture.World, Types[1], -1);
	Manager->Tick(0.5f);
	UTutorialGuideWidget::EnsureForPlayer(PC, Manager);
	UTutorialGuideWidget* Guide = FindTestWidget<UTutorialGuideWidget>(Fixture.World, PC);
	if (!TestNotNull(TEXT("Actual local-player tutorial HUD exists"), Guide)) return false;
	TestTrue(TEXT("Task checklist renders actual controls"), SaveTestWidget(Guide, FVector2D(1366, 768), TEXT("TutorialChecklist_UIValidation.png")));
	const TArray<FTutorialChecklistEntry> Entries = Manager->GetChecklistEntries();
	TestEqual(TEXT("Completed tasks, first incomplete and an unmarked note remain visible"), Entries.Num(), 4);
	int32 CompleteImages = 0;
	Guide->WidgetTree->ForEachWidget([&CompleteImages](UWidget* Widget)
	{
		if (const UImage* Image = Cast<UImage>(Widget))
		{
			if (const UObject* Resource = Image->GetBrush().GetResourceObject(); Resource
				&& Resource->GetName() == TEXT("T_TutorialComplete_Pastoral_v1")) ++CompleteImages;
		}
	});
	TestEqual(TEXT("Completed rows use two genuine texture images"), CompleteImages, 2);

	APlayerController* SecondPC = Fixture.AddPlayer();
	USeasonDailyCardWidget::ShowForPlayer(PC, Season, true);
	USeasonDailyCardWidget::ShowForPlayer(SecondPC, Season, true);
	USeasonDailyCardWidget* SecondCard = FindTestWidget<USeasonDailyCardWidget>(Fixture.World, SecondPC);
	if (!TestNotNull(TEXT("Split-screen has a distinct second-player card"), SecondCard)) return false;
	TestTrue(TEXT("Second player's local dimensions fit the same actual UI"), SaveTestWidget(SecondCard,
		FVector2D(1920, 1080) * FVector2D(SecondPC->GetLocalPlayer()->Size), TEXT("DailyFinancialReport_UIValidation_SplitPlayer.png")));
	TestNull(TEXT("No screenshot or UI operation created a user save"), Fixture.Instance->curSaveGameData);
	return true;
}

static FAutoConsoleCommand ValidateFinancialUiCommand(
	TEXT("Shoot.ValidateFinancialUI"), TEXT("Render the isolated financial/checklist automation test without starting gameplay."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
		if (Framework.GetCurrentTest())
		{
			UE_LOG(LogTemp, Warning, TEXT("[FinancialUIValidation] Another automation test is running."));
			return;
		}
		Framework.StartTestByName(TEXT("FFinancialUiRenderTest"), 0, TEXT("Shoot.UI.FinancialReportAndChecklist"));
		FAutomationTestExecutionInfo Info;
		const bool bSuccess = Framework.StopTest(Info);
		for (const FAutomationExecutionEntry& Entry : Info.GetEntries())
		{
			UE_LOG(LogTemp, Display, TEXT("[FinancialUIValidation] %s"), *Entry.ToString());
		}
		UE_LOG(LogTemp, Display, TEXT("[FinancialUIValidation] Success=%d Errors=%d"), bSuccess, Info.GetErrorTotal());
	}));
#endif

#endif
