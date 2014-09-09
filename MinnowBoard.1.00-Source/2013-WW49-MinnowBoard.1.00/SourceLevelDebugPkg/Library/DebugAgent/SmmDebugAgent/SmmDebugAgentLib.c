/** @file
  Debug Agent library implementition.

  Copyright (c) 2010 - 2013, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php.

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "SmmDebugAgentLib.h"

DEBUG_AGENT_MAILBOX         *mMailboxPointer = NULL;
DEBUG_AGENT_MAILBOX         mLocalMailbox;
UINTN                       mSavedDebugRegisters[6];
IA32_IDT_GATE_DESCRIPTOR    mIdtEntryTable[33];
BOOLEAN                     mSkipBreakpoint = FALSE;

CHAR8 mWarningMsgIgnoreSmmEntryBreak[] = "Ignore smmentrybreak setting for SMI issued during DXE debugging!\r\n";

/**
  Check if debug agent support multi-processor.

  @retval TRUE    Multi-processor is supported.
  @retval FALSE   Multi-processor is not supported.

**/
BOOLEAN
MultiProcessorDebugSupport (
  VOID
  )
{
  return FALSE;
}

/**
  Read the Attach/Break-in symbols from the debug port.

  @param[in]  Handle         Pointer to Debug Port handle.
  @param[out] BreakSymbol    Returned break symbol.

  @retval EFI_SUCCESS        Read the symbol in BreakSymbol.
  @retval EFI_NOT_FOUND      No read the break symbol.

**/
EFI_STATUS
DebugReadBreakSymbol (
  IN  DEBUG_PORT_HANDLE      Handle,
  OUT UINT8                  *BreakSymbol
  )
{
  //
  // Smm instance has no debug timer to poll break symbol.
  //
  return EFI_NOT_FOUND;
}

/**
  Get the pointer to Mailbox from the GUIDed HOB.

  @return Pointer to Mailbox.

**/
DEBUG_AGENT_MAILBOX *
GetMailboxFromHob (
  VOID
  )
{
  EFI_HOB_GUID_TYPE        *GuidHob;
  UINT64                   *MailboxLocation;  
  DEBUG_AGENT_MAILBOX      *Mailbox;

  GuidHob = GetFirstGuidHob (&gEfiDebugAgentGuid);
  if (GuidHob == NULL) {
    return NULL;
  }
  MailboxLocation = (UINT64 *) (GET_GUID_HOB_DATA(GuidHob));
  Mailbox = (DEBUG_AGENT_MAILBOX *)(UINTN)(*MailboxLocation);
  VerifyMailboxChecksum (Mailbox);
  
  return Mailbox;
}

/**
  Get Debug Agent Mailbox pointer.

  @return Mailbox pointer.

**/
DEBUG_AGENT_MAILBOX *
GetMailboxPointer (
  VOID
  )
{
  VerifyMailboxChecksum (mMailboxPointer);
  return mMailboxPointer;
}

/**
  Get debug port handle.

  @return Debug port handle.

**/
DEBUG_PORT_HANDLE
GetDebugPortHandle (
  VOID
  )
{
  return (DEBUG_PORT_HANDLE) (UINTN)(GetMailboxPointer()->DebugPortHandle);
}

/**
  Store debug register when SMI exit.

**/
VOID
SaveDebugRegister (
  VOID
  )
{
  mSavedDebugRegisters[0] = AsmReadDr0 ();
  mSavedDebugRegisters[1] = AsmReadDr1 ();
  mSavedDebugRegisters[2] = AsmReadDr2 ();
  mSavedDebugRegisters[3] = AsmReadDr3 ();
  mSavedDebugRegisters[4] = AsmReadDr6 ();
  mSavedDebugRegisters[5] = AsmReadDr7 ();
}

/**
  Restore debug register when SMI exit.

**/
VOID
RestoreDebugRegister (
  VOID
  )
{
  AsmWriteDr7 (0);
  AsmWriteDr0 (mSavedDebugRegisters[0]);
  AsmWriteDr1 (mSavedDebugRegisters[1]);
  AsmWriteDr2 (mSavedDebugRegisters[2]);
  AsmWriteDr3 (mSavedDebugRegisters[3]);
  AsmWriteDr6 (mSavedDebugRegisters[4]);
  AsmWriteDr7 (mSavedDebugRegisters[5]);
}

