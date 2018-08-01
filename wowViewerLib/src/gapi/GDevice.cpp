//
// Created by deamon on 05.06.18.
//

#include <iostream>
#include "../engine/opengl/header.h"
#include "GDevice.h"
#include "../engine/algorithms/hashString.h"
#include "shaders/GM2ShaderPermutation.h"
#include "../engine/opengl/header.h"
#include "meshes/GM2Mesh.h"
#include "meshes/GParticleMesh.h"
#include "shaders/GM2ParticleShaderPermutation.h"
#include "shaders/GAdtShaderPermutation.h"
#include "shaders/GWMOShaderPermutation.h"

BlendModeDesc blendModes[(int)EGxBlendEnum::GxBlend_MAX] = {
        /*GxBlend_Opaque*/           {false,GL_ONE,GL_ZERO,GL_ONE,GL_ZERO},
        /*GxBlend_AlphaKey*/         {false,GL_ONE,GL_ZERO,GL_ONE,GL_ZERO},
        /*GxBlend_Alpha*/            {true,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA},
        /*GxBlend_Add*/              {true,GL_SRC_ALPHA,GL_ONE,GL_ZERO,GL_ONE},
        /*GxBlend_Mod*/              {true,GL_DST_COLOR,GL_ZERO,GL_DST_ALPHA,GL_ZERO},
        /*GxBlend_Mod2x*/            {true,GL_DST_COLOR,GL_SRC_COLOR,GL_DST_ALPHA,GL_SRC_ALPHA},
        /*GxBlend_ModAdd*/           {true,GL_DST_COLOR,GL_ONE,GL_DST_ALPHA,GL_ONE},
        /*GxBlend_InvSrcAlphaAdd*/   {true,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_ONE},
        /*GxBlend_InvSrcAlphaOpaque*/{true,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO},
        /*GxBlend_SrcAlphaOpaque*/   {true,GL_SRC_ALPHA,GL_ZERO,GL_SRC_ALPHA,GL_ZERO},
        /*GxBlend_NoAlphaAdd*/       {true,GL_ONE,GL_ONE,GL_ZERO,GL_ONE},
        /*GxBlend_ConstantAlpha*/    {true,GL_CONSTANT_ALPHA,GL_ONE_MINUS_CONSTANT_ALPHA,GL_CONSTANT_ALPHA,GL_ONE_MINUS_CONSTANT_ALPHA},
        /*GxBlend_Screen*/           {true,GL_ONE_MINUS_DST_COLOR,GL_ONE,GL_ONE,GL_ZERO},
        /*GxBlend_BlendAdd*/         {true,GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA}
};

void GDevice::bindIndexBuffer(GIndexBuffer *buffer) {
    if (buffer == nullptr ) {
        if (m_lastBindIndexBuffer != nullptr) {
            m_lastBindIndexBuffer->unbind();
            m_lastBindIndexBuffer = nullptr;
        }
    } else if (buffer != m_lastBindIndexBuffer) {
        buffer->bind();
        m_lastBindIndexBuffer = buffer;
    }
}

void GDevice::bindVertexBuffer(GVertexBuffer *buffer)  {
    if (buffer == nullptr) {
        if (m_lastBindVertexBuffer != nullptr) {
            m_lastBindVertexBuffer->unbind();
            m_lastBindVertexBuffer = nullptr;
        }
    }  else if (buffer != m_lastBindVertexBuffer) {
        buffer->bind();
        m_lastBindVertexBuffer = buffer;
    }
}

void GDevice::bindVertexUniformBuffer(GUniformBuffer *buffer, int slot)  {
    if (buffer == nullptr) {
        if (m_vertexUniformBuffer[slot] != nullptr) {
            m_vertexUniformBuffer[slot]->unbind();
            m_vertexUniformBuffer[slot] = nullptr;
        }
    }  else {
        if (slot == -1) {
            buffer->bind(slot);
        } else if (buffer != m_vertexUniformBuffer[slot]) {
            buffer->bind(slot);

            m_vertexUniformBuffer[slot] = buffer;
        }
    }
}
void GDevice::bindFragmentUniformBuffer(GUniformBuffer *buffer, int slot)  {
    if (buffer == nullptr) {
        if (m_fragmentUniformBuffer[slot] != nullptr) {
            m_fragmentUniformBuffer[slot]->unbind();
            m_fragmentUniformBuffer[slot] = nullptr;
        }
    }  else if (buffer != m_fragmentUniformBuffer[slot]) {
        buffer->bind(3+slot);

        m_fragmentUniformBuffer[slot] = buffer;
    }
}


