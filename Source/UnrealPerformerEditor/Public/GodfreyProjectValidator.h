#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GodfreyProjectValidator.generated.h"

UENUM(BlueprintType)
enum class EGodfreyValidationSeverity : uint8
{
	Pass UMETA(DisplayName = "PASS"),
	Warning UMETA(DisplayName = "WARNING"),
	Fail UMETA(DisplayName = "FAIL"),
};

USTRUCT(BlueprintType)
struct FGodfreyValidationItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Validation")
	FString CheckId;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Validation")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Validation")
	EGodfreyValidationSeverity Severity = EGodfreyValidationSeverity::Pass;
};

USTRUCT(BlueprintType)
struct FGodfreyValidationReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Validation")
	TArray<FGodfreyValidationItem> Items;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Validation")
	int32 PassCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Validation")
	int32 WarningCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Validation")
	int32 FailCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Godfrey|Validation")
	EGodfreyValidationSeverity Overall = EGodfreyValidationSeverity::Pass;
};

/**
 * Editor-callable Godfrey project validator (Tools → Validate Godfrey Project).
 * Read-only checks — does not modify assets or runtime behaviour.
 */
UCLASS(BlueprintType)
class UGodfreyProjectValidator : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Godfrey|Validation")
	static FGodfreyValidationReport RunValidation();

	UFUNCTION(BlueprintCallable, Category = "Godfrey|Validation")
	static void RunValidationAndLog();

private:
	static void AddItem(FGodfreyValidationReport& Report, const FString& CheckId, EGodfreyValidationSeverity Severity, const FString& Message);
};
