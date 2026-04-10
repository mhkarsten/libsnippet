from cffi import FFI
from pycparser import c_ast, parse_file, c_generator, preprocess_file, c_parser
import pycparser_fake_libc
from glob import glob
import os
import re

PKG_DIR = "src"
BUILD_DIR = "libsnippet"
PAPI_DIR = os.getenv("PAPI_DIR", "usr")

def find_first(text, names):
    text = text.split("\n")
    for l in text:
        if any([name in l for name in names]):
            return l

def list_funcs(text):
    func_decs = []
    func_defs = []

    class DefFinder(c_ast.NodeVisitor):
        def visit_FuncDef(self, node):
            func_defs.append(node.decl.name)

        def visit_FuncDecl(self, node):
            if isinstance(node.type, c_ast.TypeDecl):
                func_decs.append(node.type.declname)
            else:
                func_decs.append(node.type.type.declname)

    parser = c_parser.CParser()
    ast = parser.parse(text)

    v = DefFinder()
    v.visit(ast)

    return func_defs, func_decs

def list_names(text):
    names = text.split("\n")
    names = filter(lambda x: "typedef" in x, names)
    names = map(lambda x: "Zydis"+(x.split("Zydis")[1]).strip("_"), names)

    return list(names)

def remove_between(text, str_start, str_end):
    start = text.find(str_start)
    end = text.find(str_end)

    if start == -1 or end == -1:
        return -1, text

    part = text[start+len(str_start):end]

    text = text.replace(text[start:end+len(str_end)], "")

    return part, text

def zydis_cdefs(builder, hdr_file):
    text = preprocess_file(hdr_file,
                           "cpp",
                           [r"-E",
                            r"-P",
                            r"-I"+pycparser_fake_libc.directory,
                            r"-I./include",
                            f"-I{os.path.abspath('zydis/include')}",
                            r"-D__attribute__(x)=",
                            r'-Dcpu_set_t=int',
                            r"-Dasm=",
                            r"-Dvolatile(...)=",
                            r"-D_GNU_SOURCE"])
    
    invalid_func_types = ["ZyanString", "ZyanVector", "ZyanAllocator"]
    
    # This is very hacky, but the header format needs to be fixed up a bit to be easily parsed
    text = text.replace("REQUIRED_BITS =\n", "REQUIRED_BITS = ")
    text = text.replace(",\n    Zy", ", Zy")
    text = text.replace(",\n        Zy", ", Zy")
    text = text.replace(",\n    const", ", const")
    text = text.replace(",\n    void", ", void")
    text = text.replace("(\n    const", "(const")
    text = text.replace(",\n    char", ", char")
    text = text.replace("(\n    Zy", "(Zy")
    text = text.replace("__extension__", "")

    # Remove things with DECLS
    pat = "(__BEGIN_DECLS(.*?)__END_DECLS)"
    for match in re.findall(pat, text, flags=re.DOTALL):
        text = text.replace(match[0], "")
   
    func_defs, func_decs = list_funcs(text)
    text = text.split("\n")
    
    # Remove a bunch of (incorrect) typdefs for basic units that were all getting set to int
    text = text[199:]

    # Remove calls to static assert
    text = list(filter(lambda x: ("_Static_assert" not in x), text))
    # Remove functions that do not get resolved properly
    text = list(filter(lambda x: not (any(t in x for t in invalid_func_types) and any(dec in x for dec in func_decs)), text))

    in_fn = False
    for i, l in enumerate(text):
        if l == "}" and in_fn:
            in_fn = False
            text[i] = ""
            continue

        # Change function definitions into declarations
        if any([fdef in l for fdef in func_defs]):
            text[i] = l.split(" {")[0] + ";"

            if text[i+1] == "{":
                in_fn = True
        
        # Some things should be resolved by the compiler
        if "REQUIRED_BITS" in l and ":" not in l:
            text[i] = l.split(" = ")[0] + " = ..."
        
        if in_fn:
            text[i] = ""
            continue
    
    text = "\n".join(text)

    # Lift pragma push/pop pairs so they can be defined as packed
    pragmas = []
    prag, text = remove_between(text, "#pragma pack(push, 1)\n", "#pragma pack(pop)\n")
    while prag != -1:
        pragmas.append(prag)
        prag, text = remove_between(text, "#pragma pack(push, 1)\n", "#pragma pack(pop)\n")
    
    for prag in pragmas:
        names = list_names(prag)
        
        first = find_first(text, names)
        
        if first:
            text = text.split(first)
            builder.cdef(text[0])
            text = first + text[1]
        
        # Sadly we cant have bitfields defined with enums in cffi
        pat = r"(\n{(?=.*(?:: (?!\d)[A-Z_():?<> +1]*;))[a-zA-Z_\s\d:;()<>+1?]+})"
        for match in re.findall(pat, prag, flags=re.DOTALL):
            prag = prag.replace(match, "")
        
        builder.cdef(prag, packed=True)
    
    with open("zydis_cdefs_ref.h", "w") as f:
        f.write(text)
    
    # Add in a dummy for va_list
    builder.cdef("typedef void *va_list;")


    builder.cdef(text)

    builder.cdef("""static const uint64_t MEM_ADDR;
                  static const uint64_t MEM_SIZE;
                  static const uint64_t MEM_PADDING;
                  static const uint64_t CODE_ADDR;
                  static const uint64_t CODE_SIZE;
                  static const uint64_t MAX_SNIPPET_SIZE;""")
    
