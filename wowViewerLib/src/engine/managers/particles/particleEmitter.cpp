//
// Created by deamon on 14.12.17.
//

#include "particleEmitter.h"
#include "../../../gapi/interface/meshes/IParticleMesh.h"
#include "../../algorithms/mathHelper.h"
#include "../../algorithms/animate.h"
#include "../../shader/ShaderDefinitions.h"
#include "generators/CParticleGenerator.h"
#include "generators/CSphereGenerator.h"
#include "generators/CPlaneGenerator.h"
#include "../../../gapi/interface/IDevice.h"
#include "../../persistance/header/M2FileHeader.h"


static GBufferBinding staticM2ParticleBindings[5] = {
    {+m2ParticleShader::Attribute::aPosition, 3, GBindingType::GFLOAT, false, 13*4, 0 },
    {+m2ParticleShader::Attribute::aColor, 4, GBindingType::GFLOAT, false, 13*4, 12},
    {+m2ParticleShader::Attribute::aTexcoord0, 2, GBindingType::GFLOAT, false, 13*4, 28},
    {+m2ParticleShader::Attribute::aTexcoord1, 2, GBindingType::GFLOAT, false, 13*4, 36},
    {+m2ParticleShader::Attribute::aTexcoord2, 2, GBindingType::GFLOAT, false, 13*4, 44},
};


enum class ParticleVertexShader : int {
    None = -1,
    Particle_Color_T2 = 0,
    Particle_CDiffuse_T2 = 1,
    Particle_Color_T4 = 2,
    Particle_Color_T5 = 3
};

enum class ParticlePixelShader : int {
    None = -1,
    Particle_Mod = 0,
    Particle_2ColorTex_3AlphaTex = 1,
    Particle_3ColorTex_3AlphaTex = 2,
    Particle_3ColorTex_3AlphaTex_UV = 3,
    Refraction = 4,

};

inline constexpr const int operator+(ParticlePixelShader const val) { return static_cast<const int>(val); };

inline constexpr const int operator+(ParticleVertexShader const val) { return static_cast<const int>(val); };

const int MAX_PARTICLE_SHADERS = 6;
static const struct {
    int vertexShader;
    int pixelShader;
} particleMaterialShader[MAX_PARTICLE_SHADERS] = {
    //Particle_Unlit_T1 = 0
    {
        +ParticleVertexShader::Particle_Color_T2,
        +ParticlePixelShader::Particle_Mod,
    },
    //Particle_Lit_T1 = 1
    {
        +ParticleVertexShader::Particle_CDiffuse_T2,
        +ParticlePixelShader::Particle_Mod,
    },
    //Particle_Unlit_2ColorTex_3AlphaTex = 2
    {
        +ParticleVertexShader::Particle_Color_T4,
        +ParticlePixelShader::Particle_2ColorTex_3AlphaTex,
    },
    //Particle_Unlit_3ColorTex_3AlphaTex = 3
    {
        +ParticleVertexShader::Particle_Color_T4,
        +ParticlePixelShader::Particle_3ColorTex_3AlphaTex,
    },
    //Particle_Unlit_3ColorTex_3AlphaTex_UV = 4
    {
        +ParticleVertexShader::Particle_Color_T5,
        +ParticlePixelShader::Particle_3ColorTex_3AlphaTex_UV,
    },
    //Particle_Refraction = 5
    {
        +ParticleVertexShader::Particle_Color_T4,
        +ParticlePixelShader::Refraction,
    }
};


ParticleEmitter::ParticleEmitter(IWoWInnerApi *api, M2Particle *particle, M2Object *m2Object) : m_seed(rand()), m_api(api), m2Object(m2Object) {

    if (!randTableInited) {
        for (int i = 0; i < 128; i++) {
            RandTable[i] = (float)std::rand() / (float)RAND_MAX;
        }
        randTableInited = true;
    }

    m_data = particle;

    switch (m_data->old.emitterType) {
        case 1:
            this->generator = new CPlaneGenerator(this->m_seed, particle);
            break;
        case 2:
            this->generator = new CSphereGenerator(this->m_seed, particle, 0 != (m_data->old.flags & 0x100));
            break;
        default:
            this->generator = nullptr;
            std::cout << "Found unimplemented generator " << m_data->old.emitterType << std::flush;
            break;
    }

//        std::cout << "particleId = " << m_data->old.particleId
//                  << "Mentioned models :" << std::endl
//                  << "geometry_model_filename = " << m_data->old.geometry_model_filename.toString() << std::endl
//                  << "recursion_model_filename = " << m_data->old.recursion_model_filename.toString()
//                  << std::endl << std::endl << std::flush;

    const float followDen = m_data->old.followSpeed2 - m_data->old.followSpeed1;
    if (!feq(followDen, 0)) {
        this->followMult = (m_data->old.followScale2 - m_data->old.followScale1) / followDen;
        this->followBase = m_data->old.followScale1 - m_data->old.followSpeed1 * this->followMult;
    }
    else {
        this->followMult = 0;
        this->followBase = 0;
    }

    uint16_t cols = m_data->old.textureDimensions_columns;
    if (cols <= 0) {
        cols = 1;
    }
    uint16_t rows = m_data->old.textureDimensions_rows;
    if (rows <= 0) {
        rows = 1;
    }
    this->textureIndexMask = cols * rows - 1;
    this->m_randomizedTextureIndexMask = 0;

    int colsVal = cols;
    int colBitsCount = -1;
    do
    {
        ++colBitsCount;
        colsVal >>= 1;
    }
    while ( colsVal );

    this->textureColBits = colBitsCount;
    this->textureColMask = cols - 1;

    //RandomizedParticles
    this->m_randomizedTextureIndexMask = 0;
    if ((m_data->old.flags & 0x8000) > 0) {
        uint64_t defCellLong = (textureIndexMask + 1) * (uint64_t)this->m_seed.uint32t();
        this->m_randomizedTextureIndexMask = static_cast<uint16_t>(defCellLong>> 32);
    }
    this->texScaleX = 1.0f / cols;
    this->texScaleY = 1.0f / rows;

    if ((m_data->old.flags & 0x10100000) > 0) {
        const bool isMultitex = (0 != (1 & (m_data->old.flags >> 0x1c)));
        if (isMultitex) {
            this->particleType = 2;
        }
        else {
            this->particleType = 3;
        }
    } else {
        this->particleType = 0;
    }

    createMesh();
}
PACK(
struct meshParticleWideBlockPS {
    float uAlphaTest;
    float padding[3]; // according to std140
    int uPixelShader;
    float padding2[3];
});

