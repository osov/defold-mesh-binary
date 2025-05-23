#include "armature.h"
#include "reader.h"

unsigned nextPOT(unsigned x) {
    if (x <= 1) return 2;
    int power = 2;
    x--;
    while (x >>= 1) power <<= 1;
    return power;
}

Armature::Armature(Reader *reader, bool verbose) {
    int boneCount = reader->ReadInt();
    if (verbose)
        dmLogInfo("reading armature, bones: %d", boneCount);

    this->boneNames.reserve(boneCount);
    this->boneParents.reserve(boneCount);
    this->localBones.reserve(boneCount);

    for (int i = 0; i < boneCount; i++) {
        this->boneNames.push_back(reader->ReadString());
        int parentIdx = reader->ReadInt();
        this->boneParents.push_back(parentIdx);
        this->localBones.push_back(reader->ReadMatrix());
        if (parentIdx == -1) {
            this->rootBoneIdx = i;
        }
    }

    int frameCount = reader->ReadInt();
    this->countFramesInAnimations.push_back(frameCount);
    if (verbose)
        dmLogInfo("frames: %d", frameCount);

    this->frames.reserve(frameCount);

    for (int i = 0; i < frameCount; i++) {
        vector<Matrix4> bones;
        bones.reserve(boneCount);
        for (int j = 0; j < boneCount; j++) {
            //3x4 transform matrix
            bones.push_back(reader->ReadMatrix());
        }

        this->frames.push_back(bones);
    }

    this->animationTextureWidth = nextPOT(this->frames[0].size() * 3);
    this->animationTextureHeight = nextPOT(frameCount);
}

Armature::~Armature() {
}

int Armature::GetFramesCount() {
    return this->frames.size();
}

int Armature::FindBone(string bone) {
    for (int i = 0; i < this->boneNames.size(); i++) {
        if (bone == this->boneNames[i])
            return i;
    }

    return -1;
}

void Armature::AddAnimation(Reader *reader, bool verbose) {
    int boneCount = reader->ReadInt();
    if (this->boneNames.size() != boneCount) {
        if (verbose)
            dmLogInfo("Unable to add animation, number of bones must match!");
        return;
    }
    for (int i = 0; i < boneCount; i++) {
        reader->ReadString();
        reader->ReadInt();
        reader->ReadMatrix();
    }
    int frameCount = reader->ReadInt();
    this->countFramesInAnimations.push_back(frameCount);
    if (verbose)
        dmLogInfo("add animation frames: %d", frameCount);

    this->frames.reserve(this->frames.size() + frameCount);

    for (int i = 0; i < frameCount; i++) {
        vector<Matrix4> bones;
        bones.reserve(boneCount);
        for (int j = 0; j < boneCount; j++) {
            //3x4 transform matrix
            bones.push_back(reader->ReadMatrix());
        }
        this->frames.push_back(bones);
    }

    this->animationTextureWidth = nextPOT(this->frames[0].size() * 3);
    this->animationTextureHeight = nextPOT(this->frames.size());
}
