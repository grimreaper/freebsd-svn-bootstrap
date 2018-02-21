--
-- Copyright (c) 2015 Pedro Souza <pedrosouza@freebsd.org>
-- All rights reserved.
--
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions
-- are met:
-- 1. Redistributions of source code must retain the above copyright
--    notice, this list of conditions and the following disclaimer.
-- 2. Redistributions in binary form must reproduce the above copyright
--    notice, this list of conditions and the following disclaimer in the
--    documentation and/or other materials provided with the distribution.
--
-- THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
-- ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
-- ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
-- FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
-- DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
-- OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
-- HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
-- LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
-- OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
-- SUCH DAMAGE.
--
-- $FreeBSD$
--

local config = require('config')

local core = {}

local compose_loader_cmd = function(cmd_name, argstr)
	if argstr ~= nil then
		cmd_name = cmd_name .. " " .. argstr
	end
	return cmd_name
end

-- Internal function
-- Parses arguments to boot and returns two values: kernel_name, argstr
-- Defaults to nil and "" respectively.
-- This will also parse arguments to autoboot, but the with_kernel argument
-- will need to be explicitly overwritten to false
local parse_boot_args = function(argv, with_kernel)
	if #argv == 0 then
		return nil, ""
	end
	if with_kernel == nil then
		with_kernel = true
	end
	local kernel_name
	local argstr = ""

	for k, v in ipairs(argv) do
		if with_kernel and v:sub(1,1) ~= "-" then
			kernel_name = v
		else
			argstr = argstr .. " " .. v
		end
	end
	if with_kernel then
		return kernel_name, argstr
	else
		return argstr
	end
end

-- Globals
function boot(...)
	local argv = {...}
	local cmd_name = ""
	cmd_name, argv = core.popFrontTable(argv)
	local kernel, argstr = parse_boot_args(argv)
	if kernel ~= nil then
		loader.perform("unload")
		config.selectkernel(kernel)
	end
	core.boot(argstr)
end

function autoboot(...)
	local argv = {...}
	local cmd_name = ""
	cmd_name, argv = core.popFrontTable(argv)
	local argstr = parse_boot_args(argv, false)
	core.autoboot(argstr)
end

-- Module exports
-- Commonly appearing constants
core.KEY_BACKSPACE	= 8
core.KEY_ENTER		= 13
core.KEY_DELETE		= 127

core.KEYSTR_ESCAPE	= "\027"

core.MENU_RETURN	= "return"
core.MENU_ENTRY		= "entry"
core.MENU_SEPARATOR	= "separator"
core.MENU_SUBMENU	= "submenu"
core.MENU_CAROUSEL_ENTRY	= "carousel_entry"

function core.setVerbose(b)
	if b == nil then
		b = not core.verbose
	end

	if b == true then
		loader.setenv("boot_verbose", "YES")
	else
		loader.unsetenv("boot_verbose")
	end
	core.verbose = b
end

function core.setSingleUser(b)
	if b == nil then
		b = not core.su
	end

	if b == true then
		loader.setenv("boot_single", "YES")
	else
		loader.unsetenv("boot_single")
	end
	core.su = b
end

function core.getACPIPresent(checkingSystemDefaults)
	local c = loader.getenv("hint.acpi.0.rsdp")

	if c ~= nil then
		if checkingSystemDefaults == true then
			return true
		end
		-- Otherwise, respect disabled if it's set
		c = loader.getenv("hint.acpi.0.disabled")
		return c == nil or tonumber(c) ~= 1
	end
	return false
end

function core.setACPI(b)
	if b == nil then
		b = not core.acpi
	end

	if b == true then
		loader.setenv("acpi_load", "YES")
		loader.setenv("hint.acpi.0.disabled", "0")
		loader.unsetenv("loader.acpi_disabled_by_user")
	else
		loader.unsetenv("acpi_load")
		loader.setenv("hint.acpi.0.disabled", "1")
		loader.setenv("loader.acpi_disabled_by_user", "1")
	end
	core.acpi = b
end

