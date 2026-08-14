// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "World/TerraDyneProjectile.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSubsystem.h"
#include "World/TerraDyneChunk.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"
#include "Components/SphereComponent.h"

ATerraDyneProjectile::ATerraDyneProjectile()
{
	PrimaryActorTick.bCanEverTick = true;

	// The impact carves terrain, so the projectile must be an authority-driven actor rather than a
	// local effect: a client-side copy would deform only its own machine and desync from everyone.
	bReplicates = true;
	SetReplicateMovement(true);

	// Create sphere collision as root
	CollisionComp = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComp"));
	CollisionComp->InitSphereRadius(50.0f);
	CollisionComp->SetCollisionProfileName(TEXT("PhysicsActor"));
	CollisionComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComp->SetCollisionObjectType(ECC_PhysicsBody);
	CollisionComp->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComp->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	CollisionComp->OnComponentHit.AddDynamic(this, &ATerraDyneProjectile::OnHit);
	SetRootComponent(CollisionComp);

	// Create mesh
	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereObj(TEXT("/Engine/BasicShapes/Sphere"));
	if (SphereObj.Succeeded()) 
	{
		MeshComp->SetStaticMesh(SphereObj.Object);
	}
	MeshComp->SetupAttachment(CollisionComp);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Projectile movement
	MovementComp = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("MoveComp"));
	MovementComp->UpdatedComponent = CollisionComp;
	MovementComp->InitialSpeed = 5000.f;
	MovementComp->MaxSpeed = 8000.f;
	MovementComp->bRotationFollowsVelocity = true;
	MovementComp->bShouldBounce = false;
	MovementComp->ProjectileGravityScale = 1.0f;

	// Defaults
	CraterRadius = 800.0f;
	CraterDepth = -1000.0f;
	bHasImpacted = false;
}

void ATerraDyneProjectile::BeginPlay()
{
	Super::BeginPlay();
	
	SetActorScale3D(FVector(2.0f));
	
	UE_LOG(LogTemp, Verbose, TEXT("Meteor spawned at %s"), *GetActorLocation().ToString());
}

void ATerraDyneProjectile::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	// Cleanup if falls below world
	if (GetActorLocation().Z < -10000.0f)
	{
		Destroy();
	}
}

void ATerraDyneProjectile::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, 
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (bHasImpacted) return;

	// Only the authority decides an impact happened; clients receive the resulting terrain edit
	// through the manager's multicast like any other change.
	if (!HasAuthority())
	{
		return;
	}

	bHasImpacted = true;

	UE_LOG(LogTemp, Log, TEXT("Meteor IMPACT at %s on %s"), *Hit.Location.ToString(), 
		OtherActor ? *OtherActor->GetName() : TEXT("NULL"));

	// Visual debug - Reduced clutter
	// DrawDebugSphere(GetWorld(), Hit.Location, CraterRadius, 32, FColor::Orange, false, 3.0f, 0, 10.0f);
	
	// Apply terrain deformation
	ApplyTerrainDeformation(Hit.Location);

	// Destroy self
	Destroy();
}

void ATerraDyneProjectile::ApplyTerrainDeformation(const FVector& ImpactLocation)
{
	UWorld* World = GetWorld();
	if (!World) return;

	// Get manager through subsystem
	ATerraDyneManager* Manager = nullptr;
	if (UTerraDyneSubsystem* Subsystem = World->GetSubsystem<UTerraDyneSubsystem>())
	{
		Manager = Subsystem->GetTerrainManager();
	}

	// Fallback: find in world
	if (!Manager)
	{
		TArray<AActor*> FoundManagers;
		UGameplayStatics::GetAllActorsOfClass(World, ATerraDyneManager::StaticClass(), FoundManagers);
		if (FoundManagers.Num() > 0)
		{
			Manager = Cast<ATerraDyneManager>(FoundManagers[0]);
		}
	}

	if (Manager)
	{
		// Authority applies and mirrors to everyone else, the same path player edits take. Applying
		// this locally on each machine instead would double-apply on the host and drift on clients.
		FTerraDyneBrushParams BrushParams;
		BrushParams.WorldLocation = ImpactLocation;
		BrushParams.Radius = CraterRadius;
		BrushParams.Strength = CraterDepth;
		BrushParams.BrushMode = ETerraDyneBrushMode::Lower;

		Manager->ApplyGlobalBrush(BrushParams.WorldLocation, BrushParams.Radius, BrushParams.Strength, BrushParams.BrushMode);
		Manager->Multicast_ApplyBrush(BrushParams);

		// Apply scorch mark (paint layer 1)
		
		UE_LOG(LogTemp, Log, TEXT("Crater applied to Sculpt layer: Radius=%.0f, Depth=%.0f"), CraterRadius, CraterDepth);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("No TerraDyneManager found!"));
	}
}
