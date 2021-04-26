-- KEYS - None
-- ARGV - None

local appl_db = "0"
local config_db = "4"
local state_db = "6"

local lossypg_reserved = 19 * 1024
local lossypg_reserved_400g = 37 * 1024
-- Number of 400G ports
local port_count_400g = 0
-- Number of lossy PG on 400G ports
local lossypg_400g = 0

local result = {}
local profiles = {}

local total_port = 0

local mgmt_pool_size = 256 * 1024
local egress_mirror_headroom = 10 * 1024

local function find_profile(ref)
    -- Remove the surrounding square bracket and the find in the list
    local name = string.sub(ref, 2, -2)
    for i = 1, #profiles, 1 do
        if profiles[i][1] == name then
            return i
        end
    end
    return 0
end

local function iterate_all_items(all_items)
    table.sort(all_items)
    local port
    local fvpairs
    for i = 1, #all_items, 1 do
        -- Count the number of priorities or queues in each BUFFER_PG or BUFFER_QUEUE item
        -- For example, there are:
        --     3 queues in 'BUFFER_QUEUE_TABLE:Ethernet0:0-2'
        --     2 priorities in 'BUFFER_PG_TABLE:Ethernet0:3-4'
        port = string.match(all_items[i], "Ethernet%d+")
        if port ~= nil then
            local range = string.match(all_items[i], "Ethernet%d+:([^%s]+)$")
            local profile = redis.call('HGET', all_items[i], 'profile')
            local index = find_profile(profile)
            if index == 0 then
                -- Indicate an error in case the referenced profile hasn't been inserted or has been removed
                -- It's possible when the orchagent is busy
                -- The buffermgrd will take care of it and retry later
                return 1
            end
            local size
            if string.len(range) == 1 then
                size = 1
            else
                size = 1 + tonumber(string.sub(range, -1)) - tonumber(string.sub(range, 1, 1))
            end
            profiles[index][2] = profiles[index][2] + size
            local speed = redis.call('HGET', 'PORT_TABLE:'..port, 'speed')
            if speed == '400000' then
                if profile == '[BUFFER_PROFILE_TABLE:ingress_lossy_profile]' then
                    lossypg_400g = lossypg_400g + size
                end
                port_count_400g = port_count_400g + 1
            end
        end
    end
    return 0
end

-- Connect to CONFIG_DB
redis.call('SELECT', config_db)

local ports_table = redis.call('KEYS', 'PORT|*')

total_port = #ports_table

local egress_lossless_pool_size = redis.call('HGET', 'BUFFER_POOL|egress_lossless_pool', 'size')

-- Whether shared headroom pool is enabled?
local default_lossless_param_keys = redis.call('KEYS', 'DEFAULT_LOSSLESS_BUFFER_PARAMETER*')
local over_subscribe_ratio = tonumber(redis.call('HGET', default_lossless_param_keys[1], 'over_subscribe_ratio'))

-- Fetch the shared headroom pool size
local shp_size = tonumber(redis.call('HGET', 'BUFFER_POOL|ingress_lossless_pool', 'xoff'))

local shp_enabled = false
if over_subscribe_ratio ~= nil and over_subscribe_ratio ~= 0 then
    shp_enabled = true
end

if shp_size ~= nil and shp_size ~= 0 then
    shp_enabled = true
else
    shp_size = 0
end

-- Switch to APPL_DB
redis.call('SELECT', appl_db)

-- Fetch names of all profiles and insert them into the look up table
local all_profiles = redis.call('KEYS', 'BUFFER_PROFILE*')
for i = 1, #all_profiles, 1 do
    table.insert(profiles, {all_profiles[i], 0})
end

-- Fetch all the PGs
local all_pgs = redis.call('KEYS', 'BUFFER_PG*')
local all_tcs = redis.call('KEYS', 'BUFFER_QUEUE*')

local fail_count = 0
fail_count = fail_count + iterate_all_items(all_pgs)
fail_count = fail_count + iterate_all_items(all_tcs)
if fail_count > 0 then
    return {}
end

local statistics = {}

-- Fetch sizes of all of the profiles, accumulate them
local accumulative_occupied_buffer = 0
local accumulative_xoff = 0
for i = 1, #profiles, 1 do
    if profiles[i][1] ~= "BUFFER_PROFILE_TABLE_KEY_SET" and profiles[i][1] ~= "BUFFER_PROFILE_TABLE_DEL_SET" then
        local size = tonumber(redis.call('HGET', profiles[i][1], 'size'))
        if size ~= nil then 
            if profiles[i][1] == "BUFFER_PROFILE_TABLE:ingress_lossy_profile" then
                size = size + lossypg_reserved
            end
            if profiles[i][1] == "BUFFER_PROFILE_TABLE:egress_lossy_profile" then
                profiles[i][2] = total_port
            end
            if size ~= 0 then
                if shp_enabled and shp_size == 0 then
                    local xon = tonumber(redis.call('HGET', profiles[i][1], 'xon'))
                    local xoff = tonumber(redis.call('HGET', profiles[i][1], 'xoff'))
                    if xon ~= nil and xoff ~= nil and xon + xoff > size then
                        accumulative_xoff = accumulative_xoff + (xon + xoff - size) * profiles[i][2]
                    end
                end
                accumulative_occupied_buffer = accumulative_occupied_buffer + size * profiles[i][2]
            end
            table.insert(statistics, {profiles[i][1], size, profiles[i][2]})
        end
    end
