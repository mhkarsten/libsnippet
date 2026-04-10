#ifndef ZYDIS_WRAPPER_H
#define ZYDIS_WRAPPER_H

#include <Zycore/Types.h>
#include <Zydis/Encoder.h>
#include <Zydis/Internal/EncoderData.h>
#include <Zydis/Internal/SharedData.h>

ZYDIS_EXPORT ZydisEncodableEncoding _ZydisGetEncodableEncoding(ZydisInstructionEncoding encoding);
ZYDIS_EXPORT ZyanU8 _ZydisGetMachineModeWidth(ZydisMachineMode machine_mode);
ZYDIS_EXPORT ZyanU8 _ZydisGetSignedImmSize(ZyanI64 imm);
//ZYDIS_EXPORT const ZyanU16 *_ZydisGetOperandSizes(const ZydisOperandDefinition *definition);

#endif
