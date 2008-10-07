/*
   For more information, please see: http://software.sci.utah.edu

   The MIT License

   Copyright (c) 2008 Scientific Computing and Imaging Institute,
   University of Utah.

   
   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
*/

/**
  \file    GPUMemMan.h
  \author    Jens Krueger
        SCI Institute
        University of Utah
  \date    August 2008
*/

#include "GPUMemMan.h"
#include <IO/Images/BMPLoader.h>
#include <Controller/MasterController.h>

using namespace std;

GPUMemMan::GPUMemMan(MasterController* masterController) :
  m_MasterController(masterController),
  m_SystemInfo(masterController->SysInfo()),
  m_iAllocatedGPUMemory(0),
  m_iAllocatedCPUMemory(0)
{

}

GPUMemMan::~GPUMemMan() {
  for (VolDataListIter i = m_vpVolumeDatasets.begin();i<m_vpVolumeDatasets.end();i++) {
    m_MasterController->DebugOut()->Warning("GPUMemMan::~GPUMemMan","Detected unfreed dataset %s.", i->pVolumeDataset->Filename().c_str());
    delete i->pVolumeDataset;
  }

  for (SimpleTextureListIter i = m_vpSimpleTextures.begin();i<m_vpSimpleTextures.end();i++) {
    m_MasterController->DebugOut()->Warning("GPUMemMan::~GPUMemMan","Detected unfreed SimpleTexture %s.", i->strFilename.c_str());

    m_iAllocatedGPUMemory -= i->pTexture->GetCPUSize();
    m_iAllocatedCPUMemory -= i->pTexture->GetGPUSize();

    delete i->pTexture;
  }

  for (Trans1DListIter i = m_vpTrans1DList.begin();i<m_vpTrans1DList.end();i++) {
    m_MasterController->DebugOut()->Warning("GPUMemMan::~GPUMemMan","Detected unfreed 1D Transferfunction.");

    m_iAllocatedGPUMemory -= i->pTexture->GetCPUSize();
    m_iAllocatedCPUMemory -= i->pTexture->GetGPUSize();

    delete i->pTexture;
    delete i->pTransferFunction1D;
  }

  for (Trans2DListIter i = m_vpTrans2DList.begin();i<m_vpTrans2DList.end();i++) {
    m_MasterController->DebugOut()->Warning("GPUMemMan::~GPUMemMan","Detected unfreed 2D Transferfunction.");

    m_iAllocatedGPUMemory -= i->pTexture->GetCPUSize();
    m_iAllocatedCPUMemory -= i->pTexture->GetGPUSize();

    delete i->pTexture;
    delete i->pTransferFunction2D;
  }

  for (Texture3DListIter i = m_vpTex3DList.begin();i<m_vpTex3DList.end();i++) {
    m_MasterController->DebugOut()->Warning("GPUMemMan::~GPUMemMan","Detected unfreed 3D texture.");

    m_iAllocatedGPUMemory -= (*i)->pTexture->GetCPUSize();
    m_iAllocatedCPUMemory -= (*i)->pTexture->GetGPUSize();

    delete (*i);
  }

  for (FBOListIter i = m_vpFBOList.begin();i<m_vpFBOList.end();i++) {
    m_MasterController->DebugOut()->Warning("GPUMemMan::~GPUMemMan","Detected unfreed FBO.");

    m_iAllocatedGPUMemory -= (*i)->pFBOTex->GetCPUSize();
    m_iAllocatedCPUMemory -= (*i)->pFBOTex->GetGPUSize();

    delete (*i);
  }

  for (GLSLListIter i = m_vpGLSLList.begin();i<m_vpGLSLList.end();i++) {
    m_MasterController->DebugOut()->Warning("GPUMemMan::~GPUMemMan","Detected unfreed GLSL program.");

    m_iAllocatedGPUMemory -= (*i)->pGLSLProgram->GetCPUSize();
    m_iAllocatedCPUMemory -= (*i)->pGLSLProgram->GetGPUSize();

    delete (*i);
  }


  assert(m_iAllocatedGPUMemory == 0);
  assert(m_iAllocatedCPUMemory == 0);
}

