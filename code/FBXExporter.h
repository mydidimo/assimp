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
#include <assimp/ai_assert.h>
//#include <assimp/material.h>

//#include <sstream>
#include <vector>
//#include <map>
#include <memory> // shared_ptr
#include <sstream> // stringstream
#include <type_traits> // is_void

using std::unique_ptr;

struct aiScene;
struct aiNode;
//struct aiMaterial;

namespace FBX
{
    const std::string NULL_RECORD = { // 13 null bytes
        '\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0'
    }; // who knows why
    const std::string SEPARATOR = {'\x00', '\x01'}; // for use inside strings
    const std::string MAGIC_NODE_TAG = "_$AssimpFbx$"; // from import
    
    class Node;
    class Property;
    
    // rotation order. We'll probably use EulerXYZ for everything
    enum RotOrder {
        RotOrder_EulerXYZ = 0,
        RotOrder_EulerXZY,
        RotOrder_EulerYZX,
        RotOrder_EulerYXZ,
        RotOrder_EulerZXY,
        RotOrder_EulerZYX,

        RotOrder_SphericXYZ,

        RotOrder_MAX // end-of-enum sentinel
    };

    // transformation inheritance method. Most of the time RSrs
    enum TransformInheritance {
        TransformInheritance_RrSs = 0,
        TransformInheritance_RSrs,
        TransformInheritance_Rrs,

        TransformInheritance_MAX // end-of-enum sentinel
    };
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
        
        std::vector<FBX::Node> connections; // conection storage
        
        // this crude unique-ID system is actually fine
        int64_t last_uid = 999999;
        int64_t generate_uid() { return ++last_uid; }
        
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
        
        // helpers
        void WriteModelNodes(
            Assimp::StreamWriterLE& s,
            aiNode* node,
            int64_t parent_uid,
            std::vector<int64_t>& mesh_uids,
            std::vector<int64_t>& material_uids
        );
        void WriteModelNodes( // usually don't call this directly
            StreamWriterLE& s,
            aiNode* node,
            int64_t parent_uid,
            std::vector<int64_t>& mesh_uids,
            std::vector<int64_t>& material_uids,
            std::vector<std::pair<std::string,aiVector3D>>& transform_chain
        );
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
        explicit Property(const char* c, bool raw=false)
                : Property(std::string(c), raw) {}
        explicit Property(const std::string& s, bool raw=false)
                : type(raw ? 'R' : 'S'), data(s.size()) {
            for (size_t i = 0; i < s.size(); ++i) {
                data[i] = uint8_t(s[i]);
            }
        }
        explicit Property(const std::vector<uint8_t>& r)
            : type('R'), data(r) {}
        explicit Property(const std::vector<int32_t>& va)
            : type('i'), data(4*va.size())
        {
            int32_t* d = reinterpret_cast<int32_t*>(data.data());
            for (size_t i = 0; i < va.size(); ++i) { d[i] = va[i]; }
        }
        explicit Property(const std::vector<double>& va)
            : type('d'), data(8*va.size())
        {
            double* d = reinterpret_cast<double*>(data.data());
            for (size_t i = 0; i < va.size(); ++i) { d[i] = va[i]; }
        }
        
        // this will catch any type not defined above,
        // so that we don't accidentally convert something we don't want.
        // for example (const char*) --> (bool)... seriously wtf C++
        template <class T>
        explicit Property(T v) : type('X') {
            static_assert(std::is_void<T>::value, "TRIED TO CREATE FBX PROPERTY WITH UNSUPPORTED TYPE, CHECK YOUR PROPERTY INSTANTIATION");
        }
        
        size_t size() { return data.size() + 1; } // TODO: array types size()
        
