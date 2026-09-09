// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GRTDemoMarkerActor.generated.h"

UCLASS(Blueprintable)
class GRT_API AGRTDemoMarkerActor : public AActor
{
	GENERATED_BODY()

public:
	AGRTDemoMarkerActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GRT")
	TObjectPtr<class UStaticMeshComponent> MarkerMesh;
};
