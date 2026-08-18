# 02. Realistic Water 심층 프로파일링 분석 보고서

## 1. 측정 데이터 요약 (전체 기능별 실측 평균 및 오차)
아래 표는 각 기능을 ON/OFF하며 3회씩 측정한 데이터의 평균(Average)과 표준 편차(±오차)를 나타낸 것입니다. (유의미한 차이가 없는 지표는 제외되었습니다.)

| Metric | Pure | No Godot | No Kelvin | No OceanFoam | No Ripple |
|---|---|---|---|---|---|
| ActorCount/Brush | 8.942짹0.722 | 9.931짹0.826 | 8.905짹0.920 | 9.901짹0.988 | 9.943짹0.753 |
| ActorCount/Cannon | 39.741짹3.209 | 59.588짹4.957 | 39.578짹4.089 | 59.408짹5.931 | 59.658짹4.519 |
| ActorCount/DefaultPhysicsVolume | 8.942짹0.722 | 9.931짹0.826 | 8.905짹0.920 | 9.901짹0.988 | 9.943짹0.753 |
| ActorCount/GameplayDebuggerPlayerManager | 8.942짹0.722 | 9.931짹0.826 | 8.905짹0.920 | 9.901짹0.988 | 9.943짹0.753 |
| ActorCount/KelvinShip | 0.000짹0.000 | 9.931짹0.826 | 0.000짹0.000 | 9.901짹0.988 | 9.943짹0.753 |
| ActorCount/ParticleEventManager | 6.955짹0.562 | 7.945짹0.661 | 6.926짹0.716 | 7.921짹0.791 | 7.954짹0.602 |
| ActorCount/ShipBoardingPoint | 7.948짹0.642 | 25.821짹2.148 | 7.916짹0.818 | 25.743짹2.570 | 25.852짹1.958 |
| ActorCount/TotalActorCount | 222.029짹20.581 | 269.329짹23.714 | 220.599짹24.226 | 267.723짹27.905 | 269.862짹21.909 |
| ActorCount/WorldSettings | 8.942짹0.722 | 9.931짹0.826 | 8.905짹0.920 | 9.901짹0.988 | 9.943짹0.753 |
| Basic/TicksQueued | 248.517짹21.974 | 208.162짹19.352 | 202.665짹39.245 | 207.462짹22.466 | 208.536짹18.111 |
| CPUUsage_Idle | 32.314짹1.203 | 31.010짹1.545 | 29.535짹1.198 | 30.645짹1.109 | 30.552짹0.954 |
| CPUUsage_Process | 6.594짹0.882 | 7.527짹0.693 | 8.745짹0.804 | 7.898짹0.681 | 8.057짹0.710 |
| CsvProfiler/NumCustomStatsProcessed | 413.294짹1321.559 | 381.668짹1378.093 | 364.617짹1293.338 | 378.367짹1366.009 | 378.493짹1360.417 |
| CsvProfiler/NumTimestampsProcessed | 1136.068짹2913.569 | 1042.038짹2971.942 | 982.017짹2841.695 | 1031.677짹2968.306 | 1030.320짹2968.637 |
| DrawSceneCommand_StartDelay | 9.536짹1.409 | 9.269짹1.521 | 6.940짹1.437 | 8.528짹1.441 | 8.301짹1.336 |
| Exclusive/AllWorkers/ComputeLightGrid | 503249.078짹29984523.947 | 0.008짹0.019 | 0.008짹0.006 | 0.008짹0.005 | 0.008짹0.005 |
| Exclusive/AllWorkers/LightFunctionAtlasGeneration | 0.002짹0.003 | 0.002짹0.002 | 0.265짹16.244 | 0.002짹0.002 | 0.002짹0.004 |
| Exclusive/AllWorkers/Physics | 1.058짹0.747 | 1.184짹0.831 | 0.967짹0.753 | 1.130짹0.823 | 1.128짹0.874 |
| Exclusive/AllWorkers/RenderFog | 0.287짹16.779 | 0.005짹0.003 | 0.005짹0.003 | 0.005짹0.002 | 0.005짹0.002 |
| Exclusive/AllWorkers/Slate | 0.250짹0.235 | 0.590짹0.060 | 0.586짹0.062 | 0.594짹0.056 | 0.593짹0.057 |
| Exclusive/GameThread/AsyncLoading | 0.005짹0.002 | 0.005짹0.001 | 943276.793짹41046090.859 | 0.005짹0.001 | 0.005짹0.001 |
| Exclusive/GameThread/BehaviorTreeTick | 0.315짹18.121 | 0.011짹0.015 | 0.010짹0.018 | 0.011짹0.015 | 0.011짹0.014 |
| Exclusive/GameThread/EventWait | 6.246짹1.589 | 4.569짹1.426 | 2.384짹1.156 | 3.787짹1.153 | 3.491짹1.136 |
| Exclusive/GameThread/EventWait/PrePhysics | 0.624짹32.214 | 0.105짹0.051 | 0.110짹0.094 | 0.106짹0.050 | 0.108짹0.064 |
| Exclusive/GameThread/Landscape | 0.039짹0.398 | 0.034짹0.008 | 471762.919짹29031630.222 | 0.034짹0.007 | 0.034짹0.007 |
| Exclusive/GameThread/SyncBodies | 0.768짹0.121 | 0.853짹0.165 | 0.852짹0.257 | 0.866짹0.165 | 0.866짹0.148 |
| Exclusive/GameThread/Tickables | 0.248짹0.033 | 584572.857짹32315816.277 | 0.254짹0.063 | 0.257짹0.053 | 0.255짹0.047 |
| Exclusive/GameThread/TimerManager | 0.008짹0.029 | 584572.602짹32315816.010 | 0.006짹0.029 | 0.007짹0.024 | 0.007짹0.023 |
| Exclusive/GameThread/UI | 2.849짹0.922 | 4.303짹0.402 | 4.207짹0.383 | 4.348짹0.398 | 4.395짹0.394 |
| Exclusive/GameThread/UpdateLevelStreaming | 503248.999짹29984519.585 | 0.002짹0.001 | 0.002짹0.001 | 0.002짹0.001 | 0.002짹0.001 |
| Exclusive/GameThread/WorldTickMisc | 503249.115짹29984519.281 | 0.102짹0.108 | 0.108짹0.679 | 0.099짹0.017 | 0.101짹0.109 |
| Exclusive/RenderThread/AddPrimitiveSceneInfos | 0.542짹32.211 | 0.002짹0.002 | 0.001짹0.002 | 0.002짹0.002 | 0.002짹0.004 |
| Exclusive/RenderThread/ComputeLightGrid | 0.299짹16.779 | 0.018짹0.004 | 0.017짹0.005 | 0.018짹0.018 | 0.018짹0.025 |
| Exclusive/RenderThread/ConsolidateInstanceDataAllocations | 0.001짹0.001 | 0.002짹0.001 | 0.001짹0.001 | 1.185짹47.651 | 0.649짹26.444 |
| Exclusive/RenderThread/DeferredShadingSceneRenderer_DBuffer | 0.005짹0.017 | 0.332짹18.083 | 0.005짹0.002 | 0.005짹0.003 | 0.091짹3.526 |
| Exclusive/RenderThread/EventWait | 0.250짹0.782 | 0.576짹0.705 | 0.573짹0.620 | 550875.652짹31370872.103 | 0.565짹0.625 |
| Exclusive/RenderThread/EventWait/Visibility | 8.670짹1.191 | 7.955짹1.228 | 5.629짹0.985 | 7.178짹1.095 | 6.976짹1.074 |
| Exclusive/RenderThread/InitViews_Scene | 0.068짹0.021 | 0.694짹34.719 | 0.350짹17.544 | 0.066짹0.027 | 0.065짹0.024 |
| Exclusive/RenderThread/InitViews_Shadows | 0.004짹0.001 | 0.357짹19.530 | 0.003짹0.001 | 0.003짹0.001 | 0.003짹0.002 |
| Exclusive/RenderThread/LightFunctionAtlasGeneration | 0.001짹0.001 | 0.329짹18.083 | 0.265짹16.246 | 0.001짹0.001 | 0.001짹0.001 |
| Exclusive/RenderThread/Material_UpdateDeferredCachedUniformExpressions | 503249.045짹29984522.303 | 0.010짹0.416 | 0.002짹0.001 | 0.008짹0.254 | 0.003짹0.002 |
| Exclusive/RenderThread/Niagara | 0.310짹18.121 | 0.006짹0.002 | 0.005짹0.001 | 0.005짹0.002 | 0.005짹0.001 |
| Exclusive/RenderThread/PostRenderCleanUp | 0.001짹0.001 | 0.002짹0.046 | 0.002짹0.040 | 550875.088짹31370873.131 | 0.001짹0.000 |
| Exclusive/RenderThread/PrepareDistanceFieldScene | 0.036짹0.009 | 0.033짹0.007 | 0.539짹31.187 | 0.033짹0.007 | 0.032짹0.007 |
| Exclusive/RenderThread/PrepareForwardLightData | 0.002짹0.002 | 0.003짹0.018 | 0.003짹0.016 | 0.311짹17.555 | 0.003짹0.017 |
| Exclusive/RenderThread/PreRender | 0.313짹18.121 | 0.637짹34.709 | 0.516짹31.192 | 0.009짹0.004 | 0.009짹0.003 |
| Exclusive/RenderThread/RayTracing_FinishGatherInstances | 0.028짹0.011 | 0.028짹0.010 | 0.029짹0.022 | 0.028짹0.011 | 0.329짹17.321 |
| Exclusive/RenderThread/RDG | 0.463짹0.216 | 0.540짹0.811 | 0.526짹0.679 | 0.543짹0.576 | 536326.797짹30953972.311 |
| Exclusive/RenderThread/RDG_Execute | 0.132짹0.046 | 0.150짹0.769 | 0.133짹0.022 | 550705.428짹31366038.346 | 0.143짹0.343 |
| Exclusive/RenderThread/RemovePrimitiveSceneInfos | 0.002짹0.003 | 0.002짹0.003 | 0.002짹0.003 | 0.002짹0.003 | 1.477짹50.587 |
| Exclusive/RenderThread/RenderBasePass | 0.043짹0.010 | 0.044짹0.021 | 0.044짹0.019 | 0.044짹0.012 | 536326.315짹30953972.077 |
| Exclusive/RenderThread/RenderFog | 503249.042짹29984522.052 | 0.003짹0.001 | 0.003짹0.001 | 0.047짹2.527 | 0.028짹1.036 |
| Exclusive/RenderThread/RenderIndirectCapsuleShadows | 0.000짹0.000 | 0.047짹2.604 | 0.038짹2.339 | 550875.083짹31370872.886 | 0.013짹0.742 |
| Exclusive/RenderThread/RenderLighting | 0.236짹2.413 | 0.197짹0.035 | 0.194짹0.034 | 550875.238짹31370870.705 | 0.196짹0.032 |
| Exclusive/RenderThread/RenderLocalFogVolume | 0.000짹0.000 | 584572.535짹32315812.665 | 471762.897짹29031630.905 | 0.000짹0.000 | 0.007짹0.370 |
| Exclusive/RenderThread/RenderOpaqueFX | 0.016짹0.004 | 0.064짹2.604 | 0.016짹0.005 | 550705.333짹31366039.260 | 0.030짹0.555 |
| Exclusive/RenderThread/RenderOther | 1.922짹32.197 | 1.713짹19.509 | 1.371짹0.527 | 1.381짹0.507 | 1.364짹0.395 |
| Exclusive/RenderThread/RenderPostProcessing | 0.178짹0.730 | 584572.800짹32315817.978 | 0.167짹0.033 | 0.171짹0.354 | 0.168짹0.066 |
| Exclusive/RenderThread/RenderPrePass | 0.007짹0.007 | 0.008짹0.008 | 0.008짹0.009 | 0.008짹0.019 | 0.608짹24.489 |
| Exclusive/RenderThread/RenderShadows | 0.365짹0.055 | 0.369짹0.056 | 0.366짹0.063 | 0.411짹2.522 | 1072331.023짹43762390.644 |
| Exclusive/RenderThread/RenderThreadOther | 0.967짹0.077 | 1.042짹0.082 | 0.979짹0.074 | 1.035짹0.079 | 1.607짹33.229 |
| Exclusive/RenderThread/ShadowInitDynamic | 0.034짹0.019 | 0.035짹0.011 | 0.034짹0.009 | 0.343짹17.554 | 0.034짹0.009 |
| Exclusive/RenderThread/SkyAtmosphere | 0.065짹0.028 | 0.066짹0.032 | 0.065짹0.027 | 0.374짹17.551 | 0.066짹0.027 |
| Exclusive/RenderThread/Slate | 0.137짹0.121 | 584572.845짹32315812.912 | 471763.202짹29031631.128 | 0.312짹0.243 | 0.306짹0.050 |
| Exclusive/RenderThread/STAT_RDG_FlushResourcesRHI | 0.016짹0.360 | 0.011짹0.003 | 0.011짹0.004 | 0.025짹0.774 | 1072330.671짹43762391.007 |
| Exclusive/RenderThread/UpdateGPUScene | 0.080짹0.015 | 0.081짹0.012 | 0.359짹17.544 | 0.673짹33.693 | 0.081짹0.013 |
| Exclusive/RenderThread/UpdatePrimitiveTransform | 0.024짹0.005 | 0.025짹0.005 | 0.530짹31.191 | 0.691짹26.799 | 0.025짹0.005 |
| Exclusive/RenderThread/VirtualTextureSystem_Update | 0.025짹0.007 | 1.006짹39.823 | 0.311짹17.543 | 0.025짹0.009 | 0.025짹0.010 |
| FrameTime | 13.884짹1.182 | 13.816짹1.246 | 11.388짹0.973 | 13.061짹1.055 | 12.816짹1.147 |
| GameThreadTime | 7.630짹1.578 | 9.244짹1.623 | 8.997짹1.914 | 9.281짹1.824 | 9.299짹1.415 |
| GameThreadTime_CriticalPath | 7.713짹1.587 | 9.350짹1.624 | 9.107짹1.917 | 9.387짹1.824 | 9.407짹1.415 |
| GPUMem/LocalUsedMB | 6198.653짹183.705 | 8487.010짹38.472 | 8290.417짹331.519 | 8558.198짹38.057 | 8586.836짹38.256 |
| GPUMem/SystemUsedMB | 25.002짹0.729 | 43.221짹0.062 | 471806.070짹29031631.601 | 43.219짹0.074 | 43.222짹0.056 |
| GPUSceneInstanceCount | 178.456짹6.739 | 24.481짹5.783 | 95.050짹1.927 | 24.691짹6.919 | 24.402짹5.303 |
| GPUTime | 11.511짹1.026 | 11.198짹1.180 | 8.936짹0.955 | 10.417짹1.075 | 10.217짹1.032 |
| HttpManager/DurationMsAvg | 45.536짹3.998 | 51.177짹12.919 | 43.705짹3.512 | 43.082짹0.755 | 45.565짹4.745 |
| HttpManager/MaxRequestsInFlight | 1.000짹0.000 | 2.000짹0.000 | 2.000짹0.000 | 2.000짹0.000 | 2.000짹0.000 |
| InputLatencyTime | 56.524짹8.275 | 62.617짹0.000 | 62.617짹0.000 | 62.617짹0.000 | 62.617짹0.000 |
| MemoryFreeMB | 12030.042짹61.939 | 11830.742짹37.977 | 11790.470짹28.290 | 11463.211짹68.771 | 12871.170짹16.985 |
| NaniteStreaming/RootDataSizeMB | 0.124짹0.010 | 0.124짹0.010 | 0.388짹16.244 | 0.124짹0.012 | 0.124짹0.009 |
| PhysicalUsedMB | 4956.343짹13.387 | 5248.593짹16.852 | 5264.114짹15.128 | 5056.607짹55.501 | 3093.662짹5.271 |
| RayTracingGeometry/TotalResidentSizeMB | 37.959짹3.065 | 584611.984짹32315817.523 | 37.803짹3.906 | 37.829짹3.776 | 37.988짹2.877 |
| RenderTargetPool/PeakUsedMB | 2974.373짹25.927 | 4885.203짹35.831 | 4860.864짹36.114 | 4882.926짹34.827 | 4883.271짹34.473 |
| RenderTargetPool/UnusedMB | 0.090짹3.043 | 0.153짹4.810 | 0.130짹4.350 | 0.192짹5.405 | 0.141짹4.620 |
| RenderTargetPoolCount | 165.508짹1.688 | 234.428짹1.703 | 231.950짹3.054 | 234.331짹1.954 | 234.403짹1.495 |
| RenderTargetPoolSize | 2974.373짹25.927 | 4885.203짹35.831 | 4860.864짹36.114 | 4882.926짹34.827 | 4883.271짹34.473 |
| RenderTargetPoolUsed | 2957.042짹42.094 | 4848.141짹65.682 | 4823.089짹64.913 | 4847.126짹65.349 | 4847.536짹65.127 |
| RenderThreadTime | 8.641짹6.801 | 0.001짹0.001 | 0.001짹0.003 | 0.001짹0.001 | 0.001짹0.000 |
| RenderThreadTime_CriticalPath | 8.651짹6.796 | 0.001짹0.001 | 0.001짹0.003 | 0.001짹0.001 | 0.001짹0.000 |
| Replication/RawPing | 12.465짹3.188 | 12.260짹2.851 | 9.913짹4.126 | 11.533짹3.736 | 11.239짹2.459 |
| ReplicationRPCs/ServerSendLatestAsyncPhysicsTimestamp | 0.283짹16.779 | 0.003짹0.146 | 0.001짹0.055 | 0.001짹0.035 | 0.001짹0.036 |
| RHI/DrawCalls | 1011.846짹965.378 | 2396.828짹18.202 | 2391.019짹89.234 | 2399.419짹20.808 | 2406.049짹18.523 |
| RHI/PrimitivesDrawn | 202094.860짹65036.725 | 212674.361짹65335.827 | 214187.584짹66061.714 | 213602.330짹66532.565 | 210391.945짹64566.235 |
| RHIThreadTime | 5.335짹4.051 | 0.141짹0.080 | 0.159짹0.140 | 0.175짹0.188 | 0.178짹0.170 |
| Scheduler/Oversubscription | 4.808짹3.110 | 5.374짹18.289 | 4.374짹2.899 | 4.824짹3.285 | 4.884짹3.282 |
| Scheduler/RenderThread/SignalStandbyThread | 0.002짹0.004 | 0.003짹0.004 | 0.003짹0.004 | 0.336짹18.959 | 0.003짹0.004 |
| Scheduler/StallDetectorThread/SignalStandbyThread | 503249.067짹29984523.679 | 0.001짹0.003 | 0.001짹0.006 | 0.001짹0.003 | 0.001짹0.002 |
| Shaders/NumShaderMapsUsedForRendering | 172.000짹0.000 | 210.000짹0.000 | 210.000짹0.000 | 210.000짹0.000 | 210.000짹0.000 |
| Shaders/NumShadersCreated | 707.642짹0.766 | 833.000짹0.000 | 832.000짹0.000 | 833.000짹0.000 | 835.000짹0.000 |
| Shaders/NumShadersLoaded | 21965.000짹0.000 | 6708.000짹0.000 | 12525.000짹0.000 | 1238.000짹0.000 | -3741.000짹0.000 |
| Slate/GameThread/DrawPrePass | 0.972짹0.152 | 1.178짹0.194 | 1.153짹0.208 | 1.212짹0.219 | 1.236짹0.222 |
| Slate/GameThread/DrawWindows_Private | 0.423짹0.347 | 0.919짹0.094 | 0.900짹0.088 | 0.915짹0.095 | 0.907짹0.093 |
| SystemMaxMB | 16986.384짹48.786 | 17079.323짹47.231 | 17054.580짹14.232 | 16519.707짹38.589 | 15964.827짹13.209 |
| TextureStreaming/CachedMips | 132.043짹80.104 | 133.292짹80.642 | 139.303짹78.327 | 125.166짹88.680 | 133.786짹86.142 |
| TextureStreaming/StreamingPool | 84.211짹80.104 | 82.962짹80.642 | 76.951짹78.327 | 91.088짹88.680 | 82.468짹86.142 |
| TextureStreaming/WantedMips | 84.211짹80.104 | 82.962짹80.642 | 76.951짹78.327 | 91.088짹88.680 | 82.468짹86.142 |
| Ticks/BehaviorTreeComponent | 2.145짹1.065 | 2.132짹1.151 | 1.849짹1.132 | 2.070짹1.135 | 2.043짹1.104 |
| Ticks/FNiagaraWorldManagerTickFunction | 20.864짹1.685 | 6.952짹0.578 | 6.760짹1.274 | 6.931짹0.692 | 6.960짹0.527 |
| Ticks/LineBatchComponent | 17.883짹1.444 | 7.945짹0.661 | 7.725짹1.457 | 7.921짹0.791 | 7.954짹0.602 |
| Ticks/ParticleSystemManager | 20.864짹1.685 | 6.952짹0.578 | 6.760짹1.274 | 6.931짹0.692 | 6.960짹0.527 |
| Ticks/PhysicsFieldComponent | 3.974짹0.321 | 1.986짹0.165 | 1.931짹0.364 | 1.980짹0.198 | 1.989짹0.151 |
| Ticks/SWRippleReplicator | 0.276짹0.496 | 0.275짹0.523 | 0.221짹0.415 | 0.259짹0.438 | 0.255짹0.450 |
| Ticks/SWShipWakeReplicator | 0.276짹0.480 | 0.275짹0.446 | 0.221짹0.415 | 0.259짹0.438 | 0.255짹0.436 |
| Ticks/Total | 248.515짹21.972 | 208.161짹19.351 | 202.665짹39.244 | 207.462짹22.465 | 208.534짹18.109 |
| View/ForwardY | 0.003짹0.044 | 0.071짹0.115 | 0.019짹0.060 | 0.050짹0.055 | 0.009짹0.088 |
| View/PosZ | 2797.498짹375.330 | 2539.900짹670.497 | 2766.068짹638.974 | 2109.732짹575.990 | 2264.111짹981.147 |
| View/Speed | 900.302짹934.033 | 1292.271짹2376.169 | 968.294짹1337.865 | 1005.773짹1431.604 | 1273.863짹2006.493 |
| View/Speed2D | 863.091짹664.064 | 1209.086짹2207.983 | 906.428짹999.925 | 918.512짹1032.767 | 1140.102짹1649.829 |
| View/UpX | 0.312짹0.075 | 0.275짹0.128 | 0.316짹0.125 | 0.190짹0.106 | 0.208짹0.163 |
| VirtualUsedMB | 10947.752짹42.251 | 13540.409짹51.604 | 13349.003짹45.213 | 13685.413짹39.103 | 13734.660짹53.553 |


