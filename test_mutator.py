from libsnippet import Instruction, Snippet, Inst_Types, Mut_Types, Op_Types, dump
import unittest
import subprocess
import os
import random
import capstone
import time

class TestInstructions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.check_call("make clean; make corpus;", shell=True)

        # Force the library to build again?
        import libsnippet

        with open(os.path.join("corpus", "test"), "rb") as f:
            cls.test_text = f.read()

        random.seed(10)

    def test_jmps(self):
        with open(os.path.join("corpus", "test3"), "rb") as f:
            test3_text = f.read()
        
        snip = Snippet(text=test3_text)
        snip.mutate(Mut_Types.MUT_REPLACE_ARG)
        snip.mutate(Mut_Types.MUT_REPLACE_ARG)
        snip.mutate(Mut_Types.MUT_REPLACE_ARG)
        snip.mutate(Mut_Types.MUT_REPLACE_ARG)
        test = snip.encode()
        snip = Snippet(text=bytes(test))

    def test_corpus(self):
        with open(os.path.join("corpus", "option2_1_9"), "rb") as f:
            test_text = f.read()

        snip = Snippet(text=test_text)
        snip.print()
        snip.remove(6)
        snip.remove(6)
        snip.remove(6)
        snip.print()
        test = snip.encode()
        snip = Snippet(text=bytes(test))
        snip.print()
    
    def test_simple(self):
        snip = Snippet(text=self.test_text)
        
        snip.insert_new(Inst_Types.INS_ADD, 0)

    # TODO: Maybe a more substantial test here, but oh well
    def test_decode(self):
        snip = Snippet(text=self.test_text)

        self.assertEqual(snip.num_inst, 3)

        for i in range(snip.num_inst):
            print(snip.text[i].mnemonic)
            if i == 2:
                self.assertEqual(snip.text[i].mnemonic.value, Inst_Types.INS_RET.value)
            else:
                self.assertEqual(snip.text[i].mnemonic.value, Inst_Types.INS_NOP.value)

    def test_create(self):
        buf = Instruction()

        # Check that all instructions can be created and dont generate errors
        for i, ins_type in enumerate(Inst_Types):
            if ins_type == Inst_Types.INS_NONE:
                continue
            buf.create(ins_type)
            self.assertEqual(buf.mnemonic.value, ins_type.value)

    def test_insert(self):
        snip = Snippet()
        buf = [Instruction() for i in range(len(Inst_Types))]
        num_insert = min(len(Inst_Types), snip.max_len)
        prev_ins = 0

        for i, ins_type in enumerate(Inst_Types):
            buf[i].create(ins_type)

        self.assertEqual(snip.num_inst, 0)

        # Insert all instructions into a program randomly
        pos = 0
        for i in range(num_insert):
            if snip.num_inst > 0:
                pos = random.randrange(0, snip.num_inst)
            
            if snip.num_inst > 0:
                prev_ins = snip.text[pos].mnemonic
            
            snip.insert(buf[i], pos)
            self.assertEqual(snip.text[pos].mnemonic.value, buf[i].mnemonic.value)
            
            if snip.num_inst > 1 and pos < snip.num_inst:
                self.assertEqual(snip.text[pos+1].mnemonic.value, prev_ins.value)

        self.assertEqual(snip.num_inst, num_insert)
        
    def test_remove(self):
        snip = Snippet()
        prev_ins = 0
        
        pos = 0
        for i, ins_type in enumerate(Inst_Types):
            if ins_type == Inst_Types.INS_NONE:
                continue

            if snip.num_inst > 0:
                pos = random.randrange(0, snip.num_inst)
            
            snip.insert_new(ins_type, pos)

        for i in range(snip.num_inst):
            pos = random.randrange(0, snip.num_inst)

            if pos < (snip.num_inst-1):
                prev_ins = snip.text[pos+1].mnemonic

            snip.remove(pos)
            
            if snip.num_inst > 0 and pos < snip.num_inst:
                self.assertEqual(snip.text[pos].mnemonic.value, prev_ins.value)
        
        self.assertEqual(snip.num_inst, 0)

    def test_encode(self):
        snip = Snippet()
        
        pos = 0
        for i, ins_type in enumerate(Inst_Types):
            if ins_type == Inst_Types.INS_NONE:
                continue

            if snip.num_inst > 0:
                pos = random.randrange(0, snip.num_inst)
            
            snip.insert_new(ins_type, pos)
        
        snip.remove(0)
        snip.insert_new(Inst_Types.INS_NOP, 0)
        
        text = snip.encode()

        self.assertEqual(text[0], 0x90)
    
    def test_mut_add(self):
        snip = Snippet(text=self.test_text)
        
        self.assertEqual(snip.num_inst, 3)

        for i in range(1, 3):
            snip.mutate(Mut_Types.MUT_ADD)
            self.assertEqual(snip.num_inst, 3 + i)

    def test_mut_remove(self):
        snip = Snippet(text=self.test_text)
        
        self.assertEqual(snip.num_inst, 3)

        for i in range(1, 3):
            snip.mutate(Mut_Types.MUT_REMOVE)
            self.assertEqual(snip.num_inst, 3 - i)    

    def test_mut_replace(self):
        # TODO: Change replace to not select the same instruction again
        snip = Snippet()
        snip.create(Inst_Types.INS_NOP, 0)

        self.assertEqual(snip.num_inst, 1)
        self.assertEqual(snip.text[0].mnemonic, Inst_Types.INS_NOP)

        snip.mutate(Mut_Types.MUT_REPLACE)

        self.assertNotEqual(snip.text[0].mnemonic, Inst_Types.INS_NOP)
        self.assertEqual(snip.num_inst, 1)

    def test_mut_replace_nop(self):
        snip = Snippet()
        snip.create(Inst_Types.INS_ADD, 0)

        self.assertEqual(snip.num_inst, 1)
        self.assertEqual(snip.text[0].mnemonic, Inst_Types.INS_ADD)

        snip.mutate(Mut_Types.MUT_REPLACE_NOP)

        self.assertEqual(snip.text[0].mnemonic, Inst_Types.INS_NOP)
        self.assertEqual(snip.num_inst, 1)

    def test_mut_replace_arg(self):
        snip = Snippet()
        original = Instruction()
        
        snip.create(Inst_Types.INS_NOP, 0)

        for ins_type in Inst_Types:
            if ins_type == Inst_Types.INS_NONE:
                continue

            original.create(ins_type)
            snip.replace(original, 0)
            
            self.assertEqual(snip.num_inst, 1)
            self.assertEqual(snip.text[0], original)
            snip.mutate(Mut_Types.MUT_REPLACE_ARG)
            
            self.assertEqual(snip.num_inst, 1)
            
            if snip.text[0].operand_count > 0:
                self.assertNotEqual(snip.text[0], original)
        
    def test_mut_swap(self):
        snip = Snippet()
        ins1 = Instruction().create(Inst_Types.INS_ADD)
        ins2 = Instruction().create(Inst_Types.INS_SUB)
        
        snip.insert(ins1, 0)
        snip.insert(ins2, 1)

        self.assertEqual(snip.num_inst, 2)
        snip.mutate(Mut_Types.MUT_SWAP)
        self.assertEqual(snip.text[0], ins2)
        self.assertEqual(snip.text[1], ins1)
        self.assertEqual(snip.num_inst, 2)
        
    def test_mut_repeat(self):
        snip = Snippet()
        snip.create(Inst_Types.INS_ADD, 0)

        self.assertEqual(snip.num_inst, 1)

        snip.mutate(Mut_Types.MUT_REPEAT)

        self.assertEqual(snip.num_inst, 2)
        self.assertEqual(snip.text[0], snip.text[1])

    def test_swap(self):
        snip = Snippet()

        for i in range(snip.max_len):
            snip.insert_new(Inst_Types.INS_ADD, i)

        snip.replace(Instruction(inst_type=Inst_Types.INS_RET), snip.num_inst-2)
        snip.print()
        snip.swap(snip.num_inst-1, snip.num_inst-2)
        print(f"Swapped {snip.num_inst-2} and {snip.num_inst-1}")
        snip.print()

    @unittest.skip("Not Implemented")
    def test_mut_snippet(self):
        pass

