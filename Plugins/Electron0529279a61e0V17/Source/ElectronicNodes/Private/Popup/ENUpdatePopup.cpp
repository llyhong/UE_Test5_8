#include "ENUpdatePopup.h"
#include "EditorStyleSet.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "SWebBrowser.h"
#include "Interfaces/IPluginManager.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Misc/FileHelper.h"

void ENUpdatePopup::OnBrowserLinkClicked(const FSlateHyperlinkRun::FMetadata& Metadata)
{
	const FString* URL = Metadata.Find(TEXT("href"));

	if (URL)
	{
		FPlatformProcess::LaunchURL(**URL, nullptr, nullptr);
	}
}

void ENUpdatePopup::Register()
{
    FString UpdateConfigPath = IPluginManager::Get().FindPlugin(TEXT("ElectronicNodes"))->GetBaseDir();
    UpdateConfigPath /= "UpdateConfig.ini";
    const FString UpdateConfigFile = FPaths::ConvertRelativePathToFull(FConfigCacheIni::NormalizeConfigIniPath(UpdateConfigPath));
    const FString CurrentPluginVersion = "3.21";

    FString LoadedConfig;
    if (FPaths::FileExists(UpdateConfigFile))
    {
        FFileHelper::LoadFileToString(LoadedConfig, *UpdateConfigFile);
    }
    else
    {
        LoadedConfig = TEXT("PluginVersionUpdate=");
        FFileHelper::SaveStringToFile(LoadedConfig, *UpdateConfigFile);
    }

    FString PluginVersionUpdate;
    FParse::Value(*LoadedConfig, TEXT("PluginVersionUpdate="), PluginVersionUpdate);

    if (PluginVersionUpdate != CurrentPluginVersion)
    {
        PluginVersionUpdate = CurrentPluginVersion;
        LoadedConfig = FString::Printf(TEXT("PluginVersionUpdate=%s"), *PluginVersionUpdate);
        FFileHelper::SaveStringToFile(LoadedConfig, *UpdateConfigFile);
    	
        FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
        {
            Open();
        });
    }
}

void ENUpdatePopup::Open()
{
	if (!FSlateApplication::Get().CanDisplayWindows())
	{
		return;
	}

	TSharedRef<SBorder> WindowContent = SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8.0f, 8.0f));

	TSharedPtr<SWindow> Window = SNew(SWindow)
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::FixedSize)
		.ClientSize(FVector2D(600, 400))
		.Title(FText::FromString("Electronic Nodes"))
		.IsTopmostWindow(true)
		[
			WindowContent
		];

	const FSlateFontInfo HeadingFont = FCoreStyle::GetDefaultFontStyle("Regular", 24);
	const FSlateFontInfo ContentFont = FCoreStyle::GetDefaultFontStyle("Regular", 12);

	TSharedRef<SVerticalBox> InnerContent = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10)
		[
			SNew(STextBlock)
			.Font(HeadingFont)
			.Text(FText::FromString("Electronic Nodes v3.21"))
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0)
		.Padding(10)
		[
			SNew(SBorder)
			.Padding(10)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(SRichTextBlock)
					.Text(FText::FromString(R"(
<LargeText>Hello and thank you for using Electronic Nodes!</>

First thing first, if you've been enjoying using it, it would mean a lot if you could just drop <a id="browser" href="https://www.fab.com/listings/d6148766-27b1-47db-a730-832c53b7a895">a small review on the marketplace page</> :). I also wanted to mention that I made another plugin to update the engine theme called <a id="browser" href="https://www.fab.com/listings/7fcc3b88-bddc-4ac7-8132-1be496bb3caf">Darker Nodes</>.

But let's keep it short, here are the cool new features (and bugfixes) of version 3.21!


<LargeText>Version 3.21</>

<RichTextBlock.Bold>Features</>

* Add a custom hook for other plugins (<a id="browser" href="https://github.com/hugoattal/ElectronicNodes/issues/116">issue #116</> - Thanks TheGeebus for the help!)

<RichTextBlock.Bold>Bugfixes</>

* Fix unstyled wire on Master Activate Off (<a id="browser" href="https://github.com/hugoattal/ElectronicNodes/issues/117">issue #117</>)


<LargeText>Version 3.20</>

<RichTextBlock.Bold>Features</>

* Add hot patch support for Linux (<a id="browser" href="https://github.com/hugoattal/ElectronicNodes/issues/112">issue #112</>)

<RichTextBlock.Bold>Bugfixes</>

* Fix wire crossing on spline (<a id="browser" href="https://github.com/hugoattal/ElectronicNodes/issues/111">issue #111</>)
* Fix PCG wire color (<a id="browser" href="https://github.com/hugoattal/ElectronicNodes/issues/113">issue #113</>)


<LargeText>Version 3.19</>

<RichTextBlock.Bold>Features</>

* Add wire crossing setting (<a id="browser" href="https://github.com/hugoattal/ElectronicNodes/issues/110">issue #110</>)


<LargeText>Version 3.18</>

<RichTextBlock.Bold>Bugfixes</>

* Fix min distance wire drawing (<a id="browser" href="https://github.com/hugoattal/ElectronicNodes/issues/109">issue #109</>)


<a id="browser" href="https://github.com/hugoattal/ElectronicNodes#changelog">See complete changelog</>
)"))
					.TextStyle(FAppStyle::Get(), "NormalText")
					.DecoratorStyleSet(&FAppStyle::Get())
					.AutoWrapText(true)
					+ SRichTextBlock::HyperlinkDecorator(
						TEXT("browser"), FSlateHyperlinkRun::FOnClick::CreateStatic(&OnBrowserLinkClicked))
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SButton)
				.Text(FText::FromString("Leave a review <3"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([]()
				{
					const FString URL = "https://www.fab.com/listings/d6148766-27b1-47db-a730-832c53b7a895";
					FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);

					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SSpacer)
				.Size(FVector2D(20, 10))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SButton)
				.Text(FText::FromString("Discover Darker Nodes"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([]()
				{
					const FString URL = "https://www.fab.com/listings/7fcc3b88-bddc-4ac7-8132-1be496bb3caf";
					FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);

					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SSpacer)
				.Size(FVector2D(20, 10))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SButton)
				.Text(FText::FromString("Close this window"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([Window]()
				{
					Window->RequestDestroyWindow();

					return FReply::Handled();
				})
			]
		];

	WindowContent->SetContent(InnerContent);
	Window = FSlateApplication::Get().AddWindow(Window.ToSharedRef());
}