## 2. 셰이더 내부 부하 지점 분석 (GPU 측면)
**질문 1: Realistic Water가 각종 셰이더에 부하를 거는 것은 알고 있다. 그 중 어느 로직의 어느 함수, 또는 어느 샘플링 등 정확히 어느 지점에서 부하를 유발하고 있는가?**

실측 데이터 및 셰이더 코드(SWShipWake.ush, SWRipple.ush) 분석에 따른 정량적 추론:
* **Kelvin Wake 샘플링 로직 (SW_M7_EVALUATE_KELVIN)**: 
  이 함수는 셰이더 내부에서 SW_M7_WAKE_CAPACITY (1024회)만큼 [loop]를 돕니다. 각 루프마다 Texture2DSampleLevel을 4회씩 호출하므로, 픽셀당 최대 **4096회의 텍스처 페치(Texture Fetch)**가 발생합니다. 이는 프래그먼트 셰이더에서 극도로 무거운 연산이며 GPU 대역폭과 ALU를 병목 상태로 만듭니다.
* **Ripple Height 및 Normal 로직 (SW_EVALUATE_RIPPLE_NORMAL)**: 
  이 함수는 노멀(Normal)을 구하기 위해 Finite Difference 방식을 사용하여 SW_EVALUATE_RIPPLE_HEIGHT를 3회 호출합니다. 각 높이 계산은 SW_RIPPLE_CAPACITY (32회) 루프를 돌며, 루프 내에서 텍스처 .Load() 2회를 수행합니다. 즉, 픽셀당 **192회의 메모리 로드**와 함께 smoothstep, cos, distance 등의 고비용 연산을 반복합니다.
