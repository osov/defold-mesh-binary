#include "model_instance.h"
#include "model.h"

static int SetURL(lua_State* L) {
	lua_getfield(L, 1, "instance");
	ModelInstance* mi = (ModelInstance* )lua_touserdata(L, -1);

	URL url = *dmScript::CheckURL(L, 2);

	int idx = mi->urls.size();
	mi->urls.push_back(url);

	return 0;
}

static int GetAnimationTextureBuffer(lua_State* L) {
	lua_getfield(L, 1, "instance");
	ModelInstance* mi = (ModelInstance* )lua_touserdata(L, -1);

	if (mi->animation == NULL) return 0;

	bool runtime = lua_toboolean(L, 2);
	if (runtime) return mi->animation->GetRuntimeBuffer(L);

	return mi->animation->GetTextureBuffer(L);
}

static int SetRuntimeTexture(lua_State* L) {
	lua_getfield(L, 1, "instance");
	ModelInstance* mi = (ModelInstance* )lua_touserdata(L, -1);

	mi->runtimeTexture = dmScript::CheckHash(L, 2);

	return 0;
}


ModelInstance::ModelInstance(Model* model, Animation* animation, bool useBakedAnimations, float scaleAABB) {
	this->useBakedAnimations = useBakedAnimations && (animation != NULL) && (animation->GetFramesCount() > 1);
	this->model = model;
	this->animation = animation;

	this->urls.reserve(this->model->meshes.size());
	this->buffers.reserve(this->model->meshes.size());

	for(auto & mesh : this->model->meshes) {
		auto buffer = mesh.CreateBuffer(this, scaleAABB);
		this->buffers.push_back(buffer);
	}

}

ModelInstance::~ModelInstance() {
	/*
	for (auto& buffer : this->buffers) {
		dmBuffer::Destroy(buffer.m_Buffer);
	}*/  
	//resource will destroy buffer anyway
}

void ModelInstance::CreateLuaProxy(lua_State* L) {
	lua_pushstring(L, this->model->name.c_str());

	lua_newtable(L);

	static const luaL_Reg f[] =
	{
		{"get_animation_buffer", GetAnimationTextureBuffer},
		{"set_runtime_texture", SetRuntimeTexture},
		{0, 0}
	};
	luaL_register(L, NULL, f);

	lua_pushstring(L, "instance");
	lua_pushlightuserdata(L, this);
	lua_settable(L, -3);

	if (!this->model->parent.empty()) {
		lua_pushstring(L, "parent");
		lua_pushstring(L, this->model->parent.c_str());
		lua_settable(L, -3);
	}

	lua_pushstring(L, "frames");
	lua_pushnumber(L, this->animation != NULL ? this->animation->GetFramesCount() : 0); //refactor
	lua_settable(L, -3);

	lua_pushstring(L, "armature");
	lua_pushnumber(L, this->model->armatureIdx); 
	lua_settable(L, -3);

	lua_pushstring(L, "position");
	dmScript::PushVector3(L, this->model->world.position);
	lua_settable(L, -3);

	lua_pushstring(L, "rotation");
	dmScript::PushQuat(L, this->model->world.rotation);
	lua_settable(L, -3);

	lua_pushstring(L, "scale");
	dmScript::PushVector3(L, this->model->world.scale);
	lua_settable(L, -3);

	lua_pushstring(L, "meshes");
	lua_newtable(L);

	int idx = 1;
	for(auto & mesh : this->model->meshes) {
		lua_newtable(L);

		static const luaL_Reg f[] =
		{
			{"set_url", SetURL},
			{0, 0}
		};
		luaL_register(L, NULL, f);

		lua_pushstring(L, "instance");
		lua_pushlightuserdata(L, this);
		lua_settable(L, -3);

		lua_pushstring(L, "buffer");
		dmScript::PushBuffer(L, this->buffers[idx-1]);
		lua_settable(L, -3);

		mesh.material.CreateLuaProxy(L);

		lua_rawseti(L, -2, idx++);

	}
	lua_settable(L, -3);
	lua_settable(L, -3);

}