EGxBlendEnum PaticleBlendingModeToEGxBlendEnum1 [14] =
    {
        EGxBlendEnum::GxBlend_Opaque,
        EGxBlendEnum::GxBlend_AlphaKey,
        EGxBlendEnum::GxBlend_Alpha,
        EGxBlendEnum::GxBlend_NoAlphaAdd,
        EGxBlendEnum::GxBlend_Add,
        EGxBlendEnum::GxBlend_Mod,
        EGxBlendEnum::GxBlend_Mod2x,
        EGxBlendEnum::GxBlend_BlendAdd
    };

extern EGxBlendEnum M2BlendingModeToEGxBlendEnum [8];
void ParticleEmitter::createMesh() {
    IDevice *device = m_api->getDevice();

    //Create Buffers
    for (int i = 0; i < 4; i++) {
        frame[i].m_indexVBO = device->createIndexBuffer();
        frame[i].m_bufferVBO = device->createVertexBuffer();

        frame[i].m_bindings = device->createVertexBufferBindings();
        frame[i].m_bindings->setIndexBuffer(frame[i].m_indexVBO);

        GVertexBufferBinding vertexBinding;
        vertexBinding.vertexBuffer = frame[i].m_bufferVBO;
        vertexBinding.bindings = std::vector<GBufferBinding>(&staticM2ParticleBindings[0],
                                                             &staticM2ParticleBindings[5]);

        frame[i].m_bindings->addVertexBufferBinding(vertexBinding);
        frame[i].m_bindings->save();


        //Get shader
        HGShaderPermutation shaderPermutation = m_api->getDevice()->getShader("m2ParticleShader", nullptr);

        //Create mesh
        gMeshTemplate meshTemplate(frame[i].m_bindings, shaderPermutation);


        uint8_t blendMode = m_data->old.blendingType;

        meshTemplate.depthWrite = blendMode <= 1;
        meshTemplate.depthCulling = true;
        meshTemplate.backFaceCulling = false;

        if (blendMode == 4)
            blendMode = 3;

    meshTemplate.blendMode = M2BlendingModeToEGxBlendEnum[blendMode];
//        meshTemplate.blendMode = static_cast<EGxBlendEnum>(blendMode);//M2BlendingModeToEGxBlendEnum1[blendMode];

        meshTemplate.start = 0;
        meshTemplate.end = 0;
        meshTemplate.element =  DrawElementMode::TRIANGLES;

        bool multitex = this->particleType >= 2;
        HBlpTexture tex0 = nullptr;
        if (multitex) {
            tex0 = m2Object->getBlpTextureData(this->m_data->old.texture_0);
        } else {
            tex0 = m2Object->getBlpTextureData(this->m_data->old.texture);
        }
        meshTemplate.texture[0] = m_api->getDevice()->createBlpTexture(tex0, true, true);
        if (multitex) {
            HBlpTexture tex1 = m2Object->getBlpTextureData(this->m_data->old.texture_1);
            HBlpTexture tex2 = m2Object->getBlpTextureData(this->m_data->old.texture_2);

            meshTemplate.texture[1] = m_api->getDevice()->createBlpTexture(tex1, true, true);
            meshTemplate.texture[2] = m_api->getDevice()->createBlpTexture(tex2, true, true);
        }

        meshTemplate.textureCount = (multitex) ? 3 : 1;

        meshTemplate.vertexBuffers[0] = m_api->getSceneWideUniformBuffer();
        meshTemplate.vertexBuffers[1] = nullptr;
        meshTemplate.vertexBuffers[2] = nullptr;

        meshTemplate.fragmentBuffers[0] = m_api->getSceneWideUniformBuffer();
        meshTemplate.fragmentBuffers[1] = nullptr;
        meshTemplate.fragmentBuffers[2] = m_api->getDevice()->createUniformBuffer(sizeof(meshParticleWideBlockPS));

        meshParticleWideBlockPS &blockPS = meshTemplate.fragmentBuffers[2]->getObject<meshParticleWideBlockPS>();
        if (blendMode == 0) {
            blockPS.uAlphaTest = -1.0f;
        } else if (blendMode == 1) {
            blockPS.uAlphaTest = 0.501960814f;
        } else {
            blockPS.uAlphaTest = 0.0039215689f;
        }

        int uPixelShader = particleMaterialShader[this->particleType].pixelShader;

        blockPS.uPixelShader = uPixelShader;

        meshTemplate.fragmentBuffers[2]->save(true);

        frame[i].m_mesh = m_api->getDevice()->createParticleMesh(meshTemplate);
    }
}