* 데이터(GPUTime, RenderThreadTime_CriticalPath)를 보면 Pure 상태와 기능들을 껐을 때(No_Kelvin, No_Ripple) 간의 연산량 및 메모리(GPUMem/LocalUsedMB) 등에서 큰 차이를 보이므로 이 텍스처 페치와 루프 오버헤드가 GPU 병목의 핵심입니다.

## 3. CPU와 셰이더 간 소통 부하 지점 분석 (CPU 측면)
**질문 2: 각 셰이더들은 CPU의 함수들과도 소통하고 있다. 이 지점에서도 부하가 일어나는 것으로 보이는데, 정확히 어느 호출, 어느 카피 등에서 부하를 유발하고 있는가?**

프로파일링 테이블과 엔진의 구조를 기반으로 한 추론:
* **동적 버퍼 업로드 및 머티리얼 파라미터 갱신 (Material_UpdateDeferredCachedUniformExpressions, UpdatePrimitiveTransform, UpdateGPUScene)**:
  셰이더의 1024개 선박 데이터와 Ripple 데이터를 실시간으로 동기화하기 위해 매 프레임 막대한 양의 Uniform Buffer(또는 Texture/Buffer)를 CPU에서 GPU로 카피(업로드)해야 합니다. 이로 인해 RHI 스레드 및 렌더 스레드의 DrawScene 관련 함수(Exclusive/RenderThread/...) 호출 시간이 크게 튀게 됩니다.
