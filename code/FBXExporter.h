/*
Open Asset Import Library (assimp)
----------------------------------------------------------------------

Copyright (c) 2006-2017, assimp team

All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the
following conditions are met:

* Redistributions of source code must retain the above
copyright notice, this list of conditions and the
following disclaimer.

* Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the
following disclaimer in the documentation and/or other
materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
contributors may be used to endorse or promote products
derived from this software without specific prior
written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

----------------------------------------------------------------------
*/

/** @file GltfExporter.h
* Declares the exporter class to write a scene to a gltf/glb file
*/
#ifndef AI_FBXEXPORTER_H_INC
#define AI_FBXEXPORTER_H_INC

#ifndef ASSIMP_BUILD_NO_FBX_EXPORTER

#include <assimp/types.h>
//#include <assimp/material.h>

//#include <sstream>
//#include <vector>
//#include <map>
#include <memory>

using std::unique_ptr;

struct aiScene;
//struct aiNode;
//struct aiMaterial;

/*namespace glTF
{
    template<class T>
    class Ref;

    class Asset;
    struct TexProperty;
    struct Node;
}*/

namespace Assimp
{
    class IOSystem;
    class IOStream;
    class ExportProperties;

    // ------------------------------------------------------------------------------------------------
    /** Helper class to export a given scene to an FBX file. */
    // ------------------------------------------------------------------------------------------------
    class FBXExporter
    {
    public:
        /// Constructor for a specific scene to export
        FBXExporter(const aiScene* pScene, const ExportProperties* pProperties);
        
        // call one of these methods to export
        void ExportBinary(const char* pFile, IOSystem* pIOSystem);
        void ExportAscii(const char* pFile, IOSystem* pIOSystem);

    private:
        bool binary; // whether current export is in binary or ascii format
        const aiScene* mScene; // the scene to export
        const ExportProperties* mProperties; // currently unused
        
        // binary files have a specific header and footer,
        // in addition to the actual data
        void WriteBinaryHeader(std::shared_ptr<IOStream> outfile);
        void WriteBinaryFooter(std::shared_ptr<IOStream> outfile);
        
        // WriteAllNodes does the actual export.
        // It just calls all the Write<Section> methods below in order.
        void WriteAllNodes(std::shared_ptr<IOStream> outfile);
        
        // Methods to write individual sections.
        // The order here matches the order inside an FBX file.
        // Each method corresponds to a top-level FBX section,
        // except WriteHeader which also includes some binary-only sections
        // and WriteFooter which is binary data only.
        void WriteHeaderExtension(std::shared_ptr<IOStream> outfile);
        // WriteFileId(); // binary-only, included in WriteHeader
        // WriteCreationTime(); // binary-only, included in WriteHeader
        // WriteCreator(); // binary-only, included in WriteHeader
        void WriteGlobalSettings(std::shared_ptr<IOStream> outfile);
        void WriteDocuments(std::shared_ptr<IOStream> outfile);
        void WriteReferences(std::shared_ptr<IOStream> outfile);
        void WriteDefinitions(std::shared_ptr<IOStream> outfile);
        void WriteObjects(std::shared_ptr<IOStream> outfile);
        void WriteConnections(std::shared_ptr<IOStream> outfile);
        // WriteTakes(); // deprecated since at least 2015 (fbx 7.4)
    };

}

#endif // ASSIMP_BUILD_NO_FBX_EXPORTER

#endif // AI_FBXEXPORTER_H_INC