// ******************** Datasets

VolumeDataset* GPUMemMan::LoadDataset(const std::string& strFilename, AbstrRenderer* requester) {
  for (VolDataListIter i = m_vpVolumeDatasets.begin();i<m_vpVolumeDatasets.end();i++) {
    if (i->pVolumeDataset->Filename() == strFilename) {
      m_MasterController->DebugOut()->Message("GPUMemMan::LoadDataset","Reusing %s", strFilename.c_str());
      i->qpUser.push_back(requester);
      return i->pVolumeDataset;
    }
  }

  m_MasterController->DebugOut()->Message("GPUMemMan::LoadDataset","Loading %s", strFilename.c_str());
  VolumeDataset* dataset = new VolumeDataset(strFilename, false, m_MasterController);  // PRECONDITION: we assume that the file has been verified before
  if (dataset->IsOpen()) {
    m_vpVolumeDatasets.push_back(VolDataListElem(dataset, requester));
    return dataset;
  } else {
    m_MasterController->DebugOut()->Error("GPUMemMan::LoadDataset","Error opening dataset %s", strFilename.c_str());
    return NULL;
  }
}


void GPUMemMan::FreeDataset(VolumeDataset* pVolumeDataset, AbstrRenderer* requester) {
  for (VolDataListIter i = m_vpVolumeDatasets.begin();i<m_vpVolumeDatasets.end();i++) {
    if (i->pVolumeDataset == pVolumeDataset) {
      for (AbstrRendererListIter j = i->qpUser.begin();j<i->qpUser.end();j++) {
        if (*j == requester) {
          i->qpUser.erase(j);
          if (i->qpUser.size() == 0) {
            m_MasterController->DebugOut()->Message("GPUMemMan::FreeDataset","Cleaning up all 3D textures associated to dataset %s", pVolumeDataset->Filename().c_str());
            FreeAssociatedTextures(pVolumeDataset);
            m_MasterController->DebugOut()->Message("GPUMemMan::FreeDataset","Released Dataset %s", pVolumeDataset->Filename().c_str());
            delete pVolumeDataset;
            m_vpVolumeDatasets.erase(i);
          } else {
            m_MasterController->DebugOut()->Message("GPUMemMan::FreeDataset","Decreased access count but dataset %s is still in use by another subsystem", pVolumeDataset->Filename().c_str());
          }
          return;
        }
      }
    }
  }
  m_MasterController->DebugOut()->Warning("GPUMemMan::FreeDataset","Dataset %s not found or not being used by requester", pVolumeDataset->Filename().c_str());
}

// ******************** Simple Textures

GLTexture2D* GPUMemMan::Load2DTextureFromFile(const std::string& strFilename) {
  for (SimpleTextureListIter i = m_vpSimpleTextures.begin();i<m_vpSimpleTextures.end();i++) {
    if (i->strFilename == strFilename) {
      m_MasterController->DebugOut()->Message("GPUMemMan::Load2DTextureFromFile","Reusing %s", strFilename.c_str());
      i->iAccessCounter++;
      return i->pTexture;
    }
  }

  TextureImage image;
  if (!BMPLoader::Load(strFilename, &image)) {
    m_MasterController->DebugOut()->Error("GPUMemMan::Load2DTextureFromFile","Unable to load file %s", strFilename.c_str());
    return NULL;
  }
  m_MasterController->DebugOut()->Message("GPUMemMan::Load2DTextureFromFile","Loading %s", strFilename.c_str());

  GLTexture2D* tex = new GLTexture2D(image.width,image.height, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 3*8, image.data, GL_LINEAR, GL_LINEAR);
  delete [] image.data;
  
  m_iAllocatedGPUMemory += tex->GetCPUSize();
  m_iAllocatedCPUMemory += tex->GetGPUSize();

  m_vpSimpleTextures.push_back(SimpleTextureListElem(1,tex,strFilename));
  return tex;
}


