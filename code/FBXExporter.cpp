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

#include "Exceptional.h" // DeadlyExportError
//#include "StringComparison.h"
//#include "ByteSwapper.h"

//#include "SplitLargeMeshes.h"

//#include <assimp/SceneCombiner.h>
#include <assimp/version.h>
#include <assimp/IOSystem.hpp>
#include <assimp/Exporter.hpp>
//#include <assimp/material.h>
#include <assimp/scene.h>
#include <assimp/mesh.h>

// Header files, standard library.
#include <memory> // shared_ptr
#include <string>
#include <sstream> // stringstream
#include <ctime> // localtime, tm_*
#include <map>
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
    // will probably need to determine UIDs, connections, etc here.
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
    outfile.reset(pIOSystem->Open(pFile,"wb"));
    if (!outfile) {
        throw DeadlyExportError(
            "could not open output .fbx file: " + std::string(pFile)
        );
    }
    
    // first a binary-specific file header
    WriteBinaryHeader();
    
    // the rest of the file is in node entries.
    // we have to serialize each entry before we write to the output,
    // as the first thing we write is the byte offset of the _next_ entry.
    // Either that or we can skip back to write the offset when we finish.
    WriteAllNodes();
    
    // finally we have a binary footer to the file
    WriteBinaryFooter();
    
    // explicitly release file pointer,
    // so we don't have to rely on class destruction.
    outfile.reset();
}

void FBXExporter::ExportAscii (
    const char* pFile,
    IOSystem* pIOSystem
){
    // remember that we're exporting in ascii mode
    binary = false;
    
    // open the indicated file for writing in text mode
    outfile.reset(pIOSystem->Open(pFile,"wt"));
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
    WriteAllNodes();
    
    // explicitly release file pointer,
    // so we don't have to rely on class destruction.
    outfile.reset();
}

