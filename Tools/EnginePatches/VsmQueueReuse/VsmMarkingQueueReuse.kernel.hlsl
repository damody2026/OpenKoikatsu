[numthreads(NUM_THREADS_PER_GROUP, 1, 1)]
void CullPerPageDrawCommandsCs(uint3 GroupId : SV_GroupID, int GroupThreadIndex : SV_GroupIndex)
{
	uint DispatchGroupId = GetUnWrappedDispatchGroupId(GroupId);

	// Uniform across the group: DispatchGroupId comes from SV_GroupID.
	if (DispatchGroupId >= InstanceCullingLoadBalancer_GetNumBatches())
	{
		return;
	}

	if (GroupThreadIndex == 0)
	{
		NumSharedMarkingJobs = 0U;
	}
	GroupMemoryBarrierWithGroupSync();

#if ENABLE_BATCH_MODE
	// Load Instance culling context batch info, indirection per group
	FContextBatchInfo BatchInfo = LoadBatchInfo(DispatchGroupId);
	FVSMCullingBatchInfo VSMCullingBatchInfo = VSMCullingBatchInfos[BatchInds[DispatchGroupId]];
#else // !ENABLE_BATCH_MODE
	// Single Instance culling context batch in the call, set up batch from the kernel parameters
	FContextBatchInfo BatchInfo = (FContextBatchInfo)0;
	BatchInfo.DynamicInstanceIdOffset = DynamicInstanceIdOffset;
	BatchInfo.DynamicInstanceIdMax = DynamicInstanceIdMax;
	// Note: for the unbatched case, the permutation will control HZB test, so we set to true
	BatchInfo.bAllowOcclusionCulling = true;

	FVSMCullingBatchInfo VSMCullingBatchInfo;
	VSMCullingBatchInfo.FirstPrimaryView = FirstPrimaryView;
	VSMCullingBatchInfo.NumPrimaryViews = NumPrimaryViews;

#endif // ENABLE_BATCH_MODE

	uint CurrentBatchProcessingMode = 0U;
	FInstanceWorkSetup WorkSetup = InstanceCullingLoadBalancer_Setup(GroupId, GroupThreadIndex, GetItemDataOffset(BatchInfo, CurrentBatchProcessingMode));

	// Publish the view range through groupshared so the loop bound is group-uniform,
	// including lanes whose WorkSetup is invalid.
	if (GroupThreadIndex == 0)
	{
		GroupFirstPrimaryView = VSMCullingBatchInfo.FirstPrimaryView;
		GroupPrimaryViewCount = VSMCullingBatchInfo.NumPrimaryViews;
	}
	GroupMemoryBarrierWithGroupSync();
	// Lane 0 wrote both values. Every lane reads the same groupshared locations.
	// WaveReadLaneFirst is per wave; both waves still observe the same groupshared values.
#if COMPILER_SUPPORTS_WAVE_VOTE
	const uint PrimaryViewCount = WaveReadLaneFirst(GroupPrimaryViewCount);
	const uint FirstPrimaryViewId = WaveReadLaneFirst(GroupFirstPrimaryView);
#else
	const uint PrimaryViewCount = GroupPrimaryViewCount;
	const uint FirstPrimaryViewId = GroupFirstPrimaryView;
#endif

	const bool bItemValid = WorkSetup.bValid;
	uint InstanceId = 0U;
	uint IndirectArgIndex = 0U;
	FDrawCommandDesc ItemDrawCommandDesc = (FDrawCommandDesc)0;
	FInstanceSceneData ItemInstanceData = (FInstanceSceneData)0;
	FPrimitiveSceneData PrimitiveData = (FPrimitiveSceneData)0;
	float3 ItemLocalBoundsCenter = float3(0.0f, 0.0f, 0.0f);
	float3 ItemLocalBoundsExtent = float3(0.0f, 0.0f, 0.0f);
	if (bItemValid)
	{
		const FInstanceCullingPayload Payload = LoadInstanceCullingPayload(WorkSetup.Item.Payload, BatchInfo);
		IndirectArgIndex = Payload.IndirectArgIndex;

		uint InstanceDataOffset = WorkSetup.Item.InstanceDataOffset;

		if (Payload.bDynamicInstanceDataOffset)
		{
			InstanceDataOffset += BatchInfo.DynamicInstanceIdOffset;
			checkSlow(InstanceDataOffset + uint(WorkSetup.LocalItemIndex) < BatchInfo.DynamicInstanceIdMax);
		}

		InstanceId = InstanceDataOffset + uint(WorkSetup.LocalItemIndex);
		ItemDrawCommandDesc = UnpackDrawCommandDesc(DrawCommandDescs[Payload.IndirectArgIndex]);

		// Load relevant instance data
		ItemInstanceData = GetInstanceSceneData(InstanceId);
		PrimitiveData = GetPrimitiveData(ItemInstanceData.PrimitiveId);

		ItemLocalBoundsCenter = ItemInstanceData.LocalBoundsCenter;
		ItemLocalBoundsExtent = ItemInstanceData.LocalBoundsExtent;
		LoadDynamicMeshBounds(ItemDrawCommandDesc.DynamicMeshBoundsIndex, ItemLocalBoundsCenter, ItemLocalBoundsExtent);

#if WITH_SHADERPRINT
		float4 BoundsColor = all(ItemLocalBoundsCenter == ItemInstanceData.LocalBoundsCenter) ? ColorRed : ColorGreen;
		AddOBBWS(ItemLocalBoundsCenter - ItemLocalBoundsExtent, ItemLocalBoundsCenter + ItemLocalBoundsExtent, BoundsColor, DFHackToFloat(ItemInstanceData.LocalToWorld));
#endif
	}

	uint ThreadTotalForAllViews = 0;
#if VSM_GENERATE_STATS
	uint TotalPageArea = 0U;
	float NumVisibleLevels = 0.0f;
#endif
	bool bMarkingJobQueueOverflow = false;
	bool bVisibleInstancesOverflow = false;

	for (uint ViewOrdinal = 0U; ViewOrdinal < PrimaryViewCount; ++ViewOrdinal)
	{
		// TargetNumMipLevels belongs to the primary view, not the lane. Lane 0 publishes it.
		if (GroupThreadIndex == 0)
		{
			FNaniteView PrimaryNaniteView = GetNaniteView(FirstPrimaryViewId + ViewOrdinal);
			GroupMipCount = (uint)PrimaryNaniteView.TargetNumMipLevels;
		}
		GroupMemoryBarrierWithGroupSync();
#if COMPILER_SUPPORTS_WAVE_VOTE
		const uint MipCount = WaveReadLaneFirst(GroupMipCount);
#else
		const uint MipCount = GroupMipCount;
#endif
		const uint PrimaryViewId = FirstPrimaryViewId + ViewOrdinal;

		bool bViewVisible = false;
		float PixelEstRadius = 0.0f;
		uint ViewCullingFlags = INSTANCE_CULLING_FLAGS_DEFAULT;
		bool bViewCacheAsStatic = false;
		bool bViewAllowWPO = false;
		bool bViewInvalidatePages = false;
		FFrustumCullData FrustumCull = (FFrustumCullData)0;
		uint ViewPageFlagMask = 0U;
		bool bViewUseReceiverMask = false;
#if USE_HZB_OCCLUSION
		bool bViewHZB = false;
		bool bUseStaticOcclusion = false;
#endif

		if (bItemValid)
		{
			FNaniteView NaniteView = GetNaniteView(PrimaryViewId);

			uint CullingFlags = INSTANCE_CULLING_FLAGS_DEFAULT;
			bool bEnableWPO = ItemDrawCommandDesc.bMaterialUsesWorldPositionOffset;

			FInstanceDynamicData DynamicData = CalculateInstanceDynamicData(NaniteView, ItemInstanceData, true);

			FBoxCull Cull;
			Cull.Init(
				NaniteView,
				ItemLocalBoundsCenter,
				ItemLocalBoundsExtent,
				ItemInstanceData.NonUniformScale,
				DynamicData.LocalToTranslatedWorld,
				DynamicData.PrevLocalToTranslatedWorld );

			Cull.bSkipWPODisableDistance |= ItemDrawCommandDesc.bMaterialAlwaysEvaluatesWorldPositionOffset;

			Cull.Distance( PrimitiveData );
			// NOTE: Cull.Distance may set Cull.bEnableWPO
			bEnableWPO = bEnableWPO && Cull.bEnableWPO;

			// NOTE: Doing this for just the primary view (not mip views) is fine for current logic
			const bool bAllowWPO = VirtualShadowMapIsWPOAllowed(PrimitiveData, NaniteView.TargetLayerIndex);
			bEnableWPO = bEnableWPO && bAllowWPO;
			bool bCacheAsStatic = ShouldCacheInstanceAsStatic(InstanceId, (NaniteView.Flags & NANITE_VIEW_FLAG_UNCACHED), bAllowWPO, NaniteView.SceneRendererPrimaryViewId);

			// TODO: Set Cull.bIsStaticGeometry and dynamic depth range culling?

#if USE_HZB_OCCLUSION
			bUseStaticOcclusion = bCacheAsStatic;
#endif
			Cull.SetupCacheDependentState(bCacheAsStatic, ItemInstanceData.InstanceId, NaniteView.SceneRendererPrimaryViewId);

			if (!bEnableWPO)
			{
				// Disable the Evaluate WPO culling flag if WPO was disabled
				CullingFlags &= ~INSTANCE_CULLING_FLAG_EVALUATE_WPO;
			}

			Cull.ScreenSize(ItemDrawCommandDesc.MinScreenSize, ItemDrawCommandDesc.MaxScreenSize);

			Cull.GlobalClipPlane();

			bool bInvalidatePages = ShouldMaterialInvalidateShadowCache(PrimitiveData, bEnableWPO)
					|| GetInstanceViewData(InstanceId, NaniteView.SceneRendererPrimaryViewId).bIsDeforming;

			BRANCH
			if( Cull.bIsVisible )
			{
				// Note: Cull.PageFlagMask may be set in Cull.Frustum()
				FrustumCull = Cull.Frustum();
			}

			StatsBufferInterlockedAdd(VSM_STAT_NON_NANITE_INSTANCES_TOTAL, NaniteView.TargetNumMipLevels);

			BRANCH
			if (Cull.bIsVisible)
			{
				bViewVisible = true;
				ViewCullingFlags = CullingFlags;
				bViewCacheAsStatic = bCacheAsStatic;
				bViewAllowWPO = bAllowWPO;
				bViewInvalidatePages = bInvalidatePages;
				ViewPageFlagMask = Cull.PageFlagMask;
				bViewUseReceiverMask = Cull.bUseReceiverMask;
#if USE_HZB_OCCLUSION
				bViewHZB = Cull.bViewHZB;
#endif
				// Compute estimated footprint in the VSM base level - note that we don't use the actual footprint because it changes under rotation and also with edge clipping which makes it unstable when used for culling.
				PixelEstRadius = CalcClipSpaceRadiusEstimate(Cull.bIsOrtho, ItemInstanceData, Cull.LocalToTranslatedWorld, NaniteView.ViewToClip) * float(VSM_VIRTUAL_MAX_RESOLUTION_XY);
			}
			else
			{
				StatsBufferInterlockedAdd(VSM_STAT_NON_NANITE_INSTANCES_FRUSTUM_CULLED, NaniteView.TargetNumMipLevels);
			}
		}

		for (uint MipLevel = 0U; MipLevel < MipCount; ++MipLevel)
		{
			bool bMarkThisMip = false;
			bool bIsSmallJob = false;
			bool bDoLargeJob = false;
			uint MarkFlags = 0U;
			uint4 RectPages = uint4(0, 0, 0, 0);
			uint JobFlagMask = 0U;
			uint JobVsmId = 0U;
			uint MipViewId = MipLevel * TotalPrimaryViews + PrimaryViewId;

			if (bItemValid && bViewVisible)
			{
				FNaniteView MipView = GetNaniteView(MipViewId);
				JobVsmId = uint(MipView.TargetLayerIndex);
				FVirtualShadowMapHandle VirtualShadowMapHandle = FVirtualShadowMapHandle::MakeFromId(MipView.TargetLayerIndex);

				FScreenRect Rect = GetScreenRect(MipView.ViewRect, FrustumCull, 4);

				bool bDetailGeometry = IsDetailGeometry(bViewCacheAsStatic, false, PixelEstRadius);
				// Update for next clip level
				PixelEstRadius *= 0.5f;

				Rect = VirtualShadowMapGetUncachedScreenRect( Rect, VirtualShadowMapHandle, MipLevel );
				RectPages = VirtualShadowMapGetPageRect( Rect );

				if (OverlapsAnyValidPage(VirtualShadowMapHandle, MipLevel, Rect, ViewPageFlagMask, bDetailGeometry, bViewUseReceiverMask))
				{
#if VSM_GENERATE_STATS
					TotalPageArea += float(GetInclusiveRectArea(RectPages));
					NumVisibleLevels += 1.0f;
#endif
					bool bHzbCulled = false;
#if USE_HZB_OCCLUSION
					if (bViewHZB)
					{
						// We are using the current-frame HZB and can use any page whether it is cached or not
						// TODO: Figure out which flag(s) we actually want here. Also need to check the non-nanite one?
						if (!IsVisibleMaskedHZB(VirtualShadowMapHandle, MipLevel, Rect, true, true, 0U, 0xFFFFFFFF, bUseStaticOcclusion))
						{
							StatsBufferInterlockedInc(VSM_STAT_NON_NANITE_INSTANCES_HZB_CULLED);
							bHzbCulled = true;
						}
					}
#endif // USE_HZB_OCCLUSION
					if (!bHzbCulled)
					{
						bMarkThisMip = true;
						MarkFlags = VirtualShadowMapGetMarkPageDirtyFlags(bViewCacheAsStatic, bViewInvalidatePages, bViewAllowWPO, false /*bInvalidateLODDelta*/);
						JobFlagMask = ViewPageFlagMask;
						uint2 RectPagesSize = (RectPages.zw + 1u) - RectPages.xy;
						bIsSmallJob = RectPagesSize.x * RectPagesSize.y <= MAX_SINGLE_THREAD_MARKING_AREA;
						// NOTE: Large job only necessary if we actually have flags to write
						bDoLargeJob = !bIsSmallJob && (MarkFlags != 0U);
					}
				}
				else
				{
					StatsBufferInterlockedInc(VSM_STAT_NON_NANITE_INSTANCES_PAGE_MASK_CULLED);
				}
			}

			// Queue round. Every lane, including invalid, culled, and HZB-culled lanes, reaches both barriers.
			// A lane enqueues at most one large job, so the round occupies at most NUM_THREADS_PER_GROUP slots.
			bool bThisJobOverflow = false;
			if (GroupThreadIndex == 0)
			{
				NumSharedMarkingJobs = 0U;
			}
			GroupMemoryBarrierWithGroupSync();

			if (bDoLargeJob)
			{
				uint LargeJobIndex = 0U;
				InterlockedAdd(NumSharedMarkingJobs, 1U, LargeJobIndex);
				if (LargeJobIndex >= MARKING_JOB_QUEUE_SIZE)
				{
					bMarkingJobQueueOverflow = true;
					bThisJobOverflow = true;
				}
				else
				{
					FSharedMarkingJob Job;
					Job.RectPages = RectPages;
					Job.VirtualShadowMapId = JobVsmId;
					Job.MipLevel = MipLevel;
					Job.FlagMask = JobFlagMask;
					Job.MarkPageDirtyFlags = MarkFlags;
					SharedMarkingJobs[LargeJobIndex] = PackJob(Job);
				}
			}
			GroupMemoryBarrierWithGroupSync();

			uint NumMarkingJobs = NumSharedMarkingJobs;
			for (uint JobIndex = 0U; JobIndex < min(MARKING_JOB_QUEUE_SIZE, NumMarkingJobs); ++JobIndex)
			{
				FSharedMarkingJob Job = UnpackJob(SharedMarkingJobs[JobIndex]);
				FVirtualShadowMapHandle VirtualShadowMapHandle = FVirtualShadowMapHandle::MakeFromId(Job.VirtualShadowMapId);
				FVirtualSMLevelOffset PageTableLevelOffset = CalcPageTableLevelOffset(VirtualShadowMapHandle, Job.MipLevel);
				uint2 RectPagesSize = (Job.RectPages.zw + 1u) - Job.RectPages.xy;
				for (uint Index = GroupThreadIndex; Index < RectPagesSize.x * RectPagesSize.y; Index += NUM_THREADS_PER_GROUP)
				{
					uint LocalPageY = uint(floor((float(Index) + 0.5f) * rcp(RectPagesSize.x)));
					uint2 vPage = Job.RectPages.xy + uint2(Index - RectPagesSize.x * LocalPageY, LocalPageY);

					// We don't need to add the correct flag mask here, as we don't look at the VSM_FLAG_DETAIL_GEOMETRY for invalidaiton processing
					MarkPageDirty(PageTableLevelOffset, vPage, Job.MipLevel, Job.FlagMask, Job.MarkPageDirtyFlags);
				}
			}

			uint NumMappedPages = 0U;
			if (bMarkThisMip && (bIsSmallJob || bThisJobOverflow))
			{
				FVirtualShadowMapHandle VirtualShadowMapHandle = FVirtualShadowMapHandle::MakeFromId(JobVsmId);
				FVirtualSMLevelOffset PageTableLevelOffset = CalcPageTableLevelOffset(VirtualShadowMapHandle, MipLevel);

				for (uint Y = RectPages.y; Y <= RectPages.w; ++Y)
				{
					for (uint X = RectPages.x; X <= RectPages.z; ++X)
					{
						if (MarkPageDirty(PageTableLevelOffset, uint2(X, Y), MipLevel, JobFlagMask, MarkFlags))
						{
							// Count pages if we're touching all of them anyway
							++NumMappedPages;
						}
					}
				}
			}
			else if (bMarkThisMip)
			{
				// Must assume we have mapped pages (or defer the cmd write also...)
				NumMappedPages = 1U;
			}

			if (NumMappedPages > 0U)
			{
				++ThreadTotalForAllViews;
				bVisibleInstancesOverflow |= !WriteCmd(MipViewId, InstanceId, IndirectArgIndex, ViewCullingFlags, bViewCacheAsStatic);
			}
			// Finish all queue reads before lane 0 may reset or overwrite the next round.
			GroupMemoryBarrierWithGroupSync();
		}
	}

	if (bItemValid)
	{
		StatsBufferInterlockedAdd(VSM_STAT_NON_NANITE_INSTANCES_DRAWN, ThreadTotalForAllViews);
		StatsBufferInterlockedEnableFlags(VSM_STAT_OVERFLOW_FLAGS, VSM_STAT_OVERFLOW_FLAG_MARKING_JOB_QUEUE, bMarkingJobQueueOverflow, true);
		StatsBufferInterlockedEnableFlags(VSM_STAT_OVERFLOW_FLAGS, VSM_STAT_OVERFLOW_FLAG_VISIBLE_INSTANCES, bVisibleInstancesOverflow, true);

		// Accumulate total number of instances for each indirect argument, is also used to allocate space and output compact range of instances later
		InterlockedAdd(DrawIndirectArgsBufferOut[IndirectArgIndex * INDIRECT_ARGS_NUM_WORDS + 1], ThreadTotalForAllViews);

#if VSM_GENERATE_STATS
		if (NumPageAreaDiagnosticSlots > 0U && TotalPageArea > LargeInstancePageAreaThreshold)
		{
			for (uint Index = 0U; Index < NumPageAreaDiagnosticSlots; ++Index)
			{
				uint PrevArea = 0U;
				// Store these after the stats slots
				InterlockedMax(OutStatsBuffer[VSM_STAT_NUM + 2U * Index], TotalPageArea, PrevArea);
				// If the area was greater then we store the persistent Primitive ID after the area.
				// This obviously has a data race, so numbers & IDs will only be approximately correct, could use 64-bit atomics instead...
				if (PrevArea < TotalPageArea)
				{
					OutStatsBuffer[VSM_STAT_NUM + 2U * Index + 1U] = PrimitiveData.PersistentPrimitiveIndex;
					break;
				}
			}
		}
#endif
	}
}
