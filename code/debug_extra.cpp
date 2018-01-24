#include <assimp/scene.h> // aiScene, aiNode
#include <assimp/postprocess.h> // aiProcess_*
#include <assimp/mesh.h> // aiMesh
#include <assimp/material.h> // aiMaterial, aiTextureType_*

#include <iostream>
#include <string>
using std::string; using std::cout; using std::endl;

// print basic info about a loaded aiMesh
void fbx_print_mesh_info(
    const aiMesh* mesh,
    size_t mesh_index=size_t(-1),
    const string &indent="",
    bool compact=true
){
    cout << indent << "mesh";
    if (mesh_index != size_t(-1)) {
        cout << " " << mesh_index;
    }
    if (mesh->mName.length) {
        cout << " (" << mesh->mName.C_Str() << ")";
    }
    if (compact) {
        cout << " [" << mesh->mNumVertices;
        cout << " / " << mesh->mNumBones;
        cout << " / " << mesh->mNumFaces;
        cout << " |";
    } else {
        cout << ":" << endl;
        cout << indent + "  vertices: " << mesh->mNumVertices << endl;
        cout << indent + "  bones: " << mesh->mNumBones << endl;
        cout << indent + "  faces: " << mesh->mNumFaces << endl;
        cout << indent + "  face types:";
    }
    const unsigned int ptypes = mesh->mPrimitiveTypes;
    if (ptypes & aiPrimitiveType_POINT) { cout << " point"; }
    if (ptypes & aiPrimitiveType_LINE) { cout << " line"; }
    if (ptypes & aiPrimitiveType_TRIANGLE) { cout << " triangle"; }
    if (ptypes & aiPrimitiveType_POLYGON) { cout << " polygon"; }
    if (compact) { cout << "]"; }
    cout << endl;
}

// prettily print the node graph to stdout
void fbx_print_node_heirarchy(
    const aiNode* node,
    const string &indent="",
    bool hideFbxNodes=false,
    bool last=false,
    bool first=true
){
    // first a quick override for $AssimpFbx$ transform nodes
    string name(node->mName.C_Str());
    if (hideFbxNodes && name.find("$AssimpFbx$") != string::npos) {
        if (node->mNumChildren == 1) {
            // skip this node
            fbx_print_node_heirarchy(
                node->mChildren[0],
                indent,
                hideFbxNodes,
                last,
                false
            );
            return;
        }
    }
    
    // now print the name
    string branchchar = "├╴";
    if (last) { branchchar = "└╴"; }
    if (first) { branchchar = ""; }
    cout << indent << branchchar << name;
    
    // if there are meshes attached, indicate this
    if (node->mNumMeshes) {
        cout << " (mesh ";
        bool sep = false;
        for (size_t i=0; i < node->mNumMeshes; ++i) {
            unsigned int mesh_index = node->mMeshes[i];
            if (sep) { cout << ", "; }
            cout << mesh_index;
            sep = true;
        }
        cout << ")";
    }
    
    // finish the line
    cout << endl;
    
    // and recurse
    string nextIndent = indent + "│ ";
    if (first) { nextIndent = indent; }
    else if (last) { nextIndent = indent + "  "; }
    for (size_t i = 0; i < node->mNumChildren; ++i) {
        bool lastone = (i == node->mNumChildren - 1);
        fbx_print_node_heirarchy(
            node->mChildren[i],
            nextIndent,
            hideFbxNodes,
            lastone,
            false
        );
    }
}