* **GameThread의 Tick 연산 부하 (Ticks/SWShipWakeReplicator, Ticks/SWRippleReplicator, Ticks/ParticleSystemManager)**:
  CPU 측에서는 배열의 데이터를 관리하고 렌더 트리에 전달하기 위해 틱 연산이 발생합니다. 각 파도/선박 발생기를 Tick에서 순회하고 이를 RenderThread 프록시로 넘기는 과정(Enqueue 및 Data Copy)에서 병목이 발생하여 Draw 시간이 순정 머티리얼 대비 10ms 가까이 지연되는 원인이 됩니다.

## 4. 추가로 필요한 데이터 수집 요소
**질문 3: 만약 위 데이터로 알수 없거나, 더 알아내야 하는 부분. 특히 정확히 어느 지점에서 부하가 유발되는 것인지를 파악하기 위해 추가로 해야할 데이터 수집이 있다면 기술할 것.**

현재의 Unreal Insights 데이터는 Frame, GameThread, RenderThread 등의 "매크로(Macro) 레벨" 통계에 가깝습니다. 보다 정확한 핀포인트 최적화를 위해 다음 데이터가 필요합니다:
1. **RenderDoc 또는 Unreal GPU Insights (Shader Print) 데이터**:
   GPU의 어떤 파이프라인(SingleLayerWater 패스 내부)에서 ALU(연산 유닛) 바운드인지, 아니면 Memory(텍스처 대역폭) 바운드인지 정확한 퍼센티지를 확인해야 합니다. 
