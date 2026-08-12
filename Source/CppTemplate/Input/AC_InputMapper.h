#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputTriggers.h"
#include "AC_InputMapper.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

// One row: an Input Action + trigger phase -> a function on the owner.
USTRUCT(BlueprintType)
struct FMappedInputBinding
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Input")
    TObjectPtr<UInputAction> Action = nullptr;

    UPROPERTY(EditAnywhere, Category = "Input")
    ETriggerEvent TriggerEvent = ETriggerEvent::Triggered;

    // Owner function to call; its parameter type is auto-detected.
    UPROPERTY(EditAnywhere, Category = "Input", meta = (GetOptions = "GetBindableFunctionNames"))
    FName FunctionName;
};

UCLASS(ClassGroup = (Input), meta = (BlueprintSpawnableComponent))
class CPPTEMPLATE_API UAC_InputMapper : public UActorComponent
{
    GENERATED_BODY()

public:
    UAC_InputMapper();

    UPROPERTY(EditAnywhere, Category = "Input")
    TObjectPtr<UInputMappingContext> MappingContext = nullptr;

    UPROPERTY(EditAnywhere, Category = "Input")
    int32 MappingPriority = 0;

    UPROPERTY(EditAnywhere, Category = "Input")
    TArray<FMappedInputBinding> Bindings;

protected:
    virtual void BeginPlay() override;

private:
    void TrySetupInput();
    void DispatchInput(const FInputActionValue& Value, FName FunctionName);

    UFUNCTION()
    void HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);

    UFUNCTION()
    TArray<FString> GetBindableFunctionNames() const;   // fills the details-panel dropdown

    const UClass* ResolveOwnerClass() const;

    bool bBound = false;
};