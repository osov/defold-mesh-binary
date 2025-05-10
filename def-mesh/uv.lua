local M = {}

local textcoord0 = hash("texcoord0")

---@type table<userdata|hash, hash>
local source_buffers = {}
---@type table<string, meshbinary.image.buffer>
local mask_textures = {}

local half_vector = vmath.vector3(0.5, 0.5, 0)
local threshold_color = 16

---@param atlas_info resource.atlas
---@param texture string
---@return number?
local function get_texture_index(atlas_info, texture)
    for index, animation in ipairs(atlas_info.animations) do
        if animation.id == texture then
            return index
        end
    end
    error(string.format("Texture '%s' not found in atlas!", texture), 3)
end

---@param atlas_info resource.atlas
---@param index number
---@return vector4, boolean
local function get_uv_texture(atlas_info, index)
    local texture_info = resource.get_texture_info(atlas_info.texture)
    local uvs = atlas_info.geometries[index].uvs
    local min_x, min_y, max_x, max_y
    for index_uv, value in ipairs(uvs) do
        if index_uv % 2 == 1 then
            min_x = math.min(min_x or value, value)
            max_x = math.max(max_x or value, value)
        else
            min_y = math.min(min_y or value, value)
            max_y = math.max(max_y or value, value)
        end
    end
    local uv_texture = vmath.vector4(min_x / texture_info.width, 1 - max_y / texture_info.height,
        max_x / texture_info.width, 1 - min_y / texture_info.height)
    local is_rotated = uvs[1] ~= uvs[3]
    return uv_texture, is_rotated
end

---@param url_mesh userdata|hash
---@param slot string
---@param atlas userdata
---@param texture string
function M.set_texture_from_atlas(url_mesh, slot, atlas, texture)
    local atlas_info = resource.get_atlas(atlas)
    go.set(url_mesh, slot, hash(atlas_info.texture))
    local index = get_texture_index(atlas_info, texture)
    if index then
        local uv_texture, is_rotated = get_uv_texture(atlas_info, index)
        go.set(url_mesh, "uv_texture", uv_texture)
        local angle = math.rad(is_rotated == true and -90 or 0)
        local rotation = vmath.vector4(math.cos(angle), math.sin(angle), 0, 0)
        go.set(url_mesh, "rotation", rotation)
    end
end

---@param url_mesh userdata|hash
---@param slot string
---@param atlas userdata
---@param textures string[]
function M.set_textures_for_mask(url_mesh, slot, atlas, textures)
    local atlas_info = resource.get_atlas(atlas)
    go.set(url_mesh, slot, hash(atlas_info.texture))
    local uvs_texture = {}
    local rotations = {}
    for index, texture in ipairs(textures) do
        local texture_index = get_texture_index(atlas_info, texture)
        if texture_index then
            local uv_texture, is_rotated = get_uv_texture(atlas_info, texture_index)
            uvs_texture[index] = uv_texture
            local angle = math.rad(is_rotated == true and -90 or 0)
            local rotation = vmath.vector4(math.cos(angle), math.sin(angle), 0, 0)
            rotations[index] = rotation
        end
    end
    go.set(url_mesh, "uvs_texture", uvs_texture)
    go.set(url_mesh, "rotations", rotations)
end

---@param url_mesh userdata|hash
---@return userdata stream
local function get_uv_stream(url_mesh)
    local res_path = go.get(url_mesh, "vertices")
    local buf = resource.get_buffer(res_path)
    return buffer.get_stream(buf, textcoord0)
end

