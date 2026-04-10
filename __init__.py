from __future__ import annotations
from . import build_library # This should build the needed shared library
from ._libsnippet import ffi, lib
from enum import Enum
import sys

# TODO: These need to match the maximum case size for AFL
MAXIMUM_TEXT_SIZE = 4096
MAX_SNIPPET_SIZE = lib.MAX_SNIPPET_SIZE

class Op_Types(Enum):
    OP_NONE         = 0
    OP_MEM          = lib.ZYDIS_OPERAND_TYPE_MEMORY
    OP_PTR          = lib.ZYDIS_OPERAND_TYPE_POINTER
    OP_REG          = lib.ZYDIS_OPERAND_TYPE_REGISTER
    OP_IMM          = lib.ZYDIS_OPERAND_TYPE_IMMEDIATE
    OP_UNU          = lib.ZYDIS_OPERAND_TYPE_UNUSED

class Mut_Types(Enum):
    MUT_ADD_DET     = lib.MUT_ADD_DET
    MUT_ADD_RAND    = lib.MUT_ADD_RAND
    MUT_REPLACE     = lib.MUT_REPLACE
    MUT_REPLACE_NOP = lib.MUT_REPLACE_NOP
    MUT_REPLACE_ARG = lib.MUT_REPLACE_ARGS
    MUT_REPEAT      = lib.MUT_REPEAT
    MUT_SWAP        = lib.MUT_SWAP
    MUT_REMOVE      = lib.MUT_REMOVE
    MUT_SNIPPET     = lib.MUT_SNIPPET
    MUT_ADD_VZEROUPPER = lib.MUT_ADD_VZEROUPPER
    MUT_ADD_JMP     = lib.MUT_ADD_JMP

class Gen_Types(Enum):
    METHOD_GENERATE = lib.METHOD_GENERATE
    METHOD_CREATE   = lib.METHOD_CREATE
    METHOD_MIXED    = lib.METHOD_MIXED
    METHOD_NONE     = lib.METHOD_NONE

class Fuzz_Types(Enum):
    FUZZ_MUTATIVE   = lib.FUZZ_MUTATIVE
    FUZZ_GENERATIVE = lib.FUZZ_GENERATIVE
    FUZZ_HYBRID     = lib.FUZZ_HYBRID
    
    def __str__(self):
        return zydis_enum_str("FUZZ_")(self)

class Norm_Types(Enum):
    NORM_DIRECT = lib.NORM_DIRECT
    NORM_64_BYTE = lib.NORM_64_BYTE
    NORM_32_BYTE = lib.NORM_32_BYTE
    NORM_16_BYTE = lib.NORM_16_BYTE
    NORM_8_BYTE = lib.NORM_8_BYTE

    def __str__(self):
        return zydis_enum_str("NORM_")(self)

def create_enum_dict(name: str):
    enum_values = {}
    for el in dir(lib):
        if not el.startswith(name):
            continue
        
        enum_values[el] = getattr(lib, el)
    
    return enum_values

Inst_Types = Enum("Inst_Types", create_enum_dict("ZYDIS_MNEMONIC_"))
Category_Types = Enum("Category_Types", create_enum_dict("ZYDIS_CATEGORY_"))

def zydis_enum_str(prefix):
    return lambda self: self.name.replace(prefix, "")

Inst_Types.__str__ = zydis_enum_str("ZYDIS_MNEMONIC_")
Category_Types.__str__ = zydis_enum_str("ZYDIS_CATEGORY_")

def get_enum_name(name: str):
    for el in dir(lib):
        if el.startswith(name):
            print(f'[{el}] = "{el}",')

def is_jump(ins: Instruction):
    return ffi.is_jump(ins.mnemonic)

def reg_to_str(reg):
    return ffi.string(lib.ZydisRegisterGetString(reg)).decode("utf-8")

def mnemonic_to_str(mnemonic):
    return ffi.string(lib.ZydisMnemonicGetString(mnemonic)).decode("utf-8")

def dump(struct):
    s = f"{ffi.typeof(struct)}: ("
    for field in dir(struct):
        data = struct.__getattribute__(field)
        if str(data).startswith("<cdata"):
            data = dump(data)
        s = s + f"{field}:{data} "
    s = s + ")"
    return s

class InstructionIndex():
    def __init__(self, obj=None):
        if obj:
            self.obj = obj
        else:
            self.obj = ffi.new("generation_index_t *")
    
    @property
    def iterations(self):
        return self.obj.iterations
    
    @property
    def count(self):
        return self.obj.count

    @property
    def threshold(self):
        return self.obj.threshold
    
    def get_element(self, i: int):
        return self.obj.instructions[i]