void GPUMemMan::FreeTexture(GLTexture2D* pTexture) {
  for (SimpleTextureListIter i = m_vpSimpleTextures.begin();i<m_vpSimpleTextures.end();i++) {
    if (i->pTexture == pTexture) {
      i->iAccessCounter--;
      if (i->iAccessCounter == 0) {
        m_MasterController->DebugOut()->Message("GPUMemMan::FreeTexture","Deleted texture %s", i->strFilename.c_str());

        m_iAllocatedGPUMemory -= i->pTexture->GetCPUSize();
        m_iAllocatedCPUMemory -= i->pTexture->GetGPUSize();

        i->pTexture->Delete();
        m_vpSimpleTextures.erase(i);
      } else {
        m_MasterController->DebugOut()->Message("GPUMemMan::FreeTexture","Decreased access count but the texture %s is still in use by another subsystem",i->strFilename.c_str());
      }
      return;
    }
  }
  m_MasterController->DebugOut()->Warning("GPUMemMan::FreeTexture","Texture not found");
}

// ******************** 1D Trans

void GPUMemMan::Changed1DTrans(AbstrRenderer* requester, TransferFunction1D* pTransferFunction1D) {
  m_MasterController->DebugOut()->Message("GPUMemMan::Changed1DTrans","Sending change notification for 1D transfer function");

  for (Trans1DListIter i = m_vpTrans1DList.begin();i<m_vpTrans1DList.end();i++) {
    if (i->pTransferFunction1D == pTransferFunction1D) {
      for (AbstrRendererListIter j = i->qpUser.begin();j<i->qpUser.end();j++) {
        if (*j != requester) (*j)->Changed1DTrans();
      }
    }
  }
}

void GPUMemMan::GetEmpty1DTrans(size_t iSize, AbstrRenderer* requester, TransferFunction1D** ppTransferFunction1D, GLTexture1D** tex) {
  m_MasterController->DebugOut()->Message("GPUMemMan::GetEmpty1DTrans","Creating new empty 1D transfer function");
  *ppTransferFunction1D = new TransferFunction1D(iSize);
  (*ppTransferFunction1D)->SetDefault();

  unsigned char* pcData = NULL;
  (*ppTransferFunction1D)->GetByteArray(&pcData);
  *tex = new GLTexture1D(GLuint((*ppTransferFunction1D)->vColorData.size()), GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE,4*8,pcData);
  delete [] pcData;

  m_iAllocatedGPUMemory += (*tex)->GetCPUSize();
  m_iAllocatedCPUMemory += (*tex)->GetGPUSize();

  m_vpTrans1DList.push_back(Trans1DListElem(*ppTransferFunction1D, *tex, requester));
}

void GPUMemMan::Get1DTransFromFile(const std::string& strFilename, AbstrRenderer* requester, TransferFunction1D** ppTransferFunction1D, GLTexture1D** tex) {
  m_MasterController->DebugOut()->Message("GPUMemMan::Get1DTransFromFile","Loading 2D transfer function from file");
  *ppTransferFunction1D = new TransferFunction1D(strFilename);

  unsigned char* pcData = NULL;
  (*ppTransferFunction1D)->GetByteArray(&pcData);
  *tex = new GLTexture1D(GLuint((*ppTransferFunction1D)->vColorData.size()), GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE,4*8,pcData);
  delete [] pcData;

  m_iAllocatedGPUMemory += (*tex)->GetCPUSize();
  m_iAllocatedCPUMemory += (*tex)->GetGPUSize();

  m_vpTrans1DList.push_back(Trans1DListElem(*ppTransferFunction1D, *tex, requester));
}

GLTexture1D* GPUMemMan::Access1DTrans(TransferFunction1D* pTransferFunction1D, AbstrRenderer* requester) {
  for (Trans1DListIter i = m_vpTrans1DList.begin();i<m_vpTrans1DList.end();i++) {
    if (i->pTransferFunction1D == pTransferFunction1D) {
      m_MasterController->DebugOut()->Message("GPUMemMan::Access1DTrans","Accessing 1D transferfunction");
      i->qpUser.push_back(requester);
      return i->pTexture;
    }
  }

  m_MasterController->DebugOut()->Error("GPUMemMan::Access1DTrans","Unable to find 1D transferfunction");
  return NULL;
}