---@param url_mesh userdata|hash
local function restore_original_uv_buffer(url_mesh)
    local source_stream, destination_stream
    if source_buffers[url_mesh] == nil then
        source_stream = get_uv_stream(url_mesh)
        local destination_buffer = buffer.create(#source_stream / 2,
            { { name = textcoord0, type = buffer.VALUE_TYPE_FLOAT32, count = 2 } })
        destination_stream = buffer.get_stream(destination_buffer, textcoord0)
        source_buffers[url_mesh] = destination_buffer
    else
        source_stream = buffer.get_stream(source_buffers[url_mesh], textcoord0)
        destination_stream = get_uv_stream(url_mesh)
    end
    buffer.copy_stream(destination_stream, 0, source_stream, 0, #source_stream)
end

---@param url_mesh userdata|hash
---@param slot string
---@param atlas userdata
---@param texture string
function M.set_uv_texture_from_atlas(url_mesh, slot, atlas, texture)
    local atlas_info = resource.get_atlas(atlas)
    go.set(url_mesh, slot, hash(atlas_info.texture))
    local index = get_texture_index(atlas_info, texture)
    if index then
        local uv_texture, is_rotated = get_uv_texture(atlas_info, index)
        local angle = math.rad(is_rotated == true and -90 or 0)
        local rotation = vmath.quat_rotation_z(angle)
        restore_original_uv_buffer(url_mesh)
        local uv_stream = get_uv_stream(url_mesh)
        local bounds = vmath.vector3(uv_texture.z - uv_texture.x, uv_texture.w - uv_texture.y, 0)
        local temp_vector = vmath.vector3()
        for index = 1, #uv_stream, 2 do
            temp_vector.x = uv_stream[index] - 0.5
            temp_vector.y = uv_stream[index + 1] - 0.5
            local result = vmath.mul_per_elem(bounds, vmath.rotate(rotation, temp_vector) + half_vector)
            uv_stream[index] = result.x + uv_texture.x
            uv_stream[index + 1] = result.y + uv_texture.y
        end
    end
end

---@class meshbinary.image.buffer
---@field stream userdata
---@field width number
---@field height number
---@field channels number

---@param file_name string
---@return meshbinary.image.buffer?
local function load_image_from_resource(file_name)
    if mask_textures[file_name] then
        return mask_textures[file_name]
    end
    local data = sys.load_resource(file_name)
    if not data then
        error(string.format("File `%s` not found!", file_name))
        return
    end
    local img = imageloader.load({ data = data })
    local stream = buffer.get_stream(img.buffer, hash("pixels"))
    local header = img.header
    ---@type meshbinary.image.buffer
    local image_buffer = {
        stream = stream,
        width = header.width,
        height = header.height,
        channels = header.channels,
    }
    mask_textures[file_name] = image_buffer
    return image_buffer
end

local function round_color(color)
    return math.floor(color / threshold_color)
end

---@param r number
---@param g number
---@param b number
---@return number
local function get_color_id(r, g, b)
    return round_color(r) + round_color(g) * threshold_color + round_color(b) * threshold_color * threshold_color
end

---@param mask meshbinary.image.buffer
---@param uv_x number
---@param uv_y number
---@return number, number, number
local function get_id_mask(mask, uv_x, uv_y)
    local width = mask.width
    uv_x = math.floor(math.min(uv_x, 0.99999) * width)
    uv_y = math.floor(math.min(uv_y, 0.99999) * mask.height)
    local index = (uv_y * width + uv_x) * mask.channels + 1
    return mask.stream[index], mask.stream[index + 1], mask.stream[index + 2]
end

---@class meshbinary.texture.info
---@field uv_texture vector4
---@field rotation quaternion
---@field bounds vector3
---@field uv_offset? vector3

---@class meshbinary.uv_info
---@field mask_color vector3
---@field texture? string
---@field uv_offset? vector3

---@param url_mesh userdata|hash
---@param slot string
---@param atlas userdata
---@param uv_info meshbinary.uv_info[]
---@param mask_file_name string
---@param default_texture? string
function M.set_uv_textures_for_mask(url_mesh, slot, atlas, uv_info, mask_file_name, default_texture)
    local atlas_info = resource.get_atlas(atlas)
    go.set(url_mesh, slot, hash(atlas_info.texture))
    ---@type table<number, meshbinary.texture.info>
    local info_textures = {}
    for _, uv in ipairs(uv_info) do
        local texture_index = get_texture_index(atlas_info, uv.texture or default_texture)
        local uv_texture, is_rotated = get_uv_texture(atlas_info, texture_index)
        local angle = math.rad(is_rotated == true and -90 or 0)
        local color_id = uv.mask_color == nil and -1 or get_color_id(uv.mask_color.x, uv.mask_color.y, uv.mask_color.z)
        info_textures[color_id] = {
            rotation = vmath.quat_rotation_z(angle),
            bounds = vmath.vector3(uv_texture.z - uv_texture.x, uv_texture.w - uv_texture.y, 0),
            uv_texture = uv_texture,
            uv_offset = uv.uv_offset
        }
    end
    local atlas_info = resource.get_atlas(atlas)
    go.set(url_mesh, slot, hash(atlas_info.texture))
    local mask = load_image_from_resource(mask_file_name)
    if mask then
        restore_original_uv_buffer(url_mesh)
        local uv_stream = get_uv_stream(url_mesh)
        local temp_vector = vmath.vector3()
        for index = 1, #uv_stream, 2 do
            local uv_x = uv_stream[index]
            local uv_y = uv_stream[index + 1]
            local r, g, b = get_id_mask(mask, uv_x, uv_y)
            local id_mask = get_color_id(r, g, b)
            local info_texture = info_textures[id_mask]
            if info_texture then
                local uv_offset = info_texture.uv_offset
                if uv_offset then
                    uv_x = (uv_x + uv_offset.x) % 1
                    uv_y = (uv_y + uv_offset.y) % 1
                end
                temp_vector.x = uv_x - 0.5
                temp_vector.y = uv_y - 0.5
                local result = vmath.mul_per_elem(info_texture.bounds,
                    vmath.rotate(info_texture.rotation, temp_vector) + half_vector)
                local uv_texture = info_texture.uv_texture
                uv_stream[index] = result.x + uv_texture.x
                uv_stream[index + 1] = result.y + uv_texture.y
            else
                error(string.format("No parameters specified for uv transfer, color (%s, %s, %s)!", r, g, b))
            end
        end
    end
end

function M.final()
    source_buffers = {}
    mask_textures = {}
end

return M