void FBXExporter::WriteBinaryHeader()
{
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

void FBXExporter::WriteBinaryFooter()
{
    outfile->Write(NULL_RECORD.c_str(), NULL_RECORD.size(), 1);

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

void FBXExporter::WriteAllNodes ()
{
    // header
    // (and fileid, creation time, creator, if binary)
    WriteHeaderExtension();
    
    // global settings
    WriteGlobalSettings();
    
    // documents
    WriteDocuments();
    
    // references
    WriteReferences();
    
    // definitions
    WriteDefinitions();
    
    // objects
    WriteObjects();
    
    // connections
    WriteConnections();
    
    // WriteTakes? (deprecated since at least 2015 (fbx 7.4))
}

//FBXHeaderExtension top-level node
void FBXExporter::WriteHeaderExtension ()
{
    FBX::Node n("FBXHeaderExtension");
    StreamWriterLE outstream(outfile);
    
    // begin node
    n.Begin(outstream);
    
    // write properties
    // (none)
    
    // finish properties
    n.EndProperties(outstream, 0);
    
    // write child nodes
    WritePropertyNode("FBXHeaderVersion", int32_t(1003), outstream);
    WritePropertyNode("FBXVersion", int32_t(EXPORT_VERSION_INT), outstream);
    WritePropertyNode("EncryptionType", int32_t(0), outstream);
    
    FBX::Node CreationTimeStamp("CreationTimeStamp");
    time_t rawtime;
    time(&rawtime);
    struct tm * now = localtime(&rawtime);
    CreationTimeStamp.AddChild("Version", int32_t(1000));
    CreationTimeStamp.AddChild("Year", int32_t(now->tm_year + 1900));
    CreationTimeStamp.AddChild("Month", int32_t(now->tm_mon + 1));
    CreationTimeStamp.AddChild("Day", int32_t(now->tm_mday));
    CreationTimeStamp.AddChild("Hour", int32_t(now->tm_hour));
    CreationTimeStamp.AddChild("Minute", int32_t(now->tm_min));
    CreationTimeStamp.AddChild("Second", int32_t(now->tm_sec));
    CreationTimeStamp.AddChild("Millisecond", int32_t(0));
    CreationTimeStamp.Dump(outstream);
    
    std::stringstream creator;
    creator << "Open Asset Import Library (Assimp) " << aiGetVersionMajor()
            << "." << aiGetVersionMinor() << "." << aiGetVersionRevision();
    WritePropertyNode("Creator", creator.str(), outstream);
    
    FBX::Node sceneinfo("SceneInfo");
    //sceneinfo.AddProperty("GlobalInfo" + FBX::SEPARATOR + "SceneInfo");
    // not sure if any of this is actually needed,
    // so just write an empty node for now.
    sceneinfo.Dump(outstream);
    
    // finish node
    n.End(outstream, true);
    
    // that's it for FBXHeaderExtension...
    
    // but binary files also need top-level FileID, CreationTime, Creator:
    std::vector<uint8_t> raw(GENERIC_FILEID.size());
    for (size_t i = 0; i < GENERIC_FILEID.size(); ++i) {
        raw[i] = uint8_t(GENERIC_FILEID[i]);
    }
    WritePropertyNode("FileId", raw, outstream);
    WritePropertyNode("CreationTime", GENERIC_CTIME, outstream);
    WritePropertyNode("Creator", creator.str(), outstream);
}

void FBXExporter::WriteGlobalSettings ()
{
    FBX::Node gs("GlobalSettings");
    gs.AddChild("Version", int32_t(1000));

    FBX::Node p("Properties70");
    p.AddP70int("UpAxis", 1);
    p.AddP70int("UpAxisSign", 1);
    p.AddP70int("FrontAxis", 2);
    p.AddP70int("FrontAxisSign", 1);
    p.AddP70int("CoordAxis", 0);
    p.AddP70int("CoordAxisSign", 1);
    p.AddP70int("OriginalUpAxis", 1);
    p.AddP70int("OriginalUpAxisSign", 1);
    p.AddP70double("UnitScaleFactor", 1.0);
    p.AddP70double("OriginalUnitScaleFactor", 1.0);
    p.AddP70color("AmbientColor", 0.0, 0.0, 0.0);
    p.AddP70string("DefaultCamera", "Producer Perspective");
    p.AddP70enum("TimeMode", 11);
    p.AddP70enum("TimeProtocol", 2);
    p.AddP70enum("SnapOnFrameMode", 0);
    p.AddP70time("TimeSpanStart", 0); // ?
    p.AddP70time("TimeSpanStop", 0); // ?
    p.AddP70double("CustomFrameRate", -1.0);
    p.AddP70("TimeMarker", "Compound", "", ""); // not sure what this is
    p.AddP70int("CurrentTimeMarker", -1);
    gs.AddChild(p);
    
    gs.Dump(outfile);
}

void FBXExporter::WriteDocuments ()
{
    // not sure what the use of multiple documents would be,
    // or whether any end-appication supports it
    FBX::Node docs("Documents");
    docs.AddChild("Count", int32_t(1));
    FBX::Node doc("Document");
    
    // generate uid
    int64_t uid = generate_uid();
    doc.AddProperties(uid, "", "Scene");
    FBX::Node p("Properties70");
    p.AddP70("SourceObject", "object", "", ""); // what is this even for?
    p.AddP70string("ActiveAnimStackName", "Take 001"); // should do this properly?
    doc.AddChild(p);
    
    // UID for root node in scene heirarchy.
    // always set to 0 in the case of a single document.
    // not sure what happens if more than one document exists.
    doc.AddChild("RootNode", int64_t(0));
    
    docs.AddChild(doc);
    docs.Dump(outfile);
}

void FBXExporter::WriteReferences ()
{
    // always empty for now.
    // not really sure what this is for.
    FBX::Node n("References");
    n.Dump(outfile);
}

size_t count_nodes(const aiNode* n) {
    size_t count = 1;
    for (size_t i = 0; i < n->mNumChildren; ++i) {
        count += count_nodes(n->mChildren[i]);
    }
    return count;
}

bool has_phong_mat(const aiScene* scene)
{
    // just search for any material with a shininess exponent
    for (size_t i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial* mat = scene->mMaterials[i];
        float shininess = 0;
        mat->Get(AI_MATKEY_SHININESS, shininess);
        if (shininess > 0) {
            return true;
        }
    }
    return false;
}

void FBXExporter::WriteDefinitions ()
{
    // basically this is just bookkeeping:
    // determining how many of each type of object there are
    // and specifying the base properties to use when otherwise unspecified.
    
    // we need to count the objects
    int32_t count;
    int32_t total_count = 0;
    
    // and store them
    std::vector<FBX::Node> object_nodes;
    FBX::Node n, pt, p;
    
    // GlobalSettings
    // this seems to always be here in Maya exports
    n = FBX::Node("ObjectType", Property("GlobalSettings"));
    count = 1;
    n.AddChild("Count", count);
    object_nodes.push_back(n);
    total_count += count;
    
    // AnimationStack / FbxAnimStack
    // this seems to always be here in Maya exports
    count = 1;
    if (count) {
        n = FBX::Node("ObjectType", Property("AnimationStack"));
        n.AddChild("Count", count);
        pt = FBX::Node("PropertyTemplate", Property("FBXAnimLayer"));
        p = FBX::Node("Properties70");
        p.AddP70string("Description", "");
        p.AddP70time("LocalStart", 0);
        p.AddP70time("LocalStop", 0);
        p.AddP70time("ReferenceStart", 0);
        p.AddP70time("ReferenceStop", 0);
        pt.AddChild(p);
        n.AddChild(pt);
        object_nodes.push_back(n);
        total_count += count;
    }
    
    // AnimationLayer / FbxAnimLayer
    // this seems to always be here in Maya exports
    count = 1;
    if (count) {
        n = FBX::Node("ObjectType", Property("AnimationLayer"));
        n.AddChild("Count", count);
        pt = FBX::Node("PropertyTemplate", Property("FBXAnimLayer"));
        p = FBX::Node("Properties70");
        p.AddP70("Weight", "Number", "", "A", double(100));
        p.AddP70bool("Mute", 0);
        p.AddP70bool("Solo", 0);
        p.AddP70bool("Lock", 0);
        p.AddP70color("Color", 0.8, 0.8, 0.8);
        p.AddP70("BlendMode", "enum", "", "", int32_t(0));
        p.AddP70("RotationAccumulationMode", "enum", "", "", int32_t(0));
        p.AddP70("ScaleAccumulationMode", "enum", "", "", int32_t(0));
        p.AddP70("BlendModeBypass", "ULongLong", "", "", int64_t(0));
        pt.AddChild(p);
        n.AddChild(pt);
        object_nodes.push_back(n);
        total_count += count;
    }
    
    // NodeAttribute / FbxSkeleton
    // bones are treated specially
    count = 0;
    if (count) {
        n = FBX::Node("ObjectType", Property("NodeAttribute"));
        n.AddChild("Count", count);
        pt = FBX::Node("PropertyTemplate", Property("FbxSkeleton"));
        p = FBX::Node("Properties70");
        p.AddP70color("Color", 0.8, 0.8, 0.8);
        p.AddP70double("Size", 100);
        p.AddP70("LimbLength", "double", "Number", "H", double(1));
        // note: not sure what the "H" flag is for - hidden?
        pt.AddChild(p);
        n.AddChild(pt);
        object_nodes.push_back(n);
        total_count += count;
    }
    
    // Model / FbxNode
    // <~~ node heirarchy
    count = count_nodes(mScene->mRootNode);
    if (count) {
        n = FBX::Node("ObjectType", Property("Model"));
        n.AddChild("Count", count);
        pt = FBX::Node("PropertyTemplate", Property("FbxNode"));
        p = FBX::Node("Properties70");
        p.AddP70enum("QuaternionInterpolate", 0);
        p.AddP70vector("RotationOffset", 0.0, 0.0, 0.0);
        p.AddP70vector("RotationPivot", 0.0, 0.0, 0.0);
        p.AddP70vector("ScalingOffset", 0.0, 0.0, 0.0);
        p.AddP70vector("ScalingPivot", 0.0, 0.0, 0.0);
        p.AddP70bool("TranslationActive", 0);
        p.AddP70vector("TranslationMin", 0.0, 0.0, 0.0);
        p.AddP70vector("TranslationMax", 0.0, 0.0, 0.0);
        p.AddP70bool("TranslationMinX", 0);
        p.AddP70bool("TranslationMinY", 0);
        p.AddP70bool("TranslationMinZ", 0);
        p.AddP70bool("TranslationMaxX", 0);
        p.AddP70bool("TranslationMaxY", 0);
        p.AddP70bool("TranslationMaxZ", 0);
        p.AddP70enum("RotationOrder", 0);
        p.AddP70bool("RotationSpaceForLimitOnly", 0);
        p.AddP70double("RotationStiffnessX", 0.0);
        p.AddP70double("RotationStiffnessY", 0.0);
        p.AddP70double("RotationStiffnessZ", 0.0);
        p.AddP70double("AxisLen", 10.0);
        p.AddP70vector("PreRotation", 0.0, 0.0, 0.0);
        p.AddP70vector("PostRotation", 0.0, 0.0, 0.0);
        p.AddP70bool("RotationActive", 0);
        p.AddP70vector("RotationMin", 0.0, 0.0, 0.0);
        p.AddP70vector("RotationMax", 0.0, 0.0, 0.0);
        p.AddP70bool("RotationMinX", 0);
        p.AddP70bool("RotationMinY", 0);
        p.AddP70bool("RotationMinZ", 0);
        p.AddP70bool("RotationMaxX", 0);
        p.AddP70bool("RotationMaxY", 0);
        p.AddP70bool("RotationMaxZ", 0);
        p.AddP70enum("InheritType", 0);
        p.AddP70bool("ScalingActive", 0);
        p.AddP70vector("ScalingMin", 0.0, 0.0, 0.0);
        p.AddP70vector("ScalingMax", 1.0, 1.0, 1.0);
        p.AddP70bool("ScalingMinX", 0);
        p.AddP70bool("ScalingMinY", 0);
        p.AddP70bool("ScalingMinZ", 0);
        p.AddP70bool("ScalingMaxX", 0);
        p.AddP70bool("ScalingMaxY", 0);
        p.AddP70bool("ScalingMaxZ", 0);
        p.AddP70vector("GeometricTranslation", 0.0, 0.0, 0.0);
        p.AddP70vector("GeometricRotation", 0.0, 0.0, 0.0);
        p.AddP70vector("GeometricScaling", 1.0, 1.0, 1.0);
        p.AddP70double("MinDampRangeX", 0.0);
        p.AddP70double("MinDampRangeY", 0.0);
        p.AddP70double("MinDampRangeZ", 0.0);
        p.AddP70double("MaxDampRangeX", 0.0);
        p.AddP70double("MaxDampRangeY", 0.0);
        p.AddP70double("MaxDampRangeZ", 0.0);
        p.AddP70double("MinDampStrengthX", 0.0);
        p.AddP70double("MinDampStrengthY", 0.0);
        p.AddP70double("MinDampStrengthZ", 0.0);
        p.AddP70double("MaxDampStrengthX", 0.0);
        p.AddP70double("MaxDampStrengthY", 0.0);
        p.AddP70double("MaxDampStrengthZ", 0.0);
        p.AddP70double("PreferedAngleX", 0.0);
        p.AddP70double("PreferedAngleY", 0.0);
        p.AddP70double("PreferedAngleZ", 0.0);
        p.AddP70("LookAtProperty", "object", "", "");
        p.AddP70("UpVectorProperty", "object", "", "");
        p.AddP70bool("Show", 1);
        p.AddP70bool("NegativePercentShapeSupport", 1);
        p.AddP70int("DefaultAttributeIndex", -1);
        p.AddP70bool("Freeze", 0);
        p.AddP70bool("LODBox", 0);
        p.AddP70("Lcl Translation", "Lcl Translation", "", "A", double(0), double(0), double(0));
        p.AddP70("Lcl Rotation", "Lcl Rotation", "", "A", double(0), double(0), double(0));
        p.AddP70("Lcl Scaling", "Lcl Scaling", "", "A", double(1), double(1), double(1));
        p.AddP70("Visibility", "Visibility", "", "A", double(1));
        p.AddP70("Visibility Inheritance", "Visibility Inheritance", "", "", int32_t(1));
        pt.AddChild(p);
        n.AddChild(pt);
        object_nodes.push_back(n);
        total_count += count;
    }
    
    // Geometry / FbxMesh
    // <~~ aiMesh
    count = mScene->mNumMeshes;
    if (count) {
        n = FBX::Node("ObjectType", Property("Geometry"));
        n.AddChild("Count", count);
        pt = FBX::Node("PropertyTemplate", Property("FbxMesh"));
        p = FBX::Node("Properties70");
        p.AddP70color("Color", 0, 0, 0);
        p.AddP70vector("BBoxMin", 0, 0, 0);
        p.AddP70vector("BBoxMax", 0, 0, 0);
        p.AddP70bool("Primary Visibility", 1);
        p.AddP70bool("Casts Shadows", 1);
        p.AddP70bool("Receive Shadows", 1);
        pt.AddChild(p);
        n.AddChild(pt);
        object_nodes.push_back(n);
        total_count += count;
    }
    
    // Material / FbxSurfacePhong, FbxSurfaceLambert, FbxSurfaceMaterial
    // <~~ aiMaterial
    // basically if there's any phong material this is defined as phong,
    // and otherwise lambert.
    // More complex materials have a bare-bones FbxSurfaceMaterial definition
    // and are treated specially, as they're not really supported by FBX.
    count = mScene->mNumMaterials;
    if (count) {
        bool has_phong = has_phong_mat(mScene);
        n = FBX::Node("ObjectType", Property("Material"));
        n.AddChild("Count", count);
        pt = FBX::Node("PropertyTemplate");
        if (has_phong) {
            pt.AddProperty("FbxSurfacePhong");
        } else {
            pt.AddProperty("FbxSurfaceLambert");
        }
        p = FBX::Node("Properties70");
        p.AddP70string("ShadingModel", "Phong");
        p.AddP70bool("MultiLayer", 0);
        p.AddP70colorA("EmissiveColor", 0.0, 0.0, 0.0);
        p.AddP70numberA("EmissiveFactor", 1.0);
        p.AddP70colorA("AmbientColor", 0.2, 0.2, 0.2);
        p.AddP70numberA("AmbientFactor", 1.0);
        p.AddP70colorA("DiffuseColor", 0.8, 0.8, 0.8);
        p.AddP70numberA("DiffuseFactor", 1.0);
        p.AddP70vector("Bump", 0.0, 0.0, 0.0);
        p.AddP70vector("NormalMap", 0.0, 0.0, 0.0);
        p.AddP70double("BumpFactor", 1.0);
        p.AddP70colorA("TransparentColor", 0.0, 0.0, 0.0);
        p.AddP70numberA("TransparencyFactor", 0.0);
        p.AddP70color("DisplacementColor", 0.0, 0.0, 0.0);
        p.AddP70double("DisplacementFactor", 1.0);
        p.AddP70color("VectorDisplacementColor", 0.0, 0.0, 0.0);
        p.AddP70double("VectorDisplacementFactor", 1.0);
        if (has_phong) {
            p.AddP70colorA("SpecularColor", 0.2, 0.2, 0.2);
            p.AddP70numberA("SpecularFactor", 1.0);
            p.AddP70numberA("ShininessExponent", 20.0);
            p.AddP70colorA("ReflectionColor", 0.0, 0.0, 0.0);
            p.AddP70numberA("ReflectionFactor", 1.0);
        }
        pt.AddChild(p);
        n.AddChild(pt);
        object_nodes.push_back(n);
        total_count += count;
    }
    
    // Texture / FbxFileTexture
    // <~~ aiTexture
    
    // AnimationCurveNode / FbxAnimCurveNode
    
    // CollectionExclusive / FbxDisplayLayer
    
    // Pose
    
    // Deformer
    
    // Video / FbxVideo
    
    // (template)
    count = 0;
    if (count) {
        n = FBX::Node("ObjectType", Property(""));
        n.AddChild("Count", count);
        pt = FBX::Node("PropertyTemplate", Property(""));
        p = FBX::Node("Properties70");
        pt.AddChild(p);
        n.AddChild(pt);
        object_nodes.push_back(n);
        total_count += count;
    }
    
    // now write it all
    FBX::Node defs("Definitions");
    defs.AddChild("Version", int32_t(100));
    defs.AddChild("Count", int32_t(total_count));
    for (auto &n : object_nodes) { defs.AddChild(n); }
    defs.Dump(outfile);
}

void FBXExporter::WriteObjects ()
{
    // numbers should match those given in definitions! make sure to check
    StreamWriterLE outstream(outfile);
    FBX::Node object_node("Objects");
    object_node.Begin(outstream);
    object_node.EndProperties(outstream);
    
    // geometry (aiMesh)
    std::vector<int64_t> mesh_uids;
    for (size_t i = 0; i < mScene->mNumMeshes; ++i) {
        // it's all about this mesh
        aiMesh* m = mScene->mMeshes[i];
        
        // start the node record
        FBX::Node n("Geometry");
        int64_t uid = generate_uid();
        mesh_uids.push_back(uid);
        n.AddProperty(uid);
        n.AddProperty(FBX::SEPARATOR + "Geometry");
        n.AddProperty("Mesh");
        n.Begin(outstream);
        n.DumpProperties(outstream);
        n.EndProperties(outstream);
        
        // output vertex data - each vertex should be unique (probably)
        std::vector<double> flattened_vertices;
        // index of original vertex in vertex data vector
        std::vector<int32_t> vertex_indices;
        // map of vertex value to its index in the data vector
        std::map<aiVector3D,size_t> index_by_vertex_value;
        size_t index = 0;
        for (size_t j = 0; j < m->mNumVertices; ++j) {
            aiVector3D vtx = m->mVertices[j];
            auto elem = index_by_vertex_value.find(vtx);
            if (elem == index_by_vertex_value.end()) {
                vertex_indices.push_back(index);
                index_by_vertex_value[vtx] = index;
                flattened_vertices.push_back(vtx[0]);
                flattened_vertices.push_back(vtx[1]);
                flattened_vertices.push_back(vtx[2]);
                ++index;
            } else {
                vertex_indices.push_back(elem->second);
            }
        }
        WritePropertyNode("Vertices", flattened_vertices, outstream);
        
        // output polygon data as a flattened array of vertex indices.
        // the last vertex index of each polygon is negated and - 1
        std::vector<int32_t> polygon_data;
        for (size_t j = 0; j < m->mNumFaces; ++j) {
            const aiFace &f = m->mFaces[j];
            for (size_t k = 0; k < f.mNumIndices - 1; ++k) {
                polygon_data.push_back(vertex_indices[f.mIndices[k]]);
            }
            polygon_data.push_back(-1 - vertex_indices[f.mIndices[f.mNumIndices-1]]);
        }
        WritePropertyNode("PolygonVertexIndex", polygon_data, outstream);
        
        // here could be edges but they're insane.
        // it's optional anyway, so let's ignore it.
        
        WritePropertyNode("GeometryVersion", int32_t(124), outstream);
        
        // here should be normals, UVs etc.
        // they're added as "layers".
        
        // finally we have the layer specifications,
        // which select the normals / UV set / etc to use.
        
        // finish the node record
        n.End(outstream, true);
    }
    
    // aiMaterial
    std::vector<int64_t> material_uids;
    for (size_t i = 0; i < mScene->mNumMaterials; ++i) {
        // it's all about this material
        aiMaterial* m = mScene->mMaterials[i];
        
        // these are used to recieve material data
        float f; aiColor3D c;
        
        // start the node record
        FBX::Node n("Material");
        
        int64_t uid = generate_uid();
        material_uids.push_back(uid);
        n.AddProperty(uid);
        
        aiString name;
        m->Get(AI_MATKEY_NAME, name);
        n.AddProperty(name.C_Str() + FBX::SEPARATOR + "Material");
        
        n.AddProperty("");
        
        n.AddChild("Version", int32_t(102));
        f = 0;
        m->Get(AI_MATKEY_SHININESS, f);
        bool phong = (f > 0);
        if (phong) {
            n.AddChild("ShadingModel", "phong");
        } else {
            n.AddChild("ShadingModel", "lambert");
        }
        n.AddChild("MultiLayer", int32_t(0));
        
        FBX::Node p("Properties70");
        
        // these properties seem duplicated in Maya output.
        // there is one for animating,
        // and another for the values...
        // for now just ignore the animating ones,
        // as they're problematic anyway.
        
        c.r = 0; c.g = 0; c.b = 0;
        m->Get(AI_MATKEY_COLOR_EMISSIVE, c);
        p.AddP70vector("Emissive", c.r, c.g, c.b);
        c.r = 0; c.g = 0; c.b = 0;
        m->Get(AI_MATKEY_COLOR_AMBIENT, c);
        p.AddP70vector("Ambient", c.r, c.g, c.b);
        c.r = 0; c.g = 0; c.b = 0;
        m->Get(AI_MATKEY_COLOR_DIFFUSE, c);
        p.AddP70vector("Diffuse", c.r, c.g, c.b);
        if (phong) {
            c.r = 0; c.g = 0; c.b = 0;
            m->Get(AI_MATKEY_COLOR_SPECULAR, c);
            p.AddP70vector("Specular", c.r, c.g, c.b);
            f = 0;
            m->Get(AI_MATKEY_SHININESS, f);
            p.AddP70double("Shininess", f);
        }
        f = 0;
        m->Get(AI_MATKEY_OPACITY, f);
        p.AddP70double("Opacity", f);
        if (phong) {
            f = 0;
            m->Get(AI_MATKEY_REFLECTIVITY, f);
            p.AddP70double("Reflectivity", f);
        }
        
        n.AddChild(p);
        
        n.Dump(outstream);
    }
    
    // write nodes (i.e. model heirarchy)
    // start at root node
    WriteModelNodes(
        outstream, mScene->mRootNode, 0, mesh_uids, material_uids
    );
    
    object_node.End(outstream, true);
}

void FBXExporter::WriteModelNodes(
    StreamWriterLE& s,
    aiNode* node,
    int64_t parent_uid,
    std::vector<int64_t>& mesh_uids,
    std::vector<int64_t>& material_uids
) {
    int64_t node_uid = 0;
    // generate uid and connect to parent, if not the root node
    if (node != mScene->mRootNode) {
        node_uid = generate_uid();
        FBX::Node c("C");
        c.AddProperties("OO", node_uid, parent_uid);
        connections.push_back(c);
    }
    
    // is this a mesh node?
    if (node->mNumMeshes == 1) {
        // connect to child mesh, which should have been written previously
        FBX::Node c("C");
        c.AddProperties("OO", mesh_uids[node->mMeshes[0]], node_uid);
        connections.push_back(c);
        // also connect to the material for the child mesh
        c = FBX::Node("C");
        c.AddProperties(
            "OO",
            material_uids[mScene->mMeshes[node->mMeshes[0]]->mMaterialIndex],
            node_uid
        );
        connections.push_back(c);
        // write model node
        FBX::Node m("Model");
        std::string name = node->mName.C_Str() + FBX::SEPARATOR + "Model";
        m.AddProperties(node_uid, name, "Mesh");
        m.AddChild("Version", int32_t(232));
        FBX::Node p("Properties70");
        p.AddP70enum("InheritType", 1);
        m.AddChild(p);
        // TODO: transform
        m.Dump(s);
    } else if (node != mScene->mRootNode) {
        // generate a null node so we can add children to it
        // (the root node is defined implicitly)
        FBX::Node m("Model");
        std::string name = node->mName.C_Str() + FBX::SEPARATOR + "Model";
        m.AddProperties(node_uid, name, "Null");
        m.AddChild("Version", int32_t(232));
        FBX::Node p("Properties70");
        p.AddP70enum("InheritType", 1);
        m.AddChild(p);
        // TODO: transform
        m.Dump(s);
    }
    
    // if more than one child mesh, make nodes for each mesh
    if (node->mNumMeshes > 1) {
        for (size_t i = 0; i < node->mNumMeshes; ++i) {
            // make a new model node
            int64_t new_node_uid = generate_uid();
            // connect to parent node
            FBX::Node c("C");
            c.AddProperties("OO", new_node_uid, node_uid);
            connections.push_back(c);
            // connect to child mesh, which should have been written previously
            c = FBX::Node("C");
            c.AddProperties("OO", mesh_uids[node->mMeshes[i]], new_node_uid);
            connections.push_back(c);
            // also connect to the material for the child mesh
            c = FBX::Node("C");
            c.AddProperties(
                "OO",
                material_uids[mScene->mMeshes[node->mMeshes[i]]->mMaterialIndex],
                new_node_uid
            );
            connections.push_back(c);
            // write model node
            FBX::Node m("Model");
            std::string name = mScene->mMeshes[node->mMeshes[i]]->mName.C_Str();
            name += FBX::SEPARATOR + "Model";
            m.AddProperties(new_node_uid, name, "Mesh");
            m.AddChild("Version", int32_t(232));
            FBX::Node p("Properties70");
            p.AddP70enum("InheritType", 1);
            m.AddChild(p);
            // TODO: transform
            m.Dump(s);
        }
    }
    
    // now recurse into children
    for (size_t i = 0; i < node->mNumChildren; ++i) {
        WriteModelNodes(
            s, node->mChildren[i], node_uid, mesh_uids, material_uids
        );
    }
}

void FBXExporter::WriteConnections ()
{
    // we should have completed the connection graph already,
    // so basically just dump it here
    FBX::Node conn("Connections");
    StreamWriterLE outstream(outfile);
    conn.Begin(outstream);
    for (auto &n : connections) {
        n.Dump(outstream);
    }
    conn.End(outstream, !connections.empty());
    connections.clear();
}

#endif // ASSIMP_BUILD_NO_FBX_EXPORTER
#endif // ASSIMP_BUILD_NO_EXPORT