void GPUMemMan::Free1DTrans(TransferFunction1D* pTransferFunction1D, AbstrRenderer* requester) {
  for (Trans1DListIter i = m_vpTrans1DList.begin();i<m_vpTrans1DList.end();i++) {
    if (i->pTransferFunction1D == pTransferFunction1D) {
      for (AbstrRendererListIter j = i->qpUser.begin();j<i->qpUser.end();j++) {
        if (*j == requester) {
          i->qpUser.erase(j);
          if (i->qpUser.size() == 0) {
            m_MasterController->DebugOut()->Message("GPUMemMan::Free1DTrans","Released TransferFunction1D");

            m_iAllocatedGPUMemory -= i->pTexture->GetCPUSize();
            m_iAllocatedCPUMemory -= i->pTexture->GetGPUSize();            

            delete i->pTransferFunction1D;
            i->pTexture->Delete();
            delete i->pTexture;
            m_vpTrans1DList.erase(i);
          } else {
            m_MasterController->DebugOut()->Message("GPUMemMan::Free1DTrans","Decreased access count but TransferFunction1D is still in use by another subsystem");
          }
          return;
        }
      }
    }
  }
  m_MasterController->DebugOut()->Warning("GPUMemMan::Free1DTrans","TransferFunction1D not found or not being used by requester");
}

// ******************** 2D Trans

void GPUMemMan::Changed2DTrans(AbstrRenderer* requester, TransferFunction2D* pTransferFunction2D) {
  m_MasterController->DebugOut()->Message("GPUMemMan::Changed2DTrans","Sending change notification for 2D transfer function");

  for (Trans2DListIter i = m_vpTrans2DList.begin();i<m_vpTrans2DList.end();i++) {
    if (i->pTransferFunction2D == pTransferFunction2D) {
      for (AbstrRendererListIter j = i->qpUser.begin();j<i->qpUser.end();j++) {
        if (*j != requester) (*j)->Changed2DTrans();
      }
    }
  }
}

void GPUMemMan::GetEmpty2DTrans(const VECTOR2<size_t>& iSize, AbstrRenderer* requester, TransferFunction2D** ppTransferFunction2D, GLTexture2D** tex) {
  m_MasterController->DebugOut()->Message("GPUMemMan::GetEmpty2DTrans","Creating new empty 2D transfer function");
  *ppTransferFunction2D = new TransferFunction2D(iSize);

  unsigned char* pcData = NULL;
  (*ppTransferFunction2D)->GetByteArray(&pcData);
  *tex = new GLTexture2D(GLuint(iSize.x), GLuint(iSize.y), GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE,4*8,pcData);
  delete [] pcData;
  
  m_iAllocatedGPUMemory += (*tex)->GetCPUSize();
  m_iAllocatedCPUMemory += (*tex)->GetGPUSize();

  m_vpTrans2DList.push_back(Trans2DListElem(*ppTransferFunction2D, *tex, requester));
}

void GPUMemMan::Get2DTransFromFile(const std::string& strFilename, AbstrRenderer* requester, TransferFunction2D** ppTransferFunction2D, GLTexture2D** tex) {
  m_MasterController->DebugOut()->Message("GPUMemMan::Get2DTransFromFile","Loading 2D transfer function from file");
  *ppTransferFunction2D = new TransferFunction2D(strFilename);

  unsigned char* pcData = NULL;
  (*ppTransferFunction2D)->GetByteArray(&pcData);
  *tex = new GLTexture2D(GLuint((*ppTransferFunction2D)->GetSize().x), GLuint((*ppTransferFunction2D)->GetSize().y), GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE,4*8,pcData);
  delete [] pcData;

  m_iAllocatedGPUMemory += (*tex)->GetCPUSize();
  m_iAllocatedCPUMemory += (*tex)->GetGPUSize();

  m_vpTrans2DList.push_back(Trans2DListElem(*ppTransferFunction2D, *tex, requester));
}

