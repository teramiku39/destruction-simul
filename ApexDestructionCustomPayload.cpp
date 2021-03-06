// Copyright Epic Games, Inc. All Rights Reserved.

#include "ApexDestructionCustomPayload.h"
#include "DestructibleComponent.h"
#include "PhysXPublic.h"
#include "ApexDestructionModule.h"
#include "AI/NavigationSystemBase.h"

#if WITH_APEX

void FApexDestructionSyncActors::BuildSyncData_AssumesLocked(const TArray<physx::PxRigidActor*>& ActiveActors)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	//We want to consolidate the transforms so that we update each destructible component once by passing it an array of chunks to update.
	//This helps avoid a lot of duplicated work like marking render dirty, computing inverse world component, etc...

	//prepare map to update destructible components
	TArray<PxShape*> Shapes;
	for (const PxRigidActor* RigidActor : ActiveActors)
	{
		if (const FApexDestructionCustomPayload* DestructibleChunkInfo = ((FApexDestructionCustomPayload*)FPhysxUserData::Get<FCustomPhysXPayload>(RigidActor->userData)))
		{
			if (GApexModuleDestructible->owns(RigidActor) && DestructibleChunkInfo->OwningComponent.IsValid())
			{
				Shapes.AddUninitialized(RigidActor->getNbShapes());
				int32 NumShapes = RigidActor->getShapes(Shapes.GetData(), Shapes.Num());
				for (int32 ShapeIdx = 0; ShapeIdx < Shapes.Num(); ++ShapeIdx)
				{
					PxShape* Shape = Shapes[ShapeIdx];
					int32 ChunkIndex;
					if (apex::DestructibleActor* DestructibleActor = GApexModuleDestructible->getDestructibleAndChunk(Shape, &ChunkIndex))
					{
						const physx::PxMat44 ChunkPoseRT = DestructibleActor->getChunkPose(ChunkIndex);
						const physx::PxTransform Transform(ChunkPoseRT);
						// begin:@minyul
						const physx::PxVec3 LinearVel = DestructibleActor->getChunkLinearVelocity(ChunkIndex);
						const physx::PxVec3 AngularVel = DestructibleActor->getChunkAngularVelocity(ChunkIndex);
						// end:@minyul
						if (UDestructibleComponent* DestructibleComponent = Cast<UDestructibleComponent>(FPhysxUserData::Get<UPrimitiveComponent>(DestructibleActor->userData)))
						{
							if (DestructibleComponent->IsRegistered())
							{
								TArray<FUpdateChunksInfo>& UpdateInfos = ComponentUpdateMapping.FindOrAdd(DestructibleComponent);
								// begin:@minyul
								//FUpdateChunksInfo* UpdateInfo = new (UpdateInfos)FUpdateChunksInfo(ChunkIndex, P2UTransform(Transform));
								FUpdateChunksInfo* UpdateInfo = new (UpdateInfos)FUpdateChunksInfo(ChunkIndex, P2UTransform(Transform), P2UVector(LinearVel), P2UVector(AngularVel));
								// end:@minyul
							}
						}
					}
				}

				Shapes.Empty(Shapes.Num());	//we want to keep largest capacity array to avoid reallocs
			}
		}
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FApexDestructionSyncActors::FinalizeSync()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	//update each component
	for (auto It = ComponentUpdateMapping.CreateIterator(); It; ++It)
	{
		if (UDestructibleComponent* DestructibleComponent = It.Key().Get())
		{
			TArray<FUpdateChunksInfo>& UpdateInfos = It.Value();
			if (DestructibleComponent->IsFracturedOrInitiallyStatic())
			{
				DestructibleComponent->SetChunksWorldTM(UpdateInfos);
			}
			else
			{
				//if we haven't fractured it must mean that we're simulating a destructible and so we should update our GetComponentTransform() based on the single rigid body
				DestructibleComponent->SyncComponentToRBPhysics();
			}

			FNavigationSystem::UpdateComponentData(*DestructibleComponent);
		}
	}

	ComponentUpdateMapping.Reset();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

TWeakObjectPtr<UPrimitiveComponent> FApexDestructionCustomPayload::GetOwningComponent() const
{
	return OwningComponent;
}

int32 FApexDestructionCustomPayload::GetItemIndex() const
{
	return ChunkIndex;
}

FName FApexDestructionCustomPayload::GetBoneName() const
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if(UDestructibleComponent* RawOwningComponent = OwningComponent.Get())
	{
		return RawOwningComponent->GetBoneName(UDestructibleComponent::ChunkIdxToBoneIdx(ChunkIndex));
	}
	else
	{
		return NAME_None;
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FBodyInstance* FApexDestructionCustomPayload::GetBodyInstance() const
{
	return OwningComponent.IsValid() ? &OwningComponent->BodyInstance : nullptr;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

#endif // WITH_APEX