/**
  Initialize debug agent.

  This function is used to set up debug enviroment for source level debug
  in SMM code.

  If InitFlag is DEBUG_AGENT_INIT_SMM, it will overirde IDT table entries
  and initialize debug port. It will get debug agent Mailbox from GUIDed HOB,
  it it exists, debug agent wiil copied it into the local Mailbox in SMM space.
  it will overirde IDT table entries and initialize debug port. Context will be
  NULL.
  If InitFlag is DEBUG_AGENT_INIT_ENTER_SMI, debug agent will save Debug
  Registers and get local Mailbox in SMM space. Context will be NULL.
  If InitFlag is DEBUG_AGENT_INIT_EXIT_SMI, debug agent will restore Debug
  Registers. Context will be NULL.

  @param[in] InitFlag     Init flag is used to decide initialize process.
  @param[in] Context      Context needed according to InitFlag.
  @param[in] Function     Continue function called by debug agent library; it was
                          optional.

**/
VOID
EFIAPI
InitializeDebugAgent (
  IN UINT32                InitFlag,
  IN VOID                  *Context, OPTIONAL
  IN DEBUG_AGENT_CONTINUE  Function  OPTIONAL
  )
{
  EFI_STATUS                    Status;
  UINT64                        DebugPortHandle;
  IA32_IDT_GATE_DESCRIPTOR      IdtEntry[33];
  IA32_DESCRIPTOR               IdtDescriptor;
  IA32_DESCRIPTOR               *Ia32Idtr;
  IA32_IDT_ENTRY                *Ia32IdtEntry;
  IA32_DESCRIPTOR               Idtr;
  UINT16                        IdtEntryCount;
  DEBUG_AGENT_MAILBOX           *Mailbox;
  UINT64                        *MailboxLocation;

  switch (InitFlag) {
  case DEBUG_AGENT_INIT_SMM:
    //
    // Check if Debug Agent initialized in DXE phase
    //
    Status = EfiGetSystemConfigurationTable (&gEfiDebugAgentGuid, (VOID **) &Mailbox);
    if (Status == EFI_SUCCESS && Mailbox != NULL) {
      VerifyMailboxChecksum (Mailbox);
      mMailboxPointer = Mailbox;
      break;
    }
    //
    // Check if Debug Agent initialized in SEC/PEI phase
    //
    Mailbox = GetMailboxFromHob (); 
    if (Mailbox != NULL) {
      mMailboxPointer = Mailbox;
      break;
    }
    //
    // Debug Agent was not initialized before, uset the local mailbox.
    //
    ZeroMem (&mLocalMailbox, sizeof (DEBUG_AGENT_MAILBOX));
    Mailbox = &mLocalMailbox;
    //
    // Save original IDT entries
    //
    AsmReadIdtr (&IdtDescriptor);      
    CopyMem (&IdtEntry, (VOID *)IdtDescriptor.Base, 33 * sizeof(IA32_IDT_GATE_DESCRIPTOR));
    //
    // Initialized Debug Agent
    //
    InitializeDebugIdt ();
    DebugPortHandle = (UINT64) (UINTN)DebugPortInitialize ((DEBUG_PORT_HANDLE) (UINTN)Mailbox->DebugPortHandle, NULL);
    UpdateMailboxContent (Mailbox, DEBUG_MAILBOX_DEBUG_PORT_HANDLE_INDEX, DebugPortHandle);
    mMailboxPointer = Mailbox;
    //
    // Trigger one software interrupt to inform HOST
    //
    TriggerSoftInterrupt (SYSTEM_RESET_SIGNATURE);

    SetDebugFlag (DEBUG_AGENT_FLAG_MEMORY_READY, 1);
    //
    // Memory has been ready
    //
    if (IsHostAttached ()) {
      //
      // Trigger one software interrupt to inform HOST
      //
      TriggerSoftInterrupt (MEMORY_READY_SIGNATURE);
    }
    //
    // Find and report PE/COFF image info to HOST
    //  
    FindAndReportModuleImageInfo (SIZE_4KB);
    //
    // Restore saved IDT entries
    //     
    CopyMem ((VOID *)IdtDescriptor.Base, &IdtEntry, 33 * sizeof(IA32_IDT_GATE_DESCRIPTOR));

    break;

  case DEBUG_AGENT_INIT_ENTER_SMI:
    SaveDebugRegister ();
    InitializeDebugIdt ();

    Mailbox = GetMailboxPointer ();
    if (GetDebugFlag (DEBUG_AGENT_FLAG_AGENT_IN_PROGRESS) == 1) {
      //
      // If Debug Agent has been communicaton state with HOST, we need skip
      // any break points set in SMM, set Skip Breakpoint flag
      //
      mSkipBreakpoint = TRUE;
    }
    if (GetDebugFlag (DEBUG_AGENT_FLAG_BREAK_ON_NEXT_SMI) == 1) {
      if (mSkipBreakpoint) {
        //
        // Print warning message if ignore smm entry break
        //
        DebugPortWriteBuffer ((DEBUG_PORT_HANDLE) (UINTN)Mailbox->DebugPortHandle,
                               (UINT8 *)mWarningMsgIgnoreSmmEntryBreak,
                               AsciiStrLen (mWarningMsgIgnoreSmmEntryBreak)
                               );
      } else {
        //
        // If SMM entry break is set, SMM code will be break at here.
        //
        CpuBreakpoint ();
      }
    }
    break;

  case DEBUG_AGENT_INIT_EXIT_SMI:
    Mailbox = GetMailboxPointer ();
    //
    // Clear Skip Breakpoint flag
    //
    mSkipBreakpoint = FALSE;
    RestoreDebugRegister ();
    break;

  case DEBUG_AGENT_INIT_THUNK_PEI_IA32TOX64:
    if (Context == NULL) {
      DEBUG ((EFI_D_ERROR, "DebugAgent: Input parameter Context cannot be NULL!\n"));
      CpuDeadLoop ();
    } else {
      Ia32Idtr =  (IA32_DESCRIPTOR *) Context;
      Ia32IdtEntry = (IA32_IDT_ENTRY *)(Ia32Idtr->Base);
      MailboxLocation = (UINT64 *) (UINTN) (Ia32IdtEntry[DEBUG_MAILBOX_VECTOR].Bits.OffsetLow + 
                                           (Ia32IdtEntry[DEBUG_MAILBOX_VECTOR].Bits.OffsetHigh << 16));
      mMailboxPointer = (DEBUG_AGENT_MAILBOX *)(UINTN)(*MailboxLocation);
      VerifyMailboxChecksum (mMailboxPointer);
      //
      // Get original IDT address and size.
      //
      AsmReadIdtr ((IA32_DESCRIPTOR *) &Idtr);
      IdtEntryCount = (UINT16) ((Idtr.Limit + 1) / sizeof (IA32_IDT_GATE_DESCRIPTOR));
      if (IdtEntryCount < 33) {
        Idtr.Limit = (UINT16) (sizeof (IA32_IDT_GATE_DESCRIPTOR) * 33 - 1);
        Idtr.Base  = (UINTN) &mIdtEntryTable;
        ZeroMem (&mIdtEntryTable, Idtr.Limit + 1);
        AsmWriteIdtr ((IA32_DESCRIPTOR *) &Idtr);
      }

      InitializeDebugIdt ();
      //
      // Initialize Debug Timer hardware and enable interrupt.
      //
      InitializeDebugTimer ();
      EnableInterrupts ();

      FindAndReportModuleImageInfo (SIZE_4KB);
    }
    break;

  default:
    //
    // Only DEBUG_AGENT_INIT_PREMEM_SEC and DEBUG_AGENT_INIT_POSTMEM_SEC are allowed for this 
    // Debug Agent library instance.
    //
    DEBUG ((EFI_D_ERROR, "Debug Agent: The InitFlag value is not allowed!\n"));
    CpuDeadLoop ();
    break;    
  }
}
