local ANIMATOR = require "def-mesh.animator"

local M = {}

M.BUFFER_COUNT = 0

local used_textures = {}

local function create_buffer(buf)
	M.BUFFER_COUNT = M.BUFFER_COUNT and M.BUFFER_COUNT + 1 or 1
	local path = "/def-mesh/buffers/buffer" .. M.BUFFER_COUNT .. ".bufferc"
	return resource.create_buffer(path, { buffer = buf, transfer_ownership = false }), path
end

function native_update_buffer(mesh_url, buffer)
	resource.set_buffer(go.get(mesh_url, "vertices"), buffer, { transfer_ownership = false })
end

function native_runtime_texture(tpath, width, height, buffer)
	local tparams = {
		width  = width,
		height = height,
		type   = resource.TEXTURE_TYPE_2D,
		format = resource.TEXTURE_FORMAT_RGBA32F,
	}
	resource.set_texture(tpath, tparams, buffer)
end

local function get_animation_texture(path, model, runtime)
	local tpath = string.format("/__anim_%s_%s%s.texturec", path, model.armature, runtime == true and "_runtime" or "")

	if not pcall(function()
			resource.get_texture_info(tpath)
		end) then
		local twidth, theight, tbuffer = model:get_animation_buffer(runtime)
		local tparams = {
			width  = twidth,
			height = theight,
			type   = resource.TEXTURE_TYPE_2D,
			format = resource.TEXTURE_FORMAT_RGBA32F,
		}

		return resource.create_texture(tpath, tparams, tbuffer)
	else
		return hash(tpath)
	end
end

local function set_texture(self, url, slot, file, texel)
	if not file then
		return false
	end
	local path = self.texture_folder .. file .. ".texturec"
	local texture_id = hash(path)
	if used_textures[texture_id] == nil then
		local data = sys.load_resource(self.texture_folder .. file)
		if not data then
			pprint(self.texture_folder .. file .. " not found")
			return false
		end
		local img = imageloader.load { data = data }
		if not pcall(function()
				texture_id = resource.create_texture(path, img.header, img.buffer)
			end) then
			--already exists?
		end
	end

	used_textures[texture_id] = (used_textures[texture_id] or 0) + 1
	self.textures[texture_id] = true
	go.set(url, slot, texture_id)
	if texel then
		pcall(function()
			go.set(url, texel, vmath.vector4(1. / img.header.width, 1. / img.header.height, 0, 0))
		end)
	end
	return true
end

local function get_bone_go(self, bone)
	if self.bones_go[bone] then
		return self.bones_go[bone]
	end

	local id = factory.create(msg.url(self.url_binary.socket, self.url_binary.path, "factory_bone"))

	local parent = self.binary:attach_bone_go(id, bone)
	if parent then
		go.set_parent(id, parent)
		self.bones_go[bone] = id

		return id
	end

	return nil
end

