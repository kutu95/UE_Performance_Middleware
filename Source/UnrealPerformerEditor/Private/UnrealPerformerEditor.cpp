#include "UnrealPerformerEditor.h"

#include "GodfreyProjectValidator.h"
#include "ToolMenus.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "UnrealPerformerEditor"

void FUnrealPerformerEditorModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealPerformerEditorModule::RegisterMenus));
}

void FUnrealPerformerEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UnregisterMenus();
}

void FUnrealPerformerEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = Menu->FindOrAddSection("Godfrey");
	Section.Label = LOCTEXT("GodfreySection", "Godfrey");
	Section.AddMenuEntry(
		"ValidateGodfreyProject",
		LOCTEXT("ValidateGodfreyProject", "Validate Godfrey Project"),
		LOCTEXT("ValidateGodfreyProjectTooltip", "Run Phase 1 hardening checks (plugins, performer, ACE, queue, assets). Results go to Output Log."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			UGodfreyProjectValidator::RunValidationAndLog();

			FNotificationInfo Info(LOCTEXT("ValidateDone", "Validate Godfrey Project finished — see Output Log (LogGodfreyValidation)."));
			Info.ExpireDuration = 4.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		})));
}

void FUnrealPerformerEditorModule::UnregisterMenus()
{
	UToolMenus::UnregisterOwner(this);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUnrealPerformerEditorModule, UnrealPerformerEditor)
