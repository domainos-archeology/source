-- mame_boot_direct.lua - load our RFC image into the DN300's RAM the way
-- sysboot would, and start COLD_START.  Driven by mame_boot_direct.py
-- through environment variables:
--   DOS_IMAGE   the RFC image (dist/sau2/domain_os)
--   DOS_SYMS    "addr name" lines (m68k-elf-nm output) for naming PCs
--   DOS_STOPS   comma list of addresses to log a full register/stack dump at
--   DOS_PROGRESS comma list of addresses to log a one-line PROGRESS at
--   DOS_FLAGS   boot flags word (d2 low; default 8)
--   DOS_FRAMES  frames to run after entry before giving up (default 3000)
-- Needs -debug -debugger none (for bpset actions) and -video none.

local PAGE, LOAD, ENTRY, BOOT_SP = 0x400, 0x101400, 0x101424, 0x17CFE0
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local dbg = manager.machine.debugger

local function log(fmt, ...) print(string.format("BOOT " .. fmt, ...)) end

local syms = {}
for line in io.lines(os.getenv("DOS_SYMS")) do
  local a, n = line:match("^(%x+)%s+%S%s+(%S+)$")
  if a then syms[#syms + 1] = { tonumber(a, 16), n } end
end
table.sort(syms, function(x, y) return x[1] < y[1] end)
local function nearest(pc)
  local best
  for _, s in ipairs(syms) do
    if s[1] <= pc then best = s else break end
  end
  if not best or pc - best[1] > 0x10000 then return "?" end
  return string.format("%s+0x%X", best[2], pc - best[1])
end

local function split(s)
  local t = {}
  for v in (s or ""):gmatch("[^,]+") do t[#t + 1] = tonumber(v) end
  return t
end

local image = assert(io.open(os.getenv("DOS_IMAGE"), "rb")):read("a")
local info_split = string.unpack(">I4", image, 0x1C + 1)
local info_move = string.unpack(">I4", image, 0x20 + 1)
local flags = tonumber(os.getenv("DOS_FLAGS") or "8")
local run_frames = tonumber(os.getenv("DOS_FRAMES") or "3000")

local phase, frames, entered_at = "prom", 0, 0
local last_pc, same_count, history, rom_count = -1, 0, {}, 0

local function load_image()
  local pages = 0
  for i = 0, #image - 1, PAGE do
    local ppn = (LOAD + i) >> 10
    if ppn >= info_split then ppn = ppn - info_split + info_move end
    local phys = ppn << 10
    local chunk = image:sub(i + 1, i + PAGE)
    for j = 1, #chunk - 1, 2 do
      mem:write_u16(phys + j - 1, string.unpack(">I2", chunk, j))
    end
    if #chunk % 2 == 1 then mem:write_u8(phys + #chunk - 1, chunk:byte(#chunk)) end
    pages = pages + 1
  end
  -- verify two pages
  for _, off in ipairs({ 0, (#image - 1) // PAGE * PAGE }) do
    local ppn = (LOAD + off) >> 10
    if ppn >= info_split then ppn = ppn - info_split + info_move end
    for j = 0, 15 do
      local want = image:byte(off + j + 1)
      local got = mem:read_u8((ppn << 10) + j)
      if want ~= got then log("readback mismatch at file 0x%X: %02X vs %02X", off + j, want, got) end
    end
  end
  log("loaded %d pages (split PPN 0x%X -> 0x%X)", pages, info_split, info_move)
end

local function regdump_action(name)
  local regs = "pc=%08X sr=%04X sp=%08X d0=%08X d1=%08X d2=%08X d3=%08X d4=%08X d5=%08X d6=%08X d7=%08X a0=%08X a1=%08X a2=%08X a3=%08X a4=%08X a5=%08X a6=%08X"
  local args = "pc,sr,sp,d0,d1,d2,d3,d4,d5,d6,d7,a0,a1,a2,a3,a4,a5,a6"
  local stk, sargs = "", ""
  for i = 0, 15 do
    stk = stk .. " %08X"
    sargs = sargs .. string.format(",d@(sp+%d)", i * 4)
  end
  return string.format('{logerror "STOP %s %s stack:%s\\n",%s%s;go}', name, regs, stk, args, sargs)
end

local function exc_action()
  -- the raw exception frame (29 words: a 68010 bus/address error frame is
  -- sr, pc, format/vector, then the fault information) and the registers
  local regs = "d0=%08X d1=%08X d2=%08X d3=%08X d4=%08X d5=%08X d6=%08X d7=%08X a0=%08X a1=%08X a2=%08X a3=%08X a4=%08X a5=%08X a6=%08X"
  local args = "d0,d1,d2,d3,d4,d5,d6,d7,a0,a1,a2,a3,a4,a5,a6"
  local stk, sargs = "", ""
  for i = 0, 28 do
    stk = stk .. " %04X"
    sargs = sargs .. string.format(",w@(sp+%d)", i * 2)
  end
  return string.format('{logerror "EXC handler=%%08X sp=%%08X %s frame:%s\\n",pc,sp,%s%s;go}', regs, stk, args, sargs)
end

local function set_breakpoints()
  for _, a in ipairs(split(os.getenv("DOS_PROM_HANDLERS"))) do
    dbg:command(string.format("bpset 0x%X,1,%s", a, exc_action()))
  end
  for _, a in ipairs(split(os.getenv("DOS_STOPS"))) do
    local cmd = string.format("bpset 0x%X,1,%s", a, regdump_action(nearest(a)))
    log("bp stop %08X %s (%d chars)", a, nearest(a), #cmd)
    dbg:command(cmd)
  end
  for raw in (os.getenv("DOS_RAW_BPS") or ""):gmatch("[^|]+") do
    dbg:command("bpset " .. raw)
  end
  for _, a in ipairs(split(os.getenv("DOS_PROGRESS"))) do
    dbg:command(string.format('bpset 0x%X,1,{logerror "PROGRESS %s pc=%%08X sp=%%08X\\n",pc,sp;go}', a, nearest(a)))
  end
end


-- The PROM's "enter mapped mode" routine (DN300 boot PROM MD REV 5, ROM
-- 0x10BA..0x1130, called from its boot paths): copy the ROM vectors to the
-- RAM trap page 0x100400, clear the PTT, fill the PFT with self-linked
-- end-of-chain entries, walk the PROM's map table at ROM 0x11E0 with the
-- same insert algorithm COLD's cold_map_pages uses, and enable the MMU.
-- sysboot leaves these tables in place and re-enables the MMU before it
-- jumps to COLD (sysboot 0x17F668), so COLD's first MMU access (the word
-- at VA 0xFFB400, COLD 0x10154A) resolves to the register at 0x8000.
local PROM_MAP = {                     -- count, first PPN, first VA (ROM 0x11E0)
  { 0x0F, 0x001, 0x000400 },           -- PROM 0x400..0x3FFF one-to-one
  { 0x10, 0x010, 0xFFB800 },           -- PFT
  { 0x01, 0x020, 0xFFB400 },           -- MMU
  { 0x01, 0x021, 0xFFB000 },           -- SIO
  { 0x01, 0x022, 0xFFAC00 },           -- timers
  { 0x01, 0x027, 0xFFA800 },           -- disk / floppy / calendar
  { 0x01, 0x026, 0xFF9C00 },           -- ring
  { 0x01, 0x025, 0xFF9800 },           -- display 1
  { 0x01, 0x024, 0xFFA000 },           -- DMA
  { 0x80, 0x080, 0xFC0000 },           -- display memory
  { 0x02, 0x02C, 0xFF7000 },           -- FPU ctl / cmd
  { 0x03, 0x02E, 0xFF7800 },           -- FPU cs ..
  { 0x01, 0x400, 0xE00000 },           -- MD stack / data (phys 0x100000)
  { 0x01, 0x401, 0x000000 },           -- trap page (phys 0x100400)
  { 0x200, 0x200, 0x080000 },          -- phys 0x80000..0xFFFFF one-to-one
  { 0xBFE, 0x402, 0x100800 },          -- phys 0x100800..0x3FFFFF one-to-one
}
local PFT, PTT = 0x4000, 0x700000

local function prom_enter_mapped()
  for i = 0, 0x3FC, 4 do mem:write_u32(0x100400 + i, mem:read_u32(i)) end   -- 0x10BA..0x10CA
  mem:write_u8(0x100400, 0xFF)                                              -- 0x10CE
  mem:write_u16(0x8000, 2)                                                  -- 0x10D6 PTT access
  mem:write_u8(0x8005, 0x40)                                                -- 0x10DE
  for i = 0, 1023 do mem:write_u16(PTT + i * 0x400, 0) end                  -- 0x10E6..0x10FA
  for i = 0, 4095 do mem:write_u32(PFT + i * 4, 0xFE008000 | i) end         -- 0x10FC..0x110E
  for _, e in ipairs(PROM_MAP) do                                           -- 0x117A..0x11DE
    local count, ppn, va = e[1], e[2], e[3]
    for k = 0, count - 1 do
      local d3 = ((va >> 4) & 0xF0000) ~ 0x701000
      local slot = PTT + (va & 0xFFFFF)
      local head = mem:read_u16(slot) & 0xFFF
      if head == 0 then
        d3 = (d3 | 0x8000) + ppn                 -- end of chain, linked to itself
        mem:write_u16(slot, ppn)
      else
        local hent = mem:read_u32(PFT + head * 4)
        local d5 = hent & 0xFFF
        d3 = d3 ~ d5                             -- new entry links to the head's old link
        d5 = d5 ~ ppn
        mem:write_u32(PFT + head * 4, hent ~ d5) -- head links to the new entry
      end
      mem:write_u32(PFT + ppn * 4, d3)
      ppn = ppn + 1
      va = va + 0x400
    end
  end
  mem:write_u8(0x8005, 0x80)                                                -- 0x111A
  mem:write_u16(0x8000, 1)                                                  -- 0x1122 MMU on
  mem:write_u8(0x8005, 0)                                                   -- 0x112A (VA 0xFFB405)
  log("PROM map installed: pid/priv=%04X PTT[5]=%04X PFT[0x405]=%08X PFT[0x20]=%08X",
      mem:read_u16(0x8000), mem:read_u16(PTT + 5 * 0x400), mem:read_u32(PFT + 0x405 * 4), mem:read_u32(PFT + 0x20 * 4))
end

local function enter()
  load_image()
  prom_enter_mapped()
  -- the frame COLD_START pops (docs/rfc-cold-start.md section 5):
  -- return address, d1 = device/ctlr, d2 = unit/flags, a1, three spare longs
  local frame = string.pack(">I4I4I4I4I4I4I4", 0x17F6A8, 0, flags & 0xFFFF, 0, 0, 0, 0)
  for j = 1, #frame, 4 do mem:write_u32(BOOT_SP + j - 1, string.unpack(">I4", frame, j)) end
  if dbg then set_breakpoints() else log("no debugger: run with -debug -debugger none for breakpoints") end
  cpu.state["SR"].value = 0x2700
  cpu.state["SP"].value = BOOT_SP
  cpu.state["PC"].value = ENTRY
  log("entry pc=%08X sp=%08X sr=%04X", cpu.state["PC"].value, cpu.state["SP"].value, cpu.state["SR"].value)
end

local function finish(reason)
  log("finish: %s after %d frames", reason, frames - entered_at)
  local pc = cpu.state["PC"].value
  log("pc=%08X (%s) sr=%04X sp=%08X usp=%08X", pc, nearest(pc), cpu.state["SR"].value, cpu.state["SP"].value, cpu.state["USP"].value)
  for i = 0, 7 do io.write(string.format("D%d=%08X ", i, cpu.state["D" .. i].value)) end
  print()
  for i = 0, 7 do io.write(string.format("A%d=%08X ", i, cpu.state["A" .. i].value)) end
  print()
  local hist = {}
  for i = math.max(1, #history - 40), #history do hist[#hist + 1] = string.format("%X", history[i]) end
  log("pc per frame (last %d): %s", #hist, table.concat(hist, " "))
  -- DOS_DUMP: "phys:len,..." physical ranges to print as words at the end
  for spec in (os.getenv("DOS_DUMP") or ""):gmatch("[^,]+") do
    local a, n = spec:match("^(%x+):(%x+)$")
    if a then
      a, n = tonumber(a, 16), tonumber(n, 16)
      local words = {}
      for i = 0, n - 2, 2 do words[#words + 1] = string.format("%04X", mem:read_u16(a + i)) end
      log("dump %08X: %s", a, table.concat(words, " "))
    end
  end
  manager.machine:exit()
end

emu.register_frame_done(function()
  frames = frames + 1
  if phase == "prom" then
    if frames == 30 then
      log("PROM parked at pc=%08X sp=%08X", cpu.state["PC"].value, cpu.state["SP"].value)
      enter()
      phase = "run"
      entered_at = frames
    end
    return
  end
  local pc = cpu.state["PC"].value
  history[#history + 1] = pc
  if pc == last_pc then same_count = same_count + 1 else same_count = 0 end
  last_pc = pc
  if pc < 0x4000 then rom_count = rom_count + 1 else rom_count = 0 end
  if rom_count >= 30 then finish(string.format("fell into the PROM (pc=%08X)", pc)) end
  if same_count >= 120 then finish(string.format("pc stuck at %08X (%s)", pc, nearest(pc))) end
  if frames - entered_at >= run_frames then finish("frame budget exhausted") end
end)