--[[
url - url to binary.go instance
path - path to .bin file in custom assets
config = {
	verbose - true to get models info
	textures - path to folder with textures
	bake -  true to bake animations into texture
	aabb - float, scale factor - force aabb creation scaled with this value
	(for frustum culling skinned meshes)
	materials - table of materials to replace,
	use editor script "Add materials from model" to generate properties
}
animations - list path to .bin animation files in custom assets
--]]
M.load = function(url, path, config, animations)
	url = type(url) == "string" and msg.url(nil, url, nil) or url

	local instance = {
		meshes = {},
		attaches = {},
		bones_go = {},
		textures = {},
		game_objects = {},
		url = url,
		url_binary = url
	}

	config = config or {}

	if config.url_binary then
		instance.url_binary = config.url_binary
	end

	instance.texture_folder = config.textures or "/assets/"
	if string.find(instance.texture_folder, "/") ~= 1 then
		instance.texture_folder = "/" .. instance.texture_folder
	end
	if string.find(instance.texture_folder, "/$") == nil then
		instance.texture_folder = instance.texture_folder .. "/"
	end

	local models
	local data, err = sys.load_resource(path)
	if err then
		error(err, 2)
		return
	end
	local animations_data
	local path_hash = hash_to_hex(hash(path))
	local is_add_animations = animations and #animations > 0
	if is_add_animations then
		animations_data = {}
		for _, animation_path in ipairs(animations) do
			local animation_data, err = sys.load_resource(animation_path)
			if err then
				error(err, 2)
				return
			else
				table.insert(animations_data, animation_data)
			end
		end
		local list_files = string.format("%s,%s", path, table.concat(animations, ","))
		path_hash = hash_to_hex(hash(list_files))
	end

	instance.binary, models = mesh_utils.load(path_hash, data, url, config.bake or false, config.verbose or false,
		config.aabb or 0, animations_data)

	instance.animator = ANIMATOR.create(instance.binary)
	if is_add_animations then
		local count_frames_in_animations = instance.binary.count_frames_in_animations
		if #animations ~= #count_frames_in_animations - 1 then
			error("The number of animations does not match! Unable to create a list of animations!", 2)
		end
		local list_animations = {}
		local first_frame = count_frames_in_animations[1]
		for index, animation_path in ipairs(animations) do
			local _, _, animation_name = string.find(animation_path, "/?([^/]+)%.")
			local finish_frame = first_frame + count_frames_in_animations[index + 1]
			list_animations[animation_name] = { start = first_frame, finish = finish_frame - 1 }
			first_frame = finish_frame
		end
		instance.animator.list = list_animations
	end

	instance.models = {}
	for name, model in pairs(models) do
		instance.models[name] = {}
		instance.total_frames = model.frames

		local anim_texture, runtime_texture

		if model.frames > 1 and config.bake then
			anim_texture = get_animation_texture(path_hash, model)
			runtime_texture = get_animation_texture(path_hash, model, true)
			model:set_runtime_texture(runtime_texture)
		end

		for i, mesh in ipairs(model.meshes) do
			--local f = mesh.material.type == 0 and "#factory" or "#factory_trans"
			--local id = factory.create(url .. f, model.position, model.rotation, {}, model.scale)

			local id = factory.create(msg.url(instance.url_binary.socket, instance.url_binary.path, "factory"),
				model.position, model.rotation, {}, model.scale)
			local mesh_url = msg.url(nil, id, "mesh")
			mesh:set_url(mesh_url);

			local material = nil
			local materials = msg.url(instance.url_binary.socket, instance.url_binary.path, "binary")
			pcall(function() material = go.get(materials, mesh.material.name) end)

			if config.materials and config.materials[mesh.material.name] then
				go.set(mesh_url, "material", config.materials[mesh.material.name])
			elseif material then
				go.set(mesh_url, "material", material)
			elseif mesh.material.type == 0 then
				go.set(mesh_url, "material", go.get(materials, "opaque"))
			else
				go.set(mesh_url, "material", go.get(materials, "transparent"))
			end

			local v, bpath = create_buffer(mesh.buffer)
			go.set(mesh_url, "vertices", v)

			instance.game_objects[i == 1 and name or (name .. "_" .. i)] =
			{
				id = id,
				url = mesh_url,
				path = bpath,
				parent = model.parent
			}

			table.insert(instance.models[name], id)

			if anim_texture then
				instance.textures[anim_texture] = true;
				instance.textures[runtime_texture] = true;
				go.set(mesh_url, "texture1", anim_texture)
				go.set(mesh_url, "texture2", runtime_texture)
			end

			if mesh.material.texture then
				set_texture(instance, mesh_url, "texture0", mesh.material.texture)
			end

			pcall(function()
				go.set(mesh_url, "base_color", mesh.material and mesh.material.color or vmath.vector4(1, 1, 1, 1))
			end)
		end
	end


	--set hierarchy
	for _, mesh in pairs(instance.game_objects) do
		if mesh.parent and instance.game_objects[mesh.parent] then
			go.set_parent(mesh.id, instance.game_objects[mesh.parent].id, false)
		else
			go.set_parent(mesh.id, url, false)
		end
	end

	instance.binary:set_frame(0, 0);
	instance.binary:update();


	---------------------------------------------------------------
	instance.hide = function(name)
		for _, id in ipairs(instance.models[name]) do
			msg.post(id, "disable")
		end
	end

	---------------------------------------------------------------
	instance.show = function(name)
		for _, id in ipairs(instance.models[name]) do
			msg.post(id, "enable")
		end
	end

	---------------------------------------------------------------

	instance.delete = function(with_objects)
		if with_objects then
			go.delete(instance.url, true)
		end

		for key, mesh in pairs(instance.game_objects) do
			resource.release(mesh.path)
		end

		if instance.binary then
			if instance.binary:delete() then --clean up textures, no more instances
				for texture, _ in pairs(instance.textures) do
					if used_textures[texture] then
						used_textures[texture] = used_textures[texture] - 1
						if used_textures[texture] == 0 then
							used_textures[texture] = nil
							resource.release(texture)
						end
					else
						resource.release(texture)
					end
				end
			end
		end
	end

	instance.set_frame = function(frame1, frame2, blend) -- use animator directly for more flexible approach
		instance.animator.set_frame(0, frame1, frame2, blend)
		instance.animator.update_tracks()
	end

	instance.attach = function(bone, target_url)
		local id = get_bone_go(instance, bone)
		go.set_parent(target_url, id, true)
	end

	instance.set_shapes = function(shapes)
		instance.binary:set_shapes(shapes)
	end

	instance.set = function(property, value)
		for _, mesh in pairs(instance.game_objects) do
			go.set(mesh.url, property, value)
		end
	end

	return instance
end

return M