GLTexture2D* GPUMemMan::Access2DTrans(TransferFunction2D* pTransferFunction2D, AbstrRenderer* requester) {
  for (Trans2DListIter i = m_vpTrans2DList.begin();i<m_vpTrans2DList.end();i++) {
    if (i->pTransferFunction2D == pTransferFunction2D) {
      m_MasterController->DebugOut()->Message("GPUMemMan::Access2DTrans","Accessing 2D transferfunction");
      i->qpUser.push_back(requester);
      return i->pTexture;
    }
  }

  m_MasterController->DebugOut()->Error("GPUMemMan::Access2DTrans","Unable to find 2D transferfunction");
  return NULL;
}

void GPUMemMan::Free2DTrans(TransferFunction2D* pTransferFunction2D, AbstrRenderer* requester) {
  for (Trans2DListIter i = m_vpTrans2DList.begin();i<m_vpTrans2DList.end();i++) {
    if (i->pTransferFunction2D == pTransferFunction2D) {
      for (AbstrRendererListIter j = i->qpUser.begin();j<i->qpUser.end();j++) {
        if (*j == requester) {
          i->qpUser.erase(j);
          if (i->qpUser.size() == 0) {
            m_MasterController->DebugOut()->Message("GPUMemMan::Free2DTrans","Released TransferFunction2D");

            m_iAllocatedGPUMemory -= i->pTexture->GetCPUSize();
            m_iAllocatedCPUMemory -= i->pTexture->GetGPUSize();            

            delete i->pTransferFunction2D;
            i->pTexture->Delete();
            delete i->pTexture;

            m_vpTrans2DList.erase(i);
          } else {
            m_MasterController->DebugOut()->Message("GPUMemMan::Free2DTrans","Decreased access count but TransferFunction2D is still in use by another subsystem");
          }
          return;
        }
      }
    }
  }
  m_MasterController->DebugOut()->Warning("GPUMemMan::Free2DTrans","TransferFunction2D not found or not being used by requester");
}

// ******************** 3D Textures

GLTexture3D* GPUMemMan::Get3DTexture(VolumeDataset* pDataset, const std::vector<UINT64>& vLOD, const std::vector<UINT64>& vBrick) {
  for (Texture3DListIter i = m_vpTex3DList.begin();i<m_vpTex3DList.end();i++) {
    if ((*i)->Match(pDataset, vLOD, vBrick)) {
      m_MasterController->DebugOut()->Message("GPUMemMan::Get3DTexture","Reusing 3D texture");
      return (*i)->pTexture;
    }
  }

  // TODO: add code here to release textures/memory if we run out of free GPU/CPU mem

  m_MasterController->DebugOut()->Message("GPUMemMan::Get3DTexture","Creating new texture");

  Texture3DListElem* pNew3DTex = new Texture3DListElem(pDataset, vLOD, vBrick);

  m_iAllocatedGPUMemory += pNew3DTex->pTexture->GetCPUSize();
  m_iAllocatedCPUMemory += pNew3DTex->pTexture->GetGPUSize();

  m_vpTex3DList.push_back(pNew3DTex);
  return (*(m_vpTex3DList.end()-1))->pTexture;
}


void GPUMemMan::FreeAssociatedTextures(VolumeDataset* pDataset) {
  for (size_t i = 0;i<m_vpTex3DList.size();i++) {
    if (m_vpTex3DList[i]->pDataset == pDataset) {

      m_MasterController->DebugOut()->Message(
        "GPUMemMan::FreeAssociatedTextures",
        "Deleteting a 3D texture of size %i x %i x %i", 
        m_vpTex3DList[i]->pTexture->GetSize().x,
        m_vpTex3DList[i]->pTexture->GetSize().y,
        m_vpTex3DList[i]->pTexture->GetSize().z);

      m_iAllocatedGPUMemory -= m_vpTex3DList[i]->pTexture->GetCPUSize();
      m_iAllocatedCPUMemory -= m_vpTex3DList[i]->pTexture->GetGPUSize();

      delete m_vpTex3DList[i];

      m_vpTex3DList.erase(m_vpTex3DList.begin()+i);
      i--;
    }
  }
}


void GPUMemMan::MemSizesChanged() {
  if (m_iAllocatedCPUMemory > m_SystemInfo->GetMaxUsableCPUMem()) {
      // TODO: CPU free resources to match max mem requirements
  }

  if (m_iAllocatedGPUMemory > m_SystemInfo->GetMaxUsableGPUMem()) {
      // TODO: GPU free resources to match max mem requirements
  }
}


