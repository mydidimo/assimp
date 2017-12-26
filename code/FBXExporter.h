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

#include "StreamWriter.h" // StreamWriterLE
#include "Exceptional.h" // DeadlyExportError

#include <assimp/types.h>
//#include <assimp/material.h>

//#include <sstream>
#include <vector>
//#include <map>
#include <memory> // shared_ptr
#include <sstream> // stringstream

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

namespace FBX
{
    const std::string NULL_RECORD = { // 13 null bytes
        '\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0'
    }; // who knows why
}

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
        std::shared_ptr<IOStream> outfile; // file to write to
        
        // binary files have a specific header and footer,
        // in addition to the actual data
        void WriteBinaryHeader();
        void WriteBinaryFooter();
        
        // WriteAllNodes does the actual export.
        // It just calls all the Write<Section> methods below in order.
        void WriteAllNodes();
        
        // Methods to write individual sections.
        // The order here matches the order inside an FBX file.
        // Each method corresponds to a top-level FBX section,
        // except WriteHeader which also includes some binary-only sections
        // and WriteFooter which is binary data only.
        void WriteHeaderExtension();
        // WriteFileId(); // binary-only, included in WriteHeader
        // WriteCreationTime(); // binary-only, included in WriteHeader
        // WriteCreator(); // binary-only, included in WriteHeader
        void WriteGlobalSettings();
        void WriteDocuments();
        void WriteReferences();
        void WriteDefinitions();
        void WriteObjects();
        void WriteConnections();
        // WriteTakes(); // deprecated since at least 2015 (fbx 7.4)
    };
}

namespace FBX
{
    /** FBX::Property
     *
     *  Holds a value of any of FBX's recognized types,
     *  each represented by a particular one-character code.
     *  C : 1-byte uint8, usually 0x00 or 0x01 to represent boolean false and true
     *  Y : 2-byte int16
     *  I : 4-byte int32
     *  F : 4-byte float
     *  D : 8-byte double
     *  L : 8-byte int64
     *  i : array of int32
     *  f : array of float
     *  d : array of double
     *  l : array of int64
     *  b : array of 1-byte booleans (0x00 or 0x01)
     *  S : string (array of 1-byte char)
     *  R : raw data (array of bytes)
     */
    class Property
    {
    public:
        // constructors for basic types.
        // all explicit to avoid accidental typecasting
        explicit Property(bool v) : type('C'), data(1) { data = {uint8_t(v)}; }
        // TODO: determine if there is actually a byte type,
        // or if this always means <bool>. 'C' seems to imply <char>,
        // so possibly the above was intended to represent both.
        explicit Property(int16_t v) : type('Y'), data(2) {
            uint8_t* d = data.data();
            (reinterpret_cast<int16_t*>(d))[0] = v;
        }
        explicit Property(int32_t v) : type('I'), data(4) {
            uint8_t* d = data.data();
            (reinterpret_cast<int32_t*>(d))[0] = v;
        }
        explicit Property(float v) : type('F'), data(4) {
            uint8_t* d = data.data();
            (reinterpret_cast<float*>(d))[0] = v;
        }
        explicit Property(double v) : type('D'), data(8) {
            uint8_t* d = data.data();
            (reinterpret_cast<double*>(d))[0] = v;
        }
        explicit Property(int64_t v) : type('L'), data(8) {
            uint8_t* d = data.data();
            (reinterpret_cast<int64_t*>(d))[0] = v;
        }
        explicit Property(const std::string& s, bool raw=false)
            : type(raw ? 'R' : 'S'), data(s.size()) {
            for (size_t i = 0; i < s.size(); ++i) {
                data[i] = uint8_t(s[i]);
            }
        }
        explicit Property(const std::vector<uint8_t>& r) : type('R'), data(r) {}
        // TODO: array types
        
        size_t size() { return data.size() + 1; } // TODO: array types size()
        
        void Dump(Assimp::StreamWriterLE &s) {
            s.PutU1(type);
            switch (type) {
            case 'C': s.PutU1(*(reinterpret_cast<uint8_t*>(data.data()))); return;
            case 'Y': s.PutU2(*(reinterpret_cast<int16_t*>(data.data()))); return;
            case 'I': s.PutU4(*(reinterpret_cast<int32_t*>(data.data()))); return;
            case 'F': s.PutU4(*(reinterpret_cast<float*>(data.data()))); return;
            case 'D': s.PutU8(*(reinterpret_cast<double*>(data.data()))); return;
            case 'L': s.PutU8(*(reinterpret_cast<int64_t*>(data.data()))); return;
            case 'S':
                s.PutU4(data.size());
                for (size_t i = 0; i < data.size(); ++i) { s.PutU1(data[i]); }
                return;
            case 'R':
                s.PutU4(data.size());
                for (size_t i = 0; i < data.size(); ++i) { s.PutU1(data[i]); }
                return;
            default:
                std::stringstream err;
                err << "Tried to dump property with invalid type '";
                err << type << "'!";
                throw DeadlyExportError(err.str());
            }
        }
    
    private:
        char type;
        std::vector<uint8_t> data;
    };
    
    class Node
    {
    public: // constructors
        Node(const std::string& n) : name(n) {}
        Node(const std::string& n, const Property &p)
            : name(n)
            { properties.push_back(p); }
        Node(const std::string& n, const std::vector<Property> &pv)
            : name(n), properties(pv) {}
    public:
        template <typename T>
        void AddProperty(
            const std::string& name,
            T value
        ){
            properties.emplace_back(value);
        }
        
        template <typename T>
        void AddChild(
            const std::string& name,
            T value
        ){
            children.emplace_back(name, Property(value));
        }
    public: // member functions
        void Dump(Assimp::StreamWriterLE &s) {
            // remember start pos so we can come back and write the end pos
            start_pos = s.Tell();
            
            // paceholder end pos because we don't know it yet
            s.PutU4(0);
            
            // number of properties
            s.PutU4(properties.size());
            
            // length of property list (bytes)
            size_t plist_size = 0;
            for (auto &p : properties) { plist_size += p.size(); }
            s.PutU4(plist_size);
            
            // length of node name
            s.PutU1(name.size());
            
            // node name as raw bytes
            s.PutString(name);
            
            // properties
            for (auto &p : properties) { p.Dump(s); }
            
            // children
            for (Node& child : children) { child.Dump(s); }
            
            // if there were children, add a null record
            if (children.size() > 0) { s.PutString(NULL_RECORD); }
            
            // now go back and write initial pos
            end_pos = s.Tell();
            s.Seek(start_pos);
            s.PutU4(end_pos);
            s.Seek(end_pos);
        }
        void Begin(Assimp::StreamWriterLE &s, int32_t numProperties);
        void End(Assimp::StreamWriterLE &s);
    public:
        std::string name;
        std::vector<Property> properties;
        std::vector<Node> children;
    private:
        size_t start_pos; // starting position in stream
        size_t end_pos; // ending position in stream
    };


    template <typename T>
    void WritePropertyNode(
        const std::string& name,
        const T value,
        Assimp::StreamWriterLE& s
    ){
        Property p(value);
        Node node(name, p);
        node.Dump(s);
    }
}

#endif // ASSIMP_BUILD_NO_FBX_EXPORTER

#endif // AI_FBXEXPORTER_H_INC