function core.setSafeMode(b)
	if b == nil then
		b = not core.sm
	end
	if b == true then
		loader.setenv("kern.smp.disabled", "1")
		loader.setenv("hw.ata.ata_dma", "0")
		loader.setenv("hw.ata.atapi_dma", "0")
		loader.setenv("hw.ata.wc", "0")
		loader.setenv("hw.eisa_slots", "0")
		loader.setenv("kern.eventtimer.periodic", "1")
		loader.setenv("kern.geom.part.check_integrity", "0")
	else
		loader.unsetenv("kern.smp.disabled")
		loader.unsetenv("hw.ata.ata_dma")
		loader.unsetenv("hw.ata.atapi_dma")
		loader.unsetenv("hw.ata.wc")
		loader.unsetenv("hw.eisa_slots")
		loader.unsetenv("kern.eventtimer.periodic")
		loader.unsetenv("kern.geom.part.check_integrity")
	end
	core.sm = b
end

function core.kernelList()
	local k = loader.getenv("kernel")
	local v = loader.getenv("kernels") or ""

	local kernels = {}
	local unique = {}
	local i = 0
	if k ~= nil then
		i = i + 1
		kernels[i] = k
		unique[k] = true
	end

	for n in v:gmatch("([^; ]+)[; ]?") do
		if unique[n] == nil then
			i = i + 1
			kernels[i] = n
			unique[n] = true
		end
	end

	-- Automatically detect other bootable kernel directories using a
	-- heuristic.  Any directory in /boot that contains an ordinary file
	-- named "kernel" is considered eligible.
	for file in lfs.dir("/boot") do
		local fname = "/boot/" .. file

		if file == "." or file == ".." then
			goto continue
		end

		if lfs.attributes(fname, "mode") ~= "directory" then
			goto continue
		end

		if lfs.attributes(fname .. "/kernel", "mode") ~= "file" then
			goto continue
		end

		if unique[file] == nil then
			i = i + 1
			kernels[i] = file
			unique[file] = true
		end

		::continue::
	end
	return kernels
end

function core.setDefaults()
	core.setACPI(core.getACPIPresent(true))
	core.setSafeMode(false)
	core.setSingleUser(false)
	core.setVerbose(false)
end

function core.autoboot(argstr)
	config.loadelf()
	loader.perform(compose_loader_cmd("autoboot", argstr))
end

function core.boot(argstr)
	config.loadelf()
	loader.perform(compose_loader_cmd("boot", argstr))
end

function core.isSingleUserBoot()
	local single_user = loader.getenv("boot_single")
	return single_user ~= nil and single_user:lower() == "yes"
end

function core.isSerialBoot()
	local c = loader.getenv("console")

	if c ~= nil then
		if c:find("comconsole") ~= nil then
			return true
		end
	end

	local s = loader.getenv("boot_serial")
	if s ~= nil then
		return true
	end

	local m = loader.getenv("boot_multicons")
	if m ~= nil then
		return true
	end
	return false
end

function core.isSystem386()
	return loader.machine_arch == "i386"
end

-- This may be a better candidate for a 'utility' module.
function core.shallowCopyTable(tbl)
	local new_tbl = {}
	for k, v in pairs(tbl) do
		if type(v) == "table" then
			new_tbl[k] = core.shallowCopyTable(v)
		else
			new_tbl[k] = v
		end
	end
	return new_tbl
end

-- XXX This should go away if we get the table lib into shape for importing.
-- As of now, it requires some 'os' functions, so we'll implement this in lua
-- for our uses
function core.popFrontTable(tbl)
	-- Shouldn't reasonably happen
	if #tbl == 0 then
		return nil, nil
	elseif #tbl == 1 then
		return tbl[1], {}
	end

	local first_value = tbl[1]
	local new_tbl = {}
	-- This is not a cheap operation
	for k, v in ipairs(tbl) do
		if k > 1 then
			new_tbl[k - 1] = v
		end
	end

	return first_value, new_tbl
end

-- On i386, hint.acpi.0.rsdp will be set before we're loaded. On !i386, it will
-- generally be set upon execution of the kernel. Because of this, we can't (or
-- don't really want to) detect/disable ACPI on !i386 reliably. Just set it
-- enabled if we detect it and leave well enough alone if we don't.
if core.isSystem386() and core.getACPIPresent(false) then
	core.setACPI(true)
end
return core