bool ParticleEmitter::randTableInited = false;
float ParticleEmitter::RandTable[128] = {};

const mathfu::mat4 strangeMat = {
    1.0f, 0, 0, 0,
    0, 1.0f, 0, 0,
    0, 0, 1.0f, 0,
    0, 0, -1.0f, 0.0
};

void ParticleEmitter::calculateQuadToViewEtc(mathfu::mat4 *a1, mathfu::mat4 &translatedViewMat) {
    if ((this->m_data->old.flags & 0x10)) {
        s_particleToView = translatedViewMat * m_emitterModelMatrix ;
    } else {
       if (a1 != nullptr) {
           s_particleToView = translatedViewMat * (*a1);
       } else {
           s_particleToView = translatedViewMat;
       }
    }

    zUpVector = translatedViewMat.GetColumn(3);
    if (this->m_data->old.flags & 0x1000) {
        p_quadToView = mathfu::mat4::ToRotationMatrix(s_particleToView);

        if ((this->m_data->old.flags & 0x10) && (abs(m_inheritedScale) > 0.000000f) ) {
            p_quadToView = p_quadToView * (1.0f / m_inheritedScale);
        }

        mathfu::vec3 v28(0,0,1.0);
        p_quadToViewZVector = p_quadToView.GetColumn(2).xyz();
        if (p_quadToViewZVector.LengthSquared() <= 0.00000023841858f) {
            p_quadToViewZVector =  mathfu::vec3(0,0,1.0);
        } else {
            p_quadToViewZVector = p_quadToViewZVector.Normalized();
        }
    }
}