class Config:
    def __init__(self, obj=None, index_iters=None, config_file=None):
        if obj:
            self.obj = obj 
        else:
            self.obj = ffi.new("struct config_t *")
        
        if config_file:
            if lib.import_config(self.obj, ffi.new("char[]", config_file.encode("utf-8"))) != 0:
                raise RuntimeError(f"Could not import config @ {config_file}")
        else:
            if lib.default_config(self.obj) != 0:
                raise RuntimeError("Could not create default config")
        
        if index_iters:
            self.obj.index.iterations = index_iters
        
        if lib.create_index(self.obj) != 0:
            raise RuntimeError("Could not create instruction index")

    @property
    def fuzz_type(self):
        return Fuzz_Types(self.obj.fuzz_method)
    
    @property
    def norm_type(self):
        return Norm_Types(self.obj.encode_method)

    @property
    def index(self):
        return InstructionIndex(self.obj.index)

class Operand:
    def __init__(self, obj):
        if obj:
            self.obj = obj
        else:
            self.obj = ffi.new("ZydisEncoderOperand *")
    
    def __eq__(self, other):
        if not isinstance(other, Operand):
            return NotImplemented

        return self.op_type == other.op_type \
               and self.value == other.value \
    
    @property
    def op_type(self) -> Op_Types:
        return Op_Types(self.obj.type)
    
    @property
    def value(self) -> dict:
        match (self.op_type):
            case Op_Types.OP_MEM:
                return {"base": reg_to_str(self.obj.mem.base), 
                        "disp": self.obj.mem.displacement,
                        "scale": self.obj.mem.scale,
                        "index": reg_to_str(self.obj.mem.index),
                        "size": self.obj.mem.scale}
            case Op_Types.OP_REG:
                return {"reg": reg_to_str(self.obj.reg.value)}
            case Op_Types.OP_IMM:
                return {"signed": self.obj.imm.s, 
                        "unsigned": self.obj.imm.u}
            case Op_Types.OP_NONE:
                return None
            case _:
                raise Exception(f"Invalid Operand Type: {self.op_type}")

class Instruction:
    def __init__(self, obj=None, inst_type=None):
        if obj:
            self.obj = obj
        else:
            self.obj = ffi.new("instruction_t *")
               
        self.operands = [Operand(ffi.addressof(self.obj.req.operands, i)) for i in range(0, 5)]
        
        # Easy contructor
        if inst_type:
            self.create(inst_type)
    
    def __eq__(self, other):
        if not isinstance(other, Instruction):
            return NotImplemented

        return self.mnemonic.value == other.mnemonic.value      \
               and self.operand_count == other.operand_count    \
               and self.jump_pos == other.jump_pos              \
               and self.operands == other.operands
    
    def __str__(self):
        dis = ffi.new("ZydisDisassembledInstruction *")
        sz = ffi.new("size_t *")
        buf = ffi.new("ZyanU8[16]")
        
        sz[0] = 16
        lib.ZydisEncoderEncodeInstruction(ffi.addressof(self.obj.req), buf, sz)
        lib.ZydisDisassembleIntel(lib.ZYDIS_MACHINE_MODE_LONG_64, 0, buf, 16, dis)
        
        return ffi.string(dis.text).decode("utf-8")

    def bytes(self):
        sz = ffi.new("size_t *")
        buf = ffi.new("ZyanU8[16]")
        
        sz[0] = 16
        lib.ZydisEncoderEncodeInstruction(ffi.addressof(self.obj.req), buf, sz)
        
        return bytearray(ffi.buffer(buf, sz[0]))

    @property
    def jump_target(self) -> Instruction:
        return Instruction(obj=self.obj.jump_target)

    @property
    def target_addrs(self) -> Int:
        return self.obj.target_addrs 
    
    @property
    def length(self) -> Int:
        return self.obj.length 
    
    @property
    def idx(self) -> Int:
        return self.obj.idx 
    
    @property
    def privileged(self) -> Bool:
        return self.obj.privileged
    
    @property
    def address(self) -> Int:
        return self.obj.address 
    
    @property
    def mnemonic(self) -> Inst_Types:
        return Inst_Types(self.obj.req.mnemonic)
    
    @property
    def operand_count(self) -> int:
        return self.obj.req.operand_count

    @property
    def category(self) -> Category_Types:
        cat_str = ffi.string(lib.ZydisCategoryGetString(lib.get_category(self.obj))).decode("utf-8")
        return Category_Types[f"ZYDIS_CATEGORY_{cat_str}"]


    def create_random(self, cfg) -> Instruction:
        ret = lib.create_random_instruction(cfg, self.obj)

        if ret < 0:
            raise RuntimeError("Could not generate instruction")

        return self

    def create(self, cfg: Config, mnemonic: Zydis_Mnemonics, operands: list(Op_Types)):
        if len(operands) > 0:
            ops = ffi.new(f"ZydisEncoderOperand[{len(operands)}]", list(map(lambda x: x.value, operands)))
        else:
            ops = ffi.NULL
        
        ret = lib.create_instruction(cfg, self.obj, mnemonic.value, ops)
        if ret < 0:
            raise RuntimeError(f"Could not create instruction: {mnemonic_to_str(mnemonic.value)}")

        return self

    def replace(self, ins: Instruction) -> Instruction:
        ffi.memmove(self.obj, ins.obj, ffi.sizeof("instruction"))

        return self

