#ifndef DEBUG_STRINGS_H
#define DEBUG_STRINGS_H

extern const char *zydis_operand_action_strings[];
extern const char *zydis_operand_visibility_strings[];
extern const char *zydis_semantic_optype_strings[];
extern const char *zydis_category_strings[];
extern const char *zydis_ielement_type_strings[];
extern const char *zydis_instruction_encoding_strings[];
extern const char *zydis_width_strings[];
extern const char *zydis_mandatory_prefix_strings[];
extern const char *zydis_branch_type_strings[];
extern const char *zydis_size_hint_strings[];
extern const char *zydis_operand_encoding_strings[];
extern const char *zydis_implreg_type_strings[];
extern const char *zydis_implmem_type_strings[];
extern const char *zydis_vector_length_strings[];
extern const char *zydis_machine_mode_strings[];

// These are more related to snippet printing
extern const char *zydis_mnemonic_strings[];
extern const char *zydis_operand_type_strings[];

// Non Zydis debug strings
extern const char *xfeature_strings[];
extern const char *fuzz_method_strings[];
extern const char *encode_method_strings[];

#endif