void ParticleEmitter::UpdateXform(const mathfu::mat4 &viewModelBoneMatrix, mathfu::vec3 &invMatTranslation, mathfu::mat4 *frameOfReference) {
    m_invMatTranslationVector = invMatTranslation;

    if (frameOfReference == nullptr || (this->m_data->old.flags & 0x10)) {
        m_emitterModelMatrix = viewModelBoneMatrix;
    } else {
        m_emitterModelMatrix = frameOfReference->Inverse() * viewModelBoneMatrix;
    }

    this->m_inheritedScale = viewModelBoneMatrix.GetColumn(0).xyz().Length();
}
void ParticleEmitter::InternalUpdate(animTime_t delta) {
    delta = fmaxf(delta, 0.0f);
    if (delta < 0.1) {
        m_deltaPosition = m_deltaPosition1;
    } else {

        float temp0 = floorf(delta / 0.1f);
        delta = (temp0 * -0.1f) + delta;

        int numberOfStepUpdates = fminf(floorf(generator->getAniProp()->lifespan / 0.1f), temp0);

        int temp3 = numberOfStepUpdates+1;
        float divider = 1.0f;
        if (temp3 < 0) {
            divider = (float)((temp3 & 1) | (temp3 >> 1)) + (float)((temp3 & 1) | (temp3 >> 1));
        } else {
            divider = temp3;
        }

        m_deltaPosition = m_deltaPosition1 * (1.0f / divider);

        for (int i = 0; i < numberOfStepUpdates; i++) {
            this->StepUpdate(0.1f);
        }
    }

    this->StepUpdate(delta);
}
void ParticleEmitter::Update(animTime_t delta, mathfu::mat4 &transformMat, mathfu::vec3 invMatTransl, mathfu::mat4 *frameOfReference, mathfu::mat4 &viewMatrix) {
    if (getGenerator() == nullptr) return;

//    if (this->particles.size() <= 0 && !isEnabled) return;

    m_prevPosition = m_emitterModelMatrix.TranslationVector3D();
    m_currentBonePos = (viewMatrix * mathfu::vec4(transformMat.GetColumn(3).xyz(), 1.0f)).z;

    mathfu::vec3 viewMatVec = viewMatrix.GetColumn(3).xyz();
    this->UpdateXform(transformMat, viewMatVec, frameOfReference);

    if (delta > 0) {
        if (this->m_data->old.flags & 0x4000) {
            m_deltaPosition1 = m_emitterModelMatrix.GetColumn(3).xyz() - m_prevPosition;
            float x = this->followMult * (m_deltaPosition1.Length() / delta) + this->followBase;
            if (x >= 0.0)
                x = fmin(x, 1.0f);

            this->m_deltaPosition =  m_deltaPosition1 * x;
        }

        if (this->m_data->old.flags & 0x40) {
            this->burstTime += delta;
            animTime_t frameTime = 30.0 / 1000.0;
            if (this->burstTime > frameTime) {
                this->burstTime = 0;

                if (this->particles.size() == 0) {
                    animTime_t frameAmount = frameTime / this->burstTime;
                    mathfu::vec3 dPos = m_emitterModelMatrix.GetColumn(3).xyz() - m_prevPosition;

                    this->burstVec = dPos * mathfu::vec3(frameAmount * this->m_data->old.BurstMultiplier);
                }
                else {
                    this->burstVec = mathfu::vec3(0, 0, 0);
                }
            }
        }
        this->InternalUpdate(delta);
    }
}
//void ParticleEmitter::Sync() {
//
//}
void ParticleEmitter::StepUpdate(animTime_t delta) {
    ParticleForces forces;

    if (delta < 0.0) return;
    if (m_data->old.flags_per_number.hex_8000 == 0) {
        //this->Sync(); // basically does buffer resize. Thus is not needed
    }

    this->CalculateForces(forces, delta);
    this->EmitNewParticles(delta);
    int i = 0;
    while (i < this->particles.size()) {
        auto &p = this->particles[i];
        p.age = p.age + delta;

        if (p.age > (fmaxf(this->generator->GetLifeSpan(p.seed), 0.001f))) {
            this->KillParticle(i);
            i--;
        } else {
            if (!this->UpdateParticle(p, delta, forces)) {
                this->KillParticle(i);
                i--;
            }
        }
        i++;
    }
}
void ParticleEmitter::EmitNewParticles(animTime_t delta) {
    if (!isEnabled) return;

    float rate = this->generator->GetEmissionRate();
    this->emission += delta * rate;
    while (this->emission > 1) {
        this->CreateParticle(delta);
        this->emission -= 1;
    }
}
void ParticleEmitter::CreateParticle(animTime_t delta) {
    CParticle2 &p = this->BirthParticle();

    this->generator->CreateParticle(p, delta);

    if (!(this->m_data->old.flags & 0x10)) {
        p.position = (this->m_emitterModelMatrix * mathfu::vec4(p.position, 1.0f)).xyz();
        p.velocity = (this->m_emitterModelMatrix * mathfu::vec4(p.velocity, 0.0f)).xyz();
        if (this->m_data->old.flags & 0x2000) {
            // Snap to ground; set z to 0:
            p.position.z = 0.0;
        }
    }
    if (this->m_data->old.flags & 0x40) {
        float speedMul = 1.0f + this->generator->getAniProp()->speedVariation * this->m_seed.Uniform();
        mathfu::vec3 r0 = this->burstVec * speedMul;
        p.velocity += r0;
    }
    if (this->particleType >= 2) {
        for (int i = 0; i < 2; i++) {
            p.texPos[i].x = this->m_seed.UniformPos();
            p.texPos[i].y = this->m_seed.UniformPos();

            mathfu::vec2 v2 = MathHelper::convertV69ToV2(this->m_data->multiTextureParam1[i]) * this->m_seed.Uniform();
            p.texVel[i] = v2  + MathHelper::convertV69ToV2(this->m_data->multiTextureParam0[i]);
        }
    }
}


void ParticleEmitter:: CalculateForces(ParticleForces &forces, animTime_t delta) {
    forces.drift = mathfu::vec3(this->m_data->old.WindVector) * mathfu::vec3(delta);


    auto g = this->generator->GetGravity();
    forces.velocity = g * mathfu::vec3(delta);
    forces.position = g * mathfu::vec3(delta * delta * 0.5f);
    forces.drag = this->m_data->old.drag * delta;
}

bool ParticleEmitter::UpdateParticle(CParticle2 &p, animTime_t delta, ParticleForces &forces) {

    if (this->particleType >= 2) {
        for (int i = 0; i < 2; i++) {
            // s = frac(s + v * dt)
            float val = (float) (p.texPos[i].x + delta * p.texVel[i].x);
            p.texPos[i].x = (float) (val - floorf(val));

            val = (float) (p.texPos[i].y + delta * p.texVel[i].y);
            p.texPos[i].y = (float) (val - floorf(val));
        }
    }

    p.velocity = p.velocity + forces.drift;


    //MoveParticle
    if ((this->m_data->old.flags & 0x4000) && (2 * delta < p.age)) {
        p.position = p.position + this->m_deltaPosition;
    }

    mathfu::vec3 r0 = p.velocity * mathfu::vec3(delta,delta,delta); // v*dt

    p.velocity = p.velocity + forces.velocity;
    p.velocity = p.velocity *  (1.0f - forces.drag);

    p.position = p.position + r0 + forces.position;
//    p.position = p.position + forces.position;

    if (this->m_data->old.emitterType == 2 && (this->m_data->old.flags & 0x80)) {
        mathfu::vec3 r1 = p.position;
        if ((this->m_data->old.flags & 0x10)) {
            if (mathfu::vec3::DotProduct(r1, r0) > 0) {
                return false;
            }
        } else {
            r1 = p.position - this->m_emitterModelMatrix.GetColumn(3).xyz();
            if (mathfu::vec3::DotProduct(r1, r0) > 0) {
                return false;
            }
        }
    }

    return true;
}

