// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GRTProjectile.generated.h"

UCLASS()
class GRT_API AGRTProjectile : public AActor
{
	GENERATED_BODY()
	
public:	
	AGRTProjectile();
	
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CollisionComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> MovementComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	float CraterRadius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	float CraterDepth;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, 
		FVector NormalImpulse, const FHitResult& Hit);

private:
	bool bHasImpacted;
	float ImpactDelay;

	void ApplyTerrainDeformation(const FVector& ImpactLocation);
	void DestroyProjectile();
};
