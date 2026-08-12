#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputTriggers.h"            // ETriggerEvent
#include "AC_InputMapper.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/** One row in the details-panel table: an Input Action -> a function to call. */
USTRUCT(BlueprintType)
struct FMappedInputBinding
{
    GENERATED_BODY()

    // Input Action asset to listen for
    UPROPERTY(EditAnywhere, Category = "Input")
    TObjectPtr<UInputAction> Action = nullptr;

    // Which phase of the action fires the function
    UPROPERTY(EditAnywhere, Category = "Input")
    ETriggerEvent TriggerEvent = ETriggerEvent::Triggered;

    // UFUNCTION on the owning actor to call. Its parameter type is inferred:
    // (no params) | float | double | bool | FVector2D | FVector | FInputActionValue
    UPROPERTY(EditAnywhere, Category = "Input", meta = (GetOptions = "GetBindableFunctionNames"))
    FName FunctionName;
};

UCLASS(ClassGroup = (Input), meta = (BlueprintSpawnableComponent))
class CPPTEMPLATE_API UAC_InputMapper : public UActorComponent
{
    GENERATED_BODY()

public:
    UAC_InputMapper();

    // Optional: a mapping context this component adds for its owner on possession
    UPROPERTY(EditAnywhere, Category = "Input")
    TObjectPtr<UInputMappingContext> MappingContext = nullptr;

    UPROPERTY(EditAnywhere, Category = "Input")
    int32 MappingPriority = 0;

    // The editor-facing table: action -> function. Add rows here, no graph needed.
    UPROPERTY(EditAnywhere, Category = "Input")
    TArray<FMappedInputBinding> Bindings;

protected:
    virtual void BeginPlay() override;

private:
    void TrySetupInput();

    // Reads the action value and calls the named function, converting the value
    // to whatever type that function's first parameter expects.
    void DispatchInput(const FInputActionValue& Value, FName FunctionName);

    UFUNCTION()
    void HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);

    // Feeds the FunctionName dropdown in the details panel
    UFUNCTION()
    TArray<FString> GetBindableFunctionNames() const;

    // Resolves the owning actor's class at both runtime and edit time
    const UClass* ResolveOwnerClass() const;

    bool bBound = false;
};