def cffi_build(builder):
    zydis_cdefs(builder, os.path.join("include", "generate.h")) # Pass the top level header file here
    
    builder.set_source(f"_libsnippet",
                  """
                  #include <Zydis/Encoder.h>
                  #include <Zydis/Wrapper.h>
                  #include <Zydis/Internal/SharedData.h>
                  #include <Zydis/Internal/EncoderData.h>
                  #include <Zydis/Zydis.h>
                  #include <Zydis/MetaInfo.h>
                  
                  #include "pipeline.h"
                  #include "generate.h"
                  #include "snippet.h"
                  #include "config.h"
                  #include "arena.h"
                  #include "debug_strings.h"
                  #include "score.h"
                  
                  """,
                  sources=[os.path.relpath(f"{PKG_DIR}/config.c", BUILD_DIR),
                           os.path.relpath(f"{PKG_DIR}/pipeline.c", BUILD_DIR),
                           os.path.relpath(f"{PKG_DIR}/snippet.c", BUILD_DIR),
                           os.path.relpath(f"{PKG_DIR}/debug_strings.c", BUILD_DIR),
                           os.path.relpath(f"{PKG_DIR}/arena.c", BUILD_DIR),
                           os.path.relpath(f"{PKG_DIR}/generate.c", BUILD_DIR),
                           os.path.relpath(f"{PKG_DIR}/score.c", BUILD_DIR),
                           os.path.relpath(f"{PKG_DIR}/bloom.c", BUILD_DIR)],
                  libraries=["c"],
                  library_dirs=[os.path.abspath("zydis/builddir")],
                  extra_compile_args=[
                      "-DDEBUG", 
                      "-g",
                      "-D_GNU_SOURCE",
                      "-mxsave",
                      f"-I{os.path.abspath('include')}", 
                      f"-I{os.path.abspath('zydis/include')}",
                      f"-I/{PAPI_DIR}/include"],
                  extra_link_args=[f"-L/{PAPI_DIR}/lib", "-lpapi", "-lxxhash", os.path.abspath("zydis/builddir/libZydis2.so"), f"-Wl,-rpath,{os.path.abspath('zydis/builddir')}"])

    builder.compile(tmpdir=BUILD_DIR, verbose=True)

    # Move .o files to the appropriate location afterwards
    os.system("mv src/*.o libsnippet/*.o obj")

if not glob(os.path.join(BUILD_DIR, "*.so")):
    os.system("make zydis")
    ffibuilder = FFI()
    cffi_build(ffibuilder)
else:
    print("No compilation needed")