end

-- Extra lossy xon buffer for 400G port
local lossypg_extra_for_400g = (lossypg_reserved_400g - lossypg_reserved) * lossypg_400g
accumulative_occupied_buffer = accumulative_occupied_buffer + lossypg_extra_for_400g

-- Accumulate sizes for management PGs
local accumulative_management_pg = (total_port - port_count_400g) * lossypg_reserved + port_count_400g * lossypg_reserved_400g
accumulative_occupied_buffer = accumulative_occupied_buffer + accumulative_management_pg

-- Accumulate sizes for egress mirror and management pool
local accumulative_egress_mirror_overhead = total_port * egress_mirror_headroom
accumulative_occupied_buffer = accumulative_occupied_buffer + accumulative_egress_mirror_overhead + mgmt_pool_size

-- Fetch mmu_size
redis.call('SELECT', state_db)
local mmu_size = tonumber(redis.call('HGET', 'BUFFER_MAX_PARAM_TABLE|global', 'mmu_size'))
if mmu_size == nil then
    mmu_size = tonumber(egress_lossless_pool_size)
end
local asic_keys = redis.call('KEYS', 'ASIC_TABLE*')
local cell_size = tonumber(redis.call('HGET', asic_keys[1], 'cell_size'))

-- Align mmu_size at cell size boundary, otherwise the sdk will complain and the syncd will fail
local number_of_cells = math.floor(mmu_size / cell_size)
local ceiling_mmu_size = number_of_cells * cell_size

-- Switch to CONFIG_DB
redis.call('SELECT', config_db)

-- Fetch all the pools that need update
local pools_need_update = {}
local ipools = redis.call('KEYS', 'BUFFER_POOL|ingress*')
local ingress_pool_count = 0
local ingress_lossless_pool_size = nil
for i = 1, #ipools, 1 do
    local size = tonumber(redis.call('HGET', ipools[i], 'size'))
    if not size then
        table.insert(pools_need_update, ipools[i])
        ingress_pool_count = ingress_pool_count + 1
    else
        if ipools[i] == 'BUFFER_POOL|ingress_lossless_pool' and shp_enabled and shp_size == 0 then
            ingress_lossless_pool_size = size
        end
    end
end

local epools = redis.call('KEYS', 'BUFFER_POOL|egress*')
for i = 1, #epools, 1 do
    local size = redis.call('HGET', epools[i], 'size')
    if not size then
        table.insert(pools_need_update, epools[i])
    end
end

if shp_enabled and shp_size == 0 then
    shp_size = math.ceil(accumulative_xoff / over_subscribe_ratio)
end

local pool_size
if shp_size then
    accumulative_occupied_buffer = accumulative_occupied_buffer + shp_size
end
if ingress_pool_count == 1 then
    pool_size = mmu_size - accumulative_occupied_buffer
else
    pool_size = (mmu_size - accumulative_occupied_buffer) / 2
end

if pool_size > ceiling_mmu_size then
    pool_size = ceiling_mmu_size
end

local shp_deployed = false
for i = 1, #pools_need_update, 1 do
    local pool_name = string.match(pools_need_update[i], "BUFFER_POOL|([^%s]+)$")
    if shp_size ~= 0 and pool_name == "ingress_lossless_pool" then
        table.insert(result, pool_name .. ":" .. math.ceil(pool_size) .. ":" .. math.ceil(shp_size))
        shp_deployed = true
    else
        table.insert(result, pool_name .. ":" .. math.ceil(pool_size))
    end
end

if not shp_deployed and shp_size ~= 0 and ingress_lossless_pool_size ~= nil then
    table.insert(result, "ingress_lossless_pool:" .. math.ceil(ingress_lossless_pool_size) .. ":" .. math.ceil(shp_size))
end

table.insert(result, "debug:mmu_size:" .. mmu_size)
table.insert(result, "debug:accumulative size:" .. accumulative_occupied_buffer)
for i = 1, #statistics do
    table.insert(result, "debug:" .. statistics[i][1] .. ":" .. statistics[i][2] .. ":" .. statistics[i][3])
end
table.insert(result, "debug:extra_400g:" .. (lossypg_reserved_400g - lossypg_reserved) .. ":" .. lossypg_400g .. ":" .. port_count_400g)
table.insert(result, "debug:mgmt_pool:" .. mgmt_pool_size)
table.insert(result, "debug:accumulative_mgmt_pg:" .. accumulative_management_pg)
table.insert(result, "debug:egress_mirror:" .. accumulative_egress_mirror_overhead)
table.insert(result, "debug:shp_enabled:" .. tostring(shp_enabled))
table.insert(result, "debug:shp_size:" .. shp_size)
table.insert(result, "debug:accumulative xoff:" .. accumulative_xoff)
table.insert(result, "debug:total port:" .. total_port)

return result