void GDevice::bindVertexBufferBindings(GVertexBufferBindings *buffer) {
    if (buffer == nullptr) {
       if (m_vertexBufferBindings != nullptr) {
           m_vertexBufferBindings->unbind();
           m_vertexBufferBindings = nullptr;
       }
        m_lastBindIndexBuffer = nullptr;
        m_lastBindVertexBuffer = nullptr;
    } else {
        m_lastBindIndexBuffer = nullptr;
        m_lastBindVertexBuffer = nullptr;

        if (buffer != m_vertexBufferBindings) {
            buffer->bind();
            m_vertexBufferBindings = buffer;
        }
    }
}

std::shared_ptr<GShaderPermutation> GDevice::getShader(std::string shaderName) {
    const char * cstr = shaderName.c_str();
    size_t hash = CalculateFNV(cstr);
    if (m_shaderPermutCache.count(hash) > 0) {

        HGShaderPermutation ptr = m_shaderPermutCache.at(hash);
        return ptr;
    }

    std::shared_ptr<GShaderPermutation> sharedPtr;
    if (shaderName == "m2Shader") {
        sharedPtr.reset( new GM2ShaderPermutation(shaderName, *this));
        m_m2ShaderCreated = true;
    } else if (shaderName == "m2ParticleShader") {
        sharedPtr.reset( new GM2ParticleShaderPermutation(shaderName, *this));
    } else if (shaderName == "wmoShader") {
        sharedPtr.reset( new GWMOShaderPermutation(shaderName, *this));
    } else if (shaderName == "adtShader") {
        sharedPtr.reset( new GAdtShaderPermutation(shaderName, *this));
    } else {
        sharedPtr.reset(new GShaderPermutation(shaderName, *this));
    }

    sharedPtr->compileShader();
    m_shaderPermutCache[hash] = sharedPtr;

    glUniformBlockBinding(sharedPtr->m_programBuffer, sharedPtr->m_uboVertexBlockIndex[0], 0);
    glUniformBlockBinding(sharedPtr->m_programBuffer, sharedPtr->m_uboVertexBlockIndex[1], 1);
    glUniformBlockBinding(sharedPtr->m_programBuffer, sharedPtr->m_uboVertexBlockIndex[2], 2);

    glUniformBlockBinding(sharedPtr->m_programBuffer, sharedPtr->m_uboFragmentBlockIndex[0], 3+0);
    glUniformBlockBinding(sharedPtr->m_programBuffer, sharedPtr->m_uboFragmentBlockIndex[1], 3+1);
    glUniformBlockBinding(sharedPtr->m_programBuffer, sharedPtr->m_uboFragmentBlockIndex[2], 3+2);

    return m_shaderPermutCache[hash];
}

HGUniformBuffer GDevice::createUniformBuffer(size_t size) {
    std::shared_ptr<GUniformBuffer> h_uniformBuffer;
    h_uniformBuffer.reset(new GUniformBuffer(*this, size));

    std::weak_ptr<GUniformBuffer> w_uniformBuffer = h_uniformBuffer;

    m_unfiormBufferCache.push_back(w_uniformBuffer);

    return h_uniformBuffer;
}

void GDevice::drawMeshes(std::vector<HGMesh> &meshes) {
    updateBuffers(meshes);

//    if (m_m2ShaderCreated) exit(0);

    int j = 0;
    for (auto &hgMesh : meshes) {
        this->drawMesh(hgMesh);
        j++;
    }
}

