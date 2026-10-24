/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20251212 (64-bit version)
 * Copyright (c) 2000 - 2025 Intel Corporation
 * 
 * Disassembling to symbolic ASL+ operators
 *
 * Disassembly of SSDT-Disable_GPU_PEG2.aml
 *
 * Original Table Header:
 *     Signature        "SSDT"
 *     Length           0x000000D5 (213)
 *     Revision         0x02
 *     Checksum         0x3B
 *     OEM ID           "ZPSS"
 *     OEM Table ID     "DGPU"
 *     OEM Revision     0x00000000 (0)
 *     Compiler ID      "INTL"
 *     Compiler Version 0x20240927 (539232551)
 */
DefinitionBlock ("", "SSDT", 2, "ZPSS", "DGPU", 0x00000000)
{
    External (_SB_.PC00.PEG2.PEGP._OFF, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG2.PEGP._ON_, MethodObj)    // 0 Arguments

    Device (DGPU)
    {
        Name (_HID, "DGPU1000")  // _HID: Hardware ID
        Method (_INI, 0, NotSerialized)  // _INI: Initialize
        {
            _OFF ()
        }

        Method (_STA, 0, NotSerialized)  // _STA: Status
        {
            If (_OSI ("Darwin"))
            {
                Return (0x0F)
            }
            Else
            {
                Return (Zero)
            }
        }

        Method (_ON, 0, NotSerialized)  // _ON_: Power On
        {
            \_SB.PC00.PEG2.PEGP._ON ()
        }

        Method (_OFF, 0, NotSerialized)  // _OFF: Power Off
        {
            \_SB.PC00.PEG2.PEGP._OFF ()
        }
    }
}

