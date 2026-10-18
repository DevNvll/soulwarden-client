#include "SwcMaterialLinkPipeline.h"

#include "InterchangeMaterialFactoryNode.h"
#include "Materials/MaterialInterface.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

void USwcMaterialLinkPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas,
	const FString& ContentBasePath)
{
	if (!BaseNodeContainer)
	{
		return;
	}
	BaseNodeContainer->IterateNodesOfType<UInterchangeBaseMaterialFactoryNode>([this](const FString& NodeUid, UInterchangeBaseMaterialFactoryNode* Node)
	{
		const FString Name = Node->GetDisplayLabel();
		const FSoftObjectPath Path(FString::Printf(TEXT("%s/%s.%s"), *MaterialFolder, *Name, *Name));
		if (Path.TryLoad() != nullptr)
		{
			Node->SetCustomReferenceObject(Path);
			Node->SetCustomIsMaterialImportEnabled(false);
			Node->SetEnabled(false);
		}
	});
}