void GDevice::updateBuffers(std::vector<HGMesh> &meshes) {
    //TODO: 1) Figure out how to collect all uniform buffers needed to render current meshes
    //TODO: 2) Create uniform buffers in cycle that would actually hold the data

    //1. Collect buffers
    std::vector<HGUniformBuffer> buffers;
    for (auto &mesh : meshes) {
        for (int i = 0; i < 3; i++ ) {
            HGUniformBuffer buffer = mesh->m_fragmentUniformBuffer[i];
            if (buffer != nullptr)
                buffers.push_back(buffer);
        }
        for (int i = 0; i < 3; i++ ) {
            HGUniformBuffer buffer = mesh->m_vertexUniformBuffer[i];
            if (buffer != nullptr)
                buffers.push_back(buffer);
        }
    }

    std::sort( buffers.begin(), buffers.end() );
    buffers.erase( unique( buffers.begin(), buffers.end() ), buffers.end() );

    //2. Create buffers and update them
    std::vector<char> c;

    int currentSize = 0;
    int buffersIndex = 0;

    HGUniformBuffer bufferForUpload;
    if (buffersIndex >= m_unfiormBuffersForUpload.size()) {
        bufferForUpload = createUniformBuffer(0);
        bufferForUpload->createBuffer();
        m_unfiormBuffersForUpload.push_back(bufferForUpload);
    } else {
        bufferForUpload = m_unfiormBuffersForUpload[buffersIndex];
    }

    for (auto buffer : buffers) {
        if (buffer->m_buffCreated) continue;

        if (currentSize + buffer->m_size > maxUniformBufferSize) {
            bufferForUpload->uploadData(&c[0], currentSize);

            buffersIndex++;
            c.resize(0);
            currentSize = 0;

            if (buffersIndex >= m_unfiormBuffersForUpload.size()) {
                bufferForUpload = createUniformBuffer(0);
                bufferForUpload->createBuffer();
                m_unfiormBuffersForUpload.push_back(bufferForUpload);
            } else {
                bufferForUpload = m_unfiormBuffersForUpload[buffersIndex];
            }
        }

        buffer->pIdentifierBuffer = bufferForUpload->pIdentifierBuffer;
        buffer->m_offset = (size_t) currentSize;
        c.insert(c.end(), (char*)buffer->pContent, ((char*)buffer->pContent)+buffer->m_size);
        currentSize += buffer->m_size;

        int bytesToAdd = uniformBufferOffsetAlign - (currentSize % uniformBufferOffsetAlign);
        for (int j = 0; j < bytesToAdd; j++) {
            c.push_back(0);
        }
        currentSize+=bytesToAdd;
    }

    bufferForUpload->uploadData(&c[0], currentSize);

    buffersIndex++;
    c.resize(0);
    currentSize = 0;

//    std::cout << "m_unfiormBuffersForUpload.size = " << m_unfiormBuffersForUpload.size() << std::endl;

}

void GDevice::drawMesh(HGMesh &hmesh) {
    bindProgram(hmesh->m_shader.get());
    bindVertexBufferBindings(hmesh->m_bindings.get());

    bindVertexUniformBuffer(hmesh->m_vertexUniformBuffer[0].get(), 0);
    bindVertexUniformBuffer(hmesh->m_vertexUniformBuffer[1].get(), 1);
    bindVertexUniformBuffer(hmesh->m_vertexUniformBuffer[2].get(), 2);

    bindFragmentUniformBuffer(hmesh->m_fragmentUniformBuffer[0].get(), 0);
    bindFragmentUniformBuffer(hmesh->m_fragmentUniformBuffer[1].get(), 1);
    bindFragmentUniformBuffer(hmesh->m_fragmentUniformBuffer[2].get(), 2);

    for (int i = 0; i < hmesh->m_textureCount; i++) {
        if (hmesh->m_texture[i] != nullptr && hmesh->m_texture[i]->getIsLoaded()) {
            bindTexture(hmesh->m_texture[i].get(), i);
        } else {
            bindTexture(m_blackPixelTexture.get(), i);
        }
    }

//        GLint current_vao;
//        GLint current_array_buffer;
//        GLint current_element_buffer;
//        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &current_vao);
//        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &current_array_buffer);
//        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &current_element_buffer);
//
//        std::cout << "current_vao = " << current_vao << std::endl <<
//            " current_array_buffer = " << current_array_buffer << std::endl <<
//            " current_element_buffer = " << current_element_buffer << std::endl <<
//            " hmesh->m_bindings->m_buffer = " << *(GLint *) hmesh->m_bindings->m_buffer << std::endl <<
//            " hmesh->m_bindings->m_bindings[0].vertexBuffer->pIdentifierBuffer = "
//                  << *(GLint *) hmesh->m_bindings->m_bindings[0].vertexBuffer->pIdentifierBuffer << std::endl <<
//            " hmesh->m_bindings->m_indexBuffer->buffer  = "
//                << *(GLint *) hmesh->m_bindings->m_indexBuffer->buffer << std::endl;
//                ;

    if (m_lastDepthWrite != hmesh->m_depthWrite) {
        if (hmesh->m_depthWrite > 0) {
            glDepthMask(GL_TRUE);
        } else {
            glDepthMask(GL_FALSE);
        }

        m_lastDepthWrite = hmesh->m_depthWrite;
    }
    if (m_lastDepthCulling != hmesh->m_depthCulling) {
        if (hmesh->m_depthCulling > 0) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }

        m_lastDepthCulling = hmesh->m_depthCulling;
    }
    if (m_backFaceCulling != hmesh->m_backFaceCulling) {
        if (hmesh->m_backFaceCulling > 0) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }

        m_backFaceCulling = hmesh->m_backFaceCulling;
    }

    if (m_lastBlendMode != hmesh->m_blendMode) {
        BlendModeDesc &selectedBlendMode = blendModes[(char)hmesh->m_blendMode];
        if (blendModes[(char)m_lastBlendMode].blendModeEnable != selectedBlendMode.blendModeEnable ) {
            if (selectedBlendMode.blendModeEnable) {
                glEnable(GL_BLEND);
            } else {
                glDisable(GL_BLEND);
            }
        }

        glBlendFuncSeparate(
            selectedBlendMode.SrcColor,
            selectedBlendMode.DestColor,
            selectedBlendMode.SrcAlpha,
            selectedBlendMode.DestAlpha
        );
        m_lastBlendMode = hmesh->m_blendMode;
    }