void ParticleEmitter::KillParticle(int index) {
    particles.erase(particles.begin()+index);
}
CParticle2& ParticleEmitter::BirthParticle() {
    CParticle2 p1;
    particles.push_back(p1);
    

    CParticle2 &p = particles[particles.size() - 1];
    return p;
}

void ParticleEmitter::prepearBuffers(mathfu::mat4 &viewMatrix) {
    szVertexBuf = std::vector<ParticleBuffStructQuad>(0);
    szIndexBuff = std::vector<uint16_t>(0);

    if (particles.size() == 0 && this->generator != nullptr) {
        return;
    }

    inverseViewMatrix = viewMatrix.Inverse();

    mathfu::mat3 rotation = mathfu::mat3::ToRotationMatrix(viewMatrix);
    mathfu::mat4 someMat = mathfu::mat4(
        rotation[0],rotation[1],rotation[2], 0.0,
        rotation[3],rotation[4],rotation[5], 0.0,
        rotation[6],rotation[7],rotation[8], 0.0,
        0, 0, 0, 1.0
    );
    this->calculateQuadToViewEtc(nullptr, viewMatrix); // FrameOfRerefence mat is null since it's not used

    // Mat34.Col(this.particleNormal, viewMatrix, 2);
    // Build vertices for each particle

    // Vertex format: {float3 pos; float3 norm; float4 col; float2 texcoord} = 12
    // Multitex format: {float3 pos; float4 col; float2 texcoord[3]} = 13

    szVertexBuf = std::vector<ParticleBuffStructQuad>(0);
    szIndexBuff = std::vector<uint16_t>(0);
    int vo = 0;
    for (int i = 0; i < particles.size(); i++) {
        CParticle2 &p = this->particles[i];
        ParticlePreRenderData particlePreRenderData;
        if (this->CalculateParticlePreRenderData(p, particlePreRenderData)) {
            if (m_data->old.flags & 0x20000) {
                this->buildVertex1(p, particlePreRenderData);
                szIndexBuff.push_back(vo + 0);
                szIndexBuff.push_back(vo + 1);
                szIndexBuff.push_back(vo + 2);
                szIndexBuff.push_back(vo + 3);
                szIndexBuff.push_back(vo + 2);
                szIndexBuff.push_back(vo + 1);
                vo += 4;
            }
            if ( m_data->old.flags & 0x40000 ) {
                this->buildVertex2(p, particlePreRenderData);
                szIndexBuff.push_back(vo + 0);
                szIndexBuff.push_back(vo + 1);
                szIndexBuff.push_back(vo + 2);
                szIndexBuff.push_back(vo + 3);
                szIndexBuff.push_back(vo + 2);
                szIndexBuff.push_back(vo + 1);
                vo += 4;
            }
        }
    }

//    std::sort(szVertexBuf.begin(), szVertexBuf.end(), [](const ParticleBuffStructQuad &a, const ParticleBuffStructQuad &b) -> bool {
//        return
//        fminf(
//            fminf(
//                fminf(
//                    a.particle[0].position.z,
//                    a.particle[1].position.z
//                ),
//                a.particle[2].position.z
//            ),
//            a.particle[0].position.z
//        ) < fminf(
//            fminf(
//                fminf(
//                    b.particle[0].position.z,
//                    b.particle[1].position.z
//                ),
//                b.particle[2].position.z
//            ),
//            b.particle[0].position.z
//        );
//    });
}

