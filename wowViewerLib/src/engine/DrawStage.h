//
// Created by Deamon on 2/2/2020.
//

#ifndef AWEBWOWVIEWERCPP_DRAWSTAGE_H
#define AWEBWOWVIEWERCPP_DRAWSTAGE_H

struct DrawStage;
struct CameraMatrices;
struct ViewPortDimensions;

typedef std::shared_ptr<DrawStage> HDrawStage;

#include <memory>
#include <mathfu/glsl_mappings.h>
#include "../gapi/interface/IDevice.h"
#include "CameraMatrices.h"


struct ViewPortDimensions{
    std::array<int, 2> mins;
    std::array<int, 2> maxs;
};

typedef std::vector<HGMesh> MeshesToRender;
typedef std::shared_ptr<MeshesToRender> HMeshesToRender;

struct DrawStage {
    HCameraMatrices matricesForRendering;

    HMeshesToRender meshesToRender;
    std::vector<HDrawStage> drawStageDependencies;


    bool setViewPort = false;
    ViewPortDimensions viewPortDimensions;

    bool clearScreen = false;
    mathfu::vec4 clearColor;

    HFrameBuffer target;
};

#endif //AWEBWOWVIEWERCPP_DRAWSTAGE_H
