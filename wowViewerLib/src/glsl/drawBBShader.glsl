#ifdef COMPILING_VS
/* vertex shader code */
layout(position = 0) in vec3 aPosition;

layout(std140) uniform sceneWideBlockVSPS {
    mat4 uLookAtMat;
    mat4 uPMatrix;
};

// Whole model
#ifndef INSTANCED
layout(std140) uniform modelWideBlockVS {
    mat4 uPlacementMat;

    vec4 uBBScale;
    vec4 uBBCenter;
    vec4 uColor;
};

void main() {
    vec4 worldPoint = vec4(
        aPosition.x*uBBScale.x + uBBCenter.x,
        aPosition.y*uBBScale.y + uBBCenter.y,
        aPosition.z*uBBScale.z + uBBCenter.z,
    1);

    gl_Position = uPMatrix * uLookAtMat * uPlacementMat * worldPoint;
}
#endif //COMPILING_VS

#ifdef COMPILING_FS
precision highp float;

layout(std140) uniform modelWideBlockVS {
    mat4 uPlacementMat;

    vec4 uBBScale;
    vec4 uBBCenter;
    vec4 uColor;
};

void main() {
    vec4 finalColor = vec4(uColor, 1.0);

    finalColor.a = 1.0; //do I really need it now?
    gl_FragColor = finalColor;
}

#endif //COMPILING_FS
