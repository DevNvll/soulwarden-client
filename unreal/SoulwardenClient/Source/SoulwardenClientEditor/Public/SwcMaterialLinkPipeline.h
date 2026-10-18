#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "SwcMaterialLinkPipeline.generated.h"

/**
 * An Interchange pipeline after the glTF pipeline of the static meshes (stage 1.2): each material of the source is the
 * key of a shared material instance (MaterialFolder/KEY). The pipeline links the slot to that instance by its path and
 * makes no material, as the reuse path of the generic material pipeline does, but with no search of the asset registry
 * (the search took about one second for each mesh).
 */
UCLASS(BlueprintType, EditInlineNew)
class SOULWARDENCLIENTEDITOR_API USwcMaterialLinkPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()

public:
	/** The folder of the shared material instances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Soulwarden")
	FString MaterialFolder = TEXT("/SwcGameContent/Materials");

protected:
	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas,
		const FString& ContentBasePath) override;

	/** The pipeline loads assets: game thread only. */
	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask) override
	{
		return false;
	}
};