int ParticleEmitter::buildVertex1(CParticle2 &p, ParticlePreRenderData &particlePreRenderData) {
    mathfu::vec2 texScaleVec(
        (particlePreRenderData.m_ageDependentValues.timedHeadCell &this->textureColMask) * this->texScaleX,
        (particlePreRenderData.m_ageDependentValues.timedHeadCell  >> this->textureColBits) * this->texScaleY);

    float baseSpin = 0;
    float deltaSpin = 0;
    //GetSpin
    this->GetSpin(p, baseSpin, deltaSpin);

    int v20 = 0;
    mathfu::vec3 m0 (0,0,0);
    mathfu::vec3 m1 (0,0,0);
    bool force0x1000 = false;
    bool dataFilled = false;
    if (m_data->old.flags & 0x4) {
        if (p.velocity.LengthSquared() > 0.00000023841858) {
            v20 = 1;
            if (!(m_data->old.flags & 0x1000)) {
                mathfu::vec3 viewSpaceVel = (s_particleToView * mathfu::vec4(-p.velocity, 0.0)).xyz();

                float v30 = 0.0;
                float translatedVelLeng = viewSpaceVel.LengthSquared();
                if (translatedVelLeng <= 0.00000023841858) {
                    v30 = 0.0;
                } else {
                    v30 = 1.0f / sqrtf(translatedVelLeng);
                }

                mathfu::vec3 viewSpaceVelNorm = viewSpaceVel * v30;

                m0 = viewSpaceVelNorm *  particlePreRenderData.m_ageDependentValues.m_particleScale.x;

                m1 = mathfu::vec3(
                    //197
                    viewSpaceVelNorm.y * particlePreRenderData.m_ageDependentValues.m_particleScale.y,
                    //199
                    -(viewSpaceVelNorm.x * particlePreRenderData.m_ageDependentValues.m_particleScale.y),
                    //177
                    0
                );
                dataFilled = true;
                force0x1000 = false;
            } else {
                force0x1000 = true;
            }
        }
    }
    if (((m_data->old.flags & 0x1000) || force0x1000) && !dataFilled) {
        mathfu::mat3 transformedQuadToViewMat = p_quadToView;
        float v39 = particlePreRenderData.m_ageDependentValues.m_particleScale.x;
        if (v20){
            float v47 = 0.0;
            mathfu::vec3 minusVel = (-p.velocity);
            float translatedVelLeng = minusVel.LengthSquared();
            if (translatedVelLeng <= 0.00000023841858) {
                v47 = 0.0;
            } else {
                v47 = 1.0f / sqrtf(translatedVelLeng);
            }

            transformedQuadToViewMat = p_quadToView * mathfu::mat3(
                minusVel.x * v47, minusVel.y * v47, 0,
                -minusVel.y * v47, minusVel.x * v47, 0,
                0, 0, 1);

            if (v47 > 0.00000023841858) {
                v39 = particlePreRenderData.m_ageDependentValues.m_particleScale.x *
                    (1.0f / sqrtf(p.velocity.LengthSquared()) / v47);
            }
        }

        if (particleType == 4) {

        }

        m0 = transformedQuadToViewMat.GetColumn(0) * v39;
        m1 = transformedQuadToViewMat.GetColumn(1) * particlePreRenderData.m_ageDependentValues.m_particleScale.y;
        deltaSpin = m1.x;
        dataFilled = true;
        if (!feq(this->m_data->old.Spin, 0) || !feq(this->m_data->old.spinVary, 0)) {
            float theta = baseSpin + deltaSpin * p.age;
            if ((this->m_data->old.flags & 0x200) && (p.seed & 1)) {
                theta = -theta;
            }

            mathfu::vec3 zAxis = p_quadToViewZVector;
            if (particleType == 4) {

            }

            mathfu::mat3 rotMat = mathfu::quat::FromAngleAxis(theta, zAxis).ToMatrix();

            m0 = rotMat * m0;
            m1 = rotMat * mathfu::vec3(deltaSpin, m1.y, m1.z);
        }
    }

    if (!dataFilled) {
        if (!feq(this->m_data->old.Spin, 0) || !feq(this->m_data->old.spinVary, 0)) {
            float theta = baseSpin + deltaSpin * p.age;
            if ((this->m_data->old.flags & 0x200) && (p.seed & 1)) {
                theta = -theta;
            }

            float cosTheta = cosf(theta);
            float sinTheta = sin(theta);

            m0 = mathfu::vec3(
                cosTheta * particlePreRenderData.m_ageDependentValues.m_particleScale.x,
                sinTheta * particlePreRenderData.m_ageDependentValues.m_particleScale.x, 0);
            m1 = mathfu::vec3(
                -sinTheta * particlePreRenderData.m_ageDependentValues.m_particleScale.y,
                cosTheta * particlePreRenderData.m_ageDependentValues.m_particleScale.y, 0);

            if (this->m_data->old.flags & 0x8000000) {
                particlePreRenderData.m_particleCenter += mathfu::vec3(m1.x, m1.y, 0);
            }
        } else {
            m0 = mathfu::vec3(
                particlePreRenderData.m_ageDependentValues.m_particleScale.x,
                0,
                0);
            m1 = mathfu::vec3(
                0,
                particlePreRenderData.m_ageDependentValues.m_particleScale.y,
                0);
        }
    }

    this->BuildQuadT3(szVertexBuf, m0, m1,
        particlePreRenderData.m_particleCenter,
            particlePreRenderData.m_ageDependentValues.m_timedColor,
            particlePreRenderData.m_ageDependentValues.m_alpha,
            texScaleVec.x, texScaleVec.y,  p.texPos);



    return 0;
}