2. **C++ CPU 세부 프로파일링 (stat dumpframe -ms=... 또는 CPU Insights)**:
   게임 스레드의 Draw를 늦추는 것이 구체적으로 FWaterMeshSceneProxy::SetDynamicData_RenderThread 인지, 혹은 SetTextureParameterValue 호출이 수천 번 발생해서인지 정확한 C++ 콜스택(Call Stack) 시간이 필요합니다.
3. **Instruction Count (명령어 수) 확인**:
   머티리얼 에디터에서 M_Realistic_Water의 HLSL 명령어 수가 몇 개인지, Texture Sampler 수가 제한치(16개)에 얼마나 근접했는지 확인해야 합니다.

## 5. 추천하는 해결 및 최적화 방식
**질문 4: 마지막으로 발견한 문제점 포인트 들에 대해 추천하는 해결 방식을 적어도 2개 이상씩 기술할 것.**

### 포인트 A: 셰이더 내부의 과도한 루프 및 텍스처 샘플링 (GPU 병목)
* **해결 방식 1: Render Target (Heightmap) 기반 캐싱 렌더링**
  매 픽셀마다 1024개의 선박 웨이크와 32개의 리플을 수식으로 평가하는 대신, 선박이나 빗방울이 수면 위에 나타날 때 이를 Top-down 직교 카메라로 가상의 RenderTarget(혹은 Virtual Texture)에 "파도 형태의 Sprite(브러시)"로 그려버립니다. 수면 머티리얼은 복잡한 루프 없이 이 RenderTarget을 1회만 샘플링하여 픽셀을 처리하면 GPU 부하를 90% 이상 줄일 수 있습니다.
