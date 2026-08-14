// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TerraDyneDemoMarkerActor.generated.h"

UCLASS(Blueprintable)
class TERRADYNE_API ATerraDyneDemoMarkerActor : public AActor
{
	GENERATED_BODY()

public:
	ATerraDyneDemoMarkerActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TObjectPtr<class UStaticMeshComponent> MarkerMesh;
};