GLFBOTex* GPUMemMan::GetFBO(GLenum minfilter, GLenum magfilter, GLenum wrapmode, GLsizei width, GLsizei height, GLenum intformat, unsigned int iSizePerElement, bool bHaveDepth, int iNumBuffers) {

  m_MasterController->DebugOut()->Message("GPUMemMan::GetFBO","Creating new FBO of size %i x %i", width, height);

  FBOListElem* e = new FBOListElem(m_MasterController,minfilter, magfilter, wrapmode, width, height, intformat, iSizePerElement, bHaveDepth, iNumBuffers);

  m_vpFBOList.push_back(e);

  m_iAllocatedGPUMemory += e->pFBOTex->GetCPUSize();
  m_iAllocatedCPUMemory += e->pFBOTex->GetGPUSize();

  return e->pFBOTex;
}

void GPUMemMan::FreeFBO(GLFBOTex* pFBO) {
  for (size_t i = 0;i<m_vpFBOList.size();i++) {
    if (m_vpFBOList[i]->pFBOTex == pFBO) {
      m_MasterController->DebugOut()->Message("GPUMemMan::FreeFBO","Freeing FBO ");
      m_iAllocatedGPUMemory -= m_vpFBOList[i]->pFBOTex->GetCPUSize();
      m_iAllocatedCPUMemory -= m_vpFBOList[i]->pFBOTex->GetGPUSize();

      delete m_vpFBOList[i];

      m_vpFBOList.erase(m_vpFBOList.begin()+i);
      return;
    }
  }
  m_MasterController->DebugOut()->Warning("GPUMemMan::FreeFBO","FBO to free not found.");
}

GLSLProgram* GPUMemMan::GetGLSLProgram(const string& strVSFile, const string& strFSFile) {
  for (GLSLListIter i = m_vpGLSLList.begin();i<m_vpGLSLList.end();i++) {
    if ((*i)->strVSFile == strVSFile && (*i)->strFSFile == strFSFile) {
      m_MasterController->DebugOut()->Message("GPUMemMan::GetGLSLProgram","Reusing GLSL program from the VS %s and the FS %s", strVSFile.c_str(), strFSFile.c_str());
      (*i)->iAccessCounter++;
      return (*i)->pGLSLProgram;
    }
  }

  m_MasterController->DebugOut()->Message("GPUMemMan::GetGLSLProgram","Creating new GLSL program from the VS %s and the FS %s", strVSFile.c_str(), strFSFile.c_str());

  GLSLListElem* e = new GLSLListElem(m_MasterController, strVSFile, strFSFile);

  m_vpGLSLList.push_back(e);

  m_iAllocatedGPUMemory += e->pGLSLProgram->GetCPUSize();
  m_iAllocatedCPUMemory += e->pGLSLProgram->GetGPUSize();

  return e->pGLSLProgram;
}

void GPUMemMan::FreeGLSLProgram(GLSLProgram* pGLSLProgram) {
  for (size_t i = 0;i<m_vpGLSLList.size();i++) {
    if (m_vpGLSLList[i]->pGLSLProgram == pGLSLProgram) {
      m_vpGLSLList[i]->iAccessCounter--;
      if (m_vpGLSLList[i]->iAccessCounter == 0) {

        m_MasterController->DebugOut()->Message("GPUMemMan::FreeGLSLProgram","Freeing GLSL program");
        m_iAllocatedGPUMemory -= m_vpGLSLList[i]->pGLSLProgram->GetCPUSize();
        m_iAllocatedCPUMemory -= m_vpGLSLList[i]->pGLSLProgram->GetGPUSize();

        delete m_vpGLSLList[i];

        m_vpGLSLList.erase(m_vpGLSLList.begin()+i);
      } else m_MasterController->DebugOut()->Message("GPUMemMan::FreeGLSLProgram","Decreased access counter but kept GLSL program in memory.");
      return;
    }
  }
  m_MasterController->DebugOut()->Warning("GPUMemMan::FreeGLSLProgram","GLSL program to free not found.");
}