//    if (hmesh->m_start == 231342) exit(0);

    glDrawElements(hmesh->m_element, hmesh->m_end, GL_UNSIGNED_SHORT, (const void *) (hmesh->m_start ));
//        glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void *) 0);
}

HGVertexBuffer GDevice::createVertexBuffer() {
    bindVertexBufferBindings(nullptr);
    std::shared_ptr<GVertexBuffer> h_vertexBuffer;
    h_vertexBuffer.reset(new GVertexBuffer(*this));

    return h_vertexBuffer;
}

HGIndexBuffer GDevice::createIndexBuffer() {
    bindVertexBufferBindings(nullptr);
    std::shared_ptr<GIndexBuffer> h_indexBuffer;
    h_indexBuffer.reset(new GIndexBuffer(*this));

    return h_indexBuffer;
}

HGVertexBufferBindings GDevice::createVertexBufferBindings() {
    std::shared_ptr<GVertexBufferBindings> h_vertexBufferBindings;
    h_vertexBufferBindings.reset(new GVertexBufferBindings(*this));

    return h_vertexBufferBindings;
}

HGMesh GDevice::createMesh(gMeshTemplate &meshTemplate) {
    std::shared_ptr<GMesh> h_mesh;
    h_mesh.reset(new GMesh(*this, meshTemplate));

    return h_mesh;
}

HGM2Mesh GDevice::createM2Mesh(gMeshTemplate &meshTemplate) {
    std::shared_ptr<GM2Mesh> h_mesh;
    h_mesh.reset(new GM2Mesh(*this, meshTemplate));

    return h_mesh;
}

HGParticleMesh GDevice::createParticleMesh(gMeshTemplate &meshTemplate) {
    std::shared_ptr<GParticleMesh> h_mesh;
    h_mesh.reset(new GParticleMesh(*this, meshTemplate));

    return h_mesh;
}

void GDevice::bindTexture(GTexture *texture, int slot) {
    if (texture == nullptr) {
        if (m_lastTexture[slot] != nullptr) {
            glActiveTexture(GL_TEXTURE0 + slot);
            m_lastTexture[slot]->unbind();
            m_lastTexture[slot] = nullptr;
        }
    }  else if (texture != m_lastTexture[slot]) {
        glActiveTexture(GL_TEXTURE0 + slot);
        texture->bind();
        m_lastTexture[slot] = texture;
    }
}

HGTexture GDevice::createBlpTexture(HBlpTexture &texture, bool xWrapTex, bool yWrapTex) {
    std::shared_ptr<GBlpTexture> hgTexture;
    hgTexture.reset(new GBlpTexture(*this, texture, xWrapTex, yWrapTex));
    return hgTexture;
}

HGTexture GDevice::createTexture() {
    std::shared_ptr<GTexture> hgTexture;
    hgTexture.reset(new GTexture(*this));
    return hgTexture;
}

void GDevice::bindProgram(GShaderPermutation *program) {
    if (program == nullptr) {
        if (m_shaderPermutation != nullptr) {
            m_shaderPermutation->unbindProgram();
            m_shaderPermutation = nullptr;
        }
    } else if (program != m_shaderPermutation) {
        program->bindProgram();
        m_shaderPermutation = program;
    }
}

GDevice::GDevice() {
    unsigned int ff = 0;
    m_blackPixelTexture = createTexture();

    m_blackPixelTexture->loadData(1,1,&ff);

    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &maxUniformBufferSize);
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uniformBufferOffsetAlign);
}