def test_mutation(text, ensure_return):
    ADDATIVE_METHODS = [Mut_Types.MUT_ADD, Mut_Types.MUT_ADD, Mut_Types.MUT_REPEAT, Mut_Types.MUT_REPEAT]
    DECREASE_METHODS = [Mut_Types.MUT_REMOVE]
    MODIFY_METHODS   = [Mut_Types.MUT_SWAP, Mut_Types.MUT_REPLACE, Mut_Types.MUT_REPLACE_NOP, Mut_Types.MUT_REPLACE_ARG]
    
    prev_snip = None
    snip = Snippet(text=text)
    snip.print()
    
    last_print = ""
    tested = 0
    count = 0
    while 1:
        mutations = MODIFY_METHODS + DECREASE_METHODS
    
        if snip.num_inst != snip.max_len:
            mutations += ADDATIVE_METHODS

        if snip.num_inst == 1:
            mutations = ADDATIVE_METHODS

        choice = random.choice(mutations)
        snip = snip.mutate(choice)

        if ensure_return and not snip.has_return():
            for i in reversed(range(snip.num_inst - 1)):
                if snip.text[i].mnemonic == Inst_Types.INS_RET:
                    snip.swap(snip.num_inst-1, i)
                    break

            # Re insert the return, so its always the last instruction
            if snip.text[snip.num_inst - 1].mnemonic != Inst_Types.INS_RET:
                if snip.num_inst < snip.max_len:
                    snip.insert_new(Inst_Types.INS_RET, snip.num_inst)
                else:
                    snip.replace(Instruction(inst_type=Inst_Types.INS_RET), snip.num_inst - 1)

        else:
            # Trim off any remaining returns
            while snip.text[snip.num_inst-2].mnemonic == Inst_Types.INS_RET:
                snip.remove(snip.num_inst - 1)

        if snip.text[snip.num_inst-1].mnemonic == Inst_Types.INS_RET \
           and snip.text[snip.num_inst-2].mnemonic == Inst_Types.INS_RET: 
            print(f"The last mutation was {choice}, variation {tested}")
            prev_snip.print()
            snip.print()
            exit(1)

        new_bytes = snip.encode()
        
        # Check that the jump destinations are sane
        # md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        # md.detail = True
        # idx = -1
        # for ins in md.disasm(new_bytes, snip.rt_address):
        #     idx += 1
        #     if not ins.group(capstone.CS_GRP_JUMP):
        #         continue
        #     
        #     if not ins.operands[0].type == capstone.CS_OP_IMM:
        #         continue
        #
        #     dest = ins.operands[0].imm
        #
        #     #print(f"Jump @ ({idx}) to {hex(dest)} ({snip.text[idx].jump_pos})")
        #
        #     if dest < snip.rt_address:
        #         if prev_snip:
        #             prev_snip.print()
        #         snip.print()
        #         print(f"The last mutation was {choice}")
        #         print(f"The size of the current instruction is: {ins.size}")
        #         print(dump(snip.text[idx].obj.req))
        #         exit(1)

        tested += 1
        if tested % 1000 == 0:
            print(f"Currently tested {tested} variations")
        
        snip = Snippet(text=bytes(new_bytes))
        prev_snip = Snippet(text=bytes(new_bytes))

        #snip.print()
        #time.sleep(1)

    assert(len(new_bytes) < max_size)

if __name__ == "__main__":
    #unittest.main(verbosity=2)
    
    with open(os.path.join("corpus", "not_taken_conditional"), "rb") as f:
        test3_text = f.read()

    test_mutation(test3_text, 1)

