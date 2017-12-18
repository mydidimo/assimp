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
#ifndef ASSIMP_BUILD_NO_EXPORT
#ifndef ASSIMP_BUILD_NO_FBX_EXPORTER

#include "FBXExporter.h"

#include "StreamWriter.h"

#include "Exceptional.h"
//#include "StringComparison.h"
//#include "ByteSwapper.h"

//#include "SplitLargeMeshes.h"

//#include <assimp/SceneCombiner.h>
//#include <assimp/version.h>
#include <assimp/IOSystem.hpp>
#include <assimp/Exporter.hpp>
//#include <assimp/material.h>
//#include <assimp/scene.h>

// Header files, standard library.
#include <memory> // shared_ptr
#include <string>
#include <sstream> // stringstream
//#include <inttypes.h>


// RESOURCES:
// https://code.blender.org/2013/08/fbx-binary-file-format-specification/
// https://wiki.blender.org/index.php/User:Mont29/Foundation/FBX_File_Structure


// FBX files have some hashed values that depend on the creation time field,
// but for now we don't actually know how to generate these.
// what we can do is set them to a known-working version.
// this is the data that Blender uses in their FBX export process.
namespace FBX {
    const std::string EXPORT_VERSION_STR = "7.4.0";
    const uint32_t EXPORT_VERSION_INT = 7400; // 7.4 == 2014/2015
    const std::string GENERIC_CTIME = "1970-01-01 10:00:00:000";
    const std::string GENERIC_FILEID = "\x28\xb3\x2a\xeb\xb6\x24\xcc\xc2\xbf\xc8\xb0\x2a\xa9\x2b\xfc\xf1";
    const std::string GENERIC_FOOTID = "\xfa\xbc\xab\x09\xd0\xc8\xd4\x66\xb1\x76\xfb\x83\x1c\xf7\x26\x7e";
}

using namespace Assimp;
using namespace FBX;

namespace Assimp {

    // ------------------------------------------------------------------------------------------------
    // Worker function for exporting a scene to binary FBX. Prototyped and registered in Exporter.cpp
    void ExportSceneFBX (
        const char* pFile,
        IOSystem* pIOSystem,
        const aiScene* pScene,
        const ExportProperties* pProperties
    ){
        // initialze the exporter
        FBXExporter exporter(pScene, pProperties);
        
        // perform binary export
        exporter.ExportBinary(pFile, pIOSystem);
    }

    // ------------------------------------------------------------------------------------------------
    // Worker function for exporting a scene to ASCII FBX. Prototyped and registered in Exporter.cpp
    void ExportSceneFBXA (
        const char* pFile,
        IOSystem* pIOSystem,
        const aiScene* pScene,
        const ExportProperties* pProperties
    ){
        // initialze the exporter
        FBXExporter exporter(pScene, pProperties);
        
        // perform ascii export
        exporter.ExportAscii(pFile, pIOSystem);
    }

} // end of namespace Assimp

FBXExporter::FBXExporter (
    const aiScene* pScene,
    const ExportProperties* pProperties
)
    : mScene(pScene)
    , mProperties(pProperties)
{
    // will probably need to determine UUIDs, connections, etc here.
    // basically anything that needs to be known
    // before we start writing sections to the stream.
}

void FBXExporter::ExportBinary (
    const char* pFile,
    IOSystem* pIOSystem
){
    // remember that we're exporting in binary mode
    binary = true;
    
    // open the indicated file for writing (in binary mode)
    std::shared_ptr<IOStream> outfile(pIOSystem->Open(pFile,"wb"));
    if (!outfile) {
        throw DeadlyExportError(
            "could not open output .fbx file: " + std::string(pFile)
        );
    }
    
    // first a binary-specific file header
    WriteBinaryHeader(outfile);
    
    // the rest of the file is in node entries.
    // we have to serialize each entry before we write to the output,
    // as the first thing we write is the byte offset of the _next_ entry.
    // Either that or we can skip back to write the offset when we finish.
    WriteAllNodes(outfile);
    
    // finally we have a binary footer to the file
    WriteBinaryFooter(outfile);
}

