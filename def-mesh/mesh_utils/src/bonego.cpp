#include "bonego.h"

BoneGameObject::BoneGameObject(dmGameObject::HInstance obj, int idx) {
	this->gameObject = obj;
	this->boneIdx = idx;
}

void BoneGameObject::ApplyTransform(vector<Matrix4>* bones) {

	Matrix4 m = bones->at(this->boneIdx);
	//m.setRow(3, Vector4(1));

	Vector4 v1 = m.getRow(0);
	Vector4 v2 = m.getRow(1);
	Vector4 v3 = m.getRow(2);

	Quat q = Quat(m.getUpper3x3());
	dmGameObject::SetRotation(this->gameObject, Quat(q.getX(), q.getZ(), -q.getY(), q.getW()));
	dmGameObject::SetPosition(this->gameObject, dmVMath::Point3(v1.getW(), v3.getW(), -v2.getW()));
}