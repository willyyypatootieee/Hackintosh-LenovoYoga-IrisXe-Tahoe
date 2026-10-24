/*
 * Intel ACPI Component Architecture
 * AML/ASL+ Disassembler version 20251212 (64-bit version)
 * Copyright (c) 2000 - 2025 Intel Corporation
 * 
 * Disassembling to non-symbolic legacy ASL operators
 *
 * Disassembly of /home/willy/New Folder/yoga-adlp-firmware-capture-20261012/acpi/SSDT4
 *
 * Original Table Header:
 *     Signature        "SSDT"
 *     Length           0x00002B22 (11042)
 *     Revision         0x02
 *     Checksum         0x8F
 *     OEM ID           "LENOVO"
 *     OEM Table ID     "CB-01   "
 *     OEM Revision     0x00000001 (1)
 *     Compiler ID      "INTL"
 *     Compiler Version 0x20200717 (538969879)
 */
DefinitionBlock ("", "SSDT", 2, "LENOVO", "CB-01   ", 0x00000001)
{
    External (_SB_.CAGS, MethodObj)    // 1 Arguments
    External (_SB_.ISME, MethodObj)    // 1 Arguments
    External (_SB_.PC00, DeviceObj)
    External (_SB_.PC00.B0D3, DeviceObj)
    External (_SB_.PC00.GFX0, DeviceObj)
    External (_SB_.PC00.IPU0, DeviceObj)
    External (_SB_.PC00.PC2M, MethodObj)    // 1 Arguments
    External (_SB_.PC00.PEG0, DeviceObj)
    External (_SB_.PC00.PEG0._ADR, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG0.ECR1, UnknownObj)
    External (_SB_.PC00.PEG0.GPRW, MethodObj)    // 2 Arguments
    External (_SB_.PC00.PEG0.PEGP, DeviceObj)
    External (_SB_.PC00.PEG0.PEGP.PAHC, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG0.PEGP.PNVM, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG0.PICM, UnknownObj)
    External (_SB_.PC00.PEG0.PPBA, MethodObj)    // 1 Arguments
    External (_SB_.PC00.PEG0.PPS0, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG0.PPS3, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG0.UPRD, MethodObj)    // 1 Arguments
    External (_SB_.PC00.PEG0.VDID, FieldUnitObj)
    External (_SB_.PC00.PEG1, DeviceObj)
    External (_SB_.PC00.PEG1._ADR, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG1.DHDW, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG1.ECR1, UnknownObj)
    External (_SB_.PC00.PEG1.GPRW, MethodObj)    // 2 Arguments
    External (_SB_.PC00.PEG1.PEGP, DeviceObj)
    External (_SB_.PC00.PEG1.PEGP.PAHC, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG1.PEGP.PNVM, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG1.PICM, UnknownObj)
    External (_SB_.PC00.PEG1.PPBA, MethodObj)    // 1 Arguments
    External (_SB_.PC00.PEG1.PPS0, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG1.PPS3, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG1.UPRD, MethodObj)    // 1 Arguments
    External (_SB_.PC00.PEG1.VDID, FieldUnitObj)
    External (_SB_.PC00.PEG2, DeviceObj)
    External (_SB_.PC00.PEG2._ADR, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG2.ECR1, UnknownObj)
    External (_SB_.PC00.PEG2.GPRW, MethodObj)    // 2 Arguments
    External (_SB_.PC00.PEG2.PEGP, DeviceObj)
    External (_SB_.PC00.PEG2.PEGP.PAHC, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG2.PEGP.PNVM, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG2.PICM, UnknownObj)
    External (_SB_.PC00.PEG2.PPBA, MethodObj)    // 1 Arguments
    External (_SB_.PC00.PEG2.PPS0, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG2.PPS3, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG2.UPRD, MethodObj)    // 1 Arguments
    External (_SB_.PC00.PEG2.VDID, FieldUnitObj)
    External (_SB_.PC00.PEG3, DeviceObj)
    External (_SB_.PC00.PEG3._ADR, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG3.ECR1, UnknownObj)
    External (_SB_.PC00.PEG3.GPRW, MethodObj)    // 2 Arguments
    External (_SB_.PC00.PEG3.PEGP, DeviceObj)
    External (_SB_.PC00.PEG3.PEGP.PAHC, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG3.PEGP.PNVM, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG3.PICM, UnknownObj)
    External (_SB_.PC00.PEG3.PPBA, MethodObj)    // 1 Arguments
    External (_SB_.PC00.PEG3.PPS0, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG3.PPS3, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG3.UPRD, MethodObj)    // 1 Arguments
    External (_SB_.PC00.PEG3.VDID, FieldUnitObj)
    External (_SB_.SHPO, MethodObj)    // 2 Arguments
    External (AR02, UnknownObj)
    External (AR0A, UnknownObj)
    External (AR0B, UnknownObj)
    External (AR0C, UnknownObj)
    External (CPID, UnknownObj)
    External (LTRW, UnknownObj)
    External (LTRX, UnknownObj)
    External (LTRY, UnknownObj)
    External (LTRZ, UnknownObj)
    External (MPGN, UnknownObj)
    External (P0WK, UnknownObj)
    External (P1WK, UnknownObj)
    External (P2WK, UnknownObj)
    External (P3WK, UnknownObj)
    External (PBR1, UnknownObj)
    External (PBR2, UnknownObj)
    External (PBR3, UnknownObj)
    External (PD02, UnknownObj)
    External (PD0A, UnknownObj)
    External (PD0B, UnknownObj)
    External (PD0C, UnknownObj)
    External (PG0E, UnknownObj)
    External (PG1E, UnknownObj)
    External (PG2E, UnknownObj)
    External (PG3E, UnknownObj)
    External (PICM, IntObj)
    External (PINI, MethodObj)    // 0 Arguments
    External (PPA0, IntObj)
    External (PPA1, IntObj)
    External (PPA2, IntObj)
    External (PPA3, IntObj)
    External (SMSL, UnknownObj)
    External (SNSL, UnknownObj)
    External (STD3, FieldUnitObj)
    External (VMCP, UnknownObj)
    External (VMDE, UnknownObj)

    Scope (\_SB.PC00.PEG0)
    {
        Name (SLOT, Zero)
        Method (CPRA, 0, NotSerialized)
        {
            If (LNotEqual (\PPA0, Zero))
            {
                Return (\PPA0) /* External reference */
            }
            Else
            {
                Return (0x00060000)
            }
        }

        Method (_STA, 0, NotSerialized)  // _STA: Status
        {
            If (LEqual (PG0E, One))
            {
                Return (0x0F)
            }

            Return (Zero)
        }

        Name (LTEN, Zero)
        Name (LMSL, Zero)
        Name (LNSL, Zero)
        Method (_INI, 0, NotSerialized)  // _INI: Initialize
        {
            If (PRES ())
            {
                Store (LTRX, LTEN) /* \_SB_.PC00.PEG0.LTEN */
                Store (SMSL, LMSL) /* \_SB_.PC00.PEG0.LMSL */
                Store (SNSL, LNSL) /* \_SB_.PC00.PEG0.LNSL */
                If (CondRefOf (PINI))
                {
                    PINI ()
                }

                If (LAnd (CondRefOf (VMCP), CondRefOf (VMDE)))
                {
                    If (LAnd (LEqual (VMDE, One), LNotEqual (And (VMCP, 0x08), Zero)))
                    {
                        Store (One, PRMV) /* \_SB_.PC00.PEG0.PRMV */
                    }
                }

                If (CondRefOf (\_SB.PC00.PEG0))
                {
                    If (LNotEqual (\_SB.PC00.PEG0.VDID, 0xFFFFFFFF))
                    {
                        If (CondRefOf (\STD3))
                        {
                            If (CondRefOf (\_SB.PC00.PEG0.PRMV))
                            {
                                If (LEqual (\_SB.PC00.PEG0.PRMV, One))
                                {
                                    Store (\STD3, \_SB.PC00.PEG0.RD3C)
                                }
                            }

                            If (LOr (\_SB.PC00.PEG0.PEGP.PNVM (), \_SB.PC00.PEG0.PEGP.PAHC ()))
                            {
                                Store (\STD3, \_SB.PC00.PEG0.RD3C)
                            }
                        }
                    }
                }
            }
        }

        Method (_PRT, 0, NotSerialized)  // _PRT: PCI Routing Table
        {
            If (PICM)
            {
                Return (AR02) /* External reference */
            }

            Return (PD02) /* External reference */
        }

        Name (PRTP, 0x04)
        OperationRegion (PXCS, SystemMemory, \_SB.PC00.PC2M (_ADR ()), 0x0480)
        Field (PXCS, AnyAcc, NoLock, Preserve)
        {
            VDID,   32, 
            Offset (0x50), 
            L0SE,   1, 
                ,   3, 
            LDIS,   1, 
            Offset (0x51), 
            Offset (0x52), 
                ,   13, 
            LASX,   1, 
            Offset (0x5A), 
            ABPX,   1, 
                ,   2, 
            PDCX,   1, 
                ,   2, 
            PDSX,   1, 
            Offset (0x5B), 
            Offset (0x60), 
            Offset (0x62), 
            PSPX,   1, 
            Offset (0xA4), 
            D3HT,   2, 
            Offset (0xD8), 
                ,   30, 
            HPEX,   1, 
            PMEX,   1, 
            Offset (0xE0), 
            SCB0,   1, 
                ,   6, 
            NCB7,   1, 
            Offset (0xE2), 
                ,   2, 
            L23E,   1, 
            L23R,   1, 
            Offset (0x324), 
                ,   3, 
            LEDM,   1, 
            Offset (0x328), 
            Offset (0x32B), 
            LTSM,   8
        }

        Field (PXCS, AnyAcc, NoLock, WriteAsZeros)
        {
            Offset (0xDC), 
                ,   30, 
            HPSX,   1, 
            PMSX,   1
        }

        Method (L23D, 0, Serialized)
        {
            If (LNotEqual (SCB0, One))
            {
                Return (Zero)
            }

            Store (One, L23R) /* \_SB_.PC00.PEG0.L23R */
            Store (Zero, Local0)
            While (L23R)
            {
                If (LGreater (Local0, 0x04))
                {
                    Break
                }

                Sleep (0x10)
                Increment (Local0)
            }

            Store (Zero, SCB0) /* \_SB_.PC00.PEG0.SCB0 */
            Store (Zero, Local0)
            While (LEqual (LASX, Zero))
            {
                If (LGreater (Local0, 0x08))
                {
                    Break
                }

                Sleep (0x10)
                Increment (Local0)
            }
        }

        Method (DL23, 0, Serialized)
        {
            Store (One, L23E) /* \_SB_.PC00.PEG0.L23E */
            Sleep (0x10)
            Store (Zero, Local0)
            While (L23E)
            {
                If (LGreater (Local0, 0x04))
                {
                    Break
                }

                Sleep (0x10)
                Increment (Local0)
            }

            Store (One, SCB0) /* \_SB_.PC00.PEG0.SCB0 */
        }

        Name (LTRV, Package (0x04)
        {
            Zero, 
            Zero, 
            Zero, 
            Zero
        })
        Name (PRMV, Zero)
        Name (RD3C, 0x02)
        Method (PRES, 0, NotSerialized)
        {
            If (LEqual (VDID, 0xFFFFFFFF))
            {
                Return (Zero)
            }
            Else
            {
                Return (One)
            }
        }

        Method (_DSM, 4, Serialized)  // _DSM: Device-Specific Method
        {
            If (LEqual (Arg0, ToUUID ("e5c937d0-3553-4d7a-9117-ea4d19c3434d") /* Device Labeling Interface */))
            {
                Switch (ToInteger (Arg2))
                {
                    Case (Zero)
                    {
                        Name (OPTS, Buffer (0x02)
                        {
                             0x00, 0x00                                       // ..
                        })
                        CreateBitField (OPTS, Zero, FUN0)
                        CreateBitField (OPTS, 0x06, FUN6)
                        CreateBitField (OPTS, 0x08, FUN8)
                        CreateBitField (OPTS, 0x09, FUN9)
                        CreateBitField (OPTS, 0x0A, FUNA)
                        CreateBitField (OPTS, 0x0B, FUNB)
                        If (LGreaterEqual (Arg1, 0x02))
                        {
                            Store (One, FUN0) /* \_SB_.PC00.PEG0._DSM.FUN0 */
                            If (LNotEqual (LTEN, Zero))
                            {
                                Store (One, FUN6) /* \_SB_.PC00.PEG0._DSM.FUN6 */
                            }

                            If (CondRefOf (ECR1))
                            {
                                If (LEqual (ECR1, One))
                                {
                                    If (LGreaterEqual (Arg1, 0x03))
                                    {
                                        Store (One, FUN8) /* \_SB_.PC00.PEG0._DSM.FUN8 */
                                        Store (One, FUN9) /* \_SB_.PC00.PEG0._DSM.FUN9 */
                                    }
                                }
                            }
                        }

                        If (LGreaterEqual (Arg1, 0x04))
                        {
                            If (CondRefOf (PPBA))
                            {
                                Store (One, FUNA) /* \_SB_.PC00.PEG0._DSM.FUNA */
                            }

                            If (CondRefOf (UPRD))
                            {
                                Store (One, FUNB) /* \_SB_.PC00.PEG0._DSM.FUNB */
                            }
                        }

                        Return (OPTS) /* \_SB_.PC00.PEG0._DSM.OPTS */
                    }
                    Case (0x06)
                    {
                        If (LGreaterEqual (Arg1, 0x02))
                        {
                            Store (And (ShiftRight (LMSL, 0x0A), 0x07), Index (LTRV, Zero))
                            Store (And (LMSL, 0x03FF), Index (LTRV, One))
                            Store (And (ShiftRight (LNSL, 0x0A), 0x07), Index (LTRV, 0x02))
                            Store (And (LNSL, 0x03FF), Index (LTRV, 0x03))
                            Return (LTRV) /* \_SB_.PC00.PEG0.LTRV */
                        }
                    }
                    Case (0x08)
                    {
                        If (CondRefOf (ECR1))
                        {
                            If (LEqual (ECR1, One))
                            {
                                If (LGreaterEqual (Arg1, 0x03))
                                {
                                    Return (One)
                                }
                            }
                        }
                    }
                    Case (0x09)
                    {
                        If (CondRefOf (ECR1))
                        {
                            If (LEqual (ECR1, One))
                            {
                                If (LGreaterEqual (Arg1, 0x03))
                                {
                                    Return (Package (0x05)
                                    {
                                        0xC350, 
                                        Ones, 
                                        Ones, 
                                        0xC350, 
                                        Ones
                                    })
                                }
                            }
                        }
                    }
                    Case (0x0A)
                    {
                        If (CondRefOf (PPBA))
                        {
                            Return (PPBA (Arg3))
                        }
                    }
                    Case (0x0B)
                    {
                        If (CondRefOf (UPRD))
                        {
                            Return (UPRD (Arg3))
                        }
                    }

                }
            }

            Return (Buffer (One)
            {
                 0x00                                             // .
            })
        }

        Method (_PRW, 0, NotSerialized)  // _PRW: Power Resources for Wake
        {
            Return (GPRW (0x69, 0x04))
        }

        Method (_PS0, 0, Serialized)  // _PS0: Power State 0
        {
            If (LEqual (HPEX, One))
            {
                Store (Zero, HPEX) /* \_SB_.PC00.PEG0.HPEX */
                Store (One, HPSX) /* \_SB_.PC00.PEG0.HPSX */
            }

            If (LEqual (PMEX, One))
            {
                Store (Zero, PMEX) /* \_SB_.PC00.PEG0.PMEX */
                Store (One, PMSX) /* \_SB_.PC00.PEG0.PMSX */
            }

            If (CondRefOf (PPS0))
            {
                PPS0 ()
            }
        }

        Method (_PS3, 0, Serialized)  // _PS3: Power State 3
        {
            If (CondRefOf (PPS3))
            {
                PPS3 ()
            }

            If (LEqual (HPEX, Zero))
            {
                Store (One, HPEX) /* \_SB_.PC00.PEG0.HPEX */
                Store (One, HPSX) /* \_SB_.PC00.PEG0.HPSX */
            }

            If (LEqual (PMEX, Zero))
            {
                Store (One, PMEX) /* \_SB_.PC00.PEG0.PMEX */
                Store (One, PMSX) /* \_SB_.PC00.PEG0.PMSX */
            }
        }

        Method (_DSD, 0, NotSerialized)  // _DSD: Device-Specific Data
        {
            If (CondRefOf (PINI))
            {
                Return (Package (0x06)
                {
                    ToUUID ("6211e2c0-58a3-4af3-90e1-927a4e0c55a4") /* Unknown UUID */, 
                    Package (0x01)
                    {
                        Package (0x02)
                        {
                            "HotPlugSupportInD3", 
                            One
                        }
                    }, 

                    ToUUID ("fdf06fad-f744-4451-bb64-ecd792215b10") /* Unknown UUID */, 
                    Package (0x01)
                    {
                        Package (0x02)
                        {
                            "FundamentalDeviceResetTriggeredOnD3ToD0", 
                            One
                        }
                    }, 

                    ToUUID ("6b4ad420-8fd3-4364-acf8-eb94876fd9eb") /* Unknown UUID */, 
                    Package (0x00){}
                })
            }
            Else
            {
                Return (Package (0x04)
                {
                    ToUUID ("6211e2c0-58a3-4af3-90e1-927a4e0c55a4") /* Unknown UUID */, 
                    Package (0x01)
                    {
                        Package (0x02)
                        {
                            "HotPlugSupportInD3", 
                            One
                        }
                    }, 

                    ToUUID ("fdf06fad-f744-4451-bb64-ecd792215b10") /* Unknown UUID */, 
                    Package (0x01)
                    {
                        Package (0x02)
                        {
                            "FundamentalDeviceResetTriggeredOnD3ToD0", 
                            One
                        }
                    }
                })
            }
        }

        Method (HPME, 0, Serialized)
        {
            If (LAnd (LNotEqual (VDID, 0xFFFFFFFF), LEqual (PMSX, One)))
            {
                Store (One, PMSX) /* \_SB_.PC00.PEG0.PMSX */
                Store (One, PSPX) /* \_SB_.PC00.PEG0.PSPX */
                Return (One)
            }

            Return (Zero)
        }

        Method (HPEV, 0, Serialized)
        {
            If (LAnd (LNotEqual (VDID, 0xFFFFFFFF), HPSX))
            {
                Store (One, HPSX) /* \_SB_.PC00.PEG0.HPSX */
                If (LEqual (PDCX, One))
                {
                    Store (One, PDCX) /* \_SB_.PC00.PEG0.PDCX */
                    If (LEqual (PDSX, Zero))
                    {
                        Store (Zero, L0SE) /* \_SB_.PC00.PEG0.L0SE */
                    }

                    Notify (^, Zero) // Bus Check
                }
            }
        }
    }

    Scope (\_SB.PC00.PEG1)
    {
        Name (SLOT, One)
        Method (CPRA, 0, NotSerialized)
        {
            If (LNotEqual (\PPA1, Zero))
            {
                Return (\PPA1) /* External reference */
            }
            Else
            {
                Return (0x00010000)
            }
        }

        Method (_STA, 0, NotSerialized)  // _STA: Status
        {
            If (LEqual (PG1E, One))
            {
                Return (0x0F)
            }

            Return (Zero)
        }

        Name (LTEN, Zero)
        Name (LMSL, Zero)
        Name (LNSL, Zero)
        Method (_INI, 0, NotSerialized)  // _INI: Initialize
        {
            If (PRES ())
            {
                Store (LTRY, LTEN) /* \_SB_.PC00.PEG1.LTEN */
                Store (SMSL, LMSL) /* \_SB_.PC00.PEG1.LMSL */
                Store (SNSL, LNSL) /* \_SB_.PC00.PEG1.LNSL */
                If (CondRefOf (PINI))
                {
                    PINI ()
                }

                If (LAnd (CondRefOf (VMCP), CondRefOf (VMDE)))
                {
                    If (LAnd (LEqual (VMDE, One), LNotEqual (And (VMCP, One), Zero)))
                    {
                        Store (One, PRMV) /* \_SB_.PC00.PEG1.PRMV */
                    }
                }

                If (CondRefOf (\_SB.PC00.PEG1))
                {
                    If (LNotEqual (\_SB.PC00.PEG1.VDID, 0xFFFFFFFF))
                    {
                        If (CondRefOf (\STD3))
                        {
                            If (CondRefOf (\_SB.PC00.PEG1.PRMV))
                            {
                                If (LEqual (\_SB.PC00.PEG1.PRMV, One))
                                {
                                    Store (\STD3, \_SB.PC00.PEG1.RD3C)
                                }
                            }

                            If (LOr (\_SB.PC00.PEG1.PEGP.PNVM (), \_SB.PC00.PEG1.PEGP.PAHC ()))
                            {
                                Store (\STD3, \_SB.PC00.PEG1.RD3C)
                            }
                        }
                    }
                }
            }
        }

        Method (_PRT, 0, NotSerialized)  // _PRT: PCI Routing Table
        {
            If (PICM)
            {
                Return (AR02) /* External reference */
            }

            Return (PD02) /* External reference */
        }

        Name (PRTP, 0x04)
        OperationRegion (PXCS, SystemMemory, \_SB.PC00.PC2M (_ADR ()), 0x0480)
        Field (PXCS, AnyAcc, NoLock, Preserve)
        {
            VDID,   32, 
            Offset (0x50), 
            L0SE,   1, 
                ,   3, 
            LDIS,   1, 
            Offset (0x51), 
            Offset (0x52), 
                ,   13, 
            LASX,   1, 
            Offset (0x5A), 
            ABPX,   1, 
                ,   2, 
            PDCX,   1, 
                ,   2, 
            PDSX,   1, 
            Offset (0x5B), 
            Offset (0x60), 
            Offset (0x62), 
            PSPX,   1, 
            Offset (0xA4), 
            D3HT,   2, 
            Offset (0xD8), 
                ,   30, 
            HPEX,   1, 
            PMEX,   1, 
            Offset (0xE0), 
            SCB0,   1, 
                ,   6, 
            NCB7,   1, 
            Offset (0xE2), 
                ,   2, 
            L23E,   1, 
            L23R,   1, 
            Offset (0x324), 
                ,   3, 
            LEDM,   1, 
            Offset (0x328), 
            Offset (0x32B), 
            LTSM,   8
        }

        Field (PXCS, AnyAcc, NoLock, WriteAsZeros)
        {
            Offset (0xDC), 
                ,   30, 
            HPSX,   1, 
            PMSX,   1
        }

        Method (L23D, 0, Serialized)
        {
            If (LNotEqual (SCB0, One))
            {
                Return (Zero)
            }

            Store (One, L23R) /* \_SB_.PC00.PEG1.L23R */
            Store (Zero, Local0)
            While (L23R)
            {
                If (LGreater (Local0, 0x04))
                {
                    Break
                }

                Sleep (0x10)
                Increment (Local0)
            }

            Store (Zero, SCB0) /* \_SB_.PC00.PEG1.SCB0 */
            Store (Zero, Local0)
            While (LEqual (LASX, Zero))
            {
                If (LGreater (Local0, 0x08))
                {
                    Break
                }

                Sleep (0x10)
                Increment (Local0)
            }
        }

        Method (DL23, 0, Serialized)
        {
            Store (One, L23E) /* \_SB_.PC00.PEG1.L23E */
            Sleep (0x10)
            Store (Zero, Local0)
            While (L23E)
            {
                If (LGreater (Local0, 0x04))
                {
                    Break
                }

                Sleep (0x10)
                Increment (Local0)
            }

            Store (One, SCB0) /* \_SB_.PC00.PEG1.SCB0 */
        }

        Name (LTRV, Package (0x04)
        {
            Zero, 
            Zero, 
            Zero, 
            Zero
        })
        Name (PRMV, Zero)
        Name (RD3C, 0x02)
        Method (PRES, 0, NotSerialized)
        {
            If (LEqual (VDID, 0xFFFFFFFF))
            {
                Return (Zero)
            }
            Else
            {
                Return (One)
            }
        }

        Method (_DSM, 4, Serialized)  // _DSM: Device-Specific Method
        {
            If (LEqual (Arg0, ToUUID ("e5c937d0-3553-4d7a-9117-ea4d19c3434d") /* Device Labeling Interface */))
            {
                Switch (ToInteger (Arg2))
                {
                    Case (Zero)
                    {
                        Name (OPTS, Buffer (0x02)
                        {
                             0x00, 0x00                                       // ..
                        })
                        CreateBitField (OPTS, Zero, FUN0)
                        CreateBitField (OPTS, 0x06, FUN6)
                        CreateBitField (OPTS, 0x08, FUN8)
                        CreateBitField (OPTS, 0x09, FUN9)
                        CreateBitField (OPTS, 0x0A, FUNA)
                        CreateBitField (OPTS, 0x0B, FUNB)
                        If (LGreaterEqual (Arg1, 0x02))
                        {
                            Store (One, FUN0) /* \_SB_.PC00.PEG1._DSM.FUN0 */
                            If (LNotEqual (LTEN, Zero))
                            {
                                Store (One, FUN6) /* \_SB_.PC00.PEG1._DSM.FUN6 */
                            }

                            If (CondRefOf (ECR1))
                            {
                                If (LEqual (ECR1, One))
                                {
                                    If (LGreaterEqual (Arg1, 0x03))
                                    {
                                        Store (One, FUN8) /* \_SB_.PC00.PEG1._DSM.FUN8 */
                                        Store (One, FUN9) /* \_SB_.PC00.PEG1._DSM.FUN9 */
                                    }
                                }
                            }
                        }

                        If (LGreaterEqual (Arg1, 0x04))
                        {
                            If (CondRefOf (PPBA))
                            {
                                Store (One, FUNA) /* \_SB_.PC00.PEG1._DSM.FUNA */
                            }

                            If (CondRefOf (UPRD))
                            {
                                Store (One, FUNB) /* \_SB_.PC00.PEG1._DSM.FUNB */
                            }
                        }

                        Return (OPTS) /* \_SB_.PC00.PEG1._DSM.OPTS */
                    }
                    Case (0x06)
                    {
                        If (LGreaterEqual (Arg1, 0x02))
                        {
                            Store (And (ShiftRight (LMSL, 0x0A), 0x07), Index (LTRV, Zero))
                            Store (And (LMSL, 0x03FF), Index (LTRV, One))
                            Store (And (ShiftRight (LNSL, 0x0A), 0x07), Index (LTRV, 0x02))
                            Store (And (LNSL, 0x03FF), Index (LTRV, 0x03))
                            Return (LTRV) /* \_SB_.PC00.PEG1.LTRV */
                        }
                    }
                    Case (0x08)
                    {
                        If (CondRefOf (ECR1))
                        {
                            If (LEqual (ECR1, One))
                            {
                                If (LGreaterEqual (Arg1, 0x03))
                                {
                                    Return (One)
                                }
                            }
                        }
                    }
                    Case (0x09)
                    {
                        If (CondRefOf (ECR1))
                        {
                            If (LEqual (ECR1, One))
                            {
                                If (LGreaterEqual (Arg1, 0x03))
                                {
                                    Return (Package (0x05)
                                    {
                                        0xC350, 
                                        Ones, 
                                        Ones, 
                                        0xC350, 
                                        Ones
                                    })
                                }
                            }
                        }
                    }
                    Case (0x0A)
                    {
                        If (CondRefOf (PPBA))
                        {
                            Return (PPBA (Arg3))
                        }
                    }
                    Case (0x0B)
                    {
                        If (CondRefOf (UPRD))
                        {
                            Return (UPRD (Arg3))
                        }
                    }

                }
            }

            Return (Buffer (One)
            {
                 0x00                                             // .
            })
        }

        Method (_PRW, 0, NotSerialized)  // _PRW: Power Resources for Wake
        {
            Return (GPRW (0x69, 0x04))
        }

        Method (_PS0, 0, Serialized)  // _PS0: Power State 0
        {
            If (LEqual (HPEX, One))
            {
                Store (Zero, HPEX) /* \_SB_.PC00.PEG1.HPEX */
                Store (One, HPSX) /* \_SB_.PC00.PEG1.HPSX */
            }

            If (LEqual (PMEX, One))
            {
                Store (Zero, PMEX) /* \_SB_.PC00.PEG1.PMEX */
                Store (One, PMSX) /* \_SB_.PC00.PEG1.PMSX */
            }

            If (CondRefOf (PPS0))
            {
                PPS0 ()
            }
        }

        Method (_PS3, 0, Serialized)  // _PS3: Power State 3
        {
            If (CondRefOf (PPS3))
            {
                PPS3 ()
            }

            If (LEqual (HPEX, Zero))
            {
                Store (One, HPEX) /* \_SB_.PC00.PEG1.HPEX */
                Store (One, HPSX) /* \_SB_.PC00.PEG1.HPSX */
            }

            If (LEqual (PMEX, Zero))
            {
                Store (One, PMEX) /* \_SB_.PC00.PEG1.PMEX */
                Store (One, PMSX) /* \_SB_.PC00.PEG1.PMSX */
            }
        }

        Method (_DSD, 0, NotSerialized)  // _DSD: Device-Specific Data
        {
            If (CondRefOf (PINI))
            {
                Return (Package (0x06)
                {
                    ToUUID ("6211e2c0-58a3-4af3-90e1-927a4e0c55a4") /* Unknown UUID */, 
                    Package (0x01)
                    {
                        Package (0x02)
                        {
                            "HotPlugSupportInD3", 
                            One
                        }
                    }, 

                    ToUUID ("fdf06fad-f744-4451-bb64-ecd792215b10") /* Unknown UUID */, 
                    Package (0x01)
                    {
                        Package (0x02)
                        {
                            "FundamentalDeviceResetTriggeredOnD3ToD0", 
                            One
                        }
                    }, 

                    ToUUID ("6b4ad420-8fd3-4364-acf8-eb94876fd9eb") /* Unknown UUID */, 
                    Package (0x00){}
                })
            }
            Else
            {
                Return (Package (0x04)
                {
                    ToUUID ("6211e2c0-58a3-4af3-90e1-927a4e0c55a4") /* Unknown UUID */, 
                    Package (0x01)
                    {
                        Package (0x02)
                        {
                            "HotPlugSupportInD3", 
                            One
                        }
                    }, 

                    ToUUID ("fdf06fad-f744-4451-bb64-ecd792215b10") /* Unknown UUID */, 
                    Package (0x01)
                    {
                        Package (0x02)
                        {
                            "FundamentalDeviceResetTriggeredOnD3ToD0", 
                            One
                        }
                    }
                })
            }
        }

        Method (HPME, 0, Serialized)
        {
            If (LAnd (LNotEqual (VDID, 0xFFFFFFFF), LEqual (PMSX, One)))
            {
                Store (One, PMSX) /* \_SB_.PC00.PEG1.PMSX */
                Store (One, PSPX) /* \_SB_.PC00.PEG1.PSPX */
                Return (One)
            }

            Return (Zero)
        }

        Method (HPEV, 0, Serialized)
        {
            If (LAnd (LNotEqual (VDID, 0xFFFFFFFF), HPSX))
            {
                Store (One, HPSX) /* \_SB_.PC00.PEG1.HPSX */
                If (LEqual (PDCX, One))
                {
                    Store (One, PDCX) /* \_SB_.PC00.PEG1.PDCX */
                    If (LEqual (PDSX, Zero))
                    {
                        Store (Zero, L0SE) /* \_SB_.PC00.PEG1.L0SE */
                    }

                    Notify (^, Zero) // Bus Check
                }
            }
        }
    }

    Scope (\_SB.PC00.PEG2)
    {
        Name (SLOT, 0x02)
        Method (CPRA, 0, NotSerialized)
        {
            If (LNotEqual (\PPA2, Zero))
            {
                Return (\PPA2) /* External reference */
            }
            Else
            {
                Return (0x00010001)
            }
        }

        Method (_STA, 0, NotSerialized)  // _STA: Status
        {
            If (LEqual (PG2E, One))
            {
                Return (0x0F)
            }

            Return (Zero)
        }

        Name (LTEN, Zero)
        Name (LMSL, Zero)
        Name (LNSL, Zero)
        Method (_INI, 0, NotSerialized)  // _INI: Initialize
        {
            If (PRES ())
            {
                Store (LTRZ, LTEN) /* \_SB_.PC00.PEG2.LTEN */
                Store (SMSL, LMSL) /* \_SB_.PC00.PEG2.LMSL */
                Store (SNSL, LNSL) /* \_SB_.PC00.PEG2.LNSL */
                If (CondRefOf (PINI))
                {
                    PINI ()
                }

                If (LAnd (CondRefOf (VMCP), CondRefOf (VMDE)))
                {
                    If (LAnd (LEqual (VMDE, One), LOr (LNotEqual (And (VMCP, 0x02), 
                        Zero), LNotEqual (And (VMCP, 0x10), Zero))))
                    {
                        Store (One, PRMV) /* \_SB_.PC00.PEG2.PRMV */
                    }
                }

                If (CondRefOf (\_SB.PC00.PEG2))
                {
                    If (LNotEqual (\_SB.PC00.PEG2.VDID, 0xFFFFFFFF))
                    {
                        If (CondRefOf (\STD3))
                        {
                            If (CondRefOf (\_SB.PC00.PEG2.PRMV))
                            {
                                If (LEqual (\_SB.PC00.PEG2.PRMV, One))
                                {
                                    Store (\STD3, \_SB.PC00.PEG2.RD3C)
                                }
                            }

                            If (LOr (\_SB.PC00.PEG2.PEGP.PNVM (), \_SB.PC00.PEG2.PEGP.PAHC ()))
                            {
                                Store (\STD3, \_SB.PC00.PEG2.RD3C)
                            }
                        }
                    }
                }
            }
        }

        Method (_PRT, 0, NotSerialized)  // _PRT: PCI Routing Table
        {
            If (LEqual (And (CPID, 0xFFFFFFF0), 0x000906A0))
            {
                If (PICM)
                {
                    Return (AR02) /* External reference */
                }

                Return (PD02) /* External reference */
            }
            Else
            {
                If (PICM)
                {
                    Return (AR0A) /* External reference */
                }

                Return (PD0A) /* External reference */
            }
        }

        Name (PRTP, 0x04)
        OperationRegion (PXCS, SystemMemory, \_SB.PC00.PC2M (_ADR ()), 0x0480)
        Field (PXCS, AnyAcc, NoLock, Preserve)
        {
            VDID,   32, 
            Offset (0x50), 
            L0SE,   1, 
                ,   3, 
            LDIS,   1, 
            Offset (0x51), 
            Offset (0x52), 
                ,   13, 
            LASX,   1, 
            Offset (0x5A), 
            ABPX,   1, 
                ,   2, 
            PDCX,   1, 
                ,   2, 
            PDSX,   1, 
            Offset (0x5B), 
            Offset (0x60), 
            Offset (0x62), 
            PSPX,   1, 
            Offset (0xA4), 
            D3HT,   2, 
            Offset (0xD8), 
                ,   30, 
            HPEX,   1, 
            PMEX,   1, 
            Offset (0xE0), 
            SCB0,   1, 
                ,   6, 
            NCB7,   1, 
            Offset (0xE2), 
                ,   2, 
            L23E,   1, 
            L23R,   1, 
            Offset (0x324), 
                ,   3, 
            LEDM,   1, 
            Offset (0x328), 
            Offset (0x32B), 
            LTSM,   8
        }

        Field (PXCS, AnyAcc, NoLock, WriteAsZeros)
        {
            Offset (0xDC), 
                ,   30, 
            HPSX,   1, 
            PMSX,   1
        }

        Method (L23D, 0, Serialized)
        {
            If (LNotEqual (SCB0, One))
            {
                Return (Zero)
            }

            Store (One, L23R) /* \_SB_.PC00.PEG2.L23R */
            Store (Zero, Local0)
            While (L23R)
            {
                If (LGreater (Local0, 0x04))
                {
                    Break
                }

                Sleep (0x10)
                Increment (Local0)
            }

            Store (Zero, SCB0) /* \_SB_.PC00.PEG2.SCB0 */
            Store (Zero, Local0)
            While (LEqual (LASX, Zero))
            {
                If (LGreater (Local0, 0x08))
                {
                    Break
                }

                Sleep (0x10)
                Increment (Local0)
            }
        }

        Method (DL23, 0, Serialized)
        {
            Store (One, L23E) /* \_SB_.PC00.PEG2.L23E */
            Sleep (0x10)
            Store (Zero, Local0)
            While (L23E)
            {
                If (LGreater (Local0, 0x04))
                {
                    Break
                }

                Sleep (0x10)
                Increment (Local0)
            }

            Store (One, SCB0) /* \_SB_.PC00.PEG2.SCB0 */
        }

        Name (LTRV, Package (0x04)
        {
            Zero, 
            Zero, 
            Zero, 
            Zero
        })
        Name (PRMV, Zero)
        Name (RD3C, 0x02)
        Method (PRES, 0, NotSerialized)
        {
            If (LEqual (VDID, 0xFFFFFFFF))
            {
                Return (Zero)
            }
            Else
            {
                Return (One)
            }
        }

        Method (_DSM, 4, Serialized)  // _DSM: Device-Specific Method
        {
            If (LEqual (Arg0, ToUUID ("e5c937d0-3553-4d7a-9117-ea4d19c3434d") /* Device Labeling Interface */))
            {
                Switch (ToInteger (Arg2))
                {
                    Case (Zero)
                    {
                        Name (OPTS, Buffer (0x02)
                        {
                             0x00, 0x00                                       // ..
                        })
                        CreateBitField (OPTS, Zero, FUN0)
                        CreateBitField (OPTS, 0x06, FUN6)
                        CreateBitField (OPTS, 0x08, FUN8)
                        CreateBitField (OPTS, 0x09, FUN9)
                        CreateBitField (OPTS, 0x0A, FUNA)
                        CreateBitField (OPTS, 0x0B, FUNB)
                        If (LGreaterEqual (Arg1, 0x02))
                        {
                            Store (One, FUN0) /* \_SB_.PC00.PEG2._DSM.FUN0 */
                            If (LNotEqual (LTEN, Zero))
                            {
                                Store (One, FUN6) /* \_SB_.PC00.PEG2._DSM.FUN6 */
                            }

                            If (CondRefOf (ECR1))
                            {
                                If (LEqual (ECR1, One))
                                {
                                    If (LGreaterEqual (Arg1, 0x03))
                                    {
                                        Store (One, FUN8) /* \_SB_.PC00.PEG2._DSM.FUN8 */
                                        Store (One, FUN9) /* \_SB_.PC00.PEG2._DSM.FUN9 */
                                    }
                                }
                            }
                        }

                        If (LGreaterEqual (Arg1, 0x04))
                        {
                            If (CondRefOf (PPBA))
                            {
                                Store (One, FUNA) /* \_SB_.PC00.PEG2._DSM.FUNA */
                            }

                            If (CondRefOf (UPRD))
                            {
                                Store (One, FUNB) /* \_SB_.PC00.PEG2._DSM.FUNB */
                            }
                        }

                        Return (OPTS) /* \_SB_.PC00.PEG2._DSM.OPTS */
                    }
                    Case (0x06)
                    {
                        If (LGreaterEqual (Arg1, 0x02))
                        {
                            Store (And (ShiftRight (LMSL, 0x0A), 0x07), Index (LTRV, Zero))
                            Store (And (LMSL, 0x03FF), Index (LTRV, One))
                            Store (And (ShiftRight (LNSL, 0x0A), 0x07), Index (LTRV, 0x02))
                            Store (And (LNSL, 0x03FF), Index (LTRV, 0x03))
                            Return (LTRV) /* \_SB_.PC00.PEG2.LTRV */
                        }
                    }
                    Case (0x08)
                    {
                        If (CondRefOf (ECR1))
                        {
                            If (LEqual (ECR1, One))
                            {
                                If (LGreaterEqual (Arg1, 0x03))
                                {
                                    Return (One)
                                }
                            }
                        }
                    }
                    Case (0x09)
                    {
                        If (CondRefOf (ECR1))
                        {
                            If (LEqual (ECR1, One))
                            {
                                If (LGreaterEqual (Arg1, 0x03))
                                {
                                    Return (Package (0x05)
                                    {
                                        0xC350, 
                                        Ones, 
                                        Ones, 
                                        0xC350, 
                                        Ones
                                    })
                                }
                            }
                        }
                    }
                    Case (0x0A)
                    {
                        If (CondRefOf (PPBA))
                        {
                            Return (PPBA (Arg3))
                        }
                    }
                    Case (0x0B)
                    {
                        If (CondRefOf (UPRD))
                        {
                            Return (UPRD (Arg3))
                        }
                    }

                }
            }

            Return (Buffer (One)
            {
                 0x00                                             // .
            })
        }

        Method (_PRW, 0, NotSerialized)  // _PRW: Power Resources for Wake
        {
            Return (GPRW (0x69, 0x04))
        }

        Method (_PS0, 0, Serialized)  // _PS0: Power State 0
        {
            If (LEqual (HPEX, One))
            {
                Store (Zero, HPEX) /* \_SB_.PC00.PEG2.HPEX */
                Store (One, HPSX) /* \_SB_.PC00.PEG2.HPSX */
            }

            If (LEqual (PMEX, One))
            {
                Store (Zero, PMEX) /* \_SB_.PC00.PEG2.PMEX */
                Store (One, PMSX) /* \_SB_.PC00.PEG2.PMSX */
            }

            If (CondRefOf (PPS0))
            {
                PPS0 ()
            }
        }

        Method (_PS3, 0, Serialized)  // _PS3: Power State 3
        {
            If (CondRefOf (PPS3))
            {
                PPS3 ()
            }

            If (LEqual (HPEX, Zero))
            {
                Store (One, HPEX) /* \_SB_.PC00.PEG2.HPEX */
                Store (One, HPSX) /* \_SB_.PC00.PEG2.HPSX */
            }

            If (LEqual (PMEX, Zero))
            {
                Store (One, PMEX) /* \_SB_.PC00.PEG2.PMEX */
                Store (One, PMSX) /* \_SB_.PC00.PEG2.PMSX */
            }
        }

        Method (_DSD, 0, NotSerialized)  // _DSD: Device-Specific Data
        {
            If (CondRefOf (PINI))
            {
                Return (Package (0x06)
                {
                    ToUUID ("6211e2c0-58a3-4af3-90e1-927a4e0c55a4") /* Unknown UUID */, 
                    Package (0x01)
                    {
                        Package (0x02)
                        {
                            "HotPlugSupportInD3", 
                            One
                        }
                    }, 

                    ToUUID ("fdf06fad-f744-4451-bb64-ecd792215b10") /* Unknown UUID */, 
                    Package (0x01)
                    {
                        Package (0x02)
                        {
                            "FundamentalDeviceResetTriggeredOnD3ToD0", 
                            One
                        }
                    }, 

                    ToUUID ("6b4ad420-8fd3-4364-acf8-eb94876fd9eb") /* Unknown UUID */, 
                    Package (0x00){}
                })
            }
            Else
            {
                Return (Package (0x04)
                {
                    ToUUID ("6211e2c0-58a3-4af3-90e1-927a4e0c55a4") /* Unknown UUID */, 
                    Package (0x01)
                    {
                        Package (0x02)
                        {
                            "HotPlugSupportInD3", 
                            One
                        }
                    }, 

                    ToUUID ("fdf06fad-f744-4451-bb64-ecd792215b10") /* Unknown UUID */, 
                    Package (0x01)
                    {
                        Package (0x02)
                        {
                            "FundamentalDeviceResetTriggeredOnD3ToD0", 
                            One
                        }
                    }
                })
            }
        }

        Method (HPME, 0, Serialized)
        {
            If (LAnd (LNotEqual (VDID, 0xFFFFFFFF), LEqual (PMSX, One)))
            {
                Store (One, PMSX) /* \_SB_.PC00.PEG2.PMSX */
                Store (One, PSPX) /* \_SB_.PC00.PEG2.PSPX */
                Return (One)
            }

            Return (Zero)
        }

        Method (HPEV, 0, Serialized)
        {
            If (LAnd (LNotEqual (VDID, 0xFFFFFFFF), HPSX))
            {
                Store (One, HPSX) /* \_SB_.PC00.PEG2.HPSX */
                If (LEqual (PDCX, One))
                {
                    Store (One, PDCX) /* \_SB_.PC00.PEG2.PDCX */
                    If (LEqual (PDSX, Zero))
                    {
                        Store (Zero, L0SE) /* \_SB_.PC00.PEG2.L0SE */
                    }

                    Notify (^, Zero) // Bus Check
                }
            }
        }
    }

    If (CondRefOf (\_SB.PC00.PEG3))
    {
        Scope (\_SB.PC00.PEG3)
        {
            Name (SLOT, 0x03)
            Method (CPRA, 0, NotSerialized)
            {
                If (LNotEqual (\PPA3, Zero))
                {
                    Return (\PPA3) /* External reference */
                }
                Else
                {
                    Return (0x00010002)
                }
            }

            Method (_STA, 0, NotSerialized)  // _STA: Status
            {
                If (LEqual (PG3E, One))
                {
                    Return (0x0F)
                }

                Return (Zero)
            }

            Name (LTEN, Zero)
            Name (LMSL, Zero)
            Name (LNSL, Zero)
            Method (_INI, 0, NotSerialized)  // _INI: Initialize
            {
                If (PRES ())
                {
                    Store (LTRW, LTEN) /* \_SB_.PC00.PEG3.LTEN */
                    Store (SMSL, LMSL) /* \_SB_.PC00.PEG3.LMSL */
                    Store (SNSL, LNSL) /* \_SB_.PC00.PEG3.LNSL */
                    If (CondRefOf (PINI))
                    {
                        PINI ()
                    }

                    If (LAnd (CondRefOf (VMCP), CondRefOf (VMDE)))
                    {
                        If (LAnd (LEqual (VMDE, One), LNotEqual (And (VMCP, 0x04), Zero)))
                        {
                            Store (One, PRMV) /* \_SB_.PC00.PEG3.PRMV */
                        }
                    }

                    If (CondRefOf (\_SB.PC00.PEG3))
                    {
                        If (LNotEqual (\_SB.PC00.PEG3.VDID, 0xFFFFFFFF))
                        {
                            If (CondRefOf (\STD3))
                            {
                                If (CondRefOf (\_SB.PC00.PEG3.PRMV))
                                {
                                    If (LEqual (\_SB.PC00.PEG3.PRMV, One))
                                    {
                                        Store (\STD3, \_SB.PC00.PEG3.RD3C)
                                    }
                                }

                                If (LOr (\_SB.PC00.PEG3.PEGP.PNVM (), \_SB.PC00.PEG3.PEGP.PAHC ()))
                                {
                                    Store (\STD3, \_SB.PC00.PEG3.RD3C)
                                }
                            }
                        }
                    }
                }
            }

            Method (_PRT, 0, NotSerialized)  // _PRT: PCI Routing Table
            {
                If (PICM)
                {
                    Return (AR0B) /* External reference */
                }

                Return (PD0B) /* External reference */
            }

            Name (PRTP, 0x04)
            OperationRegion (PXCS, SystemMemory, \_SB.PC00.PC2M (_ADR ()), 0x0480)
            Field (PXCS, AnyAcc, NoLock, Preserve)
            {
                VDID,   32, 
                Offset (0x50), 
                L0SE,   1, 
                    ,   3, 
                LDIS,   1, 
                Offset (0x51), 
                Offset (0x52), 
                    ,   13, 
                LASX,   1, 
                Offset (0x5A), 
                ABPX,   1, 
                    ,   2, 
                PDCX,   1, 
                    ,   2, 
                PDSX,   1, 
                Offset (0x5B), 
                Offset (0x60), 
                Offset (0x62), 
                PSPX,   1, 
                Offset (0xA4), 
                D3HT,   2, 
                Offset (0xD8), 
                    ,   30, 
                HPEX,   1, 
                PMEX,   1, 
                Offset (0xE0), 
                SCB0,   1, 
                    ,   6, 
                NCB7,   1, 
                Offset (0xE2), 
                    ,   2, 
                L23E,   1, 
                L23R,   1, 
                Offset (0x324), 
                    ,   3, 
                LEDM,   1, 
                Offset (0x328), 
                Offset (0x32B), 
                LTSM,   8
            }

            Field (PXCS, AnyAcc, NoLock, WriteAsZeros)
            {
                Offset (0xDC), 
                    ,   30, 
                HPSX,   1, 
                PMSX,   1
            }

            Method (L23D, 0, Serialized)
            {
                If (LNotEqual (SCB0, One))
                {
                    Return (Zero)
                }

                Store (One, L23R) /* \_SB_.PC00.PEG3.L23R */
                Store (Zero, Local0)
                While (L23R)
                {
                    If (LGreater (Local0, 0x04))
                    {
                        Break
                    }

                    Sleep (0x10)
                    Increment (Local0)
                }

                Store (Zero, SCB0) /* \_SB_.PC00.PEG3.SCB0 */
                Store (Zero, Local0)
                While (LEqual (LASX, Zero))
                {
                    If (LGreater (Local0, 0x08))
                    {
                        Break
                    }

                    Sleep (0x10)
                    Increment (Local0)
                }
            }

            Method (DL23, 0, Serialized)
            {
                Store (One, L23E) /* \_SB_.PC00.PEG3.L23E */
                Sleep (0x10)
                Store (Zero, Local0)
                While (L23E)
                {
                    If (LGreater (Local0, 0x04))
                    {
                        Break
                    }

                    Sleep (0x10)
                    Increment (Local0)
                }

                Store (One, SCB0) /* \_SB_.PC00.PEG3.SCB0 */
            }

            Name (LTRV, Package (0x04)
            {
                Zero, 
                Zero, 
                Zero, 
                Zero
            })
            Name (PRMV, Zero)
            Name (RD3C, 0x02)
            Method (PRES, 0, NotSerialized)
            {
                If (LEqual (VDID, 0xFFFFFFFF))
                {
                    Return (Zero)
                }
                Else
                {
                    Return (One)
                }
            }

            Method (_DSM, 4, Serialized)  // _DSM: Device-Specific Method
            {
                If (LEqual (Arg0, ToUUID ("e5c937d0-3553-4d7a-9117-ea4d19c3434d") /* Device Labeling Interface */))
                {
                    Switch (ToInteger (Arg2))
                    {
                        Case (Zero)
                        {
                            Name (OPTS, Buffer (0x02)
                            {
                                 0x00, 0x00                                       // ..
                            })
                            CreateBitField (OPTS, Zero, FUN0)
                            CreateBitField (OPTS, 0x06, FUN6)
                            CreateBitField (OPTS, 0x08, FUN8)
                            CreateBitField (OPTS, 0x09, FUN9)
                            CreateBitField (OPTS, 0x0A, FUNA)
                            CreateBitField (OPTS, 0x0B, FUNB)
                            If (LGreaterEqual (Arg1, 0x02))
                            {
                                Store (One, FUN0) /* \_SB_.PC00.PEG3._DSM.FUN0 */
                                If (LNotEqual (LTEN, Zero))
                                {
                                    Store (One, FUN6) /* \_SB_.PC00.PEG3._DSM.FUN6 */
                                }

                                If (CondRefOf (ECR1))
                                {
                                    If (LEqual (ECR1, One))
                                    {
                                        If (LGreaterEqual (Arg1, 0x03))
                                        {
                                            Store (One, FUN8) /* \_SB_.PC00.PEG3._DSM.FUN8 */
                                            Store (One, FUN9) /* \_SB_.PC00.PEG3._DSM.FUN9 */
                                        }
                                    }
                                }
                            }

                            If (LGreaterEqual (Arg1, 0x04))
                            {
                                If (CondRefOf (PPBA))
                                {
                                    Store (One, FUNA) /* \_SB_.PC00.PEG3._DSM.FUNA */
                                }

                                If (CondRefOf (UPRD))
                                {
                                    Store (One, FUNB) /* \_SB_.PC00.PEG3._DSM.FUNB */
                                }
                            }

                            Return (OPTS) /* \_SB_.PC00.PEG3._DSM.OPTS */
                        }
                        Case (0x06)
                        {
                            If (LGreaterEqual (Arg1, 0x02))
                            {
                                Store (And (ShiftRight (LMSL, 0x0A), 0x07), Index (LTRV, Zero))
                                Store (And (LMSL, 0x03FF), Index (LTRV, One))
                                Store (And (ShiftRight (LNSL, 0x0A), 0x07), Index (LTRV, 0x02))
                                Store (And (LNSL, 0x03FF), Index (LTRV, 0x03))
                                Return (LTRV) /* \_SB_.PC00.PEG3.LTRV */
                            }
                        }
                        Case (0x08)
                        {
                            If (CondRefOf (ECR1))
                            {
                                If (LEqual (ECR1, One))
                                {
                                    If (LGreaterEqual (Arg1, 0x03))
                                    {
                                        Return (One)
                                    }
                                }
                            }
                        }
                        Case (0x09)
                        {
                            If (CondRefOf (ECR1))
                            {
                                If (LEqual (ECR1, One))
                                {
                                    If (LGreaterEqual (Arg1, 0x03))
                                    {
                                        Return (Package (0x05)
                                        {
                                            0xC350, 
                                            Ones, 
                                            Ones, 
                                            0xC350, 
                                            Ones
                                        })
                                    }
                                }
                            }
                        }
                        Case (0x0A)
                        {
                            If (CondRefOf (PPBA))
                            {
                                Return (PPBA (Arg3))
                            }
                        }
                        Case (0x0B)
                        {
                            If (CondRefOf (UPRD))
                            {
                                Return (UPRD (Arg3))
                            }
                        }

                    }
                }

                Return (Buffer (One)
                {
                     0x00                                             // .
                })
            }

            Method (_PRW, 0, NotSerialized)  // _PRW: Power Resources for Wake
            {
                Return (GPRW (0x69, 0x04))
            }

            Method (_PS0, 0, Serialized)  // _PS0: Power State 0
            {
                If (LEqual (HPEX, One))
                {
                    Store (Zero, HPEX) /* \_SB_.PC00.PEG3.HPEX */
                    Store (One, HPSX) /* \_SB_.PC00.PEG3.HPSX */
                }

                If (LEqual (PMEX, One))
                {
                    Store (Zero, PMEX) /* \_SB_.PC00.PEG3.PMEX */
                    Store (One, PMSX) /* \_SB_.PC00.PEG3.PMSX */
                }

                If (CondRefOf (PPS0))
                {
                    PPS0 ()
                }
            }

            Method (_PS3, 0, Serialized)  // _PS3: Power State 3
            {
                If (CondRefOf (PPS3))
                {
                    PPS3 ()
                }

                If (LEqual (HPEX, Zero))
                {
                    Store (One, HPEX) /* \_SB_.PC00.PEG3.HPEX */
                    Store (One, HPSX) /* \_SB_.PC00.PEG3.HPSX */
                }

                If (LEqual (PMEX, Zero))
                {
                    Store (One, PMEX) /* \_SB_.PC00.PEG3.PMEX */
                    Store (One, PMSX) /* \_SB_.PC00.PEG3.PMSX */
                }
            }

            Method (_DSD, 0, NotSerialized)  // _DSD: Device-Specific Data
            {
                If (CondRefOf (PINI))
                {
                    Return (Package (0x06)
                    {
                        ToUUID ("6211e2c0-58a3-4af3-90e1-927a4e0c55a4") /* Unknown UUID */, 
                        Package (0x01)
                        {
                            Package (0x02)
                            {
                                "HotPlugSupportInD3", 
                                One
                            }
                        }, 

                        ToUUID ("fdf06fad-f744-4451-bb64-ecd792215b10") /* Unknown UUID */, 
                        Package (0x01)
                        {
                            Package (0x02)
                            {
                                "FundamentalDeviceResetTriggeredOnD3ToD0", 
                                One
                            }
                        }, 

                        ToUUID ("6b4ad420-8fd3-4364-acf8-eb94876fd9eb") /* Unknown UUID */, 
                        Package (0x00){}
                    })
                }
                Else
                {
                    Return (Package (0x04)
                    {
                        ToUUID ("6211e2c0-58a3-4af3-90e1-927a4e0c55a4") /* Unknown UUID */, 
                        Package (0x01)
                        {
                            Package (0x02)
                            {
                                "HotPlugSupportInD3", 
                                One
                            }
                        }, 

                        ToUUID ("fdf06fad-f744-4451-bb64-ecd792215b10") /* Unknown UUID */, 
                        Package (0x01)
                        {
                            Package (0x02)
                            {
                                "FundamentalDeviceResetTriggeredOnD3ToD0", 
                                One
                            }
                        }
                    })
                }
            }

            Method (HPME, 0, Serialized)
            {
                If (LAnd (LNotEqual (VDID, 0xFFFFFFFF), LEqual (PMSX, One)))
                {
                    Store (One, PMSX) /* \_SB_.PC00.PEG3.PMSX */
                    Store (One, PSPX) /* \_SB_.PC00.PEG3.PSPX */
                    Return (One)
                }

                Return (Zero)
            }

            Method (HPEV, 0, Serialized)
            {
                If (LAnd (LNotEqual (VDID, 0xFFFFFFFF), HPSX))
                {
                    Store (One, HPSX) /* \_SB_.PC00.PEG3.HPSX */
                    If (LEqual (PDCX, One))
                    {
                        Store (One, PDCX) /* \_SB_.PC00.PEG3.PDCX */
                        If (LEqual (PDSX, Zero))
                        {
                            Store (Zero, L0SE) /* \_SB_.PC00.PEG3.L0SE */
                        }

                        Notify (^, Zero) // Bus Check
                    }
                }
            }
        }
    }

    Scope (\_SB.PC00.PEG0.PEGP)
    {
        OperationRegion (PCCX, PCI_Config, Zero, 0x10)
        Field (PCCX, ByteAcc, NoLock, Preserve)
        {
            DVID,   32, 
            Offset (0x09), 
            PIXX,   8, 
            SCCX,   8, 
            BCCX,   8
        }

        Method (PAHC, 0, Serialized)
        {
            If (LEqual (BCCX, One))
            {
                If (LEqual (SCCX, 0x06))
                {
                    If (LEqual (PIXX, One))
                    {
                        Return (One)
                    }
                }
            }

            Return (Zero)
        }

        Method (PNVM, 0, Serialized)
        {
            If (LEqual (BCCX, One))
            {
                If (LEqual (SCCX, 0x08))
                {
                    If (LEqual (PIXX, 0x02))
                    {
                        Return (One)
                    }
                }
            }

            Return (Zero)
        }

        Method (PRES, 0, Serialized)
        {
            If (LEqual (DVID, 0xFFFFFFFF))
            {
                Return (Zero)
            }
            Else
            {
                Return (One)
            }
        }

        Method (ISGX, 0, Serialized)
        {
            If (LEqual (BCCX, 0x03))
            {
                Return (One)
            }

            Return (Zero)
        }

        If (CondRefOf (\STD3))
        {
            If (LNotEqual (\STD3, Zero))
            {
                Method (_DSD, 0, NotSerialized)  // _DSD: Device-Specific Data
                {
                    If (LOr (PAHC (), PNVM ()))
                    {
                        Return (Package (0x02)
                        {
                            ToUUID ("5025030f-842f-4ab4-a561-99a5189762d0") /* Unknown UUID */, 
                            Package (0x01)
                            {
                                Package (0x02)
                                {
                                    "StorageD3Enable", 
                                    One
                                }
                            }
                        })
                    }
                    Else
                    {
                        Return (Package (0x02)
                        {
                            ToUUID ("5025030f-842f-4ab4-a561-99a5189762d0") /* Unknown UUID */, 
                            Package (0x01)
                            {
                                Package (0x02)
                                {
                                    "StorageD3Enable", 
                                    Zero
                                }
                            }
                        })
                    }
                }
            }
        }

        Method (_PRW, 0, NotSerialized)  // _PRW: Power Resources for Wake
        {
            Return (GPRW (0x69, 0x04))
        }
    }

    If (PBR1)
    {
        Scope (\_SB.PC00.PEG1.PEGP)
        {
            Method (_S0W, 0, NotSerialized)  // _S0W: S0 Device Wake State
            {
                Return (0x04)
            }

            Device (PEGD)
            {
                Method (_S0W, 0, NotSerialized)  // _S0W: S0 Device Wake State
                {
                    Return (0x04)
                }

                Name (_ADR, Zero)  // _ADR: Address
                Method (_PRW, 0, NotSerialized)  // _PRW: Power Resources for Wake
                {
                    Return (GPRW (0x69, 0x04))
                }
            }
        }
    }

    Scope (\_SB.PC00.PEG1.PEGP)
    {
        OperationRegion (PCCX, PCI_Config, Zero, 0x10)
        Field (PCCX, ByteAcc, NoLock, Preserve)
        {
            DVID,   32, 
            Offset (0x09), 
            PIXX,   8, 
            SCCX,   8, 
            BCCX,   8
        }

        Method (PAHC, 0, Serialized)
        {
            If (LEqual (BCCX, One))
            {
                If (LEqual (SCCX, 0x06))
                {
                    If (LEqual (PIXX, One))
                    {
                        Return (One)
                    }
                }
            }

            Return (Zero)
        }

        Method (PNVM, 0, Serialized)
        {
            If (LEqual (BCCX, One))
            {
                If (LEqual (SCCX, 0x08))
                {
                    If (LEqual (PIXX, 0x02))
                    {
                        Return (One)
                    }
                }
            }

            Return (Zero)
        }

        Method (PRES, 0, Serialized)
        {
            If (LEqual (DVID, 0xFFFFFFFF))
            {
                Return (Zero)
            }
            Else
            {
                Return (One)
            }
        }

        Method (ISGX, 0, Serialized)
        {
            If (LEqual (BCCX, 0x03))
            {
                Return (One)
            }

            Return (Zero)
        }

        If (CondRefOf (\STD3))
        {
            If (LNotEqual (\STD3, Zero))
            {
                Method (_DSD, 0, NotSerialized)  // _DSD: Device-Specific Data
                {
                    If (LOr (PAHC (), PNVM ()))
                    {
                        Return (Package (0x02)
                        {
                            ToUUID ("5025030f-842f-4ab4-a561-99a5189762d0") /* Unknown UUID */, 
                            Package (0x01)
                            {
                                Package (0x02)
                                {
                                    "StorageD3Enable", 
                                    One
                                }
                            }
                        })
                    }
                    Else
                    {
                        Return (Package (0x02)
                        {
                            ToUUID ("5025030f-842f-4ab4-a561-99a5189762d0") /* Unknown UUID */, 
                            Package (0x01)
                            {
                                Package (0x02)
                                {
                                    "StorageD3Enable", 
                                    Zero
                                }
                            }
                        })
                    }
                }
            }
        }

        Method (_PRW, 0, NotSerialized)  // _PRW: Power Resources for Wake
        {
            Return (GPRW (0x69, 0x04))
        }
    }

    If (PBR2)
    {
        Scope (\_SB.PC00.PEG2.PEGP)
        {
            Method (_S0W, 0, NotSerialized)  // _S0W: S0 Device Wake State
            {
                Return (0x04)
            }

            Device (PEGD)
            {
                Method (_S0W, 0, NotSerialized)  // _S0W: S0 Device Wake State
                {
                    Return (0x04)
                }

                Name (_ADR, Zero)  // _ADR: Address
                Method (_PRW, 0, NotSerialized)  // _PRW: Power Resources for Wake
                {
                    Return (GPRW (0x69, 0x04))
                }
            }
        }
    }

    Scope (\_SB.PC00.PEG2.PEGP)
    {
        OperationRegion (PCCX, PCI_Config, Zero, 0x10)
        Field (PCCX, ByteAcc, NoLock, Preserve)
        {
            DVID,   32, 
            Offset (0x09), 
            PIXX,   8, 
            SCCX,   8, 
            BCCX,   8
        }

        Method (PAHC, 0, Serialized)
        {
            If (LEqual (BCCX, One))
            {
                If (LEqual (SCCX, 0x06))
                {
                    If (LEqual (PIXX, One))
                    {
                        Return (One)
                    }
                }
            }

            Return (Zero)
        }

        Method (PNVM, 0, Serialized)
        {
            If (LEqual (BCCX, One))
            {
                If (LEqual (SCCX, 0x08))
                {
                    If (LEqual (PIXX, 0x02))
                    {
                        Return (One)
                    }
                }
            }

            Return (Zero)
        }

        Method (PRES, 0, Serialized)
        {
            If (LEqual (DVID, 0xFFFFFFFF))
            {
                Return (Zero)
            }
            Else
            {
                Return (One)
            }
        }

        Method (ISGX, 0, Serialized)
        {
            If (LEqual (BCCX, 0x03))
            {
                Return (One)
            }

            Return (Zero)
        }

        If (CondRefOf (\STD3))
        {
            If (LNotEqual (\STD3, Zero))
            {
                Method (_DSD, 0, NotSerialized)  // _DSD: Device-Specific Data
                {
                    If (LOr (PAHC (), PNVM ()))
                    {
                        Return (Package (0x02)
                        {
                            ToUUID ("5025030f-842f-4ab4-a561-99a5189762d0") /* Unknown UUID */, 
                            Package (0x01)
                            {
                                Package (0x02)
                                {
                                    "StorageD3Enable", 
                                    One
                                }
                            }
                        })
                    }
                    Else
                    {
                        Return (Package (0x02)
                        {
                            ToUUID ("5025030f-842f-4ab4-a561-99a5189762d0") /* Unknown UUID */, 
                            Package (0x01)
                            {
                                Package (0x02)
                                {
                                    "StorageD3Enable", 
                                    Zero
                                }
                            }
                        })
                    }
                }
            }
        }

        Method (_PRW, 0, NotSerialized)  // _PRW: Power Resources for Wake
        {
            Return (GPRW (0x69, 0x04))
        }
    }

    If (PBR3)
    {
        Scope (\_SB.PC00.PEG3.PEGP)
        {
            Method (_S0W, 0, NotSerialized)  // _S0W: S0 Device Wake State
            {
                Return (0x04)
            }

            Device (PEGD)
            {
                Method (_S0W, 0, NotSerialized)  // _S0W: S0 Device Wake State
                {
                    Return (0x04)
                }

                Name (_ADR, Zero)  // _ADR: Address
                Method (_PRW, 0, NotSerialized)  // _PRW: Power Resources for Wake
                {
                    Return (GPRW (0x69, 0x04))
                }
            }
        }
    }

    If (CondRefOf (\_SB.PC00.PEG3))
    {
        Scope (\_SB.PC00.PEG3.PEGP)
        {
            OperationRegion (PCCX, PCI_Config, Zero, 0x10)
            Field (PCCX, ByteAcc, NoLock, Preserve)
            {
                DVID,   32, 
                Offset (0x09), 
                PIXX,   8, 
                SCCX,   8, 
                BCCX,   8
            }

            Method (PAHC, 0, Serialized)
            {
                If (LEqual (BCCX, One))
                {
                    If (LEqual (SCCX, 0x06))
                    {
                        If (LEqual (PIXX, One))
                        {
                            Return (One)
                        }
                    }
                }

                Return (Zero)
            }

            Method (PNVM, 0, Serialized)
            {
                If (LEqual (BCCX, One))
                {
                    If (LEqual (SCCX, 0x08))
                    {
                        If (LEqual (PIXX, 0x02))
                        {
                            Return (One)
                        }
                    }
                }

                Return (Zero)
            }

            Method (PRES, 0, Serialized)
            {
                If (LEqual (DVID, 0xFFFFFFFF))
                {
                    Return (Zero)
                }
                Else
                {
                    Return (One)
                }
            }

            Method (ISGX, 0, Serialized)
            {
                If (LEqual (BCCX, 0x03))
                {
                    Return (One)
                }

                Return (Zero)
            }

            If (CondRefOf (\STD3))
            {
                If (LNotEqual (\STD3, Zero))
                {
                    Method (_DSD, 0, NotSerialized)  // _DSD: Device-Specific Data
                    {
                        If (LOr (PAHC (), PNVM ()))
                        {
                            Return (Package (0x02)
                            {
                                ToUUID ("5025030f-842f-4ab4-a561-99a5189762d0") /* Unknown UUID */, 
                                Package (0x01)
                                {
                                    Package (0x02)
                                    {
                                        "StorageD3Enable", 
                                        One
                                    }
                                }
                            })
                        }
                        Else
                        {
                            Return (Package (0x02)
                            {
                                ToUUID ("5025030f-842f-4ab4-a561-99a5189762d0") /* Unknown UUID */, 
                                Package (0x01)
                                {
                                    Package (0x02)
                                    {
                                        "StorageD3Enable", 
                                        Zero
                                    }
                                }
                            })
                        }
                    }
                }
            }

            Method (_PRW, 0, NotSerialized)  // _PRW: Power Resources for Wake
            {
                Return (GPRW (0x69, 0x04))
            }
        }
    }

    Scope (\_GPE)
    {
        Method (P0L6, 0, NotSerialized)
        {
            If (\_SB.ISME (P0WK))
            {
                \_SB.SHPO (P0WK, One)
                Notify (\_SB.PC00.PEG0, 0x02) // Device Wake
                \_SB.CAGS (P0WK)
            }
        }

        Method (P1L6, 0, NotSerialized)
        {
            If (\_SB.ISME (P1WK))
            {
                If (CondRefOf (\_SB.PC00.PEG1.DHDW))
                {
                    \_SB.PC00.PEG1.DHDW ()
                    Notify (\_SB.PC00.PEG1, Zero) // Bus Check
                }
                Else
                {
                    \_SB.SHPO (P1WK, One)
                    Notify (\_SB.PC00.PEG1, 0x02) // Device Wake
                }

                \_SB.CAGS (P1WK)
            }
        }

        Method (P2L6, 0, NotSerialized)
        {
            If (\_SB.ISME (P2WK))
            {
                \_SB.SHPO (P2WK, One)
                Notify (\_SB.PC00.PEG2, 0x02) // Device Wake
                \_SB.CAGS (P2WK)
            }
        }

        If (CondRefOf (\_SB.PC00.PEG3))
        {
            Method (P3L6, 0, NotSerialized)
            {
                If (\_SB.ISME (P3WK))
                {
                    \_SB.SHPO (P3WK, One)
                    Notify (\_SB.PC00.PEG3, 0x02) // Device Wake
                    \_SB.CAGS (P3WK)
                }
            }
        }
    }
}