int ParticleEmitter::buildVertex2(CParticle2 &p, ParticlePreRenderData &particlePreRenderData) {
    mathfu::vec2 texScaleVec(
        (particlePreRenderData.m_ageDependentValues.timedTailCell &  this->textureColMask) * this->texScaleX,
        (particlePreRenderData.m_ageDependentValues.timedTailCell >> this->textureColBits) * this->texScaleY);

    // tail cell
    mathfu::vec3 m0 = mathfu::vec3(0, 0, 0);
    mathfu::vec3 m1 = mathfu::vec3(0, 0, 0);
    // as above, scale and rotation align to particle velocity
    float trailTime = this->m_data->old.tailLength;
    if ((this->m_data->old.flags & 0x400)) {
        trailTime = fminf(p.age, trailTime);
    }
    mathfu::vec3 viewVel = p.velocity * -1.0f;
    viewVel = (this->s_particleToView * mathfu::vec4(viewVel, 0)).xyz() * trailTime;
    mathfu::vec3 screenVel = mathfu::vec3(viewVel.xy(), 0);

    if (mathfu::vec3::DotProduct(screenVel, screenVel) > 0.0001) {
        float invScreenVelMag = 1.0f / screenVel.Length();
        particlePreRenderData.m_ageDependentValues.m_particleScale = particlePreRenderData.m_ageDependentValues.m_particleScale * invScreenVelMag;
        screenVel = mathfu::vec3(mathfu::vec2::HadamardProduct(screenVel.xy(), particlePreRenderData.m_ageDependentValues.m_particleScale), 0);
        m1 = mathfu::vec3(-screenVel.y, screenVel.x, 0);
        m0 = viewVel * 0.5f;
        particlePreRenderData.m_particleCenter += m0;
    }
    else {
        m0 = mathfu::vec3(
            particlePreRenderData.m_ageDependentValues.m_particleScale.x * 0.05f,
            0,
            0);
        m1 = mathfu::vec3(
            0,
            particlePreRenderData.m_ageDependentValues.m_particleScale.y * 0.05f,
            0);
    }

    this->BuildQuadT3(szVertexBuf, m0, m1,
                      particlePreRenderData.m_particleCenter,
                      particlePreRenderData.m_ageDependentValues.m_timedColor,
                      particlePreRenderData.m_ageDependentValues.m_alpha,
                      texScaleVec.x, texScaleVec.y,  p.texPos);


    return 1;
}

void ParticleEmitter::GetSpin(CParticle2 &p, float &baseSpin, float &deltaSpin) const {

    if (!feq(m_data->old.baseSpin, 0.0f) || !feq(m_data->old.spinVary, 0.0f)) {
        CRndSeed rand(p.seed);

        if (feq(m_data->old.baseSpinVary, 0.0f)) {
            baseSpin = m_data->old.baseSpin;
        } else {
            baseSpin = m_data->old.baseSpin + rand.Uniform() * m_data->old.baseSpinVary;
        }

        if (feq(m_data->old.spinVary, 0.0f)) {
            deltaSpin = m_data->old.Spin;
        } else {
            deltaSpin = m_data->old.Spin + rand.Uniform() * m_data->old.spinVary;
        }
    } else {
        baseSpin = m_data->old.baseSpin;
        deltaSpin = m_data->old.Spin;
    }
}


int ParticleEmitter::CalculateParticlePreRenderData(CParticle2 &p, ParticlePreRenderData &particlePreRenderData) {

    float twinkle = this->m_data->old.TwinklePercent;
    auto &twinkleRange = this->m_data->old.twinkleScale;

    float twinkleMin = twinkleRange.min;
    float twinkleVary = twinkleRange.max - twinkleMin;


    int rndIdx = 0;
    uint16_t seed = p.seed;
    animTime_t age = p.age;
    if (twinkle < 1 || !feq(twinkleVary,0)) {
        rndIdx = 0x7f & ((int)(age * this->m_data->old.TwinkleSpeed) + seed);
    }

    if (twinkle < ParticleEmitter::RandTable[rndIdx]) {
        return 0;
    }

    //fillTimedParticleData
    fillTimedParticleData(p, particlePreRenderData, seed);

    //Back
    float weight = twinkleVary * ParticleEmitter::RandTable[rndIdx] + twinkleMin;
    particlePreRenderData.m_ageDependentValues.m_particleScale *= weight;

    if ((this->m_data->old.flags & 0x20)) {
        particlePreRenderData.m_ageDependentValues.m_particleScale *= this->m_inheritedScale;
    }

    particlePreRenderData.m_particleCenter = (s_particleToView * mathfu::vec4(p.position, 1.0)).xyz();
    particlePreRenderData.invertDiagonalLen = 1.0;

    return 1;
}