void FBXExporter::WriteBinaryHeader(
    std::shared_ptr<IOStream> outfile
){
    // first a specific sequence of 23 bytes, always the same
    const char binary_header[24] = "Kaydara FBX Binary\x20\x20\x00\x1a\x00";
    outfile->Write(binary_header, 1, 23);
    
    // then FBX version number, "multiplied" by 1000, as little-endian uint32.
    // so 7.3 becomes 7300 == 0x841C0000, 7.4 becomes 7400 == 0xE81C0000, etc
    {
        StreamWriterLE outstream(outfile);
        outstream.PutU4(EXPORT_VERSION_INT);
    } // StreamWriter destructor writes the data to the file
    
    // after this the node data starts immediately
    // (probably with the FBXHEaderExtension node)
}

void FBXExporter::WriteBinaryFooter(
    std::shared_ptr<IOStream> outfile
){
    outfile->Write(GENERIC_FOOTID.c_str(), GENERIC_FOOTID.size(), 1);
    for (size_t i = 0; i < 4; ++i) {
        outfile->Write("\x00", 1, 1);
    }

    // here some padding is added for alignment to 16 bytes.
    // if already aligned, the full 16 bytes is added.
    size_t pos = outfile->Tell();
    size_t pad = 16 - (pos % 16);
    for (size_t i = 0; i < pad; ++i) {
        outfile->Write("\x00", 1, 1);
    }

    // now the file version again
    {
        StreamWriterLE outstream(outfile);
        outstream.PutU4(EXPORT_VERSION_INT);
    } // StreamWriter destructor writes the data to the file

    // and finally some binary footer added to all files
    for (size_t i = 0; i < 120; ++i) {
        outfile->Write("\x00", 1, 1);
    }
    outfile->Write(
        "\xf8\x5a\x8c\x6a\xde\xf5\xd9\x7e\xec\xe9\x0c\xe3\x75\x8f\x29\x0b",
        1,
        16
    );
}

void FBXExporter::ExportAscii (
    const char* pFile,
    IOSystem* pIOSystem
){
    // remember that we're exporting in ascii mode
    binary = false;
    
    // open the indicated file for writing in text mode
    std::shared_ptr<IOStream> outfile(pIOSystem->Open(pFile,"wt"));
    if (!outfile) {
        throw DeadlyExportError(
            "could not open output .fbx file: " + std::string(pFile)
        );
    }
    
    // this isn't really necessary,
    // but the Autodesk FBX SDK puts a similar comment at the top of the file.
    // Theirs declares that the file copyright is owned by Autodesk...
    std::stringstream head;
    using std::endl;
    head << "; FBX " << EXPORT_VERSION_STR << " project file" << endl;
    head << "; Created by the Open Asset Import Library (Assimp)" << endl;
    head << "; http://assimp.org" << endl;
    head << "; -------------------------------------------------" << endl;
    head << endl;
    const std::string ascii_header = head.str();
    outfile->Write(ascii_header.c_str(), ascii_header.size(), 1);
    
    // write all the sections
    WriteAllNodes(outfile);
    
    // done
}

void FBXExporter::WriteAllNodes (
    std::shared_ptr<IOStream> outfile
){
    // header
    // (and fileid, creation time, creator, if binary)
    WriteHeaderExtension(outfile);
    
    // ...included in WriteHeader
    
    // global settings
    WriteGlobalSettings(outfile);
    
    // documents
    WriteDocuments(outfile);
    
    // references
    WriteReferences(outfile);
    
    // definitions
    WriteDefinitions(outfile);
    
    // objects
    WriteObjects(outfile);
    
    // connections
    WriteConnections(outfile);
    
    // WriteTakes? (deprecated since at least 2015 (fbx 7.4))
}

void FBXExporter::WriteHeaderExtension (
    std::shared_ptr<IOStream> outfile
){
    // make the top header
    
}

void FBXExporter::WriteGlobalSettings (
    std::shared_ptr<IOStream> outfile
){
}

void FBXExporter::WriteDocuments (
    std::shared_ptr<IOStream> outfile
){
}

void FBXExporter::WriteReferences (
    std::shared_ptr<IOStream> outfile
){
}

void FBXExporter::WriteDefinitions (
    std::shared_ptr<IOStream> outfile
){
}

void FBXExporter::WriteObjects (
    std::shared_ptr<IOStream> outfile
){
}

void FBXExporter::WriteConnections (
    std::shared_ptr<IOStream> outfile
){
}

#endif // ASSIMP_BUILD_NO_FBX_EXPORTER
#endif // ASSIMP_BUILD_NO_EXPORT