class Snippet:
    def __init__(self, obj=None, text=None, cfg=None):
        self.max_len = MAX_SNIPPET_SIZE # TODO: These are pretty much included in snippet_t now
        self.code_size = lib.CODE_SIZE
        self.mem_size = lib.MEM_SIZE
        self.mem_addr = lib.MEM_ADDR
        
        if cfg:
            self.cfg = cfg
        else:
            self.cfg = Config()

        if obj:
            self.obj = obj
        else:
            self.obj = ffi.new("struct snippet_ *")
            lib.snippet_init(self.cfg.obj, self.obj)

        # Easy contructor
        if text:
            assert len(text) > 0
            assert self.start_address != None
            self.decode(text, len(text), self.start_address)
    
    def __del__(self):
        lib.snippet_destroy(self.obj)

    def __eq__(self, other):
        if not isinstance(other, Snippet):
            return NotImplemented

        return self.max_len == other.max_len                    \
               and self.count == other.count                    \
               and self.start_address == other.start_address
    
    @property
    def count(self) -> int:
        return self.obj.count

    @property
    def start_address(self) -> int:
        return self.obj.start_address

    def print(self) -> None:
        lib.snippet_print(self.obj, sys.stdout, False, True)
    
    # Decodes a program text of size sz assumed to be at rt_address and adds it into the snippet
    def decode(self, text: bytes, sz: int, rt_address: int, check=False) -> Snippet:
        ret = lib.snippet_decode(self.obj, rt_address, ffi.new("ZyanU8[]", text), sz)
        if ret < 0:
            raise RuntimeError("Could not decode snippet")
        
        if check:
            ret = lib.pipeline_decode(self.obj)
            if ret < 0:
                raise RuntimeError("Error running the decode pipline")

        return self

    # Encodes the given snippet into a bytes, and returns those bytes
    def encode(self, check=False) -> bytearray:
        text = ffi.new(f"uint8_t[{MAXIMUM_TEXT_SIZE}]")
        
        if check:
            ret = lib.pipeline_encode(self.obj)
            if ret < 0:
                raise RuntimeError(f"Error running the encode pipeline")
        
        ret = lib.snippet_encode(self.obj, text, MAXIMUM_TEXT_SIZE)
        if ret < 0:
            raise RuntimeError(f"Could not decode snippet, error on instruction {-ret}")

        return bytearray(text)[0:ret]
    
    def mutate(self, mut_type: Mut_Types) -> Snippet:
        ret = lib.mutate_snippet(self.cfg.obj, self.obj, mut_type.value)

        if ret < 0:
            self.print()
            raise RuntimeError(f"Could not mutate snippet with method {mut_type} of size {self.count}")
        
        return self

    def create(self, gen_type: Gen_Types, len: Int) -> Snippet:
        if gen_type == Gen_Types.METHOD_NONE:
            return self
        
        ret = lib.create_snippet(self.cfg.obj, self.obj, len, gen_type.value)

        if ret < 0:
            raise RuntimeError("Could not mutate snippet")
        
        return self
    
    def allocate_instruction(self) -> Instruction:
        ret =  lib.snippet_allocate(self.obj)

        if ret < 0:
            raise RuntimeError("Could not allocate instruction")

        return Instruction(obj=obj)

    def get_instruction(self, idx: int) -> Instruction:
        ins = lib.snippet_get(self.obj, idx)

        if ins == ffi.NULL:
            raise RuntimeError("Invalid instruction index")

        return Instruction(obj=ins)

    def remove_instruction(self, idx: int = -1, instr: Instruction = None) -> Snippet:
        if idx == -1 and instruction == None:
            raise RuntimeError("Invalid arguments")
        
        if idx != -1:
            ins = lib.snippet_get(self.obj, idx)
        else:
            ins = instr.obj

        if ins == ffi.NULL:
            raise RuntimeError("Invalid instruction")
        
        ret = lib.snippet_remove(self.obj, ins)
        if ret < 0:
            raise RuntimeError("Failed to remove instruction")

        return self

    def append_instruction(self, instr: Instruction) -> Snippet:
        ret = lib.snippet_append(self.obj, instr.obj)
        
        if ret < 0:
            raise RuntimeError("Failed to append instruction")

        return self

    def insert_instruction(self, instr: Instruction, idx: Int) -> Snippet:
        ret = lib.snippet_insert_at(self.obj, instr.obj, idx)

        if ret < 0:
            raise RuntimeError("Failed to insert instruction")
        
        return self

    def swap_instructions(self, idx1: int, idx2: int) -> Snippet:
        ret = lib.snippet_swap(self.obj, idx1, idx2)

        if ret < 0:
            raise RuntimeError("Failed to swap instructions")

        return self

    def free(self) -> Snippet:
        ret = lib.snippet_free(self.obj)
        
        if ret < 0:
            raise RuntimeError("Failed to free snippet")

        return self
