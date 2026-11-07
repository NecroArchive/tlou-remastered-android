"""Execute the production x86 patch; do not merely model its desired predicate."""
import struct
import unittest
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import tlou_patches
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE, UC_HOOK_MEM_READ
from unicorn.x86_const import UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RSP, UC_X86_REG_RBX, UC_X86_REG_R12

GROUP=tlou_patches.BASE_PATCHES['static-ray-hit-submesh-bounds']
WRITES={at:(bytes.fromhex(old),bytes.fromhex(new)) for at,old,new in GROUP if new}
START=0xF75162
VALID=0xF751B6
NEXT=0xF75410
ASSERT=0xF75183

class StaticRayHitBounds(unittest.TestCase):
    def execute(self,count,index,patched):
        cpu=Uc(UC_ARCH_X86,UC_MODE_64)
        cpu.mem_map(0xF75000,0x1000)
        cpu.mem_map(0x200000,0x2000)
        cpu.mem_map(0x300000,0x10000)
        # Real count load, zero/empty test, signed hit-index load and branch.
        code=bytes.fromhex('428b8423f002000085c00f849e0200004b638c35dc0d000048894c241839c87f33e81865b3ff')
        cpu.mem_write(START,code)
        # Real valid-hit address computation, followed by a sentinel memory read.
        body=bytes.fromhex('4869442418900300004a038423f80200008b00')
        cpu.mem_write(VALID,body)
        if patched:
            for at,(_,new) in WRITES.items():cpu.mem_write(at,new)
        cpu.reg_write(UC_X86_REG_RBX,0x200000)
        cpu.reg_write(UC_X86_REG_R12,0)
        from unicorn.x86_const import UC_X86_REG_R13, UC_X86_REG_R14
        cpu.reg_write(UC_X86_REG_R13,0x200000)
        cpu.reg_write(UC_X86_REG_R14,0)
        cpu.reg_write(UC_X86_REG_RSP,0x201F00)
        cpu.mem_write(0x2002F0,struct.pack('<I',count&0xffffffff))
        cpu.mem_write(0x200DDC,struct.pack('<I',index&0xffffffff))
        cpu.mem_write(0x2002F8,struct.pack('<Q',0x300000))
        cpu.mem_write(0x300000,bytes([0x5a])*0x10000)
        events=[]; reads=[]
        def step(uc,address,size,user):
            if address in (NEXT,VALID+len(body)) or (address==ASSERT and not patched):
                events.append('skip' if address==NEXT else 'assert' if address==ASSERT else 'valid')
                uc.emu_stop()
        cpu.hook_add(UC_HOOK_CODE,step)
        cpu.hook_add(UC_HOOK_MEM_READ,lambda uc,kind,addr,size,value,user:reads.append((addr,size)))
        cpu.emu_start(START,0xF75500,count=25)
        self.assertEqual(len(events),1)
        return events[0],reads,cpu.reg_read(UC_X86_REG_RAX)

    def test_valid_hits_keep_original_address_and_reads(self):
        for count in [1,2,8,16]:
            for index in range(count):
                self.assertEqual(self.execute(count,index,False),self.execute(count,index,True))
                state,reads,_=self.execute(count,index,True)
                self.assertEqual(state,'valid')
                self.assertIn((0x300000+index*0x390,4),reads)

    def test_invalid_hit_skips_before_submesh_load(self):
        for count in [0,1,2,8,16]:
            for index in [count,count+1,100000,-1,-2147483648]:
                state,reads,_=self.execute(count,index,True)
                self.assertEqual(state,'skip',(count,index))
                self.assertFalse(any(0x300000<=a<0x310000 for a,_ in reads))

    def test_original_failure_is_reproduced_not_just_suppressed(self):
        self.assertEqual(self.execute(2,2,False)[0],'assert')
        self.assertEqual(self.execute(2,2,True)[0],'skip')
        self.assertEqual(self.execute(-1,0,True)[0],'skip')

if __name__=='__main__':unittest.main()