        void Dump(Assimp::StreamWriterLE &s) {
            s.PutU1(type);
            uint8_t* d;
            size_t N;
            switch (type) {
            case 'C': s.PutU1(*(reinterpret_cast<uint8_t*>(data.data()))); return;
            case 'Y': s.PutI2(*(reinterpret_cast<int16_t*>(data.data()))); return;
            case 'I': s.PutI4(*(reinterpret_cast<int32_t*>(data.data()))); return;
            case 'F': s.PutF4(*(reinterpret_cast<float*>(data.data()))); return;
            case 'D': s.PutF8(*(reinterpret_cast<double*>(data.data()))); return;
            case 'L': s.PutI8(*(reinterpret_cast<int64_t*>(data.data()))); return;
            case 'S':
            case 'R':
                s.PutU4(data.size());
                for (size_t i = 0; i < data.size(); ++i) { s.PutU1(data[i]); }
                return;
            case 'i':
                N = data.size() / 4;
                s.PutU4(N); // number of elements
                s.PutU4(0); // no encoding (1 would be zip-compressed)
                s.PutU4(data.size()); // data size
                d = data.data();
                for (size_t i = 0; i < N; ++i) {
                    s.PutI4((reinterpret_cast<int32_t*>(d))[i]);
                }
                return;
            case 'd':
                N = data.size() / 8;
                s.PutU4(N); // number of elements
                s.PutU4(0); // no encoding (1 would be zip-compressed)
                s.PutU4(data.size()); // data size
                d = data.data();
                for (size_t i = 0; i < N; ++i) {
                    s.PutF8((reinterpret_cast<double*>(d))[i]);
                }
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
        Node() = default;
        Node(const std::string& n) : name(n) {}
        Node(const std::string& n, const Property &p)
            : name(n)
            { properties.push_back(p); }
        Node(const std::string& n, const std::vector<Property> &pv)
            : name(n), properties(pv) {}
    public:
        // add a single property to the node
        template <typename T>
        void AddProperty(T value) {
            properties.emplace_back(value);
        }
        
        // convenience function to add multiple properties at once
        template <typename T, typename... More>
        void AddProperties(T value, More... more) {
            properties.emplace_back(value);
            AddProperties(more...);
        }
        void AddProperties() {}
        
        // add a child node directly
        void AddChild(const Node& node) { children.push_back(node); }
        
        // convenience function to add a child node with a single property
        template <typename T>
        void AddChild(
            const std::string& name,
            T value
        ){
            children.emplace_back(name, Property(value));
        }
    public:
        // Properties70 Nodes
        template <typename... More>
        void AddP70(
            const std::string& name,
            const std::string& type,
            const std::string& type2,
            const std::string& flags,
            More... more
        ){
            Node n("P");
            n.AddProperties(name, type, type2, flags, more...);
            AddChild(n);
        }
        void AddP70int(const std::string& name, int32_t value) {
            Node n("P");
            n.AddProperties(name, "int", "Integer", "", value);
            AddChild(n);
        }
        void AddP70bool(const std::string& name, bool value) {
            Node n("P");
            n.AddProperties(name, "bool", "", "", int32_t(value));
            AddChild(n);
        }
        void AddP70double(const std::string& name, double value) {
            Node n("P");
            n.AddProperties(name, "double", "Number", "", value);
            AddChild(n);
        }
        void AddP70numberA(const std::string& name, double value) {
            Node n("P");
            n.AddProperties(name, "Number", "", "A", value);
            AddChild(n);
        }
        void AddP70color(const std::string& name, double r, double g, double b) {
            Node n("P");
            n.AddProperties(name, "ColorRGB", "Color", "", r, g, b);
            AddChild(n);
        }
        void AddP70colorA(const std::string& name, double r, double g, double b) {
            Node n("P");
            n.AddProperties(name, "Color", "", "A", r, g, b);
            AddChild(n);
        }
        void AddP70vector(const std::string& name, double x, double y, double z) {
            Node n("P");
            n.AddProperties(name, "Vector3D", "Vector", "", x, y, z);
            AddChild(n);
        }
        void AddP70string(const std::string& name, const std::string& value) {
            Node n("P");
            n.AddProperties(name, "KString", "", "", value);
            AddChild(n);
        }
        void AddP70enum(const std::string& name, int32_t value) {
            Node n("P");
            n.AddProperties(name, "enum", "", "", value);
            AddChild(n);
        }
        void AddP70time(const std::string& name, int64_t value) {
            Node n("P");
            n.AddProperties(name, "KTime", "Time", "", value);
            AddChild(n);
        }
    
    public: // member functions
        void Dump(std::shared_ptr<Assimp::IOStream> outfile) {
            Assimp::StreamWriterLE outstream(outfile);
            Dump(outstream);
        }
        void Dump(Assimp::StreamWriterLE &s) {
            // write header section (with placeholders for some things)
            Begin(s);
            
            // write properties
            DumpProperties(s);
            
            // go back and fill in property related placeholders
            EndProperties(s, properties.size());
            
            // write children
            DumpChildren(s);
            
            // finish, filling in end offset placeholder
            End(s, !children.empty());
        }
        void Begin(Assimp::StreamWriterLE &s) {
            // remember start pos so we can come back and write the end pos
            start_pos = s.Tell();
            
            // placeholders for end pos and property section info
            s.PutU4(0); // end pos
            s.PutU4(0); // number of properties
            s.PutU4(0); // total property section length
            
            // node name
            s.PutU1(name.size()); // length of node name
            s.PutString(name); // node name as raw bytes
            
            // property data comes after here
            property_start = s.Tell();
        }
        void DumpProperties(Assimp::StreamWriterLE& s) {
            for (auto &p : properties) { p.Dump(s); }
        }
        void DumpChildren(Assimp::StreamWriterLE& s) {
            for (Node& child : children) { child.Dump(s); }
        }
        void EndProperties(Assimp::StreamWriterLE &s) {
            EndProperties(s, properties.size());
        }
        void EndProperties(Assimp::StreamWriterLE &s, size_t num_properties) {
            if (num_properties == 0) { return; }
            size_t pos = s.Tell();
            ai_assert(pos > property_start);
            size_t property_section_size = pos - property_start;
            s.Seek(start_pos + 4);
            s.PutU4(num_properties);
            s.PutU4(property_section_size);
            s.Seek(pos);
        }
        void End(Assimp::StreamWriterLE &s, bool has_children) {
            // if there were children, add a null record
            if (has_children) { s.PutString(NULL_RECORD); }
            
            // now go back and write initial pos
            end_pos = s.Tell();
            s.Seek(start_pos);
            s.PutU4(end_pos);
            s.Seek(end_pos);
        }
    public:
        std::string name;
        std::vector<Property> properties;
        std::vector<Node> children;
    private:
        size_t start_pos; // starting position in stream
        size_t end_pos; // ending position in stream
        size_t property_start; // starting position of property section
    };

    /* convenience function to create a node with a single property,
     * and write it to the stream. */
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
    
    // convenience function to create and write a property node,
    // holding a single property which is an array of values.
    // does not copy the data, so is efficient for large arrays.
    void WritePropertyNode(
        const std::string& name,
        const std::vector<double>& v,
        Assimp::StreamWriterLE& s
    ){
        Node node(name);
        node.Begin(s);
        s.PutU1('d');
        s.PutU4(v.size()); // number of elements
        s.PutU4(0); // no encoding (1 would be zip-compressed)
        s.PutU4(v.size() * 8); // data size
        for (auto it = v.begin(); it != v.end(); ++it) { s.PutF8(*it); }
        node.EndProperties(s, 1);
        node.End(s, false);
    }
    
    // convenience function to create and write a property node,
    // holding a single property which is an array of values.
    // does not copy the data, so is efficient for large arrays.
    void WritePropertyNode(
        const std::string& name,
        const std::vector<int32_t>& v,
        Assimp::StreamWriterLE& s
    ){
        Node node(name);
        node.Begin(s);
        s.PutU1('i');
        s.PutU4(v.size()); // number of elements
        s.PutU4(0); // no encoding (1 would be zip-compressed)
        s.PutU4(v.size() * 4); // data size
        for (auto it = v.begin(); it != v.end(); ++it) { s.PutI4(*it); }
        node.EndProperties(s, 1);
        node.End(s, false);
    }
}

#endif // ASSIMP_BUILD_NO_FBX_EXPORTER

#endif // AI_FBXEXPORTER_H_INC