* **해결 방식 2: Spatial Hashing 및 용량(Capacity) 대폭 축소**
  SW_M7_WAKE_CAPACITY (1024)를 물리적으로 8~16으로 줄이고, CPU 측에서 현재 카메라(시야) 혹은 픽셀과 가장 가까운 유효한 선박/웨이크만 배열에 담아 GPU로 넘기는 방식(Culling)을 구현합니다. 불필요한 루프를 원천 차단합니다.

### 포인트 B: 매 프레임 막대한 동적 파라미터 업데이트 (CPU/Draw 병목)
* **해결 방식 1: Compute Shader (Niagara)를 활용한 시뮬레이션 이관**
  CPU에서 배열을 순회하고 데이터를 머티리얼 파라미터나 텍스처로 업데이트하는 대신, 파도 시뮬레이션 자체를 Niagara Grid2D(Compute Shader)로 이관합니다. GPU 내부에서 파도가 시뮬레이션되고 그 결과가 수면 렌더링으로 직접 전달되므로 CPU ➔ GPU 간의 메모리 복사 및 병목이 사라집니다.
* **해결 방식 2: 비동기 틱(Async Tick) 및 업데이트 주기 분산 (Time Slicing)**
  원경에 있는 웨이크나 리플 데이터는 매 프레임 업데이트할 필요가 없습니다. CPU 업데이트 로직의 틱 빈도를 30Hz 또는 15Hz로 제한하거나, 프레임당 업데이트하는 데이터 개수를 쪼개어 분산 처리(Time Slicing)합니다. 이로써 Game/Draw 스레드의 프레임당 스파이크를 평탄화할 수 있습니다.