bool ModelInstance::UseBakedAnimations() {
	if ((this->animation != NULL) && (this->animation->HasRootMotion())) return false;
	return this->useBakedAnimations;
}

void ModelInstance::Update(lua_State* L) {
	if (this->animation == NULL) return;

	int meshCount = this->model->meshes.size();

	this->SetShapeFrame(L, this->animation->GetFrameIdx()); //TODO blending, multi tracks

	if (this->UseBakedAnimations()) {
		if (this->animation->IsBlending()) {
			//set runtime texture
			lua_getglobal(L, "native_runtime_texture");

			dmScript::PushHash(L, this->runtimeTexture);
			this->animation->GetRuntimeBuffer(L);
			lua_call(L, 4, 0);
		}

		for (int i = 0; i < meshCount; i++) {
			lua_getglobal(L, "go");
			lua_getfield(L, -1, "set");
			lua_remove(L, -2);

			dmScript::PushURL(L, this->urls[i]);
			lua_pushstring(L, "animation");
			dmScript::PushVector4(L, this->animation->GetBakedUniform());
			lua_call(L, 3, 0);
		}

		return;
	}

	for (int i = 0; i < meshCount; i++) {
		this->ApplyArmature(L, i);
	}

}

void ModelInstance::ApplyArmature(lua_State* L, int meshIdx) {
	if (this->animation->bones == NULL) return;

	if (this->useBakedAnimations) { // overrride baked settings, e.g. we have root motion
		lua_getglobal(L, "go");
		lua_getfield(L, -1, "set");
		lua_remove(L, -2);

		dmScript::PushURL(L, this->urls[meshIdx]);
		lua_pushstring(L, "animation");
		dmScript::PushVector4(L, Vector4(0,0,0,0));
		lua_call(L, 3, 0);
	}

	for (int idx : this->model->meshes[meshIdx].usedBonesIndex) { // set only used bones, critical for performance
		int offset = idx * 3;

		Matrix4* m = &this->animation->bones->at(idx);

		Vector4 data[3] = {
			m->getRow(0), 
			m->getRow(1), 
			m->getRow(2)
		};

		//alternative - dmGameObject::SetProperty - defold/engine/gameobject/src/gameobject/gameobject.h

		for (int i = 0; i < 3; i ++) {
			lua_getglobal(L, "go");
			lua_getfield(L, -1, "set");
			lua_remove(L, -2);

			dmScript::PushURL(L, this->urls[meshIdx]);
			lua_pushstring(L, "bones");
			dmScript::PushVector4(L, data[i]);

			lua_newtable(L);
			lua_pushstring(L, "index");
			lua_pushnumber(L, offset + i + 1);
			lua_settable(L, -3);

			lua_call(L, 4, 0);
		}
	}
}

void ModelInstance::SetShapes(lua_State* L, unordered_map<string, float>* values) {
	if (this->model->shapes.empty()) return;
	vector<string> modified;

	for (auto & it : *values) {
		string name = it.first;
		float value = it.second;
		directShapeValues[name] = value;
		if (CONTAINS(&this->model->shapes, name) && (this->shapeValues[name] != value)) {
			this->shapeValues[name] = value;
			modified.emplace_back(name);
		}
	}

	if (modified.size() > 0) {
		this->CalculateShapes(&modified);
		this->ApplyShapes(L);
	}

	for (auto it = this->directShapeValues.begin(); it != this->directShapeValues.end();) {
		if (it->second == 0) {
			it = this->directShapeValues.erase(it);
		} else ++it;
	}
}