void ParticleEmitter::fillTimedParticleData(CParticle2 &p,
                                            ParticleEmitter::ParticlePreRenderData &particlePreRenderData,
                                            uint16_t seed) {
    float percentTime = p.age / getGenerator()->GetMaxLifeSpan();
    CRndSeed rand(seed);
    if (fminf(percentTime, 1.0f) <= 0.0f) {
        percentTime = 0.0f;
    } else if (percentTime >= 1.0f) {
        percentTime = 1.0f;
    };

    mathfu::vec3 defaultColor(255.0f, 255.0f, 255.0f);
    mathfu::vec2 defaultScale(1.0f, 1.0f);
    float defaultAlpha = 1.0f;
    uint16_t defaultCell = 1;

    auto &ageDependentValues = particlePreRenderData.m_ageDependentValues;

    ageDependentValues.m_timedColor = animatePartTrack<C3Vector, mathfu::vec3>(percentTime, &m_data->old.colorTrack, defaultColor) / 255.0f;
    ageDependentValues.m_particleScale = animatePartTrack<C2Vector, mathfu::vec2>(percentTime, &m_data->old.scaleTrack, defaultScale);
    ageDependentValues.m_alpha = animatePartTrack<fixed16, float>(percentTime, &m_data->old.alphaTrack, defaultAlpha);

    uint64_t defCellLong = 0;
    if (m_data->old.headCellTrack.timestamps.size > 0) {
        defaultCell = 0;
        ageDependentValues.timedHeadCell = animatePartTrack<uint16_t, uint16_t>(
            percentTime,
            &m_data->old.headCellTrack,
            defaultCell);
        ageDependentValues.timedHeadCell = textureIndexMask & (ageDependentValues.timedHeadCell + m_randomizedTextureIndexMask);
    } else {
        if ((m_data->old.flags & 0x10000)) {
            uint64_t defCellLong = (textureIndexMask + 1) * (uint64_t)rand.uint32t();
            ageDependentValues.timedHeadCell = static_cast<uint16_t>(defCellLong >> 32);
        } else {
            ageDependentValues.timedHeadCell = 0;
        }
    }

    defaultCell = 0;
    ageDependentValues.timedTailCell = animatePartTrack<uint16_t, uint16_t>(percentTime, &m_data->old.tailCellTrack, defaultCell);
    ageDependentValues.timedTailCell = (ageDependentValues.timedTailCell + m_randomizedTextureIndexMask) & textureIndexMask;

    float scaleMultiplier = 1.0f;
    if (m_data->old.flags & 0x80000) {
        scaleMultiplier = fmaxf(1.0f + rand.Uniform() * m_data->old.scaleVary.y, 0.000099999997);
        ageDependentValues.m_particleScale.x =
            fmaxf(1.0f + rand.Uniform() * m_data->old.scaleVary.x, 0.000099999997) *
            ageDependentValues.m_particleScale.x;
    }
    else {
        scaleMultiplier = fmaxf(1.0f + rand.Uniform() * m_data->old.scaleVary.x, 0.000099999997);
        ageDependentValues.m_particleScale.x = scaleMultiplier * ageDependentValues.m_particleScale.x;
    }
    ageDependentValues.m_particleScale.y = scaleMultiplier * ageDependentValues.m_particleScale.y;
}

inline float paramXTransform(uint32_t x) {
    return ((x & 0x1F)/32.0f) + (float)(x >> 5);
}

void
ParticleEmitter::BuildQuadT3(
    std::vector<ParticleBuffStructQuad> &szVertexBuf,
    mathfu::vec3 &m0, mathfu::vec3 &m1,
    mathfu::vec3 &viewPos, mathfu::vec3 &color, float alpha,
    float texStartX, float texStartY, mathfu::vec2 *texPos) {

    static const float vxs[4] = {-1, -1, 1, 1};
    static const float vys[4] = {1, -1, 1, -1};
    static const float txs[4] = {0, 0, 1, 1};
    static const float tys[4] = {0, 1, 0, 1};
    ParticleBuffStructQuad record;

//    mathfu::mat4 inverseLookAt = mathfu::mat4::Identity();
    mathfu::mat4 inverseLookAt = this->inverseViewMatrix;

    for (int i = 0; i < 4; i++) {

        record.particle[i].position = ( inverseLookAt * mathfu::vec4(
                m0 * vxs[i] + m1 * vys[i] + viewPos,
                1.0
        )).xyz();
        record.particle[i].color = mathfu::vec4_packed(mathfu::vec4(color, alpha));

        record.particle[i].textCoord0 =
            mathfu::vec2(txs[i] * this->texScaleX + texStartX,
                         tys[i] * this->texScaleY + texStartY);

        record.particle[i].textCoord1 =
            mathfu::vec2(
                txs[i] * paramXTransform(this->m_data->old.multiTextureParamX[0]) + texPos[0].x,
                tys[i] * paramXTransform(this->m_data->old.multiTextureParamX[0]) + texPos[0].y);
        record.particle[i].textCoord2 =
            mathfu::vec2(
                txs[i] * paramXTransform(this->m_data->old.multiTextureParamX[1]) + texPos[1].x,
                tys[i] * paramXTransform(this->m_data->old.multiTextureParamX[1]) + texPos[1].y);

    }

    szVertexBuf.push_back(record);
}

void ParticleEmitter::collectMeshes(std::vector<HGMesh> &meshes, int renderOrder) {
    auto &currentFrame = frame[m_api->getDevice()->getUpdateFrameNumber()];
    if (!currentFrame.active)
        return;

    HGParticleMesh mesh = frame[m_api->getDevice()->getUpdateFrameNumber()].m_mesh;
    mesh->setRenderOrder(renderOrder);
    meshes.push_back(mesh);
}

void ParticleEmitter::updateBuffers() {
    auto &currentFrame = frame[m_api->getDevice()->getUpdateFrameNumber()];
    currentFrame.active = !szVertexBuf.empty();

    if (!currentFrame.active)
        return;

    currentFrame.m_indexVBO->uploadData((void *) szIndexBuff.data(), (int) (szIndexBuff.size() * sizeof(uint16_t)));
    currentFrame.m_bufferVBO->uploadData((void *) szVertexBuf.data(), (int) (szVertexBuf.size() * sizeof(ParticleBuffStructQuad)));

    currentFrame.m_mesh->setEnd(szIndexBuff.size());
    currentFrame.m_mesh->setSortDistance(m_currentBonePos);
    currentFrame.m_mesh->setPriorityPlane(m_data->old.textureTileRotation);

}