void ModelInstance::SetShapeFrame(lua_State* L, int idx1) {
	//TODO: blending

	if (this->model->shapes.empty() || this->model->shapeFrames.size() < idx1) return;

	vector<string> modified;

	for (auto & it : this->model->shapeFrames[idx1]) {
		string name = it.first;
		float value = it.second;
		if (CONTAINS(&this->directShapeValues, name)) continue;
		if (this->shapeValues[name] != value 
		/*&& fabs(this->shapeValues[name] - value) > this->threshold*/) {
			this->shapeValues[name] = value;
			modified.emplace_back(name);
		}
	}

	if (modified.size() > 0) {
		this->CalculateShapes(&modified);
		this->ApplyShapes(L);
	}
}

void ModelInstance::CalculateShapes(vector<string>* shapeNames) {
	Quat iq = Quat::identity();
	this->blended.clear();
	unordered_map<int, bool> modified;

	for (auto &name : *shapeNames) {
		for (auto &it: this->model->shapes[name]) {
			modified[it.first] = true;
		}
	}

	for (auto &it : modified) {
		int idx = it.first;
		ShapeData v;
		v.p = Vector3(0);
		v.n = Vector3(0);
		//v.q = Quat::identity();
		float weight =  0;

		for (auto &s : this->shapeValues) {
			float value = s.second;
			if (value == 0) continue;

			auto shape = &this->model->shapes[s.first];
			if (CONTAINS(shape, idx)) {
				ShapeData* delta = &shape->at(idx);
				weight += value;
				v.p +=  delta->p * value;
				v.n += delta->n * value;
				//v.q *= Lerp(value, iq, delta->q);
			}
		}

		Vertex vertex = this->model->vertices[idx];

		if (weight > 1) {
			v.p = vertex.p + v.p / weight;
			v.n = vertex.n + v.n / weight;
		}
		else if (weight > 0) {
			v.p = vertex.p + v.p;
			v.n = vertex.n + v.n;
		} else {
			v.p = vertex.p;
			v.n = vertex.n;
			//v.q = iq;
		}

		this->blended[idx] = v;
	}

	for (auto it = this->shapeValues.begin(); it != this->shapeValues.end();) {
		if (it->second == 0) {
			it = this->shapeValues.erase(it);
		} else ++it;
	}
}

void ModelInstance::ApplyShapes(lua_State* L) {
	int size = this->model->meshes.size();
	for (int i = 0; i < size; i++) {
		Mesh* mesh = &this->model->meshes[i];

		bool needUpdate = false;

		float* positions = 0x0;
		float* normals = 0x0;

		uint32_t components = 0;
		uint32_t stride = 0;
		uint32_t items_count = 0;

		dmBuffer::GetStream(this->buffers[i].m_Buffer, dmHashString64("position"), (void**)&positions, &items_count, &components, &stride);
		dmBuffer::GetStream(this->buffers[i].m_Buffer, dmHashString64("normal"), (void**)&normals, &items_count, &components, &stride);

		for (auto & it : this->blended) {	
			ShapeData vertex = it.second;
			if (!CONTAINS(&mesh->vertexMap, it.first)) continue;
			needUpdate = true;

			for (auto & count : mesh->vertexMap[it.first]) {

				int idx = stride * count;

				positions[idx] = vertex.p.getX();
				positions[idx + 1] = vertex.p.getY();
				positions[idx + 2] = vertex.p.getZ();

				normals[idx] = vertex.n.getX();
				normals[idx + 1] = vertex.n.getY();
				normals[idx + 2] = vertex.n.getZ();

			}
		}

		if (needUpdate) {
			lua_getglobal(L, "native_update_buffer");

			dmScript::PushURL(L, this->urls[i]);
			dmScript::PushBuffer(L, this->buffers[i]);
			lua_call(L, 2, 0);
		}
	}
}

URL* ModelInstance::AttachGameObject(dmGameObject::HInstance go, string bone) {
	int idx = this->animation->FindBone(bone);
	if (idx > -1) {
		this->animation->CreateBoneGO(go, idx);
		return &this->urls[0];
	}
	return NULL;
}
